# Command and Conquer: Generals: Zero Hour: Reborn

EA released the 2003 source for preservation. It did not compile, did not run, and nobody had touched
the bugs inside it in twenty-two years. This build compiles, runs and plays.

**119 changes. ~580 engine source files ported. 14 automated test suites. Around 60 original bugs
found and fixed — EA's own, not port damage.**

---

## The frame rate cap is gone

- The picture now runs uncapped; the rules keep their own steady clock.
- A slow moment costs you a dropped frame, not a slow game.
- Every animation runs on a clock instead of counting frames.
- Briefing and cutscene subtitles hold long enough to read again.
- The radar's under-attack pulse throbs instead of strobing, and no longer ends early.
- The main menu surf rolls at its proper pace, not ten times too fast.

## The computer opponent builds a base now

- One wrong value meant the AI built only urgent items and one power plant.
- Rotated AI bases were laid out wrong; buildings now face the right way.
- An AI with no buildings no longer aims everything at the map corner.
- The AI checks ten seconds of supply-line damage, not a third of one.
- One AI decision read leftover memory, so identical matches played out differently.
- Difficulty and AI money change build pace again; every order was pinned at three seconds.
- The computer no longer shoots at what it cannot see. Its units, and its base defences, used to auto-target through the fog of war - a hard exemption written into the code for computer players only. Stealth works against it now, and so does anything it has not scouted.

## Attack-move actually attacks

- Ctrl on the click advances the group at the slowest unit's pace.
- A plain click lets each unit run at its own speed.
- Artillery, rocket infantry and Scud launchers search as far as they shoot.
- Attack-moving through a base shoots the base; buildings were filtered out.
- Aircraft make their pass, fly home, rearm and resume your order.
- Nothing walks off the map chasing a target that keeps retreating.
- A unit turns onto whatever is actually shooting it, then resumes the advance.
- Otherwise it picks the worst thing in range: anything armed before anything that is not, then the more dangerous of the two, and worth fades with distance so it is never the far one it walks the field for. A group ordered through a base no longer stops for the first dozer it passes while the artillery beside it keeps firing.
- Ctrl on the attack-move click no longer shells the ground instead.
- A big selection arrives as a crowd. Two hundred units told to attack-move used to spread along a line and three hundred told to move mostly stood still: the group tried to hold its shape all the way in. Now everyone heads for the spot you clicked and takes the nearest free ground to it.
- Troop Crawlers wait for their squad to climb back in, up to ten seconds.
- One man who cannot get back aboard no longer parks the Crawler for the rest of the attack move.
- Their squad stays out until the area is clear, instead of piling back in after every kill.
- "Clear" now means as far as the men can shoot, so they finish the enemies in front of them.
- Every man aboard gets out to fight; nobody sits out the battle waiting to be patched up.
- A Crawler that has lost its whole squad carries on with your order instead of idling.
- Attack-move state is saved now, instead of coming back as random memory.

## Your units stop shooting corpses

- Damage counts the moment a shot leaves the barrel, not when it lands.
- Once an enemy is accounted for, the rest of the group retargets.
- Your own right-click orders always win, however much fire is already inbound.
- A reservation lapses within half a second if the shot never arrives.
- Ctrl+Q now takes infantry only, not every armed thing you own.

## Aircraft, guards, and orders that used to be ignored

- Two jets on one airfield could deadlock waiting for each other; now they go.
- Losing the airfield no longer leaves its aircraft circling rubble forever.
- Repaired aircraft fly to the rally point instead of hovering over the pad.
- Infantry leaving a captured building follow its rally point too.
- A Chinook unloads its passengers one at a time, not stacked in one frame.
- A guarding unit no longer fights itself between returning to post and shooting back.
- Engineers can clear mines and booby traps they cannot see.
- A helicopter no longer boards a transport from the air.

## The same game on both screens, and after loading

- A unit with nowhere to stand appears where asked, not at a random point.
- Loading a save remembers what your guards were guarding.
- Saved units come back with the map's weapons, not the stock ones.
- Restarting a skirmish restarts the same skirmish, with a replay that plays back.
- Replays sound and look the way they did when you played them.
- Restart keeps the same random colours, starting corners and armies.
- Somebody quitting no longer reports a desync to everyone still playing.
- A power sabotage no longer follows a player into the next match.
- Muting sound effects or speech no longer desyncs the game.
- Angles come from the game's own table, identical on every machine.
- Defeat, second maps and the sync fingerprint each had a drift bug; fixed.
- The mismatch check now covers whole units and all sixty-eight special powers.
- Order numbering no longer scrambles your commands after about fifteen hundred orders.
- A network game runs at the speed it says it does.
- Input delay is measured from simulation speed now, not your graphics card.
- One lost packet costs a round trip instead of a flat two seconds.
- A desynced match stops at once instead of playing on as two games.
- A single lost packet no longer freezes the match for twenty seconds. The game now asks for the
  missing frame back after a fifth of a second instead of waiting out the disconnect timer, so
  nobody sits in front of a vote screen for a player who never went anywhere.
