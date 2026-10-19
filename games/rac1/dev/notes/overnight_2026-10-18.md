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

## Piece 7, step 1: the tie and shrub chain starts as records (18 October, night)

- vif.c: VIF1 operations for records (uya_vif1_op_reg / _strow / _unpack / _mscal): the VIF's own effects,
  called by a record in its order, no VIF codes decoded, not counted as chain commands.
- TieProc (direct mode): its chain start (MPG REF; STMOD, UNPACK 9 at VU 966; MSCAL 0x40; STROW; BASE 0,
  OFFSET 0x2C, STMOD 0, STCYCL 4/4; FLUSHA) becomes one marker (NATIVE_TSTART_MARK) and a record that
  does the same at the marker, the REF's data read when the chain is walked. UYA_TSTARTREC 0/1/2.
- ShrubProc (direct mode): its start (MPG REF; STCYCL 4/4, UNPACK 3 at VU 0; MSCALF 0) the same way
  (NATIVE_SSTART_MARK, UYA_SSTARTREC 0/1/2). The records' R_START stays as it was.
- Check mode (record applied, then the original qwords through the VIF from the same state; VIF1 state
  and VU1 memory compared): tie 1000 of 1000 starts identical on levels 0, 3, 5, 13; shrub 800 of 800 on
  levels 3 and 13.
- Pixels: software, all 19 levels walking, 0 px against UYA_TSTARTREC=0 UYA_SSTARTREC=0; GL on the GPU
  (gl_ab_win.sh), levels 0, 3, 13, 0 px.
- Census (level 3, per send): MPG 32 -> 22 (15k -> 10k words), program starts from the chain 17.7 ->
  14.7, V4-32 unpacks 24.6 -> 21.6. Left: the particles (221571), the strips (57843), the tie clip start
  (224979, GL only), the remaining MPGs of those.
- The tie clip part's start (GL, when the clip part goes direct) as a record too: CNT 2 with DIRECT 2
  (A+D, sent through native_gif), REF 10 (UNPACK at VU 966), MSCAL 0x40 (the clip program's setup, 224979,
  still run at the marker: it leaves registers and the strip-tag templates other programs read), STROW,
  BASE/OFFSET/STMOD/STCYCL, FLUSHA. Emitted before TieProc knows the part goes direct, so its 10 qwords are
  taken back out of the scratchpad (nothing is flushed between) and a marker written instead. Check mode
  on the GPU (levels 3, 13): 2000 tie starts (main and clip) each, 0 differ; GL pixels 0 on levels 0, 3, 13.
- PartProc (221571, the particles): a word-for-word port with check mode is being drafted (agent), to be
  integrated and then made direct.

## Piece 7, step 2: the particles native and direct (18 October, night)

- PartProc (func_00218B10, 3060 bytes of asm) ported word for word (native_partproc.c, agent draft,
  integrated): UYA_NPARTPROC 0/1/2. Check mode, walking: 958-971 runs per level identical on levels 1, 3,
  8, 14 (the agent: 18,190 runs on all 19 levels); paths: screen quads, world quads, streaks, two-point
  culls, specials, paging appends, full/last/empty blocks. Not run anywhere: the line records (byte+1 & 3
  == 2). Native vs the game's routine: 0 px, all 19 levels walking.
- Direct (UYA_NPARTDIRECT 0/2): PartProc's whole part of the chain (MPG REF; CNT 6 + UNPACK 6 at VU 0; REF
  14: UNPACK 10 at VU 6, MSCALF 0, BASE 0, OFFSET 0x200, DIRECT 3; the record blocks: UNPACK at TOPS +
  MSCALF 0xA0; CNT 0 FLUSHA) is checked against the templates once built; its first qword becomes a NEXT
  past the part (NATIVE_PART_MARK). At the marker the qword is put back, the shape checked again, and the
  same operations done (uya_vif1_op_*, vu_mpg, native_gif) reading the part's own words; any other shape
  goes back to the chain. The program runs as from the VIF.
- Checks: UYA_STARTTRACE=a,b (vif.c) logs every VU1 program start with TOPS, TOP, DBF and a hash of VU1
  data memory: direct vs chain identical for every start in vsyncs 1500-1520 (level 3: 1073 starts,
  level 8: 795); level 3: 800 parts drawn directly, 0 back to the chain. Pixels: software all 19 levels
  0 px against UYA_NPARTDIRECT=0; GL on the GPU, levels 3, 8, 14, 0 px against UYA_NPARTPROC=0.
- Census, level 3 on the GPU, per send: program starts from the chain 14.7 -> 5.3, MPG 22 -> 11.8
  (10.0k -> 4.9k words), V4-32 unpacks 21.6 -> 11.2 (3.3k -> 134 words). What is left is the strip effect
  (57843: its starts, its masked V2/V4-8/V3 unpacks, its MPGs); its writers are being ported (agent).
