## v0.1.0 (2026-11-11)
### Release status

v0.1.0 is cut before the comparator gates (maintainer decision 2026-11-10). It has **not** cleared the three gates of org golden rule 6 (a fidelity sheet from its source, the per-PR delivery smoke, and an anchor reproducing the source's own trend). The anchor is pending, and so is the ns-3 contrib module `hopnet`: it is still being written, and an `ns3/` directory in this release's archive, if there is one, is that work in progress as it stood on `main`. No result from this release counts as a benchmark number, and it does not enter the benchmarks suite until a release clears the gates; that gated release is proposed as v0.2.0 (swarm-routing/.github ROADMAP "Releases"). It ships `core/` (MIT OR GPL-2.0-only, with `core/LICENSE`) and `docs/` (plus `ns3/` if it exists), as they stand on `main` at the release commit.


### Feat

- **core**: the local-repair slice — repair ant, notification and error ants (#37)
- **core**: the inter-zone half — bordercasting, external ants, interrt, data routing (#33)
- **core**: the intra-zone half — internal ants, intrart, eqs. 4.1-4.3, neighbour discovery (#25)
