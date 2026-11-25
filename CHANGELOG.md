## v0.2.1 (2026-11-25)

### Fix

- **core**: build the error ant's path without vector::insert (gcc 13 -fpic release) (#233)

## v0.2.0 (2026-11-24)
### Release status

v0.2.0 is the "gated" release of `docs/roadmap.md` (maintainer decision 2026-11-24, #198): each gate of org golden rule 6 has given its verdict, and **the anchor's verdict is negative**. The thesis's headline trend (Wang 2007, Fig. 5.11: HOPNET delivering more than AntHocNet as the network scales) is **not reproduced** by this reconstruction. Three pre-registered pilot rounds against AntHocNet missed their threshold, so the anchor is recorded as a written infeasibility verdict (`docs/anchor-verdict.md`, #5, #126) and the anchor campaign itself was not run. **HOPNET's numbers from this release do not count in comparisons** and do not enter the benchmarks suite. The other gates passed: the fidelity sheet (`docs/fidelity.md`) and the per-PR delivery smoke (`tools/bench/campaigns/smoke.yaml`, first run `smoke-47db292`, #28). It ships `core/` (MIT OR GPL-2.0-only, with `core/LICENSE`), the ns-3 contrib module `hopnet` in `ns3/` (GPL-2.0-only) with the swarm-core router observer (#40), the benchmark pipeline and `docs/`. Every reading stays selectable through its `core::Config` switch. The 2009 journal paper, read in part on 2026-11-24 (prose and pseudocode, not its equations; #21), gives the shape of HOPNET's own next-hop rule, which this reconstruction replaced with ACO roulette: implementing it, default-off with its own A/B, is v0.3.0 work, and a new pre-registered anchor may follow on the maintainer's decision.


### Feat

- **core**: take swarm-core from its vcpkg port (manifest mode) (#173)
- **release**: each release opens the hopnet-core port pr in swarm-routing/vcpkg-registry (#146)
- **core**: core/ installs as the cmake package hopnet-core, for its vcpkg port (#144)
- **core**: build core/ at C++23, the standard of the ns-3 release the org supports (#134)
- **core**: consume swarm-core by release: swarm-core.lock, URL + URL_HASH, ns-3 dependencies on tags with a canary on main (#133)
- **core**: query detection by overhearing and a random query-processing delay (#5 (s), (t)) (#115)
- **core**: no pheromone toward the next path node is a link failure (#112)
- **core**: zrp query detection and early termination for route discovery (#111)
- **core**: internal-ant period 2 s, discovery retry 3 s, neighbour loss 3 periods (#110)
- **bench**: benchmark pipeline with anthocnet beside hopnet, the smoke plan and the anchor pilot (#101)
- **ns3**: the hopnet contrib module on swarm-core: adapter, helper, arm, example, tests (#85)
- **observer**: report through swarm-core's router observer (#48)

### Fix

- **make**: the vcpkg branch of core-test clears a cached SWARM_CORE_DIR (#175)
- **core**: a zone node that moved away leaves the intrart after the neighbour timeout (#99)
- **ns3**: no udp peek past a short payload; subnet broadcasts are the base's (#98)

## v0.1.0 (2026-11-11)
### Release status

v0.1.0 is cut before the comparator gates (maintainer decision 2026-11-10). It has **not** cleared the three gates of org golden rule 6 (a fidelity sheet from its source, the per-PR delivery smoke, and an anchor reproducing the source's own trend). The anchor is pending, and so is the ns-3 contrib module `hopnet`: it is still being written, and an `ns3/` directory in this release's archive, if there is one, is that work in progress as it stood on `main`. No result from this release counts as a benchmark number, and it does not enter the benchmarks suite until a release clears the gates; that gated release is proposed as v0.2.0 (swarm-routing/.github ROADMAP "Releases"). It ships `core/` (MIT OR GPL-2.0-only, with `core/LICENSE`) and `docs/` (plus `ns3/` if it exists), as they stand on `main` at the release commit.


### Feat

- **core**: the local-repair slice — repair ant, notification and error ants (#37)
- **core**: the inter-zone half — bordercasting, external ants, interrt, data routing (#33)
- **core**: the intra-zone half — internal ants, intrart, eqs. 4.1-4.3, neighbour discovery (#25)
