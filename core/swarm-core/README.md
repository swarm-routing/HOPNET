# Vendored swarm-core primitives

Exact copies of the simulator-free primitives HOPNET's core reuses from
[`swarm-routing/ns3-swarm-core`](https://github.com/swarm-routing/ns3-swarm-core)
`core/` (MIT OR GPL-2.0-only, same copyright holder), at the commit in
[`PIN`](PIN). They are vendored, not fetched, because the licence gate
(`reusable-licence-check.yml`, `--core core/`) requires every quote include of a
`core/` file to resolve under `core/`.

Never edit these files here. To move to a newer swarm-core, copy its `core/`
files at the new commit over these, update `PIN`, and run the core tests. CI
(`.github/workflows/core.yml`) fails when a file differs from swarm-core at `PIN`.

The ns-3 module (`ns3/`, later) links the `swarm-core` module instead and must
pin the same commit.
