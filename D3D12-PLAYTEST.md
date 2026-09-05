# Native D3D12 playtest

This is a development build, not a feature-complete renderer release.

## September 6 integrated transparency/mesh/shadow batch

The staged build includes native queued descriptions for transparent meshes,
particles, line groups, segmented/streak trails, ring/sphere effects, dynamic
meshes, and sorted procedural material passes. They retain the existing shared
far-to-near triangle ordering rather than rendering particles immediately.
Projected-shadow resources and simple decals no longer require a D3D8 device.

For this batch, play USA on Winding River, then a larger map:

- Build Missile Defenders/Humvees and attack: watch missile trails, smoke,
  explosions, and transparent effects overlapping vehicles/buildings.
- Pan/rotate/zoom while effects overlap. Look for wrong-depth blending,
  flickering, solid rectangles, disappearing ribbons, or displaced effects.
- Check shadows, scorch marks and decals on slopes and around bridges.
- Check transparent/animated meshes, material overlays, and vehicles reflected
  in water. The approved water appearance and tank reflections are unchanged.
- Save/load and start a second match to exercise queued resource cleanup.

Optional custom terrain edging has native texture/mask/modulation passes, but
its caller is still gated by the existing `TEST_CUSTOM_EDGING` build switch.
Ordinary skirmishes do not validate that optional feature; it is not enabled
silently as part of this migration.

## Run the staged game

From the repository root in PowerShell:

```powershell
Start-Process -FilePath "$PWD\steamfiles\Generals.exe" -WorkingDirectory "$PWD\steamfiles" -ArgumentList '-win','-quickstart','-xres','1280','-yres','720'
```

The copied game directory contains the current development executable. This does
not launch the original Steam executable. Choose USA for the initial skirmish.
Start with Seaside Mutiny, then Winding River. `-quickstart` skips the intro; omit
it when specifically testing the EA logo and movies.

## What to check while playing

- Play a 20-minute match, build a sizeable army, and move it across the map.
- Check minimap terrain/icons, newly revealed shroud, roads, slopes and water edges.
- Check animated units, tree wind, shadows, weapon effects, smoke and rain.
- Move the camera over rivers and bridges. The Winding River vertical left-edge
  streak and black river shroud pass have been fixed; check for recurrence after
  panning, save/load and a second match. Shoreline blending still needs work.
- Exercise minimize/restore, resolution changes, save/load, and a second match.
- Campaign/script-driven camera blur, crossfades and monochrome tint/fades have
  native implementations but still need mission-level testing.

For a bug report include the map, faction, resolution, action that triggered it,
and a screenshot. Preserve `NativeD3D12.log` and `DebugLogFile.txt` from the copied
game directory. The native log appends sessions; use the last initialization.
Do not publish these logs without reviewing them for personal paths or data.

## Building and staging from another checkout

Owning the Steam game supplies runtime assets, not the source build dependencies.
Follow README.md and D3D12-MIGRATION-NOTES.md for the external SDK/source drops,
Visual Studio C++ tooling, Windows SDK, and Win32 build setup. A clean-machine
full-game build has not been certified yet.

Once configured, reuse that build directory:

```powershell
cmake --build build-full --target ww3d2 --config Release --parallel 16
cmake --build build-full --target generals --config Release --parallel 16
```

Copy your legally installed Zero Hour files to an isolated test directory. With
the game closed, back up its executable and replace **only Generals.exe** with
`build-full/Release/generals.exe`. Keep the installation's original Bink/Miles
DLLs; do not copy the build's stub DLLs over them. Launch with the copied directory
as the working directory, as above.

Native HLSL ships in `native_d3d12_renderer.cpp` and `native_d3d12_lighting.h` and
is compiled at runtime. No separate rewritten shader binary or game texture pack
is needed. Missing original assets are distinct from renderer faults. The optional
locally generated StreetHoleCover texture is not distributed in this repository.

The active rendering path uses native D3D12. Legacy source/types/header dependencies
still remain; this document does not claim they have all been removed.
