# AGENTS.md — guide for AI coding agents

Operational guide for LLM agents working in `swarm-routing/hopnet`. Read this
before changing anything. Org-wide conventions:
[`swarm-routing/.github` CONTRIBUTING.md](https://github.com/swarm-routing/.github/blob/main/CONTRIBUTING.md). Shape and rules mirror
[AntHocNet's AGENTS.md](https://github.com/danieljoppi/AntHocNet/blob/main/AGENTS.md), the source repository.

## What this repo is

**Status: `core/` in progress (#5; started by the maintainer's decision of 2026-11-10): the intra-zone half, the inter-zone discovery and forwarding, and §4.3's local repair (repair, notification and error ants; readings (i)–(l) accepted 2026-11-11) are written; the local repair at a transit relay follows reading (m), accepted 2026-11-11; the ns-3 module and the gates are not. The ns-3 module waits until ARA's builds green against swarm-core (maintainer decision 2026-11-10, #3).** `docs/fidelity.md` (#4) and the accepted pre-registration on #5 govern every reading. Directories marked (planned) do not exist yet: update this file in the PR that creates each.

HOPNET (Wang, 2007 MSc thesis, University of Manitoba) as a standalone hexagonal protocol: ant-colony routing combined with zone routing, where ants hop between routing zones (proactive inside a zone, reactive between zones). It is the one published result found that benchmarks against AntHocNet directly, claiming better scaling. A simulator-agnostic `core/` and an ns-3 `ns3/` adapter (contrib module `hopnet`) linking against `swarm-core`. There is no AntHocNet arm to promote: it is written from the thesis.

```
core/       simulator-agnostic algorithm + unit tests (no ns-3 headers; MIT OR GPL-2.0-only, core/LICENSE):
            hopnet-config.h (parameters, each citing its fidelity row), hopnet-ant (the ant header + wire codec),
            hopnet-intrazone-table (IntraRT), hopnet-intrazone (internal ants, eqs. 4.1-4.3, neighbours, selection),
            hopnet-interzone-table (InterRT), hopnet-interzone (bordercasting, external ants, data routing),
            hopnet-data (the data header + wire codec), hopnet-observe.h (what the core reports to swarm-core's
            `IRouterObserver`, #40; `kindNames()` in hopnet-ant is the table the ns-3 module registers);
            test/ (ctest); built on swarm-core's dual-licensed core/ primitives (pinned: SWARM_CORE_REF in core/CMakeLists.txt)
ns3/        (planned) contrib module `hopnet` (GPL-2.0-only): adapter, helper, examples, test suite
            (pass `&Observer()` to IntraZone and InterZone, register `kindNames()` once: #40)
docs/       fidelity sheet (thesis section -> implementation), parameters with sources and the unknowns
```

## Build & verify

CI today is `.github/workflows/core.yml` (`core-test`: `core/` with strict warnings as errors and ctest, again under ASan + UBSan; `core-format`: clang-format-18 with the org style), plus the licence, org-sync and PR-title checks. [`reusable-ns3-ci.yml`](https://github.com/swarm-routing/.github/blob/main/.github/workflows/reusable-ns3-ci.yml) joins once `ns3/` exists (as in ARA and BeeAdHoc), with `dependencies: swarm-core=swarm-routing/ns3-swarm-core@main`. Locally:

```bash
make core-test   # core/ alone: cmake -S core -B build/core && cmake --build build/core && ctest --test-dir build/core
cmake -S core -B build/sanitized -DHOPNET_CORE_SANITIZE=ON && cmake --build build/sanitized && ctest --test-dir build/sanitized   # as CI
cmake -S core -B build/local -DSWARM_CORE_DIR=/path/to/ns3-swarm-core/core   # a local swarm-core instead of the pin
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
   comments where needed), matching its directory in `LICENSING.md`; an
   outside contributor may add their own `Copyright (c) <year> <Name>` line
   beneath the org line, never replace it, and signs off every commit (DCO,
   CONTRIBUTING "Outside contributions"). CI (`reusable-licence-check.yml`)
   checks the SPDX line, and the org line's place when a header names more
   than one holder; a header with a single, other holder (a replaced org line,
   or third-party code) passes it, so review catches that. A new directory
   gets its row in `LICENSING.md` in the same PR.
2. **The licence boundary only goes one way.** GPL code may use the dual
   parts; dual code never pulls GPL-only code in: a `core/` file includes only
   the C++ standard library, other `core/` files and the dual-licensed `core/`
   of a dependency pinned by commit and declared as the `core-deps` input of
   every workflow that runs the licence check (one calling
   `reusable-licence-check.yml`, or one calling `reusable-ns3-ci.yml`, which
   forwards it; workflow file names differ per repo), each with the same pin,
   with `core-roots` naming the repo's own `core/`
   (without it the check skips core includes; a protocol's `core/` on
   swarm-core's; CI checks it, `check-headers.py --core-dep`), and a dual script imports no GPL-only code. Code coming from AntHocNet keeps its
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
   Every job that runs steps has a `timeout-minutes` cap (a job that only
   calls a reusable workflow cannot take one: the called workflow's jobs carry it), and every apt, npm or installer step
   its own, sized from recent green runs; `apt-get update` runs with
   `--error-on=any` inside a shell retry. Ubuntu mirror outages are the usual
   cause of a red run.
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
  `release:vX.Y.Z` label only when a release in the repository's ladder
  (`docs/roadmap.md`; until it lands, the org ROADMAP's release train)
  gates the issue: that label set is the version's scope,
  and the version is released when its issues are done (CONTRIBUTING
  "Cutting a release"). Labels come from `labels.json` via the `sync-labels`
  workflow.
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
- **`main` takes no direct push**, in any repo: its ruleset requires a PR and
  the required checks, not an up-to-date branch. Bots push `bot/<name>` branches
  and open PRs; a release is the bump PR from `bot/release`, tagged after it
  merges (CONTRIBUTING "Cutting a release").
- A PR behind `main` may merge as it is: the ruleset does not require an
  up-to-date branch. Update it first
  (`gh api -X PUT "repos/<owner>/<repo>/pulls/<n>/update-branch"`, with
  `<owner>/<repo>` the PR's base repository and `<n>` its number) only when
  `main` changed what it touches (a template, a workflow, a required check),
  then merge after its CI on the updated branch. A
  repository with a merge queue (`merge_queue = true` in its `[settings]`)
  is not updated by hand: enable auto-merge and the queue tests each PR on
  the latest `main` and merges it (CONTRIBUTING "Merge when CI is green").
  Never force-push a branch someone else pushed. A behind `bot/release` PR is the exception:
  dispatch the release `bump` again, so the version and changelog are rebuilt
  on the new `main`.
- **Settings are code.** Repository settings, rulesets, required checks and the
  Actions allowlist change only by a PR to the repo's `[settings]` in
  `.github/org-sync.toml` or to `templates/rulesets/` in `swarm-routing/.github`,
  never in the UI or by a direct API write. A `templates/rulesets/` change
  applies on merge; a repo's `[settings]` change applies when `repo-settings`
  in `swarm-routing/.github` is dispatched with mode `apply` after the merge
  (the daily run only checks). A required check runs on every PR (no `paths:`
  filter; skip work inside a gate job instead) and is named exactly as its
  check run. A new third-party action goes into `defaults.json`
  `selected_actions` (`templates/rulesets/`) before or with the workflow that
  uses it: in the same PR in `swarm-routing/.github`, otherwise merged there
  first, or `repo-settings` refuses the repo.
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
| Know which release a piece of work serves, or why something is not planned | [`docs/roadmap.md`](docs/roadmap.md) (the release ladder; issues carry `release:` labels) |
| The plan | swarm-routing/.github#52, org ROADMAP "Protocol repositories" |
| The source | Wang, *HOPNET* (2007 thesis), [UManitoba MSpace](https://mspace.lib.umanitoba.ca/bitstream/1993/20935/1/Wang_HOPNET_a.pdf); AntHocNet `docs/research-landscape-2026.md` (Other swarm protocols) |
| Shared primitives | `swarm-routing/ns3-swarm-core`; `core/` fetches its `core/` at `SWARM_CORE_REF` (`core/CMakeLists.txt`); bump it together with `core-deps` in `.github/workflows/licence.yml` and `SWARM_CORE_REF` in the `Makefile` |
| The core's readings and parameters | `docs/fidelity.md`, the accepted pre-registration on #5, `core/hopnet-config.h` |
| Benchmark against AntHocNet, ARA, AODV, OLSR | `swarm-routing/ns3-tools` arenas, results into `swarm-routing/benchmarks` |
| Record an architecture decision | [`docs/adr/`](docs/adr/) (`0000-template.md`) |
| Pick up open work | this repo's issues — highest open `priority:P*` first; newest open `handoff` issue for work in flight |
