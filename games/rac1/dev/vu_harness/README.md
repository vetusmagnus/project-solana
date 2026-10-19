# dev/vu_harness: native VU1 programs against the translated ones

Each native VU1 program (host/src/native) was checked here word for word against the program it
replaces: the translated microcode that build.py generates from your disc in
`build/linux/gen/vup_<image>_vu1.c`. These are Linux tools, built with zig (on PATH, or the
`ziglang` pip package; set `ZIG` to use another).

| Image | Program | Native code |
| --- | --- | --- |
| 13507 | ties | native_tie.c |
| 56467 | shrubs | native_shrub.c |
| 13859 | mobys | native_moby.c |
| 55907 | terrain (tfrag) | native_tfrag.c |
| 224979 | tie clip | native_tieclip.c |
| 912339 | shrub clip | native_shrubclip.c |
| 903379 | terrain clip | native_tfragclip.c |
| 221571 | effect: glints, sparkles | native_fx221571.c |
| 57843 | effect: strips with a clipper | native_fx57843.c |

## 1. Capture real input

Record every run of one program during one vsync (with the native paths off, so the translated
program runs):

    cd dev/tests && ./run.sh still cap 3 UYA_VUCAPSEQ=224979,1250 UYA_NATIVE=0
    # -> out/still_cap_3/vucapseq.bin: start pc, VU state and memory before, GIF packets, state after

Take captures from more than one level and from walking runs (`run.sh walk`) to reach more paths.

## 2. Replay a capture through both

    ./build.sh 224979 "native_tieclip_h.c ../../host/src/native/native_tieclip.c"
    ./h_224979 ../tests/out/still_cap_3/vucapseq.bin
    # native: 21 runs compared; 21 identical in every word; VU memory after the run identical in 21

The `native_*_h.c` files adapt each native program to the harness; their first lines give the
build line. `build_t.sh` is the same with a trace source of your choice (`TRACE_SRC`).
`tfrag_harness.c` (terrain) also checks VU memory and the carried registers, and replays each run
from starts the capture does not use.

## 3. Fuzz

The fuzzers mutate captured input and made-up input, run both, and compare. They also count
which program addresses were reached, so you can see what was never tested.

| Fuzzer | Build | Run |
| --- | --- | --- |
| moby | `./moby_fuzz.sh` | `./moby_fuzz CAPTURE [variants] [mode bits]` |
| tie clip | `./tieclip_fuzz.sh` | `./tieclip_fuzz CAPTURE [iterations] [seed]` |
| shrub clip | `./shrubclip_fuzz.sh` | `./shrubclip_fuzz CAPTURE [cases] [seed]` |
| terrain clip | build line at the top of tfragclip_fuzz.c | `./tfragclip_fuzz CAPTURE [variants]` |
| 221571 | `./fx221571_fuzz.sh` | `./fx221571_fuzz CAPTURE [iterations] [seed]` |
| 57843 | `./fx57843_fuzz.sh` | `./fx57843_fuzz CAPTURE [iterations] [seed]` |

Known: 57843 differs only on NaN-fed runs (which NaN an operation keeps is up to the compiler).

## Notes

- Captures and the generated program sources contain game data. Keep them out of the repository.
- The same approach (capture the state before and after, run both, compare everything) is what
  check mode does for native game routines such as TfragProc, inside the running game.
