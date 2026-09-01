# Command and Conquer: Generals: Zero Hour: Reborn

EA released the 2003 source for preservation. It did not compile, did not run, and nobody had touched
the bugs inside it in twenty-two years. This build compiles, runs and plays.

**119 changes. ~580 engine source files ported. 14 automated test suites. Around 60 original bugs
found and fixed â€” EA's own, not port damage.**

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
- It scouts. There was no such thing as scouting in the computer opponent - it never needed to look, because it could already see everything. One cheap unit now tours the enemy start positions and keeps going round for the rest of the match, replaced out of spare change when it dies. Every difficulty scouts; an opponent that never looks reads as broken, not as easy. And it tours on purpose rather than in a circle: it goes to whichever base it has gone longest without seeing, nearest first, and it does not walk across the map to look again at something it looked at a minute ago - it stays and watches instead. Two scouts never trail each other round the same lap.
- It takes the oil derricks instead of blowing them up. An oil derrick pays whoever owns it and costs one infantryman to walk in - and the computer used to march past the neutral ones and shell yours, which is the one thing you can do with a derrick that earns nobody anything. It now keeps a cheap infantryman doing the rounds of every derrick, refinery and hospital it has found, takes them, and when there is nothing left to take it leaves him standing on the last one instead of walking him home. And it no longer picks a capturable building as a target of its own accord: it can still be ordered to level one, and one that shoots back is still shot back at.
- Nor does it know which start position you took. It used to read that straight off the lobby, for every player, from the first second. Now it works it out: each position it has not looked at is a suspect, and the odds are simply how many opponents are still unaccounted for over how many places they could be - three of you on an eight-position map is 3 in 7 for each. Every empty position its scout crosses off makes the rest likelier, 3 in 6, then 3 in 5. When the numbers meet, it stops looking: three opponents with three places left to be is not a guess, and walking over to confirm it would waste the trip. On a two-player map that is true from the start, so nothing changed there - it never wastes a second hunting for an address it can work out by subtraction. Where it has not worked you out yet, it attacks the position you are most likely to be on, and finds out on arrival.
- Nor does it read your base off the map. Where your base is, what it is worth, which supply dock to expand to and where to aim a superweapon all came off a walk of your object list, in the shroud, from the first second of the match. The computer now only counts what it can see, plus the buildings it has already found - buildings do not walk away. Before it has scouted you, all it knows is where you started, which is on the map preview anyway.

## Generals powers the computer actually meant to buy

- It spent every promotion point the moment it had one, on whatever in its list happened to be cheap enough. The strong three-point ability at the end of the list was never reached, because the points were always already gone.
- From Medium up it saves. If the next thing in its own list is only out of reach on points, it waits for them instead of buying filler.
- Which list it draws from now follows its personality, so the powers coming at you tell you which kind of opponent you are facing.

## The computer comes in where you are thin

- The game has kept a value-and-danger map of every square of the battlefield, per player, since 2003. It has a query interface and even a debug view. Nothing in the computer opponent had ever read it - its only users were two map-script actions almost nobody used.
- At the top level the opponent now aims its attacks at where your money is rather than at the middle of your buildings, which is also the middle of your defences.
- It reads that map through the fog like everything else: only ground it has actually seen counts. An opponent that has not scouted you still attacks the old way.

## The computer expands, and defends what it takes

- It never decided to expand. The machinery was all there - place a supply centre beside a pile, pick a pile worth taking, send a team to sit on it - and every piece of it only ran when a map script said so. An opponent that ran its starting piles dry simply stopped earning.
- It now goes and takes the money that is lying around, out of spare cash so the army never pays for it.
- From Brutal up the expansion comes with a defence structure facing the enemy, placed in the same job. An undefended expansion is a gift.
- Measured: a third more army in the field and a quarter more money spent over the match.

## The computer spends its money

- It no longer sits on a pile of cash. Past a level that depends on the difficulty, the more money it has the faster it builds - twice the pile, half the wait, and it stops at four times. It is spending sooner, not building faster: the rate itself is untouched.
- It puts more harvesters on a supply centre with more piles around it, instead of the same three numbers everywhere.

## The computer picks its fights
- Which enemy it goes after was the nearest one and nothing else, plus a rule with its sign the wrong way round: an opponent who had lost his units or his production had his distance treated as half the map, so the computer ignored the one it was about to beat. That is what dragged matches out.
- It now weighs distance, whether the target is crippled - an opportunity, not a distraction - and how much of what it can see that player is worth. Only a genuinely finished enemy is skipped.
- It still refuses to gang up with another computer opponent on one victim, and still gently prefers whoever is already shooting at it.
## The computer knows when to quit

