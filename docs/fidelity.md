# HOPNET: fidelity sheet

What the thesis specifies, what this implementation is to do, and where they
differ. Every row cites its source (`[KEY] §x`, `Fig. N`, `eq. N`, `p. N`), or
says `repo choice: <reason>`, or `unknown`. Checked by `swarm-routing/.github`
`tools/fidelity/fidelity.py check`.

`core/` is being written in slices (#5): the intra-zone half
(`core/hopnet-intrazone.{h,cc}`: the internal ants, eqs. 4.1–4.3, neighbour
discovery and loss, selection inside the zone), then the inter-zone half
(`core/hopnet-interzone.{h,cc}`, `hopnet-interzone-table`, `hopnet-data`:
bordercasting and its query control, the destination's generation check, the
external backward ant, the InterRT, data inside and between zones). §4.3's
local repair (notification and error ants) is the next slice. The **Implementation** column
records what the fresh implementation does or is to do, and **Status** says
how it stands against the thesis. Section, equation and figure numbers are the thesis's; **page numbers
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
| Peripheral (boundary) nodes are exactly the radius away; interior nodes are closer; all others are exterior | [W07] p. 23, p. 24, Fig. 4.1 | Peripheral iff the **minimum** IntraRT `hops` over the column equals the radius (a cell via a longer path does not make an interior node peripheral) | as written; "minimum over the column" is a repo choice (maintainer decision 2026-11-10, [#5](https://github.com/swarm-routing/HOPNET/issues/5#issuecomment-6757235563)) |
| Two routing tables per node: a proactive Intrazone Routing Table (IntraRT) and an on-demand Interzone Routing Table (InterRT) | [W07] §4.1 | Two tables in `core/`; the pheromone one may be `swarm-core`'s `PheromoneTable` if it fits | as written |
| IntraRT: rows are the node's one-hop neighbours, columns the nodes of its zone; each cell holds ⟨pheromone, times visited, hops, SeqNo⟩ | [W07] p. 25 | Table keyed (neighbour, zone node) with those four fields | as written |
| IntraRT "times visited" field: defined, never used by any rule in ch. 4 | [W07] p. 25 | Kept as a counter, read by nothing | unknown: [W07] gives no rule that reads it |
| IntraRT `SeqNo` stops the same forward ant from updating the table twice | [W07] p. 25 | Per-cell last sequence number; a repeat is ignored | as written |
| Ant header: Source, Destination, SequenceNo, Type, Hops, Path, Time; five types: 0 internal forward, 1 external forward, 2 backward, 3 notification, 4 error | [W07] p. 25 | One POD ant header, little-endian, leading version byte (org rule 3); the field widths are ours. Version 2 adds two fields [W07] does not have: `target`, the Path node an external ant is being routed to (transit relays need it, row *Path lists the bordercasters*; unset on intra-zone ants, which is how an external backward ant is told from an internal one), and `travelled`, the hops made (row *Hops an external ant or a data packet may travel*). Data carries its own versioned header: source, destination, target, travelled, the InterRT route | as written (layout: repo choice under org rule 3) |
| (Source, SequenceNo) identifies an ant generation; a node increments its sequence number for every forward or backward ant it sends; duplicates are discarded | [W07] p. 25, p. 27 | Bounded (source, seq) history (org rule 7) | as written |
| Internal forward ants are sent periodically by every node to all its neighbours, with Hops set to the zone radius; they sample paths inside the zone | [W07] §4.1, p. 24, p. 25 | Periodic timer through an injected timer port; Hops = radius | as written (period: a repo choice, below) |
| How an internal forward ant moves past the first hop: a zone-limited flood. On receipt from neighbour n a node applies eq. 4.1 to cell (n, source) unless that cell already holds this SeqNo; it re-broadcasts only the first copy of (source, seq), Hops decremented, while Hops > 0; where Hops reaches 0 the first copy becomes an internal backward ant | [W07] p. 24, p. 25, p. 26 | One-hop broadcast per transmitting node per generation; per-cell SeqNo guard; backward ants only from where the hop budget ends (not from every node reached) | repo choice (maintainer decision 2026-11-10, [#5](https://github.com/swarm-routing/HOPNET/issues/5#issuecomment-6757235563)). [W07] says only "to all its neighbors" with a blank Destination; the per-cell SeqNo (p. 25) only has a job if copies arrive via several neighbours, and ZRP's intrazone routing floods with a hop limit the same way. Every interior node's column is filled by that node's own forward ants (eq. 4.1 source updates), so replies from every node would add traffic but no column |
| Internal backward ant returns along the forward ant's Path to its source | [W07] §4.1, p. 26 | Unicast back along the recorded path | as written |
| Neighbour discovery by the internal forward and backward ants themselves; no separate hello message is described | [W07] p. 23 | Discovery: the first-hop internal forward ant from n creates cell (n, n), hops 1, by its eq. 4.1 deposit. Loss: n's row is removed after 2 internal-ant periods (2 s) with no ant from n, or at once on a unicast transmit failure to n reported through the link-state port | discovery as written; loss: repo choice (maintainer decision 2026-11-10, [#5](https://github.com/swarm-routing/HOPNET/issues/5#issuecomment-6757235563)): AntHocNet's two detectors ([DDG04] §3.3–§3.4; `Config::helloInterval` × `allowedHelloLoss` = 1 s × 2); [W07] gives no loss rule, and eq. 4.2 never decays a silent neighbour's own column |
| Forward-ant pheromone update at the node v_j an ant moves to from v_i, entry (v_i, S): eq. 4.1, φ += ε / (T(v_S, v_i) + w(v_i, v_j)), the "source update" of Islam et al. ([W07] ref. [19]) | [W07] eq. 4.1, p. 26 | Same law | as written (ε: a repo choice, below; the measure of w open, below) |
| Evaporation: every other neighbour's entry in the same column is multiplied by (1 − E), eq. 4.2; it runs on each ant update, not on a timer | [W07] eq. 4.2, p. 26 | Applied inside the update, no periodic decay | as written |
| Each ant accumulates the path time T(v_S, v_i) + w(v_i, v_j) as it moves | [W07] p. 26 | Time field of the ant header | as written |
| Backward-ant update for the destination column: eq. 4.3, φ += ε / T′; other neighbours in column D decay by eq. 4.2 | [W07] eq. 4.3, p. 26 | At node v_k, having come from v_b, cell (v_b, D) gains ε / T′ with **T′ = T(S, D) − T(S, v_k)**, the time from v_k to D through v_b. Each Path entry carries the cumulative Time at which the forward ant left that node | repo choice (maintainer decision 2026-11-10, [#5](https://github.com/swarm-routing/HOPNET/issues/5#issuecomment-6757235563)): of the readings of the printed T′ = T(v_k, v_D) − T(v_k, v_b), the time still to go from v_b is 0 on the first backward hop (infinite deposit), and the time already covered contradicts "more pheromone closer to the destination"; this one mirrors eq. 4.1 and is [DDG04] §3.1's backward-ant estimate |
| Next-hop selection rule (for ants and for data) | [W07] p. 25, p. 26 | Standard ACO random-proportional (roulette) selection over the IntraRT column of the destination: neighbour n is chosen with probability φ(n, D)^β / Σ_m φ(m, D)^β over the neighbours m with φ(m, D) > 0, β = 20 for ants and data (Parameters). The column is the packet's current target inside the zone: the destination for intra-zone data; for an external forward ant, the peripheral node it is being sent to while bordercasting, then the destination once a peripheral node finds it in its zone. Inter-zone data is source-routed over the Path nodes of its InterRT record (loose source route, row *Path lists the bordercasters* below): the record fixes the next Path node, and each transit node selects a next hop toward that node by this rule, over the IntraRT column of the next Path node. Internal forward ants carry no destination and select nothing: they flood the zone (row above). Through `swarm-core`'s `swarm-selection` (`selectNextHop`, weights (φ / φ_best)^β, the same distribution without overflow; swarm-routing/ns3-swarm-core#27), or the same rule written in `core/` if that is not merged first | deviation: repo choice (maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/HOPNET/issues/4)). p. 25 announces "the pheromone (φ) update and node selection procedure", but only the updates (eqs. 4.1–4.3) follow; no selection rule appears in [W07]. The rule here is the standard ACO one, not HOPNET's; revisit it if [W09] gives the rule |
| Data to a destination inside the zone: found in the IntraRT, and a route always exists because the table is proactive | [W07] p. 25 | IntraRT lookup | as written; how data picks among the neighbours is the row above |
| A data packet is dropped at a node whose pheromone toward the destination is zero on every link | [W07] p. 34 | Drop and count it | as written |
| InterRT: Destination, SeqNo, Path, Expire; a source route kept until it expires | [W07] p. 27 | Source-route cache with an expiry time | as written (expiry open, below) |
| Data to a destination outside the zone: an unexpired InterRT path is used at once and the packet is source-routed ("self-directed") along it; otherwise route discovery starts | [W07] p. 27 | Source route carried in our data header | as written |
| Route discovery between zones (bordercasting): the source sends an external forward ant to each of its peripheral nodes along IntraRT paths; a peripheral node looks the destination up in its IntraRT, then its InterRT, and otherwise forwards the ant to its own peripheral nodes; repeated until the destination is found | [W07] p. 27, p. 28, Fig. 4.1 | Same. On an InterRT hit the ant follows the stored route: the route's bordercasters join Path ahead of the ant and it is routed to each in turn; not used if one of them is on Path already or Path would overflow (then the node bordercasts) | as written; what follows an InterRT hit is a repo choice accepted by the maintainer on 2026-11-10 ([#5](https://github.com/swarm-routing/HOPNET/issues/5#issuecomment-6779349702) (h)): [W07] p. 27 names the lookup only, and an intermediate reply would bypass the destination's generation check |
| Query control: a peripheral node does not bordercast back toward the zone the ant came from, so ants move away from the source | [W07] p. 28, Fig. 4.1 | A bordercaster that received the ant from neighbour c skips peripheral p if p is on the ant's Path, or if c holds the highest pheromone in its IntraRT column p (ties by node id); c is excluded from the first-hop choice toward every kept p. Duplicate external forward ants are dropped at bordercasters (bounded (source, seq) history), not at transit relays | repo choice (maintainer decision 2026-11-10, [#5](https://github.com/swarm-routing/HOPNET/issues/5#issuecomment-6757235563)): reproduces the worked example exactly (D skips S and A, sends to K and M) from local state; ZRP's rule (skip nodes in the previous bordercaster's zone) also fits it but needs an extended zone HOPNET does not keep. Transit-relay discard would kill the source's own bordercast when two peripherals share a first hop |
| An external forward ant dies at a peripheral node with no peripheral nodes of its own to send to | [W07] p. 28 | Drop | as written |
| External forward ants carry no hop limit (Hops left blank) | [W07] p. 25 | No TTL beyond duplicate suppression; Path capped (Parameters) | as written; the Path cap is a repo choice (org rule 7) |
| Path lists the bordercasters (with the source and destination), not transit nodes; the peripheral node that finds the destination in its zone sends the ant on along its IntraRT path; between consecutive Path nodes an ant or a data packet is routed hop by hop by the selection rule | [W07] p. 28, Fig. 4.1 | Same; inter-zone data is a loose source route over the Path nodes | as written: the example's Paths ⟨S, D, M⟩ and ⟨T, U, M, D, S⟩ (maintainer decision 2026-11-10, [#5](https://github.com/swarm-routing/HOPNET/issues/5#issuecomment-6757235563)) |
| At the destination: if the ant's sequence number is greater than the stored one, the ant becomes a backward ant (type 1 → 2) and returns along Path; otherwise it dies | [W07] p. 27 | Compared with the highest seq the destination has accepted **from the ant's source**, kept in the bounded (source, seq) history; one backward ant per (source, seq); the destination stores no route back to the source | repo choice (maintainer decision 2026-11-10, [#5](https://github.com/swarm-routing/HOPNET/issues/5#issuecomment-6757235563)): [W07] says "for destination", which taken literally compares another node's counter with the destination's own entry; this is the comparison the stated purpose needs, with no side effect [W07] does not describe |
| The source inserts the backward ant's path in its InterRT and sends data along it | [W07] p. 27 | Same | as written |
| External backward ants update no pheromone | [W07] p. 27 | They carry the path back only | repo choice (maintainer decision 2026-11-10, [#5](https://github.com/swarm-routing/HOPNET/issues/5#issuecomment-6757235563)): §4.2 describes only the InterRT insertion, and IntraRT columns are zone nodes only (p. 25), so a deposit for an out-of-zone destination has no place and no reader |
| Multiple paths: HOPNET "is able to find multiple paths" from a source to a destination | [W07] p. 28 | One InterRT path per destination, inserted by the source only; replaced by a newer generation (higher SeqNo) or a notification ant, removed by an error ant or expiry. Data is not split over paths | repo choice (maintainer decision 2026-11-10, [#5](https://github.com/swarm-routing/HOPNET/issues/5#issuecomment-6757235563)): the InterRT record holds one Path and one SeqNo (p. 27) and the destination returns one backward ant per generation; the claim is read as finding alternative paths over time (generations, local repair) |
| Intra-zone link failure: repaired by the proactive IntraRT maintenance after a short period | [W07] §4.3 | Nothing beyond the periodic ants and eq. 4.2 | as written |
| Inter-zone link failure: the upstream node buffers packets and repairs locally by sending a forward ant to the destination | [W07] §4.3, p. 28, p. 29, Fig. 4.3 | Local repair with a bounded buffer: the next `core/` slice, whose open readings are listed on [#5](https://github.com/swarm-routing/HOPNET/issues/5#issuecomment-6779349702) | as written (buffer and wait: repo choices, below) |
| Repair succeeds: buffered packets go on the new route; a notification ant (type 3) tells the source, nodes it visits remove invalid InterRT routes, and the source replaces its path | [W07] p. 29, Fig. 4.2 | Same (the next `core/` slice) | as written |
| Repair fails (no backward ant "after a period of time"): an error ant (type 4) goes to the source, which restarts route discovery if it still needs the route | [W07] p. 29, p. 30, Fig. 4.3 | Same (the next `core/` slice) | as written (the period: a repo choice, below) |
| How a broken link is detected, and what starts a local repair | [W07] §4.3 | A unicast transmit failure through the link-state port (the adapter feeds MAC failures), or the 2 s neighbour timeout. Local repair starts only when a **data** packet's transmission fails and no other neighbour has pheromone toward the next Path node; otherwise the packet takes that neighbour. Table cleanup alone never starts a repair | repo choice (maintainer decision 2026-11-10, [#5](https://github.com/swarm-routing/HOPNET/issues/5#issuecomment-6757235563)): [DDG04] §3.4 (failed unicasts or missed hellos; repair only on a failed data packet) |

## Parameters

| Parameter | Value | Source | Notes |
|---|---|---|---|
| Zone radius (hops) | 2 | [W07] p. 36, p. 37 | The radius of the AntHocNet comparison ("a constant zone radius of 2", Fig. 5.7, Fig. 5.8) |
| Zone radius, other values studied | 2, 3, 4, 5 | [W07] Fig. 5.4, Fig. 5.5, Fig. 5.6 | Sweep against network size (20, 50, 100, 200 nodes); radius 4 in Fig. 5.9 and Fig. 5.10 |
| Zone radius in the AODV comparisons (Fig. 5.1–5.3, 5.12–5.14) | — | unknown | Not stated |
| Internal forward ant hop limit | the zone radius | [W07] p. 25 | |
| External forward ant hop limit | none | [W07] p. 25 | "We leave this field blank"; duplicates bound the flood |
| Internal forward ant period | 1 s | repo choice (maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/HOPNET/issues/4)): the internal ants are HOPNET's only neighbour-discovery traffic, the role of AntHocNet's hello; AntHocNet's `Config::helloInterval` default is 1 s | [W07] p. 24 says only "periodically"; the thesis's main proactive rate |
| ε, the deposit scale in eqs. 4.1 and 4.3 | 1 | repo choice (maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/HOPNET/issues/4)): a cell holds only deposits and multiplicative decay, so scaling ε scales every φ alike and leaves φ^β / Σ φ^β unchanged; ε sets units only (any absolute threshold on φ must scale with it) | "a run time parameter provided by the user" ([W07] p. 26), no value |
| Evaporation rate E, range found suitable | 0.20–0.25 | [W07] p. 33 | "We found the evaporation rate of 20% to 25% to be suitable" |
| Evaporation rate E, value used in the figures | — | unknown | Only the range is given |
| Evaporation rate E, this implementation | 0.20 | repo choice (maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/HOPNET/issues/4)): the lower end of the 20–25 % range [W07] p. 33 found suitable, so the slowest rate the thesis endorses (0.10 was too slow: ants cycled) | Revisit if [W09] gives the value |
| Evaporation rate E, a value that fails | 0.10 | [W07] p. 33 | Too slow: "the ants travel in a cycle" |
| Edge time w(v_i, v_j) | (Q_mac + 1) · T̂_mac at the sending node, T̂_mac a running average of the MAC service time with α = 0.7; 3 ms before the first MAC sample | repo choice (maintainer decision 2026-11-10, [#5](https://github.com/swarm-routing/HOPNET/issues/5#issuecomment-6757235563)): AntHocNet's per-hop estimate ([DDG04] §3.1–§3.2, α = 0.7; `Config::hopTimeSec` = 3 ms, sourced there to Ducatelle's 2007 thesis), so both anchor arms measure path time alike; local and deterministic | "the time period on each edge" ([W07] p. 26), no measure. Supplied to `core/` through an injected link-metric port |
| Initial pheromone of a new IntraRT cell | none (0 until the first deposit) | repo choice (maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/HOPNET/issues/4)): the cell is created by its first eq. 4.1 or 4.3 deposit, so there is no extra constant, and a neighbour with no pheromone is never a candidate, consistent with the drop rule of [W07] p. 34 | [W07] gives no value |
| Next-hop selection exponent β (ants and data) | 20 | repo choice (maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/HOPNET/issues/4)): AntHocNet's `Config::betaAnts` and `Config::betaData` defaults (20), sourced there to Ducatelle's 2007 thesis; [W07] gives no selection rule (Mechanisms) | One value for ants and data, as AntHocNet; [DDG04] §3.2–§3.3 instead squares pheromone for data only. Revisit with the rule if [W09] gives it |
| InterRT Expire time | 3 s after the last use, refreshed each time data is sent on the path | repo choice (maintainer decision 2026-11-10, [#5](https://github.com/swarm-routing/HOPNET/issues/5#issuecomment-6757235563)): RFC 3561 §10 `ACTIVE_ROUTE_TIMEOUT`, AODV being [W07]'s baseline (§5.2); refresh-on-use is AODV's rule, not [W07]'s | "for a certain period of time" ([W07] p. 27). A fixed 3 s lifetime would rediscover a working route every 3 s at one packet per second |
| Local-repair wait before the error ant | 5 × the lost path's estimated delay; 1 s when there is no estimate | repo choice (maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/HOPNET/issues/4)): AntHocNet's `Config::repairWaitFactor` (5, from [DDG04] §3.4) and `Config::repairTimeout` (1 s); the estimate is the Time the ants carried over the lost path | [W07] p. 29 says only "after a period of time" |
| Local-repair buffer (packets, time) | 64 packets, 3 s per packet | repo choice (maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/HOPNET/issues/4)): AntHocNet's ns-3 pending queue (`m_queue(64, Seconds(3))`, the `QueueTimeout` default) | Bounded (org rule 7); [W07] gives no size |
| Route-discovery retry policy at the source | at most one discovery per destination per 1 s, while data for it waits | repo choice (maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/HOPNET/issues/4)): AntHocNet's `Config::reactiveRetryInterval` (1 s) | [W07] p. 29 says only "re-initiate" after an error ant |
| Duplicate-suppression history size | 4096 (source, seq) entries | repo choice (maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/HOPNET/issues/4)): AntHocNet's `Config::maxHistory` | Bounded (org rule 7); [W07] gives no size |
| Path cap of an ant (external forward ants carry no hop limit) | 100 nodes | repo choice (maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/HOPNET/issues/4)): AntHocNet's `Config::maxPathLength`; a safety bound only (org rule 7), above any path in the scenarios here | [W07] p. 25 leaves the external ant's Hops blank |
| InterRT capacity (destinations) | 4096 | repo choice: a bound only (org rule 7), the size of the duplicate history; a node holds records only for destinations it sends to or repairs toward (#5 (d)), 20 flows in the anchor. A full table drops its expired records, then refuses a new destination | [W07] gives no size |
| Hops an external ant or a data packet may travel | 100, the Path cap | repo choice, accepted by the maintainer on 2026-11-10 ([#5](https://github.com/swarm-routing/HOPNET/issues/5#issuecomment-6779349702) (g)): AntHocNet's `Config::maxPathLength` bounds every hop its ants make, and the Path cap row already took that constant; no new one | Sheet review point 2 below. Counted in the `travelled` field; a node does not forward a packet or ant that has made 100 hops |
| IntraRT capacity (cells, neighbour × zone node) | 4096 | repo choice: a bound only (org rule 7), the size of the duplicate history; the anchor's densest point (about 19 neighbours in range, 2-hop zones of about 60 nodes) needs about 1100 cells. A new cell in a full table is refused | [W07] gives no size |
| Simulator | GloMoSim (on PARSEC) | [W07] §5.1 | Version not stated; HOPNET implemented at the network layer |
| AODV base scenario: nodes, area | 50, 3000 m × 1000 m | [W07] §5.2 | Varied 10–190 nodes in Fig. 5.1–5.3 (area for those not restated) |
| AODV base scenario: mobility | random waypoint, speed U[0, 10] m/s | [W07] §5.2 | Pause time not stated for the base scenario |
| AODV base scenario: simulated time | 900 s | [W07] §5.2 | |
| AODV base scenario: traffic | 20 CBR sources, one 64-byte packet every 3 s | [W07] §5.2 | Start times not stated |
| AODV base scenario: radio | 802.11, range 250 m, 2 Mbit/s | [W07] §5.2 | Propagation model not stated |
| Runs or seeds per point, confidence intervals | — | unknown | Not stated anywhere in ch. 5; the figures show single curves without error bars. This implementation's runs: ≥ 20 paired seeds per point (org rule 6) |
| AntHocNet comparison: base scenario | 50 nodes, 1500 m × 300 m, random waypoint up to 20 m/s, pause 30 s, 900 s | [W07] p. 36 | "We used the same simulation set up used in AntHocNet [5]" |
| AntHocNet comparison: area sweep | 1500 m to 2500 m long side, 300 m short side | [W07] p. 36, Fig. 5.7 | As [DDG04] Fig. 1 |
| AntHocNet comparison: pause sweep | 0, 10, 30, 70, 100, 200, …, 900 s | [W07] Fig. 5.8, Fig. 5.9, Fig. 5.10 | The area of this sweep is not stated ([DDG04] §4.2 used 2500 m × 300 m) |
| AntHocNet comparison: scaling sweep | scaling factor 1 to 2; edges × factor, nodes × factor², up to 200 nodes in 3000 m × 1000 m | [W07] p. 39, Fig. 5.11 | [W07] starts from 1500 m × 300 m, which × 2 gives 3000 m × 600 m, not the stated end point; [DDG04] §4.2 starts from 1500 m × 500 m, which does. The anchor uses [DDG04]'s start area, 1500 m × 500 m (maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/HOPNET/issues/4)); [W07]'s 1500 m × 300 m is recorded as the thesis's inconsistency |
| AntHocNet comparison: traffic, radio range, propagation, MAC | — | unknown | Not restated in [W07]. [DDG04] §4.1: 20 CBR sources, one 64-byte packet per second, starting in [0, 180] s; two-ray propagation; 300 m range; 2 Mbit/s; 802.11 DCF. [W07]'s own base scenario uses 250 m and one packet every 3 s. The anchor here uses [DDG04]'s values (maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/HOPNET/issues/4)) |
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
  range, traffic) or [W07] is silent, [DDG04]'s values are used: 1500 m ×
  500 m start area, 300 m range, one packet per second (repo choice,
  maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/HOPNET/issues/4)). [W07]'s 1500 m × 300 m start
  area is inconsistent with its own stated end point and is recorded, not used.
- *AntHocNet*: `swarm-routing/AntHocNet` at a pinned commit, whose reference
  paper is [DDG04]. That is closer to the AntHocNet in [W07]'s figures than
  anything [W07] itself specifies, but it is not the same implementation and
  not the same simulator; agreement is judged on direction, not values.
- *HOPNET*: the next-hop selection rule, the internal ant period, ε, the
  evaporation rate and the bounded sizes are now repo choices
  (maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/HOPNET/issues/4)); the next-hop rule is a declared
  deviation, standard ACO roulette selection, to revisit if [W09] gives
  HOPNET's own. The open readings, the edge-time measure and the InterRT
  expiry were pre-registered and accepted (maintainer decision 2026-11-10,
  [#5](https://github.com/swarm-routing/HOPNET/issues/5#issuecomment-6757235563)); the two review points under *Unknowns* remain.

So the arm does **not** ship as an infeasibility verdict on the AntHocNet
side: the anchor below is testable. If [W09] is read and still leaves the
selection rule open (or until it is read), the HOPNET implemented here is a reconstruction, and the
anchor tests that reconstruction; say so wherever its numbers appear.

## Anchor candidate

Accepted as the anchor (maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/HOPNET/issues/4)). AntHocNet is pinned at a commit stated with each run.

| Trend | Scenario | Metric | Comparators | Source |
|---|---|---|---|---|
| HOPNET (zone radius 2) delivers more than AntHocNet at every scaling factor, and its lead grows with scale: about +0.01 at factor 1, about +0.035 at factor 2 in the thesis | [DDG04]'s scaling experiment, which [W07] reuses: scaling factor s ∈ {1, 1.25, 1.5, 1.75, 2}, 50·s² nodes rounded to the nearest integer, halves up (50, 78, 113, 153, 200: a repo choice, [DDG04] §4.2 gives only the end points) in (1500·s) m × (500·s) m, random waypoint up to 20 m/s, pause 30 s, 900 s, 20 CBR sources of one 64-byte packet per second, 802.11, 2 Mbit/s, 300 m range (the [DDG04] values where [W07] is silent or inconsistent: repo choice) | Paired by seed, HOPNET minus AntHocNet delivery ratio, ≥ 20 seeds per point: 95 % CI above zero at s = 2, and the s = 2 delta larger than the s = 1 delta (paired difference of deltas, CI above zero). Delay and overhead reported, not gated ([W07] gives neither for this experiment) | AntHocNet (`swarm-routing/AntHocNet` at a pinned commit, defaults stated with the run) | [W07] Fig. 5.11, p. 39; [DDG04] §4.2, Fig. 3 |

Not gated, recorded as checks on the reconstruction (the two thesis results
that cut against HOPNET): Fig. 5.7 (HOPNET below
AntHocNet as the area lengthens) and Fig. 5.10 (radius 4 ahead of AntHocNet
for pause 70–400 s). A HOPNET that beats AntHocNet in Fig. 5.11's scenario
but not where the thesis says it lost is worth a second look before anyone
calls the anchor met.

## Unknowns

Settled as repo choices by the maintainer decision of 2026-11-10
([#4](https://github.com/swarm-routing/HOPNET/issues/4)), each with its reason
in the rows above: the next-hop selection rule (a declared deviation: ACO
roulette, β = 20), the internal forward ant period, ε, the evaporation rate
used, the initial pheromone, the local-repair wait and buffer, the discovery
retries, the history size and the Path cap; the scenario values where [W07] is
silent or inconsistent ([DDG04]'s); and the anchor.

Settled by the maintainer decision of 2026-11-10 on
[#5](https://github.com/swarm-routing/HOPNET/issues/5#issuecomment-6757235563) (the pre-registration golden rule 11 asks for), each
with its reason in the rows above: the T′ term of eq. 4.3, the bordercast skip
rule and where duplicates are dropped, the sequence number the destination
compares, external backward ants (no pheromone, one InterRT path), neighbour
discovery and loss and link-break detection, how internal forward ants move
(a zone-limited flood), the edge-time measure w and the InterRT expiry.

Raised in review of [#23](https://github.com/swarm-routing/HOPNET/pull/23)
and not covered by that decision, so open for the maintainer before `core/`:

1. Whether a node re-broadcasts a later copy of an internal forward ant that
   arrives with more remaining Hops than the copy it already forwarded. With
   "first copy only", a copy that arrived over a longer path can leave nodes
   within the radius unreached.
2. A bound on transit hops between consecutive Path nodes, for external ants
   and inter-zone data. Duplicates are dropped at bordercasters only, the Path
   cap counts bordercasters only, and roulette selection can send a packet back
   and forth between two transit relays. Accepted on 2026-11-10 on
   [#5](https://github.com/swarm-routing/HOPNET/issues/5#issuecomment-6779349702) (g) with the inter-zone slice
   (a total of 100 hops, the Path cap).

Facts about the thesis that stay unknown:
the evaporation rate its figures used, the zone radius of its AODV comparisons, its AntHocNet implementation and parameters, its traffic and radio settings for that comparison, and its runs per point.

[W09] is the first place to look for every row above and for HOPNET's own
next-hop rule.
