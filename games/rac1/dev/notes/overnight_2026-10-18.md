# Overnight work, 17–18 October 2026

Goal: drawing with no emulation, while the output stays identical to the game's own routines.

## Now native (all on by default)

| Piece | Switch (0 = the game's routine) | Verified |
|---|---|---|
| TieProc, direct and clip | `UYA_NTIEPROC` (2 direct) | check mode identical; 0 px software on 19 levels; 0 px GL on 7 |
| ShrubProc, direct (main, fade, clip, billboards) | `UYA_NSHRUBPROC` (2 direct) | 0 px on 19 levels, standing and walking |
| MobyProc | `UYA_NMOBYPROC` (2) | check mode identical on 19 levels walking; 0 px on 19 |
| Moby skin driver (replaces the VU0 image) | `UYA_NMOBYSKIN` (2) | check mode identical (~550k runs); 0 px on 19 |
| Moby direct (records; no VIF at draw time) | `UYA_NMOBYDIRECT` (2) | check mode 0 differences in ~4M program starts; 0 px on 19, standing and walking |
| All together vs all off | — | 0 px on 19 levels, standing and walking (software); GL 0 px on levels 3, 13 (all 79 frames), Kerwan and level 2 |

Mode 1 for each switch runs the native routine and the game's one and compares them.

## Decisions taken without you

- Shrub 6-vertex classes read a stale colour from VU memory. Kept exact: VU1 memory stays the programs' working memory (`UYA_SHRUBPRIV=1` gives a private image instead).
- Native code calls libm `sqrtf` through a volatile pointer, matching the generated code (MXCSR is round-toward-zero).
- Direct draws are labelled `PATH1 …` / `PATH2 …`, because gs.c reads the GIF path from the label.
- I rejected the first moby direct draft, which ran a VIF interpreter at draw time, and had it rebuilt on the records pattern.

## Not yet exercised

List-full cases, the retry, the skip flags, MobyProc blended and point lights, and the fog fallbacks. None came up on the 19 levels walked.

## For you

1. Run the Windows build (`build/windows/rac1_wrapper.exe`, cross-built in WSL) on Windows.
2. Review and commit; a suggested message is below. The tree also holds the earlier uncommitted terrain work (`native_terrain.c/h`, `native_tfragproc.c`, `dev/tests`, `dev/vu_harness`).
3. Build and publish a kit, if you want one.

## Piece 4, started the morning of 18 October

- The game's own GIF data (VIF1 DIRECT: HUD, text, fades, sky, shadow volumes, GS state; the GS library's
  PATH3 packets; any PATH1 packet the fast converter cannot take) now goes to the GS as native draw-list
  ops, register by register, exactly as gs.c's GIF front end would take it (native_gif in
  native_dlist.c; new ops 10–14 in gs.c: RGBAQ, ST+Q, UV, vertex kick, fog). Only IMAGE transfers
  (texture uploads, piece 5) still go as GIF data. `UYA_DLIST=0` sends everything as GIF (to compare).
- Level 3, walking: about 1M qwords as ops, 4.5M IMAGE qwords as GIF, 0 split tags.
- Decision: no per-function hooks for the HUD and text. The ops they now produce are the GS register
  writes and vertex kicks piece 6 takes as its input, so a host rewrite of each sprite and glyph
  routine would emit the same ops for no gain. The shadow volumes' destination-alpha pass is a
  renderer (piece 6) matter: it needs a stencil, or alpha kept exactly, on the GL side.
- Verified: 0 differing pixels on all 19 levels, walking (converter on vs `UYA_DLIST=0`). The GL replay
  tool (dev/tests/build_replay.sh) was rebuilt for the new ops.
- Leftover moby program starts (13859, about 14 a frame in the stats): all on the boot screens, before a
  level program is loaded (vsyncs 177–299 in the tests, 118 a frame). There the executable's own copies of
  MobyProc (func_00212658) and of the skin driver (func_001EE9F8, VU0 image 104691) run; only the level
  copies were hooked. Both are now in HOST_HOOKS. Check mode on the boot screen: MobyProc 100/100 runs
  identical, skin driver 100/100; boot frames 0 px (20 frames, native vs the moby switches off); levels 3
  and 13 walking 0 px against everything off. In gameplay no moby program starts from the chain. Testing
  aid: `UYA_STARTLOG=image,n,from` logs the chain tags before an image's starts from the VIF.
- The tfrag program starts are only the first 124 TfragProc runs after a level loads.

## Piece 5, first measurement

