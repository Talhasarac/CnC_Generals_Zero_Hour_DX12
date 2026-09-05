# Native D3D12 integration status — 2026-09-05

The interrupted parallel run has been stopped. Integration and verification are
now handled locally by the main agent, with no further delegation. The earlier
assignments were scope proposals, not completed migrations.

## Repaired and integrated in this recovery batch

- Native scoped draw-state snapshots restore nested passes, viewport/scissor,
  material resources, and pipeline settings without mutating the old state cache.
- Render2D directly submits native position/color/UV geometry; UI gradient stages
  are isolated from terrain stages, and grayscale icons keep opaque blending.
- Terrain tracks use a vertex-buffer slice with relative indices, not a vertex
  offset accidentally passed as an index offset. Camera matrices use the engine's
  D3D-depth projection and the native row-vector convention.
- Bib overlays have their own native pass and unconditional depth comparison.
- Background tile submission preserves the parent cloud/noise/shroud material.
  The parent material producer is still legacy-style; this is NOT a complete
  terrain-state migration.
- Trees use native cutout/shroud/sway state and world-space shroud coordinates.
- Volumetric shadow geometry has explicit increment/decrement native pass state.
  The composite reads only shadow-count stencil bits, preserves player-color bits,
  does not modify stencil/depth, and modulates scene RGB rather than overwriting it.
- Player-color stencil overlays no longer inherit arbitrary preceding depth,
  alpha-test, blend, or lighting state.
- Native draw descriptions validate layout/ranges and preserve baseVertex on
  textured submissions. They are a migration interface, not proof that producers
  have stopped authoring legacy state.
- Unsupported CPU surface formats stay empty/native, with zero dimensions and a
  null lock. They never fall back to a null D3D8 device.
- Resident GPU buffers start in COMMON with explicit copy/read transitions,
  eliminating the initial-state diagnostic warning.

## Additional native producer migrations

