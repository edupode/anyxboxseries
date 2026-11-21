# Credits

AnyXboxSeries by edupode

Copyright (c) 2026 edupode

[github.com/edupode/anyxboxseries](https://github.com/edupode/anyxboxseries)

Preserve this credit and the licence notices when redistributing covered code. Display the attribution in a prominent user interface credit when the licence requires it, including in products that incorporate covered code. See Section 14 and Exhibit B of the [licence](LICENSE).

## Technical references

- **Leonida** by ItsNotRexed and contributors. Reference for the Xbox shader container layout, separate GPU translation stage, and published RBF0/PSIN format notes. The upstream repository is GPL-3.0. The local scanner follows documented format facts and does not copy source. [RBF0 notes](https://github.com/ItsNotRexed/Leonida/blob/aae4d99e10784eb21b4f14d08470c79613b9e7fe/docs/rbf0.md) · [PSIN notes](https://github.com/ItsNotRexed/Leonida/blob/aae4d99e10784eb21b4f14d08470c79613b9e7fe/docs/psin.md) · [Licence](https://github.com/ItsNotRexed/Leonida/blob/aae4d99e10784eb21b4f14d08470c79613b9e7fe/LICENSE)
- **ScarlettX** by Freminet and contributors. Reference for synthetic Direct3D 12 diagnostics and separate DXR and shader bring-up. [Reviewed revision](https://github.com/Freminet/ScarlettX/tree/bda18b4d3dab501dd0f372e5d1921f462c6bd457)
- **Xbox2Windows** by R0Y00 and contributors. MIT-licensed source used for the XMem desktop shim (`xmem_shim.cpp`, `xmem_shim.def`, `era_heap.*`, `era_query.*`) and PE mapper (`pe_map.cpp`), included under `native/third_party/xbox2windows/`; the XMem query source has guarded Windows macro definitions for this project's build. The upstream README says title CPU execution and full-game startup remain unverified; the allocator layout and query behavior still need confirmation against Scarlett call sites. [Source revision](https://github.com/R0Y00/Xbox2Windows/tree/bc24dfd7ebbd38867477a562e9c51fd1cfc59cbb) · [MIT licence](https://github.com/R0Y00/Xbox2Windows/blob/bc24dfd7ebbd38867477a562e9c51fd1cfc59cbb/LICENSE)
- **AMD**. RDNA 2 instruction encodings and behavior. [RDNA 2 ISA reference](https://docs.amd.com/v/u/en-US/rdna2-shader-instruction-set-architecture)
- **Microsoft**. DXIL container and compiler API, and the DirectX Raytracing API. [DirectX Shader Compiler](https://github.com/microsoft/DirectXShaderCompiler) · [DXR specification](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html)
- **Microsoft Xbox GDK Samples**. SimpleTriangle and GameInputInterfacing are API references at revision [`b1e77b6`](https://github.com/microsoft/Xbox-GDK-Samples/tree/b1e77b6ac10f95b07296025fda171ca2ddc42dbe). The sample repository is MIT-licensed; this project uses the documented APIs and does not copy sample source. [Upstream licence](https://github.com/microsoft/Xbox-GDK-Samples/blob/b1e77b6ac10f95b07296025fda171ca2ddc42dbe/LICENSE)
