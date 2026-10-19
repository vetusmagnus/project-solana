#!/bin/bash
# win_run.sh LEVEL "VARS" [NAME]: one walking GL run of the Windows build on the GPU (as gl_ab_win.sh: the
# level jump at vsync 300, the same keys, UYA_FIXEDTIME=1), with extra settings VARS; prints the log's path.
# Uses ~/solana-dev/rac1/build/windows (the Windows cross-build), copied to %LOCALAPPDATA%\solana-test.
lv=$1; vars=$2; name=${3:-winrun}
S=$(dirname "$(readlink -f "$0")")
W=$(wslpath "$(cmd.exe /c 'echo %LOCALAPPDATA%' 2>/dev/null | tr -d '\r')")/solana-test
mkdir -p "$W"
cp ~/solana-dev/rac1/build/windows/rac1_wrapper.exe ~/solana-dev/rac1/build/windows/rac1_game.dll ~/solana-dev/rac1/build/windows/rac1_game.sym "$W/" || exit 1
rec="$W/${name}_$lv.bin"; rm -f "$rec" "$rec.log"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(wslpath -w "$S/run_win.ps1")" -Dir "$(wslpath -w "$W")" -Level "$lv" -Rec "$(wslpath -w "$rec")" -Iso "$(wslpath -w "$(readlink -f ${RAC1_ISO:-$HOME/rac1.iso})")" -Vars "UYA_FIXEDTIME=1 $vars" > /dev/null
rm -f "$rec"
echo "$rec.log"
