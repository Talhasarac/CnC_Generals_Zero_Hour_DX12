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
	# a match that has not been decided by here is a result in itself: the AI cannot finish
	[int] $MaxFrames = 30000,
	# names this batch's logs, so two batches can be compared afterwards
	[string] $Tag = "ai",
	# map sizes to spread the batch over, in playable cells a side
	[int[]] $MapCells = @(96, 128, 160),
	# where the game is; the default is this repo's own Run directory
	[string] $RunDir = "$PSScriptRoot\GeneralsMD\Run",
	# minutes before a wedged run is killed rather than waited on forever
	[int] $TimeoutMinutes = 20
)

$ErrorActionPreference = "Stop"

$exe = Join-Path $RunDir "generals.exe"
if (-not (Test-Path $exe)) {
	throw "no generals.exe in $RunDir - build first (build.bat Release copies it there)"
}

# One row per match. Slot results are kept as a hashtable per row so the summary can pivot on them.
$rows = @()

for ($i = 0; $i -lt $Runs; $i++) {

	$cells = $MapCells[$i % $MapCells.Count]
	$prefix = "{0}_{1:d3}" -f $Tag, $i
	$log = Join-Path $RunDir "$($prefix)DebugLogFile.txt"

	# a stale log from an earlier batch would read as this run's result if the process died early
	Remove-Item $log -ErrorAction SilentlyContinue

	# -observer costs no slot and makes every slot an AI, so the batch measures AI against AI
	# rather than an AI against an idle human seat (see the observer note in setupAutoSkirmish).
	$args = @(
		"-headless", "-quickstart", "-noshellmap", "-observer",
		"-randommap", $i, $Players, $cells,
		"-autoskirmish", $Players,
		"-aidiff", $Difficulty,
		"-seed", $i,
		"-maxframes", $MaxFrames,
		"-logPrefix", $prefix
	)

	Write-Host ("[{0,3}/{1}] seed {2} cells {3} ... " -f ($i + 1), $Runs, $i, $cells) -NoNewline
	$sw = [Diagnostics.Stopwatch]::StartNew()
	$proc = Start-Process -FilePath $exe -ArgumentList $args -WorkingDirectory $RunDir -PassThru
	if (-not $proc.WaitForExit($TimeoutMinutes * 60 * 1000)) {
		$proc.Kill()
		Write-Host "KILLED (wedged past $TimeoutMinutes min)"
		$rows += [pscustomobject]@{ Seed = $i; Cells = $cells; Why = "wedged"; Frames = 0; Wall = $sw.Elapsed.TotalSeconds; Slots = @{} }
		continue
	}
	$sw.Stop()

	if (-not (Test-Path $log)) {
		Write-Host "NO LOG (exit $($proc.ExitCode))"
		$rows += [pscustomobject]@{ Seed = $i; Cells = $cells; Why = "no log"; Frames = 0; Wall = $sw.Elapsed.TotalSeconds; Slots = @{} }
		continue
	}

	$why = "no result"
	$frames = 0
	$slots = @{}

	foreach ($line in Get-Content $log) {
		if ($line -match "HEADLESS RESULT: (.+?) on frame (\d+)") {
			$why = $Matches[1]
			$frames = [int]$Matches[2]
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

	$rows += [pscustomobject]@{ Seed = $i; Cells = $cells; Why = $why; Frames = $frames; Wall = $sw.Elapsed.TotalSeconds; Slots = $slots }
}

# ---------------------------------------------------------------------------------------------
Write-Host ""
Write-Host "=== $Tag : $Runs matches, $Players players, $Difficulty, cap $MaxFrames frames ==="

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
$summary | Format-Table -AutoSize

# the per-match detail, for whoever wants to look at one game rather than the average
$csv = Join-Path $RunDir "$Tag-batch.csv"
$rows | Select-Object Seed, Cells, Why, Frames, Wall | Export-Csv -Path $csv -NoTypeInformation
Write-Host "per-match rows: $csv"
