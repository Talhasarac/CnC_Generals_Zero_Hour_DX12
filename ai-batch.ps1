<#
.SYNOPSIS
  Runs a batch of headless skirmishes and reports the win rate.

.DESCRIPTION
  Section 0 of AI-ROADMAP.md: no AI change can be claimed without a number behind it, and one
  match proves nothing - the same seed on the same map is repeatable, but a different seed is a
  different game. This runs the same configuration over a spread of seeds and maps and prints
  the win rate per slot, so a change can be reported as "40% -> 65%" instead of "it felt better".

  Each run is one process, headless (no picture, no sound) with the logic tick running flat out,
  bounded by -maxframes so a match the AI cannot close out still ends. The result comes back in
  that run's own log, which -logPrefix names, so runs never overwrite each other's answers.

  A run's map comes from -randommap, so nothing has to be installed to compare two builds - the
  seed is the map.

.EXAMPLE
  .\ai-batch.ps1 -Runs 20 -Tag base
  .\ai-batch.ps1 -Runs 20 -Tag counter-comp -Difficulty brutal
    ... then compare the two win rates.
#>
param(
	# how many matches to play
	[int] $Runs = 20,
	# players in each match; slot 0..N-1 all AI under -autoskirmish
	[int] $Players = 2,
	# easy | medium | brutal (anything else is read as brutal by the game)
	[string] $Difficulty = "brutal",
	# rung for the odd-numbered slots; empty means the same as -Difficulty, so a rung plays itself
	[string] $Difficulty2 = "",
	# a match that has not been decided by here is a result in itself: the AI cannot finish
	[int] $MaxFrames = 30000,
	# names this batch's logs, so two batches can be compared afterwards
	[string] $Tag = "ai",
	# map sizes to spread the batch over, in playable cells a side
	[int[]] $MapCells = @(96, 128, 160),
	# first seed of the batch. A change tuned against seeds 0..23 has to be confirmed on seeds it
	# was not tuned on, or the number is a fit to twenty-four maps rather than a result
	[int] $SeedStart = 0,
	# where the game is; the default is this repo's own Run directory
	[string] $RunDir = "$PSScriptRoot\GeneralsMD\Run",
	# which executable in that directory to play. Two builds can sit side by side under different
	# names, which is the only way to measure a change against its own baseline without rebuilding
	# between batches - and the only way to run a batch at all while a copy of the game is open.
	[string] $Exe = "generals.exe",
	# extra arguments handed to every run, for sweeping a knob the game reads from the command line
	# (e.g. -ExtraArgs "-pathcongestion",'10') without a rebuild between batches
	[string[]] $ExtraArgs = @(),
	# minutes before a wedged run is killed rather than waited on forever
	[int] $TimeoutMinutes = 20
)

$ErrorActionPreference = "Stop"

$exe = Join-Path $RunDir $Exe
if (-not (Test-Path $exe)) {
	throw "no $Exe in $RunDir - build first (build.bat Release copies it there)"
}

# One row per match. Slot results are kept as a hashtable per row so the summary can pivot on them.
$rows = @()