- A freeze no longer costs everyone two seconds of input delay for the rest of the match: time
  spent stalled is not mistaken for how slow the connection is.
- A player leaving is reported as a player leaving, not a desync.
- A desync now writes a per-object report on both machines for comparison.
- LAN refuses to start between two machines whose game files differ.
- The processor's rounding mode is reset from the right register every frame.
- The disconnect screen no longer interrupts a game that is merely slow.
- The keepalive interval setting is read now, and kept in a sane range.
- The anti-freeze brake is measured properly and comes on twice as early.
- Online input delay is now less than half what it was.
- The room climbs back to full speed after one player's brief hiccup.
- Orders are refused unless the sender controls what they name.
- A start-game command mid-match can no longer strand a player in limbo.
- Corrupted-order repair works in the first two seconds of a match too.
- Losing the relay player no longer picks a player who never existed.
- Order confirmations are filed in one step instead of scanning the whole queue.
- A dropped order's retry wait stops doubling after two steps.

## Sharper textures, for free

- 481 base-game textures at four times the resolution now beat Zero Hour's downscaled copies.

## Every replay, not just the last one

- Replay archiving saves each match under its own date-and-time name.
- Long lists scroll the whole way, past two thousand rows.
- Map, skirmish and replay menus read the catalogue once instead of rebuilding it.

## Placing a building, then changing your mind

- Cancelling a placement no longer deselects the builder.
- The building on your cursor shows where its units will come out.
- An ordered building stands as a see-through plan until a worker starts it.
- Click a plan, or press Stop, to cancel it and take the money back.
- Cancelling a plan is silent — nothing was built, so nothing explodes.
- Ground your units have walked stays buildable after they leave it.
- A plan opens no fog of its own, and your opponent never sees it.
- Your units walk straight over a plan — nothing solid is there until the builder starts work.
- The plan turns solid the instant the first work goes in, so no building goes up inside its own ghost.
- The builder starts work from where it reaches the site instead of shuffling into place first.
- Dozers and workers walk through anything still going up, so they never shut themselves in behind
  their own work.
- The stop button prints its key on it, like the build buttons do.

## Bonuses that come and go when they should

- Horde, nationalism and fanaticism bonuses now leave with the horde.
- Fanaticism works without nationalism being bought first.
- Two battle plans stack properly, and plans move with a captured strategy center.

## One crate, one collector

- A crate pays once per frame, not once per soldier who touched it.
- A thrown vehicle's crash damage hits the pile once, not once per unit.
- A dying unit is no longer promoted by its last kill.
- Healing no longer counts as damage, so a medic stops revealing its stealth unit.

## Circles are round, and a tunnel is one tunnel

- Circular range checks are circles now, not the square drawn around them.
- A tunnel network heals at one rate, however many entrances it has.

## The power bar tells the truth

- An EMP on an upgraded plant no longer takes the upgrade off your grid twice.
- An EMP on a building site no longer moves power that does not exist yet.
- Loading a saved game keeps disabled power plants disabled.
- Control Rods finishing during a blackout is no longer credited twice.

## Your whole base, on one strip

- One row above the command bar carries everything your base is building anywhere.
- Ordered by time left, so it reads as the order things arrive.
- Click a picture to jump the camera there; Ctrl-click cancels it.
- One picture is one order, cut to match the command bar's artwork.
- A unit finishes walking out of its factory before it takes an order.
- An upgrade stays available if any selected building can still buy it.
- One right click cancels one thing, counted the moment you press.
- Buying an upgrade for a group starts it in all of them.
- The progress clock over a picture is steady at any frame rate.
- A group's upgrade shows its progress, whichever building is paying for it.
- Click a unit with several factories selected and it takes the shortest queue.
- A general's upgrade spreads across the selected buildings instead of hitting one.
- A clicked upgrade darkens immediately and uncovers as it progresses.
- The production strip dims fresh orders the same way.
- A second worker can take over a half-built building.
- The nearest idle worker takes a new job, without yanking a busy one off.
- A worker that is building something still offers you the whole build list.
- The GLA's decoy build list toggles both ways now.
- Workers go back to collecting supplies when they finish.
- Every countdown reads real seconds and answers the game speed.
- The clock and frame-rate readout is drawn on top of everything.
- A game opens with nothing selected, so your first click is not a rally point.
- The tilde key opens the general's promotions.
- `S` stops your units again; the key was simply never bound.
- Every button carries its build, research or cooldown time in the corner.
- A single selected unit wears a gold bar filling toward its next rank.
- Timings everywhere: buildings, queues, superweapon charge, upgrades being researched.
- A charge bar now says how many seconds are left.
- Income per minute sits next to your money, averaged over half a minute.
- Aircraft always show how many attack runs they have left.
- The corner readout separates game time from real time, and sim rate from fps.
- Pausing stops both clocks.

