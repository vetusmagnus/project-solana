# run_win.ps1 -Dir DIR -Level N -Rec FILE -Iso ISO [-Vars "K=V K=V"]: one GL run of the Windows build in DIR
# (the walking run of dev/tests/run.sh glrec: level jump at vsync 300, the same keys, GS input recorded for
# vsyncs 1560..1639 to FILE), on this machine's GPU, with UYA_FIXEDTIME=1. Called by gl_ab_win.sh.
param([string]$Dir, [int]$Level, [string]$Rec, [string]$Iso, [string]$Vars = "")
$env:RAC1_ISO = $Iso
$env:UYA_LEVEL = "$Level@300"; $env:UYA_SKIPMOVIES = "10"; $env:UYA_RENDERER = "gl"
$env:UYA_KEYS = "cross@1300+10,lup@1400+220,rright@1500+80,lleft@1620+100,rleft@1680+60"
$env:UYA_GSREC = "$Rec,1560,80"; $env:UYA_VSYNCS = "1642"; $env:UYA_FIXEDTIME = "1"   # (the game clock a field per vsync: no waiting, the same frames whatever the load)
foreach ($kv in ($Vars -split '\s+' | Where-Object { $_ -match '=' })) { $k, $v = $kv -split '=', 2; Set-Item -Path "env:$k" -Value $v }
$log = "$Rec.log"
# (-NoNewWindow: the game shares this console, so it does not wait for Enter at its end)
$p = Start-Process -FilePath "$Dir\rac1_wrapper.exe" -WorkingDirectory $Dir -NoNewWindow -PassThru -RedirectStandardError $log -RedirectStandardOutput "$log.out"
if (-not $p.WaitForExit(600000)) { $p.Kill(); Write-Output "timeout" ; exit 1 }
Write-Output "exit $($p.ExitCode)"