for ($i = 0; $i -lt $Runs; $i++) {

	$seed = $SeedStart + $i
	$cells = $MapCells[$i % $MapCells.Count]
	$prefix = "{0}_{1:d3}" -f $Tag, $seed
	$log = Join-Path $RunDir "$($prefix)DebugLogFile.txt"

	# a stale log from an earlier batch would read as this run's result if the process died early
	Remove-Item $log -ErrorAction SilentlyContinue

	# -observer costs no slot and makes every slot an AI, so the batch measures AI against AI
	# rather than an AI against an idle human seat (see the observer note in setupAutoSkirmish).
	# -multiInstance: the single-instance guard counts a headless run as "Generals is already
	# running" and bails at once, so a batch cannot be measured while a copy of the game is open
	$args = @(
		"-headless", "-quickstart", "-noshellmap", "-observer", "-multiInstance",
		"-randommap", $seed, $Players, $cells,
		"-autoskirmish", $Players,
		"-aidiff", $Difficulty,
		"-seed", $seed,
		"-maxframes", $MaxFrames,
		"-logPrefix", $prefix
	)
	# a second rung for the odd slots, so one rung can be played against another
	if ($Difficulty2) { $args += "-aidiff2"; $args += $Difficulty2 }
	if ($ExtraArgs.Count) { $args += $ExtraArgs }

	Write-Host ("[{0,3}/{1}] seed {2} cells {3} ... " -f ($i + 1), $Runs, $seed, $cells) -NoNewline
	$sw = [Diagnostics.Stopwatch]::StartNew()
	$proc = Start-Process -FilePath $exe -ArgumentList $args -WorkingDirectory $RunDir -PassThru
	if (-not $proc.WaitForExit($TimeoutMinutes * 60 * 1000)) {
		$proc.Kill()
		Write-Host "KILLED (wedged past $TimeoutMinutes min)"
		$rows += [pscustomobject]@{ Seed = $seed; Cells = $cells; Why = "wedged"; Frames = 0; Wall = $sw.Elapsed.TotalSeconds; Slots = @{}; Pf = $null }
		continue
	}
	$sw.Stop()

	if (-not (Test-Path $log)) {
		Write-Host "NO LOG (exit $($proc.ExitCode))"
		$rows += [pscustomobject]@{ Seed = $seed; Cells = $cells; Why = "no log"; Frames = 0; Wall = $sw.Elapsed.TotalSeconds; Slots = @{}; Pf = $null }
		continue
	}

	$why = "no result"
	$frames = 0
	$slots = @{}
	# what the pathfinder did over the whole match, for comparing a pathing change against its
	# baseline: search time and count say what it cost, nopath/outofcells say whether it broke,
	# blocked/stuck say whether the traffic jams it was aimed at actually got shorter
	$pf = [pscustomobject]@{ FindMs = 0.0; Finds = 0; Expands = 0; NoPath = 0; OutOfCells = 0; Blocked = 0; Stuck = 0 }

	foreach ($line in Get-Content $log) {
		if ($line -match "HEADLESS RESULT: (.+?) on frame (\d+)") {
			$why = $Matches[1]
			$frames = [int]$Matches[2]
		}
		elseif ($line -match "HEADLESS PATHFIND: (.*)") {
			$report = $Matches[1]
			# "queue 6000x/47 find 2313x/0 ... expand 11414x | nopath 0 outofcells 0 blocked 17 stuck 0"
			# Only the outermost scope is timed, so a findPath called from the queue is counted but
			# charged nothing - the match's pathfinder time is the sum over every timed slot, and
			# the search count comes from the entry points that actually run a search.
			foreach ($m in [regex]::Matches($report, "(\d+)x/([\d.]+)")) {
				$pf.FindMs += [double]$m.Groups[2].Value
			}
			foreach ($slot in @("find", "closest", "patch", "attack")) {
				if ($report -match "(?<![\w.])$slot (\d+)x") { $pf.Finds += [int]$Matches[1] }
			}
			if ($report -match "(?<![\w.])expand (\d+)x") { $pf.Expands = [int]$Matches[1] }
			if ($report -match "nopath (\d+)")            { $pf.NoPath = [int]$Matches[1] }
			if ($report -match "outofcells (\d+)")        { $pf.OutOfCells = [int]$Matches[1] }
			if ($report -match "blocked (\d+)")           { $pf.Blocked = [int]$Matches[1] }
			if ($report -match "stuck (\d+)")             { $pf.Stuck = [int]$Matches[1] }
		}
		elseif ($line -match "HEADLESS PLAYER (\d+) '(.*?)': (\w+) \| score (\d+) \| money (\d+) earned (\d+) spent \| units (\d+) built (\d+) lost (\d+) killed peak (\d+)") {
			$slots[[int]$Matches[1]] = [pscustomobject]@{
				Name = $Matches[2]; Status = $Matches[3]; Score = [int]$Matches[4]
				Earned = [int]$Matches[5]; Spent = [int]$Matches[6]
				Built = [int]$Matches[7]; Lost = [int]$Matches[8]; Killed = [int]$Matches[9]
				Peak = [int]$Matches[10]
			}
		}
	}

	$winner = ($slots.Keys | Where-Object { $slots[$_].Status -eq "WON" } | Select-Object -First 1)
	$winnerText = if ($null -ne $winner) { "player $winner" } else { "no winner" }
	Write-Host ("{0}, {1}, frame {2}, {3:n0}s" -f $why, $winnerText, $frames, $sw.Elapsed.TotalSeconds)

	$rows += [pscustomobject]@{ Seed = $seed; Cells = $cells; Why = $why; Frames = $frames; Wall = $sw.Elapsed.TotalSeconds; Slots = $slots; Pf = $pf }
}

# ---------------------------------------------------------------------------------------------
Write-Host ""
Write-Host "=== $Tag : $Runs matches, $Players players, $Difficulty$(if ($Difficulty2) { " vs " + $Difficulty2 }), cap $MaxFrames frames$(if ($ExtraArgs.Count) { ", " + ($ExtraArgs -join ' ') }) ==="