- MPG census by source (level 3, GPU, after the particles): strips 57843 (0x10E810 packet) 1.4/send x 3
  MPGs; mobys 13859 (0x10FAA0, DrawMobys' VU1_addDataRef, outside MobyProc) 0.9 x 6; shrub clip 912339
  (0x1022E0, ShrubProc's clip part start) 0.4 x 3.
- ShrubProc's clip start: its MPG now in the record too (R_CSTART carries the packet; uya_vif1_op_mpg_packet
  in vif.c applies a microprogram packet: NOP/FLUSH*/MPG codes only). Records vs chain (UYA_SSTARTREC=0):
  0 px and identical start traces (levels 3: 1686 starts, 8: 1185).

## Piece 7, step 3: the strip service native (18-19 October)

- native_strip.c (agent draft, integrated): the strip setup func_001F7868, one strip func_L00_001FDE48,
  the strip list func_001F8B6C, word for word (UYA_NSTRIP 0/1/2). recomp.py: HOST_HOOKS_ALLREFS, an
  opt-in wider data-reference extraction for these hooks (every gp access incl. lwc1/swc1 and addiu from
  gp; lui with the first addiu/load/store based on it); the other hooks unchanged.
- Check mode identical everywhere (agent: setup 31,750 runs, strip 64,981, list 3,340 over 19 levels;
  here: levels 1, 9, 11, 17 identical). Native vs the game's: 0 px, all 19 levels walking.
- Census (agent, per walk): besides these three, seven level-specific builders write strips:
  L01_0021F9C8, L01_002BA150, L02_00250A58, L02_002A52D0, L06_00216B38, L09_002C2B08, L12_002BD3D0. They
  make most strips on some levels (level 9: about 59 starts a send; level 6: 20; level 2: 9; level 1: 7).
- A strip block in the chain (level 9): CNT with STCYCL 4/4 + UNPACK V4-32 1 at TOPS (the strip's GIF tag),
  then UNPACK 1 at VU 940, STCOL, STCYCL 3/1, STMOD 0, STMASK + UNPACK V2-32 (masked) at TOPS+1, UNPACK
  V4-8 at TOPS+2, STMASK + UNPACK V3-32 (masked) at TOPS+3, MSCALF 0x60 (0x70 clipped), FLUSH. Blocks can
  span tags: some REF vertex data written earlier in the chain. So recognising strip blocks in the walker
  would mean decoding that command stream (the VIF again, for one command set); removing the VIF for
  strips needs every producer hooked (the three above and the seven level builders), with the records
  holding the vertex data before it is VIF-encoded.

## Piece 7, step 4: the builders' strips direct, the last producers, the VIF1 stub (19 October)

- native_stripb.c (agent port of the seven level builders, word for word, UYA_NSTRIPB 0/1/2; check mode
  identical). In mode 2 each run is wrapped by native_stripd_begin/end and the builders' record point tells
  native_stripd of each strip (native_stripd_sem: its chain range, the GIF tag and VU 940 qword, where its
  ST/colour/position arrays are, in the range or REF'd, grid cells positions first, its start). The strips
  of the three service writers go through native_stripd_convert (the template's shape). Level 9: 0 ranges
  kept in the chain.
- vif.c, the stub (UYA_VIFSTUB 0/1/2): VIF1's own commands are NOP, FLUSH*, MARK, MPG and DIRECT; any other
  goes to the "full VIF", counted (uya_vif1_full_n) and logged once per command with the chain tag. 2
  drops them (testing). UYA_STUBTRACE=from,n: the last 16 chain tags and the 12 qwords after the tag, for
  the first n full-VIF commands from vsync `from`.
- What still needed the full VIF in gameplay (GPU census, vsyncs 1100-1600), and the fixes:
  - every level, 2 unpacks a frame: DrawTfrag's two transform packets (CNT 4; STCYCL 4/4; UNPACK V4-32 4
    at VU 5 and at 0x14D) in front of TfragProc's part. When TfragProc draws everything directly they
    become a record (native_tfragproc.c, UYA_NTFXFORM 0/1): the first packet's qword a NEXT past both
    (NATIVE_TXFORM_MARK); at the marker the qword is put back, the shape checked, the two unpacks done
    from the chain's own words.
  - level 13: a second particle routine, func_0022CEB8 (one list, up to 30 particles a block, VU0
    microprograms 0xC80/0xC90), writing PartProc's chain format (the same MPG and templates). Hooked at
    entry and return (UYA_NPARTLIST 0/1): the game's routine runs and its part goes through PartProc's
    part_direct (shape checked, NEXT marker, native_part_tag).
  - level 1: func_L01_00263338, a level writer of two 46-vertex strips (the second the first's CTXT copy:
    its own ST pairs, then a REF back to the first strip's colours, positions and MSCAL). Hooked
    (uya_stripl1, HOST_HOOKS_ALLREFS); its two strips go to native_stripd as semantic strips.
- The boot screens (vsync ~177) still use the full VIF (the executable's tie, terrain and part chains before
  a level program is loaded); the stub keeps running them.
- Results: software, all 19 levels, default vs UYA_NTFXFORM=0 UYA_NPARTLIST=0 UYA_NSTRIPDIRECT=0: 0 px
  (level 18 differed in the parallel run, 0 px rerun alone). GL on the GPU, levels 1, 3, 9, 13, the same
  A/B: 0 px. UYA_VIFSTUB=2 (the full VIF's commands dropped) vs 1 on the GPU, levels 1, 2, 3, 6, 9, 12,
  13: 0 px. GPU census, all 19 levels, vsyncs 1100-1600: VIF1 gets only NOP, FLUSH, FLUSHA, MPG (3
  sources, ~6 a send) and DIRECT; no UNPACK, MSCAL, STCYCL, STMOD, STMASK, STROW/STCOL, BASE or OFFSET.
- Still on the full VIF: the boot screens (11 command kinds logged once at vsync ~177), and the software
  renderer's paths (it draws terrain and mobys from VU memory, so their chains still go through the VIF).
