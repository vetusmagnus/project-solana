# Moby direct draw path: design

Read-only design. Sources: host/src/native/native_mobyproc.c, native_moby.c/.h, native_shrub.c,
native_shrubproc.c, native_tie.c, host/src/sdk/dma.c, host/src/gfx/vif.c, host/src/gfx/vu.c, scratchpad
mobyproc_notes.md / mobyskin_notes.md / piece4_notes.md, and the executable and level-0 images (read with
scratchpad mdd/dumpt.py, segments parsed as in dev/tests/disasm.py). Addresses are level 0 (PAL) unless
marked. Nothing in either tree was edited, built or run.

---------------------------------------------------------------------------------------------------
## 0. Summary of the decision

* **MobyProc keeps writing the chain exactly as it does now** (every tag, every hole, the same `sp`).
  It records the draws alongside it, then at its exit it overwrites only the **first qword of each
  "run"** with a marker that is a valid DMA tag: **NEXT, qwc 0, addr = the run's end**, VIF words
  `NATIVE_MOBY_MARK` and the segment id. The walker draws the segment when it meets the marker, then
  follows the NEXT. If the segment is unknown, the NEXT still skips the run cleanly (no garbage tags).
* Holes stay in the chain. The skin driver, the colour patch, DmaMobyTextures and the shadow splice
  are unchanged and all finish before the chain is walked, so at walk time the hole contents are final.
  Records hold **chain addresses**, read at walk time, the way the REF tags would have read them.
