# AGENTS.md — guide for AI coding agents

Operational guide for LLM agents working in `swarm-routing/hopnet`. Read this
before changing anything. Org-wide conventions:
[`swarm-routing/.github` CONTRIBUTING.md](https://github.com/swarm-routing/.github/blob/main/CONTRIBUTING.md). Shape and rules mirror
[AntHocNet's AGENTS.md](https://github.com/swarm-routing/AntHocNet/blob/main/AGENTS.md), the source repository.

## What this repo is

**Status: `core/` in progress (#5; started by the maintainer's decision of 2026-11-10): the intra-zone half, the inter-zone discovery and forwarding, and §4.3's local repair (repair, notification and error ants; readings (i)–(l) accepted 2026-11-11) are written; the local repair at a transit relay follows reading (m), accepted 2026-11-11. After the anchor pilot `pilot-1` was NO-GO (HOPNET's own control traffic collapsed the channel, #109), readings (n)–(r), accepted 2026-11-13, cut that traffic: internal-ant period 2 s, discovery retry 3 s, neighbour loss after 3 periods, ZRP query detection and early termination, and no pheromone toward the next Path node treated as a link failure ([ADR-0002](docs/adr/0002-cut-control-traffic-after-pilot-1.md)); `pilot-2` re-runs the pilot on them. The ns-3 module `hopnet` carries them out on swarm-core's `SwarmRoutingBase` (#5; it waited until ARA's built green against swarm-core, maintainer decision 2026-11-10, #3, met on ARA's `main`); the gates (delivery smoke at ≥ 20 seeds, the anchor against AntHocNet, #28) are not run yet.** `docs/fidelity.md` (#4) and the accepted pre-registration on #5 govern every reading.

HOPNET (Wang, 2007 MSc thesis, University of Manitoba) as a standalone hexagonal protocol: ant-colony routing combined with zone routing, where ants hop between routing zones (proactive inside a zone, reactive between zones). It is the one published result found that benchmarks against AntHocNet directly, claiming better scaling. A simulator-agnostic `core/` and an ns-3 `ns3/` adapter (contrib module `hopnet`) linking against `swarm-core`. There is no AntHocNet arm to promote: it is written from the thesis.

```
core/       simulator-agnostic algorithm + unit tests (no ns-3 headers; MIT OR GPL-2.0-only, core/LICENSE):
            hopnet-config.h (parameters, each citing its fidelity row), hopnet-ant (the ant header + wire codec),
            hopnet-intrazone-table (IntraRT), hopnet-intrazone (internal ants, eqs. 4.1-4.3, neighbours, selection),
            hopnet-interzone-table (InterRT), hopnet-interzone (bordercasting, external ants, data routing),
            hopnet-repair.cc (§4.3's local repair: the InterZone members for repair, notification and error ants),
            hopnet-data (the data header + wire codec), hopnet-observe.h (what the core reports to swarm-core's
            `IRouterObserver`, #40; `kindNames()` in hopnet-ant is the table the ns-3 module registers);
            test/ (ctest); built on swarm-core's dual-licensed core/ primitives (pinned: SWARM_CORE_REF in core/CMakeLists.txt)
ns3/        contrib module `hopnet` (GPL-2.0-only), compiling core/ on swarm-core:
  model/hopnet-header.*            the ant payload and the DataHeader right after each data packet's UDP header (core/'s codecs)
  model/hopnet-routing-protocol.*  the adapter: IntraZone + InterZone per node on SwarmRoutingBase (`&Observer()` to both, `kindNames()`
                                   registered once, #40); RouteInput takes every data packet; holds the packets the core queues
  helper/hopnet-helper.*           HopnetHelper (swarm-core's SwarmRoutingHelper; the WifiLinkMonitor gives the edge time)
  helper/hopnet-bench-arms.cc      the `hopnet` arm of ns3-tools' routing-compare (built only beside routing-bench)
  examples/hopnet-example.cc       a small RWP MANET; CI's determinism program
  test/hopnet-test-suite.cc        `test.py -s hopnet`: the core tests, both readers' roundtrip and fuzz, a 4-node chain across zones,
                                   determinism, the observer's trace sources identical with and without
docs/       fidelity sheet (thesis section -> implementation), parameters with sources and the unknowns
tools/bench/campaigns/  campaign plans: smoke.yaml (the change-test smoke against the base), pilot.yaml (the anchor's 5-seed pilot `pilot-1`, #5), pilot-2.yaml (the same after readings (n)–(r), ADR-0002)
.github/workflows/benchmark-cell.yml   one benchmark cell at a pinned source_sha (reusable-benchmark-cell.yml, build mode); dispatched by ns3-campaign
```

## Build & verify

CI today is `.github/workflows/core.yml` (`core-test`: `core/` with strict warnings as errors and ctest, again under ASan + UBSan; `core-format`: clang-format-18 with the org style), plus the licence, org-sync and PR-title checks, and `.github/workflows/ns3.yml`: [`reusable-ns3-ci.yml`](https://github.com/swarm-routing/.github/blob/main/.github/workflows/reusable-ns3-ci.yml) builds `ns3/` in `ns3-dev:latest-{debug,sanitized}` with strict warnings and coverage (Sonar off until the repository has a project, swarm-routing/.github#104), with `dependencies: swarm-core=swarm-routing/ns3-swarm-core@main` and `routing-bench=swarm-routing/ns3-tools@main:ns3/routing-bench`, runs `test.py -s hopnet`, `routing-compare --scenario=paper --nNodes=20 --time=30 --runs=1 --protocols=hopnet` as the delivery smoke and `hopnet-example --seed={seed}` as the determinism program. Locally:

```bash
make core-test   # core/ alone: cmake -S core -B build/core && cmake --build build/core && ctest --test-dir build/core
cmake -S core -B build/sanitized -DHOPNET_CORE_SANITIZE=ON && cmake --build build/sanitized && ctest --test-dir build/sanitized   # as CI
cmake -S core -B build/local -DSWARM_CORE_DIR=/path/to/ns3-swarm-core/core   # a local swarm-core instead of the pin
make help    # the org's standard target names: build, test, lint, format, check, clean, shell, ci
make check   # everything CI gates on
make test    # core-test, then the ns-3 module in ns3-dev:latest-debug (SWARM_CORE_NS3=<checkout>, default ../ns3-swarm-core):
docker run --rm -v "$PWD:/src/hopnet:ro" -v "$PWD/../ns3-swarm-core:/src/ns3-swarm-core:ro" \
    ghcr.io/swarm-routing/ns3-dev:latest-debug \
    ns3-build-contrib --strict-warnings --quality-modules hopnet --test swarm-core=/src/ns3-swarm-core hopnet=/src/hopnet/ns3
```

Benchmarks (#28, swarm-routing/.github#156): change tests and the anchor
run here, released versions in swarm-routing/benchmarks (its ADR-0002).
`.github/workflows/benchmark-cell.yml` is a thin caller of
swarm-routing/.github's `reusable-benchmark-cell.yml` in build mode: each cell
checks out its `source_sha`, builds `hopnet` with swarm-core at
`SWARM_CORE_REF`, routing-bench at a pinned ns3-tools commit and AntHocNet's
ns-3 module (the anchor's comparator, golden rule 12: AntHocNet's v2.7.0
release commit once it ships, `main`'s commit until then; `stage` copies its
`core/` into the module, as its `make install-ns3` does) on the ns3-dev image
given by digest, and runs `routing-compare`. Campaign plans live in
`tools/bench/campaigns/` and run through `ns3-campaign` (swarm-routing/ns3-tools
`python/ns3_tool/change_plan.py`; its docstring is the plan format).
`smoke.yaml` is the change-test smoke: HOPNET at a PR's head against its base,
two families (`grid-rwp`, `static-mesh`), 20 paired seeds. For a PR that
changes `core/` or `ns3/`, copy it with a new `generation` and the PR's base
and head commits in `pins`, then:

```bash
pip install pyyaml                                    # a YAML plan needs PyYAML
C="python3 ../ns3-tools/python/ns3_tool/campaign.py"   # or ns3-campaign (pip install ../ns3-tools/python)
$C plan P.yaml                                        # every job; dispatches nothing
$C dispatch P.yaml --cap 4                            # resumable: re-run until every label is logged
$C status P.yaml --brief                              # poll lightly (15 min)
G=$(python3 -c 'import sys,yaml; print(yaml.safe_load(open(sys.argv[1]))["generation"])' P.yaml)
$C collect P.yaml --out ".campaign/$G/cells"          # rejects a cell whose ##PROV## is not its pin or the campaign's one dependency set
$C verdicts P.yaml --dir ".campaign/$G/cells"         # head minus base (or minus `reference`), paired 95 % CI per cell
```

and put the `verdicts` lines in the PR (no hand numbers, ns3-tools rule 13).
`pilot.yaml` is a one-arm plan with `reference: anthocnet`: `verdicts` prints
hopnet minus anthocnet per cell. The dependency commits come from
`benchmark-cell.yml` at the dispatch ref (`main`), not from the plan, and
`collect` (ns3-tools v0.2.0 or later, swarm-routing/ns3-tools#148) also
rejects a cell whose `##PROV## modules=` dependency commits are not the one
set the campaign's cells recorded first: do not merge a change to that file's
`dependencies` while a campaign is in flight, or its later cells are rejected.
State lives in `.campaign/<generation>/` (git-ignored); a cell's
artifact is kept 7 days.

## Golden rules (invariants — do not break)

Org-wide (1–9), then this repo's own.

<!-- org-sync:begin org-rules -->
1. **Licensing per directory, as AntHocNet** ([`LICENSING.md`](LICENSING.md);
   AntHocNet [LICENSING.md](https://github.com/swarm-routing/AntHocNet/blob/main/LICENSING.md),
   [ADR-0028](https://github.com/swarm-routing/AntHocNet/blob/main/docs/adr/0028-core-is-dual-licensed.md)). A
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
   shared module is `swarm-core` (`#include "ns3/swarm-core-module.h"`), and
   every protocol builds on it, AntHocNet included: `core/` on its `core/`, the
   ns-3 adapter on `SwarmRoutingBase` (swarm-routing/ns3-swarm-core#14).
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
13. **Written fresh.** No AntHocNet code exists for HOPNET; `core/` (dual) is original work, and anything reused from another protocol keeps that protocol's licence and provenance. If the thesis is not openly redistributable, its copy stays in a private paper store the maintainer keeps, never in this public repository.

## Conventions

<!-- org-sync:begin conventions -->
- Issues follow the org taxonomy ([CONTRIBUTING.md](https://github.com/swarm-routing/.github/blob/main/CONTRIBUTING.md#issues), from
  [AntHocNet ADR-0013](https://github.com/swarm-routing/AntHocNet/blob/main/docs/adr/0013-track-bugs-and-findings-as-issues.md)): one type label
  (`bug`/`enhancement`/`chore`/`documentation`/`verification`, plus `epic` for
  umbrellas), area label(s) (`protocol`/`adapter`/`ns3`/`benchmark`/
  `observability`/`packaging`/`learn-site`/`literature`), one `model:*`
  suggestion (a model on the Claude Pro plan), one `priority:P1|P2|P3` on
  non-epic issues (none on an epic), a
  `release:vX.Y.Z` label only when a release in the repository's ladder
  (`docs/roadmap.md`; until it lands, the org ROADMAP's release train)
  gates the issue: that label set is the version's scope,
  and the version is released when its issues are done (CONTRIBUTING
  "Cutting a release"). Labels come from `labels.json` via the `sync-labels`
  workflow, the shared `release:v0.1.0` to `release:v0.4.0` and
  `release:v1.0.0` among them; a version outside that set is added to
  `swarm-routing/.github`'s `labels.json` (and CONTRIBUTING's label table)
  when a ladder needs it, and reaches a repository when its `sync-labels` runs
  (monthly, or dispatched after the change merges),
  or, in a repository that keeps its own release labels (AntHocNet), created
  there with `gh label create release:vX.Y.Z` when its ladder adds the version.
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
  only reports drift. Private repositories are never listed
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
| Give a repository its docs/references.bib, or review it: every paper and org release it relies on, confirmed from Crossref, DataCite or Zenodo (a work with no DOI, such as a thesis, from its publisher's or institution's record, with its url), DOI links first, the fidelity Sources table and CITATION.cff references generated from it | `swarm-routing/.github` `tools/fidelity/fidelity.py` (bib), `tools/zenodo/zenodo.py` | `references` |
| Take open pull requests from review to merge: CI, mergeable state and unresolved review threads in one view, each finding verified and fixed or answered, then merged when green (auto-merge where the repository has a merge queue) | `swarm-routing/.github` `tools/pr-status/pr_status.py` | `pr-shepherd` |
| Measure what a Claude Code session loads from the agent files (`CLAUDE.md` and its `@` imports, or `AGENTS.md` alone), or where a finished Claude Code session's tokens went, before cutting or adding agent text | `swarm-routing/skills` `tools/token-audit/token_audit.py` | `token-audit` |
| Register a repository for the OpenSSF Best Practices badge, propose its answers and check they stay current | `swarm-routing/.github` `tools/bestpractices/bestpractices.py` | `bestpractices` |

A skill only this repository uses lives in its own `.claude/skills/`; a skill
two repositories need moves to the plugin. Skills that file, cite and extract
papers stay in a private library the maintainer keeps; finding
new papers and confirming a reference is public (`lit-search`). A public skill
may point to that library's skills, never carry its content. This section is managed by org-sync: add
a row in `swarm-routing/.github` `templates/agents/skills.md`, not here (an edit
here is undone by the next `org_sync.py apply`).
<!-- org-sync:end skills -->

<!-- org-sync:begin agent-defaults -->
## Working defaults

Condensed from [andrej-karpathy-skills](https://github.com/forrestchang/andrej-karpathy-skills).

- **Assumptions:** state the ones that matter. Ask only when the choice
  belongs to the maintainer: a default, a published number, a cell, the scope.
  Then give your recommendation and its reason with the question, never the
  options alone. Otherwise pick the sensible option, say which, and keep
  going. Long unattended runs, like campaigns, depend on that.
- **Simplicity:** the least code that solves the task. No speculative
  features, configurability or abstractions for one use.
- **Surgical diffs:** every changed line traces to the request. Match the
  surrounding style. Mention unrelated dead code rather than deleting it, and
  remove only the orphans your own change created.
- **Verifiable goals:** a bug gets a reproducing test first; a refactor passes
  before and after. Finish when the check passes, not when the code looks right.

## Session habits

- **Handoffs are always a ticket.** When a session ends with work in flight, or a
  handoff is asked for, open a GitHub issue from the *Handoff* template (label
  `handoff`); a session that continued a handoff updates that issue instead of
  opening another. Never write a handoff file in the repo. A session that
  continues handed-off work starts from the newest open `handoff` issue for
  that work; a new task starts from the request. Close the issue, and any
  older one it superseded, when the work it hands off is done ([AntHocNet ADR-0013](https://github.com/swarm-routing/AntHocNet/blob/main/docs/adr/0013-track-bugs-and-findings-as-issues.md) item 5).
- **Attach only the repositories the task needs.** Each attached repository's
  agent files are loaded into every request, whether the task touches it or
  not.
- **Start from the record, not from memory:** the matching `handoff` issue,
  then the issue's acceptance criteria, then `main`. Check a claim against its
  source before it goes into an issue or a PR.
- **Script what repeats.** A check or a lookup run more than a few times
  becomes a script in `tools/` that prints a summary; the model reads the
  verdict, not the raw data (AntHocNet ADR-0014).
- **Measure before cutting or adding agent text:** the `token-audit` skill
  (`swarm-routing/skills` `tools/token-audit/token_audit.py`) prints what a
  session loads and where a finished session's tokens went.

## Response style — caveman (token economy)

Answer in tight, low-filler prose: drop hedging, pleasantries, and preamble; keep
the substance. Fewer output tokens on every reply, same information.

**Carve-out (the record stays full).** Keep byte-for-byte exact — code,
commands, file paths, error text, benchmark numbers. And keep the *durable
record* detailed, never terse: commit messages, PR bodies, ADRs, `docs/`, and
issue / finding threads. Cross-session traceability depends on those being
complete — AntHocNet's AC_BE_NQOS / A2 investigation was only recoverable
because the issue threads were thorough. Caveman the chat, not the record.

## Agent pitfalls

- `gh api … -F body=@file` reads the file; `-f body=@file` posts the literal
  text `@file`.
- When the permission system refuses a command (a merge, say), stop and report
  it. Never retry it another way, and never ask another agent to run it.
- A PR body's `Closes #N` closes the issue on merge; write `Part of #N` while
  acceptance criteria remain open.
- Before saying a tool or check is missing, look at `main` (`git fetch`,
  `origin/main`), not a stale local checkout.
- In an issue, PR or comment, write every issue of another repository in
  full, `swarm-routing/<repo>#N`, also inside a list or a range. A bare `#N`
  links to the repository the text is in: in
  `swarm-routing/AntHocNet#65, #151` the second number points at this
  repository's #151 (swarm-routing/.github#277).
- A sentence in `ROADMAP.md` or an AGENTS.md that calls an issue done is not
  evidence: read the issue's acceptance criteria. On 2026-11-12 nine issues
  described as done had open criteria (swarm-routing/.github#274).
- Change text inside `org-sync:begin/end` markers only through the template
  in `swarm-routing/.github` `templates/`: one PR there, then org-bot's
  `bot/org-sync` refresh PR in each repository. A hand edit per repository
  stays red on `check / org-sync` (swarm-routing/.github#236).
- In a repository with a merge queue, merge by enabling auto-merge (squash);
  a direct merge (`PUT pulls/<n>/merge`) is refused with HTTP 405. Only the
  required checks (`gh api 'repos/{owner}/{repo}/rules/branches/main'`, which
  `gh` fills from the checkout) gate a merge: a red or pending report-only
  check (`codecov/patch`, Sonar) is triaged, not waited on. A red `dco` on an
  outside contributor's commit still holds the PR until that author signs
  off; on the maintainer's or an agent's commits it is expected
  (swarm-routing/.github#212). `dco` is not a required check and runs on
  `pull_request` only, so merge an outside contributor's PR, by auto-merge or
  directly, only after `dco` passed on the head commit that merges. Never push
  to an outside contributor's PR: ask them for the change, or make it in a
  follow-up PR after the merge (swarm-routing/.github#288). If the maintainer
  must push to such a PR, first disable auto-merge, or, if the PR is already
  in a merge queue, remove it from the queue (disabling auto-merge does not).
  Re-enable auto-merge, or enqueue it again, only after `dco` passed on the
  new head: GitHub keeps auto-merge on after a push by someone with write
  access, and `dco` is not a required check.
- `review / coderabbit` is a required check (the review gate,
  swarm-routing/.github#294): it passes once CodeRabbit has reviewed the PR,
  once it says it will not, for a draft, or 30 minutes after the PR opened
  (then with one comment). So auto-merge may be enabled at once: the merge
  waits for the gate, and the ruleset holds it while a CodeRabbit thread is
  unresolved. Still read every thread and fix or answer it before resolving.
  Where the gate is not required yet (`gh api
  'repos/{owner}/{repo}/rules/branches/main'` does not list it), wait first
  with `python3 -I ../.github/tools/pr-status/pr_status.py gate REPO#N`,
  which waits for CodeRabbit alone (swarm-routing/.github#238).
- Never add a `Signed-off-by` trailer, for the maintainer or anyone else: a
  DCO sign-off is a person's own certification (CONTRIBUTING "Outside
  contributions").
- Releases are cut on request only: dispatch the release `bump` when the
  maintainer asks, once the version's `release:vX.Y.Z` issues are done and
  its exit criteria in the ladder (`docs/roadmap.md`, or the org ROADMAP's
  release train until it lands) are met; never approve a gated publish. A
  failed publish is recovered with "Re-run all jobs", never "Re-run failed
  jobs" (CONTRIBUTING "Cutting a release"). Never type a release's DOI: read
  it from Zenodo.
- Cite a paper in a public issue, PR or doc by its `https://doi.org/<DOI>`
  link, read from its record (`lit-search` confirms it), or, for a work with
  no DOI, by its publisher's or institution's page; never point into or name
  the private paper library there.
<!-- org-sync:end agent-defaults -->

## Where to look

| You want to… | Go to |
|--------------|-------|
| The org's invariants, labels, issue shape | [`swarm-routing/.github` CONTRIBUTING.md](https://github.com/swarm-routing/.github/blob/main/CONTRIBUTING.md) |
| Know which release a piece of work serves, or why something is not planned | [`docs/roadmap.md`](docs/roadmap.md) (the release ladder; issues carry `release:` labels) |
| The plan | swarm-routing/.github#52, org ROADMAP "Protocol repositories" |
| The source | Wang, *HOPNET* (2007 thesis), [UManitoba MSpace](https://mspace.lib.umanitoba.ca/bitstream/1993/20935/1/Wang_HOPNET_a.pdf); AntHocNet `docs/research-landscape-2026.md` (Other swarm protocols) |
| Shared primitives | `swarm-routing/ns3-swarm-core`; `core/` fetches its `core/` at `SWARM_CORE_REF` (`core/CMakeLists.txt`); bump the pin in all four places together: `SWARM_CORE_REF` in `core/CMakeLists.txt` and in the `Makefile`, and `core-deps` in `.github/workflows/licence.yml` and `.github/workflows/ns3.yml`; the `swarm-core / pin` check (`swarm-core-pin.yml`) fails when they name different SHAs or when swarm-core main's `core/` is no longer the pinned one |
| The core's readings and parameters | `docs/fidelity.md`, the accepted pre-registration on #5, `core/hopnet-config.h` |
| Benchmark against AntHocNet, ARA, AODV, OLSR | `swarm-routing/ns3-tools` arenas, results into `swarm-routing/benchmarks` |
| Record an architecture decision | [`docs/adr/`](docs/adr/) (`0000-template.md`) |
| Pick up open work | this repo's issues — highest open `priority:P*` first; newest open `handoff` issue for work in flight |
