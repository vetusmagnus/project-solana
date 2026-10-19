#!/bin/bash
# build_replay.sh: builds gs_replay_gl (OUT/gs_replay_gl), which replays a UYA_GSREC recording
# through the GL renderer with an EGL context and no window (Mesa llvmpipe works) and writes
# each frame as r_NNNNN.ppm.
#
# Rebuild it whenever gs.c, gs_gl.c or the mesh limits (GSMESH_N / NMESH) change: an old build
# rejects mesh ids above its limit and silently leaves those draws out.
# Needs gcc and the EGL and GL development headers (Ubuntu: libegl-dev libgl-dev).
source "$(dirname "$0")/common.sh"
cd "$R"
gcc -O2 -DGS_GL -I host/include -I host/src/gs -DGS_C="\"$R/host/src/gs/gs.c\"" dev/gs_replay.c \
    host/src/gs/gs_gl.c dev/gs_gl_egl.c -o "$OUT/gs_replay_gl" -ldl -lpthread -lm && echo "built $OUT/gs_replay_gl"
