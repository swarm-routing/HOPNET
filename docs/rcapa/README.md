# RCAPA: regressions in a release of swarm-routing/HOPNET

An **RCAPA** (root cause analysis with corrective and preventive actions) is
the full record of one **regression in a published library**: a version of
it that another project consumes (a release pinned by a lock, PyPI, a vcpkg
port) worked, and a later one broke. A bug or change that never reached a
published version needs none (an ordinary `bug` issue is enough), a Docker
image needs one only when its composition is the cause, and an upstream
ns-3 bug is an `NS3-NNN` in swarm-routing/ns3-tools. The org's rule and its decision table are in
[CONTRIBUTING.md, "RCAPA"](https://github.com/swarm-routing/.github/blob/main/CONTRIBUTING.md#rcapa)
([ADR-0003](https://github.com/swarm-routing/.github/blob/main/docs/adr/0003-rcapa-for-ns3-bugs-and-released-regressions.md)).
This page is the project's flow and its index.

## The flow for this project

The library other projects would consume from HOPNET is its simulator-free
`core/`, through the `hopnet-core` vcpkg port. That port is not published yet (it
waits in swarm-routing/vcpkg-registry `templates/` until a release installs
`core/` as a CMake package), so **RCAPA does not apply yet**: the flow below
starts with the first published port version. The ns-3 module, the GitHub
releases and their Zenodo DOIs are not consumed at a version by another
project; a defect there is an ordinary `bug` issue.

| | |
|---|---|
| Published as | nothing yet: the `hopnet-core` vcpkg port, from its first published version |
| ID prefix | `HOP-NNN` |
| Ticket | an issue in swarm-routing/HOPNET, form *Regression in a release (RCAPA)*, title `[rcapa] HOP-NNN <short description>`, labels `rcapa` and `bug`, opened from the consumer's finding |
| Verified by | the consumer's failing build or test against the `hopnet-core` version that broke and the one before it; then `make core-test` (ctest with strict warnings) locally, and the `core` check (`core.yml`), which also builds and runs the tests under ASan + UBSan |
| Corrective action | the fix PR with a core test in `core/test/` that fails without it; a wire-format change bumps the version byte (golden rule 3); then a release and the port bump org-bot opens in swarm-routing/vcpkg-registry |
| Preventive action | the core test or fuzz case that would have caught it, and a consumer case in swarm-routing/vcpkg-registry `test/consumer/` if only a consumer showed it |
| Closes when | a release carries the fix **and** the `hopnet-core` port is bumped to it in the registry, with the consumer that found it green on the new baseline |

```mermaid
sequenceDiagram
    participant C as A vcpkg consumer
    participant T as RCAPA ticket
    participant G as core checks
    participant M as main
    participant R as HOPNET release and port bump
    C->>T: a check that passed on the old version fails on the new one
    T->>G: reproduce with the consumer's check, new version against old
    T->>M: fix with a test that fails without it
    M->>R: a release carries the fix
    R->>C: the consumer moves to it and runs green
    C->>T: release and green run recorded, ticket closed
```

## Index

| ID | RCAPA | RCAPA ticket | Fixed in |
|----|-------|--------------|----------|
| HOP-001 | [core/ fails a GCC 13 Release -fPIC build](rcapa-errorant-stringop-overflow.md) | [swarm-routing/HOPNET#232](https://github.com/swarm-routing/HOPNET/issues/232) | not yet released |

## Adding one: the ticket first

1. Open the RCAPA ticket (the table above). `NNN` is the next free number in
   the index.
2. Copy [`_template.md`](_template.md) to `rcapa-<short-description>.md`
   (lowercase, hyphens: what breaks, not where it was found), and add its row
   to the index in the same pull request.
3. The ticket stays open until a release carries the fix, the `hopnet-core` port is bumped to it in swarm-routing/vcpkg-registry, and the consumer that found it runs green on the new baseline; record the release and the baseline in the index.

## What an RCAPA answers

1. **What broke, in which release, and who did it touch?** The symptom, the
   releases that fail and the last that passes, the users, numbers or
   downstream pins it affects.
2. **How do I see it myself?** Exact commands, the release or commit, an
   image by tag and digest when one is involved, seeds, expected against
   actual.
3. **Why did it happen, and why did our checks let it into a release?** Five
   whys, each citing its evidence: a source line at a named commit, a log, a
   run, a comment. An answer without a source is a hypothesis and says so.
4. **What does it look like?** Mermaid diagrams: the failing call chain, the
   types or components involved.
5. **What did we do about it?** Corrective actions (the fix, its PR, the
   release that carries it) and preventive actions (the test or check that
   catches a recurrence). An action not done yet is an open issue, linked.
6. **When?** A dated timeline and every link.

## Writing one

- Every issue and PR reference is fully qualified: `swarm-routing/<repo>#N`,
  also for this repository's own items; an ns-3 item is a link whose text is
  `nsnam/ns-3-dev#N` to its full gitlab.com URL. Never a bare number with a
  hash. In a mermaid block, which cannot hold a hash, name the item in words.
- Mermaid: no HTML entities inside a block; write template arguments as
  words.
- An RCAPA is never deleted. Later findings update its status and timeline.
