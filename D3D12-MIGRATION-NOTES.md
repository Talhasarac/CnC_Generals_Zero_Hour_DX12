# Native D3D12 migration checkpoint — 2026-09-04

The game reaches a rendered skirmish with the native backend. The port is **not
feature-complete or visually equivalent** to the original renderer.

## Surface readback/recolor crash fix — 2026-09-05

The Steam crash stack reached SurfaceClass::Get_Description through texture
recoloring after rendering had failed. Failed Get_Level_Description previously
left its output uninitialized; recoloring then allocated from that description
and tried another readback. Empty surfaces fell through to a null D3D8 pointer.

Description failures now return UNKNOWN/zero dimensions. Empty surfaces safely
reject locks and copies. CPU surface allocation is independent of a live GPU.
Recoloring acquires its source once, validates dimensions/format, and retains the
original texture if no usable source exists; no invalid recolor enters the cache.

Incremental Release ww3d2/game builds passed with 16 jobs; CPU tests passed
(71 tests, 2255 checks), including three new surface regression cases. GPU smoke
passed (1/1). The updated Steam installation completed a 900-simulation-frame
USA Air Force Winding River run with debug layer off and profiling on, exit 0.
Both Steam and steamfiles executables were updated; the prior Steam executable
is backed up in _NativeD3D12_SurfaceFix_20260905-040236.

The animated-menu reproduction was not verified: unattended startup stayed in
intro movies and was stopped. The preceding intermittent GPU/device-loss trigger
and device recovery remain separate unresolved issues, not fixed by these guards.

## Inactive water renderer and header cleanup — 2026-09-05

Removed the D3D8 water draw fallbacks, vertex/index-buffer allocation and locks,
device/resource members, shader handles, disabled assembly-shader compilation,
and disabled bump-texture conversion/loading. The native draw implementations
and serialized water-type values are retained. The unfinished reflected-sea
resource path retains its previous early exit; this cleanup does not enable it.
The water header no longer declares D3D8 COM resources or shader registers and
uses a forward declaration for the still-active engine index-buffer class.

Removed unused D3DX8 includes from water, flat/base terrain, camera shake,
asset management, vertex buffers, missing textures, textures and surfaces.
This is not repository-wide removal: active legacy state/type dependencies,
other inactive renderer branches and the historical project sources remain.
In particular, native_matrix_math.h still uses D3DX matrix data types.

Validation: incremental Release ww3d2 and generals builds with 16 jobs; 68 CPU
tests / 2239 checks passed; native GPU smoke passed (1/1). The rebuilt executable
was staged in steamfiles and completed a 900-simulation-frame USA Winding River
skirmish with exit code 0 and no ERROR/WARNING/CORRUPTION/failed entries in that
session's native renderer diagnostics. No visual parity claim is made by this
automated run. Existing build directories and configuration were reused.

## Reflection capture lifecycle batch — 2026-09-05

Native render-target resolution queries now report the bound texture dimensions,
so `CameraClass::Apply` no longer constructs a window-sized viewport for a small
reflection/shadow target. Native target creation bypasses legacy device assertions.
The mirror pass begins recording before binding/clearing, checks missing targets
and bind failure, clears stale capture color, and restores the backbuffer before
restoring the camera viewport. This does not yet enable the unfinished specialized
sea resource/material path; reflection imagery still needs to be consumed there.

Incremental Release ww3d2/game builds passed with 16 jobs. CPU tests: 68 / 2239
checks. GPU smoke: 1/1 (3.75 seconds), including a 16x8 offscreen capture,
target-dimension changes, non-presenting submission, and corner pixel checks when
sampling that capture in a subsequent 64x64 frame. This is capture-lifecycle
evidence, not visual certification of game-world reflections.

## Native bump material batch — 2026-09-05

