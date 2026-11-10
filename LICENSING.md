# Licensing

Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing).
Licensed per directory, as AntHocNet
([LICENSING.md](https://github.com/danieljoppi/AntHocNet/blob/main/LICENSING.md),
[ADR-0028](https://github.com/danieljoppi/AntHocNet/blob/main/docs/adr/0028-core-is-dual-licensed.md)).

| Path | Licence |
|------|---------|
| `core/` | `MIT OR GPL-2.0-only` |
| everything else (ns-3 modules, docs, data) | `GPL-2.0-only` |

`MIT OR GPL-2.0-only` means you may use that code under either licence, at
your option ([LICENSE-MIT](LICENSE-MIT) or [LICENSE](LICENSE)). Every source
file carries its SPDX identifier; CI checks it (`reusable-licence-check.yml`).

The dependency only goes one way: GPL code may use `core/`, and a `core/` file
includes only the C++ standard library, other `core/` files and the
dual-licensed primitives of
[`swarm-core`](https://github.com/swarm-routing/ns3-swarm-core)'s `core/`, at
the commit `core/CMakeLists.txt` pins. CI checks that too.