- Its teams fought to the last man. The word "retreat" appeared nowhere in the opponent's code - the single most visible thing that made it look stupid.
- It now measures the fight, not the health bar: how long its force lasts against how long it needs to finish yours. A unit at a fifth of its health that still out-damages what is shooting it stays; a full-health one being melted leaves. A health percentage gets both of those backwards.
- Two levels. From Steady up, a unit that is personally finished pulls out of a fight its team is still winning. From Brutal up, the whole team breaks off when the exchange is lost. Easy never quits - that is part of what makes it easy.
- It only counts what it can see. An opponent that flinches from something it has not found is reading your unit list again.
- It weighs a fight by what is in it, not by who is nearby: only things that can shoot count, and buildings do not. Its own base is not a reason to feel safe and yours is not a reason to run.
- Its aircraft are left out of it. A retreat order was what took a parked jet off the runway, so while a fight raged near the airfield the whole wing took off, flew at the base, landed, and did it again every few seconds instead of ever reaching you. Aircraft already fly home on their own when the load is spent.
- Matches finish. Two of these opponents used to fail to settle 65% of their games inside sixteen minutes; it is half that now, and they end in under seven minutes on average instead of ten.

## The computer builds against what you field

- Which unit it trained next was a coin flip. It gathered the teams sharing the highest priority number in its data and picked one at random - not one line looked at what it was fighting. An opponent facing nothing but aircraft went on building tanks.
- Now the priority is the start of a score, not the whole of it. What it can *see* you fielding weights the choice: air pushes anti-air up, stealth pushes detectors up, armour pushes anti-tank up. Its own data decides what counts as the answer to what, so a mod's units are read correctly too.
- How much that weighs depends on the level, from nothing at all on Easy - which is what the game always did - to fully on Merciless.
- Anti-air was the computer's oldest hole. It is closed.

## Six difficulty levels, and none of them cheat

- Easy, Steady, Medium, Hard, Brutal, Merciless. Medium to Brutal used to be a cliff; the steps in between are where a player actually needs them.
- Every level plays by your rules. No level gets extra money, cheaper units, faster building, longer vision or tougher units - in either direction. What changes is what the computer is allowed to decide: how often it looks at the map, how long it takes to react, whether it counters what you field, whether it masses before attacking, whether it pulls damaged units out.
- Each step up switches on exactly one new thing, so you can say in a sentence what you get for climbing.
- Measured, not asserted: Merciless beats Easy 15-0 over 32 headless matches with the seats swapped both ways, on twice the army and twice the spending.
- The three new levels showed up in the lobby as "Closed" and lost their start position on the map every time the list refreshed. Both fixed. Easy is slow and brave. Steady saves its damaged units. Medium expands on its own. Hard counters your build. Brutal masses and commits. Merciless is the ceiling - everything on, no hesitation.
- The ladder reads as one ladder wherever a seat is listed. Half of it was named in EA's old words - "Easy Army", "Medium Army" - and half in ours, in the same drop-down, because four separate lists answered "what is this seat called" and no two of them agreed. It is Easy, Steady, Medium, Hard, Brutal, Merciless now, in the lobby, on the seat, in the game info panel and in the online browser alike.
- All six are in the network and online lobbies too, not just skirmish. Those two only ever offered three, and a seat set to anything above Medium was sent to the other machines as Brutal - so a game meant to be played against Steady was played against Brutal by everyone at the table, host included. The seat list also drops down far enough to reach the bottom of it - it was still sized for the five entries the game shipped with, so the last levels were off the end of the list.
- Making Easy easier means giving it worse decisions, never less money. That is the whole promise.
- The computer also picks a personality each match and keeps it: one plays for the attack, the other for the base. Same resources, spent differently.

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
- A big selection arrives as a crowd. Two hundred units told to attack-move used to spread along a line and three hundred told to move mostly stood still: the group tried to hold its shape all the way in. Past forty units everyone now heads for the spot you clicked and takes the nearest free ground to it. Below forty the group keeps its spacing on the way in, because that is what stops a dozen tanks arriving in single file.
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
- Ctrl+D takes every unit of the kinds you have selected that is on the screen, and pressed twice inside half a second takes them across the whole map. One tank selected and two keystrokes is your whole armour, wherever it is standing. It was on Shift+Ctrl+E, which is three fingers for something you do in every fight, and it only ever reached across the map when the screen had nothing left to give - so the wide selection happened to you instead of being asked for.

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
- A turret loaded from a save is still tracking what it was tracking, and a worker loaded mid-job carries on with it, instead of both snapping back to their default state.
- A shell already in the air finishes its flight after a load instead of going off in the launcher's face.
- An aircraft carrier remembers the order it was given, and each runway its own ramp.
- A barracks that has already put its mob on the street does not put a second one out after a load.
- Effects tied together - a smoke column and its embers - stay tied together across a save.
- A loaded save puts the world back in the order it was saved in, so the match plays on from where it was instead of resolving everything backwards.
- And it shows you your own memory of the fog, not everyone else's: a loaded game used to draw what every player remembered on top of the world.
- Saved units come back with the map's weapons, not the stock ones.
- Restarting a skirmish restarts the same skirmish, with a replay that plays back.
- Replays sound and look the way they did when you played them.
- Restart keeps the same random colours, starting corners and armies.
- Somebody quitting no longer reports a desync to everyone still playing.
- A power sabotage no longer follows a player into the next match.
- Muting sound effects or speech no longer desyncs the game.
- Pausing no longer leaks a little memory for every sound that was about to start.
- A sound cancelled before it started really is cancelled. The check compared a pointer against a handle - two different things sharing one slot - so it never matched and the sound played anyway.
- Angles come from the game's own table, identical on every machine.
- Defeat, second maps and the sync fingerprint each had a drift bug; fixed.
- The mismatch check now covers whole units and all sixty-eight special powers.
- Order numbering no longer scrambles your commands after about fifteen hundred orders.
- A network game runs at the speed it says it does.
- A frame of orders now travels in one datagram instead of four or five: the packet was still the 476 bytes a 2003 modem could carry, and every extra datagram was another chance to arrive late.
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
- A player whose name holds a comma, a colon or a control character is turned away at the door instead of scrambling the lobby's idea of who is in the room.
- An open seat can be taken. The map's number of starting positions used to overrule the host: a four position map turned away the fifth player even with a seat left open for him, and that seat stayed open on everyone's screen.
- A map sent to you over the network has to be a map. The other machine used to name any file it liked and fill it with anything at all, and it was written where the name pointed; now the name cannot leave the map folder, the kind has to be one a map transfer carries, and the contents have to match the kind.
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
- A long thin texture loads at the size it was drawn at. Anything wider than eight to one used to be stretched onto a bigger, blurrier one, because that was the limit of a 2002 graphics card; the card is asked now, and modern ones have no such limit.