Texture lookups per vsync, level 3 standing (UYA_FPSLOG=1 UYA_NTEXWHY=1): 1225 from kept uploads, 10743
from GS memory: PSMT8 at TBP 0 4382, PSMT8 mip levels 1+ with no upload there 5710, PSMT8 level 0 with
no upload 318, CT32 250, PSMT8 upload of another size 75. Level 13: 744 / 7659, the same mix. Research
on where each class comes from: piece5_research.md.

## Piece 5, first steps (18 October, afternoon)

Changes (host/src/gs/gs.c, gs_gl.c):
- Kept uploads (urec) now also keep PSMCT32 uploads (palettes, the 8x8 grey), accept a texture that is the
  top left corner of an upload with the same buffer width (square uploads of non-square textures), and
  hold 2048 records; a new upload takes the next slot whose record is dead (its first block written
  since), so the level's resident uploads (palettes, mip levels 2 and 3, shrub billboards) stay.
- Palettes come from the kept 16x16 PSMCT32 upload when it is intact (clut_from_upload), not from GS
  memory. Check mode (UYA_NTEXCHECK=1): 1.12 million palettes compared with GS memory, 0 differ.
- GL: every level of a mipmapped texture is decoded when its batch is drawn, and only the levels the
  record's own Q range can reach (each texture record in a batch keeps its Q range; a native mesh draw's
  range comes from the mesh's box under the draw's matrix: Q = k / w, w linear). The batch is always
  drawn before an upload changes memory (gsgl_cpu_write_rect flushes). UYA_GLMIPDEFER=0 turns it off.
- Testing aid: UYA_NTEXWHY=1 also logs why a palette is not from its upload and the first TBP 0 lookups.

Results, level 3 walking, per vsync:
- Software: lookups by upload 3353, by GS memory 1283 (was 1225 / 10743). The software renderer still
  decodes every level of a state, so its TBP 0 lookups stay (736); it is the reference, not the target.
- GL: lookups by upload 4444, by GS memory 1652 (was 1223 / 10305); palettes 1877 from uploads, 2 from
  GS memory (was 0 / 1885). Of the 1652, 1616 are TBP 0 (still to find: draws whose LOD range reaches
  level 0 by the bounds, or draws outside the two ranges above).
- Pixels: software walking 0 differing pixels on levels 3, 13 and 5 against UYA_NTEX=0.
- Later the same afternoon: a native mesh draw's Q bound also takes w all negative (the game's w is
  negative in front of the camera for tie/terrain meshes; the first version only took positive w, so
  every mesh draw read every level). GL, level 3, per vsync: lookups by upload 4237, by GS memory 38
  (TBP 0 12); palettes 2259 from uploads, 2 from GS memory.
- Verified: software walking, all 19 levels, 0 px against UYA_NTEX=0 (walkab_par.sh; two levels that
  differed in the parallel batch matched on a rerun, see below). GL on the GPU: one recording per level
  replayed twice, with the defaults and with UYA_NTEX=0 UYA_GLMIPDEFER=0 (dev/scripts/gl_replay_ab.sh):
  levels 0, 3, 5, 8, 11, 13, 18, 79-80 frames each, 0 px. (gl_ab_win.sh replays both recordings through
  the same GS code, so it cannot test GS-side changes; gl_replay_ab.sh does.)
- UYA_FIXEDTIME under a -j 16 build plus a GPU run diverged in 2 of 38 runs; walkab_par.sh now reruns a
  differing level alone.
- (Superseded:) Not verified yet: the GL pixel comparison.

## Piece 5: can GS memory go? (18 October, evening)

Question: if every texture the game samples is an intact upload, uploads need not be written into
emulated GS memory at all. Test: UYA_NTEXPOISON=1 (textures) / 2 (palettes) / 3 makes every 8-bit
texture level and palette that is not an intact upload solid magenta; frames are compared with a normal
run (software, walking). Any difference is a draw that really samples such memory.

