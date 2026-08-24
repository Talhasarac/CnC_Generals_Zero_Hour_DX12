#!/usr/bin/env python3
"""Apply the ground-collision / drift / flash pass to the loose game INIs.

The shipped ParticleSystem.ini and FXList.ini live inside INIZH.big; pull them out as loose
overrides first (loose files beat archives), then run this over them:

    cd GeneralsMD/Run
    python ../Code/Tools/bigfile.py extract INIZH.big "data/ini/particlesystem.ini" -o .
    python ../Code/Tools/bigfile.py extract INIZH.big "data/ini/fxlist.ini"        -o .
    python ../Code/Tools/vfx_tune.py --dry-run        # what it would touch
    python ../Code/Tools/vfx_tune.py

Re-running is safe: a block that already carries a key is left alone, so this is idempotent and
composes with hand edits.  To undo, extract the file again - that is the revert.

The numbers below are the whole tuning surface; they are meant to be edited.  Nothing here is
derived from anything, it is all taste.
"""

import argparse
import os
import re
import sys

# ---------------------------------------------------------------- the knobs

# Solids: chunks and sparks that should hit the ground and come off it again.
SOLID_TEXTURES = ("exdirtdebris", "exspark", "exshell", "exfireembr", "exstarburst")
SOLID = {"GroundCollision": "Yes", "GroundBounce": "0.35", "GroundFriction": "0.80"}
# Friction is charged on every frame a particle is being pressed into the ground, not once per
# landing, so it compounds: 0.80 skids for about a second before settling, 0.55 stops on the spot.

# Soft: smoke, dust, dirt and flame that should pile up against the terrain and spread along it
# instead of sinking into the hillside.  Bounce 0 = no hop, high friction = it keeps sliding.
SOFT_TEXTURES = ("exsmok", "excloud", "exdust", "exdrtexp", "expuddle", "exfire01", "exexplo")
SOFT = {"GroundCollision": "Yes", "GroundBounce": "0.00", "GroundFriction": "0.94"}

# Never collide these: the collision reads the terrain heightmap only, so anything living on the
# water surface would be clamped to the seabed under it.  Lens flares are screen-facing glows with
# no business touching the ground at all.
NO_COLLIDE_TEXTURES = ("exwater", "exwave", "exhydrant", "exsonic", "exlnzflar")

# Weapon trails: let the smoke wander instead of hanging in a rigid tube.  These angles are the
# ones the artists used on the eight systems that already ship with WindMotion set.
TRAIL_TEXTURES = ("exsmok", "excloud", "excontrail")
TRAIL_WIND = {
    "WindMotion": "PingPong",
    "WindAngleChangeMin": "0.069813",
    "WindAngleChangeMax": "0.139626",
    "WindPingPongStartAngleMin": "0.000000",
    "WindPingPongStartAngleMax": "0.261799",
    "WindPingPongEndAngleMin": "5.497787",
    "WindPingPongEndAngleMax": "6.283185",
}
# A trail whose puffs never grow reads as a solid tube; give the flat ones a little spread.
TRAIL_SIZE_RATE = "0.05 0.12"

# Every missile exhaust in the game is a STREAK - a ribbon drawn through its particles - so wind is
# the wrong tool there (it shoves each particle independently and kinks the ribbon).  DriftVelocity
# moves them all the same way, which shears the ribbon smoothly instead.  Nine STREAKs ship with it
# at zero, and those are the trails that hang in the air perfectly still; the rest are artist-set
# and left alone.  Straight up, so it does not depend on which way the missile was flying.
STREAK_DRIFT = "X:0.00 Y:0.00 Z:0.10"

# Muzzle flash: a short warm flash on the ground under the gun.  65 of the 431 shipped FXLists
# already carry a LightPulse and 141 a ViewShake, so both rules below only fill in the lists that
# have neither - the hand-authored ones are left exactly as they are.
MUZZLE_LIGHT = """  LightPulse
    Color = R:255 G:214 B:150
    Radius = 45
    IncreaseTime = 0
    DecreaseTime = 150
  End
"""

