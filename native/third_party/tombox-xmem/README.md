# TomBox XMem candidate

This directory vendors the `xmem` compatibility crate from
[omonkeyz/xbox-emulator](https://github.com/omonkeyz/xbox-emulator), reviewed at
revision `a96117690a38bd758bfb4a87e2d9a3d38185a894` on 2026-11-22. The upstream
crate is dual-licensed MIT OR Apache-2.0; this copy is distributed under MIT.
See [LICENSE-MIT](LICENSE-MIT).

The source is kept as a separate candidate DLL. The `compat-common` dependency
was removed and its two uses were replaced with this crate's small local logger
and a default Windows build value. Those edits are in `lib.rs`, the modules
which call `record`, and `pool.rs`. Duplicate Windows FFI declarations were
normalized to the same pointer-shaped ABI; helpers used only by retained tests
are marked test-only. The remaining XMem source and its tests are from upstream.

This candidate is not the default `xmem.dll`. Its exports and Windows memory
behavior can be smoke-tested here, but no authorized GTA VI executable is
available to validate the title-specific ABI or allocator attributes. Do not
interpret a passing smoke test as evidence that the game boots.
