# Zero Hour, rebuilt

EA released the 2003 source code for preservation. It does not compile, does not run, and nobody had
touched the bugs inside it in twenty-two years. This build compiles, runs, and plays — and along the
way a long list of things that were quietly wrong since 2003 got put right.

**119 changes. ~580 engine source files ported. 14 automated test suites. Around 60 original bugs
found and fixed — not port damage, EA's own.**

The engineering record is in `CHANGELOG-DEV.md`. This page is what it means when you play.

---

## The frame rate cap is gone — and the game did not get faster

The original game rendered at about 33 frames per second and, worse, **tied the speed of the game
itself to that number**. A faster machine did not give you a smoother picture; it gave you a game
running at the wrong speed.

Those two things are now separate. The picture runs as fast as your hardware allows. The rules run on
their own clock, at exactly the pace they were designed for.

That sounds simple and it was not. Every animation in the game counted frames instead of time —
explosion flashes, tree sway, water, menu transitions, camera drift, screen fades. Uncap the picture
and all of it starts racing. Each one now has a clock of its own, so **you get a smooth, modern
picture and the game still plays like Zero Hour**.

Two more that only show up once the picture is fast: the subtitles under the mission briefings and
cutscenes used to hold each line for a number of frames rather than for a length of time, so on a
modern machine they flicked past faster than anyone reads. And the red pulse the radar gives you when
your base is being attacked was counting frames too — it strobed instead of throbbing, and it timed
itself out early. Both run on the clock now.

The surf on the beach behind the main menu is the clearest example of how deep this went. The
original code measured how much real time had passed, and then ignored the answer and moved every
wave forward by a thirty-third of a second — correct, and only correct, while the picture was pinned
at thirty frames. On a modern machine that made the waves break about ten times too fast. They roll
in at their proper pace now, and the whole water surface is timed off a clock precise enough that it
does not lurch between frames.

## The computer opponent builds a base now

One wrong value in the original code meant the skirmish AI only ever built what its script had marked
as urgent, plus a single power plant. Everything on its normal build list — the vast majority of what
it should be doing — was skipped. Every skirmish you have ever played against this code was against
an opponent that could not build.

Fixed. And with it:

- Rotated AI bases were laid out wrong; depending on which building happened to be first in a list,
  either nothing moved or every building faced the wrong way.
- An AI player who lost all its buildings aimed everything — attacks, defence, supply hunting — at
  the corner of the map.
- The AI could not tell its supply lines were under attack: it only ever looked at the last third of
  a second of damage, in a check meant to cover ten seconds.
- One AI decision was made from leftover memory, so **two identical matches did not play out
  identically** — the same class of fault that desyncs a multiplayer game.
- **The difficulty settings for build speed did nothing.** Every build order the AI placed was
  pinned to the same three-second wait, whatever the map, the difficulty or the AI's own money.
  Those numbers are read again: a rich AI presses its advantage, a broke one slows down, and a
  mission script that asks for a thirty-second pause between buildings finally gets one. The
  average pace is unchanged, so a skirmish still feels like Zero Hour.

## Attack-move actually attacks

Attack-move was close to useless in the original. Units picked a target, drove straight past it
without firing, or locked on to something and walked off the map chasing it.

It took four separate fixes to sort out, and one of them was only found by putting probes into a live
match and reading what came back:

> engage: MIG → Scorpion tank at range 337
> disengage: after 12 frames, target alive, ammo 2 → 0

The plane *had* attacked. It emptied its rack. But a fight that short with a survivor was being
written off as a failure, and the aircraft was penalised before it could try again. Aircraft were
being punished for doing exactly what aircraft do.

What you get now:

- **Groups arrive as a group when you ask them to.** Hold Ctrl on the attack-move click and every
  ground unit in the selection goes at the pace of the slowest one, so they hit the line together.
  Click it plainly and each unit runs at its own speed — which is what you want when the group is a
  scout screen, or when one damaged truck would otherwise walk your tanks in at a crawl. It used to
  be one shared pace, always, with no way to say otherwise.
- **Artillery, rocket infantry and Scud launchers find targets again.** They used to search only as
  far as they could see, which is far shorter than they can shoot.
- **Attack-moving through a base shoots the base.** Buildings were being filtered out entirely.
- **Aircraft make their pass, empty their load, fly home, rearm, and resume the order you gave them.**
  Previously they circled, picked target after target, never fired, and never came back.
- **Nothing walks off the map any more** chasing a target that keeps retreating.
- **Whoever shoots you first gets shot back.** An attack-moving unit used to keep grinding away at
  whatever it happened to pick — usually the nearest thing — while something else emptied a magazine
  into its flank. It now turns onto whatever is actually hurting it, as long as it can reach it, and
  goes back to the advance once that is dealt with.
- **Loading a save does not scramble what your attack-moving units do next.** The count of rounds a
  unit had spent on its current fight — which is how the game tells a real firing pass from a failed
  approach — was not among the things written into a save file, and on load it came back as whatever
  happened to be sitting in memory. Units resuming an attack-move from a save could pause a second
  before finding their next target, or not, at random. It is saved now.

## Your units stop shooting corpses

Send ten tanks at one enemy and the original game had all ten fire, every one of them aiming at a
target the first two shells had already killed. The shells were in the air, the target still showed
full health to everyone else, and by the time it fell eight rounds had gone into a wreck. The same
thing happened with rockets, artillery and anything else whose shot takes time to arrive: the longer
the flight, the more of the volley was wasted.

Now a shot is counted the moment it leaves the barrel. Everything that picks its own targets — units
on guard, units on attack-move, anything defending itself — knows how much damage is already on its
way to each enemy, and how much that enemy has left. Once an enemy is accounted for, the rest of the
group looks past it and puts its fire on the next target instead. If there is nothing else in range,
they hold, rather than emptying a clip into something that is about to fall over anyway.

Two things this deliberately does *not* do. It never overrules you: if you right-click a target, your
units shoot that target, exactly as ordered, however much fire is already heading its way. And it
never leaves a unit standing idle on a promise — if the shot that reserved an enemy is intercepted or
decoyed, the reservation lapses within half a second and your units open up again.

The practical effect is that a group kills more things per volley. Same units, same weapons, fewer
rounds spent on the dead.

**Ctrl+Q takes your infantry.** The three keys under your left hand are one per arm — Q infantry,
W vehicles, E aircraft — but Q was grabbing every armed thing you own, tanks and jets included, so
there was no way to pull just the foot soldiers out of a mixed army. Press it twice for the whole
map. Ctrl+A is still everything at once.

## Aircraft, guards, and orders that used to be ignored

- **A pair of jets on a two-runway airfield could stop taking off.** Each one waited for the other
  to finish taxiing, and neither ever decided that the wait was over: whoever taxied next restarted
  the countdown, so two Raptors sat on a working airfield doing nothing while the battle went on
  without them. They now wait for exactly one aircraft, once, and go.
- **Losing the airfield used to leave its aircraft in limbo.** With the runway gone the game told
  the plane it had permission to use it, so it went on flying a landing pattern for a building that
  was rubble, and one destroyed mid-rearm left the aircraft parked on nothing. They now do what they
  should when their base dies.
- **Repaired aircraft come back to you.** A Comanche or a Chinook that finished patching itself up
  used to lift off and hover over the pad. If the airfield has a rally point, it flies there.
  Infantry walking out of a captured building follow the building's rally point too, instead of
  standing in the doorway.
