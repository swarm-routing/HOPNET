# HOPNET

<!-- org-sync:begin badges -->
[![Release](https://img.shields.io/github/v/release/swarm-routing/HOPNET?sort=semver&cacheSeconds=1800)](https://github.com/swarm-routing/HOPNET/releases)
[![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.24125265.svg)](https://doi.org/10.5281/zenodo.24125265)
[![Cite](https://img.shields.io/badge/cite-CITATION.cff-blue)](CITATION.cff)
[![Licence: GPL-2.0-only, core/ MIT OR GPL-2.0-only](https://img.shields.io/badge/licence-GPL--2.0--only%2C%20core%2F%20MIT%20OR%20GPL--2.0--only-blue)](LICENSING.md)
[![Conventional Commits](https://img.shields.io/badge/Conventional%20Commits-1.0.0-yellow)](https://www.conventionalcommits.org)
[![Colony Arena: HOPNET](https://img.shields.io/badge/Colony%20Arena-project%20page-orange)](https://swarm-routing.github.io/projects/HOPNET/)
[![OpenSSF Scorecard](https://api.securityscorecards.dev/projects/github.com/swarm-routing/HOPNET/badge)](https://scorecard.dev/viewer/?uri=github.com/swarm-routing/HOPNET)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue?logo=cplusplus)](AGENTS.md)
[![Runs on ns-3](https://img.shields.io/badge/runs%20on-ns--3-informational)](https://www.nsnam.org)
[![Built on: ns3-dev](https://img.shields.io/badge/built%20on-ns3--dev%20image-blue?logo=docker)](https://github.com/orgs/swarm-routing/packages/container/package/ns3-dev)
[![codecov](https://codecov.io/gh/swarm-routing/HOPNET/graph/badge.svg)](https://codecov.io/gh/swarm-routing/HOPNET)
[![CodeRabbit Pull Request Reviews](https://img.shields.io/coderabbit/prs/github/swarm-routing/HOPNET?utm_source=oss&utm_medium=github&utm_campaign=swarm-routing%2FHOPNET&labelColor=171717&color=FF570A&link=https%3A%2F%2Fcoderabbit.ai&label=CodeRabbit+Reviews)](https://coderabbit.ai)
<!-- org-sync:end badges -->

HOPNET (Wang, 2007) as a standalone swarm routing protocol for MANETs: ant-colony routing over routing zones, ants hopping between zones. A simulator-agnostic `core/` plus the ns-3 contrib module `hopnet` on [`swarm-core`](https://github.com/swarm-routing/ns3-swarm-core).

**Status: `core/` written, ns-3 module in** ([#5](https://github.com/swarm-routing/HOPNET/issues/5)): the intra-zone half (internal ants, IntraRT, neighbour discovery), the inter-zone half (bordercasting, InterRT, data forwarding) and §4.3's local repair are written and tested, and the ns-3 module `hopnet` carries them out on swarm-core; its anchor against AntHocNet (the thesis's Fig. 5.11) is **not reproduced**: three pre-registered pilot rounds missed their threshold, so it is recorded as a [written infeasibility verdict](docs/anchor-verdict.md) and HOPNET's numbers do not count in comparisons; the delivery smoke at ≥ 20 seeds is not run yet. Every reading follows the [fidelity sheet](docs/fidelity.md). See [AGENTS.md](AGENTS.md) for the plan.

**Release status:** the first release, v0.1.0, comes before the comparator gates (maintainer decision 2026-11-10): it has **not** cleared them (org golden rule 6: fidelity sheet, per-PR delivery smoke, anchor). It ships `core/` and `docs/`; the ns-3 module `hopnet` and the anchor are pending, so no number from v0.1.0 counts as a benchmark result. The gated release is proposed as v0.2.0; the ladder is in [docs/roadmap.md](docs/roadmap.md). Releases are cut by [`release.yml`](https://github.com/swarm-routing/HOPNET/blob/main/.github/workflows/release.yml) and listed under [Releases](https://github.com/swarm-routing/HOPNET/releases).

<!-- org-sync:begin citing -->
## Releases & citing

Versions follow [SemVer](https://semver.org) ([CHANGELOG.md](CHANGELOG.md)). Each release is listed under [Releases](https://github.com/swarm-routing/HOPNET/releases), built by the org's release workflow and archived on [Zenodo](https://zenodo.org) with its own DOI.

To cite this repository, use the “Cite this repository” button (from [CITATION.cff](CITATION.cff)):

| DOI | resolves to |
|---|---|
| [`10.5281/zenodo.24125265`](https://doi.org/10.5281/zenodo.24125265) | **concept DOI**: always the latest release. This is the badge above, and the right citation when you mean this project. |

Each release also has its own **version DOI**, listed on [its Zenodo record](https://doi.org/10.5281/zenodo.24125265): cite that one when you reproduce a number measured at that release.

Please also cite the work it builds on:

- D. H. Joppi, *ns3-swarm-core*, 2026, [doi:10.5281/zenodo.24138628](https://doi.org/10.5281/zenodo.24138628). The shared primitives core/ builds on (pinned by commit: SWARM_CORE_REF).
- J. Wang, *HOPNET: a Hybrid ant colony OPtimization routing algorithm for mobile ad hoc NETwork*, University of Manitoba, 2008, <https://hdl.handle.net/1993/20935> (no DOI: none registered). The primary source this repository implements; docs/fidelity.md maps it to the code and marks the repository choices. Its title page is dated December 2007; the issued date in the University of Manitoba's MSpace record is 2008, the year given here.

What this implementation reproduces from its sources, and every deliberate deviation, is in [docs/fidelity.md](docs/fidelity.md).

Using it in your work? Contributions are welcome, and citing the DOI helps just as much.
<!-- org-sync:end citing -->

## Epics

<!-- epics:begin -->
<!-- Generated by swarm-routing/.github tools/epics/epics.py from the issues labelled `epic`; edit the issues, then re-run it. -->

What is planned and in progress here: the issues labelled [`epic`](https://github.com/swarm-routing/HOPNET/issues?q=label%3Aepic). An epic's open sub-issues carry a `priority:*` and a `model:*` label, so a contributor or an AI agent can pick one up ([how issues work](https://github.com/swarm-routing/.github/blob/main/CONTRIBUTING.md#issues)). The whole org's plan: the [roadmap](https://github.com/swarm-routing/.github/blob/main/ROADMAP.md#epics).

| Epic | Status | Sub-issues closed |
|------|--------|-------------------|
| [#3](https://github.com/swarm-routing/HOPNET/issues/3) HOPNET as a standalone hexagonal protocol (scheduled after Termite) | In progress | 2 / 3 |

<!-- epics:end -->

## Licence

See [LICENSING.md](LICENSING.md).
