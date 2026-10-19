#!/bin/bash
# gl_ab_win.sh A B "LEVELS" "VARS_A" "VARS_B": gl_ab.sh, but the game runs as the Windows build on this
# machine's GPU (about a minute a run, instead of 10-15 under Xvfb's llvmpipe); the two recordings are
# replayed and compared in WSL as before. Both runs use UYA_FIXEDTIME=1 (run_win.ps1). Uses the Windows cross-build of the WSL tree
# (build/windows: python3 build.py --platform windows ...), copied to %LOCALAPPDATA%\solana-test.
cd ~/solana-dev/rac1/dev/tests
export RAC1_ISO=${RAC1_ISO:-$HOME/rac1.iso}
unset DISPLAY WAYLAND_DISPLAY
[ -x out/gs_replay_gl ] || ./build_replay.sh || exit 1
a=$1; b=$2; levels=$3; va=$4; vb=$5
S=$(dirname "$(readlink -f "$0")")
W=$(wslpath "$(cmd.exe /c 'echo %LOCALAPPDATA%' 2>/dev/null | tr -d '\r')")/solana-test
mkdir -p "$W"
cp ~/solana-dev/rac1/build/windows/rac1_wrapper.exe ~/solana-dev/rac1/build/windows/rac1_game.dll ~/solana-dev/rac1/build/windows/rac1_game.sym "$W/" || exit 1
iso=$(wslpath -w "$(readlink -f "$RAC1_ISO")")
ps1=$(wslpath -w "$S/run_win.ps1")
for lv in $levels; do
    for n in "$a" "$b"; do
        v=$va; [ "$n" = "$b" ] && v=$vb
        rec="$W/glrec_${n}_$lv.bin"; rm -f "$rec"
        powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$ps1" -Dir "$(wslpath -w "$W")" -Level "$lv" -Rec "$(wslpath -w "$rec")" -Iso "$iso" -Vars "$v" > /dev/null
    done
    echo "level $lv (Windows GPU): $(./gl_compare.sh "$W/glrec_${a}_$lv.bin" "$W/glrec_${b}_$lv.bin" | tail -1)"
    rm -f "$W/glrec_${a}_$lv.bin" "$W/glrec_${b}_$lv.bin"
done