- **A Chinook told to fly somewhere and unload no longer empties itself in a single frame.** Eight
  passengers used to appear on top of each other. They get out one at a time now, unless you
  actually ordered an evacuation.
- **A guarding unit under fire stopped fighting itself.** Guard mode had a unit break off its move
  to shoot back, then break off shooting to resume the move, over and over, so it did neither well.
  The order to return to its post and the order to defend itself no longer fight.
- **Clearing mines works on mines you cannot see.** Mines and booby traps are invisible by nature,
  and the game refuses to let a unit walk up to something invisible. So an engineer ordered to clear
  a minefield simply stood still. Ordering a disarm now overrides that rule — and only that.
- **A helicopter no longer boards a transport from the air.** The check for "am I close enough to
  get in" only asked whether the transport was off the ground, not whether the two were anywhere
  near each other.

## The same game on both screens, and the same game after loading

- **A unit with nowhere to stand no longer appears somewhere else entirely.** When the game spawns
  something — a crew bailing out, a garrison emptying, anything placed *near* a spot rather than on
  it — and every nearby space is taken, it used to drop the new unit at a leftover position from
  whatever ran before it: a random point on the map, or off it. It now appears where it was asked
  for, crowded or not.
- **Loading a save remembers what your guards were guarding.** The guard order was written to the
  file one slot too far and overwrote the position it was guarding, so units restored from a save
  could come back watching the wrong place.
- **Restarting a skirmish restarts the same skirmish.** Hitting restart threw away the number the
  match was built from and started the next attempt on a different one, so the replay it was writing
  no longer matched the game being played and would not play back. Restart now hands the game back
  the number it had.
- **And it restarts with the same colours, the same corners and the same armies.** "Random" colour,
  "random" starting position and "random" army are drawn from that same shared number the moment the
  match begins. On a restart they had already been drawn once — the slots now held real answers — so
  there was nothing left to draw and the second attempt started from the first attempt's results,
  with the shared sequence a few numbers further along than the replay expected. The lobby's choices
  are now put back before the restart, so the second attempt makes the same picks the first one did.
- **Somebody quitting no longer ends the game for everyone else.** The game checks that all the
  machines still agree by comparing a fingerprint of the world every so often, and if two of them
  differ the match stops. A player leaving sent one last fingerprint from a machine that was already
  shutting the game down — and that fingerprint was counted. The four people still playing were told
  they had desynced because a fifth had quit. Only players who are actually still in the game count
  now, both for the comparison and for deciding whether everyone has reported in yet.
- **A power sabotage does not follow you into the next game.** Shutting a base's power down set a
  timer, and nothing cleared that timer when the match ended. Start another game without quitting to
  the desktop and the player who had been sabotaged began the new map blacked out — no radar, no
  defences, nothing building — for the first sixteen minutes of it, and only on the machines that
  had watched the sabotage happen.
- **Turning your sound off no longer desyncs the game.** Missions and maps play scripted sounds, and
  the game picks which take of a sound to play from the same shared number sequence that decides
  everything else in the match. If you had sound effects muted, that sound never got as far as being
  picked — so your machine skipped a number every other machine used, and from there on the two were
  playing different games. Same for a player with speech turned off. The take is still identical on
  every screen; it just no longer costs the simulation anything to choose it.
- **Two machines playing the same match drift apart less.** Defeating a player, playing a second
  map without restarting, and the fingerprint the game uses to notice that two players have stopped
  agreeing — all three had bugs that made one side quietly diverge from the other, and the
  fingerprint itself was checking the same number 128 times instead of the wall it was supposed to
  be checking. Fixed. This is groundwork: network play is still not something this build claims.
- **The mismatch alarm can see all of a unit now.** Every so often each machine boils the whole
  world down to a single number and the machines compare notes; if two numbers differ the match
  stops and says so. That summary was being taken from the first thirty-two switches of each thing
  it looked at and no further. A unit that was immobilised, disguised, deployed or carrying a
  different rider on one screen and not on another added up to exactly the same number, and
  thirty-six of the game's sixty-eight special powers never counted at all. The summary covers the
  whole of it now. This does not stop two machines drifting apart; it stops them drifting apart
  quietly, and moves the report back to the frame it actually started on.
- **Your orders stop arriving in the wrong order a few minutes into a network game.** Every order
  you give is numbered so the other machines can put your orders back in the order you gave them.
  The counter was sixteen bits wide and started near the top of its range, so it rolled over to
  zero after about fifteen hundred orders - a few minutes of ordinary play - and from that moment
  the game read your newest orders as your oldest ones. Your own commands could execute back to
  front, and because sorting them depended on the order the packets happened to arrive in, two
  machines could end up executing the same commands in different orders and drift apart. The
  numbering still rolls over; the game now reads it correctly when it does.

- **A network game runs at the speed it says it does.** Playing against other people, the
  simulation was gated by two separate clocks at once — the network's and the game's own — and a
  tick only happened when both agreed. Two free-running clocks of the same speed agree less often
  than either one alone, so a multiplayer match quietly ran slower than the identical skirmish
  offline, and drifted further the longer it went. There is one clock now.
- **Your input delay is no longer set by your graphics card.** The game decides how far ahead of
  itself everyone has to play from the slowest machine in the room — but it was measuring "slowest"
  by how many frames each player *drew*, not how many the simulation actually stepped. Since this
  build lets the picture run free of the simulation, someone with a modest graphics card and a
  perfectly healthy processor reported a third of their real speed, got a window too small to
  deliver their orders in, and stalled on almost every frame — and the game's response to one
  player stalling is to slow everyone down. It measures the simulation now.

- **One lost packet no longer freezes the game for two seconds.** Nobody's next frame can happen
  until everyone's orders for it have arrived, so a single dropped packet stalls the whole match
  until it is sent again — and the game always waited a flat two seconds before trying. It now
  measures the round trip to each player and waits about that long instead, which on a normal
  connection is under a fifth of a second. It keeps a little extra room for connections that swing
  around, backs off when a link is genuinely down rather than hammering it, and never waits longer
  than the two seconds it used to, so nobody ends up worse off than before.

- **A desynced match stops, instead of playing on as two different games.** The game already
  noticed when two machines stopped agreeing about the world — and then kept going anyway, letting
  both sides play out a match that had already stopped being the same match, and telling you at the
  score screen. Every order given after that point lands somewhere different on each screen. It now
  stops the moment it notices, on every machine at once.
- **A player leaving is no longer reported as a desync.** Someone quitting stays listed as present
  for a few moments longer than their last heartbeat survives, and the game read that gap — a
  packet that is not here yet — as proof that the two sides disagreed. Losing a player is now
  losing a player.
- **When a match does desync, the game writes down what happened.** Until now the whole report was
  one number per player: they differ. The game now keeps a rolling record of every object in the
  world — where it was, how hurt it was, and its own fingerprint — for the last several checkpoints,
  and writes it out on both machines when the check fails. Lining up the two files points at the
  single unit, building or projectile that went its own way, which is the difference between fixing
  a desync and guessing at it.
- **A LAN game refuses to start between two machines with different game files.** Two people whose
  data files differ cannot play the same match, however well the connection behaves — one hand-edited
  file and the two simulations part company a few minutes in, with nothing on screen to say why. The
  check for this was written and then switched off before release; it is back on, so a mismatched
  join is turned away in the lobby with a reason, instead of becoming a ruined game.