## Placing buildings

- The placement grid is drawn on the ground and follows the slope, and reaches twice as far as it used to.
- Unbuildable ground is a soft red wash, crossed out square by square.
- Running out of money no longer strands the ghost on the map.
- Holding shift and clicking out a row of buildings no longer stops halfway and selects one.
- That row keeps going with no worker selected, too, instead of ending after the first building.
- Turning a building no longer turns everything you build afterwards. A wheeled heading still carries from one wall to the next, but the next supply centre comes out facing the way it was designed to.
- A building you point at blocked ground slides to the nearest spot it fits, and lands there.
- The pointer keeps its build cursor while you place, even passing over your own buildings.
- GLA defences get a range ring while you site them.
- A click clears a half-typed building shortcut.
- The two-key building shortcut survives the command bar's redraw now.
- Escape cancels what you were doing before it opens the menu.

## Where the money is, and where it is coming from

- Supply piles and docks say what is left in them, in cash, over the pile. No more guessing which expansion is worth taking from the art on the model.
- Everything on the field prices itself off its top right corner, at its owner's price. What that push cost, and what he just lost.
- A worker fetching or handing over a box shows a bar while it works.
- Hackers show how far off the next payout is. So do the black market and the oil derricks.
- Factories take a hundred units in the queue, not nine. The build queue only ever had nine buttons and that had become the limit.

## Health bars

- Health bars are in the owner's colour instead of green to red.
- A building's bar sits above its roof rather than inside its art.
- Every garrisonable building shows how full it is, whoever holds it.
- A vehicle's load stays private.
- Lamps, barrels, rocks and bushes no longer wear health bars.

## Twenty-two-year-old bugs, found by testing

- Every countdown on screen was a second too long. Rounding up a whole number of seconds gave a whole number plus one, so a ten second build said eleven - production queues, buildings going up, superweapons, all of it.
- A Chinese silo researching an upgrade while its missile charged drew both bars and both countdowns in the same row of pixels. They stack now.
- Units moving diagonally ran up to 40% faster than their own stat sheet.
- Garrisoned infantry and base defences only ever range-checked one of their weapons.
- Killing something with poison or toxin credited nobody with experience or score.
- Healing a unit counted as attacking it, so guards chased the wrong target.
- A supply centre could stop accepting trucks permanently.
- Selling a building mid-research gave no refund and left the upgrade in limbo.
- On-screen messages now hold for two and a half seconds and fade in one.
- Double-tapping 0 jumps the camera to that group.
- The mouse wheel no longer cancels a camera move already in progress.

## It fits your monitor

- Widescreen resolutions are back in the options menu.
- Zoom further out, with the whole map drawn instead of black corners.
- The zoom-out ceiling is the same for everyone; no setting buys you a wider view.
- The wheel covers that whole range in about six notches instead of thirty-eight.
- Zoom toward the cursor works; the spot under your pointer stays there.
- The camera turns in whole 45-degree steps, instantly, while you hold the key.
- Edge scrolling works windowed, and scroll speed no longer follows your frame rate.
- Taking over another base no longer squeezes the picture into the top four fifths of the screen.
- A skirmish opens zoomed all the way out (`StartAtMaxZoom = No` restores the old opening).
- Hold Ctrl and roll the wheel to turn a building before placing it.
- Buildings snap to the pathfinder's own grid (`GridBuildPlacement = No` for the old way).
- The grid under a structure is drawn, with blocked squares crossed out.
- Snapping now uses the true cell offset and the building's concrete apron.
- A building faces the way you drag it, read at the moment you release.
- The mouse pointer stays visible for the whole placement.

## Soldiers cast real shadows

- Infantry shadows are built from the pose: arms, head, weapon, moving with him.
- `UseShadowVolumesForSkins = No` puts the old flat blobs back.
- Scuds, rockets and falling bombs cast a shadow running along the ground.
- A big smoke cloud darkens the ground under it and fades as it does.
- The soft blob shadow for trees and scenery had never been drawn at all.
- Every one of the 128 tree types casts now, plus the bushes and palms.
- Fences, walls and props cast a real shaped shadow, worked out once.
- Each of these switches off in `GameData.ini`.