# Impact: bigger, oranger, slower to fade.
IMPACT_LIGHT = """  LightPulse
    Color = R:255 G:170 B:90
    Radius = 80
    IncreaseTime = 0
    DecreaseTime = 350
  End
"""

# Camera shake accumulates across simultaneous hits and is clamped, so it is only worth spending on
# rare, large events - a shell landing every second from thirty tanks would just pin it at the cap.
DEATH_SHAKE = """  ViewShake
    Type = SUBTLE
  End
"""

MUZZLE_RE = re.compile(r"(MuzzleFlash|GunFire|CannonFire)", re.I)
IMPACT_RE = re.compile(r"(Detonation|Explosion|Impact)", re.I)
DEATH_RE = re.compile(r"(Die|DeathExplosion)", re.I)


# ------------------------------------------------------------------ parsing

def split_blocks(text, keyword):
    """Split an INI into (prefix, [(header, body_lines)]) on top-level '<keyword> <name>' blocks.

    A block runs to the next line that is exactly 'End' at column 0; nugget 'End's inside an
    FXList are indented, which is what keeps them from closing the outer block early.
    """
    lines = text.split("\n")
    prefix, blocks = [], []
    i = 0
    while i < len(lines):
        if lines[i].startswith(keyword + " "):
            header = lines[i]
            body = []
            i += 1
            while i < len(lines) and lines[i].rstrip("\r") != "End":
                body.append(lines[i])
                i += 1
            end = lines[i] if i < len(lines) else "End"
            i += 1
            blocks.append([header, body, end])
        else:
            if blocks:
                blocks[-1].append(lines[i])   # trailing blank lines etc. ride with the last block
            else:
                prefix.append(lines[i])
            i += 1
    return prefix, blocks


def field(body, name):
    """Value of '<name> = ...' in a block body, or None."""
    for line in body:
        t = line.strip()
        if t.startswith(name) and t[len(name):].lstrip().startswith("="):
            return t.split("=", 1)[1].strip()
    return None


def has_field(body, name):
    return field(body, name) is not None


def is_zero_drift(value):
    """True for a missing DriftVelocity or one that is X:0 Y:0 Z:0 however it is spelled."""
    if value is None:
        return True
    nums = re.findall(r"[-+]?\d*\.?\d+", value)
    return len(nums) == 3 and all(float(n) == 0.0 for n in nums)


def texture_matches(tex, prefixes):
    tex = (tex or "").lower()
    return any(tex.startswith(p) for p in prefixes)


# ------------------------------------------------- ParticleSystem.ini rules

def tune_particles(text, report):
    prefix, blocks = split_blocks(text, "ParticleSystem")
    for block in blocks:
        header, body = block[0], block[1]
        name = header.split(None, 1)[1].strip() if " " in header else "?"
        tex = field(body, "ParticleName")
        priority = field(body, "Priority")
        ptype = field(body, "Type")

        add = {}

        # collision: PARTICLE sprites only.  STREAK segments and VOLUME_PARTICLE stacks are drawn
        # from the position rather than at it, so clamping them to the ground looks wrong.
        if (ptype == "PARTICLE"
                and not has_field(body, "GroundCollision")
                and not texture_matches(tex, NO_COLLIDE_TEXTURES)):
            if texture_matches(tex, SOLID_TEXTURES):
                add.update(SOLID)
                report("solid bounces", name)
            elif texture_matches(tex, SOFT_TEXTURES) or priority == "DUST_TRAIL":
                add.update(SOFT)
                report("soft settles", name)

        # drift: weapon trails only, and only the smoke ones - a flamethrower jet must stay a jet.
        # PARTICLE only again: a STREAK is a line drawn through its particles, and shoving those
        # sideways one at a time turns the trail into a zigzag rather than bending it.
        if (ptype == "PARTICLE"
                and priority == "WEAPON_TRAIL"
                and field(body, "WindMotion") in (None, "Unused")
                and texture_matches(tex, TRAIL_TEXTURES)):
            add.update(TRAIL_WIND)
            if field(body, "SizeRate") in ("0.00 0.00", "0.0 0.0", "0 0"):
                add["SizeRate"] = TRAIL_SIZE_RATE
                report("trail spreads", name)
            report("trail drifts", name)

        # the ribbon trails: shear the whole thing upward rather than kinking it
        if (ptype == "STREAK"
                and priority == "WEAPON_TRAIL"
                and is_zero_drift(field(body, "DriftVelocity"))):
            add["DriftVelocity"] = STREAK_DRIFT
            report("exhaust rises", name)

        if add:
            # replace in place where the key already exists (SizeRate), append the rest
            for i, line in enumerate(body):
                key = line.strip().split("=", 1)[0].strip()
                if key in add:
                    indent = line[:len(line) - len(line.lstrip())] or "  "
                    body[i] = "%s%s = %s" % (indent, key, add.pop(key))
            body.extend("  %s = %s" % (k, v) for k, v in add.items())

    out = list(prefix)
    for block in blocks:
        out.append(block[0])
        out.extend(block[1])
        out.append(block[2])
        out.extend(block[3:])
    return "\n".join(out)