- **The maths is nailed down to the same setting on every machine.** Two computers only agree on
  where a shell lands if their processors are rounding numbers the same way, and that setting is a
  shared one: a graphics or sound driver can change it out from under the game and never put it
  back. The game already reset it every frame, but the reset read the wrong register to decide what
  to write. It now reads the right one, and there are tests that break the machine's maths on
  purpose and prove the reset puts it back.
- **The disconnect screen stops interrupting games that are merely slow.** In a big eight-player
  match the game routinely waits a few seconds for everyone's orders, and five seconds of that used
  to be enough to throw the disconnect screen over the top of the battle — with a vote attached that
  could end with somebody kicked out of a game nobody had left. The game now checks whether anyone
  has actually stopped talking before it does that. If everybody is still sending, it waits and
  catches up. If a player really has gone quiet, the screen comes up as before, and there is a
  hard limit so a genuinely wedged game still gives you the screen rather than hanging forever.
- **The setting that keeps your connection alive through a router was never being read.** Every
  online game sends a small packet to each player now and then purely so the router in between does
  not quietly close the door it opened for them. How often that happens was a configurable number
  that the game printed at startup and then ignored. It is a real setting now, and it is kept inside
  the range that actually keeps the door open — a game can ask for these packets more often on a
  fussy connection, but nothing can set it so far apart that the connection lapses between them.
- **The brake that stops an online game freezing was being switched off by the very thing it
  watches for.** When your machine is running out of the margin it keeps between the network and
  the simulation, it eases off its own speed for a moment so everyone catches up smoothly instead
  of the game locking solid. That margin was measured with a subtraction that ran backwards
  whenever an order arrived for a moment that had already passed — and the answer it produced was
  read as the largest margin possible, so the brake stayed off at precisely the moment it was
  needed. It is measured properly now.
- **And that brake now comes on early enough to be worth having.** It was set to wait until the
  margin was down to a single frame — a thirtieth of a second, less time than it takes to tell the
  other machines anything at all. By then the freeze it exists to prevent has already happened.
  Working out why turned up something worth knowing: the amount the game runs ahead of what you see
  is supposed to scale with how bad your connection is, but the formula only ever exceeds its own
  minimum once the two worst connections in the room add up to well over half a second of lag — a
  game nobody finishes. So every match anyone actually plays runs at that minimum, and the safety
  margin it was given works out to that one frame. The brake starts twice as early now, which is
  the difference between easing off and lurching.
- **One player's two-second hiccup used to slow the match down for the rest of the game.** An online
  game runs everyone at the speed of the slowest machine in the room, which is fair enough while
  somebody is actually struggling. The problem was getting back out of it. Each machine reports how
  fast it is running, and a machine that is keeping up reports exactly the speed it was told to run
  at — so once the room had been dropped to twelve frames a second, twelve was what everyone
  reported, and twelve was the answer for the rest of the match. Nothing was wrong on anyone's
  machine any more; the game had simply agreed with itself to stay slow. The original code has a
  half-built escape hatch for this — the slowest player is allowed to run a fraction faster "just in
  case they are able to" — but it is handed to whichever player happens to sit in the lowest slot
  among everyone tied at the bottom, which is almost never the player who was slow, and everyone
  else stays pinned. Now every machine is given that fraction. Whoever genuinely cannot keep up
  still holds the room back, exactly as intended, and the moment they recover the speed climbs back
  on its own — from twelve frames a second to thirty in about six seconds. A room held down by a
  machine that really is that slow settles just above it and stays there instead of surging and
  falling back.
- **The repair for a corrupted order was turned off for the first two seconds of every game.** When
  a player's orders for a moment arrive damaged rather than late, the game throws them away and asks
  that player to send them again — and that request is answered only if the moment in question is
  still recent enough to be worth resending. The check for "recent enough" counted backwards from
  the start of the match, which for the first couple of seconds gave an answer of roughly four
  billion, so every request was turned down as ancient history. Those first seconds are precisely
  when it matters: connections are still settling and everybody is giving their opening orders at
  once. A stall there had to sit through the retry and then the disconnect screen instead of being
  repaired on the spot. It counts properly now.

- **When the player relaying everyone's orders left, the game could pick a relay that does not
  exist.** One player in every online game quietly carries the traffic for everybody, and there is a
  standing order of who takes over when they drop out. Reading the next name off that list could
  read past the end of it and come back with a number that was never a player at all, and the game
  then tried to talk to them twice a second. That is a crash rather than a stall, and it lands at the
  worst moment in a match — right after someone has already dropped. The list is now read properly,
  and when it genuinely runs out the game says so instead of inventing somebody.

- **A dropped order used to make the game wait longer each time it went missing — up to two full
  seconds.** When a packet goes astray it is sent again, and the wait before each attempt was
  doubling: the third and fourth time the same order went missing, everybody sat there for a second
  and then two. Doubling like that is what you do to a connection that has gone away, but the
  connection has not gone away — it dropped one packet, and the whole game is standing still waiting
  for that one packet, with nothing else to send. The wait now stops growing after two steps, so the
  longest anyone stands still is well under a second, and the game still backs off enough to be
  polite to a connection that really has died — which it has its own, separate way of noticing.

## Sharper textures, for free

Zero Hour shipped downscaled copies of a lot of the base game's textures, and because of the order
the game loads its archives in, those small copies were the ones you saw — the full-size originals
were sitting right there in the base game's files, unused. Where both exist, the bigger one wins now.
On a standard install that is 481 textures, each of them four times the resolution you were getting:
buildings, walls, vehicles.

## Every replay, not just the last one

The game overwrites the same replay file after every match, so the only way to keep a game was to go
and rename the file before starting the next one. Turn on replay archiving and each game is also
saved under its own date-and-time name, in its own folder, automatically.

## Placing a building, then changing your mind

Right-clicking to cancel a building you were about to place used to also deselect the builder, so
putting down something else meant finding and clicking the dozer again. The cancel now cancels the
placement and nothing else.

## Bonuses that come and go when they should

A squad of infantry standing over its own dead used to keep the horde bonus the dead were paying
for. Nationalism and fanaticism were worse: once a unit had been in a horde with those upgrades
bought, it kept the damage bonus for the rest of its life, alone in a corner of the map, and
fanaticism refused to work at all unless nationalism had been bought first. All three bonuses now
come and go with the horde. (If you make maps or mods and want the old rule, a horde module can ask
for it by name — nothing that ships with the game changes.)

The strategy center had the same disease from the other end. Running two battle plans told each of
your units it was under the newest one only, undoing a plan scrambled the center's own record of
what it had handed out, and capturing an enemy strategy center gave you nothing while its old owner
kept the bonus. Plans now move with the building.

## One crate, one collector

Walk a five-man squad over a salvage crate and you used to get paid five times, because the crate is
only cleared away at the end of the frame and everybody who touched it in that frame counted. Same
frame, same crate, one payout now.

A vehicle thrown onto a crowd had the mirror-image bug: the crash damage was an area weapon, fired
once for every unit in the pile, so a group of five each took five hits. It hits what it lands on,
once.

A unit already on its way down could still be promoted by its last kill — new upgrades, new weapon
bonuses, the promotion sound and the flash, on a corpse. And getting healed used to count as taking
damage, which meant a stealth unit was dragged into view by its own medic.

