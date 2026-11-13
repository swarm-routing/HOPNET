# Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
# SPDX-License-Identifier: GPL-2.0-only
#
# HOPNET: the org's standard targets (swarm-routing/.github#11). Every org
# repo has these same target names, with this repo's own commands behind them;
# `org-sync check` verifies the names are present, never the recipes. Add
# repo-only targets at the end. `make help` lists everything with a `## ` note.

NS3_IMAGE ?= ghcr.io/swarm-routing/ns3-dev:latest-debug
PLATFORM  ?=
# container kind: the image's Dockerfile and its build arguments
DOCKERFILE ?= Dockerfile
BUILD_ARGS ?=
# The org tooling (check-headers.py, org-sync): a shallow clone, refreshed to ORG_REF.
ORG_DIR   ?= $(HOME)/.cache/swarm-routing/dotgithub
ORG_REF   ?= main
# swarm-core's core/ at the commit core/CMakeLists.txt pins (SWARM_CORE_REF), for the include check.
SWARM_CORE_SRC ?= $(HOME)/.cache/swarm-routing/ns3-swarm-core
SWARM_CORE_REF ?= 0919dbfe4a48495a2eda32335a1be6fae453c87c

.DEFAULT_GOAL := help
.PHONY: help build test lint format check clean shell ci headers org-sync-check org-tools core-test swarm-core dist

help: ## list the targets
	@awk 'BEGIN{FS=":.*## "} /^[a-zA-Z0-9_-]+:.*## /{printf "  %-16s %s\n", $$1, $$2}' $(MAKEFILE_LIST)

build: ## build what this repo ships
	if [ -d ns3 ]; then docker run --rm -v "$(CURDIR):/src:ro" $(NS3_IMAGE) ns3-build-contrib hopnet=/src/ns3; else echo "make build: no ns3/ module yet"; fi

test: core-test ## the fast tests CI runs on every PR
	if [ -d ns3 ]; then docker run --rm -v "$(CURDIR):/src:ro" $(NS3_IMAGE) ns3-build-contrib --test hopnet=/src/ns3; else echo "make test: no ns3/ module yet"; fi

# The org .clang-format unless this repo ships its own, as CI's format check.
CLANG_STYLE = $(if $(wildcard .clang-format),file,file:$(ORG_DIR)/.clang-format)

lint: org-tools ## static checks, no changes
	git ls-files "*.h" "*.cc" "*.cpp" "*.hpp" | xargs -r clang-format-18 --style=$(CLANG_STYLE) --dry-run --Werror

format: org-tools ## apply the formatters
	git ls-files "*.h" "*.cc" "*.cpp" "*.hpp" | xargs -r clang-format-18 --style=$(CLANG_STYLE) -i

clean: ## remove build outputs
	rm -rf build dist

shell: ## an interactive shell in the dev image with this repo at /src
	@command -v docker >/dev/null || { echo "make shell: docker not found (Docker Desktop, Colima, or docker.io)" >&2; exit 127; }
	docker run --rm -it -v "$(CURDIR):/src" -w /src $(if $(PLATFORM),--platform $(PLATFORM)) $(NS3_IMAGE) bash

org-tools:  # the same templates CI checks against: refreshed to ORG_REF every time
	@[ -d "$(ORG_DIR)/.git" ] || { git init -q "$(ORG_DIR)" && git -C "$(ORG_DIR)" remote add origin https://github.com/swarm-routing/.github; }
	@git -C "$(ORG_DIR)" fetch -q --depth 1 origin $(ORG_REF) && git -C "$(ORG_DIR)" checkout -q --detach FETCH_HEAD

swarm-core:  # swarm-core at SWARM_CORE_REF, for the include check
	@[ -d "$(SWARM_CORE_SRC)/.git" ] || git init -q "$(SWARM_CORE_SRC)"
	@git -C "$(SWARM_CORE_SRC)" fetch -q --depth 1 https://github.com/swarm-routing/ns3-swarm-core $(SWARM_CORE_REF) && git -C "$(SWARM_CORE_SRC)" checkout -q --detach FETCH_HEAD

headers: org-tools swarm-core ## licence headers (org rule 1)
	python3 -I "$(ORG_DIR)/tools/checks/check-headers.py" --dual core/ --core core/ --core-dep "$(SWARM_CORE_SRC)/core" .

org-sync-check: org-tools ## org-managed files match the org templates
	python3 -I "$(ORG_DIR)/tools/org-sync/org_sync.py" check .

check: lint test headers org-sync-check ## everything CI gates on

ci: check ## exactly what CI runs

core-test: ## core/ alone, no ns-3: CMake + ctest with CI's strict warnings
	cmake -S core -B build/core && cmake --build build/core && ctest --test-dir build/core --output-on-failure

# The release asset for swarm-routing/.github's reusable-release.yml, which sets
# RELEASE_VERSION (locally: .cz.toml's version). A git archive of the commit, so
# only tracked files, deterministically: hopnet-<ver>/ with
# core/ with its LICENSE, docs/ (and ns3/ once it exists),
# the licence files, README.md, AGENTS.md, the Makefile and CHANGELOG.md (once
# the first bump writes it).
RELEASE_VERSION ?= $(shell sed -n 's/^version = "\(.*\)"$$/\1/p' .cz.toml)
dist: ## the release asset in dist/: hopnet-<ver>.tar.gz
	rm -rf dist && mkdir -p dist
	git archive --format=tar.gz --prefix=hopnet-$(RELEASE_VERSION)/ -o dist/hopnet-$(RELEASE_VERSION).tar.gz HEAD \
		$$(git ls-tree --name-only HEAD core ns3 docs LICENSE LICENSE-MIT LICENSING.md README.md AGENTS.md Makefile CHANGELOG.md)
	tar -tzf dist/hopnet-$(RELEASE_VERSION).tar.gz | grep -qx 'hopnet-$(RELEASE_VERSION)/core/LICENSE'
