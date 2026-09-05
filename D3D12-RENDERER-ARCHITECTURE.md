# Native D3D12 renderer structure

This is a behavior-preserving extraction, not another rendering API or a legacy
translation layer. Engine-facing `NativeD3D12Renderer` signatures remain stable;
draws still issue native D3D12 commands directly.

All implementation files below live in
`GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/`.

| File/module | Responsibility and ownership |
| --- | --- |
| `native_d3d12_renderer.{h,cpp}` | Coordinator: device, swap chain, command queue/list, fences, frame begin/end, resize, shutdown and profiling. |
| `native_d3d12_draw.cpp` | Coordinator's draw recording, snapshots, constants and geometry resolution. No independent submission or presentation. |
| `native_d3d12_textures.cpp` | Coordinator's texture creation/uploads, render-target binding and synchronous texture readback. |
| `native_d3d12_pipeline_cache.{h,cpp}` | Independent owner of PSOs, root signature, compiled shaders and device identity. Receives only native PSO settings/layout values, not the renderer or mutable engine state. |
| `native_d3d12_shaders.{h,cpp}` | Embedded basic/textured/lighting HLSL. No runtime filesystem lookup or loose shader deployment. |
| `native_d3d12_frame_resources.{h,cpp}` | Independent owner of per-frame mapped upload pages and retained buffer/texture/upload references. Reuses allocations after explicit retirement. |
| `native_d3d12_texture_codec.{h,cpp}` | CPU texture-format decoding. No device or renderer dependency. |
| `native_d3d12_resources.h` | Shared CPU authoring buffers, GPU allocation identity, descriptor ownership and texture handles. |
| `native_d3d12_state.h`, `native_d3d12_lighting.h` | Draw/material snapshots and CPU lighting layout. |
| `native_d3d12_diagnostics.{h,cpp}` | Shared HRESULT logging and timing helpers. |

## Lifetime rules

- Only the coordinator signals/waits frame fences and submits command lists.
- `BeginFrame` waits for its slot's fence and resets the command allocator/list
  before calling `FrameResources::Retire`. Retirement clears retained references
  and resets upload offsets, but keeps mapped upload pages allocated.
- Texture readback can suspend/resume recording; it does **not** retire the slot,
  clear targets, or invalidate queued resource snapshots.
- Shutdown drains submitted GPU work before releasing frame resources or PSOs.
- The pipeline cache rejects a different device until `Reset`; the existing
  descriptor-pool identity still rejects stale textures/buffer versions.
- Pipeline keys, shader text, root bindings, constant layout and draw order are
  unchanged by the extraction. Shader compilation stays on demand and cached.

The draw/texture files deliberately remain coordinator implementation units:
target switches and synchronous readback need the same frame state. They are not
presented as independent owners. Device/presentation and mutable draw-state
ownership could be extracted later if needed; a frame graph or multi-API interface
is not required for this refactor.

## Build and validation

Both full-game and SDK-only smoke configurations use `NATIVE_D3D12_SOURCES` in
`GeneralsMD/Code/CMakeLists.txt`. Preserve existing build directories; ordinary
source changes do not require another configure.

```powershell
cmake --build build-full --target ww3d2 --config Release --parallel 16 -- /clp:ErrorsOnly
cmake --build build-full --target generals test_ww3d2 d3d12_smoke --config Release --parallel 16 -- /clp:ErrorsOnly
.\build-full\Release\test_ww3d2.exe
ctest --test-dir build-full -C Release -R '^d3d12_smoke$' --output-on-failure
```

The smoke executable checks module ownership/cache reuse/device reset and upload
slot retirement, alongside GPU pixel tests for rendering, textures, state,
readback and lifecycle. In-game visual regression should cover reflective water
with moving units, transparent smoke/trails, shadows and UI/minimap. These tests
do not certify that every remaining engine-side legacy state interface is gone.

### Extraction validation (2026-09-06)

- Incremental Release `ww3d2`, `generals`, `test_ww3d2` and `d3d12_smoke` builds
  passed with 16 jobs; no clean/recreated build directories.
- `test_ww3d2`: 92 tests, 2,889 checks, zero failures.
- D3D12 smoke passed in both the full and SDK-only existing build directories;
  the full-build smoke reported debug validation enabled.
- Two 20-second USA Winding River runs (ordinary and forced reflective water)
  remained running and reached rendered frames with no errors/warnings in their
  native-renderer log sessions. These are startup/rendering checks, not long-match,
  graceful-exit or visual-parity certification; test processes were stopped.
- Only the updated executable was copied to the staged `steamfiles` installation;
  retail Steam files and runtime audio/video DLLs were not changed.
