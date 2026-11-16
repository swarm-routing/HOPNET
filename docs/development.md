# Developer guide

This repository implements HOPNET (Wang, 2007 MSc thesis), ant-colony routing combined with zone routing: proactive inside a zone, reactive between zones, as two parts: a simulator-free
`core/` (the algorithm, plain C++23, with its own unit tests) and the ns-3
contrib module `hopnet` in `ns3/`, an adapter that runs the core inside ns-3 on
swarm-routing/ns3-swarm-core's `SwarmRoutingBase`. This page shows how to
set up, build, test, run and lint both, and what CI checks on a pull request.
The rules a change must keep are in [AGENTS.md](../AGENTS.md) ("Golden
rules"); what the implementation follows from the paper is
the fidelity sheet [fidelity.md](fidelity.md) and the anchor verdict [anchor-verdict.md](anchor-verdict.md); how issues and pull requests work is in the org's
[CONTRIBUTING.md](https://github.com/swarm-routing/.github/blob/main/CONTRIBUTING.md).

## Prerequisites

| Tool | Version | Needed for |
| --- | --- | --- |
| Linux or macOS, `git`, `make`, `curl` | any recent | everything |
| C++ compiler with C++23 | GCC 13 or Clang 17 or newer (CI: GCC 13.3 on Ubuntu 24.04) | `core/` alone |
| CMake | 3.20 or newer (CI: 3.28) | `core/` alone |
| Docker | any recent; about 4 GB free for the debug image, 6 GB more for the sanitized one | the ns-3 module |
| clang-format | 18 (`clang-format-18`) | formatting |
| Python | 3.11 or newer | licence headers, org-sync, the benchmark scripts in `tools/bench/` |

You do not install ns-3: the module builds inside the org's prebuilt image
`ghcr.io/swarm-routing/ns3-dev` (ns-3 3.49 today, GCC 13.3, CMake 3.28),
pulled on first use. `core/` needs network access on its first configure,
to download swarm-core (see "Dependencies").

## Get the code

The ns-3 build also needs swarm-routing/ns3-swarm-core checked out next to
this repository, at the release `swarm-core.lock` names (the tag CI builds
on):

```bash
git clone https://github.com/swarm-routing/HOPNET.git
git clone --branch "$(sed -n 's/^SWARM_CORE_TAG=//p' HOPNET/swarm-core.lock)" \
    https://github.com/swarm-routing/ns3-swarm-core.git
cd HOPNET
```

To run the comparison harness as well (see "Run"), clone
swarm-routing/ns3-tools next to them too, at the release `ns3.yml` names
(`git clone --branch v1.0.0 https://github.com/swarm-routing/ns3-tools.git`
today). There are no submodules.

## Dependencies

- **swarm-core's `core/`** (the shared primitives): `swarm-core.lock` names
  one swarm-core release, and `core/CMakeLists.txt` downloads its tarball by
  URL and checks its SHA256. To work against a local, unreleased swarm-core,
  configure with `-DSWARM_CORE_DIR=/path/to/ns3-swarm-core/core`. Nobody
  edits the lock by hand: the `swarm-core / pin` check fails when a newer
  release is out, and the org bot opens the bump PR.
- **ns-3**: from the `ns3-dev` image. The ns-3 module also needs the
  swarm-core ns-3 module (the checkout above) and, for the harness arm,
  ns3-tools' `routing-bench`; CI fetches both at release tags
  (`dependencies:` in `.github/workflows/ns3.yml`).
- A vcpkg-based setup is planned (swarm-routing/.github#489); the
  convention today is CONTRIBUTING "Dependencies".

## Build

`core/` alone, with the same strict warnings as CI:

```bash
cmake -S core -B build/core -DCMAKE_BUILD_TYPE=Debug
cmake --build build/core -j
```

The ns-3 module, with swarm-core's, inside the `ns3-dev` image (both
checkouts mounted read-only; only the contrib modules compile):

```bash
make build    # uses ../ns3-swarm-core; SWARM_CORE_NS3=/path/to/checkout to change it
# the same command written out:
docker run --rm -v "$PWD:/src/hopnet:ro" -v "$PWD/../ns3-swarm-core:/src/ns3-swarm-core:ro" \
    ghcr.io/swarm-routing/ns3-dev:latest-debug \
    ns3-build-contrib --strict-warnings --quality-modules hopnet swarm-core=/src/ns3-swarm-core hopnet=/src/hopnet/ns3
```

## Test

| What | Command | CI |
| --- | --- | --- |
| `core/` unit tests | `make core-test` | `core-test` / `core-format` (core.yml) |
| `core/` under ASan + UBSan | `cmake -S core -B build/sanitized -DCMAKE_BUILD_TYPE=Debug -DHOPNET_CORE_SANITIZE=ON && cmake --build build/sanitized -j && ctest --test-dir build/sanitized --output-on-failure` | `core-test` / `core-format` |
| `core/` tests, then the ns-3 suites (`test.py -s hopnet` and swarm-core's), strict warnings as errors | `make test` | `ci / ns-latest debug` |
| the same on the sanitized image | `make test NS3_IMAGE=ghcr.io/swarm-routing/ns3-dev:latest-sanitized` | `ci / ns-latest sanitized` |

A passing suite prints `1 of 1 tests passed` for each module. CI also runs,
on the image only: the delivery smoke (`routing-compare ... --protocols=hopnet`,
see "Run"), the determinism gate (`hopnet-example --seed=N` twice, byte-identical), coverage for Codecov (report-only), and,
weekly and on `main`, a canary leg that builds on swarm-core's and
ns3-tools' `main` instead of their releases (it never fails the run).

## Run

The module's example, a small mobile network (`--seed` picks the run; the
same seed gives byte-identical output, which CI checks):

```bash
docker run --rm -v "$PWD:/src/hopnet:ro" -v "$PWD/../ns3-swarm-core:/src/ns3-swarm-core:ro" \
    ghcr.io/swarm-routing/ns3-dev:latest-debug \
    ns3-build-contrib --run "hopnet-example --seed=1" swarm-core=/src/ns3-swarm-core hopnet=/src/hopnet/ns3
```

The harness arm: `routing-compare`, from swarm-routing/ns3-tools, runs `hopnet`
on a scenario and prints the `##RUN##` line (delivery ratio, delay,
overhead) CI's smoke reads. With ns3-tools cloned next to this repository:

```bash
docker run --rm -v "$PWD:/src/hopnet:ro" -v "$PWD/../ns3-swarm-core:/src/ns3-swarm-core:ro" \
    -v "$PWD/../ns3-tools:/src/ns3-tools:ro" ghcr.io/swarm-routing/ns3-dev:latest-debug \
    ns3-build-contrib --run "routing-compare --scenario=paper --nNodes=20 --time=30 --runs=1 --protocols=hopnet" \
    swarm-core=/src/ns3-swarm-core routing-bench=/src/ns3-tools/ns3/routing-bench hopnet=/src/hopnet/ns3
```

Paired comparisons over many seeds (a change test, the anchor against the
paper's trend) run on GitHub Actions, one cell per job, through
`.github/workflows/benchmark-cell.yml` and ns3-tools' `ns3-campaign`; the
plans are in `tools/bench/campaigns/` and the procedure is AGENTS.md "Build &
verify". Dispatching cells needs write access to this repository.

## Docker and dev containers

`make shell` opens an interactive shell in the `ns3-dev` debug image with
this checkout at `/src`; ns-3 is at `/opt/ns-3`, and `ns3-build-contrib
--help` lists the build options. Set `NS3_IMAGE` for another profile
(`ghcr.io/swarm-routing/ns3-dev:latest-sanitized`, or a pinned ns-3 release
such as `3.48-debug`); the images are described in
swarm-routing/ns3-containers ("Which image"). There is no `.devcontainer/`
yet.

## Lint and format

CI formats C++ with clang-format 18 and the org's style file, which this
repository does not copy:

```bash
curl -fsSL -o /tmp/org.clang-format https://raw.githubusercontent.com/swarm-routing/.github/main/.clang-format
git ls-files '*.h' '*.cc' | xargs clang-format-18 --style=file:/tmp/org.clang-format --dry-run --Werror   # check
git ls-files '*.h' '*.cc' | xargs clang-format-18 --style=file:/tmp/org.clang-format -i                  # fix
make headers          # licence headers, and core/ includes only the standard library and swarm-core's core/
make org-sync-check   # the org-managed files match the org templates
make check            # lint, both test levels, headers and org-sync together
```

`make headers` and `make org-sync-check` fetch the org tools into
`~/.cache/swarm-routing/`.

## CI map

| Workflow | Job (check name) | What it checks | Required |
| --- | --- | --- | --- |
| `core.yml` | `core-test` | `core/` alone: debug with strict warnings, ASan + UBSan, the `hopnet-core` CMake package | yes |
| `core.yml` | `core-format` | clang-format-18 on `core/` | yes |
| `ns3.yml` | `ci / ns-latest debug`, `ci / ns-latest sanitized` | the module builds with strict warnings on the swarm-core and ns3-tools releases, the test suites pass, the delivery smoke, determinism | yes |
| `ns3.yml` | `ci / format` | clang-format-18 on every `.h`/`.cc` | yes |
| `licence.yml` | `headers / licence` | licence headers; `core/` includes nothing outside the standard library and swarm-core's `core/` at the pinned commit | yes |
| `swarm-core-pin.yml` | `swarm-core / pin` | `swarm-core.lock` matches its release and no newer release is out | yes |
| `ns3.yml` | `codecov`, the canary leg | coverage upload; the build on the dependencies' `main` | no |
| `benchmark-cell.yml` | | one benchmark cell, dispatched by `ns3-campaign` | no (manual) |
| `org-sync.yml` | `check / org-sync` | the org-managed files match the templates | yes |
| `pr-title.yml` | `title / title` | the PR title is a lowercase Conventional Commit | yes |
| `review-gate.yml` | `review / coderabbit` | CodeRabbit has reviewed the PR | yes |
| `dco.yml`, `scorecard.yml`, `sync-labels.yml` | | sign-off on outside contributions; OpenSSF Scorecard; labels | no |

The required checks are the ones the `main` ruleset lists: `gh api
repos/swarm-routing/HOPNET/rules/branches/main`. `main` merges through a
merge queue, so a pull request is merged by enabling auto-merge.

## Release

Releases follow the org procedure: [CONTRIBUTING "Cutting a release"](https://github.com/swarm-routing/.github/blob/main/CONTRIBUTING.md#cutting-a-release).
Here `release.yml` is dispatched with `dry-run`, then `bump`; merging the
bot's `chore(release): bump version A to B` PR tags the version and
publishes `hopnet-<ver>.tar.gz` (`make dist` builds it locally), and the org
bot then opens the `hopnet-core` port bump in swarm-routing/vcpkg-registry.
What each release must show is [roadmap.md](roadmap.md).

## Troubleshooting

- **`make build` or `make test` cannot find swarm-core.** They mount
  `../ns3-swarm-core`; clone it there (see "Get the code") or pass
  `SWARM_CORE_NS3=/path/to/checkout`.
- **The ns-3 build fails on a swarm-core symbol that CI does not complain
  about.** Your swarm-core checkout is on `main`, not the release in
  `swarm-core.lock`; check out that tag.
- **The first `core/` configure fails offline.** It downloads swarm-core's
  release tarball; configure once online, or point `SWARM_CORE_DIR` at a
  checkout's `core/`.
- **A warning fails the build.** `--strict-warnings` turns warnings in this
  module's sources into errors (ns-3's and swarm-core's headers are exempt);
  `make core-test` uses the same flags for `core/`.
- **`ci / format` fails but your editor's formatter agreed.** Use
  clang-format 18 with the org's style file (see "Lint and format").
- **`swarm-core / pin` went red on a PR that does not touch the lock.** A new
  swarm-core release is out; the org bot's bump PR fixes it, after which your
  PR passes again.
- **Docker on Apple silicon is slow or fails with an exec format error.** The
  `ns3-dev` images are `linux/amd64` only; `make shell PLATFORM=linux/amd64`.
- **The determinism gate fails.** Something reaching the output depends on
  wall-clock time, libc `rand()` or unordered-container order (golden rule 5).
