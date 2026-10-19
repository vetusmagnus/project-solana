#!/bin/bash
# gl_replay_ab.sh "VARS_B" [LEVELS]: one GL run per level on the GPU (the Windows build, as gl_ab_win.sh), its recording
# replayed twice in WSL: with the defaults, and with VARS_B (e.g. "UYA_NTEX=0 UYA_GLMIPDEFER=0"), and the frames compared.
# For changes in the GS side (gs.c, gs_gl.c), which the replay runs: gl_ab_win.sh replays both recordings the same way.
cd ~/solana-dev/rac1/dev/tests; export RAC1_ISO=~/rac1.iso; unset DISPLAY WAYLAND_DISPLAY
./build_replay.sh > /dev/null 2>&1 || { echo "replay build failed"; exit 1; }
S=/mnt/c/Users/Nathan/Desktop/project-solana/games/rac1/dev/scripts
W=$(wslpath "$(cmd.exe /c 'echo %LOCALAPPDATA%' 2>/dev/null | tr -d '\r')")/solana-test
mkdir -p "$W"; cp ~/solana-dev/rac1/build/windows/rac1_wrapper.exe ~/solana-dev/rac1/build/windows/rac1_game.dll ~/solana-dev/rac1/build/windows/rac1_game.sym "$W/"
VB=$1; for lv in ${2:-3 13}; do
  rec="$W/p5r_$lv.bin"; rm -f "$rec"
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(wslpath -w $S/run_win.ps1)" -Dir "$(wslpath -w "$W")" -Level $lv -Rec "$(wslpath -w "$rec")" -Iso "$(wslpath -w ~/rac1.iso)" -Vars "UYA_FIXEDTIME=1" > /dev/null
  rm -rf out/p5a out/p5b; mkdir -p out/p5a out/p5b
  REPLAY_QUIET=1 out/gs_replay_gl "$rec" out/p5a > /dev/null 2>&1 &
  REPLAY_QUIET=1 env $VB out/gs_replay_gl "$rec" out/p5b > /dev/null 2>&1 &
  wait
  echo "level $lv, one recording, defaults vs $VB: $(python3 cmp_frames.py out/p5a out/p5b | tail -1)"
  rm -f "$rec"
done