Native HLSL now evaluates environment bump and bump-with-luminance operations
instead of terminating those material chains. Texture samples are evaluated in
stage order: the signed gradient and 2x2 matrix affect only the next sample, while
its RGB is modulated by luminance scale/offset. Bump stages preserve accumulated
surface color/alpha. The formulas follow Microsoft's
[bump mapping specification](https://learn.microsoft.com/en-us/windows/win32/direct3d9/bump-mapping-formulas);
no legacy graphics API or shader bytecode executes this work.

CPU-authored U8V8, X8L8V8U8 and L6V5U5 surfaces encode signed gradients into
filterable native BGRA textures, with an exact encoded zero and separate luminance.
Material constants grew from 48 to 80 bytes per stage, with matching HLSL packing.

Release renderer/game builds succeeded incrementally with 16 jobs. CPU tests:
68 tests / 2239 checks. GPU smoke: 1/1 passed (3.98 seconds), including positive,
negative, neutral, off-diagonal and luminance bump pixel checks. A 900-simulation-
frame USA Winding River skirmish exited normally with the D3D12 debug layer enabled.
This was an integration/crash check, not a visual or large-battle certification.

Still required: replace the specialized sea's legacy bump-frame generation and
reflection-target initialization, integrate native reflected-scene rendering and
sea patch placement, verify authored bump assets through their complete loading
paths, then continue terrain/material, long-match, cleanup and distribution work.
The bump-material tests alone do not prove the reflected-water feature finished.

## River left-edge wall fix — 2026-09-05

The reported vertical water streak was reproduced in a USA Winding River match.
The stencil-shadow composite saved its projection with `Get_Transform`, whose
native projection path read an uninitialized output from an inactive legacy call.
Restoring that value corrupted subsequent water geometry; diagnostic changes
merely changed the garbage matrix and sometimes moved the water offscreen.

Both projection getters now use the maintained projection state in the proper
matrix convention. Projection state can also be assigned without a device, like
world/view state. A device-free regression verifies projection save, identity
override and restore (67 CPU tests / 2215 checks pass).

Correct geometry also exposed leftover river sparkle/noise stages in the separate
shroud pass. That single-texture pass now explicitly terminates stages 1–3,
preventing black river polygons without disabling water or its shroud.

Incremental Release renderer/game builds used 16 jobs with no reconfiguration.
GPU smoke passed (1/1, 3.67 seconds). In-game USA checks at 1280x720 verified the
bridge and downstream huts: textured water remains visible, without the vertical
wall or black shroud-pass polygons. All temporary probes were removed. The staged
`steamfiles/Generals.exe` contains this fix. Shoreline blending, reflections and
the other unfinished migration items below remain separate work.

## Filtering, river and screen-effect follow-up — 2026-09-05

This follow-up supersedes the screen-filter and river status below:

- Native capability reporting now advertises supported point/linear/mip and
  anisotropic filtering. Previously DEFAULT/BEST resolved to point sampling,
  including the shroud texture. This fixes that concrete cause of blocky edges,
  not every terrain/depth artifact.
- River materials now combine base water, edge RGB/opacity, and animated
  sparkle-times-noise using native material stages, then apply the original
  separate shroud pass. Flat water explicitly selects a prelit material. Direct
  sea/grid draws isolate material state and use their actual vertex-color offsets;
  sea texture lookup occurs after initialization and resident geometry is reused.
- Motion blur uses GPU-resident scene capture and native quad taps. Crossfades
  have native masked compositing with tactical-view-relative UVs. Monochrome,
  red/green tint and fade amount are wired into the game's filter path, not merely
  supported by a standalone renderer API. Pixel root constants now have 11 words.
- Capture targets are recreated after display resize, previous capture color is
  preserved for blur/crossfade, and depth is cleared for the new scene pass.
  Filter selection is recorded before capture setup. End-pan blur guards zero
  scroll length. Default compositing establishes its depth/blend state explicitly.

Incremental Release ww3d2 and generals builds passed with 16 jobs; no configure,
clean or build-directory recreation was performed. GPU smoke passed with debug
validation (1/1, 3.61 seconds), including new cropped-UV crossfade, blur-opacity,
river-edge-opacity and tinted-monochrome fade pixel tests. The final CPU suite also
passed 66 tests / 2183 checks.

USA runtime checks used Seaside Mutiny and Winding River at 1280x720. The computer-
use skill was used to inspect the latter's base, minimap, bridge and river views.
The visual check found a vertical streak along the left side in some river views;
the later fix above addresses it. These short runs do not certify long-match stability,
all factions, campaign filter sequences, or visual parity.

Still unfinished: specialized reflected/bump water and other
remaining terrain/material artifacts, legacy source/header cleanup, long-match
and full effect coverage, and a clean-machine full-game distribution verification.
See [D3D12-PLAYTEST.md](D3D12-PLAYTEST.md) for launch and playtest instructions.

## Published source checkpoint — 2026-09-05

This section supersedes the older progress summaries below. Native model lighting
now covers ambient, diffuse, emissive and specular shading, material color sources,
directional/point/spot lights and inverse-transpose normal transforms. Both solid
and textured meshes use the GPU lighting path; screen draws remain unlit. Existing
CPU skin deformation is retained. Vertex constants are 1264 bytes and pipeline
keys have 29 entries, including normal and secondary-color offsets.

The native DDS reader now computes rectangular BC mip layouts from each level's
dimensions, retains the complete chain and rejects truncated data. This repairs
the wave256 shoreline checkerboard, locator graphics, light beams and wall textures
without modifying their assets. Pixel tests cover lighting, UI isolation, scaled
normals, point/spot attenuation and rectangular mip upload/readback through 1x1.

Validation on the development machine: incremental Release ww3d2/game builds,
D3D12 smoke (1/1, debug-layer validation), and 66 CPU tests / 2183 checks passed.
USA on Seaside Mutiny at 1280x720 showed a 348 FPS median over 88 post-startup
samples with camera movement; this is not a controlled DX8 comparison. A process
module check found native D3D12/DXGI and no loaded D3D8/D3D9 translation runtime.
Full clean-machine game-build reproducibility has not yet been verified.

Remaining: terrain/shroud edge artifacts, specialized river/reflection/bump water,
additional screen filters, broader material/animation/effect parity, stability and
performance coverage, and removal of inactive legacy source/header dependencies.
The optional generated StreetHoleCover replacement is not part of this source
commit. Without it, a missing-asset warning may remain for that road definition.

### Source-only build requirements

New HLSL is embedded in native_d3d12_renderer.cpp and native_d3d12_lighting.h;
D3DCompile compiles it at runtime. There are no separate shader binaries to copy.
Commit the native helper headers and test source along with the CMake changes.

The standalone smoke target requires Visual Studio C++/CMake and a Windows SDK,
not installed game assets or the third-party game dependency drops:

```powershell
cmake -S GeneralsMD/Code -B build-d3d12-smoke -G "Visual Studio 17 2022" -A Win32 -DNATIVE_D3D12_SMOKE_ONLY=ON
cmake --build build-d3d12-smoke --config Release --target d3d12_smoke --parallel 16
ctest --test-dir build-d3d12-smoke -C Release --output-on-failure
```

Full-game builds still require the README's external dependencies. The local
GameSpy drop was sourced from TheSuperHackers/GamespySDK at b1b77d8 and is not
vendored by this commit; supply it under Code/Libraries/Source/GameSpy. Legacy
DirectX headers/import libraries, zlib 1.1.4 and LZH-Light sources remain external.
Keep original installed Bink/Miles runtime DLLs: the generated build-stub DLLs
must not replace those shipped with the player's game. Keep game data, build
caches, SDK staging, extracted references and runtime logs out of commits.

## GPU wind, vertex fog and flat-water material — 2026-09-05

Three additional native-renderer batches are implemented and deployed:

- Tree sway now consumes the existing logic-timed breeze offsets in native HLSL.
  The tree vertex's former normal field carries sway index, push-aside darkening
  and base height. Lower vertices remain anchored; canopy displacement scales
  with height above the base. Position-generated shroud UVs follow displacement.
  The effect is scoped to tree drawing and keyed into the input-layout PSO cache.
- Vertex fog supports linear, exponential and exponential-squared modes, with
  camera-depth or range distance and the recorded fog color. The renderer receives
  the actual world-view matrix. Solid and textured screen quads bypass world fog.
  This is vertex fog, not a claim of arbitrary pixel/table-fog parity or lighting.
- Native material stages can now write/read a temporary color. Flat translucent
  water uses it to reproduce base water + sparkle * animated world-space noise,
  followed by shroud modulation, retaining base opacity. The draw explicitly uses
  two-sided culling and restores culling/temporary-result state afterward. This
  does not implement the separate reflected/bump-mapped sea or river shader.

Vertex constants are now 624 bytes, including the 11-entry sway table and fog
parameters/world-view matrix. NativeMaterialStage is 48 bytes with uint4 result
flags matching HLSL. Pipeline keys have 27 entries, including sway input offset.

Validation: incremental ww3d2, generals and smoke builds passed with exactly 16
jobs, no configure/clean. GPU smoke passed 1/1 with debug validation/profiling
(3.26 seconds); CPU suite passed 66 tests / 2183 checks. New GPU pixel checks cover
wind deformation/root anchoring/darkening/state cleanup, three fog modes, range
fog color, UI fog isolation, and water's temporary-register multiply-add/shroud.

USA/America runtime validation used Seaside Mutiny at 1280x720, seed 12345. The
launcher correctly capped the requested six slots to this map's two-player limit.
The USA base, minimap and shoreline water rendered; observed base/shoreline views
were above 330 FPS, not a controlled cross-version benchmark. Missing-texture
markers remain visible in the map. The test process was closed afterward.
The computer-use skill was used to inspect the game and navigate the minimap.

Evidence: `build-full/d3d12-wind-fog-water-usa-runtime.log` and
`build-full/d3d12-wind-fog-water-usa-game.log`. Logs append; use the final native
initialization section. Current build/staged executable SHA256:
`4ED72AB31621571365B439BE9DDB7F1666E304445A359E0242D7CEB7CDF0BF1E`.

Remaining requested work: reflected/bump-mapped water and river effects, full
model lighting, optional screen filters and visual/asset cleanup. Skin deformation
is not entirely absent: dx8renderer.cpp already calls Get_Deformed_Vertices and
fills native-backed vertex buffers. Its lighting/visual parity still needs testing;
do not replace the game's existing animation logic merely to rename this path.
The older remaining-work lists below describe earlier checkpoints.

## Texture fidelity, surface access and terrain materials — 2026-09-05

Completed three coherent renderer batches using the existing Release builds,
16 jobs, ww3d2 before generals, without configure/clean operations:

- Procedural and TGA textures now respect requested mip counts and generate
  filtered mip chains on upload. Odd-size mip filtering includes the final row
  and column. X8 source alpha is forced opaque; DDS X8 uses its native X8 format.
  The missing-texture checker now has a valid row pitch. Terrain grain is reduced
  visually, but residual terrain/shroud edges are not claimed to match DX8.
- DDS-only assets are no longer skipped when GPU compression is disallowed.
  Native CPU decoding supports BC1/BC2/BC3 and the loaded packed formats, with
  clipped partial blocks and pitch/size validation. GPU surface access now reads
  actual pixels and mip levels instead of returning blank allocations. It submits
  pending work in queue order, waits for a fence, and resumes the current frame
  without presenting, clearing, changing its target or recycling upload pages.
  Base-surface edits are revision-uploaded; editing a compressed source replaces
  it with BGRA storage while submitted draws retain the old allocation. Higher
  mip surfaces are readback snapshots, not independently writable GPU mip views.
- Alpha terrain materials share the base atlas resource without changing the
  texture binding during Apply. Their filters, UV selection and blend states are
  explicit. The single-pass native material uses blend-then-lighting stages, not
  the legacy vendor-specific eight-stage driver trick.

Validation: incremental ww3d2 and generals builds passed; GPU smoke suite passed
1/1 with D3D12 debug validation and profiling (3.43 seconds). New checks cover
generated/odd mips, opaque alpha, BC transparency, truncated source rejection,
compressed final-mip readback, resource sharing after owner destruction,
offscreen frame continuation across readbacks, and terrain blend then lighting.
The CPU ww3d2 suite passed 66 tests / 2183 checks.

Runtime validation used **USA/America**, Tournament Urban, six-player automatic
setup, seed 12345, 1280x720 windowed. The command center, trees, terrain, UI and
minimap render. A sampled fixed-view checkpoint showed median 327 FPS over 80
presentation samples after discarding the first three (294–332 range). This is
not a controlled original-DX8 comparison or a full-game visual certification.
Observed native GPU time was about 0.5 ms, UI preparation about 0.6–0.8 ms.
The process loaded system D3D12/DXGI/compiler modules, with no D3D8/D3D9,
DXVK/Vulkan/ReShade module in the inspected process snapshot.

The menu-only issue was a startup-input behavior: MainMenuInput intentionally
reveals the initially hidden menu after mouse movement or a character. Verified
main-menu buttons and Solo Play submenu visually after input; no gameplay/input
code was changed to bypass that behavior.

Evidence: `build-full/d3d12-mips-runtime.log`,
`build-full/d3d12-texture-readback-usa-runtime.log`,
`build-full/d3d12-texture-readback-usa-game.log`,
`build-full/d3d12-menu-validation-runtime.log`, and
`build-full/d3d12-texture-terrain-final-runtime.log` with matching final game log.
Native logs append; use the last initialization section per file.
Current build/staged executable SHA256:
`4C778B8108C89C5CDFCF1156E6F260C9B5826EBB0F721FB09C03FE7A1495A1B8`.

Still outstanding: full world lighting/fog, specialized water/reflection/bump,
tree wind and skinning fidelity, optional screen filters, residual visual defects,
broader map/effect testing and inactive legacy source cleanup. This checkpoint
does not complete those subsystems or the overall renderer port.

## USA minimap repair — 2026-09-05

Reproduced with vanilla USA (`America`) on Tournament Urban: unit markers and the
camera outline rendered, but minimap terrain was black. Radar's first supported
surface format is BGR24; `SurfaceClass::Draw_Pixel` silently omitted three-byte
pixels, so the bulk terrain generation left its CPU surface empty. The packed
surface writer now supports all 1–4-byte pixels and preserves adjacent bytes.
The native CPU surface revision/upload path remains unchanged.

Verified USA terrain, roads/buildings, unit markers and camera outline visually.
Clicking the restored minimap moved the world camera and its outline to the
selected map position. Navigation evidence is in `build-full/d3d12-usa-radar-navigation.log`.
The D3D12 smoke suite passes, including new packed-pixel guard-byte checks.
The ww3d2 CPU suite passes: 66 tests, 2183 checks. Updated its old test that
expected BGR24 writes to be ignored, plus stale FVF expectations that described
the pre-migration two-float-stride bug instead of the actual vertex struct.
All builds were incremental Release with 16 jobs; no configure or clean.

The unattended launcher now optionally honors `GENERALS_D3D12_TEST_SIDE=America`
for its local slot, validating the name against playable templates. This is a
test setup equivalent to faction selection in the lobby; it does not change
normal launch defaults, radar unlock rules or the simulation. Unknown names abort
the automatic match rather than silently testing the wrong faction.

Evidence: `build-full/d3d12-usa-radar-before.log`,
`build-full/d3d12-usa-radar-after.log`, and the matching after-timing log.
Use the last initialization section in appended native logs.
Staged/build executable SHA256:
`511F8B487119B5A0E20F4A2B24BF6DC51980AD1BD527CD5E7406AF9E79C4EEB3`.
At this earlier checkpoint world terrain was noisy and the menu-only launch
showed background art without buttons. See the newer texture/menu findings above.

## UI cache fix — 2026-09-05

Window repaint profiling isolated the main UI cost to command-button callbacks.
Count, seconds and price badges each shared one mutable DisplayString across all
buttons. Alternating values invalidated sentence/texture caches on every frame.
Each button now lazily owns three independent labels, initialized with the gadget
and freed through DisplayStringManager on GWM_DESTROY. Existing text/font/position
invalidation remains responsible for updates; no rendering is skipped or reordered.
This changes rendering cache ownership, not simulation, input or file formats.

Matched windowed 1280x720, Tournament Urban, six players, seed 12345, 600 logic
frames, no interaction during the benchmark:

- Before: median 116 FPS, median UI CPU 4.58 ms.
- After: median 343 FPS (316–348 sampled range), median UI CPU 0.53 ms.
- FPS medians use 17 presentation-rate samples with >1000 draws, excluding the
  first three such samples for warm-up. UI samples exclude the first ten samples
  with scene CPU >1 ms. These are sampled measurements, not frame-time percentiles
  or a controlled comparison against the original DX8 executable.
- Evidence: `build-full/d3d12-ui-baseline-runtime.log` and
  `build-full/d3d12-ui-cache-benchmark-runtime.log`, last initialization section.
  Temporary callback profiling is recorded in `build-full/d3d12-ui-callback-profile.log`
  and was removed from the source after diagnosis. The native profile now retains
  the useful overlays/windows/remainder breakdown.

Incremental ww3d2 and generals Release builds passed with 16 jobs; the existing
D3D12 GPU regression suite passed (1/1, debug validation and profiling enabled).
Computer-use visual checks covered EA/loading, distinct button prices/times,
switching between dozer and command center, queued production and a countdown
changing from 10s to 2s. The clean benchmark exited with code 0. The updated staged
and build executables have SHA256
`357C2569995F34E7CAB6EBA26B82165A33B58A0324DD0C5374AB031B670147C5`.
Terrain noise remains visible and is separate from this UI performance fix.

## Previous performance follow-up — 2026-09-04

The roughly 30 FPS checkpoint below has been superseded. With the same
Tournament Urban / six-player / seed 12345 test, later builds measured roughly
104–109 FPS at 800x600 after loading. The 1280x720 repeat logged 104–112 FPS
after warm-up, with 109 FPS visible in the final window capture.
These are windowed presentation-rate samples, not a reproduced 400 FPS DX8
comparison or a promise for all maps, camera positions and battle sizes.

Implemented in the follow-up:

- Revisioned native DEFAULT-heap vertex/index buffers. Unchanged meshes stay in
  GPU memory; submitted frames retain old versions across writes and deletion.
- Dynamic UI/sorting arenas stream only the requested range instead of creating
  full-capacity GPU snapshots for each small edit. Draws inside write locks also
  take independent transient copies.
- Cached index ranges with revision invalidation and per-draw bounds checks;
  zero-base draws no longer build temporary rebased index arrays.
- Shared per-frame texture upload pages with 512-byte placement alignment,
  fence-safe reuse and preserved in-frame copy ordering.
- Pixel-shader variants for zero through four active material stages instead of
  sampling every texture stage for every sprite.
- Capability-checked DXGI immediate/tearing flags for VSync-off windowed
  presentation, including matching resize flags. This can permit visible tearing.
  Implementation follows Microsoft's guidance:
  https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/variable-refresh-rate-displays
- Opt-in GPU timestamps, presentation-rate logging and engine graphics CPU
  breakdowns via `GENERALS_D3D12_PROFILE=1`; profiling does not enable the debug layer.

Representative 800x600 samples: geometry uploads approximately 977 KiB/frame
(previously 128,328 KiB), native draw preparation approximately 1.5 ms,
texture-upload CPU approximately 0.2 ms, GPU execution approximately 1–2 ms,
fence wait approximately zero, and Present approximately 0.1–0.2 ms. Engine-side
UI painting remains around 4.5–5 ms, scene CPU around 2 ms and preparation around
1.8 ms. Those CPU-side costs remain candidates for further optimization.

Evidence: `build-full/d3d12-resident-final-800-runtime.log` and
`build-full/d3d12-resident-final-720-runtime.log` (last initialization section),
plus matching `*-timing.log` files. Earlier experiments are retained separately.
The GPU pixel suite also passes with profiling and D3D12 debug validation enabled,
including new tests for buffer reuse, mutation, owner destruction, dynamic arenas,
index-cache invalidation and all material stage-count variants. No CMake
reconfiguration or clean build was used.

## Current tested build

- Build: `build-full/Release/generals.exe` (existing Win32 Release configuration).
- Deployed copy: `steamfiles/Generals.exe`; SHA-256 matches the build output.
- Preserve the staged real Bink/audio DLLs. Do not replace them with build stubs.
- No changes were made to the H: Steam installation during this checkpoint.
- The final game test was closed; no benchmark was left running.

## Completed and verified in this checkpoint

- Native frame/fence lifetime, reusable upload pages, descriptor retirement,
  texture update ordering, PSO caching, render-target transitions and readback.
- Correct initial engine render states, row-major transforms, FVF strides,
  sorting-buffer offsets, four-stage material state and surface-upload revisions.
- Direct native input layouts consume the source vertices. Packed BGRA colors
  and UV transforms/projection are processed in vertex shaders, rather than
  expanding all vertices on the CPU for every draw. Input layout offsets are
  included in PSO cache keys. Only the referenced contiguous VB range is uploaded.
- Tree alpha cutouts, packed stencil-composite colors, and decal vertex colors,
  per-batch vertex offsets, world transform and material bindings.
- Visual inspection: EA/loading sequence, game UI, terrain, buildings and units
  render. The earlier solid rectangles around tree shadows disappeared after the
  decal fix. Terrain remains visibly noisy and requires further investigation.

## Performance evidence

Repeat test: 800x600 windowed, Tournament Urban, six-player auto-skirmish,
seed 12345, maximum 900 logic frames, profiling enabled and D3D12 debug layer off.
Camera/AI activity can affect these observations; this is not a comprehensive
benchmark across maps or resolutions.

- Before: approximately 14–15 FPS; native CPU draw preparation approximately
  43–44 ms and geometry traffic approximately 230,360 KB/frame.
- After GPU vertex processing and decal fixes: approximately 29–31 FPS after
  warm-up; representative native draw preparation approximately 12–13 ms and
  geometry traffic approximately 128,328 KB/frame.
- An intermediate sparse-index gather experiment regressed performance and was
  replaced. It is not the final implementation.
- Evidence: `build-full/d3d12-baseline-runtime.log`,
  `build-full/d3d12-pre-compaction-runtime.log`,
  `build-full/d3d12-final-timing.log`, and `build-full/d3d12-final-runtime.log`.
  Native logs append across launches; use the last initialization/run when
  comparing. Counters are sampled frames, not whole-run averages.

The final running process loaded system `d3d12.dll`, `D3D12Core.dll`, `dxgi.dll`
and `D3DCOMPILER_47.dll`. No D3D8/D3D9, DXVK, Vulkan or ReShade module was found
in that process snapshot. The generated game link dependencies contain native
D3D12/DXGI/compiler libraries, not d3d8/d3dx8/d3d9 libraries. This runtime check
does not mean all legacy source branches have been removed.

## Validation and incremental workflow

All commands below reuse the existing build directories. Configure/generate is
not a correctness check and is unnecessary for ordinary source edits.

```powershell
cmake --build build-full --target ww3d2 --config Release --parallel 16 -- /clp:ErrorsOnly
cmake --build build-full --target generals --config Release --parallel 16 -- /clp:ErrorsOnly
cmake --build GeneralsMD/Code/build-d3d12 --target d3d12_smoke --config Release --parallel 16
ctest --test-dir GeneralsMD/Code/build-d3d12 -C Release --output-on-failure
```

The GPU pixel regression suite passed with D3D12 debug validation enabled. It
covers solid/texture color, alpha cutouts and blending, in-frame texture updates
and owner destruction, four-stage materials, matrix convention, GPU UV transforms
and projection, sparse indices/base vertices/strips, invalid-index rejection,
shadow color, BGRA offscreen rendering, descriptor recycling, frame reuse,
resize and reinitialization. See the smoke build's `Testing/Temporary/LastTest.log`.

## Remaining work

- Verify residual terrain/shroud edges and depth-state fidelity after the mip
  and blend fixes; implement full world-lighting/fog fidelity.
- Finish specialized water/reflection/bump, tree animation, skinning and material
  features. Some native paths currently approximate or omit legacy effects.
- Audit direct specialized draws for material-state isolation and vertex layouts;
  the decal fix illustrates why smoke-test success alone is insufficient.
- Extend persistent geometry ownership to remaining specialized direct draws;
  the common mesh path now uses resident buffers. Continue profiling remaining
  UI submission (including tooltips) and scene preparation without changing gameplay pacing.
- Extend surface behavior beyond base edits/readback snapshots, optional screen filters and broader
  gameplay visual coverage (different maps, factions, effects and resolutions).
- Remove inactive legacy DX8 device/resource/shader branches and dependency
  headers without changing gameplay, physics, input or audio.

Do not describe this checkpoint as a completed D3D8-to-D3D12 port.