## Circles are round, and a tunnel is one tunnel

Range checks against a circular area were really checks against the square around it, so the corners
counted as inside. The bigger the radius, the more territory a unit was "in range" of without being
anywhere near it. Now measured properly.

Infantry hiding in a tunnel network healed once per tunnel per frame: five tunnels, five times the
healing rate the data asks for. The network heals at its own rate now, however many entrances it
has.

## The power bar tells the truth

A knocked-out power plant is supposed to count for nothing until it comes back. The original game
kept two separate sets of books on that, and they disagreed with each other in four different ways.

- **An EMP on a plant you had already upgraded took the upgrade off your grid twice** — once when
  the plant went down, again when the plant was sold, captured or lost its upgrade while still
  down. The bar showed a blackout your base was not actually in, and everything on it shut off.
- **An EMP on a half-built plant moved power that did not exist yet.** A building site has never
  produced anything; disabling and re-enabling one used to add and subtract its full output anyway.
- **Loading a saved game brought disabled plants back as working ones**, so the power you loaded
  into was not the power you saved.
- **A Control Rods upgrade that finished while the plant was down was credited immediately and then
  credited again when the plant came back up** — free power, permanently, for anyone whose plant got
  hit at the right moment.

## Your whole base, on one strip

The original showed you one factory at a time and made you click through the rest.

There is now a single row above the command bar carrying **everything your base is building
anywhere** — units and upgrades, oldest first, each one filling up as it progresses. Click a picture
to jump the camera to the building making it. Hold Ctrl and click to cancel it, wherever it is. One
picture is one order: a Chinese barracks hands you two Red Guards for a single click, and the strip
shows that as the one thing you paid for and the one thing a cancel takes back.

Around it, the whole production loop got tightened:

- **An upgrade you can buy is offered even if one of the selected buildings already has it.** Select
  every barracks you own and the upgrade button used to grey out the moment the first one in the
  group had bought it, leaving you to hunt the rest down one at a time. The button now lights if
  *any* of them can still buy it, and the click goes to one that can.
- **One right click cancels one thing.** Cancelling a queued unit or upgrade used to take three or
  four tries: the click was only counted if the mouse had not drifted a single pixel off the
  picture between pressing and releasing. It counts the moment you press now. And the upgrade being
  researched — the one thing you most want to take back — was the one button that refused
  clicks at all while it was running.
- **Buy an upgrade for a group and it spreads.** Select four barracks, click the upgrade, and all
  four start it, as far as your money goes. It used to buy exactly one and quietly throw the other
  three clicks away.
- **The progress clock stopped flickering.** The filling circle over a picture was drawn once per
  game tick but the picture is redrawn far more often than that, so most frames showed no progress
  at all and the whole thing strobed. It is steady at any frame rate now.
- **A group's upgrade shows its progress.** With several buildings selected, the one actually paying
  for the upgrade is rarely the one the command bar happens to be reading, so the button showed
  nothing while the research ran.
- **Select several factories, click a unit, and it goes to whichever has the shortest queue.**
  Shift-click queues five, spread the same way.
- **A general's upgrade goes to whichever selected building is free.** Some upgrades are bought once
  for your whole army rather than per building, and those always went to the same one of the selected
  buildings — click three of them with four barracks selected and one barracks did all three jobs in
  a row while the other three stood idle. Each click now goes to the shortest queue, and the bar
  counts the clicks you have already made this instant, so a fast run of three lands on three
  different buildings.
- **Clicking an upgrade shows immediately that you clicked it.** The picture is now covered when the
  work starts and uncovers as it progresses, instead of filling up with a shadow. The old way drew
  nothing at all until the first percent had gone by, so an upgrade you had just paid for looked
  exactly like one you had never touched — and with a long upgrade in a queue behind other work, it
  could look untouched for a while.
- **The strip above the command bar dims the same way.** A cameo that has just joined the queue is
  covered and uncovers as it is built, so a fresh order looks like a fresh order. Items waiting
  behind the one being worked on stay covered.
- **You can hand a half-built building to another worker.** Click a second worker onto a building
  already going up and it takes over; the one that was on it goes back to being idle. The game used
  to refuse the click outright — the cursor did not even offer it — on the grounds that two workers
  cannot build one thing faster. True, but that is not what you were asking for.
- **The nearest free worker takes a new job** — and a worker already halfway through a building does
  not get yanked off it, unless you say so. An idle worker will pick up any half-finished building nobody is working on,
  including one whose builder died.
- **A worker that is building something still offers you the whole build list.** Its buttons used to
  grey out until it finished, so lining up the next structure meant waiting or hunting down another
  worker by hand. Click anyway: the order goes to the nearest idle worker, and only falls back to
  interrupting a busy one when every worker you own is busy. Same for the build list you get with
  nothing selected — it stays usable when the whole crew is out working.
- **The GLA's fake-building list is not a one-way door.** The toggle that swaps a worker's build
  list between real structures and decoys — the ones that go up in smoke on the Demolition
  general — did nothing on the build list you get with nothing selected. So a worker left on the
  decoy page stayed on it, and the only way back was to find that worker on the map and select it.
  The button works from either list now.
- **Workers go back to collecting supplies** after they finish, instead of standing there.
- **The times on the buttons are real seconds, not "seconds at normal speed".** Speed the game up
  and a 20 second barracks reads 10, because that is how long you are going to be waiting. Every
  countdown the interface prints now answers the speed the same way: build and research times,
  general's power cooldowns, the supply drop timer, the superweapon clock and the build tooltip.
- **The clock and frame-rate readout is smaller, bolder, and drawn on top of everything.** It used
  to be painted before the interface, so any panel or dialog covered the one number you look at
  when something is going wrong.
- **A game opens with nothing selected.** It used to start with your command centre picked, so your
  first click on the ground moved its rally point and the command bar opened on a menu you were not
  looking at.
- **The tilde key opens the general's promotions.** The panel was a click away on the stars button
  and nowhere on the keyboard, which is the wrong way round for something you want open the second
  a promotion lands.
- **`S` stops your units again.** The key was simply not bound: the stop button on the command bar
  worked, so nobody noticed that the key every RTS player reaches for did nothing at all.
- **Every button says how long it takes.** Each picture in the command bar carries its time in the
  corner: six seconds for a worker, twenty for a barracks, a hundred and twenty for a Scud storm.
  It is the price in seconds, and it stays put while the thing is being built — what one costs is
  still the question you are asking when one is already on the way, and the progress is on the
  cameo anyway. Upgrades carry their research time the same way. A general's power or special
  ability carries its cooldown, and that one does count down: how long until you can fire it, and
  once it is ready, how long the next one will make you wait.
- **A single selected unit shows its experience.** The portrait wears a gold bar filling toward the
  next rank. The chevron on the cameo already said which rank a unit holds and nothing about how
  close the next one is, which is the half of it you actually decide with — one more kill, or half
  a career away.
- **Timings everywhere**: seconds left on a building going up, the current unit and the whole queue
  behind it, a charge bar on superweapons, a progress clock on upgrades being researched. A bar only
  appears while something is really happening: your command centre used to wear a yellow bar from
  the first second of the match, charging a general's power nobody had bought yet.
- **The charge bar over a superweapon or a supply drop zone now says how many seconds are left**,
  the same way the production bar beside it always has. A bar filling up tells you something is
  coming; the number tells you whether it is worth waiting for.
