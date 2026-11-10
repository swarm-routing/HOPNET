# HOPNET: fidelity sheet

What the thesis specifies, what this implementation is to do, and where they
differ. Every row cites its source (`[KEY] §x`, `Fig. N`, `eq. N`, `p. N`), or
says `repo choice: <reason>`, or `unknown`. Checked by `swarm-routing/.github`
`tools/fidelity/fidelity.py check`.

`core/` is not written yet (HOPNET golden rule 11: fidelity sheet before code).
Until it is, the **Implementation** column records what the fresh
implementation is to do, and **Status** says how that plan stands against the
thesis. Section, equation and figure numbers are the thesis's; **page numbers
for [W07] are the thesis's printed page numbers** (the PDF page is the printed
page + 8). The thesis PDF is a scan with an OCR layer; every value below was
read from the page image, not only from the OCR text.

## Sources

| Key | Citation | Access |
|---|---|---|
| W07 | J. Wang, *HOPNET: A Hybrid ant colony OPtimization routing algorithm for Mobile ad hoc NETwork*, MSc thesis, University of Manitoba, December 2007 (MSpace hdl 1993/20935). The primary source: the only HOPNET text the org holds in full | in hand |
| W09 | J. Wang, E. Osagie, P. Thulasiraman, R. K. Thulasiram, *HOPNET: A hybrid ant colony optimization routing algorithm for mobile ad hoc network*, Ad Hoc Networks 7(4):690–705, 2009, DOI 10.1016/j.adhoc.2008.06.001 (Crossref and Semantic Scholar: 2009; OpenAlex gives 2008, the online-first year). The journal version. **Not held** (Elsevier, paywalled; listed in the papers library's MANUAL-DOWNLOADS.md). No row cites it | institution |
| DDG04 | G. Di Caro, F. Ducatelle, L. M. Gambardella, *AntHocNet: An Ant-Based Hybrid Routing Algorithm for Mobile Ad Hoc Networks*, Parallel Problem Solving from Nature – PPSN VIII, LNCS 3242, pp. 461–470, Springer, 2004, DOI 10.1007/978-3-540-30217-9_47 (verified against Crossref). AntHocNet's reference paper, and the source of the AntHocNet experiments [W07] §5.4 compares against (see *The comparison the thesis ran*) | in hand |
| DDG05 | G. Di Caro, F. Ducatelle, L. M. Gambardella, *AntHocNet: an adaptive nature-inspired algorithm for routing in mobile ad hoc networks*, European Transactions on Telecommunications 16(5):443–455, 2005, DOI 10.1002/ett.1062 (verified against Crossref; [W07]'s bibliography entry [5] gives the issue as 16(2)). The paper [W07] cites as its AntHocNet reference. Held as its preprint, IDSIA Technical Report IDSIA-27-04-2004; locators below are the preprint's | in hand |

## Which version of HOPNET

Two texts describe HOPNET:

- **The thesis**, [W07] ch. 4 (the algorithm) and ch. 5 (the evaluation in
  GloMoSim against AODV, AntHocNet and ZRP). This sheet is written from it;
  every row is the thesis's unless it says otherwise.
- **The journal paper**, [W09] (2009, four authors). It is the published,
  peer-reviewed version and may state what the thesis leaves out (the
  next-hop selection rule, timer values, ε). We do not hold it. Before `core/`
  is written, someone with Elsevier access should read [W09] against the
  `unknown` rows below; any row it settles gets a `[W09]` source, and any
  place where it disagrees with [W07] needs a stated choice of version
  (pre-registered in an issue).

There is no AntHocNet arm to compare with: HOPNET was never implemented in
AntHocNet (CONTEXT.md), so there is no *Differences from* section.

## Mechanisms

| Mechanism | Source | Implementation | Status |
|---|---|---|---|
| Zones: a node's routing zone is itself and every node within the zone radius, measured in hops; zones overlap and can differ in size | [W07] p. 23 | Zone membership from the hop counts the intra-zone ants record | as written |
| Peripheral (boundary) nodes are exactly the radius away; interior nodes are closer; all others are exterior | [W07] p. 23, p. 24, Fig. 4.1 | Peripheral iff the IntraRT `hops` field equals the radius | as written |
| Two routing tables per node: a proactive Intrazone Routing Table (IntraRT) and an on-demand Interzone Routing Table (InterRT) | [W07] §4.1 | Two tables in `core/`; the pheromone one may be `swarm-core`'s `PheromoneTable` if it fits | as written |
| IntraRT: rows are the node's one-hop neighbours, columns the nodes of its zone; each cell holds ⟨pheromone, times visited, hops, SeqNo⟩ | [W07] p. 25 | Table keyed (neighbour, zone node) with those four fields | as written |
| IntraRT "times visited" field: defined, never used by any rule in ch. 4 | [W07] p. 25 | Kept as a counter, read by nothing | unknown: [W07] gives no rule that reads it |
| IntraRT `SeqNo` stops the same forward ant from updating the table twice | [W07] p. 25 | Per-cell last sequence number; a repeat is ignored | as written |
| Ant header: Source, Destination, SequenceNo, Type, Hops, Path, Time; five types: 0 internal forward, 1 external forward, 2 backward, 3 notification, 4 error | [W07] p. 25 | One POD ant header, little-endian, leading version byte (org rule 3); the field widths are ours | as written (layout: repo choice under org rule 3) |
| (Source, SequenceNo) identifies an ant generation; a node increments its sequence number for every forward or backward ant it sends; duplicates are discarded | [W07] p. 25, p. 27 | Bounded (source, seq) history (org rule 7) | as written |
| Internal forward ants are sent periodically by every node to all its neighbours, with Hops set to the zone radius; they sample paths inside the zone | [W07] §4.1, p. 24, p. 25 | Periodic timer through an injected timer port; Hops = radius | as written (period open, below) |
| How an internal forward ant moves past the first hop (copied to every neighbour, or one next hop chosen by pheromone) | [W07] p. 24, p. 26 | To be chosen | unknown: "sends out internal forward ants periodically to all its neighbors" covers the first hop only; the next-hop rule that would govern later hops is not given (row below) |
| Internal backward ant returns along the forward ant's Path to its source | [W07] §4.1, p. 26 | Unicast back along the recorded path | as written |
| Neighbour discovery by the internal forward and backward ants themselves; no separate hello message is described | [W07] p. 23 | Neighbours learned from ant traffic only | unknown: [W07] does not say how a node learns its neighbours before its first ant returns, nor when a silent neighbour is dropped |
| Forward-ant pheromone update at the node v_j an ant moves to from v_i, entry (v_i, S): eq. 4.1, φ += ε / (T(v_S, v_i) + w(v_i, v_j)), the "source update" of Islam et al. ([W07] ref. [19]) | [W07] eq. 4.1, p. 26 | Same law | as written (ε and the measure of w open, below) |
| Evaporation: every other neighbour's entry in the same column is multiplied by (1 − E), eq. 4.2; it runs on each ant update, not on a timer | [W07] eq. 4.2, p. 26 | Applied inside the update, no periodic decay | as written |
| Each ant accumulates the path time T(v_S, v_i) + w(v_i, v_j) as it moves | [W07] p. 26 | Time field of the ant header | as written |
| Backward-ant update at v_b for the destination column: eq. 4.3, φ(v_b, v_D) += ε / T′ with T′ = T(v_k, v_D) − T(v_k, v_b); other neighbours in column D decay by eq. 4.2 | [W07] eq. 4.3, p. 26 | Same law | unknown: the subscripts of T′ admit two readings (the time still to go from v_b to D, or the time already covered); [W07] says only that it puts more pheromone "on the path that is closer to the destination". Pre-register the reading |
| Next-hop selection rule (for ants and for data) | [W07] p. 25, p. 26 | To be chosen | unknown: p. 25 announces "the pheromone (φ) update and node selection procedure", but only the updates (eqs. 4.1–4.3) follow; no selection rule appears in [W07]. The first thing to look for in [W09] |
| Data to a destination inside the zone: found in the IntraRT, and a route always exists because the table is proactive | [W07] p. 25 | IntraRT lookup | as written; how data picks among the neighbours is the row above |
| A data packet is dropped at a node whose pheromone toward the destination is zero on every link | [W07] p. 34 | Drop and count it | as written |
| InterRT: Destination, SeqNo, Path, Expire; a source route kept until it expires | [W07] p. 27 | Source-route cache with an expiry time | as written (expiry open, below) |
| Data to a destination outside the zone: an unexpired InterRT path is used at once and the packet is source-routed ("self-directed") along it; otherwise route discovery starts | [W07] p. 27 | Source route carried in our data header | as written |
| Route discovery between zones (bordercasting): the source sends an external forward ant to each of its peripheral nodes along IntraRT paths; a peripheral node looks the destination up in its IntraRT, then its InterRT, and otherwise forwards the ant to its own peripheral nodes; repeated until the destination is found | [W07] p. 27, p. 28, Fig. 4.1 | Same | as written |
| Query control: a peripheral node does not bordercast back toward the zone the ant came from, so ants move away from the source | [W07] p. 28, Fig. 4.1 | To be chosen | unknown: the worked example (D does not send to S and A) states the effect, not the rule. Which peripheral nodes are excluded (those in the previous bordercaster's zone, those on the ant's Path, or those the previous node also reaches) must be pre-registered |
| An external forward ant dies at a peripheral node with no peripheral nodes of its own to send to | [W07] p. 28 | Drop | as written |
| External forward ants carry no hop limit (Hops left blank) | [W07] p. 25 | No TTL beyond duplicate suppression | as written; a safety cap is a repo choice (org rule 7) |
| Each node a bordercast passes appends itself to Path; the peripheral node that finds the destination in its zone sends the ant on along its IntraRT path | [W07] p. 28 | Same | as written |
| At the destination: if the ant's sequence number is greater than the one its InterRT holds "for destination", the ant becomes a backward ant (type 1 → 2) and returns along Path; otherwise it dies | [W07] p. 27 | One backward ant per (source, seq) | unknown: which InterRT entry the destination compares against (the source's, as the purpose suggests, or literally the destination's own) is not settled by the text. Pre-register |
| The source inserts the backward ant's path in its InterRT and sends data along it | [W07] p. 27 | Same | as written |
| Whether external backward ants update any pheromone | [W07] p. 27 | To be chosen | unknown: §4.2 describes only the InterRT insertion |
| Multiple paths: HOPNET "is able to find multiple paths" from a source to a destination | [W07] p. 28 | To be chosen | unknown: how many InterRT paths are kept per destination and how data chooses among them is not stated |
| Intra-zone link failure: repaired by the proactive IntraRT maintenance after a short period | [W07] §4.3 | Nothing beyond the periodic ants and eq. 4.2 | as written |
| Inter-zone link failure: the upstream node buffers packets and repairs locally by sending a forward ant to the destination | [W07] §4.3, p. 28, p. 29, Fig. 4.3 | Local repair with a bounded buffer | as written (buffer and wait open, below) |
| Repair succeeds: buffered packets go on the new route; a notification ant (type 3) tells the source, nodes it visits remove invalid InterRT routes, and the source replaces its path | [W07] p. 29, Fig. 4.2 | Same | as written |
| Repair fails (no backward ant "after a period of time"): an error ant (type 4) goes to the source, which restarts route discovery if it still needs the route | [W07] p. 29, p. 30, Fig. 4.3 | Same | as written (the period open, below) |
| How a broken link is detected | [W07] §4.3 | MAC transmit failure from the adapter, through an `ILinkState` port | unknown: [W07] names the event ("a link being otherwise broken"), not its detection |

## Parameters

| Parameter | Value | Source | Notes |
|---|---|---|---|
| Zone radius (hops) | 2 | [W07] p. 36, p. 37 | The radius of the AntHocNet comparison ("a constant zone radius of 2", Fig. 5.7, Fig. 5.8) |
| Zone radius, other values studied | 2, 3, 4, 5 | [W07] Fig. 5.4, Fig. 5.5, Fig. 5.6 | Sweep against network size (20, 50, 100, 200 nodes); radius 4 in Fig. 5.9 and Fig. 5.10 |
| Zone radius in the AODV comparisons (Fig. 5.1–5.3, 5.12–5.14) | — | unknown | Not stated |
| Internal forward ant hop limit | the zone radius | [W07] p. 25 | |
| External forward ant hop limit | none | [W07] p. 25 | "We leave this field blank"; duplicates bound the flood |
| Internal forward ant period | — | unknown | "periodically" ([W07] p. 24); the thesis's main proactive rate |
| ε, the deposit scale in eqs. 4.1 and 4.3 | — | unknown | "a run time parameter provided by the user" ([W07] p. 26), no value |
| Evaporation rate E, range found suitable | 0.20–0.25 | [W07] p. 33 | "We found the evaporation rate of 20% to 25% to be suitable" |
| Evaporation rate E, value used in the figures | — | unknown | Only the range is given |
| Evaporation rate E, a value that fails | 0.10 | [W07] p. 33 | Too slow: "the ants travel in a cycle" |
| Edge time w(v_i, v_j) | — | unknown | "the time period on each edge" ([W07] p. 26); how it is measured (per-hop delay of the ant, a MAC estimate) is not given |
| Initial pheromone of a new IntraRT cell | — | unknown | |
| InterRT Expire time | — | unknown | "for a certain period of time" ([W07] p. 27) |
| Local-repair wait before the error ant | — | unknown | "after a period of time" ([W07] p. 29) |
| Local-repair buffer (packets, time) | — | unknown | Must be bounded (org rule 7) |
| Route-discovery retry policy at the source | — | unknown | Only "re-initiate" after an error ant ([W07] p. 29) |
| Duplicate-suppression history size | — | unknown | Must be bounded (org rule 7) |
| Simulator | GloMoSim (on PARSEC) | [W07] §5.1 | Version not stated; HOPNET implemented at the network layer |
| AODV base scenario: nodes, area | 50, 3000 m × 1000 m | [W07] §5.2 | Varied 10–190 nodes in Fig. 5.1–5.3 (area for those not restated) |
| AODV base scenario: mobility | random waypoint, speed U[0, 10] m/s | [W07] §5.2 | Pause time not stated for the base scenario |
| AODV base scenario: simulated time | 900 s | [W07] §5.2 | |
| AODV base scenario: traffic | 20 CBR sources, one 64-byte packet every 3 s | [W07] §5.2 | Start times not stated |
| AODV base scenario: radio | 802.11, range 250 m, 2 Mbit/s | [W07] §5.2 | Propagation model not stated |
| Runs or seeds per point, confidence intervals | — | unknown | Not stated anywhere in ch. 5; the figures show single curves without error bars |
| AntHocNet comparison: base scenario | 50 nodes, 1500 m × 300 m, random waypoint up to 20 m/s, pause 30 s, 900 s | [W07] p. 36 | "We used the same simulation set up used in AntHocNet [5]" |
| AntHocNet comparison: area sweep | 1500 m to 2500 m long side, 300 m short side | [W07] p. 36, Fig. 5.7 | As [DDG04] Fig. 1 |
| AntHocNet comparison: pause sweep | 0, 10, 30, 70, 100, 200, …, 900 s | [W07] Fig. 5.8, Fig. 5.9, Fig. 5.10 | The area of this sweep is not stated ([DDG04] §4.2 used 2500 m × 300 m) |
| AntHocNet comparison: scaling sweep | scaling factor 1 to 2; edges × factor, nodes × factor², up to 200 nodes in 3000 m × 1000 m | [W07] p. 39, Fig. 5.11 | [W07] starts from 1500 m × 300 m, which × 2 gives 3000 m × 600 m, not the stated end point; [DDG04] §4.2 starts from 1500 m × 500 m, which does. Reading for the anchor: [DDG04]'s |
| AntHocNet comparison: traffic, radio range, propagation, MAC | — | unknown | Not restated in [W07]. [DDG04] §4.1: 20 CBR sources, one 64-byte packet per second, starting in [0, 180] s; two-ray propagation; 300 m range; 2 Mbit/s; 802.11 DCF. [W07]'s own base scenario uses 250 m and one packet every 3 s |
| AntHocNet implementation and its parameters | — | unknown | [W07] names none: no simulator port, no source, no parameter. See *The comparison the thesis ran* |
| ZRP comparison scenario | 200 nodes, 5000 m × 1250 m, random waypoint 20 m/s, range 250 m | [W07] p. 42, Fig. 5.15 | From [W07] ref. [45]; ZRP is not an org arm |

## The comparison the thesis ran

**Simulator.** HOPNET was simulated in GloMoSim, version not stated
([W07] §5.1). AntHocNet's own experiments ([DDG04] §4.1, [DDG05] §4.1) used
QualNet.

**Scenarios.** [W07] §5.4 (p. 36) states the base scenario (50 nodes,
1500 m × 300 m, 20 m/s, pause 30 s, 900 s) and says it reuses AntHocNet's
setup from its reference [5], which is [DDG05]. [DDG05] §4.1 does **not**
contain these experiments (its scenarios are 100 nodes in 3000 m × 1000 m and
a 100–1500-node scalability study); they are the three experiments of
[DDG04] §4.2: the long side 1500 → 2500 m ([DDG04] Fig. 1), the pause time
0–900 s ([DDG04] Fig. 2), and the scaling factor 1 → 2 up to 200 nodes in
3000 m × 1000 m ([DDG04] Fig. 3). [W07] p. 39 restates [DDG04] §4.2's
description of the scaling experiment almost word for word.

**AntHocNet's implementation and parameters.** [W07] does not say that
AntHocNet was implemented or run in GloMoSim, and gives no AntHocNet
parameter. The AntHocNet curves in [W07] match [DDG04]'s published AntHocNet
curves, read from both plots:

| [W07] figure (AntHocNet curve) | [DDG04] figure (AntHocNet curve) |
|---|---|
| Fig. 5.7: about 0.965 at 1500 m, a drop near 2300 m, about 0.893 at 2500 m | Fig. 1: about 0.968 at 1500 m, about 0.903 at 2300 m, about 0.895 at 2500 m |
| Fig. 5.11: about 0.961 at factor 1, about 0.914 at factor 2 | Fig. 3: about 0.962 at factor 1, about 0.913 at factor 2 |
| Fig. 5.8 and p. 37: rises from 10 to 30 s, falls until 70 s, then climbs to about 0.98 at 900 s | Fig. 2: about 0.875 at 0 s, a peak near 30 s, a dip near 60 s, about 0.99 at 900 s |

The reading this supports, which the text neither states nor rules out: the
AntHocNet numbers are **[DDG04]'s QualNet results, transcribed**, not a
paired run beside HOPNET. If so, AntHocNet's configuration is recoverable
only as "the IDSIA implementation of [DDG04], with [DDG04]'s parameters, in
QualNet", and the two arms differed in simulator, radio model and very
likely traffic and range. [W09] may say more; until someone reads it, the
thesis's AntHocNet configuration is **unrecoverable from [W07]** beyond the
scenario.

**The headline trend.** The abstract and ch. 6 claim HOPNET is "highly
scalable for large networks compared to AntHocNet"; the figure is
Fig. 5.11 (p. 43): HOPNET's delivery ratio stays about 0.97 to factor 1.6 and
ends about 0.95 at factor 2, AntHocNet's falls from about 0.96 to about 0.91,
so the gap widens from about +0.01 to about +0.035 (read from the plot). Two
other comparisons cut the other way and belong in the record: in the area
sweep (Fig. 5.7) HOPNET delivers **less** than AntHocNet at every length, and
in the pause sweep with radius 2 (Fig. 5.8) AntHocNet is ahead at long pause
times; HOPNET leads there only with radius 4 between 70 and 400 s
(Fig. 5.9, Fig. 5.10, p. 38).

**Feasibility.** The trend can be put to a paired test in ns-3, but not
reproduced as the thesis measured it:

- *Scenario*: recoverable, as [DDG04] §4.1–§4.2's scaling experiment, which
  the thesis claims to reuse. Where [W07] and [DDG04] disagree (start area,
  range, traffic), take [DDG04]'s values as a repo choice and say so.
- *AntHocNet*: `swarm-routing/AntHocNet` at a pinned commit, whose reference
  paper is [DDG04]. That is closer to the AntHocNet in [W07]'s figures than
  anything [W07] itself specifies, but it is not the same implementation and
  not the same simulator; agreement is judged on direction, not values.
- *HOPNET*: blocked on its own unknowns, chiefly the next-hop selection rule,
  the internal ant period, ε, the edge-time measure, the query-control rule
  and the InterRT expiry. Each needs [W09] or a pre-registered repo choice
  before `core/`.

So the arm does **not** ship as an infeasibility verdict on the AntHocNet
side: the anchor below is testable. If [W09] is read and still leaves the
selection rule open, the HOPNET implemented here is a reconstruction, and the
anchor tests that reconstruction; say so wherever its numbers appear.

## Anchor candidate

| Trend | Scenario | Metric | Comparators | Source |
|---|---|---|---|---|
| HOPNET (zone radius 2) delivers more than AntHocNet at every scaling factor, and its lead grows with scale: about +0.01 at factor 1, about +0.035 at factor 2 in the thesis | [DDG04]'s scaling experiment, which [W07] reuses: scaling factor s ∈ {1, 1.25, 1.5, 1.75, 2}, 50·s² nodes in (1500·s) m × (500·s) m, random waypoint up to 20 m/s, pause 30 s, 900 s, 20 CBR sources of one 64-byte packet per second, 802.11, 2 Mbit/s, 300 m range (the [DDG04] values where [W07] is silent or inconsistent: repo choice) | Paired by seed, HOPNET minus AntHocNet delivery ratio, ≥ 20 seeds per point: 95 % CI above zero at s = 2, and the s = 2 delta larger than the s = 1 delta (paired difference of deltas, CI above zero). Delay and overhead reported, not gated ([W07] gives neither for this experiment) | AntHocNet (`swarm-routing/AntHocNet` at a pinned commit, defaults stated with the run) | [W07] Fig. 5.11, p. 39; [DDG04] §4.2, Fig. 3 |

Not gated, recorded as checks on the reconstruction: Fig. 5.7 (HOPNET below
AntHocNet as the area lengthens) and Fig. 5.10 (radius 4 ahead of AntHocNet
for pause 70–400 s). A HOPNET that beats AntHocNet in Fig. 5.11's scenario
but not where the thesis says it lost is worth a second look before anyone
calls the anchor met.

## Unknowns

The rows marked `unknown` above, in the order they block `core/`:

1. Next-hop selection rule for ants and data (announced on [W07] p. 25, never given).
2. How internal forward ants move beyond the first hop.
3. Internal forward ant period.
4. ε (eqs. 4.1, 4.3) and the edge-time measure w.
5. The reading of T′ in eq. 4.3.
6. The query-control rule (which peripheral nodes a bordercast skips).
7. Which sequence number the destination compares an external forward ant with.
8. Whether external backward ants deposit pheromone; how many InterRT paths are kept and used.
9. Initial pheromone, InterRT expiry, local-repair wait and buffer, discovery retries, history sizes.
10. Neighbour discovery and loss, and link-break detection.
11. The evaporation rate actually used (0.20–0.25 found suitable).
12. The thesis's AntHocNet implementation and parameters, its traffic and radio settings, and its runs per point.

[W09] is the first place to look for 1–11.
