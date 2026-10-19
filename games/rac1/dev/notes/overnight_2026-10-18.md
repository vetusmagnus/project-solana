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

## The boot screens off the full VIF (19 October, morning)

- Cause: native_terrain.c found the tfrag table by the level's id (0x16100C on levels 0, 5, 14, 16, else
  0x160FCC). Before a level program is loaded the executable's TfragProc reads its own (0x160F8C, templates
  0x160A80), so the boot screens had no meshes and their terrain went to the VU1 through the chain (the
  template, MSCAL, the tfrag packets: STROW, STMOD, STMASK, V3-16 / V4-8 unpacks).
- Fix: TfragProc tells the terrain where it reads the table, the count and its templates t4..t7
  (native_terrain_where, from its own gp references); the meshes are built there, at once when the place
  is new (boot, a level loaded) so the first frame already draws directly. UYA_NTERRWHERE=0: the old
  lookup by level id (testing).
- Checks: every level's reported addresses equal the old per-level constants. GPU census from boot to the
  level: no command reached the full VIF (vsyncs 170-290: NOP, FLUSH, FLUSHA, MPG, DIRECT only). Software,
  19 levels, native terrain vs UYA_NTERRAIN=0: 0 px (level 15 differed in runs overlapping another
  session's build; alone, 0 px). Software boot frames (vsyncs 170-260): 0 px. GL gameplay, levels 3, 14, 15,
  new vs old lookup: 0 px.
- GL boot frames, native terrain vs the VU1 path: 2,680 px over 89 frames (about 38 a frame). Not from this
  change: the GPU terrain meshes are not bit-exact with the VU program (gameplay, level 3, the same A/B:
  59,779 px over 79 frames). The boot screens used to avoid it only by going through the VIF.

## The GS model, item 1: the native renderers draw with materials (19 October, night)

(Scope: dev/notes/gs_model_scope.md. Items 1-3 asked for; item 4, textures without GS memory, left.)

- native_draw.h / native_draw.c: a material in host terms (the texture by where the game's upload put it,
  its palette, filtering and levels, wrap, mip levels, the primitive's flags; parts a material gives in
  `has`), read by the renderer from the game's own material blocks (nd_material_from_regs: TEX0, TEX1,
  CLAMP, MIPTBP1/2, PRIM of one pass; any other register, or a value with bits a material has no field
  for, stays as register writes). Tie, shrub, moby and terrain meshes (and their clip passes) draw with
  nd_regs_mesh_draw / nd_mesh_draw (op 15): no GS register writes. Pass settings in host terms too
  (nd_pass: blend, alpha/depth/target tests, target, depth, scissor, offset, FBA; fog colour, TEXA, colour
  clamp, PRMODE/PRMODECONT, dither, PABE, TEXFLUSH): the native effect programs' packets (fx 57843,
  221571, the tie/shrub/moby packet paths) have their register runs turned into materials and pass
  settings (nd_pack, ops 17 and 18); their vertices stay as vertex strips. UYA_NDRAW: 0 register writes as
  before, 1 check, 2 (default).
