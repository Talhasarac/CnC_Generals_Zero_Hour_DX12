
# Command & Conquer Generals (inc. Zero Hour) Source Code

This repository includes source code for Command & Conquer Generals, and its expansion pack Zero Hour. This release provides support to the Steam Workshop for both games ([C&C Generals](https://steamcommunity.com/workshop/browse/?appid=2229870) and [C&C Generals - Zero Hour](https://steamcommunity.com/workshop/browse/?appid=2732960)).


## What This Fork Changes (Zero Hour)

This fork builds Zero Hour with Visual Studio 2022 (CMake, Win32) and adds
quality-of-life and modernization changes on top of the retail 1.04 behaviour.
You still need to own the game — the fork ships no game data.

Everything below is player-visible. Nothing changes unit stats, weapons or
balance.

### Runs on modern PCs

- **Direct3D 9 renderer.** The game's DirectX 8 calls are translated to DX9 by
  a `d3d8.dll` built from source and placed next to the exe, so the game runs
  on current GPUs and drivers without the legacy DX8 runtime.
- **Real sound.** Music, speech and effects play through the game's own
  `mss32.dll`, exactly as retail does.
- **Widescreen resolutions.** The Options menu no longer hides non-4:3 modes —
  1920x1080, 2560x1440 and the rest of your monitor's native modes are listed
  and selectable. The view itself keeps retail proportions (same horizontal
  view as 4:3), which is what 16:9 players have always used.
- **The 1.04 menu text is back.** "Custom Match" on the Solo Play submenu used
  to read `MISSING: 'GUI:CustomMission'`; the patch's text overlay is now
  loaded.
- **Crash reports are readable.** If the game does die, it writes a full,
  symbolized report (with file and line numbers) to
  `Documents\Command and Conquer Generals Zero Hour Data\ReleaseCrashInfo.txt`.

### Framerate and camera

- **Uncapped framerate.** Rendering runs as fast as your machine allows, while
  the game simulation stays locked at its original rate — so game speed does
  **not** change with framerate. Scrolling speed and camera shake are now
  time-based, so they feel identical at 60 or 240 FPS.
- **Smooth zoom.** Zooming used to update only on the fixed 30 Hz camera step
  and stuttered on fast displays; it now moves every rendered frame at the same
  speed as before.
- **Zoom out further.** The manual zoom-out limit is stretched to 1.6x the
  retail ceiling. Maps still open framed the way their author intended —
  default, scripted and reset views are untouched.
- **Optional: raise the ceiling yourself.** Add `MaxCameraHeight = 465` to
  `Options.ini` (in your Zero Hour Data folder) to push the cap further. The
  retail value is the floor, so this can only ever zoom out more, never closer,
  and it applies at every detail preset.

### Reading the battlefield

- **Health bars are always on.** No need to select or hover a unit — every
  unit and building shows its health bar (the "Show object health" option still
  turns the whole system off).
- **Selection is easier to see.** A selected object's health bar gets a white
  line above it.
- **Group numbers are always visible**, not just while selected.
- **Transport and garrison pips always show**, including empty slots, so you
  can see capacity at a glance. (They stay hidden on buildings still under
  construction, which cannot hold anyone yet.)
- **Construction shows time, not percent.** A building going up displays the
  seconds remaining — measured from the progress your workers are actually
  making, so extra builders shorten the number — both as floating text over
  the site and in the construction panel.
- **Production progress on the bar.** Your own factories show a yellow
  production-progress bar with the seconds left on the current unit.
- **The whole queue is timed.** The production label reads e.g. `52s (70s)`:
  the current unit first, then everything still queued behind it.
- **Rally lines draw correctly.** With several buildings selected, every rally
  line is drawn over the buildings instead of all but the last disappearing
  behind them.

### Building and commanding

- **Select buildings as a group.** Buildings can be box-selected, shift-added
  and double-clicked (all of a type on screen), just like units. Units and
  buildings still never end up in the same selection.
- **Builds spread across factories.** With several same-type factories
  selected, each click sends the unit to whichever selected factory has the
  shortest queue.
- **Shift-click to queue five.** Holding Shift on a build button queues 5 units
  in one click, spread across your selected factories.
- **Units find their own lanes.** Pathfinding now charges a small extra cost
  for tiles other units have already reserved on their path, so a moving group
  spreads into parallel lanes instead of stacking into one line.

### Getting more out of your GPU

No engine changes needed — these are `Options.ini` keys (back up the file
first):

- `StaticGameLOD = Custom` with `MaxParticleCount = 10000` (4x the High
  preset) for much denser explosions and smoke.
- Turn every quality toggle on: `UseShadowVolumes`, `UseShadowDecals`,
  `UseCloudMap`, `UseLightMap`, `ShowSoftWaterEdge`, `ExtraAnimations`,
  `HeatEffects`, `ShowTrees`, `BuildingOcclusion`, `TextureReduction = 0`,
  `AntiAliasing = 4`, and `DynamicLOD = no` so the game never silently
  downgrades quality mid-match.

### Under the hood

Dozens of latent bugs in EA's original code were found and fixed while porting,
including several that crashed the game outright on modern compilers (inline
assembly that corrupted CPU registers, buffer overruns in the replay and
compression code, an unusable crash-stack walker). These are invisible in play
— they are the reason the game runs at all. The full list is in
[CLAUDE.md](CLAUDE.md); the porting roadmap is in [PLAN.md](PLAN.md).


## Dependencies

If you wish to rebuild the source code and tools successfully you will need to find or write new replacements (or remove the code using them entirely) for the following libraries;

- DirectX SDK (Version 9.0 or higher) (expected path `\Code\Libraries\DirectX\`)
- STLport (4.5.3) - (expected path `\Code\Libraries\STLport-4.5.3`)
- 3DSMax 4 SDK - (expected path `\Code\Libraries\Max4SDK\`)
- NVASM - (expected path `\Code\Tools\NVASM\`)
- BYTEmark - (expected path `\Code\Libraries\Source\Benchmark`)
- RAD Miles Sound System SDK - (expected path `\Code\Libraries\Source\WWVegas\Miles6\`)
- RAD Bink SDK - (expected path `\Code\GameEngineDevice\Include\VideoDevice\Bink`)
- SafeDisk API - (expected path `\Code\GameEngine\Include\Common\SafeDisk` and `\Code\Tools\Launcher\SafeDisk\`)
- Miles Sound System "Asimp3" - (expected path `\Code\Libraries\WPAudio\Asimp3`)
- GameSpy SDK - (expected path `\Code\Libraries\Source\GameSpy\`)
- ZLib (1.1.4) - (expected path `\Code\Libraries\Source\Compression\ZLib\`)
- LZH-Light (1.0) - (expected path `\Code\Libraries\Source\Compression\LZHCompress\CompLibSource` and `CompLibHeader`)


## Compiling (Win32 Only)

To use the compiled binaries, you must own the game. The C&C Ultimate Collection is available for purchase on [EA App](https://www.ea.com/en-gb/games/command-and-conquer/command-and-conquer-the-ultimate-collection/buy/pc) or [Steam](https://store.steampowered.com/bundle/39394/Command__Conquer_The_Ultimate_Collection/).

The quickest way to build all configurations in the project is to open `rts.dsw` in Microsoft Visual Studio C++ 6.0 (SP6 recommended for binary matching to Generals patch 1.08 and Zero Hour patch 1.04) and select Build -> Batch Build, then hit the “Rebuild All” button.

If you wish to compile the code under a modern version of Microsoft Visual Studio, you can convert the legacy project file to a modern MSVC solution by opening `rts.dsw` in Microsoft Visual Studio .NET 2003, and then opening the newly created project and solution file in MSVC 2015 or newer.

NOTE: As modern versions of MSVC enforce newer revisions of the C++ standard, you will need to make extensive changes to the codebase before it successfully compiles, even more so if you plan on compiling for the Win64 platform.

When the workspace has finished building, the compiled binaries will be copied to the folder called `/Run/` found in the root of each games directory. 


## Known Issues

Windows has a policy where executables that contain words “version”, “update” or “install” in their filename will require UAC Elevation to run. This will affect “versionUpdate” and “buildVersionUpdate” projects from running as post-build events. Renaming the output binary name for these projects to not include these words should resolve the issue for you.


## STLport
STLport will require changes to successfully compile this source code. The file [stlport.diff](stlport.diff) has been provided for you so you can review and apply these changes. Please make sure you are using STLport 4.5.3 before attempting to apply the patch.


## Contributing

This repository will not be accepting contributions (pull requests, issues, etc). If you wish to create changes to the source code and encourage collaboration, please create a fork of the repository under your GitHub user/organization space.


## Support

This repository is for preservation purposes only and is archived without support. 


## License

This repository and its contents are licensed under the GPL v3 license, with additional terms applied. Please see [LICENSE.md](LICENSE.md) for details.
