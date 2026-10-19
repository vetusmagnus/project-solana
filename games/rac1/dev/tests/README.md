# dev/tests: checking a change against the game

Every native replacement in Project Solana ships only after it gives the same picture as the
code it replaces. These scripts run the game headless from a level jump with fixed inputs, dump
frames or record GS input, and compare two runs.

They are bash and Python, for Linux or WSL2 (Ubuntu). They are not part of the release kit.

## Setup

1. Build the game in this tree with every level:
   `python3 build.py --iso /path/to/rac1.iso --levels 00,01,02,03,04,05,06,07,08,09,10,11,12,13,14,15,16,17,18 -j 8`
   (build.py builds only level 00 by default; a level jump then finds no level program and
   the native paths never run).
2. `export RAC1_ISO=/path/to/rac1.iso`
3. For the GL checks: `sudo apt install xvfb libegl-dev libgl-dev`, then `./build_replay.sh`.
4. For the disassembly helpers: `pip install rabbitizer`.

Output goes to `dev/tests/out` (set `OUT` to change it; a recording is about 180 MB).

## The checks

| What | Commands | Pass |
| --- | --- | --- |
| One level, software renderer | `./run.sh still on 3` then `./run.sh still off 3 UYA_NTFPROC=0`, then `python3 cmp_frames.py out/still_on_3/d out/still_off_3/d` | 0 differing pixels |
| All 19 levels | `./all_levels.sh on` then `./all_levels.sh off UYA_NTFPROC=0`, then `./cmp_levels.sh on off` | 0 on every level |
| Walking frames | `./run.sh walk on 13` and `./run.sh walk off 13 ...`, then `cmp_frames.py` | 0 differing pixels |
| GL renderer | `./run.sh glrec on 3` and `./run.sh glrec off 3 ...`, then `./gl_compare.sh out/glrec_on_3.bin out/glrec_off_3.bin` | 0, or the tolerance agreed beforehand |
| A VU program's native version | see `dev/vu_harness/README.md` | every run identical |

Replace `UYA_NTFPROC=0` with the switch of whatever you changed (see the switch list in the
project notes).

## Traps

- **Load.** Headless runs give different frames when the machine is busy. Keep `JOBS` at 3 or
  4, and rerun a level before believing a difference. `UYA_FIXEDTIME=1` (a game clock that
  moves one field per vsync) may remove this; untested.
- **Problem lines.** `run.sh` counts crash and fault lines in the log. Four "unimplemented call"
  lines at boot are normal and are not counted.
- **Stale replay tool.** Rebuild `gs_replay_gl` whenever gs.c, gs_gl.c or the mesh limits change.

## Reading game code

- `python3 disasm.py 2352C8 40`: the executable (decomp/baserom/SCES_509.16) from 0x2352C8.
- `python3 disasm.py 281EA0 40 3`: level 3's program (build/levels/level_03, written by build.py).
- `python3 find_bytes.py 2352C8 64 0 3 13`: where an executable routine sits in levels 0, 3, 13.
  Level code moves per level; this finds it by its bytes with addresses masked.