- **Income per minute next to your money**, averaged over the last half-minute of what you actually
  earned — so it does not swing wildly the moment you spend something.
- **Aircraft always show how many attack runs they have left**, selected or not. That is the number
  that decides whether you commit them or send them home.
- **The corner readout now tells you two different things instead of one confusing one.** It reads
  `00:12:30(00:13:04)   30hz(144fps)`: game time and, in brackets, the real time you have been
  sitting there; then the rate the battle is being simulated at and, in brackets, the rate your
  screen is being drawn at. They are not the same number, and the difference is the interesting
  part - the drawing rate can be in the hundreds while the simulation is struggling. When it
  struggles the first number drops below 30 and the two clocks drift apart by exactly the time it
  lost.
  The drawing rate really is the drawing rate now: both figures used to be counted off the same
  clock, so the number in brackets was a second copy of the simulation rate and moved with it.
  Pausing stops both clocks. The real-time one used to keep running while the game stood still, so
  five minutes in the menu read as five minutes the simulation had fallen behind.

**Placing buildings.** The grid you place on is drawn on the ground now instead of over the top of
everything — it follows the slope, your units and buildings stand on it rather than under it, and
ground you cannot build on is a soft red wash instead of a scribble. Three more:

- **Running out of money no longer strands the building you were placing.** Shift-hold a row of
  bunkers, run dry halfway, and the ghost used to freeze on the map: it could not be placed, could
  not be turned, and the click that failed went through to the ground as a move order for whatever
  you had selected. Now it simply keeps following the cursor until the money comes in.
- **The range ring appears for GLA defences too.** A stinger site has no gun of its own — it keeps
  three soldiers alive beside it and they carry the missiles — so the game called it unarmed and
  drew nothing while you sited it. The faction whose defences most need placing was the one faction
  that got no help placing them.
- **A click clears a half-typed building shortcut.** With the structure grid armed and waiting for
  its second key, a click could leave it armed, and the next A or S you pressed became a building to
  place instead of an attack-move or a stop — which cancelled your selection.
- **The two-key building shortcut works.** It was being thrown away the instant it was armed: the
  command bar redraws itself the moment you press Q, and the redraw wiped the half-typed shortcut,
  so the second key never had one to finish — Q-Q did nothing and Q-A was an attack-move that
  cancelled your selection. It now lives until you pick a cell, select something else, or two real
  seconds pass. (It used to count drawn frames rather than time for that wait, which on a fast
  machine was a third of a second.)
- **Escape takes back what you were doing before it opens the menu.** With a building on the cursor,
  a targeting order armed, or a half-typed shortcut waiting, Escape drops that and nothing else.
  Press it again with nothing pending and the pause menu comes up as always.

**Health bars are in the owner's colour.** They used to run green to red by how damaged something
was — which is what the length of the bar was already telling you — and told you nothing about whose
it was. In the middle of a fight that is the question you actually have. They now match the colour
the minimap and the selection rings use, so you can read a brawl without clicking anything.

**Health bars sit above the roof, not on it.** A building's hit box is its walls, so the bar was
placed at the top of the *walls* - which on a steep civilian roof is halfway up the picture, and from
the side is inside the building in front. Buildings now carry their bar clear of their own art, and
the garrison squares ride with it.

**A building you can garrison shows how full it is.** The little squares over a building — one per
room, filled for each soldier inside — used to appear only on buildings you already held, which is
exactly when you least need them. Every garrisonable building shows them now, whoever is in it: how
many rooms are left is the question you ask before sending a squad, and how many are occupied is the
question you ask before shooting at it. A vehicle's load stays private — what is inside an enemy
transport is real intelligence; what is inside a building is what you can see out of its windows.

**And the scenery does not wear one.** Street lamps, phone boxes, barrels, planters, fire hydrants,
rocks and bushes all have hit points in this engine, so with health bars always on, a town map came
up with hundreds of little lines over things nobody fights. Buildings keep theirs — a civilian
building is cover to garrison, a tech building is worth capturing, and both are worth shooting — and
so does anything that can move.

## Twenty-two-year-old bugs, found by testing

Nothing here was guessed at. Every fix in this section came out of a test written against the
original code, and each one was proved by putting the bug back and watching the test fail again.

- **Units moving diagonally ran up to 40% faster than their own stat sheet.** The game measured
  speed with the wrong formula — exact along the axes, wrong on every diagonal. Braking distances
  came from the same number, so units overshot where you told them to stop. Every unit in the game
  now moves at the speed it says it does, in every direction.
- **Garrisoned infantry and base defences refused targets they could hit.** Only one of their weapons
  was ever checked for range, no matter how many they had.
- **Killing something with poison or toxin credited nobody** — no experience, no score, and the
  victim never even registered who had attacked it.
- **Healing a unit counted as attacking it.** Guards chased the wrong target, tunnels reacted to
  nothing, units flinched from being repaired.
- **A supply centre could stop accepting trucks permanently** if the truck using it died at the wrong
  moment. This is the well-known "my supply centre is stuck" bug, and this is what caused it.
- **Selling a building mid-research gave no refund and left the upgrade in limbo.** EA's own comment
  in that code reads *"Empirically, in release the code can loop forever. So we limit to 100
  passes."* — they knew something was wrong there and capped the symptom.
- **On-screen messages disappeared the instant they appeared.** The designed reading time never
  worked, in the retail game either. Fixed — and then shortened: the corner used to hold each line
  for five seconds and take another three to fade it, so what you did eight seconds ago was still
  sitting there under everything you had done since. It reads for two and a half seconds and fades
  in about one.
- **Double-tapping 0 never jumped the camera to that group.** Nine of the ten control groups took
  you to their units; the 0 group answered the key that selects it and ignored the key that finds
  it.
- **The mouse wheel cancelled whatever camera move was in progress** — zoom while panning, the most
  common camera gesture there is, fought you every time.

## It fits your monitor

Widescreen resolutions were filtered out of the options menu entirely: 1920x1080 was not even on the
list. It is now.

The command bar is the one the game shipped with — the painted bar, in its own artwork, where it
has always been. It was rebuilt for a while into three separate floating panels so nothing would be
stretched on a widescreen monitor; that was tried in game and put back, because the original bar is
what the game looks like.

Alongside it: zoom further out (with the whole map drawn, so the corners are not black), zoom toward
the cursor, 45-degree snapping for camera rotation and building placement, edge scrolling in
windowed mode, and scrolling speed that no longer depends on your frame rate. The extra zoom-out was
pulled back in after playing with it — a quarter again on top of what the original allowed, rather
than half, which had the camera so high the units stopped reading as units.

**Zoom toward the cursor works now.** It was in the options and it did nothing at all. The camera
does not jump to a new zoom, it glides there over the next fraction of a second - and the game was
measuring how far the ground had slid under your pointer straight away, before the camera had moved
a millimetre, so it always measured nothing and moved nothing. Then it did measure the slide, and
handed it to the part of the camera that scrolls in screen inches rather than ground yards, so the
camera went somewhere else instead. The spot under your pointer is now
held there for the whole glide. Roll the wheel over the far corner of your base and you end up at
that corner, not in the middle of the screen.