- gs.c: the frame's two passes are host state (gs_pass_t gpass, decoded once when set), and a command's
  pipeline state is built in parts (cmd_pass from the pass, cmd_target, cmd_texture). A material draw
  builds its state from the material and the pass (cmd_material), not from registers; the material's
  palette loads as its load mode says (the GS's CLUT buffer semantics), and the register image the
  game's own GIF still reads afterwards is kept as the PS2's GS would hold it.
- gs_gl.c: per-material programs for native meshes: the pixel stage compiled with the material's
  switches as constants (textured, texture function, fog, alpha test and function, wrap modes, filters,
  levels, the blend's terms, depth format); the values stay uniforms. UYA_GLSPEC=0: the general program.
- The game's own GIF state packets (VIF1 DIRECT, PATH2) go through the same packing at the sink: their
  register runs become pass settings and materials. Only texture transfers (BITBLTBUF..TRXDIR, uploads:
  item 4) stay as register writes.
- Checks: UYA_NDRAW=1 on level 3: 1.4 million material draws, the state identical to the register path's
  for every one. Software, all 19 levels, materials vs register writes: 0 px (meshes alone, then with the
  packets packed; level 13 differed once in the parallel run, 0 px rerun alone).

## Item 2 begun: the game's 2D producers as host primitives

- native_2d.c: a producer done natively from its arguments; its packet's place in the chain becomes a
  marker (NATIVE_2D_MARK, a NEXT past the packet) and a record, drawn where the chain meets it as host
  primitives (gs.c op 19: the material, then vertices the host assembles into the primitive kind's
  triangles, strips, fans or sprites; the GS's vertex registers and queue are left as the same GIF would
  leave them, for game GIF that still follows). UYA_N2D: 0, 1 check, 2 (default).
- DrawTexturedQuad func_001F5800 (glyphs, HUD images): check 50/50 packets identical on the boot screens.
  Its GIF template is found by its exact contents near the chain word (0x6C0 before it in the executable,
  0x8C0 in the levels).
- The sky's two emitters (asm): func_0022D3F8 (Gouraud triangles: per vertex RGBAQ, the colour word
  sign-extended so its top bit becomes Q's bits, and XYZ2) and func_0022D520 (textured: TEX0 when a
  triangle's texture byte changes, the game's own sky texture pager inline (func_0022D6B4: palette and
  image placed at the allocation pointer, the TEX0 kept in the sky table, an upload entry added to the
  list at 0x18D140), per vertex ST as 12-bit fixed point, RGBAQ 0x808080 + the vertex's alpha, Q 1.0,
  XYZ2). Records of several op 19 blocks (the material changing inside a packet). Check: every packet
  identical, levels 1, 3, 9 (over 6,000 of each kind a level).
- gs.c op 19 is host code assembling primitives; when the GS's vertex queue already holds vertices from
  before (no PRIM since), the vertices go through the queue as the GIF would send them (counted and
  logged; not seen in the tests).
- Checks: software, all 19 levels, UYA_N2D=0 vs default: 0 px (level 6 differed once in the parallel
  run, 0 px rerun alone); boot frames (vsyncs 170-400): 0 px. GL on the GPU, levels 1, 3, 9, 13: 0 px.
  Item 1 with it: GL levels 1, 3, 9, 13, UYA_NDRAW=0 UYA_N2D=0 vs default: 0 px; per-material programs vs
  the general one (one recording replayed in WSL's GL twice; the replay is Mesa's compiler, not the
  GPU driver's): levels 3, 9, 13: 0 px.
- What the game still sends as GIF (level 3, GPU, UYA_SRCSTATS, a vsync): PATH2 507 packets, 1,508 drawn
  vertex kicks (and the texture uploads' data); native 2D 15 records, 85 kicks. Of the game's GIF
  primitives (UYA_PRIMLOG, software, level 3) about 726 of 925 are the shadow volumes: FRAME with FBMSK
  0xFFFFFF (alpha only), TEX0 0x664021000, ALPHA 0x8000000044: func_002298B0's side quads and caps
  (hand-written asm, from the scratchpad) with the destination-alpha stencil. The rest: the frame copy
  strips (AA_BlurPass's tables, built once by the asm func_001FAB40), the darkening pass that reads the
  stencil (func_00228690), billboards, the GS state blocks (already packed into pass settings).

## Item 2 continued: the shadows, the billboards, the frame copy (19 October, early morning)

- The shadow volumes, func_002298B0 (asm; the bulk of the game's 2D on level 3): its three packets (the
  sides: a quad, PRIM 0x54, per record whose side flag is the pass, from the scratchpad records; the
  front and back caps: fans, PRIM 0x55, when the sign times the cap's depth allows) built natively, each
  a record of host primitives; VU0's vf1/vf2 left as the routine leaves them. Check: 14,000 calls a level
  identical (levels 3, 9). op 20: the vertex colour a packet leaves with no vertex of its own.
- The darkening pass that reads the stencil, func_00228690 (C in the decomp; translated in the levels):
  the TEST setting (destination alpha test on) as a pass setting (op 18) and its 32-pixel sprite strips
  as host primitives. Check: identical (levels 3, 9).
- The glow billboards, func_001F9478 (C, with VU0 sine/cosine): the game's routine runs; at its return
  each billboard packet it wrote (its fixed layout checked exactly) becomes a record of host primitives.
- The frame copy, AA_BlurPass func_001FB848 and its twin func_001FB8A8: their REF to a table the game
  builds once (func_001FAB40) becomes a marker; the table (its exact shape checked: TEST, CLAMP, TEX1,
  TEX0 the frame buffer, 16 sprite strips; the twin's line) becomes the pass setting, the material and
  host sprites drawn with the colour the GS holds (op 19 flag 2). The table is kept with the record and
  compared again where the chain meets the marker; changed (the display set up again): GIF data.
- A hazard found: in the levels a translated body can be shared by look-alike routines (the same bytes
  but for their addresses: the catalogue names VU1_texFlush and three others func_001FB848), and a hook
  in it then gets that body's relocations, which name other words in another routine (the frame copy's
  hook saw a gp offset of -0x1240 and "chain word" 0x29 from texFlush's body). Every 2D hook now checks
  its references strictly (chain_word_ok: the chain pointer's own word, holding a qword-aligned EE
  address; the known gp offsets and tables) and otherwise lets the game's routine run. The earlier
  builds passed every pixel test, but a hook taking a wrong body's references could have written to the
  wrong memory: the builds from this round on have the checks.
- Level 3 on the GPU (UYA_SRCSTATS, averaged over 400 vsyncs): the game's own GIF went from about 1,470
  drawn vertex kicks a vsync (with only the quad and the sky native) to about 490; the native 2D draws
  about 740. Final build: software 19 levels 0 px (level 5 once in the parallel run, 0 px alone), boot
  frames 0 px, GL levels 1, 3, 9, 13 0 px.

## Where items 2 and 3 stand (19 October, morning)

- Item 2 is partly done: the glyph/HUD quad, the sky, the shadow volumes and their darkening pass, the
  billboards and the frame copy are host draws; what remains of the game's GIF on level 3 is a few
  hundred vertex kicks (other effects with ST textures, overlays, the GS state packets, which are already
  pass settings), and on other levels and screens whatever producers those use. Every one of these still has to keep the GS's
  register side effects for whatever GIF follows (op 19 leaves them as the GIF would), until all the
  producers are ported. The ports left: the remaining ST-textured effects seen on level 3 (to be identified), the HUD sprite
  family (func_00200468 and siblings), DrawRectOverlay, the fades and clears, the letterbox, the text
  variants (func_001F5988, func_001F5BB8), the menus; and, to describe the frame copy without reading
  its table, the table builder func_001FAB40 itself.
- Item 3 not started: it needs item 2 done first. In gameplay the GPU targets already never sync with GS
  memory (piece 6: 0 pages read back or uploaded); what keeps them "GS buffers by address" is that the
  game's own GIF and the texture reads name them by FBP / TBP. Once the frame copy and the shadow pass are
  native, the targets can be host objects (the back buffer, the display buffer, Z) with the boot and
  level-load overlap (loading textures in the Z buffer's memory) handled explicitly, and the page
  coherence code (owner / valid per 8 KB page) removed.

## Item 2 finished: the remaining producers (19 October, day)

- A census of who writes the game's GIF packets, per level (testing aid, host/src/rac1/cwatch.c): a
  build made with UYA_RC_CWATCH=1 (tools/recomp.py) wraps every call in the translated code, and the
  call that moved the VIF1 chain pointer over a packet owns it (the innermost); UYA_CWATCH=0 (or the
  chain word, hex) and UYA_CWATCH_AT=a,b print, at vsync b, each writer's packets, qwords and drawn
  vertex kicks a vsync. dev/scripts/cw_build.sh builds such a tree (~/solana-dev/rac1_cw).
- What it showed (all 19 levels, walking): besides texture uploads (item 4) and state-only packets
  (frame setup blocks, already pass settings), the game's own drawing came from: the sky's two emitters
  and the frame copy (supposed native since last night, but the hooks' strict checks compared the gp
  offsets with the executable's (-0x5D00): in the levels (-0x5AC0, -0x5A80) the game's routines ran;
  the pixel tests passed because the fallback is the game's routine), the fogged quads
  func_L00_001FD428 (up to 970 kicks a vsync on level 10), func_001F856C's fogged fans, the stencil
  clear func_002284E8, the screen clear (func_001FB530's REF to the table at 0x152140), HudSprite
  func_00200468 and its strip func_00200650; and the GS register packets of VU1_addGSregister and
  VU1_setScissor.
- Fixed: the checks now take the layout's own words (gp_chain_word: the chain word must be what gp
  and the offset give; the sky's table and count offsets per layout); the levels name the frame copy's
  twin as a second place of the first (func_001FB848__lNNat...): its hook goes on every place
  (HOST_HOOKS_ALLPLACES) and the table gives the size.
- New ports (native_2d.c): VU1_addGSregister and VU1_setScissor from their arguments (the write as a
  pass setting or material, op 17/18); the stencil clear from its arguments; HudSprite and its strip
  from their arguments (the levels' copies' addresses, GetFrameTex's among them, from the hook's
  references: recomp.py now adds a routine's call targets for HOST_HOOKS_CALLS); the screen clear as
  the frame copy (the table parsed when the REF is written, kept, compared again at the marker); the
  fogged quads and fans when the routine returns (their VU0 math is the game's own): the packet's fixed
  layout checked word for word, then a record.
- op 19 flag 4: the vertices have no UV (UV is what the GS holds). Several ports (the sky, the shadow
  volumes and caps, the darkening pass, the frame copy's line) left the GS's UV register (and with
  flag 0 its ST) at 0 where the game's packets do not write them; no pixel showed it, but the register
  image now matches.
- build.py: UYA_SKIPGEN=1 compiles the host sources against the last build's generated code (a change
  to host/src alone: seconds instead of ~45 minutes).
- Also ported: the fade func_001F55C0 (VU1_addGSregister RGBAQ, then a REF to the table at 0x13CD90:
  converted at its return, the table kept and compared at the marker) and DrawRectOverlay func_001F5650
  (the subtitles' box; its fixed packet converted at its return).
- A regression found and fixed on the way: VU1_addGSregister's records put markers inside the glow
  billboards' part of the chain, and the billboards' walk stopped at the first one (most billboards went
  back to GIF data; pixels identical, the fallback being exact). The walks (billboards, fans) now step
  over markers.
- After (census, all 19 levels walking, vsyncs 1450-1750, the build before the fade/overlay/billboard
  fixes): the game's own drawing through GIF fell from hundreds of vertex kicks a vsync per level (level
  10: ~1,400; level 3: ~490 after last night) to at most ~5, from: the billboards' walk (fixed since),
  DrawRectOverlay and the fade (ported since), the sky's setup table (a REF to 0x13D1F0, A+D state, 1.3
  kicks on levels 0, 12, 15), and writeGSHeader packets from rare callers (~1.4 kicks, 0.04 packets a
  vsync). Not covered by the census: menus, cutscenes, boss fights.
- Checks: UYA_N2D=1 on levels 1, 3, 6, 9, 10: every port with a check identical (quad, sky both, shadow
  volumes, darkening pass, GS settings, stencil clear, HUD sprites). Software, all 19 levels, UYA_N2D=0 vs
  default: 0 px (final build: all 0 in the parallel run); boot frames 0 px. GPU (Windows build), levels
  1, 3, 9, 13: 0 px.

## Item 3: the GL targets as the game's buffers (19 October, day)

- Measured first (dev/scripts: gldl.sh, UYA_GLDLLOG=1 now also logs uploads into a target): over a whole
  level 3 run on the GPU (boot, level load, walking) the targets meet GS memory at three places only:
  vsync 1 (the frame buffer's first use takes its contents from GS memory), the boot screens' texture
  uploads (vsyncs 26, 29) and the level load's (vsync 893) into the Z buffer's memory (45 uploads each:
  the pages they partly cover read back first), and the Z buffer taking those pages again before it is
  next drawn (vsync 918, with 8 pages of the second frame buffer). In gameplay: nothing (the one row
  there is the GS recording's own snapshot, a testing aid). Three buffers in all: 0000 and 1000
  (colour, PSMCT32, FBW 8) and 1E00 (Z).
- So with the textures in GS memory (item 4, left alone), the targets' link to it is these uploads and
  reads. The change (gs_gl.c, UYA_GLTGT: 1 the default, 0 the earlier model):
  - the targets are the game's buffers, kept for the session: no LRU cache by address (the earlier
    model evicted the least recently used of 24); a buffer set up at another one's base with another
    width or format retires it (its newer pages stored first, logged; not seen in the runs);
  - the meeting points with GS memory are the named ones above, nothing else;
  - each buffer keeps, per row of pages, whether they are all current and all its own: a draw whose
    rows are skips the page work before (ensure_valid) and after (claim). Level 3 walking on the GPU:
    about 12,000 of these a vsync skipped, 5 done (draws reaching row 14 of the second frame buffer,
    which is the Z buffer's first page), 0 pages moved.
  - A first version took a buffer's whole texture (1024 rows) as its area: the two frame buffers then
    "overlapped", retired each other every frame and lost 643 pixels on level 9 through GS memory; the
    replay A/B caught it. Fixed: retirement only at the same base; the flags per row.
- What stays and why: the buffers are named by the game's own description (FRAME/ZBUF: base, width,
  format), which is the only name the game gives them; the 8 KB-page sync code stays for the uploads
  into the Z buffer's memory at boot and load and for any read of a buffer's memory as a texture, CLUT
  or readback, because those textures live in GS memory. Removing that needs item 4.
- Checks (the same GS input replayed in WSL's GL, UYA_GLTGT=0 vs the default): levels 3 and 13 from
  vsync 0 to 1000 (boot screens, the level load, the start of gameplay; dev/scripts/gl_replay_boot_ab.sh):
  999 frames each, 0 px; levels 1, 3, 9, 13 walking (vsyncs 1560-1639): 0 px. The GPU A/B of the game side
  (UYA_N2D=0 vs default) on the final build: 0 px on levels 1, 3, 9, 13.
