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

- **Groups arrive as a group**, at the pace of the slowest unit, instead of trickling in one by one.
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

## Your whole base, on one strip

The original showed you one factory at a time and made you click through the rest.

There is now a single row above the command bar carrying **everything your base is building
anywhere** — units and upgrades, oldest first, each one filling up as it progresses. Click a picture
to jump the camera to the building making it. Hold Ctrl and click to cancel it, wherever it is. One
picture is one order: a Chinese barracks hands you two Red Guards for a single click, and the strip
shows that as the one thing you paid for and the one thing a cancel takes back.

Around it, the whole production loop got tightened:

- **Select several factories, click a unit, and it goes to whichever has the shortest queue.**
  Shift-click queues five, spread the same way.
- **The nearest free worker takes a new job** — and a worker already halfway through a building does
  not get yanked off it. An idle worker will pick up any half-finished building nobody is working on,
  including one whose builder died.
- **Workers go back to collecting supplies** after they finish, instead of standing there.
- **Timings everywhere**: seconds left on a building going up, the current unit and the whole queue
  behind it, a charge bar on superweapons, a progress clock on upgrades being researched.
- **Income per minute next to your money**, averaged over the last half-minute of what you actually
  earned — so it does not swing wildly the moment you spend something.
- **Aircraft always show how many attack runs they have left**, selected or not. That is the number
  that decides whether you commit them or send them home.

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
  worked, in the retail game either.
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
windowed mode, and scrolling speed that no longer depends on your frame rate.

**Hold Ctrl and roll the wheel to turn a building before you put it down** — a clean 45 degrees a
notch, and the next building you place keeps the same facing, so a wall or a line of bunkers goes
down straight without aiming each piece by hand.

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

## It does not crash

- **It used to crash at the main menu.** Two blocks of hand-written assembly, untouched since 2003,
  were handing back registers they had quietly destroyed. On a 2003 compiler it happened to be
  harmless. On a modern one it took the whole game down — and the crash pointed at a completely
  innocent part of the engine.
- **It crashed every single time you quit.** Quitting is now instant, instead of hanging for three
  seconds and then faulting twice on the way out.
- **A long chat message or an unusual map name could kill the process outright.**
- The window used to go grey and *Not Responding* during the scripted camera moves on the menu.
- Half a dozen rarer crashes in combat — poison clouds, mine clearing, killing a garrisoned building,
  crew-killing weapons — all traced and fixed.

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
stretch was tried and reverted too — the game's own bar is the one people want to see. All of it is
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

---

## Not there yet

- **The intro and mission videos do not play** — the video decoder is a licensed component that was
  never part of the released source.
- **Online and LAN play are untested.**
- You need to own the game. No game data ships here.