# -------------------------------------------------------- FXList.ini rules

def tune_fxlists(text, report):
    prefix, blocks = split_blocks(text, "FXList")
    for block in blocks:
        header, body = block[0], block[1]
        name = header.split(None, 1)[1].strip() if " " in header else "?"
        joined = "\n".join(body)

        # only decorate lists that already do something visible at a place in the world
        if "ParticleSystem" not in joined:
            continue

        additions = []
        if "LightPulse" not in joined:
            if MUZZLE_RE.search(name):
                additions.append(MUZZLE_LIGHT)
                report("muzzle light", name)
            elif IMPACT_RE.search(name):
                additions.append(IMPACT_LIGHT)
                report("impact light", name)

        # shake: rare and large only - something that scars the terrain when it dies
        if ("ViewShake" not in joined and "TerrainScorch" in joined and DEATH_RE.search(name)):
            additions.append(DEATH_SHAKE)
            report("death shake", name)

        for chunk in additions:
            body.extend(chunk.rstrip("\n").split("\n"))

    out = list(prefix)
    for block in blocks:
        out.append(block[0])
        out.extend(block[1])
        out.append(block[2])
        out.extend(block[3:])
    return "\n".join(out)


# ------------------------------------------------------------------ driver

def run(indir, dry_run, verbose):
    counts = {}
    examples = {}

    def report(rule, name):
        counts[rule] = counts.get(rule, 0) + 1
        examples.setdefault(rule, []).append(name)

    jobs = [("ParticleSystem.ini", tune_particles), ("FXList.ini", tune_fxlists)]
    for filename, fn in jobs:
        path = os.path.join(indir, filename)
        if not os.path.exists(path):
            print("missing: %s  (extract it from INIZH.big first)" % path)
            return 1
        with open(path, "r", encoding="latin-1", newline=None) as f:
            text = f.read()
        tuned = fn(text, report)
        if dry_run:
            continue
        with open(path, "w", encoding="latin-1", newline="\r\n") as f:
            f.write(tuned)
        print("wrote %s" % path)

    if not counts:
        print("nothing to do - already tuned, or the INIs are not where I looked")
    for rule in sorted(counts):
        print("%-16s %4d" % (rule, counts[rule]))
        if verbose:
            for n in examples[rule]:
                print("                      %s" % n)
            continue
        for n in examples[rule][:3]:
            print("                 e.g. %s" % n)
    if dry_run:
        print("(dry run - nothing written)")
    return 0


SELFCHECK_PARTICLES = """ParticleSystem TestDebris
  Priority = DEATH_EXPLOSION
  Type = PARTICLE
  ParticleName = EXDirtDebris.tga
  Gravity = -0.10
End

ParticleSystem TestStreak
  Priority = WEAPON_TRAIL
  Type = STREAK
  ParticleName = EXSmokNew1.tga
  SizeRate = 0.00 0.00
End

ParticleSystem TestSpray
  Priority = WEAPON_EXPLOSION
  Type = PARTICLE
  ParticleName = EXWater04.tga
End

ParticleSystem TestTrail
  Priority = WEAPON_TRAIL
  Type = PARTICLE
  ParticleName = EXSmokNew1.tga
  SizeRate = 0.00 0.00
End
"""