**A skirmish opens zoomed all the way out.** The game used to drop you at the map author's own
camera height, which is tighter than the wheel will go, so the first thing anyone did on every
single map was roll the wheel back to see their base and the ground around it. It now opens at the
widest view the wheel can reach — the main menu behind the buttons is left alone, at the framing it
was built for. Middle-click still snaps back to the author's framing, and
`StartAtMaxZoom = No` in `GameData.ini` puts the old opening back.

**Hold Ctrl and roll the wheel to turn a building before you put it down** — a clean 45 degrees a
notch, and the next building you place keeps the same facing, so a wall or a line of bunkers goes
down straight without aiming each piece by hand.

**Buildings snap to the grid the game itself thinks in** (experimental, on by default). Put two
structures down next to each other by eye and they used to end up a few feet out of line, leaving a
stripe of ground between them too narrow to walk a soldier through but too wide to look deliberate.
Placement now lands on the same ten-foot squares the pathfinder reasons about, so neighbours share
an edge, a row of them comes out straight, and the gaps you leave are gaps you meant to leave. Put
`GridBuildPlacement = No` in `Options.ini` for the old to-the-pixel placement.

**And now you can see those squares.** Pick a structure and the ground under it comes up ruled into
the grid it is about to land on — a wide patch, enough to plan a whole row against, fading out
towards its edge rather than stopping on a hard line. Every square you cannot build on — water, a
cliff, rubble, something already standing there — is crossed out in red as you pass over it, so you
find the edge of the buildable ground by looking at it instead of by being told *no* after the click.

Two things were quietly out by a hair and are now exact. The squares the game reasons about are not
where the round numbers are — they sit half a foot off — so every building was going down half a
foot inside its neighbour's square, which is the sliver the snapping was meant to remove in the
first place. And a factory's concrete apron is wider than the building on top of it; that apron is
what you see, and what the game measures when it decides whether two buildings fit, so it is now
what gets lined up. Buildings sit *on* their squares now rather than near them.

**A building now faces the way you drag it.** Press, pull out in the direction you want it to look,
let go — that has always been how you aim a structure, but the game was reading your aim two frames
late, so anything faster than a slow deliberate drag went down pointing somewhere you never chose.
It uses the direction your hand was actually going when you released. If you have the 45 degree snap
turned on it now takes the nearest of the eight headings — half the circle used to round the wrong
way and shove the building a whole notch past where you were pointing, which is why aiming south
felt like a wrestling match. The heading you aimed carries over to the next building, same as the
wheel. And the mouse pointer no longer disappears the moment
a building lands on your cursor: it stays visible for the whole placement, and turns into a no-go
sign over ground you cannot build on.

## Soldiers cast real shadows

Every vehicle and building in the game throws a proper shadow that has its shape — a tank's turret,
a barrel, a gun. Infantry never did. They got a soft grey oval painted on the ground under them, the
same oval whether the soldier was standing, running, kneeling to fire or throwing himself flat, and
the same oval for a rocket trooper as for a bike rider. The engine's own comment gave the reason:
shadows were built once from a model's fixed vertices, and a soldier's vertices move every frame with
his skeleton, so soldiers were simply skipped.

They are not skipped any more. A soldier's shadow is now rebuilt from the pose he is actually in, so
it has arms, a head, a weapon, and it moves when he does — you can see a squad's shadows stretch
across the ground ahead of them in the low sun, and see a man drop prone by his shadow alone. It is
the same shadow system the tanks use, so it stretches with the time of day and falls across walls and
slopes like theirs do.

If you would rather have the old flat blobs back — they are cheaper to draw — put
`UseShadowVolumesForSkins = No` in the game's `GameData.ini`.

Two more things had no shadow at all. A Scud, a Tomahawk, a rocket, a bomb falling out of a
bomber — every one of them crossed the map with nothing underneath it, and there was no way to tell
from the screen whether a missile was about to clear a ridge or plough into it. They all have a
shadow now, sized from the missile itself, and it runs along the ground beneath the thing as it
flies: you can watch a Scud's shadow climb a hill ahead of the Scud.

And the smoke. A burning building, a dust plume, the black column off a wrecked tank — the smoke
hung over the terrain in full daylight with clean bright ground beneath it. Now a big soft cloud
darkens the ground under itself, and the patch drifts and fades exactly as the smoke does, so a fire
that dies out takes its shade with it. Only real clouds get one: bullet trails, muzzle flashes and
sparks are too small and too short-lived to earn it, and fire and glows are still light rather than
shade. It is one soft patch per plume rather than a shadow for every puff, so a battlefield full of
smoke costs no more than a battlefield with one fire.

Then the trees — and it turned out to be bigger than the trees. The game has two kinds of shadow:
the sculpted kind that has the shape of the thing casting it, and the soft dark pool that trees,
bushes, fences and scenery get. **The soft pool has never once been drawn in this build.** Not
faintly, not in the wrong place: the game was painting it onto the ground in a way that could not
darken a single pixel, so a forest sat on clean bright sand next to tanks with shadows underneath
them. The blob is now blended onto the ground the way its own artwork describes it — the same
picture, the same darkness EA chose, actually visible.

So every tree on the map has a shadow now — every one of them, not most: 27 of the game's 128 tree
types never said whether they should cast at all and silently got nothing. They cast now, and so do
the bushes and the palms.

It is the soft pool rather than the tree's own outline, and that is where it stays. Giving trees a
sculpted shadow like the tanks have was tried and put back: the models are flat sheets of leaves,
and the sculpted kind needs something solid to work from. A palm managed to throw its trunk and
nothing else; a leafy tree threw nothing; and forcing the leaves through laid the tree's shadow
across its own branches. The pool under the tree is the honest answer for foliage, and it is the
one EA drew the artwork for.

And the scenery nobody gave a shadow to at all: fences, walls of tyres, rubbish piles, low
shrubs, the props that dress a map. They stood on the terrain with nothing under them, which is
the one thing the eye picks out instantly. Anything that cannot move and had no shadow now casts
a real one with its own shape — a fence throws a line of posts across the road. It costs nothing
to keep: the thing never moves, so its shadow is worked out once.

All of it is switchable the same way: `ShadowsForProjectiles = No`, `ShadowsForParticles = No`
and `ShadowsForProps = No` in `GameData.ini`.

## Bright things can glow

Put `Bloom = 60` in your `Options.ini` and the sun off a windscreen, a muzzle flash, a burning
building and the glare on water bleed a little light into the air around them, the way a camera
does. Nothing else changes — the effect only touches what is already bright, and everything below
the line stays exactly as it was.

It is off until you put that line in, and you get the two dials that matter. `Bloom = 40` for a
hint of it, `Bloom = 100` for the full thing. `BloomThreshold = 80` raises the bar so only the
genuinely blinding things glow; lower it and more of the picture joins in. This game's artwork was
painted in 2003 for a screen with no glow at all, so there is no setting that is right for
everyone — 100 with a low threshold visibly washes out a beach. Start at 40 and walk it up until it
looks right to you.

## It does not crash

- **It used to crash at the main menu.** Two blocks of hand-written assembly, untouched since 2003,
  were handing back registers they had quietly destroyed. On a 2003 compiler it happened to be
  harmless. On a modern one it took the whole game down — and the crash pointed at a completely
  innocent part of the engine.
