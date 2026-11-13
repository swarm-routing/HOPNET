# 0002. Cut HOPNET's control traffic after the pilot-1 NO-GO

- **Status:** accepted
- **Date:** 2026-11-13
- **Issue:** #5 (readings (n)–(r)), #109

## Context

The anchor pilot `pilot-1` (#5, pre-registered before dispatch) ran HOPNET
beside AntHocNet in the anchor's geometry ([W07] Fig. 5.11, [DDG04] §4.2),
5 seeds at s = 1 and s = 2. AntHocNet met its threshold (B); HOPNET missed
its own (A: mean PDR at s = 1 ≥ 92 %, against the thesis's about 0.97).
Read by the pre-registered table, the verdict was **NO-GO (reconstruction)**.
The results are in the issue thread, not here.

The investigation in #109 found no code defect: `core/` and the ns-3
adapter send what `docs/fidelity.md` and the accepted readings (a)–(m) say.
The cause was HOPNET's own control traffic under those readings:

- The internal forward ant ran at a 1 s period. It is a radius-2 flood plus
  a backward ant from every node where the hop budget ends, so it costs
  1 + N₁ + 2·N₂ transmissions per node per period.
- Discovery was retried every 1 s, shorter than a discovery took under load.
- A neighbour was lost after 2 missed periods, so two broadcasts lost in a
  congested channel removed a live link.
- Reading (b)'s query control let every bordercaster bordercast every
  discovery to all its peripheral nodes, and relays never stopped a copy. A
  discovery cost hundreds to thousands of external-ant transmissions, and at
  s = 2 the external forward ants were most of the control load.
- A Path node with no pheromone toward the next Path node dropped the
  packet ([W07] p. 34), and the source kept its unusable record.

The MAC queues saturated, discoveries failed, and held packets expired.
#109 has the counts and the local diagnostic runs. Each of the five was a
judgement call, a repo choice or a repo reading, not a fix, so it was the
maintainer's to make.

## Decision

On 2026-11-13 the maintainer accepted the five recommendations of #109.
They are pre-registered on #5 as readings (n)–(r) before any code
(<https://github.com/swarm-routing/HOPNET/issues/5#issuecomment-6860655715>).
Each is the default, and each is recorded in `docs/fidelity.md` with its
old value and its evidence:

1. **(n)** `Config::internalAntPeriod`: 1 s → **2 s** (repo choice; [W07]
   p. 24 says "periodically").
2. **(o)** `Config::discoveryRetryInterval`: 1 s → **3 s**, the 3 s hold
   (repo choice; [W07] p. 29 says "re-initiate").
3. **(p)** `Config::allowedAntLoss`: 2 → **3**. With (n), the neighbour
   timeout, and the stale IntraRT cell rule (#93) that follows it, go from
   2 s to 6 s (repo choice; [W07] gives no loss rule).
4. **(q)** On top of (b), **ZRP's query detection and early termination**
   (Haas & Pearlman 2001, <https://doi.org/10.1109/90.944341>; the BRP
   draft, <https://datatracker.ietf.org/doc/draft-ietf-manet-zone-brp/>
   §3–§4):
   - every node that handles an external forward ant records its query
     with the bordercaster the first copy came from;
   - a transit relay drops a bordercast copy from another bordercaster,
     while one bordercast's own branches, and copies directed at the
     destination or following a cached InterRT route, go on
     ([clarified in review](https://github.com/swarm-routing/HOPNET/issues/5#issuecomment-6861246132));
   - a peripheral node so covered still looks the destination up but does
     not bordercast.

   This is a repo reading and a deviation in cost from [W07] p. 28, which
   names only the skip. `Config::queryDetection` turns it off.
5. **(r)** Inter-zone data with **no pheromone toward its next Path node is
   handled as a failed transmission** (#5 (e)): the source drops its record
   and rediscovers, and a Path node or transit relay starts the local
   repair of (i)–(m). This is a repo reading and a deviation from the drop
   of [W07] p. 34; intra-zone data is still dropped.
   `Config::noPheromoneIsLinkFailure` turns it off.

None of these changes the wire format. The next pilot, `pilot-2`, keeps
`pilot-1`'s image digest, dependency set (AntHocNet `c3230ded`), geometry,
seeds, thresholds A and B, and go/no-go table unchanged, so the two
generations differ only in HOPNET's commit.

## Alternatives considered

- **Keep the 1 s period and retry, and fix only query control.** Rejected.
  Internal ants alone saturated the channel at s = 1, where external ants
  were the smaller share.
- **A longer period (5 s).** Rejected: it measured worse than 2 s, because
  the neighbour and stale-cell timeouts scale with the period and data was
  routed through stale cells.
- **ZRP's skip rule in place of (b)** (skip peripheral nodes inside the
  previous bordercaster's zone). Rejected again, as in (b): it needs the
  extended (2r − 1) zone HOPNET does not keep.
- **Detection by overhearing (QD2) and the random query-processing delay.**
  Deferred here; adopted after `pilot-2` as readings (s) and (t) (see the
  amendment below). QD2 needs promiscuous reception of unicast ants in the adapter,
  and the delay slows every discovery. Both remain candidates if (q) is not
  enough.
- **Dropping duplicates at every transit relay.** Rejected, as in (b): it
  kills the source's own bordercast when two peripheral nodes share a first
  hop. (q) terminates only copies from another bordercaster.
- **No reading change, a written infeasibility verdict (golden rule 12).**
  Premature: the gap traced to parameters and readings that the thesis
  leaves open, not to anything the thesis states.

## Consequences

- HOPNET's defaults now differ from the first reconstruction in two
  readings, (q) and (r), that go beyond [W07]. Each is behind a `Config`
  switch, so an A/B against the old reading needs no code. A result that
  depends on them says so.
- The neighbour and stale-cell timeouts are now 6 s, so a vanished zone
  node is kept up to 6 s instead of 2 s. (r) catches data sent toward it
  in that window.
- `pilot-2` is read against the unchanged pre-registered thresholds. If it
  is NO-GO again, the next step is a new reading or an infeasibility
  verdict, pre-registered on #5, not a threshold change.
- The anchor's AntHocNet pin moves to the v2.7.0 release commit in its own
  change before the anchor, not in this decision, so `pilot-1` and
  `pilot-2` stay comparable.

## Amendment, 2026-11-13: readings (s) and (t) after the `pilot-2` NO-GO

`pilot-2` was NO-GO again (#5): HOPNET's PDR at s = 1 stayed under
threshold A, and at s = 2 discoveries still failed. The maintainer approved
the two candidates deferred above. They are pre-registered on #5 before any
code (<https://github.com/swarm-routing/HOPNET/issues/5#issuecomment-6867580858>),
are the defaults, and are recorded in `docs/fidelity.md` ("Query control",
"Query-processing delay"):

1. **(s) Query detection by overhearing, ZRP's QD2.** The ns-3 adapter
   receives promiscuously and hands every HOPNET ant addressed to another
   node to `InterZone::overhear`, which records a bordercast copy's query
   with its previous bordercaster, as a transit relay does under (q). The
   adapter decides nothing; the core sends nothing and changes no table.
   `Config::queryOverhearing` turns it off.
2. **(t) A random query-processing delay before a bordercast** (the BRP
   draft §4 and Appendix E.2, `RELAY_JITTER`): a bordercaster addressed by
   a copy that found no route and is not covered waits U[0, 0.1 s), drawn
   from the node's `IRng`, before it bordercasts; the query's origin never
   waits. In the wait, a copy detected from another bordercaster cancels
   the bordercast, and the Path nodes of the copies it sees are skipped as
   peripheral nodes. `Config::queryProcessingDelay = 0` turns it off.

Both are repo readings and deviations in cost from [W07] p. 28, as (q) is.
Neither changes the wire format. With both off, HOPNET is exactly as at
`pilot-2`'s commit. `pilot-3` keeps `pilot-2`'s image, dependency set,
geometry, seeds, thresholds and go/no-go table, so it differs from
`pilot-2` only in HOPNET's commit. If `pilot-3` is NO-GO too, a written
infeasibility verdict (golden rule 12) is drafted on #5 for the maintainer
to decide on; the thresholds do not change.
