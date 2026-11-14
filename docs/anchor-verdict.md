# The HOPNET anchor: written infeasibility verdict

**Status: verdict recorded (2026-11-14). The anchor's claim is not
reproduced.** Three pre-registered pilot rounds ran HOPNET beside AntHocNet
in the anchor's geometry. The last round ran in two scenario variants. Every
generation missed threshold A, while AntHocNet met threshold B in every one.
The maintainer approved the last round (2026-11-14) on the condition that a
miss in both of its generations publishes this verdict, and both missed. The
anchor itself was not run. HOPNET golden rule 12 and org golden rule 6 apply:

- this arm's numbers do **not** count in comparisons until an anchor passes;
- the implementation and every reading stay in the repository, behind their
  switches.

The pilot pre-registrations, results and decisions are in the
[#5](https://github.com/swarm-routing/HOPNET/issues/5) thread, linked below.
The reconstruction it tested is [`fidelity.md`](fidelity.md). Every number
here comes from the scripts (`bench_parse.py --all`, `ns3-campaign verdicts`)
on recorded runs. None was typed from a log.

[W07] is Wang's thesis, <https://mspace.lib.umanitoba.ca/bitstream/1993/20935/1/Wang_HOPNET_a.pdf>.
[DDG04] is AntHocNet's reference paper. Both are listed in
[`fidelity.md`](fidelity.md#sources).

## The claim tested

[W07] Fig. 5.11, p. 39: in [DDG04]'s scaling experiment, HOPNET (zone
radius 2) delivers more than AntHocNet at every scaling factor:

- about 0.97 at s = 1, about +0.01 over AntHocNet;
- about +0.035 over AntHocNet at s = 2.

The anchor ([`fidelity.md`](fidelity.md#anchor-candidate)) tests that trend
paired by seed at ≥ 20 seeds. Before spending the anchor's 20 seeds, each
pilot ran 5 seeds and checked two pre-registered thresholds
(https://github.com/swarm-routing/HOPNET/issues/5#issuecomment-6845052808):

- **A:** HOPNET's mean PDR at s = 1 ≥ 92 %, about 5 pp below the thesis's
  0.97, to allow for the reconstruction and the simulator port;
- **B:** AntHocNet's mean PDR at s = 1 ≥ 91 %, so the scenario and harness
  can deliver.

| A | B | Read |
|---|---|---|
| met | met | Go |
| met | not met | Go, with the flag |
| not met | met | No-go (reconstruction) |
| not met | not met | No-go (scenario) |

s = 2 is reported, not decisive.

## The pilot geometry

The pilots ran 20 CBR sources of one 64-byte packet per second, random
waypoint at U[1, 20] m/s with 30 s pauses, 900 s, a 300 m disk range, and
802.11 at 2 Mbit/s. They used routing-bench's `paper` scenario on
`ns3-dev:3.48-optimized` (`sha256:5585a93c…`). The areas were:

- s = 1: 50 nodes in 1500 m × 500 m;
- s = 2: 200 nodes in 3000 m × 1000 m.

The exceptions from [W07], each a row in [`fidelity.md`](fidelity.md):

- the traffic and radio are [DDG04] §4.1's, which [W07] does not restate;
- the disk range replaces two-ray ground;
- the s = 1 area: see `pilot-3-area300` below.

The comparator is swarm-routing/AntHocNet at `c3230ded` with its defaults.
#113 moves the pin to the v2.7.0 release; it was held until this round
was collected.

## What was tried

Every reading below was pre-registered on #5 before its code, and each pilot
before its dispatch.

| Readings | What they are | Where |
|---|---|---|
| (a)–(h) | The reconstruction from [W07] ch. 4: T′ of eq. 4.3, the bordercast skip, duplicates, external backward ants, neighbour discovery and loss, internal-ant movement, edge time, InterRT expiry, the inter-zone slice | [#5](https://github.com/swarm-routing/HOPNET/issues/5#issuecomment-6757235563), [#5](https://github.com/swarm-routing/HOPNET/issues/5#issuecomment-6781581190) |
| (i)–(m) | §4.3's local repair: repair, notification and error ants, the repair at a transit relay | [#5](https://github.com/swarm-routing/HOPNET/issues/5#issuecomment-6783813122), [#5](https://github.com/swarm-routing/HOPNET/issues/5#issuecomment-6784548894) |
| (n)–(r) | After `pilot-1`, cutting HOPNET's own control traffic: a 2 s internal-ant period, a 3 s discovery retry, neighbour loss after 3 periods, ZRP query detection and early termination, no pheromone toward the next Path node treated as a link failure | [ADR-0002](adr/0002-cut-control-traffic-after-pilot-1.md), [#5](https://github.com/swarm-routing/HOPNET/issues/5#issuecomment-6860655715), #109 |
| (s)–(t) | After `pilot-2`: ZRP's query detection by overhearing (QD2), and a random query-processing delay U[0, 0.1 s) before a bordercast | ADR-0002 amendment, [#5](https://github.com/swarm-routing/HOPNET/issues/5#issuecomment-6867580858) |
| Scenario | The thesis's printed s = 1 area, 1500 m × 300 m (`pilot-3-area300`) | [#5](https://github.com/swarm-routing/HOPNET/issues/5#issuecomment-6870386731), #119 |

## Evidence

All pilots ran 5 seeds with `--protocols=hopnet,anthocnet` in one invocation
per seed. Each value is the 5-seed mean ± 95 % CI half-width. AntHocNet's
cells are byte-identical across `pilot-1`, `pilot-2` and `pilot-3` (same pin,
same seeds).

| Generation | HOPNET commit | s = 1 HOPNET (A ≥ 92 %) | s = 1 AntHocNet (B ≥ 91 %) | s = 2 HOPNET | s = 2 AntHocNet | Read | Results |
|---|---|---|---|---|---|---|---|
| `pilot-1` | (a)–(m) | 37.36 ± 5.94 | 97.42 ± 1.01 | 7.06 ± 1.84 | 83.58 ± 8.32 | No-go (reconstruction) | [#5](https://github.com/swarm-routing/HOPNET/issues/5#issuecomment-6858292616) |
| `pilot-2` | `25bfa385`, + (n)–(r) | 89.34 ± 2.10 | 97.42 ± 1.01 | 19.46 ± 4.34 | 83.58 ± 8.32 | No-go (reconstruction) | [#5](https://github.com/swarm-routing/HOPNET/issues/5#issuecomment-6867103023) |
| `pilot-3` | `61042abd`, + (s)–(t) | **91.92 ± 1.50** | 97.42 ± 1.01 | 25.78 ± 3.06 | 83.58 ± 8.32 | No-go (reconstruction) | [#5](https://github.com/swarm-routing/HOPNET/issues/5#issuecomment-6873247451) |
| `pilot-3-area300` | `61042abd`, at 1500 m × 300 m | **91.78 ± 1.59** | 97.30 ± 0.34 | — | — | No-go (reconstruction) | [#5](https://github.com/swarm-routing/HOPNET/issues/5#issuecomment-6873247451) |

Paired by seed, HOPNET − AntHocNet in the last round (`ns3-campaign
verdicts`, `PAIRED-WORSE` in every cell):

| Cell | dPDR | d(99th-percentile delay) | dNRL |
|---|---|---|---|
| `pilot-3`, s = 1 | −5.50 pp [−6.58, −4.42] | +1466 ms | +35.7 |
| `pilot-3`, s = 2 | −57.80 pp [−63.96, −51.64] | +1361 ms | +1148 |
| `pilot-3-area300`, s = 1 | −5.52 pp [−7.12, −3.92] | +1754 ms | +34.6 |

What the readings did, as reported (the `--diag` lines on #5, not decisive):

- From `pilot-1` to `pilot-3`, HOPNET's s = 1 PDR rose from 37 % to 92 %, and
  its NRL fell from about 329 to 46.
- In `pilot-3`, HOPNET's s = 1 losses are spread over `route` (4.6–7.3 %) and
  `mac` (5.8–7.2 %) drops. At s = 2, `route` drops are still 48–58 % and
  `chan` drops 17–26 %, with 5.1–5.3 M control transmissions per seed and a
  median route setup of 4.9–52 s. Discovery at scale is what fails.

Why each generation is read as it is:

- **`pilot-3`** misses A by 0.08 pp on the mean, its CI straddling 92 %.
- **`pilot-3-area300`**, the thesis's printed geometry, is within noise of
  `pilot-3`: the s = 1 area does not explain the gap.
- **B holds everywhere**, so by the table the miss is the reconstruction's,
  not the scenario's or the harness's.
- **The comparison runs the wrong way.** In the same cells HOPNET is about
  5.5 pp **below** AntHocNet at s = 1, and 58 pp below at s = 2. The thesis
  reports it about 1 pp and 3.5 pp **above**. The anchor's trend, a lead that
  grows with scale, is not approached.

**One deviation in `pilot-3-area300`.** org-bot's swarm-core pin bumps
(#122, #125) merged on `main` while the round was in flight. Its
cells were built on swarm-core `6419567d` (s1–s3) and `e20aad3f` (s4–s5),
not the pre-registered `69664219`. Local runs on the pilot's image settle
it. Seed 1 on `69664219` and on `e20aad3f` is byte-identical to the CI cell
built on `6419567d`, and seed 4 on `69664219` to the CI cell built on
`e20aad3f`. That holds for every line except `##PERF##`, `##PROV##` and
`##ARGS##`, so the results are those of the pre-registered set. The details
are in the results comment.

## What the thesis leaves unrecoverable

Each of these had to be filled by a repository choice
([`fidelity.md`](fidelity.md), the Unknowns section and the rows it cites),
and any one of them could hold the missing points:

- the internal forward ant's period ([W07] p. 24: "periodically");
- the neighbour-loss rule (none given);
- the next-hop selection rule and its exponent β (a declared deviation: ACO
  roulette, β = 20);
- the edge-time measure w (p. 26: "the time period on each edge", no
  measure);
- query control beyond p. 28's bordercast skip;
- the local-repair waits and buffers, and the discovery retry policy;
- the traffic, radio range and propagation of the AntHocNet comparison
  (p. 36 says only that AntHocNet's set-up was reused);
- the simulator port: GloMoSim, with no source released;
- the AntHocNet configuration [W07] ran against (no source, no parameters).

[W07] is also internally inconsistent on the s = 1 area: 1500 m × 300 m,
whose doubling is not the stated s = 2 end point. Both readings have now
been run.

## Verdict

**HOPNET's anchor, [W07] Fig. 5.11, is not reproduced by this
reconstruction.** HOPNET ships as an arm whose anchor is recorded as
infeasible:

- its numbers do not count in comparisons (org golden rule 6);
- the gap is stated here with its evidence;
- every reading remains selectable through its `core::Config` switch for
  anyone who wants to try another.

The maintainer's remark of 2026-11-13 is part of the record: these protocols
"were made for very specific cases and maybe our scenarios are not the strong
scenario for them."

## What follows

- **Reopening.** A new pre-registered anchor would need a source that settles
  an item of the list above. [W09], the journal version, has not been read
  ([`fidelity.md`](fidelity.md#sources)); reviewing the sheet against it once
  it is in hand is #21. No pilot or anchor is planned.
- **#113** (AntHocNet pinned at v2.7.0) is no longer an anchor
  prerequisite; it lands as a plain pin bump.
- **Process.** A dependency pin bump that merges while a campaign is in
  flight changes the later cells. It is noted on #5 for follow-up.
