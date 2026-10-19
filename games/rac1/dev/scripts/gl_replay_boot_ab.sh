#!/bin/bash
# gl_replay_boot_ab.sh "VARS_B" [LEVELS] [FROM,N]: as gl_replay_ab.sh, but the recording covers the boot screens,
# the level load and the start of gameplay (default vsyncs 0..1000: where the GL targets meet GS memory, the
# boot's and the level load's uploads into the Z buffer's memory): one GL run on the GPU, its recording replayed
# twice in WSL, with the defaults and with VARS_B (e.g. "UYA_GLTGT=0"), the frames compared.
cd ~/solana-dev/rac1/dev/tests; export RAC1_ISO=~/rac1.iso; unset DISPLAY WAYLAND_DISPLAY
./build_replay.sh > /dev/null 2>&1 || { echo "replay build failed"; exit 1; }
S=/mnt/c/Users/Nathan/Desktop/project-solana/games/rac1/dev/scripts
W=$(wslpath "$(cmd.exe /c 'echo %LOCALAPPDATA%' 2>/dev/null | tr -d '\r')")/solana-test
mkdir -p "$W"; cp ~/solana-dev/rac1/build/windows/rac1_wrapper.exe ~/solana-dev/rac1/build/windows/rac1_game.dll ~/solana-dev/rac1/build/windows/rac1_game.sym "$W/"
VB=$1; R=${3:-0,1000}; N=${R#*,}
for lv in ${2:-3}; do
  rec="$W/p6r_$lv.bin"; rm -f "$rec"
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(wslpath -w $S/run_win.ps1)" -Dir "$(wslpath -w "$W")" -Level $lv -Rec "$(wslpath -w "$rec")" -Iso "$(wslpath -w ~/rac1.iso)" \
    -Vars "UYA_FIXEDTIME=1 UYA_GSREC=$(wslpath -w "$rec"),$R UYA_VSYNCS=$((${R%,*} + N + 2))" > /dev/null
  rm -rf out/p6a out/p6b; mkdir -p out/p6a out/p6b
  REPLAY_QUIET=1 out/gs_replay_gl "$rec" out/p6a > /dev/null 2>&1 &
  REPLAY_QUIET=1 env $VB out/gs_replay_gl "$rec" out/p6b > /dev/null 2>&1 &
  wait
  echo "level $lv, vsyncs $R, one recording, defaults vs $VB: $(python3 cmp_frames.py out/p6a out/p6b | tail -1)"
  rm -f "$rec"
done
