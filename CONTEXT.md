# CONTEXT.md — orientation and history

Background an agent or a new contributor needs that does not belong in
`AGENTS.md`: domain notes, current state, glossary, open questions. Read the
section a task needs, not the whole file.

## Current state

**Status: `core/` and the ns-3 module are written; the gates are not run** (#5; the maintainer started it on 2026-11-10; fidelity sheet #4). HOPNET was a stretch goal in AntHocNet v2.5.0 and was never implemented there: there is no arm and no data. [`docs/fidelity.md`](docs/fidelity.md) maps the thesis (and the AntHocNet comparison it ran) row by row; `core/` was written from it in slices: the intra-zone half, the inter-zone discovery and forwarding, then the local repair of §4.3 (`core/hopnet-repair.cc`). The ns-3 module `hopnet` (`ns3/`, #85) carries it out on swarm-core. What is open: the gates (the delivery smoke at ≥ 20 seeds and the anchor against AntHocNet, #28, #5) and the review of the fidelity sheet against the 2009 journal paper (#21).

## Glossary

## Open questions

- Which AntHocNet configuration did the thesis compare against, and can its scaling claim be reproduced as an anchor? (AGENTS.md rule 12.) Partly answered in [`docs/fidelity.md`](docs/fidelity.md) (*The comparison the thesis ran*): the thesis names no AntHocNet implementation or parameter, and its AntHocNet curves match the published QualNet results of Di Caro, Ducatelle & Gambardella (PPSN 2004), so they look transcribed rather than rerun. The scenario is recoverable from that paper and the trend is testable as a paired anchor against `swarm-routing/AntHocNet`; the journal version (Ad Hoc Networks 2009, not held) may say more.
- The thesis never gives HOPNET's next-hop selection rule (and leaves the ant period, ε and the timers open). `core/` is written with each of them as a repo choice recorded in [`docs/fidelity.md`](docs/fidelity.md) (maintainer decisions on #4 and #5); whether the 2009 journal paper settles any of them is the review in #21.