SELFCHECK_FXLISTS = """FXList WeaponFX_TestShellDetonation
  Sound
    Name = Boom
  End
  ParticleSystem
    Name = Puff
  End
End

FXList WeaponFX_TestQuiet
  Sound
    Name = Click
  End
End

FXList FX_TestTankDieExplosion
  ParticleSystem
    Name = Fireball
  End
  TerrainScorch
    Type = RANDOM
  End
End
"""


def cmd_selfcheck():
    """Exercise the block parser and both rule sets on a synthetic INI - no game data needed."""
    seen = []

    def report(rule, name):
        seen.append((rule, name))

    # nugget "End"s are indented, the block's own "End" is not: the parser must not stop early
    _prefix, blocks = split_blocks(SELFCHECK_FXLISTS, "FXList")
    assert len(blocks) == 3, len(blocks)
    assert "Sound" in "\n".join(blocks[0][1]), blocks[0][1]
    assert "TerrainScorch" in "\n".join(blocks[2][1]), blocks[2][1]

    tuned = tune_particles(SELFCHECK_PARTICLES, report)
    rules = {r for r, _ in seen}
    names = {n for _, n in seen}

    assert ("solid bounces", "TestDebris") in seen, seen
    assert "TestSpray" not in names, "a water particle must not be clamped to the seabed"
    # and prove it block by block, not just by the report
    for block_name, forbidden in (("TestStreak", "GroundCollision"),
                                  ("TestSpray", "GroundCollision"),
                                  ("TestStreak", "WindMotion")):
        body = tuned.split("ParticleSystem " + block_name + "\n", 1)[1].split("\nEnd", 1)[0]
        assert forbidden not in body, (block_name, forbidden, body)
    assert ("trail drifts", "TestTrail") in seen, seen
    assert ("trail spreads", "TestTrail") in seen, seen
    assert ("exhaust rises", "TestStreak") in seen, seen
    assert is_zero_drift(None) and is_zero_drift("X:0.00 Y:0.0 Z:0")
    assert not is_zero_drift("X:0.00 Y:0.00 Z:-0.20")
    assert "GroundBounce = 0.35" in tuned, tuned
    assert "WindMotion = PingPong" in tuned, tuned
    # SizeRate is replaced in place, not appended twice
    assert tuned.count("SizeRate = ") == 2, tuned          # one per WEAPON_TRAIL block
    # every block still closes
    assert tuned.count("\nEnd") == SELFCHECK_PARTICLES.count("\nEnd"), tuned

    seen2 = []
    again = tune_particles(tuned, lambda r, n: seen2.append((r, n)))
    assert not seen2, "second pass should be a no-op, got %r" % (seen2,)
    assert again == tuned

    seen3 = []
    fx = tune_fxlists(SELFCHECK_FXLISTS, lambda r, n: seen3.append((r, n)))
    assert ("impact light", "WeaponFX_TestShellDetonation") in seen3, seen3
    assert ("death shake", "FX_TestTankDieExplosion") in seen3, seen3
    assert "WeaponFX_TestQuiet" not in {n for _, n in seen3}, "no ParticleSystem, nothing to light"
    # block-closing "End"s sit at column 0 and must be unchanged; the three nuggets added
    # (two lights and a shake) each bring an indented one
    assert fx.count("\nEnd") == SELFCHECK_FXLISTS.count("\nEnd"), fx
    assert fx.count("\n  End") == SELFCHECK_FXLISTS.count("\n  End") + 3, fx

    seen4 = []
    fx2 = tune_fxlists(fx, lambda r, n: seen4.append((r, n)))
    assert not seen4, seen4
    assert fx2 == fx

    print("vfx_tune selfcheck OK (%d rules fired)" % len(rules))


def main(argv):
    if len(argv) == 2 and argv[1] == "selfcheck":
        cmd_selfcheck()
        return 0

    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("--dir", default="Data/INI",
                   help="where the loose INIs are (default: Data/INI, i.e. run from GeneralsMD/Run)")
    p.add_argument("--dry-run", action="store_true", help="report what would change, write nothing")
    p.add_argument("-v", "--verbose", action="store_true", help="name every system, not just three")
    args = p.parse_args(argv[1:])
    return run(args.dir, args.dry_run, args.verbose)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