## Every replay, not just the last one

- Replay archiving saves each match under its own date-and-time name.
- Long lists scroll the whole way, past two thousand rows.
- Map, skirmish and replay menus read the catalogue once instead of rebuilding it.

## Placing a building, then changing your mind

- Cancelling a placement no longer deselects the builder.
- The building on your cursor shows where its units will come out.
- An ordered building stands as a see-through plan until a worker starts it.
- Click a plan, or press Stop, to cancel it and take the money back.
- Cancelling a plan is silent â€” nothing was built, so nothing explodes.
- Ground your units have walked stays buildable after they leave it.
- A plan opens no fog of its own, and your opponent never sees it.
- Your units walk straight over a plan â€” nothing solid is there until the builder starts work.
- The plan turns solid the instant the first work goes in, so no building goes up inside its own ghost.
- The builder starts work from where it reaches the site instead of shuffling into place first.
- Dozers and workers walk through anything still going up, so they never shut themselves in behind
  their own work.
- The stop button prints its key on it, like the build buttons do.
- A captured worker stops working for its old owner. Being captured cleared its orders but not its
  job, so it carried on building and repairing for the player who just lost it.

## Bonuses that come and go when they should

- Horde, nationalism and fanaticism bonuses now leave with the horde.
- Fanaticism works without nationalism being bought first.
- Two battle plans stack properly, and plans move with a captured strategy center.
- The Bombardment Cannon cannot be fired in the middle of a battle plan change. The turret was only
  switched off once it had finished swinging back, so the whole swing was still a firing window.
- Passengers do not step out of a transport that is itself inside something. They would have
  appeared inside whatever was carrying it.

## Weapons and units that were quietly wrong