- **It crashed every single time you quit** — and it took two goes to kill. The first cause was
  fixed, the freeze stayed, and the reason turned out to be older and further down: on the way out
  the game throws away the list of players before it throws away their armies, so every unit still
  standing was handed back to a commander who no longer existed. It faulted twice on every exit,
  and each fault stopped to write itself up out of an eighty-megabyte symbol file. That is the
  several seconds you sat looking at a frozen window. Quitting takes about half a second now, from
  the main menu or out of a live match, and the log it leaves behind is clean.
- **A long chat message or an unusual map name could kill the process outright.**
- The window used to go grey and *Not Responding* during the scripted camera moves on the menu.
- Half a dozen rarer crashes in combat — poison clouds, mine clearing, killing a garrisoned building,
  crew-killing weapons — all traced and fixed.
- **Blowing up a full transport was a coin flip.** Kill a Technical, a Battle Bus or a tunnel with
  Terrorists inside and the passengers explode, and their explosion kills the vehicle that is
  already in the middle of killing them. The game was walking a list of passengers that the second
  death had just thrown away. EA patched two versions of this by hand in 2003, one per patch, as
  players kept finding new ways to trigger it; every remaining version is closed now. Neutron
  Shells on a loaded Technical, a bunker buster on a tunnel network, a half-damaged Battle Bus —
  all survivable.
- **A unit could be loaded into a vehicle that no longer exists**, or a vehicle destroyed on the
  same frame could still spawn its crew. Both left something in the game holding a pointer to a
  corpse.
- **A unit that killed itself took the game with it, sometimes.** Every unit runs on a little
  machine that decides what it is doing this instant — and that machine belongs to the unit. A
  Terrorist reaching a truck, a suicide bomber, anything that dies to its own attack, destroyed the
  machine in the middle of the machine's own turn, and the next few instructions were reading a unit
  that had already been cleared away. It survives its own death now and finishes the turn properly.
- **Ordering a Jarmen Kell to snipe an empty bike** — one whose rider you already shot — was a
  crash rather than a refusal. So was a formation move that included something which cannot drive
  itself, and telling a hacker to step out of the way at the exact moment it stood up.

And when something does go wrong, it now writes a readable crash report instead of the window
silently vanishing.

## Long orders stopped hitching

Zero Hour ships with a two-tier pathfinder: a coarse pass to sketch the route, then a detailed one to
walk it. **The coarse pass never worked.** A single early exit meant it gave up on almost every
search, so every long move fell back to searching the entire map, cell by cell.

Measured on a real map, a destination 23 cells away across a ridge:

| | Cells searched | Time |
|---|---|---|
| Original | 55,000 | 256 ms |
| Now | 10,000 | 25 ms |

**Ten times faster.** That is what removes the hitch when you send a large army somewhere far.

The pathfinder has a fixed number of scratch records to search with, and three separate faults were
handing them out and never taking them back:

- **Every wall, fence and building on the map permanently held one.** They were being used to
  remember which structure sits on a cell — something that has nothing to do with searching. Build
  up a base over half an hour and the search runs out of room to think in, and units simply stop
  taking orders. That state now lives on the map cell itself, where it belongs, and the search keeps
  everything it started with. The same fault had a second face: with the records gone, a finished
  building could end up standing on ground the game still believed was clear.
- **Every abandoned search leaked one**, and an abandoned search is the normal case — a blocked
  route, an unreachable target, a unit that gets a new order mid-move.
- **A recycled record could still point at the last unit that used it**, which is how a unit
  occasionally set off along someone else's route.

Two more long-standing ones from the same pass: a single unit told to move no longer gets nudged to
the middle of the nearest square — a lone Chinook lands where you clicked, while a group still gets
the tidy formation. And a footprint change around a structure could permanently mark the ground
beside it as unbuildable and unwalkable, for the rest of the match.

**A seven-opponent skirmish was still stuttering, though**, and we went looking with a stopwatch
built into the game. Over one match, 513 turns ran long, and on average **93 percent of that lost
time was one thing: the pathfinder**. The worst single turn took **three seconds** - the game
visibly stops while one unit works out where to walk.

The cause was a rescue we had added ourselves. When the coarse route came out too narrow for a big
vehicle, we let the search start over with the whole map open to it. Generous, and ruinous: the
pathfinder is allowed 5,000 squares of thinking per turn, and one of those restarted searches was
measured chewing through **140,513 squares in 285 milliseconds**. The rescue is gone. A route too
narrow for a tank is a route to widen, not an excuse to search the whole map over again.

So we widened it. The coarse route was being opened out by one square on each side around the
**starting point only** — the developers' own note says it is there so a unit can get around friendly
units standing next to it — and the rest of the route, all the way to the destination, stayed a
single ribbon ten squares wide. Now the whole length of it is opened out. That is the same rescue,
applied where it costs something proportional to how far you are walking, rather than to how big the
map is.

And the search itself now has a stopping point. Every other search in the pathfinder had one; the
main one had none, so a search for a destination it could never reach kept going until it had visited
every square it could get to. It now stops after 20,000 squares and hands back the best part of the
route it did work out, so the unit sets off towards where you clicked and works out the rest as it
goes — instead of the game stopping dead for three seconds and the unit then standing there anyway.
The limit is a fixed count of squares, not a stopwatch, so a slow machine and a fast one still play
the same match.

Measured again on the same seven-opponent match, those three together:

| | Long turns | Average | Worst turn | Turns over a second |
|---|---|---|---|---|
| Before | 513 | 144 ms | 2,976 ms | 8 |
| After | 201 | 61 ms | 243 ms | none |

**Then we found why it was still 61 milliseconds.** A pathfinder works by guessing how far it still
has to go and walking towards the best guess. This one's guess was far too low — it priced every step
as a straight step on open ground, while the route it actually charges for costs more for a diagonal,
more again for every turn, and more still for squeezing between buildings. A guess that low stops
steering: instead of heading for the destination, the search spreads out sideways and ends up looking
at nearly every square it is allowed to touch. The log caught it in the act — a route about 315
squares long visiting **17,578 squares**.

The guess now accounts for what a step really costs. The route it finds can be slightly longer than
the theoretical best, and in exchange the search visits a fraction of the squares. On a unit walking
across a map that is a trade worth making every time.

**And one more, the big one.** Whenever the pathfinder looked at a square, it also tried drawing a
straight line from that square all the way to the destination, checking every square on the line for
anything in the way — a good shortcut when the line is clear. It did that from *every* square it
looked at. On an open map with seven computer opponents on the move, that came to **130 million**
of those checks in ten seconds of stuttering, with a single turn hitting **1.4 million**. One
infantryman crossing open ground burned 130 milliseconds by himself. The shortcut is still there,
but it is now only drawn from a square that is genuinely closer to the destination than anything
tried so far — which is the only time it has a new chance of getting through.

That one change took the stuttering turns from 615 to 108 and the time the game spends finding
routes from 11.8 seconds to 1.6. The worst single route went from 130 milliseconds to 12.

**Then the queue itself.** A pathfinder keeps a list of squares still to look at, cheapest first.
Adding one meant reading down that list from the cheapest end until the right place turned up —
about four hundred steps every time, eight times for every square examined. The list runs both ways,
so it now comes in from whichever end is closer, and the two ordinary cases — cheaper than
everything, dearer than everything — take no steps at all. Same order out, same routes, less work.

