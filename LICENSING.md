# Licensing

Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing).
Licensed per directory, as AntHocNet
([LICENSING.md](https://github.com/swarm-routing/AntHocNet/blob/main/LICENSING.md),
[ADR-0028](https://github.com/swarm-routing/AntHocNet/blob/main/docs/adr/0028-core-is-dual-licensed.md)).

| Path | Licence |
|------|---------|
| `core/` | `MIT OR GPL-2.0-only` |
| `core/test/consumer-package/` (the `find_package(hopnet-core)` consumer) | `MIT OR GPL-2.0-only` |
| everything else (ns-3 modules, docs, data) | `GPL-2.0-only` |

<!-- org-sync:begin licence-texts -->
Texts: [`LICENSE`](LICENSE) (GPL-2.0) and [`LICENSE-MIT`](LICENSE-MIT) (the
MIT option). Each source file states its licence in its
`SPDX-License-Identifier` line; CI checks it
(`swarm-routing/.github` `cmd/check-headers`; locally `make headers`).
<!-- org-sync:end licence-texts -->

The dependency only goes one way: GPL code may use `core/`, and a `core/` file
includes only the C++ standard library, other `core/` files and the
dual-licensed primitives of
[`swarm-core`](https://github.com/swarm-routing/ns3-swarm-core)'s `core/`, at
the commit `core/CMakeLists.txt` pins. CI checks that too.