- First result: thousands of pixels on most levels (Ratchet's own textures). Cause: a bug in the upload
  bookkeeping, not the game: urec_drawn marked one page too many after a draw's frame or Z buffer
  (pg <= (bp + n) / 32), and the Z buffer (0x1E00, 448 rows) ends exactly at block 0x2C00, where the
  level's resident textures start; so those uploads counted as drawn over and their draws fell back to
  GS memory (same pixels, but through memory). Fixed: (bp + n - 1) / 32. Also: draws now mark only the
  rows a primitive reaches (its lowest vertex; a native mesh's projected box), not the whole scissor.
- After the fix, all 19 levels: 0 to 774 pixels over 17 frames (levels 1, 2, 7, 8, 9, 11, 13, 14: 0).
  What is left is the game itself sampling memory that holds another texture: a moby's TEX0 keeps a
  stale TBP when its distance test did not stream that level this frame, but the GS's LOD still picks
  it at the edge (e.g. level 3: a 256x256 texture at 3717, TBW 4, over this pass's 128x128 upload at
  3717). On a PS2 that is a brief texture glitch; reproducing it exactly needs GS memory.
- Decision: keep GS memory as the fallback store for those samples (uploads keep writing it); the
  textures and palettes themselves come from the game's uploads. Dropping the fallback would change
  those pixels (an improvement on the PS2's look, but not the original).
- Checks after the fix: software, all 19 levels, 0 px against UYA_NTEX=0; GL on the GPU, one recording
  replayed with and without (gl_replay_ab.sh): levels 3, 13, 0, 14, 79 frames each, 0 px.
- Testing aids added (piece 5): UYA_NTEXPOISON, UYA_NTEXWHY_FROM / _N (each non-upload lookup with its class),
  UYA_NTEXWHY_DRAW=n (draws that reach the uploads' memory). The GL recordings timed out (the game ran at about 1
  vsync/s under llvmpipe this afternoon, half this morning's speed; the deferral is not the cause: on
  and off run at the same speed). A separate session is making the GL tests faster (the real GPU, or
  UYA_FIXEDTIME); the GL check runs once that is in.

## Next

Pieces 5–7 (textures, render state and targets, then DMA/VIF to a stub).

## Suggested commit message

```
Native TieProc, ShrubProc, MobyProc, moby skin and moby direct drawing

Replace the game's tie, shrub and moby draw routines with native
word-for-word versions that draw directly through records at chain
markers, keeping chain order and the one-frame lag. Each has a
UYA_N* switch (0 game routine, 1 check, 2 native/direct). Verified
pixel-identical on all 19 levels, software and GL.
```

## Piece 6: where it stands (18 October, evening)

Measured on the GPU (Windows build, level 3 walking, UYA_FPSLOG=1 UYA_GLDLLOG=1; dev/scripts/win_run.sh):
- Targets are already plain GL textures (gs_gl.c tgt_t: back buffer 1000/8, display 0000/8, Z 1E00/8),
  and gsgl_present draws the display target to the window directly. The game's 16 copy strips are
  ordinary GPU draws between targets.
- From the level's start to the end of the run: 0 pages read back from the GPU into GS memory and 0
  uploaded from GS memory into targets (one full readback at vsync 1563 is the test's own GS recording
  starting). So in gameplay the GPU path does not sync with GS memory.
- The syncs left are at boot (45) and at level load (45): the game uploads loading-screen textures
  (PSMT8H, 32x32 and 64x64) into blocks 0x2330..0x25E0, which the Z buffer (1E00..2C00) uses during play;
  the Z pages they partly cover are read back once to keep the rest. That is the game sharing memory
  between a 2D texture and the Z buffer, not a host habit.
- What is left of piece 6 is the GS register front end: the native renderers still describe draws as
  GS register writes (TEX0, TEX1, ALPHA, TEST, FRAME, ZBUF...) and vertex kicks, which gs.c turns into
  the GL pipeline state (cmd_setup -> pcmd_t -> gs_gl.c). That is a translation, not emulated memory;
  replacing it with a native material API is a large redesign with no visible change.
- Testing aid: dev/scripts/win_run.sh LEVEL "VARS" [NAME] runs the Windows build on the GPU once and
  prints its log's path.

## Piece 7: what the chain still carries (18 October, evening)

Measured on the GPU (win_run.sh, UYA_VIFSTATS=1100,1600: VIF1 commands with their data words, and VU
program starts, between those vsyncs), per DMA send:

| | level 3 | level 13 | what it is |
|---|---|---|---|
| DIRECT | 381 (194k words) | 341 (171k) | texture uploads and the game's own GIF data (already native ops) |
| MPG | 32 (14.7k words) | 27 (12.4k) | microprogram uploads (the host identifies the program by them) |
| UNPACK | 36 (6.1k words) | 15 (2.0k) | data for the VU1 programs still run from the chain |
| MSCAL(F) | 18 | 11 | program starts |

VU1 starts per send (level 3): 221571 (effects) 8, 57843 (effects) 5, 13507 at 0x0040 (tie) 2, 56467 at
0x0000 (shrub) 1, 224979 at 0x0040 (tie clip) 1.3. VU0: image 28259, about 1280 runs per send (micro mode,
from EE code). Research on each producer: piece7_research.md.