- A bunker buster shot down on the way in no longer clears out the building it was aimed at. It emptied the bunker from wherever it happened to be destroyed - a Point Defence Laser did not save you, it just moved the explosion.
- A Battle Bus in its wrecked form takes attack orders again. Every order you gave it was dropped on the way, because the crew inside are 'held' and held units were skipped.
- A sniper cannot pick at an empty Stinger Site. The rule that stops snipers hurting empty buildings only counted passengers, and a Stinger Site's men are spawned, not carried.
- A booby trap shot off a building, or sold with it, releases the building. The mark was only ever cleared when the trap went off, so the building stayed marked for the rest of the match and no second trap could be laid.
- Something flagged unselectable is unselectable. The flag existed and nothing read it.
- Double-clicking to grab everything of a kind grabs what a box would, not things you can only click one at a time.
- A bounty pays what the percentage says. Rounding up a fraction that was already a whole number paid a dollar extra on every kill.
- A supply upgrade pays for what arrives. The bonus was a flat sum handed over on arrival, so a truck turning up with one box collected the same as a full load - and a driver dropping off little and often earned several times what the upgrade is worth.
- Taking over a defeated ally's base no longer starts his research again from scratch. A player upgrade is bought once for the whole player, and the buildings you inherit were left paying for a second copy of what you already had.
- A tank drives out the moment it is paid for. The factory doors only began opening once the vehicle was already finished, and it stood inside waiting for them, so every single unit cost its build time plus the whole door animation on top - on every tank, all game. The doors now start moving during the last seconds of the build and are open on the frame the unit is done. And they stay open while there is another vehicle behind him, however long that takes: a factory working through a queue keeps its doors up and shuts them when the queue runs out, instead of dropping and hauling them back for every unit. That was worth watching in its own right - there is no artwork for a door that changes its mind partway, so a door caught mid-close and pulled back open snapped wide in a single frame.

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
- Select a building and what it is making moves to the front of that row, ahead of everything else, still soonest first among itself. It used to get a whole second row of its own under the base's, which said the same thing twice and pushed the strip further up the screen; now the head of the strip is the building you are looking at and the tail is the rest of your base.
- Click a picture to jump the camera there; Ctrl-click cancels it.
- One picture is one order, cut to match the command bar's artwork.
- The strip is half the size it was: it says the same thing and takes back the screen it was eating.
- Whatever is actually being built counts down inside its own picture. The ones queued behind it stay blank, because their wait depends on everything in front of them.
- A unit finishes walking out of its factory before it takes an order.
- An upgrade stays available if any selected building can still buy it.
- One right click cancels one thing, counted the moment you press.
- Buying an upgrade for a group starts it in all of them.
- The progress clock over a picture is steady at any frame rate.
- A group's upgrade shows its progress, whichever building is paying for it.
- Click a unit with several factories selected and it takes the shortest queue.
- A plane stays buildable while any selected airfield still has a free spot. One full airfield used to grey the button out for the whole group, even with three empty runways in the same selection.
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
- The superweapon countdowns are pictures now, not a list of names: the same strip treatment, in the top right corner, under that readout. Each one wears its own seconds and a sweeping clock, the next to land sits at the right hand end, and if you are watching more than eighteen of them the rest become a count.
- Every button's corner markings - the hotkey, the price, the time, the number queued - sit flush in the corners instead of floating a few pixels inside them, and are set a size smaller than the button's own text: four labels at full size were eating the picture they were meant to annotate.
- The messages in the top left corner - out of money, building captured, a unit lost - are set a size smaller, the same notch the superweapon countdowns were taken down by. They are notes on the battlefield, not part of the panel, and at full size a run of them was climbing over the fight they were reporting.
- A superweapon's wait is written in plain seconds - 200, not 3:20 - so it compares with every other countdown on the screen at a glance. Once it is charged the number goes away entirely and the picture itself breathes in your colour, which is the one thing you want to catch out of the corner of your eye.
- The countdown inside a strip picture lost its black backing plate. On a picture that small the plate was most of the picture; the number now sits straight on the sweeping clock.
- Every picture in the row stands in a tray - the same tray your general's powers stand in down in the corner, your own side's copy of it, so the American, Chinese and GLA strips each carry their own metal. The tray is turned back to front, because that bar grows out of the right hand corner and the strip grows out of the left, so its heavy rail leads the row instead of standing between every pair of pictures. It is never stretched to fit, and the trays close up behind each other by exactly what that bar closes its own up by, so the row reads as one run of metal rather than a line of loose boxes. The strip is flush against the left edge, and a black rectangle laid over the battlefield is gone.
- The queue stands up out of the corner instead of lying across the bottom of the screen. It is a column now, five pictures high, growing upward from just above the command bar with the next thing to arrive at the bottom of it - so the one picture you actually read is always in the same place, a thumb's width above the buttons, and it does not move when six more tanks are ordered. What is left over closes the column as a sixth cell wearing a "+N". A busy base used to lay its whole queue across the battlefield; now it costs one picture's width of screen, whatever it has coming, and the buildings going up on the ground stand in a second column beside it. Watching a match, that is one column per player, side by side.
- A slot in it is a slot of that bar to the pixel, at every resolution - the size is measured off the bar itself rather than guessed - so the queue is read at a glance instead of squinted at: these were postage stamps a third of that size. Ten tanks ordered back to back are one picture with an x10 in its corner rather than ten copies of the same tank eating the row, so what is left of the row still says what else is coming.
- Above the queue is a row of what is actually going up on the ground: every building your workers are raising, each with its own sweep and the seconds it still needs, soonest first. Click one and the camera goes to the site - a half-finished building under fire on the far side of the map is one click away instead of a hunt across the minimap. Nothing in that row can be cancelled by mistake: a building already standing is sold or lost, never cancelled, so Ctrl over it does nothing.
- The superweapon strip in the top right stands in the same metal, the right way round this time: it is anchored to the right hand edge and grows leftwards, which is the way that bar in the corner grows, so its rail closes the row against the edge of the screen. The black rectangle behind those pictures is gone with it, and the countdowns are the size of the ones down in the corner rather than half of it.
- A game opens with nothing selected, so your first click is not a rally point.
- Your general's powers no longer climb the right hand edge of the screen. They sit three abreast, filling from the bottom corner where the first one has always been and wrapping upward, so a general holding eleven of them still reads as a block instead of a ladder from the radar to the sky. The keys follow the same shape: the first press picks a row, the second picks the power in it, so every power is two keys away - F1 F1 for the one in the corner, F2 F1 for the row above it. Only the keys you can press next are written on the pictures, so you are never reading eleven labels to find one, and Escape drops a row you thought better of - so does clicking a power with the mouse, and so does a couple of seconds' pause, so a key hit by mistake in a fight never turns your next one into a superweapon. Rows with nothing in them yet cannot be picked at all: early on, when you hold three powers, only the row you have answers a key.
- The tilde key opens the general's promotions, and Escape closes them - Escape used to bring up the pause menu on top of the open screen, leaving the small exit button in its corner as the only way out. The screen is five columns wide and 1 to 5 now name them: each number sits in the corner of the science that column would sell you next, and pressing it twice buys that one - once to mark, once to spend, so a point is never gone to a key hit in a hurry. Buy the top of a chain and the number drops onto the next rung down, so a whole column goes in with one finger while the fight carries on behind you.
- `S` stops your units again; the key was simply never bound.
- Every button carries its build, research or cooldown time in the corner.
- A single selected unit wears a gold bar filling toward its next rank.
- Timings everywhere: buildings, queues, superweapon charge, upgrades being researched.
- A charge bar now says how many seconds are left.
- Income per minute sits next to your money, averaged over half a minute.
- Aircraft always show how many attack runs they have left.
- The corner readout separates game time from real time, and sim rate from fps.
- Pausing stops both clocks.
- Watching a match, the strip becomes every player's queue at once: one row each, bordered in that player's colour, showing the three that land soonest and a count of the rest. Buildings going up on the ground are in those rows too, so a player answering an attack with three war factories is visible while the concrete is still wet, and eight players fit on one screen because a row is three pictures wide.
- Every row and every countdown stands in real metal, cut from the same tray the general's powers sit in down in the corner - the side your command bar is showing. Watching a match there is no bar of your own, so both strips used to fall back to a flat black box for the whole match; now they wear the metal of the player you are watching, and follow it when you switch seats.
- Watching, the bars over the buildings are everyone's too - what each factory is turning out, and how long the superweapons have left. An observer used to see none of it.
- Watching, you see the fog the player you are watching sees. The game only ever remembered one player's fog, so switching seats handed you the first player's idea of the map.
- An ally, and anyone watching, now sees a stealthed unit's muzzle flash, its promotion and the cash it earns - if they can see the unit at all. Those three asked 'is it mine' instead of 'can I see it', so allies and observers got nothing.

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
- Two buildings can no longer be put down on the same spot online. A build order travels to the other players before anything appears on the ground, and every click made while it was in flight was answered by a map that still showed the square as empty - so a shift-held row on a bad connection came out as buildings standing inside each other. The ghost now knows about the orders you have already spent, and turns red over them or slides to the square beside; and the order itself is checked again where it lands, on every machine, after the first building is standing. A second one aimed at the same ground is refused there and costs you nothing.
- A click clears a half-typed building shortcut.
- The two-key building shortcut survives the command bar's redraw now.
- Every structure is those two keys and nothing else. The letter written on a cell is the second half of its pair, and pressed on its own it used to build whatever sat in the slot that letter names - so the same barracks went up either by the shortcut painted on it or by one bare key nobody had written anywhere, and a key pressed after a shortcut you had thought better of put a building on your cursor. And the pairs are read off the key bindings now instead of a list written out beside them: the second letters had been left behind when the bottom row of the command bar moved to Z X C V B N M, so half the cells answered a letter that was not on them.
- The command bar is driven by its own grid of keys - the buttons in the shape they are in, under your left hand - and there is no longer a switch for it. It shipped off by default, which meant nobody ever saw it.
- Escape cancels what you were doing before it opens the menu.

