# What the public Xbox Emulator source demonstrates

Reviewed revision `a96117690a38bd758bfb4a87e2d9a3d38185a894` on 2026-11-22.

## GTA VI versus other title milestones

The repository's tested-title table distinguishes Fortnite from GTA VI: Fortnite is reported to reach a lobby, with scene, audio, and gameplay limitations; GTA VI 1.0.2.1 is marked **not run because the dump has no executable**. The full shader-sweep badge in the README belongs to Fortnite and must not be read as a GTA VI result. [Tested titles and status](https://github.com/omonkeyz/xbox-emulator/blob/a96117690a38bd758bfb4a87e2d9a3d38185a894/README.md#tested-titles) · [Milestones](https://github.com/omonkeyz/xbox-emulator/blob/a96117690a38bd758bfb4a87e2d9a3d38185a894/docs/MILESTONES.md) · [Blockers](https://github.com/omonkeyz/xbox-emulator/blob/a96117690a38bd758bfb4a87e2d9a3d38185a894/docs/BLOCKERS.md)

TomBox is a Rust runtime with substantial Xbox D3D12 device and command-queue wrappers. That work is much larger than adding a `D3D12XboxCreateDevice` export: Xbox command-list and queue vtables expose console methods such as `PresentX`, so an unwrapped desktop COM object is not a complete substitute. AnyXboxSeries currently builds a C++ prototype with two root-signature exports and does not yet implement that console interface. [TomBox D3D12 export map](https://github.com/omonkeyz/xbox-emulator/blob/a96117690a38bd758bfb4a87e2d9a3d38185a894/crates/compat/d3d12_xs/exports.txt) · [Device and queue wrapper](https://github.com/omonkeyz/xbox-emulator/blob/a96117690a38bd758bfb4a87e2d9a3d38185a894/crates/compat/d3d12_xs/src/device_wrap.rs)

The Discord screenshot in the task shows a rendered scene, but does not identify the process, build, input package, or interaction used to produce it. The text under the image says “still larp btw”; that may be a joke or role-play signal, but it does not establish provenance either way. Treat the capture as an unverified visual claim, not proof that this repo booted GTA VI.

## Reused code and licence

The root README offers the project under MIT OR Apache-2.0. The XMem crate is comparatively isolated from the full runtime, and includes allocator, pools, virtual/physical pages, write-watch, statistics, exports, and tests. Its source is vendored under `native/third_party/tombox-xmem/` with the MIT notice retained. Changes are limited to replacing the workspace `compat-common` dependency with a local logger/default-build helper and correcting two duplicate Windows FFI declarations to one pointer-shaped ABI. CMake builds the DLL as a separate candidate and runs the retained tests plus a local smoke test. It does not replace the existing default shim pending a title-level compatibility run. [Upstream licence](https://github.com/omonkeyz/xbox-emulator/blob/a96117690a38bd758bfb4a87e2d9a3d38185a894/LICENSE-MIT) · [XMem crate](https://github.com/omonkeyz/xbox-emulator/tree/a96117690a38bd758bfb4a87e2d9a3d38185a894/crates/compat/xmem)

No TomBox D3D12 implementation is copied in this change. Its component has Rust-specific COM wrappers and dependencies on other runtime crates; porting isolated exports would create a misleading partial shim. Leonida's public source separately documents its asset viewer and reports that GTA VI does not boot at the reviewed revision; see [the Leonida audit](leonida-boot-claims.md).