- ShaderClass now exposes an explicit native pipeline description, and texture
  filters expose native sampler descriptions. New consumers do not reconstruct
  these from the D3D8 state cache. Existing mesh consumers still use the old API.
  The historical source-blend enum value 3 is intentionally preserved as
  DEST_COLOR (the engine's actual mapping), despite its misleading enum name.
- ShaderClass also authors native base/detail texture operations directly.
  CPU tests enumerate all 52 detail color/alpha combinations and six primary
  modes, including alpha operands, bump operations and reversed subtraction.
  Render2D's gradient path consumes this shared builder, and its obsolete D3D8
  fallback and D3D8-specific includes have been removed. Mesh callers are not
  migrated merely by adding this builder; their bindings/mappers remain below.
- Native FVF layout construction is cached by format and stride. This removes
  repeated decoding, not the remaining legacy buffer/resource contracts.
- Native material coordinates now distinguish camera-normal and reflection-vector
  generation from authored UVs. The native vertex shader generates these from
  the mesh normal and current world/view transform, including unlit materials.
  Normal transforms are refreshed when the world/view matrix changes, not only
  when lighting is enabled. The remaining legacy material builder forwards these
  modes during migration; mapper producers themselves still need replacement.
  GPU tests verify normal/reflection sampling with conflicting UVs, disabled
  lighting, mirrored/nonuniform transforms, and changed surface normals.
- TextureMapperClass now exposes a native coordinate-authoring contract for
  scale/linear/grid/rotate/sine/step/zigzag/random UV animation and classic/grid
  normal/reflection environment maps. Matrix and composite projectors implement
  the same contract, used by projected terrain shadows without subtype casts.
  The camera-independent entry point rejects screen/world-space environment
  mappers; these now have an explicit NativeMapperContext entry point carrying
  view, projection and world/view matrices. Grid world-space environment and
  edge mapping also author native coordinates. Screen projection carries clip W
  into the projected divisor instead of using depth Z. Bump mappers now describe
  UV animation, the rotating bump matrix and signed sample decode parameters.
  VertexMaterialClass has a native mapping-description builder that resolves UV
  channels through a native vertex layout and preserves caller-owned material
  operations. Projected terrain shadows consume it with explicit camera context.
  Immediate generic meshes and non-sorted procedural passes now consume these
  descriptions; the transparent sorter and its material-pass path still need switching.
  Contract tests verify UV offsets, matrix conventions, environment modes and
  unchanged output on unsupported requests. These changes are included in the
  steamfiles executable deployed with the mesh-decal integration batch below.
- Snow uses native vertex/index data and explicit pipeline, camera and sampler
  settings. The unused D3D8 point-sprite allocation/draw path was removed.
- Water wakes/tracks author base/shroud material bindings directly and submit
  native buffer slices. Their animation and geometry generation are unchanged.
- Script fades and the status-circle/team-dot render path set their native
  blend operations explicitly, including reverse subtract and multiply.
- Roads now author native base/cloud/lightmap passes. The second lightmap pass
  retains the road-alpha mask, rather than changing translucent-edge blending.
- Bridges use explicit native base/cloud and separate depth-equal shroud passes.
  Damage-state loading and visibility logic are preserved.
- Shadow decals bind native pipeline/material state and camera matrices without
  cached texture transforms. Full object-projected shadows are still separate
  unfinished work; this change must not be described as converting all shadows.
- Projected terrain shadow draws now use native projector materials and textured
  submission rather than an untextured draw preceded by legacy material setup.
  MatrixMapperClass authors native orthographic, perspective, depth-gradient and
  normal-gradient coordinates; CompositeMatrixMapperClass composes its internal
  mapping without temporarily changing ViewToPixel. CPU tests verify each mode.
  Arbitrary object-projected mesh passes and generic mapper producers remain
  unfinished. The standard USA smoke scene is not proof of all projector modes.
- Heat distortion/smudges are no longer disabled by a D3D8 hardware probe.
  A GPU-only CopyCurrentRenderTarget snapshot preserves the active render target
  and transitions the copied resource for sampling without CPU readback or a
  fence wait. Native five-vertex distortion fans use view-space geometry,
  viewport-aware UVs and vertex opacity. Empty smudge lists do not trigger copies.
  The vertical distortion now uses the Y offset, correcting the old X-for-Y typo.

## Native mesh-decal integration

- Rigid and skinned mesh decals now author native vertex layouts, material
  bindings, mapper coordinates, camera transforms and lighting. They submit CPU
  geometry directly through the renderer's native upload/draw path; decalmsh.cpp
  no longer binds legacy buffers or calls DX8Wrapper. Skin deformation is unchanged.
- Mesh-decal dispatch passes its camera explicitly. Per-run native state is scoped,
  preserving surrounding passes. Material runs compare the materials at polygon
  vertex indices, fixing the previous polygon-index/vertex-index mismatch.
- Native light-environment authoring converts engine world-space directional and
  point lights into explicit camera-space constants without replaying old states.
  Vertex material authoring preserves the supplied lighting environment.
- Both native PSO variants now support signed rasterizer depth bias. Bias is part
  of the PSO cache key and captured/restored state. Decals use negative bias instead
  of capability-dependent displacement of authored geometry.
- The updated executable was copied to steamfiles (SHA256 matched the build).
  A 20-second USA/Winding River diagnostic run reached frame 1380 with no early
  exit or logged errors/warnings. This was nonvisual and terminated by the test
  harness, not a graceful-exit or combat-decal visual validation.

## Immediate rigid/skinned mesh queue

- Non-sorting texture categories now receive explicit native geometry views from
  their rigid or dynamic skin containers. Polygon ranges validate index/base-vertex
  bounds and submit directly with cached native vertex layouts and resident owners.
- The native category path authors pipeline/material/mapper state, camera-aligned
  and oriented transforms, skin world-space transforms, per-object lighting and
  scene fog. Opacity/additive/forced-multiply and temporary UV overrides are kept.
  Sorted categories still use the old queue until its shared ordering is migrated.
- TextureClass::Prepare_Native_Texture preserves lazy initialization, last-use time
  and editable-surface uploads without applying legacy texture-stage state. Both
  the new mesh path and mesh decals use this preparation method.
- CPU tests cover blend/opacity policies and indexed ranges, including overflow
  rejection without mutating the description. Release renderer/game builds and
  GPU smoke tests pass. The copied executable ran USA/Winding River, continuing
  to issue draws with no latest-session D3D12 error/warning messages.
- Visual verification for this batch was attempted using computer-use, but window
  capture failed (CreateForMonitor 0x80070057), then activation failed with
  GetCursorPos access denied (0x80070005). No screenshot was obtained; visual parity
  remains unverified. The owned test process was terminated after diagnostics.

## Native texture resource preparation

- All external Upload_Native_Surface callers now use resource-only preparation,
  including native Render2D, terrain/world helpers, trees, snow, roads, bridges,
  projected shadows and fades. Raw upload is protected within TextureClass so draw
  producers cannot bypass lazy initialization and usage tracking.
- Prepare_Native_Texture is virtual for resource-specialized textures.
  AlphaTerrainTextureClass refreshes a shared view of the base atlas in that method,
  independently of its still-unconverted material-state Apply method. It must not
  upload its throwaway 8x8 CPU surface over the shared terrain atlas.
- Existing cache consumers invoke preparation through TextureClass::Apply; native
  producers do not need that state-setting entry point. CPU tests cover first-use
  initialization, repeated use, virtual dispatch and reinitialization after inactivity.
- Renderer/game incremental builds pass; 86 CPU tests / 2775 checks pass. The copied
  executable completed a 20-second USA/Winding River diagnostic smoke without an
  early exit or latest-session D3D12 error/warning messages. No new visual capture
  was attempted; this does not close the existing visual validation gap.

## Composite projector context and ownership

- Native matrix composition now passes NativeMapperContext recursively through
  composite projectors. Screen and world-space environment matrices use explicit
  projection/view inputs; camera-independent mapper families retain their animation
  math. Unknown matrix producers fail without changing the caller's output.
- Removed the context-free Get_Native_Coordinates value API. Ordinary matrix
  projectors still support checked world/view-only coordinate authoring; composites
  reject that abbreviated contract because an inner mapper may require a camera.
- Fixed composite copy construction adding a second reference to an already-owned
  cloned inner mapper. Nested clone/destruction tests verify all inner instances
  are released, without invoking the legacy Apply/Calculate camera paths.
- Renderer/game builds and the mapper tests pass. These changes are included in
  the procedural-pass executable deployed and smoke-tested below.

## Native procedural mesh material passes

- MaterialPassClass now authors native pipeline, material bindings, mapping,
  lighting and fog descriptions. It rejects unsupported stages/mappers rather
  than falling back to legacy installation. Existing legacy consumers remain until
  their geometry/ordering paths are migrated.
- Non-sorted rigid/skinned procedural queues, including delayed passes, now use
  explicit geometry and camera inputs. Culled APT triangles are submitted directly
  with validated base-vertex ranges. Per-mesh opacity/emissive overrides modify
  value descriptions, not shared VertexMaterialClass assets.
- Shroud and crossfade-mask subclasses implement native projection descriptions.
  Their world-space planar mapping no longer inverts or reads the cached D3D view
  matrix. Scoped native state replaces installation/uninstallation in mesh callers.
  Scene color masks and surrounding stencil state are preserved.
- Renderer/game incremental builds and 90 CPU tests / 2855 checks pass. Tests cover
  procedural pipeline/material/lighting authoring, failure preservation and planar
  projection with nonidentity world transforms. The updated steamfiles executable
  ran USA/Winding River for 20 seconds, reaching frame 1440 without an early exit
  or logged D3D12 errors/warnings. This is nonvisual; crossfade/shroud appearance
  and sorted variants still require validation/migration respectively.

## Additional heightmap shroud/mask passes

- HeightMap's additional shroud and alpha-mask passes now use the shared native
  material-pass contract, explicit camera matrices and validated tile VB/IB views.
  Scoped state preserves the scene color-write mask and stencil state. Those two
  callers no longer install/uninstall legacy material state around terrain draws.
- The separate wireframe caller was migrated in the subsequent batch below.
  Main terrain multipass materials and flat-terrain material setup remain unfinished.
- Existing renderer/game incremental builds pass. The updated steamfiles executable
  ran USA/Winding River for 20 seconds through frame 1440 without an early exit or
  logged D3D12 errors/warnings. This nonvisual integration check is not proof of
  shroud/mask visual parity. No shared renderer or test source changed in this batch.

## Native wireframe and textureless material arithmetic

- Native rasterizer fill mode is captured/restored and keyed in both PSO variants.
  Scene wireframe switches call the native rasterizer directly. Screen quads and
  Render2D explicitly stay solid. HeightMap's old renderTerrainPass was removed;
  its wireframe pass uses explicit native geometry, pipeline and material values.
- Enabled material submissions without textures now execute the material shader,
  rather than silently bypassing factor/stage arithmetic through the basic shader.
  A renderer-owned neutral SRV is created/uploaded once on demand and released on
  shutdown. Terrain wireframe uses a GPU gray factor with resident geometry, not
  CPU vertex recoloring. This does not migrate the remaining legacy producers.
- Incremental renderer/game builds, 90 CPU tests / 2855 checks and GPU smoke pass.
  New pixel checks cover both wireframe PSOs, edge/interior coverage, scoped fill
  restoration, solid screen quads, textureless factor and multistage arithmetic,
  no-UV/base-vertex submissions, and returning to the basic diffuse path.
- Custom edging inspection found its sole caller guarded by TEST_CUSTOM_EDGING;
  it remains an optional legacy path, not evidence of active production rendering.
- Updated steamfiles/Generals.exe ran USA/Winding River for 20 seconds through
  frame 1440 without early exit or logged D3D12 errors/warnings. The owned process
  was stopped afterward. This was nonvisual and does not validate wireframe
  appearance, complete material parity, or graceful shutdown.

## Flat terrain producer conversion

- FlatHeightMap now authors explicit base and framebuffer-modulation passes:
  prelit diffuse, optional world-projected shroud, baked per-tile texture, then
  cloud/light-map multiplication. Native camera/world transforms, filtering,
  color mask and scoped state replace its shader-manager pass replay.
- W3DTerrainBackground::drawVisiblePolys accepts the parent's material value,
  selects the existing 1x/2x/4x tile texture, validates geometry and submits it.
  It no longer calls DX8Wrapper or builds a material from the legacy cache.
  Unrelated shoreline/road/bridge setup remains outside the native scope.
- Shared native_terrain_material.h is tested by GPU pixels for plain tiles,
  shrouded tiles, textures-disabled rendering, cloud-only, noise-only and combined
  framebuffer multiplication. Noise-only filtering is explicit, rather than
  depending on a previous cloud pass's sampler state.
- Renderer/game incremental builds, 90 CPU tests / 2855 checks and GPU smoke pass.
  FlatHeightMap.cpp is compiled by gameenginedevice, but normal gameplay selects
  HeightMapRenderObjClass. No alternate-renderer scene validation is claimed and
  no normal match was rerun as a substitute. Main HeightMap materials remain next.

## Main heightmap terrain passes

- Main HeightMap now submits its base atlas, UV1 alpha blend and optional
  cloud/light-map framebuffer multiplication through authored native materials
  and pipelines. Original pass order, reflection cloud/noise exclusions, night
  behavior, filtering, prelit colors and RGB write masks are retained explicitly.
  Depth-only terrain passes explicitly suppress color writes. Legacy terrain
  shader-manager setup/draw calls and inactive pretransformed draws were removed
  from this main path; remaining extra-blend/shoreline setup is separate.
- GPU pixel tests verify UV0/UV1 selection and texture-alpha times vertex-alpha
  blending at zero, intermediate and full vertex alpha. Shared modulation tests
  cover cloud/noise combinations. Renderer/game builds, 90 CPU tests / 2855 checks
  and GPU smoke pass using the existing build directories and 16 jobs.
- Third-texture extra-blend tiles still use the old road-material setup and need
  migration. Scene visual parity and special depth/reflection scene coverage
  remain unverified; shader-unit tests do not establish full terrain parity.
- Updated steamfiles/Generals.exe ran USA/Winding River for 20 seconds through
  frame 1500 without early exit or logged D3D12 errors/warnings. The owned test
  process was stopped afterward. This was a nonvisual integration check.

## Third-texture terrain blend tiles

- renderExtraBlendTiles now receives an explicit camera and submits native
  geometry/material/pipeline descriptions. It no longer binds engine-cache
  buffers, installs a legacy material or invokes the road shader manager.
- Dynamic index access exposes its native buffer and allocation offset, matching
  dynamic vertex access. The caller validates byte ranges and offsets both views
  into their shared allocations, preserving buffer ownership for native uploads.
- Base overlay alpha, optional cloud/noise stages, the masked second light-map
  pass and white debug mode are authored explicitly. Existing geometry generation,
  visibility, buffer growth and successful-pass statistics remain intact.
- Renderer/game builds, 90 CPU tests / 2855 checks and GPU smoke pass. New pixel
  tests exercise base alpha overlay, single modulation and masked second-pass
  multiplication. Broad scene/three-way seam visual parity remains unverified.
- Updated steamfiles/Generals.exe ran USA/Winding River for 20 seconds through
  frame 1500 without early exit or logged D3D12 errors/warnings. The owned process
  was stopped. This is nonvisual integration evidence, not seam appearance proof.

## Native shoreline alpha passes

- Both BaseHeightMap shoreline paths (game-sorted and editor-unsorted) now use
  native camera, material, pipeline and dynamic geometry submissions. The effect
  writes alpha only and restores the caller's state on normal and early exits.
  Native target format checks replace the cached backbuffer format dependency.
- NativeTerrainDrawDynamic validates both allocation counts and byte ranges,
  including dynamic vertex/index offsets, before submitting resident-owner views.
  Shoreline geometry generation, sorting and batching were preserved.
- Renderer/game incremental builds, 90 CPU tests / 2855 checks and the existing
  GPU regression suite pass. This batch did not add shoreline-specific pixel
  tests; shoreline appearance and WorldBuilder behavior remain unverified.
- Updated steamfiles executable ran USA/Winding River for 20 seconds through
  frame 1440 without early exit or logged D3D12 errors/warnings. The owned process
  was stopped afterward. This nonvisual check is not shoreline visual validation.

## River and flat-water materials

- Replaced setupFlatWaterShader/setupJbaWaterShader and their draw-time cache
  replay with explicit camera, native material/pipeline and dynamic submissions.
  River edge opacity, temporary sparkle/noise arithmetic, additive behavior,
  flat-water inline shroud and river's separate shroud multiply are preserved.
  Scoped state prevents water's four stages from leaking into later draws.
- Material stages are compacted when river edge texture is absent, fixing the
  disabled-stage gap that previously prevented later sparkle/noise evaluation.
  Noise projection uses world positions directly, not a cached view inverse.
- Updated the WorldBuilder caller for the explicit camera signature (not built
  or run as part of normal renderer iteration). Reflective sea, deforming-grid
  water and reflection resource integration are not completed by this batch.
- Renderer/game builds, 90 CPU tests / 2855 checks and GPU smoke pass. New pixel
  tests exercise the production water material helper for edge alpha, missing
  edge with highlights and missing sparkle. Water scene appearance remains
  unverified; unit pixels do not prove full visual parity.
- Updated steamfiles executable ran USA/Winding River for 20 seconds through
  frame 1440 without early exit or logged D3D12 errors/warnings. The owned process
  was stopped afterward. This was a nonvisual runtime check.

## Integrated native water rendering

- capture2.rdc pixel history at (450,355) identifies why forced sea looked opaque:
  EID 34846 outputs reflected color with alpha 0.50196, but fails depth testing
  (water depth 0.99254739 versus existing depth 0.99246550). The visible surface
  is terrain, not an opaque water shader. Reflection target 738 is populated and
  bound correctly. The forced test used ocean Z=7 rather than river Z=27.
  Diagnostic reflection modes now prefer the nearest river's upper authored
  height, falling back to a water polygon, and keep reflection/mesh/culling levels
  consistent. Normal map settings and depth testing are unchanged. Runtime logs
  confirm the corrected level 27 and native sea draws. User visual retest pending.

- Deforming-grid water now receives an explicit camera and uses scoped native
  pipeline/material/geometry submission, without DX8Wrapper state or sampler
  replay. CPU height/velocity simulation and grid vertex generation are preserved.
  Native index ranges are checked; transient vertices retain no resident owner.
- Reflection modes 1 and 2 now create native reflection targets, render mirrored
  scene/sky, restore camera/viewport/target state, and submit tiled XY sea patches
  with projected reflection coordinates. Targets resize with the framebuffer.
  Mode 2 lazily caches all 32 caustic gradient textures for animated distortion;
  mode 1 samples the reflection without distortion. Sea shroud is a native pass.
  Sky plane and sky-body draws now author native geometry/material/pipeline state.
  Grid Y-only resizing, low-opacity underflow, neutral bump decoding and degenerate
  sky billboard directions are corrected. Reflection clears match the resource's
  optimized clear color to avoid repeated D3D12 performance warnings.
- Renderer/game builds, 90 CPU tests / 2855 checks and GPU smoke pass. Smoke also
  checks wrapped gradient generation and perspective reflection-coordinate math.
  The updated executable is deployed to steamfiles; no game DLLs were replaced.
- Nonvisual USA/Winding River regression runs exercise reflective modes 1 and 2
  and an opt-in deforming grid. Logs confirm reflection frames, sea submissions,
  all 32 bump uploads, and a 4225-vertex/8446-index grid submission.
  GENERALS_D3D12_TEST_WATER_TYPE and GENERALS_D3D12_TEST_WATER_EXTENT are optional
  process-local test overrides; type 3 creates a diagnostic grid when absent.
  Normal launches use map/game settings, not these test overrides.
- Visual parity is still pending user playtesting. Historical mirror-plane
  clipping of intersecting objects and sorted sun behavior remain limitations;
  no claim is made for long-match/device-loss or every scripted water variant.
- User visually confirmed river water looked correct before this batch. This is
  not evidence for reflective sea or deforming-grid water.

## Verified

Existing build directories reused, Release configuration, exactly 16 parallel
jobs per build, renderer first and game second. No configure, clean, or full
solution rebuild.

- ww3d2 and generals compile/link successfully.
- test_ww3d2: 90 tests, 2855 checks. Explicit-context mapper tests
  cover camera rotation, unlit edge generation and perspective screen coordinates.
- d3d12_smoke: passes with GPU pixel checks, including nested state/resource
  restoration, textured base vertices, malformed submissions, and both stencil
  allocation modes. Additional pixel tests cover GPU framebuffer copies,
  offscreen copies, continued rendering to the original target, independently
  sampled copies, and rejection of self-copies/wrong-size/out-of-frame requests.
  Textured and untextured GPU tests also verify negative/positive decal depth bias,
  PSO cache separation and scoped bias restoration. CPU tests cover native material
  lighting, explicit point-light camera transforms, and rigid/skinned material runs.
- Earlier USA / Winding River at 1280x720: visually inspected base, minimap, trees,
  river, bridge, UI and shadow decals using the computer-use skill. This is a
  short visual smoke test, not a stock-renderer comparison or long-match test.
- Heat distortion's GPU copy primitive is pixel-tested; the effect itself still
  needs visual validation in a match with distortion-producing explosions/units.
  Snow, wakes and script-fade variants also need broader scene coverage.
- The inspected river view did not reproduce the historical left-edge wall.
- Latest diagnostic game session: no D3D12 errors, corruption, failed calls, or
  warnings. Diagnostic runs are not representative release FPS benchmarks.
- Only steamfiles/Generals.exe was replaced. Retail Steam files and DLLs were
  not modified. Test processes were ended after inspection, not used as evidence
  of a validated graceful shutdown.

## Still unfinished — do not label the whole port complete

| System | Main files | Remaining work |
| --- | --- | --- |
| Shared draw/material producers | WW3D2/dx8wrapper.cpp, shader.cpp, vertmaterial.cpp, matpass.cpp, mapper.cpp, matrixmapper.cpp, texturefilter.cpp | Replace producer-side mutable D3D8-style state with authored native pipeline/material/coordinate descriptions. The current builder still converts the old cache. |
| Mesh and sorting submission | WW3D2/dx8renderer.cpp, dx8polygonrenderer.cpp, mesh.cpp, dynamesh.cpp, sortingrenderer.cpp, dx8fvf.cpp | Immediate rigid/skinned categories and non-sorted procedural/delayed passes now author native material/layout/pipeline descriptions. Migrate the transparent sorter and its pass path, dynamic mesh producers and remaining parent buffer-cache plumbing. Preserve shared triangle ordering. Mesh visual parity still needs verification. |
| Full terrain materials | HeightMap.cpp, FlatHeightMap.cpp, BaseHeightMap.cpp, TerrainTex.cpp, W3DShaderManager.cpp, W3DTerrainBackground.cpp | Main HeightMap base/UV1/cloud/noise, third-texture overlays, shoreline alpha, additional shroud/mask/wireframe, and flat-terrain passes now use native descriptions and draws. Remaining parent cache plumbing and optional scorch/tree branches need audit. Terrain scene parity needs validation; obsolete terrain shader-manager implementations remain for cleanup. |
| Edging and mesh decals | W3DCustomEdging.cpp, WW3D2/decalmsh.cpp | Custom edging still needs explicit pass descriptions. Rigid/skinned mesh decals now use native submissions, but combat-scene visual coverage remains. Roads, bridges and projected-manager shadow decals also need broader visual coverage. |
| Remaining effects | Shadow/W3DProjectedShadow.cpp, Water/W3DWater.cpp, WW3D2/pointgr.cpp, linegrp.cpp, seglinerenderer.cpp, streakRender.cpp | Remove remaining active cached-state setup for object-projected shadows, main water, particles and lines. Particle sorting must be migrated together with sortingrenderer.cpp to preserve ordering with translucent meshes, not replaced with immediate draws. Native drawing already present is not full state-interface migration. |
| Camera/frame and resource APIs | WW3D2/camera.cpp, ww3d.cpp, texture.cpp, surfaceclass.cpp, dx8caps.cpp, formconv.cpp | Remaining old types/cache contracts and proven inactive resource/device branches; preserve asset formats. Native viewport/clear calls are already integrated. |
| Indicators and optional features | W3DInGameUI.cpp, W3DDebugIcons.cpp, W3DGranny.cpp, W3DWebBrowser.cpp, WW3D2/dx8webbrowser.cpp | Audit active indicator paths separately from generic Render2D and the now-native status-circle/fade paths; unsupported historical browser/Granny features are not implemented by disabling their initialization. |
| Coverage and distribution | renderer tests, runtime integration, documentation | Long matches, multiple factions/maps, large armies, resolution/minimize/restore, device/resource lifetime, clean-checkout packaging and visual comparison with stock. |

Occurrences in comments or dead branches are not proof of an active D3D8 runtime.
The running renderer issues native D3D12 commands, but much of its producer-side
state interface still needs architectural migration. No D3D9On12 or DXVK is used.