## Where the money is, and where it is coming from

- Supply piles and docks say what is left in them, in cash, over the pile. No more guessing which expansion is worth taking from the art on the model.
- Every build button carries its price in the top right corner, opposite the build time already in the other one. It was only ever in the tooltip, which means hovering one button at a time to compare two of them.
- A worker fetching or handing over a box shows a bar while it works.
- That bar is now the work itself. A worker used to reset it to empty at the very moment it finished and then walk away, so every trip looked like it left mid-handover; and after taking its last box it stood at the pile for one more full loading cycle, taking nothing. It leaves the instant the load is done - which is a little more money per trip, on every worker you own.
- Hackers show how far off the next payout is. So do the black market and the oil derricks.
- Factories take a hundred units in the queue, not nine. The build queue only ever had nine buttons and that had become the limit.

## Health bars

- A health bar keeps its size against the unit at any resolution. It was a fixed number of pixels wide and three tall, so the bigger the screen the thinner the thread over a tank.
- Health bars are in the owner's colour instead of green to red. A building going up fills its bar in that same colour as it rises, so a glance across the map says whose expansion it is - the seconds written over it already say it is not finished. A disabled building keeps the blue.
- A building's bar sits above its roof rather than inside its art.
- Every garrisonable building shows how full it is, whoever holds it.
- A vehicle's load stays private.
- What you have selected wears a white frame just outside its bar, so a selection is still readable in a crowd where every unit carries a bar - and the bar itself keeps its owner colour all the way round.
- Lamps, barrels, rocks and bushes no longer wear health bars.
- Bridges no longer float a health bar over the middle of the river. There was never anything under it: the span is part of the terrain, and the bar belonged to an invisible marker standing in the water.
- The bar is part of the unit now: click it and you select whoever it belongs to. Zoomed out, an infantryman is a few pixels of helmet under a bar that is far easier to hit, and a unit half behind a building still has its bar in the clear. It only answers clicks that would have missed everything anyway, so it never takes a click away from the unit you were actually pointing at, and right-clicking bare ground under a bar still orders a move. And it stands down the moment you have something selected: with a force in hand your clicks are aimed at the ground, and a bar hanging over that ground used to cost you the whole selection and hand you back a single passer-by.
- The city itself stops wearing them too. A building nobody owns shows a bar only if there is something to do about it - troops can go inside, or it can be taken. A row of houses does not, and neither does the concrete apron each of them stands on, which is a separate two-thousand-point object that was drawing a second bar down at street level.