**Then the queue itself, properly.** Coming in from the nearer end still cost about eighty steps
for every square the search looked at — four hundred and thirty thousand steps in a single turn on
a busy map, more work than the searching. So the list is not read at all any more. The game keeps a
direct pointer to where each price belongs, so adding a square to the queue is one step instead of
eighty, however long the queue has grown. What comes out is in exactly the same order, which is the
point: same queue, same routes, same game on every machine — without the reading.

**And the ten-second hiccup.** Every ten seconds or so the game redraws its map of which areas
connect to which — it has to, because you and seven opponents keep putting buildings in the way.
Joining two areas together meant rewriting the entire table, once per join, and on a big map that
table has thousands of entries. The whole redraw landed inside a single turn: thirty to forty
milliseconds where nothing else happened, no matter what was on screen. Twenty-five turns in one
match were nothing but this. Joining two areas is now a single operation instead of a full rewrite.
The map is still redrawn on exactly the same turns, and comes out exactly the same — it just stops
costing you a turn.

**And seven opponents all thinking at once.** Every computer player asks itself the same questions on
a fixed rhythm - what to build next, what squad to raise, which bridge to repair - and every one of
them was handed the same stopwatch, started on the same turn. Seven opponents therefore did all of
their thinking on the same turn, over and over, for the entire match, while the six turns in between
had nothing to do. They now take it in turns: each opponent gets its own slot in the cycle. Nobody
thinks any less often, and no opponent is any slower - the work is simply spread across the turns
instead of piled onto one of them.

**And their squads marching out at the same moment.** When a computer player finishes a squad and
sends it off toward you, the game works out the whole route across the map right then and there,
while everything else waits. One of those cost thirty milliseconds - a whole turn on its own - and in
a seven-opponent match six of them arrived on the same turn: eighty milliseconds where the game did
nothing but plan marches. Only one squad is sent on its way per turn now; the rest go on the next
turn, and the one after. A squad that used to leave on turn 4,120 leaves on turn 4,121 instead, which
is a thirtieth of a second - you cannot see it, and it was sitting still waiting for orders either
way.

**And the map's own scripts, looked up by name.** A map is a few hundred little scripts, and one
script calling another means finding it by name — which meant reading down every player's list of
scripts, and every folder inside those lists, until the name turned up. The computer opponents' own
behaviour is written in exactly those scripts and they call each other constantly. The names are
fixed the moment a map is loaded, so they are now looked up directly instead of hunted for.

**And the places on the map, looked up by name.** A map is dotted with named spots to walk to and
named regions to watch — "move here", "is anyone standing in this area yet". Every time a script
mentioned one, the game read down the entire list of them until the name turned up, and the area
question is asked by every script that watches a region, on every pass. Both lists are fixed the
moment a map loads, so both are now looked up directly.

**And every burning building asking where its smoke is.** A damaged structure, a laser beam, a
beacon, a shockwave — each of them holds on to an effect by number and asks the game to hand it back,
every frame, for as long as it lives. Asking meant reading down the list of every effect alive in the
match until the right one turned up, and a late-game fight has hundreds of them alive at once while a
burning base has dozens of things doing the asking. Every effect now knows where it is, so the
question is answered in one step instead of hundreds — and an effect that dies is no longer hunted
down that same list before it can be cleared away.

## Sound, video, and getting it to start at all

- **Audio is real**, through the game's own audio library. The sound SDK was stripped from the source
  release, so the game was silent; it now binds to the library the retail game ships with, without
  needing the SDK at all.
- **The picture goes through a modern graphics path.** The original talks to a graphics interface
  Windows barely supports; roughly 5,600 calls are translated to a current one, without touching a
  single one of them. There is an optional switch for an even newer path on top of that.
- **No disc, no registry keys, no retail installer.** The game demanded a CD before it would let you
  press Play, and looked for the base game through a registry entry only the 2003 installer wrote. It
  now accepts a normal modern install on the hard drive.
- **The 1.04 patch content works.** Patch files were being hidden by the older files shipped
  alongside them, so patched menus showed a missing-text error and some buttons went nowhere.

---

## How this was done

Worth knowing, because it is why the list above is trustworthy.

**Ported from the leaves inward.** The smallest libraries first, each one compiled, tested and green
before anything that depends on it was touched. No big-bang rewrite, no modernisation for its own
sake — the 2003 code is still the 2003 code.

**Every change that touches real behaviour leaves a test behind.** 14 automated suites now run on
every build. They are not decoration: **most of the bugs in this document were found by those tests,
not by reading the code.** Linking proves nothing; running proves something.

**Every fix was proved non-vacuous.** Put the bug back, watch the exact test fail, restore the fix.
A test that passes both ways is worth nothing, and there are none of those here.

**Some answers only exist in a running game.** There is no debugger on the machine this was built on.
So: a crash reporter that symbolises its own stack, a sampling profiler written for the job, and
temporary probes dropped into live matches and pulled back out again once they had answered the
question. The aircraft attack-move fix came from a log line, not an argument.

**Things that did not work were reverted, and written down.** A wider field of view for widescreen
looked past the edge of the maps and went back. Four separate pathfinding experiments were tried in
game, made things worse, and were reverted. A three-panel command bar that fixed the widescreen
stretch was tried and reverted too — the game's own bar is the one people want to see. Tree shadows
that leaned away from the sun went the same way, on the evidence of before-and-after screenshots,
and so did sculpted shadows for trees - the leaves ended up shadowing their own branches.
All of it is
recorded so nobody re-tries it in two years.

**Bugs deliberately left alone are pinned by a test** that documents the current behaviour, so
changing it later is a decision rather than an accident.

**Infantry shadows were switched on in the wrong place the first time.** The code that gives a unit
its shadow lives in two spots, and the first version only touched the one the game runs when you
flip shadows back on in the options screen — so in a real game nothing changed and the soldiers kept
their blobs. Both spots are covered now, and the game's own log confirms every soldier on screen is
having a shadow built from his pose, frame by frame. What is still missing is a pair of eyes: nobody
has stood in front of a skirmish full of soldiers to judge how they look, or measured what they cost
in frames. If they turn out to be ugly or expensive, `UseShadowVolumesForSkins = No` turns them off
and this entry gets rewritten.

**The shade under smoke went missing between builds and was written a second time.** The feature
was described here and had tests written for it, and then the code itself was simply not in the
project any more — so the tests could not build, and the test runner kept passing because it was
still running yesterday's copy of itself. The notes were detailed enough to rebuild it from, which
is the whole argument for keeping them: same rules for which clouds earn a patch, same softness,
same tests, and this time both the tests and a screenshot of a dust plume on the ground say so.

**The missing tree shadows were found with screenshots, not with reasoning.** Three separate
explanations for them looked convincing on paper and were all wrong. What settled it was painting
the shadows bright red for one build and looking: red under some trees and not others said the
drawing worked and the colour did not. Every state the graphics card was actually using was then
read back out of it mid-frame rather than assumed from the source.

**Zoom toward the cursor has been fixed twice and eyeballed once.** The first fix measured a camera
that had not moved; the second found the camera being pushed in the wrong units. The second one is
argued from the code rather than watched: the mouse wheel cannot be faked from outside the game —
injected wheel events never reach its input, which is how it was established that they never
arrived rather than that they arrived and did nothing. If it still misses the mark, say so and it
gets a third look.

---

## Not there yet

- **The intro and mission videos do not play** — the video decoder is a licensed component that was
  never part of the released source.
- **Online and LAN play are untested.**
- You need to own the game. No game data ships here.