* The segment drawer writes into **VU1 memory (0x1100C000)** exactly what the VIF would have written
  (the program reads stale VU memory, see §3), keeps a mirror of the VIF1 state (TOPS/DBF/STCYCL/
  STMASK/STCOL/STROW/STMOD) that it reads from vif.c at the start of a segment and writes back at its
  end, and calls the same native_moby_run() at each MSCAL (factored out of vu.c's native_vu1()).
* Check mode (UYA_MOBYDCHK=1): MobyProc records but writes only a harmless marker into the address
  word of a CNT tag; the walker draws through the chain as now, and at every moby MSCAL the VU memory
  the program is about to read is compared with what the records would have written.

---------------------------------------------------------------------------------------------------
## 1. What MobyProc records, where the markers go, when the data is final

### 1.1 The chain MobyProc writes (per call), with its pieces

Templates (SPR copies made at setup; values read from the level-0 image, see §2.1):

| SPR | gp (L0) | addr (L0) | tag | VIF words | use |
|---|---|---|---|---|---|
| 0x3F40 | g1 -0x6430 | 0x1608D0 | CNT qwc 4 | STCYCL 4/4; UNPACK V4-32 num 4, addr (abs) patched | matrix rows / constants |
| 0x3E80 | g4 -0x63D0 | 0x160930 | REF | NOP; UNPACK 0x69 V3-16 signed, +TOPS, addr 0 (num patched) | positions |
| 0x3E90 | g5 -0x63C0 | 0x160940 | NEXT | NOP; UNPACK 0x6E V4-8 unsigned, +TOPS, addr 0x061 | colours (data inline: the hole) |
| 0x3EA0 | g6 -0x63B0 | 0x160950 | REF | NOP; UNPACK 0x6E V4-8 unsigned, +TOPS, 0x061 | extra pass colours |
| 0x3EB0 | g7 -0x63A0 | 0x160960 | NEXT | NOP; UNPACK 0x75 V2-16 **masked**, signed, +TOPS, 0x0C2 | extra pass ST (inline) |
| 0x3EC0 | g8 -0x6390 | 0x160970 | REF | NOP; NOP | the class packet (its own VIF codes) |
| 0x3ED0 | g9 -0x6380 | 0x160980 | CNT qwc 0 | NOP; MSCAL (imm patched 0x0A / 0x0E) | the program start |
| 0x3F00..30 | a5abs 0x1C3240 | | CNT qwc 2 / DIRECT 2; GIF tag A+D NLOOP 1; A+D ALPHA_1 0x8000000044 (0x3F20) or TEST_1 0x5360B (0x3F30) | GS state blocks |

Chain layout (native_mobyproc.c):

1. **Chain start** (lines 226..239, 0x256BF8): q0 `CNT qwc 0, VIF NOP / MSCAL 0x0000`; q1 `REF qwc 4 ->
   a6abs (0x1C2800)` (the program's VIF setup codes, §2.2); `CNT 6 + STCYCL + UNPACK V4-32 x6 -> 0x123`,
   6 qwords (a7abs 0x16CB40 +0..0x50); the same to 0x287. 16 qwords.
2. **Per moby, per packet** (lines 723..800, 0x257500): first two packets: matrix CNT (template 0x3F40,
   addr 0x129 when s4 < 0 / 0x28D when s4 > 0) + vf15..vf18 (lines 731..738); `T2` REF positions
   (qwc = byte 1 of the packet entry's +0xC word, addr = hole + byte2*16, num = byte 3); `T3` NEXT colours
   (qwc = byte 2, data = the hole's start, next = hole end); **the hole** (byte1 + byte2 qwords, unwritten);
   `T4` REF the class packet (addr/qwc from the packet entry's +0 dword); first packet only, if t6
   (flags & 0x208): CNT+FLUSHA, CNT 2/DIRECT 2, GIF tag, A+D (lines 775..795); `T5` CNT MSCAL 0x0A/0x0E
   (line 799). After the packets: the restoring GS block (lines 801..813).
3. **Extra pass** (lines 841..965, 0x257B88, moby+0x73 fade): matrix as above (rows rotated toward the
   camera, vf18 += SPR 0x3EE0 = (0, 0, 1000.0, 0)); `T2` REF positions (qwc a0 at hole+a1*16), `0x3EA0`
   REF colours (qwc a1 at hole+(a1+a0)*16), `0x3EB0` NEXT ST (qwc a1, inline = the hole's start, next =
   hole end), hole = ST(a1) | positions(a0) | colours(a1); `T5` (0x3EC0) REF class packet; `T6` (0x3ED0)
   MSCAL with s3 = 0x1400000A (always the clipping start 0x50). No GS blocks.
4. **Pass 2** (a3 != 0; lines 1101..1104, 0x257848): `CNT+FLUSHA` (emit_close), then **the shadow slot**
   (g14 = its address; MobyProc leaves it unwritten), then pass-2 mobys as in 2/3.
5. **End** (lines ~1155..1161, 0x2577A0): `CNT+FLUSHA`, g13 = sp.

### 1.2 Runs and markers

A run is a stretch of the chain that only the moby program consumes:

* run A = [q0 of the chain start, the pass-2 CNT+FLUSHA) when a3 != 0, else [q0, the final CNT+FLUSHA);
* run B = [slot + 0x10, the final CNT+FLUSHA) (pass 2 only; empty when no pass-2 moby was drawn: then no
  marker - its first qword is the final CNT+FLUSHA, which must stay).

The shadow slot sits between A and B and is written later by 0x250C70 (verified by disassembly
0x250C70..0x250DB0: with [0x1600BC] == 0 the slot becomes CNT qwc 0; else NEXT to the shadow chain,
whose last tag is a NEXT to **[0x1600C0] + 0x10**, i.e. slot + 0x10 = run B's first qword). So run B's
marker is reached after the shadows, as the pass-2 mobys are now. DmaMobyTextures' NEXT from the slot
DrawMobysSetup reserved returns to that slot + 0x10 = MobyProc's q0 = run A's marker, so the texture
uploads are walked before the marker, as now. Chain overflow (0x256F54 check) ends a pass between mobys,
so a run never holds a partial moby.

Marker (written at MobyProc's exit, over the run's first qword):
```
q[0] = 0x20000000              NEXT, qwc 0
q[1] = run end (chain address) the walker (and a PS2 DMAC) continues there
q[2] = NATIVE_MOBY_MARK        VIF NOP with a magic immediate, e.g. 0x00004D42
q[3] = segment id (< 0x10000)  VIF NOP
```
dma.c, next to lines 131..132:
```
if (!irq && id == 2 && qwc == 0 && q[2] == NATIVE_MOBY_MARK) { native_terrain_flush(); native_moby_tag(q); t = addr; continue; }
```
(`continue` before the TTE send at line 141, like the tie/shrub markers.) Writing the marker at exit
rather than at run start means no back-patching and an all-or-nothing fallback: if anything in the call
could not be recorded (a class packet that does not decode, a template or setup block that is not as
expected, the record ring full), no marker is written and the whole call goes through the chain.

The overwritten qword is q0 (run A: the chain start's MSCAL 0) or the first matrix CNT of pass 2 (run
B). Nothing reads either after MobyProc: the skin driver and the colour patch write only inside holes,
0x250C70 writes only the slot, DrawMobys reads only [0x161280].

### 1.3 Records (ring like native_tie.c's: DSEG 4096 segments, DREC 1 << 17 records of 4 words)

| kind | words | written at (native_mobyproc.c) | drawer does |
|---|---|---|---|
| R_START | chain addr of the 6 constants (sp - 0xD0), setup block addr (R->ref) | after line 239 | MSCAL 0 (VIF start(): TOP = TOPS, flip DBF; native_moby_run(0x0000)); apply the setup codes (§2.2); V4-32 6 qwords -> 0x123 and 0x287 (each preceded by STCYCL 4/4) |
| R_MTX | chain addr of the 4 rows (sp + 16 at line 734), VU addr 0x129 / 0x28D | lines 731..738, 912..918 | STCYCL 4/4; 4 qwords -> VU addr (absolute, no +TOPS) |
| R_PKT | flags (extra pass bit), hole addr (sp + 0x20 / + 0x30), counts word (lw 0xC(s1)), class packet addr + qwc (ld 0(s1)) | lines 740..772, 921..957 | positions, colours (and ST in the extra pass) from the hole; the class packet's codes (§2.4) |
| R_GIF | chain addr of the GIF tag (the qword after the CNT 2 tag), 2 | lines 777..795, 803..813 | uya_gif_packet(that, 2) as PATH2 (the FLUSHA is a no-op on the host) |
| R_MSCAL | pc (0x50 / 0x70) | line 799, line 958 | VIF start() semantics on the mirror; native_moby_run(pc, VU1 mem, TOP) + kicks |

The order of records is the order of the chain pieces, so the GS blocks fall between a packet's unpacks
and its MSCAL (first packet) and after the moby's last MSCAL (restore) exactly as in the chain.

### 1.4 When the draw data is final, and what the walk sees

Frame N: DrawMobys (0x251070) = DrawMobysSetup -> MobyProc (chain with holes, records to t9) ->
DrawMobysCleanUp (0x250FC0): DmaMobyTextures (texture NEXT splice into the reserved slot) ->
ProcessMobyAnimData -> MobyAnimProc (0x256918: per record joint evaluation 0x252018 + skin driver
0x1EEBD8, ch8 into each hole; **final ch8 wait at 0x256990**) -> colour patch 0x2568A8 (overwrites the
colour section of flags & 0x10 mobys' holes) -> shadow splice 0x250C70. The chain is kicked by the frame
function in frame N+1 (the one-frame lag, as for ties and shrubs); frame N+1 builds into the other packet
buffer. So **every hole is final after DrawMobysCleanUp of frame N**, long before the walk, and the walk
reads the same bytes the REF/NEXT tags would have read. The matrices and constants are written by
MobyProc itself into the chain (final at its exit). The class packets are level data, possibly patched
(texture words) - read at walk time like the REF.

What must stay in the chain: the texture splice (before run A), the shadow slot (between the runs), the
two CNT+FLUSHA closers, the MPG/"VU1 data" parts DrawMobysSetup puts before MobyProc's start, and
DrawMobyList's surrounding GS packets. All are outside the runs, so they keep their order relative to the
marker; the GS-state blocks inside a run are replayed in order from R_GIF.

DrawMobyList (0x250F40, a3 = 0) uses the same MobyProc and gets one run per call, with its own chain
start (its own setup/OFFSET), so it is handled identically.

---------------------------------------------------------------------------------------------------
## 2. What VU1 program 13859 / native_moby.c reads, and how the direct path supplies it

### 2.1 The templates are static data in the program image (not built at run time)

Level 0 (build/levels/level_00, gp 0x166D00), gp-relative as MobyProc loads them:

```
0x1608D0 g1 : 10000004 00000000 01000404 6C040000   CNT 4 | STCYCL 4/4 | UNPACK V4-32 n4 addr 0
0x160930 g4 : 30000000 00000000 00000000 69008000   REF   | NOP | UNPACK V3-16  signed +TOPS 0x000
0x160940 g5 : 20000000 00000000 00000000 6E00C061   NEXT  | NOP | UNPACK V4-8 unsigned +TOPS 0x061
0x160950 g6 : 30000000 00000000 00000000 6E00C061   REF   | NOP | UNPACK V4-8 unsigned +TOPS 0x061
0x160960 g7 : 20000000 00000000 00000000 750080C2   NEXT  | NOP | UNPACK V2-16 masked signed +TOPS 0x0C2
0x160970 g8 : 30000000 00000000 00000000 00000000   REF   | NOP | NOP
0x160980 g9 : 10000000 00000000 00000000 14000000   CNT 0 | NOP | MSCAL 0
0x160990 g10: 00000000 00000000 447A0000 00000000   (0, 0, 1000.0, 0): the extra pass's offset
```
Level 3 holds the same values 0x40 lower (0x1608F0..), as the notes' -0x40 gp offsets say. **The
executable's own copy uses gp-0x6450.. (its MobyProc at 0x212658 loads lq -0x6450..-0x6400 at
0x212820..0x212834, g1 at gp-0x64B0)**, i.e. 0x1608B0.. and 0x160850, which hold the same seven qwords.
The executable's 0x160930.. (gp-0x63D0) holds unrelated GIF REGLIST descriptors (D_00160930 etc. of the
HUD code). So the templates come from the level's own image and must be taken through the hook's gp
offsets (R->g_t[k], R->g_unpack, R->g_off), never hard-coded. The constants (0x16CB40), the setup block
(0x1C2800) and the GS qwords (0x1C3240) are zero in the executable file: built at run time (level 0's
image has them: below). **Verify all of them at MobyProc entry** (like native_shrubproc.c's
direct_ok(), line ~285): IDs, VIF command bytes and immediates as above; if any differs, record nothing
for this call.

### 2.2 The program's VIF setup block (REF at a6abs 0x1C2800, 4 qwords; level 0 image)

```
01000404 STCYCL cl 4 wl 4      20000000 STMASK = 0x20202020   31000000 STCOL = 0x1000 x4
03000000 BASE 0                02000164 OFFSET 0x164 (DBF := 0, TOPS := BASE)
05000000 STMOD 0               30000000 STROW = 0, 0, 0, 0
```
Executed after q0's MSCAL 0, so **every MobyProc chain resets the double buffer**: after the start TOPS =
0, DBF = 0, and MobyProc's s4 parity (s4 = 1 at entry, negated before each packet: first packet ->
0x129) matches. Mask 0x20202020: per row, x, y, w from the data, z from STCOL (q = 1.0 in 4.12). The
drawer reads these 4 qwords at walk time and applies them with a 7-command interpreter (or verifies them
at MobyProc time and applies the constants).

### 2.3 VU1 memory the program reads (native_moby.c)

B = TOP - 356 (line 665); everything through Q() masks the address to 10 bits (line 57).

| VU address | content | read at | supplied by |
|---|---|---|---|
| 0x123 / 0x287 (B+647) | A+D GIF tag (x = fog minimum, f32) | 674, 223, 229 | R_START: constants q0 |
| 0x124 / 0x288 (B+648) | strip GIF tag (x = projection factor; PRIM, PRE) | 669, 237, 518, 718 | q1 |
| 0x125 / 0x289 (B+649) | guard band scale | 672 | q2 |
| 0x126 / 0x28A (B+650) | clip scale | 671 | q3 |
| 0x127 / 0x28B (B+651) | unclip scale | 673 | q4 |
| 0x128 / 0x28C (B+652) | screen offset (w = fog offset) | 670 | q5 |
| 0x129..0x12C / 0x28D..0x290 (B+653..656) | object-to-screen rows vf15..vf18 | 668 | R_MTX (first two packets of each moby; packets 3.. reuse the buffer's) |
| TOP + vtx - 1 | x y z (s32 from s16), w | 161 (x y z only) | R_PKT positions |
| TOP + 0x60 + vtx | r g b a (one word each) | 163 | R_PKT colours |
| TOP + 0xC1 + vtx | s t q (4.12), w | 162, 419 | class packet (main) / hole ST (extra pass) |
| TOP + 0x12D | header: x culled winding, y texture table offset, z first switch vertex | 248..251 | class packet |
| TOP + 0x12E.. | strip words (4 per qword) | 253, 274, 395 | class packet (the program also writes them: 258, 560) |
| TOP + 0x12D + header.y .. | texture table: A+D qwords, w = next switch vertex | 196, 222, 234, 259, 284 | class packet |
| 976, 978..993 | scratch (written before read) | 676, 406..447 | program |
| 712..839, 840..967 | output buffers (words not written stay) | kick() 119 | program |

Start: **0x0050** (clipping: per-vertex fog, culling, clip) when the bounding sphere crosses the guard
band, **0x0070** otherwise (s3 0xA / 0xE, mobyproc notes §2.2); the extra pass always 0x0050; 0x0000
once at the chain start (resets S.vf27, line 657). TOP = the VIF's TOPS when the packet's unpacks run
(vif.c start(), line 157: TOP := TOPS, then TOPS flips between BASE and BASE+OFFSET).

### 2.4 Unpack formats the drawer reproduces (bit for bit with vif.c)

n = byte 3 of the packet entry's +0xC word; pq = byte 1 (position qwords), cq = byte 2 (colour qwords);
the skin driver's hole is [colours: cq qwords][positions: pq qwords] (main), [ST cq][positions pq]
[colours cq] (extra). (mobyskin_notes.md §2.2 names byte 1 / byte 2 the other way round; the tag code,
lines 754..770, settles it: T2 REF qwc = byte 1 at hole + byte2*16.)

* **Positions, UNPACK 0x69 V3-16 signed, +TOPS 0, cl = wl = 4** (no mask, STMOD 0): for i < n,
  `VU[TOP+i] = { s16 h[3i], s16 h[3i+1], s16 h[3i+2], w }`, h = the hole at hole + cq*16. vif.c
  (unpack_plain, vn 2 -> `default:` case, line 87; elem() line 66) takes **w = the next halfword
  h[3i+3]**, sign-extended, and returns 0 when that halfword lies past the command's data words
  nwords = (48n + 31) / 32: so the last vertex's w = h[3n] when n is odd (the hole's padding halfword:
  the skin driver's junk w or zero), 0 when n is even.
* **Colours, UNPACK 0x6E V4-8 unsigned, +TOPS 0x061**: `VU[TOP+0x61+i] = { b[4i], b[4i+1], b[4i+2],
  b[4i+3] }` zero-extended, from the hole's start (main) or hole + (cq+pq)*16 (extra).
* **Extra-pass ST, UNPACK 0x75 V2-16 masked signed, +TOPS 0x0C2**: vn = 1 gives {s, t, s, t}; the mask
  (row r = min(i % wl, 3), 0x20 per row) keeps x, y, w from the data and z from STCOL[r]:
  `VU[TOP+0xC2+i] = { s16 s, s16 t, 0x1000, s16 t }`, from the hole's start. (Mask path of vif.c,
  lines 113..150.)
* **Matrix / constants, V4-32, absolute address** (the template's imm has no FLG bit): plain copies;
  each tag's VIF0 word is STCYCL 4/4, which resets cl/wl on the mirror (it matters if a class packet
  changed them).
* **The class packet** (REF via 0x3EC0; qwc = upper word of ld 0(s1), addr = its low word): its own VIF
  codes. Contents are not visible statically (level data loaded at run time; no RAM dump on disk), so
  decode them generally, once per packet, like native_shrub.c's vif_class() / native_shrub_direct_class()
  (lines ~380..470): a decoder over STCYCL, STMASK, STROW, STCOL, STMOD, NOP, FLUSH*, MARK, and UNPACK
  of every format **with** masks (unlike the shrub one, which rejects them) and +TOPS or absolute
  addresses, giving an image of (TOP-relative or absolute) qword writes with a per-lane write mask, plus
  `src[]` for V4-32 qwords (re-read from the level data at draw time: the texture A+D words may be
  patched per frame) and `vol[]` for the bytes they come from (excluded from the change check). Any
  other command (MSCAL, MPG, DIRECT, BASE/OFFSET, ITOP, a packet longer than its qwc...) -> the packet
  is undecodable -> the call records nothing. The decode depends on the VIF state at entry
  (cl, wl, mask, row, col, mode); key the cache by (address, qwc, entry-state hash) and store the exit
  state (a class packet that changes STMASK/STCYCL affects the extra pass's ST and the next packets).
  Per draw: compare the raw bytes (non-volatile) with the cached copy, re-decode on a change, then
  memcpy each written qword (lanes per mask; V4-32 qwords from src).

### 2.5 MSCAL bookkeeping on the mirror

R_MSCAL: `TOP = TOPS; if (DBF) { TOPS = BASE; DBF = 0; } else { TOPS = (BASE + OFFSET) & 0x3FF; DBF = 1; }
ITOP = ITOPS;` (vif.c start(), lines 154..161), then the run. R_START is MSCAL 0 first, then the setup
codes (OFFSET resets DBF), then the constant unpacks: the same order as q0, q1, q2.. in the chain.

Needed from vif.c (new, ~25 lines): `void uya_vif1_state(VifState *out)` and `void
uya_vif1_set_state(const VifState *in)` over vif[1]'s cl, wl, mode, mask, row[4], col[4], base, ofst,
tops, top, itops, itop, dbf (Vif, line 24). The drawer reads them at segment start and writes them back
at its end, so whatever runs on VIF1 after the run sees the state the skipped codes would have left.

### 2.6 Running the program

Factor vu.c lines 241..252 (native_vu1's 13859 branch) into `int uya_moby_vu1_run(u8 *mem, u32 pc,
u32 top)` (the first-call native_moby_reset(), native_moby_run(), the kicks with uya_gs_src "PATH1
native moby") and call it from both the VU dispatch and the drawer. The drawer passes top explicitly
(the mirror's TOP) instead of vu_vif_top(1). The drawer must only be enabled when native mobys are on
(UYA_NATIVE != 0), since a marker skips the chain and nothing would run the translated program.

---------------------------------------------------------------------------------------------------
## 3. Carried state and stale reads

### 3.1 native_moby.c's carried state

`S` (line 42): vf27[4] (output buffer rotation; reset by the 0x0000 start, line 657), vf12w (ST w at a
switch in slot 3, culling parity in clip_tri, lines 453/614), e29/e30 (the last two screen edges, the
0x0050 culling test of the next run's first entries, line 569), clip (the clip-flag shift register,
line 575). The mesh caches (mmt, mh_k) and counters are not draw state. All of it is only touched
inside native_moby_run, which the drawer calls **at the same chain position, in the same order, with the
same pc and TOP, on the same memory** as the VIF path does now. Mixed frames (a call that fell back to
the chain, DrawMobyList chains, pass 1 / pass 2) stay in order because both paths run at walk time in
chain order. So S evolves identically; nothing extra is needed beyond not calling native_moby_run
anywhere else (in particular the check mode must not run it a second time: §4.3).

The VIF double buffer is not native_moby state: it is the VIF mirror (§2.5), reset by every chain start.

### 3.2 Stale VU memory: yes, the moby program has such reads

* **The three padding strip words before the switch to vertex 0** (front_all, lines 271..291): their
  vertices go through front(); the first one, V[end-3], feeds the edge S.e29/S.e30 of the last drawn
  entry (line 577) and so the culling of the next 0x0050 run. A padding word may name a vertex beyond
  this packet's n: its position/colour/ST are whatever an earlier packet - or another program - left
  in that buffer.
* **Output buffer words the program does not write** (the 4th word of the A+D tag at line 223, the ST w
  at line 589) go out in the kicked GIF packets (kick(), line 119): GS-ignored fields, but they are
  read from whatever VU1 memory held.
* The strip-word writes (lines 258, 560) and the 976..993 scratch modify VU memory later runs may see.
* Positions' w lanes (from the hole's next halfword, §2.4) are never read by native_moby but are VU
  memory other programs could read later.

**Decision (as for shrubs, native_shrub.c lines 17..20 and 554..557): keep VU1 memory (0x1100C000) as
the program's working memory while other VU1 programs still share it.** The drawer writes exactly the
words (and lanes) the VIF would, including the w lanes above; it does not keep a private image (a
private image is exact only while nothing else writes there, and ties/shrubs/clip programs do).

---------------------------------------------------------------------------------------------------
## 4. Implementation plan

### Step 1: plumbing (no behaviour change) - ~80 lines
* vif.c: VifState get/set (§2.5).
* vu.c: `uya_moby_vu1_run()` factored out of native_vu1(); a check hook `native_mobyseg_chk(pc, mem,
  top)` called before native_moby_run when the check mode is on.
* native_moby.h: `#define NATIVE_MOBY_MARK 0x00004D42u` (any unused NOP immediate; tie 0x4954, shrub
  0x5253, terrain 0x4D4B are taken), the seg API.
* dma.c: the marker line (§1.2) and, for the check mode, one line for the check marker (step 4).

### Step 2: records in MobyProc - ~120 lines (native_mobyproc.c)
* Switch UYA_NMOBYDIRECT (0 off, 1 check, 2 direct; default 0 until step 6), only with mode_ == 2 and
  native mobys on.
* At entry: verify templates (SPR 0x3E80..0x3ED0, 0x3F40, 0x3EE0 after they are written, line ~224),
  the GS qwords (0x3F00..0x3F30) and the setup block (R->ref), §2.1/2.2. Open run A
  (`native_mobyseg_begin()`), remember q0's address.
* Record at the sites in §1.3 (chain addresses, counts, class packet pointer). Call the class decoder
  (step 3) for each class packet as it is recorded; on failure mark the call "no direct".
* Pass 2 (line 1101): close run A at the emit_close address; open run B at slot + 0x10 (after line 1104).
* Exit (finish, ~line 1160): if direct and nothing failed, write the NEXT markers for the non-empty runs;
  else drop the segments.
* Statistics: calls direct / fallen back (why), packets, decoder cache hits.

### Step 3: the drawer - ~450..550 lines (new host/src/native/native_mobyseg.c)
* Record ring and segments (copy native_tie.c lines 496..507).
* Class packet decoder + cache (§2.4): ~200 lines, modelled on native_shrub.c vif_class()/
  native_shrub_direct_class() plus masks, absolute addresses, exit state.
* seg_draw(id): read the VifState; per record: R_START (MSCAL 0 + setup + constants), R_MTX, R_PKT (the
  three hole unpacks of §2.4 + the class image), R_GIF (uya_gif_packet), R_MSCAL (mirror flip +
  uya_moby_vu1_run); write the VifState back. Addresses through the same masking as dma.c's phys()
  (chain pointers may be in a mirror segment).
* native_moby_tag(q): validate, seg_draw(q[3]); return 1.

### Step 4: check mode, UYA_NMOBYDIRECT=1 (like UYA_SHRUBDCHK) - ~150 lines
* MobyProc records as in direct mode but **does not write the NEXT marker**: it writes a check marker
  into the **address word of the run's first qword**, which is always a CNT tag (q0 at run A; the
  matrix CNT from template 0x3F40 at run B): `q[1] = 0x4D440000 | seg id`. A CNT's address field is
  ignored by the DMAC and by dma.c (used only for REF/NEXT/CALL, lines 143..153), and the VIF never sees
  it, so the chain still draws as now.
* dma.c: `if (id == 1 && (q[1] >> 16) == 0x4D44) native_mobyseg_chk_begin(q[1] & 0xFFFF);` then the
  tag is processed normally.
* chk_begin: run the segment's records **without writing VU memory and without running the program**:
  produce, for each R_MSCAL, the expected (pc, TOP, post-flip TOPS/DBF, VIF cl/wl/mask/row/col/mode) and
  the list of (VU address, words, lane mask) written since the previous MSCAL (constants, matrix,
  positions incl. w, colours, ST, class image). Queue them.
* native_mobyseg_chk(pc, mem, top) (from vu.c, before native_moby_run, when image 13859 runs from the
  chain): pop the head; compare pc, top, the vif[1] state, and every listed lane against VU1 memory;
  log the first 10 differences (address, VU words, record words, which record), count packets/differ,
  log every 20000 packets. Also count R_GIF records between MSCALs against the PATH2 DIRECT packets seen
  (a counter bumped in vif.c's DIRECT for the moby chain), to check the GS-state order.
* Equal inputs to the same function with the same memory elsewhere imply equal output, so this checks the
  whole path without running the program twice (running it twice would advance S.vf27/e29/e30/clip/
  vf12w and the mesh cache: §3.1).

### Step 5: direct mode on, pixel check
* UYA_NMOBYDIRECT=2 and the software renderer: frames vs UYA_NMOBYDIRECT=0, **0 differing pixels**
  (the existing frame-compare scripts), several levels (pass 2 + shadows: a level with deferred mobys;
  colour patch (flags & 0x10); extra pass (moby+0x73); flags 0x208 GS blocks; DrawMobyList users: the
  pause/menu screens; chain overflow).
* Then the GPU renderer (mm_draw / mm_draw_clip path is the same native_moby_run, so it should match).

### Step 6: default on; later
* Make 2 the default once checks are clean across levels.
* Follow-up (not in this design): the skin driver could hand positions/colours straight to the drawer
  (skipping the hole), and the class image could feed native_moby's mesh layer without VU memory once no
  other program reads what mobys leave in VU1 memory.

### Sizes
native_mobyproc.c +120; native_mobyseg.c (new) ~500..600 incl. check; vif.c +25; vu.c +30; dma.c +3;
native_moby.h +15. About 700..800 lines.

---------------------------------------------------------------------------------------------------
## 5. Risks

1. **Class packet VIF codes are unknown until run time.** The decoder must handle masks, STMOD and
   absolute/+TOPS addresses and carry the VIF state across packets; anything else -> fall back for the
   call. Check mode will show what real packets contain (log the command mix on the first decode).
2. **Patched class-packet words** (texture A+D possibly rewritten per frame): re-read V4-32 qwords from
   their source at draw time; if non-V4-32 bytes change, the raw compare re-decodes (cost only).
3. **Exact VIF semantics of the hole unpacks**: the V3 w lane (next halfword; 0 / padding at the end by
   n's parity), V2 masked {s, t, col, t}, sign/zero extension. Copy vif.c's rules, not the PS2 manual's;
   check mode compares every lane.
4. **VIF state after a skipped run**: written back from the mirror; a bug there would only show in what
   follows the moby chain (tie/shrub chains set their own BASE/OFFSET, but STMASK/STCYCL/ROW carry).
   Check mode compares the mirror against vif[1] at every MSCAL.
5. **Marker failure modes**: an evicted/unknown segment skips the run (draws missing, not corrupted);
   size the ring for >1 frame of calls (DrawMobys + DrawMobyList calls, 2 runs each). A chain that is
   walked twice re-draws from the same records (fine while they are in the ring).
6. **S state ordering**: any native_moby_run call outside chain order (e.g. a check mode that runs the
   program) breaks bit-exactness; keep exactly one call per MSCAL.
7. **The shadow slot / pass 2**: run B's marker relies on 0x250C70 returning to slot + 0x10 (verified
   for level 0; check other levels' copies have the same code - they are byte-identical copies per the
   catalogue) and on the slot being written whenever g14 != 0 (as now).
8. **Template / setup verification per level**: data lives at per-level addresses (level 3: -0x40 gp,
   +0x80 abs); always from the hook's relocations.
9. **Interaction with MobyProc's own check mode** (UYA_NMOBYPROC=1 compares the chain with the
   translated routine): direct/check markers only with UYA_NMOBYPROC=2.
10. **Program image**: the drawer assumes VU1 runs 13859 for this chain (the MPG/data parts before the
   chain start stay in the chain); assert uya_vu_cur_image()/find_image agrees in check mode.