## Twenty-two-year-old bugs, found by testing

- A rifleman pulled off an oil derrick mid-capture left it flashing and chiming for the rest of the match, and the derrick changed hands anyway with nobody standing on it. Walking away now stops the capture, whether you or the computer gave the order.
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

- The writing on screen grows with your monitor now. Every panel is stretched to your resolution and always was, but the text inside it stayed the size it was drawn at in 2003, and what growth there was stopped dead at twice - so on a 2560 wide screen a command bar three times its original size still wore eight point lettering. The keys on the build buttons, the prices and the build times were the worst of it: at 2K they were drawn, and unreadable.
- The strip of everything your base is building follows the screen too, instead of staying a row of postage stamps under a command bar three times its size.
- Widescreen resolutions are back in the options menu.
- Borderless fullscreen: start with `-borderless` and the game fills the screen at your desktop resolution with no frame and no display mode change, so alt-tab is instant, nothing on the desktop gets shuffled around behind it, and a second monitor stays usable. Screen-edge scrolling works there, the way it does in fullscreen. The loading picture still opens as its own small window with your desktop around it, and the game only takes the screen once it has something to draw.
- Picking High detail gives you high resolution textures. The setting was ignored: the resolution came from a machine benchmark that answers 'low' on anything modern, so there was no way to ask for better.
- The main menu plays its battle again instead of showing one still picture. The game asks Windows how much memory the machine has and compares the answer against a 256MB minimum, and on a machine with more than 4GB installed that answer arrived as minus one - so the better the machine, the more certain the game was that it did not meet the 2003 minimum. It switched off the animated menu, forced textures down and, at the preset detail levels, took the trees out. It reads the real figure now.
- The Default button in the options menu no longer throws away your resolution. It reset the display along with everything else, dropping you to 800x600 with no undo.
- Text does not vanish at large sizes. Any font over 100 points simply failed to load, which on a 4K screen is a missing line of interface.
- Zoom further out, with the whole map drawn instead of black corners.
- The zoom-out ceiling is the same for everyone; no setting buys you a wider view.
- The wheel covers that whole range in about six notches instead of thirty-eight.
- Zoom toward the cursor works; the spot under your pointer stays there.
- The camera stays above the ground. Zoomed in on a slope it used to end up inside the hill it was looking over, and the world opened up along the near plane.
- The far edge of the view opens with the height, instead of stopping at a distance fixed for the stock zoom - which is what put black beyond the terrain when you zoom out past it.
- The box on the radar follows the camera when you pan. It only ever redrew itself when the zoom or the angle changed, so scrolling left it behind.
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
- Nor can a map handed to you over the network, however long a name the other machine gives it.
- An order too big for one packet arrives in pieces, and a piece that claims to belong outside the order is dropped instead of landing there.
- A chat line too long for a packet is dropped rather than arriving as a different, shorter line.
- The window no longer goes *Not Responding* during the menu's camera moves.
- Poison clouds, mine clearing, garrison kills and crew-killing weapons: all traced and fixed.
- Blowing up a full transport is survivable now, in every remaining variant.
- A unit can no longer load into a vehicle that no longer exists.
- A unit that kills itself survives its own turn now.
- Sniping an empty bike, formation moves and hacker evasion no longer crash.
- An order naming a player who is not there is dropped instead of crashing.
- An order that names one selected unit when nothing is selected is dropped too, instead of reading off the end of an empty list.
- Subdual weapons work on a unit that is being healed. Healing could drive the subdual meter below zero and it stayed there, so the next stun gun had to fill a hole before it did anything.
- A stream of projectiles remembers which ones it fired. The list was cleared into a local of the same name, so the real one started as whatever was in the recycled memory.
- A unit that has to deploy before firing does it standing still. It used to set up wherever it happened to be when the target came into range, then pack straight back up because it was still on its way somewhere.
- An Aurora that went supersonic to attack comes back down again. The timer ran out and nothing put the normal engine back.
- Calling in a gunship selects it for the player who called it, not for everyone watching the match.
- A transport told to load into something it cannot enter - or into itself, which happens when it is part of the group you gave the order to - ignores the order instead of flying over and hovering beside it.
- A pilot ejecting from a wreck no longer plays a promotion sound and animation somewhere out in the map.
- A sound finishing does not take the game with it. Every sound carries a description of itself, and that description can be gone by the time the sound ends - it is dropped when the sound is renamed, cleared by hand when what it points at is about to be deleted, and absent on a sound that was queued to repeat after a delay. The 2003 code checked for that in two places and then read straight through it in fifteen others, one of which is where every finished sound goes. It crashed mid-match with a stack that is all audio and names nothing that caused it. A sound with no description is now simply not music and not speech, which is what all fifteen questions were asking; the channel it was using is still handed back, because losing one leaks a voice for the rest of the match and enough of them go quiet.
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
- Units heading the same way take the lane beside the traffic instead of joining the back of it. Ground another unit is already driving over costs a little to plan through, so a column spreads into parallel lanes at the gate it forms at. Over twenty-four full matches the time units spend standing behind each other dropped by a third, and the searches to do it cost 6% more squares - the price is capped, or a crowded route would be worth walking around the map to avoid.
- A unit that has stood behind another one for a second and a half stops waiting and plans its own way round it. For that one route, ground somebody else is parked on costs real money, so the tank at the back pulls out of the line, drives past the ones standing in it and rejoins in front, instead of idling until the line decides to move. Two things had been holding it there. The engine only counted you as blocked while you were actually touching the unit ahead, and a tank in a queue does not touch every frame - it creeps up, nudges, backs off its speed, drifts apart, touches again - so the count kept resetting to zero and the two-second rule that asks for a new route never fired at all; it now decays instead of resetting, so touching more often than you clear still gets you there. And a unit approaching something to shoot refused to re-plan for as long as its target stood still, which is precisely the case where a dozen tanks arrive at the same building and stack up behind each other. Over forty-eight full matches the time units spend standing behind each other fell on both halves of the batch, and the worst jam in it was halved; the searches that do it never once came back with no route.
- A group you send across the map travels abreast instead of nose to tail. Up to forty units it is given one corridor wide enough for the whole group and split into columns, so it crosses a gap side by side and the outside of the column takes the wider way round. Where the ground is too tight for a full-width corridor - a bridge, a ramp, a gap between two cliffs - the group is given the widest corridor that fits instead of being dropped back to every unit for itself, which is what used to turn exactly those crossings into a single queue. The columns then close up to whatever width the ground allows and open out again on the far side, and a unit whose place in the column would put it in a cliff or a wall takes the middle of the road instead of driving into the rock and stopping. The gap between columns is measured off the units you actually selected rather than fixed at a guess that was narrower than a tank, so a group of tanks spreads wider than a group of scout cars and side by side means side by side instead of a trip spent shoving. Two units sharing a column drive one line, in file, which is what a column is for, and they arrive on that same spacing rather than piling onto one spot. The route itself is straightened as it is handed out: a corner is only driven if it is a real corner, so a group crossing open ground goes in long straight runs rather than nodding left and right the whole way, which is both faster and how a column of tanks is supposed to look. That corridor is only skipped for selections larger than any hand ever drags a box around.
- Routes now look ahead in time, not just in space. Every unit on the move leaves a note of *when* it expects to be on each square and which way it will be facing, and a route that would put you on a square somebody else wants at that same moment, coming the other way, costs extra to plan. Traffic going your way is free - the unit in front of you is about to not be there, and a queue along one road is what a road is for. So two groups sent through each other pick their gaps before they meet instead of grinding to a halt nose to nose in the middle. In the model of this movement code, sixteen units crossing each other on open ground cut the time spent standing still by a quarter, arrived sooner, and the planning itself cost under one percent more.
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
- Putting a building on uneven ground froze the picture for a sixteenth of a second. Levelling the dirt under a foundation asked the game to re-light every blade of grass on the map - sixty-one milliseconds, measured, for a patch a few paces wide. It now redoes the patch. Over a seven-minute four-way battle that took the worst frame of the match from 82 milliseconds down to 19, and left nothing at all above a thirtieth of a second.