$decided = @($rows | Where-Object { $_.Why -eq "decided" })
Write-Host ("decided {0}/{1}   frame-limited {2}   failed {3}" -f
	$decided.Count, $rows.Count,
	@($rows | Where-Object { $_.Why -eq "frame limit reached" }).Count,
	@($rows | Where-Object { $_.Why -in @("wedged", "no log", "no result") }).Count)

if ($decided.Count -gt 0) {
	Write-Host ("average length {0:n0} frames ({1:n1} min of game time)" -f
		($decided | Measure-Object Frames -Average).Average,
		(($decided | Measure-Object Frames -Average).Average / 30 / 60))
}

# The player list carries the civilian and neutral sides before the playable ones, so the indices
# in the log are not 0..N-1. Pivot on whichever ones actually turned up.
$slotIds = $rows | ForEach-Object { $_.Slots.Keys } | Sort-Object -Unique

$summary = foreach ($p in $slotIds) {
	$mine = @($rows | Where-Object { $_.Slots.ContainsKey($p) } | ForEach-Object { $_.Slots[$p] })
	if ($mine.Count -eq 0) { continue }
	$wins = @($mine | Where-Object { $_.Status -eq "WON" }).Count
	[pscustomobject]@{
		Slot     = $p
		Wins     = $wins
		"Win%"   = [math]::Round(100.0 * $wins / $rows.Count, 1)
		AvgPeak  = [math]::Round(($mine | Measure-Object Peak -Average).Average, 1)
		AvgKills = [math]::Round(($mine | Measure-Object Killed -Average).Average, 1)
		AvgLost  = [math]::Round(($mine | Measure-Object Lost -Average).Average, 1)
		AvgSpent = [math]::Round(($mine | Measure-Object Spent -Average).Average, 0)
	}
}
if ($summary) { $summary | Format-Table -AutoSize | Out-String | Write-Host }

# Pathfinder cost and traffic, averaged per match. A match that produced no log has nothing to
# say here, so it is left out rather than counted as a zero.
$pfRows = @($rows | Where-Object { $null -ne $_.Pf -and $_.Frames -gt 0 })
if ($pfRows.Count -gt 0) {
	$avg = { param($f) [math]::Round((($pfRows | ForEach-Object { $f.Invoke($_.Pf) } | Measure-Object -Average).Average), 1) }
	Write-Host ("pathfind per match: {0:n0} searches / {1:n0}ms, {2:n0} cell expansions | nopath {3} outofcells {4} | blocked unit-frames {5} (stuck {6})" -f
		(& $avg { param($p) $p.Finds }),
		(& $avg { param($p) $p.FindMs }),
		(& $avg { param($p) $p.Expands }),
		(& $avg { param($p) $p.NoPath }),
		(& $avg { param($p) $p.OutOfCells }),
		(& $avg { param($p) $p.Blocked }),
		(& $avg { param($p) $p.Stuck }))
	# blocked unit-frames scale with match length and army size, so the rate is the comparable number
	$totFrames = ($pfRows | Measure-Object Frames -Sum).Sum
	$totBlocked = ($pfRows | ForEach-Object { $_.Pf.Blocked } | Measure-Object -Sum).Sum
	Write-Host ("blocked unit-frames per 1000 logic frames: {0:n1}" -f (1000.0 * $totBlocked / $totFrames))
}

# the per-match detail, for whoever wants to look at one game rather than the average
$csv = Join-Path $RunDir "$Tag-batch.csv"
$rows | Select-Object Seed, Cells, Why, Frames, Wall,
	@{n="Searches";e={ if ($_.Pf) { $_.Pf.Finds } }},
	@{n="SearchMs";e={ if ($_.Pf) { [math]::Round($_.Pf.FindMs, 1) } }},
	@{n="Expands";e={ if ($_.Pf) { $_.Pf.Expands } }},
	@{n="NoPath";e={ if ($_.Pf) { $_.Pf.NoPath } }},
	@{n="OutOfCells";e={ if ($_.Pf) { $_.Pf.OutOfCells } }},
	@{n="Blocked";e={ if ($_.Pf) { $_.Pf.Blocked } }},
	@{n="Stuck";e={ if ($_.Pf) { $_.Pf.Stuck } }} | Export-Csv -Path $csv -NoTypeInformation
Write-Host "per-match rows: $csv"