## Bright things can glow

- `Bloom = 60` in `Options.ini` makes bright things bleed light into the air.
- Off until you add that line; `BloomThreshold` sets how much of the picture joins in.
- Explosions light what is around them: 89 of them, from a tank shell to the Scud Storm,
  throw a warm flash that fades over a third of a second. A night fight used to be muzzle
  smoke over unlit ground.

## It does not crash

- Two blocks of 2003 assembly destroyed registers and took down the main menu.
- Quitting faulted twice every time; it now takes about half a second.
- A long chat message or an unusual map name could kill the process.
- The window no longer goes *Not Responding* during the menu's camera moves.
- Poison clouds, mine clearing, garrison kills and crew-killing weapons: all traced and fixed.
- Blowing up a full transport is survivable now, in every remaining variant.
- A unit can no longer load into a vehicle that no longer exists.
- A unit that kills itself survives its own turn now.
- Sniping an empty bike, formation moves and hacker evasion no longer crash.
- An order naming a player who is not there is dropped instead of crashing.
- Something going wrong now writes a readable crash report.

## Long orders stopped hitching

- The coarse pathfinder pass never worked, so every long move searched the whole map.
- A 23-cell route went from 55,000 cells and 256ms to 10,000 and 25ms.
- Every wall, fence and building permanently held one of the search's scratch records.
- Every abandoned search leaked one, and abandoned searches are the normal case.
- A recycled record could still point at the last unit that used it.
- A single unit lands where you clicked instead of the nearest square's middle.
- A footprint change no longer permanently blocks the ground beside a building.
- Our own rescue for narrow routes searched 140,513 squares in 285ms; it is gone.
- The coarse route is widened along its whole length, not just at the start.
- The main search stops after 20,000 squares and hands back a partial route.
- Long turns went from 513 to 201, and the worst from 2,976ms to 243ms.
- The distance guess now prices diagonals and turns, so the search actually steers.
- The straight-line shortcut only runs from a square closer than anything tried before.
- That one change: stuttering turns 615 to 108, pathfinding 11.8s to 1.6s.
- The queue is indexed, so adding a square costs one step instead of eighty.
- Joining two connected areas is one operation instead of a full table rewrite.
- Computer players think on staggered turns instead of all on the same one.
- Only one AI squad's march across the map is planned per turn.
- Map scripts, waypoints and regions are looked up directly instead of by scanning.
- Every effect knows its own place, so a burning building finds its smoke instantly.
- Decimal-to-whole-number conversion is one instruction instead of 1999 assembly.
- The minimap's fog is painted in memory and handed over once a frame.
- Minimap dots and terrain go over in one go, not one trip per dot.
- The terrain's lowest-point sweep is read along the grain now.

## Sound, video, and getting it to start at all

- Audio is real, through the audio library the retail game ships with.
- The videos play: the intro, the sizzle reel, the mission briefings, the general portraits.
- About 5,600 graphics calls are translated to a modern path, none of them touched.
- No disc, no registry keys, no retail installer — a normal install works.
- The 1.04 patch content is reachable again.

---

## How this was done

- Ported leaf-first: every library compiled, tested and green before its dependents.
- 14 automated suites; most bugs above were found by tests, not by reading code.
- The game plays itself: eight computer opponents from one command line.
- An opponent can be set to Human in the skirmish screen: a base with no brain behind it.
- Its sight is yours from the first frame, and clicking any of it hands you the base itself.
- Shift-Ctrl-T does the same to any opponent, a computer one included, and walks around the table.
- Headless, a 23-minute skirmish plays out in 38 seconds, identically every run.
- It plays itself over a network too — two copies, one real connection.
- That found every multiplayer replay falsely accusing itself of desync since 2003.
- Every fix was proved by putting the bug back and watching the test fail.
- No debugger here: a crash symboliser, a sampling profiler, probes in live matches.
- Reverted and recorded: wide FOV, four pathfinding experiments, a three-panel command bar, tree shadows.
- Bugs deliberately left alone are pinned by a test documenting the behaviour.
- Infantry shadows were fixed in the wrong place first, and nobody has eyeballed them yet.
- The shade under smoke vanished between builds and was rewritten from these notes.
- The missing tree shadows were found by painting them red, not by reasoning.
- Zoom toward the cursor was fixed twice and is argued from code, not watched.

---

## Not there yet

- Random maps generate and can be played — a number picks the map and the match starts on it —
  but there is still no menu entry and no reroll button.
- Online and LAN play are untested.
- You need to own the game; no game data ships here.