## Sound, video, and getting it to start at all

- Audio is real, through the audio library the retail game ships with.
- The videos play: the intro, the sizzle reel, the mission briefings, the general portraits.
- The pointer is on screen over them. It used to appear only once the main menu did, so clicking through the logos was done blind.
- About 5,600 graphics calls are translated to a modern path, none of them touched.
- No disc, no registry keys, no retail installer â€” a normal install works.
- The startup screen is this build's own, so you can see which one you launched before the menu loads.
- The 1.04 patch content is reachable again.

---

## How this was done

- Ported leaf-first: every library compiled, tested and green before its dependents.
- 14 automated suites; most bugs above were found by tests, not by reading code.
- The game plays itself: eight computer opponents from one command line.
- An opponent can be set to Human in the skirmish screen: a base with no brain behind it.
- Its sight is yours from the first frame, and clicking any of it hands you the base itself.
- Shift-Ctrl-T does the same to any opponent, a computer one included, and walks around the table.
- Headless, a 23-minute skirmish plays out in 38 seconds, identically every run - and opens no window at all, so twenty of them in a row leave the desktop and the keyboard focus alone.
- It plays itself over a network too â€” two copies, one real connection.
- That found every multiplayer replay falsely accusing itself of desync since 2003.
- Every fix was proved by putting the bug back and watching the test fail.
- No debugger here: a crash symboliser, a sampling profiler, probes in live matches.
- Reverted and recorded: wide FOV, four pathfinding experiments, a three-panel command bar, tree shadows.
- The opponent's decisions are argued with a number: 20 headless matches per change, same seeds, win rate and match length before and after.
- That caught two changes that looked right and measured catastrophic - a wave that waited jammed the whole production line, and a retreat rule that counted buildings as gunfire sent every attack home. Both showed up as twenty matches with zero kills.
- Massing an army before attacking is written and measured but switched off: it needs somewhere to wait that is not the production queue.
- The self-play harness cannot see a traffic jam. Leaving a queue was measured over ninety-six matches and thirty-three of the forty-eight maps came out bit-identical, because a computer opponent moves five units at a time and never forms the queue a human makes by dragging a box round fifteen tanks. Where a jam did occur it got better, and nothing regressed, but the honest statement is that the number is thin: the rule fires about twice a match against the machine, and the case it was written for is one a person creates. Each unit that takes the way round says so in the log.
- Then it was asked to swing wider, and four ways of doing that were tried and thrown away. Charging more than double for driving over somebody gave the same routes to one square in fifty-six thousand, and so did looking half again as far ahead: past the point where going round is cheaper than going through, paying more does not move the line further out. Planning a lane's clearance around the traffic, and giving up on the queue half a second sooner, both did change the routes, and both made units wait longer than doing nothing at all - spread them harder and they park across each other, which is the same wall the lane-picking above ran into. A unit that leaves a queue has to leave it late and by the shortest way round, or it is not saving anyone time.
- Bugs deliberately left alone are pinned by a test documenting the behaviour.
- Infantry shadows were fixed in the wrong place first, and nobody has eyeballed them yet.
- The shade under smoke vanished between builds and was rewritten from these notes.
- The missing tree shadows were found by painting them red, not by reasoning.
- Zoom toward the cursor was fixed twice and is argued from code, not watched.
- The frame was finally measured rather than guessed at: 2003's own stopwatches were switched back on for a separate measurement build, and a seven-minute four-way match with two hundred and seventy units on the map was timed scope by scope.
- That killed a plan. Spreading the shadow work over sixteen cores would have bought two percent of a frame; particles, four hundredths of one percent. Nine tenths of the time is spent handing triangles to the graphics card, which no amount of threads makes faster. The plan is written down, with the numbers, so nobody spends a fortnight rediscovering it.
- One thing did come out of it: sixteen thousand five hundred lock operations per frame, all on the same lock, every time a scrap of memory is taken or given back. That is the next thing worth chasing.
- Then the question changed from "how fast" to "how steady", and the stopwatch had to change with it: an average is exactly the number that hides a stutter. The game now keeps the shape of every frame it draws and reports the worst ones, with the name of what took the time. The building-foundation freeze above was found that way, in one run, having been in the game since 2003.

---

## Not there yet

- Random maps generate and can be played â€” a number picks the map and the match starts on it â€”
  but there is still no menu entry and no reroll button.
- Online and LAN play are untested.
- You need to own the game; no game data ships here.
