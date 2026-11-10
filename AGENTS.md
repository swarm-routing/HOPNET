# AGENTS.md — guide for AI coding agents

Operational guide for LLM agents working in `swarm-routing/hopnet`. Read this
before changing anything. Org-wide conventions:
[`swarm-routing/.github` CONTRIBUTING.md](https://github.com/swarm-routing/.github/blob/main/CONTRIBUTING.md). Shape and rules mirror
[AntHocNet's AGENTS.md](https://github.com/danieljoppi/AntHocNet/blob/main/AGENTS.md), the source repository.

## What this repo is

**Status: `core/` in progress (#5; started by the maintainer's decision of 2026-11-10): the intra-zone half is written; the inter-zone half, the ns-3 module and the gates are not.** `docs/fidelity.md` (#4) and the accepted pre-registration on #5 govern every reading. Directories marked (planned) do not exist yet: update this file in the PR that creates each.

HOPNET (Wang, 2007 MSc thesis, University of Manitoba) as a standalone hexagonal protocol: ant-colony routing combined with zone routing, where ants hop between routing zones (proactive inside a zone, reactive between zones). It is the one published result found that benchmarks against AntHocNet directly, claiming better scaling. A simulator-agnostic `core/` and an ns-3 `ns3/` adapter (contrib module `hopnet`) linking against `swarm-core`. There is no AntHocNet arm to promote: it is written from the thesis.

```
core/       simulator-agnostic algorithm + unit tests (no ns-3 headers; MIT OR GPL-2.0-only, core/LICENSE):
            hopnet-config.h (parameters, each citing its fidelity row), hopnet-ant (the ant header + wire codec),
            hopnet-intrazone-table (IntraRT), hopnet-intrazone (internal ants, eqs. 4.1-4.3, neighbours, selection);
            test/ (ctest); swarm-core/ (swarm-core's primitives vendored at swarm-core/PIN, never edited here)
ns3/        (planned) contrib module `hopnet` (GPL-2.0-only): adapter, helper, examples, test suite
docs/       fidelity sheet (thesis section -> implementation), parameters with sources and the unknowns
```

## Build & verify

CI today is `.github/workflows/core.yml` (`core-test`: `core/` with strict warnings as errors and ctest, again under ASan + UBSan, and the vendored swarm-core files diffed against `core/swarm-core/PIN`; `core-format`: clang-format-18 with the org style), plus the licence, org-sync and PR-title checks. [`reusable-ns3-ci.yml`](https://github.com/swarm-routing/.github/blob/main/.github/workflows/reusable-ns3-ci.yml) joins once `ns3/` exists (as in ARA and BeeAdHoc), with `dependencies: swarm-core=swarm-routing/ns3-swarm-core@main`. Locally:

```bash
make core-test   # core/ alone: cmake -S core -B build/core && cmake --build build/core && ctest --test-dir build/core
cmake -S core -B build/asan -DHOPNET_CORE_SANITIZE=ON && cmake --build build/asan && ctest --test-dir build/asan
make help    # the org's standard target names: build, test, lint, format, check, clean, shell, ci
make check   # everything CI gates on
docker run --rm -v "$PWD:/src/hopnet:ro" -v "$PWD/../ns3-swarm-core:/src/ns3-swarm-core:ro" \
    ghcr.io/swarm-routing/ns3-dev:latest-debug \
    ns3-build-contrib --test swarm-core=/src/ns3-swarm-core hopnet=/src/hopnet/ns3
```

## Golden rules (invariants — do not break)

Org-wide (1–9), then this repo's own.

<!-- org-sync:begin org-rules -->
1. **Licensing per directory, as AntHocNet** ([`LICENSING.md`](LICENSING.md);
   AntHocNet [LICENSING.md](https://github.com/danieljoppi/AntHocNet/blob/main/LICENSING.md),
   [ADR-0028](https://github.com/danieljoppi/AntHocNet/blob/main/docs/adr/0028-core-is-dual-licensed.md)). A
   simulator-free `core/` and the standalone tooling are `MIT OR GPL-2.0-only`;
   ns-3 modules, the site, docs and data are `GPL-2.0-only`. Every original
   `.h`/`.hpp`/`.cc`/`.cpp`/`.py`/`.sh` starts with
   `// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)` +
   `// SPDX-License-Identifier: <MIT OR GPL-2.0-only | GPL-2.0-only>` (`#`
   comments where needed), matching its directory in `LICENSING.md`. CI
   (`reusable-licence-check.yml`) checks it. A new directory gets its row in
   `LICENSING.md` in the same PR.
2. **The licence boundary only goes one way.** GPL code may use the dual
   parts; dual code never pulls GPL-only code in: a `core/` file includes only
   the C++ standard library and other `core/` files (CI checks it), and a dual
   script imports no GPL-only code. Code coming from AntHocNet keeps its
   licence: its `core/`/`tools/` (dual) may land in dual or GPL paths here; its
   `ns3/` modules and `web/` (GPL-2.0-only) only in GPL paths. Third-party code
   (AntHocNet's vendored `aomdv`/`gpsr`, IITP RAS) lands only in GPL paths with
   its provenance recorded. Write clean implementations against public ns-3
   interfaces rather than copying ns-3's `src/aodv`/`src/olsr`.
3. **Wire formats are POD + versioned.** Control headers are
   simulator-independent POD structs with bounds-checked little-endian
   readers/writers, a leading version byte, and deterministic roundtrip + fuzz
   tests. Changing a field or its units bumps the version byte.
4. **No contrib module named `core`** (collides with ns-3's `src/core`). The
   shared module is `swarm-core` (`#include "ns3/swarm-core-module.h"`).
5. **Determinism.** A fixed `--seed=<N>` gives byte-identical output. All
   randomness through the simulator's seeded streams or an injected RNG port;
   never libc `rand()`, wall-clock time, or unordered-container iteration order
   in anything that reaches an output.
6. **Comparisons are paired.** Per-seed deltas across >= 20 seeds, emitting the
   canonical `##CONFIG##`, `##RUN##`, `##BENCH##`, `##DIAG##` lines. A
   comparator arm (a protocol we did not design) counts only after its three
   gates: a fidelity sheet from its paper, the per-PR delivery smoke, and an
   anchor reproducing the paper's own trend.
7. **Keep bounded structures bounded** (visited paths, dedup histories, queues
   carry a configured cap).
8. **CI hygiene.** Actions pinned by full commit SHA with a `# vX.Y.Z` comment;
   buildx caches scoped per profile; transient artifacts `retention-days` <= 7.
9. **Nothing shipped depends on `.claude/`.** Skills hold procedures; scripts
   that anything shipped needs live in `tools/` (AntHocNet ADR-0014).
<!-- org-sync:end org-rules -->
10. **`core/` never includes a simulator header**; the adapter only converts headers, executes `RouteDecision`s and owns timers/queues (AntHocNet golden rules 1–2).
11. **Fidelity sheet before code.** Every parameter cites its thesis section or is marked a repo choice / unknown (zone radius, ant rates, the AntHocNet configuration the thesis compared against); deviations live in `docs/fidelity.md`.
12. **The anchor is against AntHocNet.** The thesis's headline trend is HOPNET vs AntHocNet, so its anchor run pairs the two through `swarm-routing/ns3-tools`; the AntHocNet configuration used is pinned and stated. If the thesis leaves it unrecoverable, the arm ships as a written infeasibility verdict.
13. **Written fresh.** No AntHocNet code exists for HOPNET; `core/` (dual) is original work, and anything reused from another protocol keeps that protocol's licence and provenance. The thesis stays in the private `swarm-routing/papers` repo if not openly redistributable.

## Conventions

<!-- org-sync:begin conventions -->
- Issues follow the org taxonomy ([CONTRIBUTING.md](https://github.com/swarm-routing/.github/blob/main/CONTRIBUTING.md#issues), from
  [AntHocNet ADR-0013](https://github.com/danieljoppi/AntHocNet/blob/main/docs/adr/0013-track-bugs-and-findings-as-issues.md)): one type label
  (`bug`/`enhancement`/`chore`/`documentation`/`verification`, plus `epic` for
  umbrellas), area label(s) (`protocol`/`adapter`/`ns3`/`benchmark`/
  `observability`/`packaging`/`learn-site`/`literature`), one `model:*`
  recommendation, one `priority:P1|P2|P3` on non-epic issues, a
  `release:vX.Y.Z` label only when a roadmap gates the issue. Labels come from
  `labels.json` via the `sync-labels` workflow.
- Issue bodies: **Context** (evidence, numbers, links) · **Scope** ·
  **Acceptance criteria** (checkboxes) · **Depends on** (cross-repo form
  `swarm-routing/<repo>#N`) · **References**. Children of an epic say "Part of
  #N" and are linked as sub-issues. Comments you post end with the Claude Code
  footer.
- Issues migrated from AntHocNet carry `migrated` and start with *Migrated from
  `danieljoppi/AntHocNet#N`*.
- Epics are listed where contributors look. In a public repository, this
  README's `## Epics` section and the org
  [ROADMAP](https://github.com/swarm-routing/.github/blob/main/ROADMAP.md#epics)
  are generated from the issues labelled `epic` by `swarm-routing/.github`'s
  `tools/epics/epics.py`. An open epic is Planned (no sub-issue closed) or In
  progress (some closed); it becomes Done or Dropped only when you close the
  epic itself (as completed or as not planned), even if every sub-issue is
  closed. Whenever you change an epic (open, close, rename or re-scope it, add
  or remove its `epic` label) or one of its sub-issues (close, reopen, add,
  unlink), refresh the lists in that PR or the next one. You need
  `swarm-routing/.github` checked out next to this repository and `gh` logged
  in (or `GH_TOKEN` set). Run `python3 ../.github/tools/epics/epics.py readme .`
  in the repository that owns the epic (for a sub-issue of another
  repository's epic, that repository, not this one), then
  `python3 tools/epics/epics.py roadmap ROADMAP.md` and
  `python3 tools/epics/epics.py profile profile/README.md` in `.github`. Never
  edit a generated section by hand; the weekly `epics` workflow in `.github`
  only reports drift. Private repositories (such as `papers`) are never listed
  and keep no generated section.
- Make minimal, reviewable changes; update the relevant doc/ADR when you change
  a documented decision.
<!-- org-sync:end conventions -->

## Git workflow

<!-- org-sync:begin git-workflow -->
- Branch from `main` (`claude/<short-topic>`), one focused change per PR.
- **Always open a PR to `main`** once the branch is pushed — every change is
  tracked through one.
- **PRs are squash-merged** (the only merge method enabled). The PR title is
  the commit on `main`, so it is a lowercase Conventional Commit
  (`feat:`/`fix:`/`docs:`/`chore:`/`ci:`…, optional scope).
- Push with `git push -u origin <branch>`; merge when CI is green.
<!-- org-sync:end git-workflow -->

<!-- org-sync:begin skills -->
## Agent skills

The org's procedures ship as the Claude Code plugin **`swarm-routing`**, from
[`swarm-routing/skills`](https://github.com/swarm-routing/skills). This
repository's `.claude/settings.json` enables it, so Claude Code offers a skill
when the task matches its description. Other agents (Codex, Cursor, Copilot, …)
install the same skills with `npx skills add swarm-routing/skills`
([skills.sh](https://skills.sh)). A skill is the procedure; the script does the
work and runs without it (golden rule 9), so an agent without the skill starts
from the script; so does a session that does not read this repository's
settings (some hosted sessions with several repositories do not). If Claude
Code says the plugin is missing (it was called
`org-repo` before 2026-11-09, and a cached marketplace keeps the old list), run
`claude plugin marketplace update swarm-routing` and start a new session.

| Task | Script (any agent) | Skill |
|------|--------------------|-------|
| Bring this repository onto the org templates, or fix their drift | `swarm-routing/.github` `tools/org-sync/org_sync.py` | `org-repo` |
| Write or review a protocol's paper-fidelity sheet (`docs/fidelity.md`) | `swarm-routing/.github` `tools/fidelity/fidelity.py` | `fidelity-sheet` |
| Check a diff against the org golden rules before a PR or a review | `swarm-routing/.github` `tools/protocol-review/check_invariants.py` | `protocol-review` |
| Bring the README, AGENTS/CLAUDE/CONTEXT and docs/ to the org documentation standard | `swarm-routing/.github` `tools/docs-review/check_docs.py` | `docs-review` |
| Find new papers on a topic, or confirm a reference (Crossref, Semantic Scholar, OpenAlex; free sources only) | `swarm-routing/.github` `tools/lit-search/lit_search.py` | `lit-search` |

A skill only this repository uses lives in its own `.claude/skills/`; a skill
two repositories need moves to the plugin. Skills that file, cite and extract
papers into the private papers library stay in `swarm-routing/papers`; finding
new papers and confirming a reference is public (`lit-search`). A public skill
may point to the papers skills, never carry library content. This section is managed by org-sync: add
a row in `swarm-routing/.github` `templates/agents/skills.md`, not here (an edit
here is undone by the next `org_sync.py apply`).
<!-- org-sync:end skills -->

## Where to look

| You want to… | Go to |
|--------------|-------|
| The org's invariants, labels, issue shape | [`swarm-routing/.github` CONTRIBUTING.md](https://github.com/swarm-routing/.github/blob/main/CONTRIBUTING.md) |
| The plan | swarm-routing/.github#52, org ROADMAP "Protocol repositories" |
| The source | Wang, *HOPNET* (2007 thesis), [UManitoba MSpace](https://mspace.lib.umanitoba.ca/bitstream/1993/20935/1/Wang_HOPNET_a.pdf); AntHocNet `docs/research-landscape-2026.md` (Other swarm protocols) |
| Shared primitives | `swarm-routing/ns3-swarm-core`; vendored into `core/swarm-core/` at `PIN` (its README: how to move the pin) |
| The core's readings and parameters | `docs/fidelity.md`, the accepted pre-registration on #5, `core/hopnet-config.h` |
| Benchmark against AntHocNet, ARA, AODV, OLSR | `swarm-routing/ns3-tools` arenas, results into `swarm-routing/benchmarks` |
| Record an architecture decision | [`docs/adr/`](docs/adr/) (`0000-template.md`) |
| Pick up open work | this repo's issues — highest open `priority:P*` first; newest open `handoff` issue for work in flight |
