# Piece 7 research: what still goes through the DMA/VIF layer, and how to make it direct

Sources: the decomp (src/game/vuchain.c, draw.c, partproc.c, drawquad.c, shadowproc.c, initonce.c,
src/overlays/shared/vendor_*.c), config/overlays/functions.tsv, the executable's and level 3's bytes
(disasm.py; .DVP.ovlytab for the microcode's load addresses; scans for `lui 0x1500` / vcallms words),
the microcode (decoded with tools/vu_isa.py), and host/src (dma.c, vif.c, vu.c, native_*.c).
Nothing was built or run: counts per send are the ones measured earlier (overnight_2026-10-18.md);
everything marked **verify** needs a run.

Address conventions: "exe" = the executable's copy. Every routine named here also has a copy in each
level (kind `exe` in functions.tsv, 19 places); level 3 ("L03") addresses are given. HOST_HOOKS by the
exe name covers every level's copy, as with TieProc. The executable's game data (.lit 0x15F000 and up:
.data, .bss) is **replaced by each level's** (level 3: lit 0x15F000, bss 0x161F80, data 0x166180, text
0x1E4600), so data addresses (camera 0x187040/0x187180, view 0x18CE00, constant blocks) move per level
and must come from the hook's absolute-address list. The microcode and its upload packets live in the
resident core (.vutext 0x100080-0x112310) and are **the same in every level**.

Microprogram images (tools/vu_recomp.py group id = image name), with their upload packets in .vutext
(the qword before the packet holds the packet's qwc; the packet is FLUSH + MPG):

| image | VU | blocks (VU addr, size) | qwc word / packet | uploaded by |
|---|---|---|---|---|
| 221571 particles | 1 | 0x0000 0x6C8 | 0x101070 / 0x101080 (0x6D qw) | PartProc |
| 57843 strips | 1 | 0x0000 0x1260 | 0x10E800 / 0x10E810 (0x128) | func_001F7868 (VU1_addDataRef), func_L02_002A52D0 |
| 13507 tie | 1 | 0x0000 0x1EC8 | (TieProc consts, R->mpg_qwc) | TieProc's chain start |
| 224979 tie clip | 1 | 0x0000 0x3760 | 0x10AD10 / 0x10AD20 | TieProc's clip part |
| 56467 shrub | 1 | 0x0000 0xB60 | 0x101750 / 0x101760 (0xB7) | ShrubProc's chain start |
| 912339 shrub clip | 1 | 0x0000 0x1268 | 0x1022D0 / 0x1022E0 (0x128) | ShrubProc's clip part |
| 55907 / 903379 tfrag | 1 | | 0x103560 / 0x107010 | TfragProc |
| 13859 moby | 1 | | 0x10FA90 / 0x10FAA0 (0x287) | DrawMobys (VU1_addDataRef) |
| 28259 trig | 0 | 0x0C80 0x2F0 (+0x0000 0x30 stub, never uploaded) | VIF0 chain 0x10E4C0 (END, MPG 0x5E at 0xC80) | initonce (VU0_loadMicroProgram, once) |
| 436083 lighting | 0 | 0x0000 0x580 | VIF0 chain 0x100AE0 (END, MPG 0xB0 at 0) | end of DrawFrame (func_001F3D78), every frame |
| 104691 skin | 0 | 0x0000 0xA40 | VIF0 chain 0x100080 | DrawMobysSetup |

## 1. Effects 221571 and 57843

### 1.1 Image 221571: PartProc builds all of it

**Builder: PartProc func_00218B10** (exe, hand-written asm, 3060 bytes; L03 0x256C08; one call per
frame from DrawFrame func_001F3D78 (L03 0x1F2220, jal at 0x1F25C8), after `VU1_addGSregister(8, 5)`;
DrawFrame then sets D_0015F704 = 8, the "resident VU1 program" id). UpdateParts func_00218A80 (L03
0x256B78) is the per-particle update (jalr through the table at 0x1CE100 + type*4); it touches no VIF.
PartProc uses VU0 **macro mode only** (no vcallms), DMA ch9 (toSPR) and ch8 (fromSPR), and waits with
`bc0f` loops.

What PartProc does (exe addresses; gp offsets are the same in the level copy):
1. Pass 1 (0x218B10-0x218ED4): loads the camera (vf28 = cam+0x140 position, vf29..31 rows, vf20..23
   the view-projection matrix scaled by 1024 in xyz rows, from 0x187040; vf24/26/27 from the view
   block 0x18CE00 +0x1C0/+0xB0/+0x1E0; vf25 = gp-0x64E0). DMAs the particle array (pointer gp-0x6B54,
   count gp-0x6B4C, **64 bytes a particle**) to SPR 0x0000/0x0800 in 32-particle halves (ch9), culls
   each (behind/near/far by its fade nibbles at +9, frustum by VCLIPW of two points, flag tests on
   byte +1), and z-buckets survivors into 1024 linked lists (heads SPR 0x3000.., nodes from SPR
   0x1000; "special" ones listed at SPR 0x3800). Writes **back into the particle array**: byte +1
   (sort/visibility bits) for the special list.
2. Builds a back-to-front index list at SPR 0x0000 (0x218F10-0x218F64).
3. Loads two tables to SPR (ch9): 0x40 qw from 0x1CDD00 -> SPR 0x3000 (texture cache, 8 bytes per
   texture index), 0x40 qw from 0x165E00 -> SPR 0x3400 (a cosine table, 1.0, 0.99970...).
4. Chain start, at the chain pointer gp-0x5D00 (D_00161000):
   - REF qwc *(u16*)0x101070 -> 0x101080: FLUSH, **MPG 217 instructions at 0** (image 221571);
   - CNT 6 (template gp-0x6540: STCYCL 4/4, UNPACK V4-32 6 at VU 0): rows 0..3 = the matrix at
     0x187040+0x40.. (rows x, y, z scaled by 1024.0, translation as is), 4 = *(0x18CE00+0x210) (vf25:
     Q numerator), 5 = *(0x18CE00+0x1A0) (vf26: screen offset);
   - REF 14 qw -> 0x1DEFB0 (template gp-0x6530: STCYCL, UNPACK V4-32 10 at VU 6): VU 6 = vf27 (1.0,
     1.0625), 7..10 the corner ST qwords, 11..13 the GIF tags (quad, line, streak), 14/15 the A+D
     templates (ALPHA_1 0x42, TEX0_1 0x06); then VIF **MSCALF 0x0000** (setup), **BASE 0, OFFSET
     0x200**, DIRECT 3 (GIF tag + A+D TEX1_1 = 0x0000FF90_00000120, CLAMP_1 = 5).
5. Pass 2 (0x21915C-0x219618): per visible particle (back to front) a record written into SPR
   0x1000/0x1800 (two 2 KB halves = 128 qwords each); each half is a **CNT block**: header from vf17
   (gp-0x6520: CNT qwc patched = n-1, VIF NOP + UNPACK V4-32 +TOPS at 0, num patched = n-2), the
   records, trailer vf18 (gp-0x6510: NOP NOP NOP **MSCALF 0x00A0**); the first record's qword 2 .w
   (block +0x3C) gets the record count in qwords. A full half goes to the chain by **ch8 fromSPR**
   (MADR = chain pointer, SADR = half), then the chain pointer advances. The last block is closed the
   same way, then vf19 (gp-0x6500: CNT 0, FLUSHA).
6. Texture paging (0x219704, a `bgezal` subroutine): when a particle's texture (byte +2, SPR 0x3000
   cache) is not resident this frame, a slot is appended to the GIF paging table (16 bytes at
   0x18D140 + 16*n, count gp-0x77A8 = D_0015F558, VRAM cursor gp-0x7D8C) and the TEX0 is cached in
   SPR. DoGifPaging later turns those slots into the texture uploads (VIF1 DIRECT IMAGE packets).
   Also writes particle byte +1 |= 0x20 for drawn particles.

Record layouts (what PartProc writes; native_fx221571.c's header has the program's reading):
- mode 0 screen quad (4 qw): +0 xyz = position - camera (vf1), w 0; +0x10 RGBA as 4 words (bytes of
  particle +4), alpha word replaced by the distance fade ((fade * byte +7) >> 12); +0x20 xy = size *
  (cos, sin) from the SPR 0x3400 table (index byte +8 and +8+0x300/4) * Q (Q = size.w*vf25.w / depth),
  z = byte +3 (ALPHA_1 data), w = 0 (count in the first record); +0x30 TEX0 data (64 bits).
- mode 1 world quad (4 qw): as mode 0, w = 1, +0x20/+0x24 floats size*table*f4.
- line (4 qw): w = -1, +0x20 the second point, +0x30 the second colour.
- streak (6 qw): w = -2, +0x10 second point, +0x20 half width (normalised cross product), colours,
  TEX0 at +0x50.

The program (native_fx221571.c, already native, word for word): 0x0000 setup loads VU 0..15 into
vf16..31 and sets vi14 = 1023, vi15 = 512, vi8 = -2; 0x00A0 per block flips vi15 between 0 and 512
(the program double-buffers by itself; the VIF's BASE 0 / OFFSET 0x200 TOPS toggling agrees), walks
the records and builds one GIF packet at vi15 + 128 (A+D ALPHA/TEX0, RGBA, ST/XYZ x4), kicked once.

**Feasibility of a direct version: good, on the TieProc pattern.** One builder, one call per frame,
fixed templates. Plan:
- hook func_00218B10 (`"func_00218B10": "uya_partproc"`; gp offsets -0x6B54, -0x6B4C, -0x6B44,
  -0x64E0, -0x6540..-0x64F0, -0x5D00, -0x77A8, -0x7D8C; absolute 0x187040, 0x18CE00, 0x1CDD00,
  0x165E00, 0x18D140, 0x1DEFB0 (via the template REF), 0x101070/0x101080);
- port it word for word (VU0 macro results and flags left in rc_vu0, SPR contents, particle byte
  writes, paging slots, gp variables, DMA register writes), with UYA_NPARTPROC 0/1/2 (1: check
  mode, run both from the same state and compare chain bytes, SPR, particle array, paging table);
- direct: records = the setup constants (VU 0..15 + the DIRECT 3 GS writes) and each block's records;
  the chain gets one marker CNT (NATIVE_PART_MARK) where the REFs/blocks were; at the marker: send
  the DIRECT 3 through native_gif, write VU 0..15 and each block into **real VU1 memory** at the TOPS
  the VIF would have used (keep the VIF mirror: BASE 0 / OFFSET 0x200 / DBF toggle per start), run
  native_fx221571 on rc_vu1 (0x0000 once, 0x00A0 per block), as vu.c does now.
- Nothing on the EE reads VU1 memory or the chain back (scan: no EE load from 0x1100C000; the only
  EE<->VU memory traffic is VU0 macro vsqi in the skin driver). But the **VU1 register file** carries
  over: 57843 reads vf13.w and vf6.w, which it never writes (221571's setup leaves vf12.zw/vf13.zw
  multiplied by 0), so the native runs must stay in chain order on rc_vu1.

### 1.2 Image 57843: a strip service with many callers

Setup: **func_001F7868** (exe, 488 bytes; L03 0x1F5FD8): if D_0015F704 != 7, VU1_addDataRef(0x10E810,
*(u16*)0x10E800) (the MPG) and D_0015F704 = 7; then one CNT: FLUSH, STCYCL 4/4, UNPACK V4-32 12 at VU
932 (0x6C0C43A4): 932..935 = (camera-0x100) x M, 936..939 = (camera-0x80) x M (M = scale 1024,
translation -camera*1024, element [14] + D_0015F448: z bias), 940 = GIF tag (0x8000, 0x303EC000,
0x412; w = view+0x210, the Q numerator), 941 = view+0x190 (vf29), 942 = view+0x1A0 (vf30), 943 =
(view+0x22C, view+0x228, 0, 0) fog clamp; then BASE 0, OFFSET 0x1D2, **MSCALF 0x0000**; then
func_00234FA8 (REF D_0013D010, DIRECT 11: GS register reset). (camera = D_00187180, view = D_0018CE00.)

Strip writers (scan for `lui 0x1500` = MSCALF, all 19 levels; template-built MSCALs would be missed):
- **func_L00_001FDE48** (shared, all levels; L03 0x1F7730): draw one strip; a0 = n vertices, a1 = xyz
  (12 bytes each), a2 = colours (4 bytes), a3 = ST (8 bytes), t0 bit 0 = clipped. Writes a CNT from
  the template at L03 0x1C43A0: STCYCL 4/4, UNPACK 1 at TOPS+0 = the strip's GIF tag (NLOOP |= n),
  UNPACK 1 at 940, **STCOL (1,1,1,1)**, STCYCL 3/1, STMOD 0; then three UNPACKs (templates at L03
  0x160F40/0x160F60/0x160F80, num |= n): **STMASK 0xA0A0A0A0 + V2-32 at TOPS+1** (s, t, then COL
  1.0, 1.0), V4-8 at TOPS+2 (colour), **STMASK 0x80808080 + V3-32 at TOPS+3** (x, y, z, COL 1.0);
  the arrays copied in; MSCALF 0x60 (plain) or 0x70 (clipped) + FLUSH; patches the CNT's qwc.
- **func_001F8B6C** (exe, asm, 1612 bytes; L03 0x1F7924): the D_0015F578 strip list (lists at
  0x15F57C/0x15F580), called with the setup from func_001F7B70 (L03 0x1F62E0) in DrawFrame between
  DrawShrubs and DrawMobys, inside SetupGifPaging/DoGifPaging; header template at 0x1E0280; same
  MSCALF 0x60/0x70 + FLUSH ending. It also reads SPR 0x2400..0x2500 (visibility samples) for alpha.
- level/shared builders: func_L02_002A52D0 (7 levels; uploads 57843 itself), func_L02_00250A58 (7),
  func_L01_002BA150 (6), func_L01_0021F9C8 (4), func_L09_002C2B08, func_L06_00216B38. Moby draw
  callbacks (AddDrawCallback, run in ExecuteDrawCallbacks*) call func_001F7868 and then these, e.g.
  func_L00_002B4918 (L03 0x2A6298) and func_L00_002DACC0 (L03 0x2D3EE8); level 12's func_L12_002BD3D0.

In level 3 the ~5 starts per send are setups (0x0000) and strips (0x0060/0x0070): which callers
produce them is **verify** (UYA_STARTLOG=57843,n,from lists the chain tags before each start).

The program (native_fx57843.c, native, word for word): transforms the strip in place in VU memory,
kicks it; clipped triangles go through a Sutherland-Hodgman clipper into fans at 998. Its quirks
matter for a direct version: it reads **three vertices ahead and one past the strip's end** (stale VU
memory), the clipped strip's second-vertex test reads the **clip flags left by the previous run**, the
plain strip's last Q numerator is vf29.w for even N, and the STQ w words come from vf13.w / vf6.w of
whatever ran before.

**Feasibility:** the input format is trivial (GIF tag + 3 qw/vertex: (s,t,1,1), (r,g,b,a) words,
(x,y,z,1)), so a hook can write VU memory itself. But there are 3 all-level producers plus 6
level-specific ones. Recommended: hook func_L00_001FDE48, func_001F8B6C and func_001F7868 (records:
the setup's 12 qwords, each strip's tag + arrays + clipped flag; marker CNT in the chain; at the
marker write VU memory at TOPS exactly as the UNPACKs would, including the masked lanes, keep the
TOPS mirror (BASE 0 / OFFSET 0x1D2), run native_fx57843 on rc_vu1). The level-specific builders can
stay in the chain until a census shows they matter; VU memory must stay the real VU1 memory (stale
reads past the end).

## 2. The tie and shrub starts that still come from the chain

All three come from the **chain starts that TieProc and ShrubProc still emit in direct mode**, not
from DrawTies/DrawShrubs themselves (DrawTies_1 func_00236BE0, L03 0x2837B8; DrawShrubs func_00229E50,
L03 0x276DE0, only call the Proc routines).

- **13507 at 0x0040** (2 a send): TieProc's start, native_tieproc.c emit_start(): REF to the MPG +
  6 qwords of constants (REF 9 qw: UNPACK V4-32 9 at VU 966..974 (0x6C0903C6), STMOD 0, **STROW**
  (row from consts+0x30: the colour offset STMOD 1 uses), BASE 0, OFFSET 0x2C, STCYCL 4/4, MSCAL
  0x40). The program at 0x40 only loads vf21 = VU 969, vf23.xz = 968, vf22 = 970 and ends. **The host
  does not run it** (vu.c native_vu1: pc 0x40 returns 1); native_tie_draw reads 966..975 itself. Two
  a send = two TieProc runs a frame (**verify**: main pass + a second list, or the boot/menu chain).
- **224979 at 0x0040** (1.3 a send): the clip part's start (emit_start with mpg2 and 9 consts: VU
  966..975 and the same STROW/BASE/OFFSET), only when the clip list has entries. The program's 0x40
  loads vf21 = 971, vf23.xz = 970, vf22 = 972, **clears VU 976, 983, 990, 997, 1004, 1011 and writes
  their z (isw) with 1751, 1753, ...**: GIF strip-tag templates in VU memory. The host runs it
  natively (native_tieclip_run, registers seeded from and written back to rc_vu1: vi1..15 except
  9/10/11/13, vf21..23, vf29..31, vf1.w, vf25.w). Later clip runs read those registers; **the shrub
  program reads what it leaves in VU memory** (native_shrub.c: "a tie clip packet's strip tag").
- **56467 at 0x0000** (1 a send): ShrubProc's start (emit_mpg + copyq(consts, 5): UNPACK of VU 0..2
  and MSCAL 0). The program loads vf31/vf30/vf29 = VU 0/1/2, vi1 = 856, vi8 = 520, vi11 = 120 and
  stores vf17; native_shrub_run(0x0000) copies VU 0..2 into its state (A+D tag, fog scale/offset,
  fog clamp) and the output buffers 856/520. In direct mode the records also have R_START (the same
  constants) and the clip part's R_CSTART, so this chain start is redundant, but it is what writes VU
  0..2 in real VU1 memory, which later programs (57843/221571 reading past their data, the shrub
  quirk) could see.

To make them direct: move each start into the records the Proc already emits (shrub: R_START exists;
tie: add one), so the marker writes the constants into VU1 memory at their addresses and runs the
start natively; the MPG REF can go (see 4.2). Keep real VU1 memory as the working memory
(moby_direct_design.md §3.2 and native_shrub.c decided the same), in chain order, so the stale reads
see the same words.

## 3. VU0 image 28259: a trig library called from EE code

Started only by `vcallms` from EE code (scan of the exe and of all 19 levels; VIF0 carries nothing but
the MPG chains of VU0_loadMicroProgram func_002347F0 (L03 0x28E1F0), which is a ch0 chain: CHCR
0x145, TADR = the packet). 28259's 0xC80 block is uploaded once (initonce, VU0_loadMicroProgram(
&D_0010E4C0)) and survives the per-frame 436083 (0x000-0x580) and 104691 (0x000-0xA40) uploads.

Entry points (from the microcode):
- **0x0C80 cos**: vf1.x += pi/2 (I = 1.5708), then falls into sin.
- **0x0C90 sin**: vf5.x = pi, vf6.x = -pi; x = max(min(x, pi - x), -pi - x) (folds to
  [-pi/2, pi/2]); vf2 = x^2, vf3 = x^3, vf4 = x^5, vf5 = x^7, vf6 = x^9;
  vf1.x = x + x^3*(-0.166667) + x^5*0.00833303 + x^7*(-0.000198074) + x^9*2.60189e-06 (ACC chain:
  mula by vf0.w, then madda with I, E bit at 0x0D08, the last madd in the delay slot). In: vf1.x;
  out: vf1.x. Clobbers vf2..vf6 (x lanes), ACC.x, I, MAC/status flags.
- **0x0D18 Euler rotation**: vf31.xyz = vf1.xyz (angles); vf20/21/22 = identity rows, vf22 by MR32
  of vf0; FMOR reads the MAC zero flags of that add, so an axis whose angle is exactly 0 is
  skipped. For x: sin/cos by the subroutine at 0x0E20 (both at once: sin(a) to vf1.x, sin(a+pi/2) to
  vf2.x, same polynomial and folding, using vf11..16 for the second), Rx = rows (1,0,0), (0,c,s),
  (0,-s,c). For y (vf1.x = vf31.y): Ry = (c,0,-s), (0,1,0), (s,0,c), applied by the subroutine at
  0x0F28 (vf20..22 = rows of vf20..22 times vf24..26: R = R x Ry). For z (vf31.z): Rz = (c,s,0),
  (-s,c,0), (0,0,1), applied the same way. End 0x0E10 (E bit) with vf23 = vf0 = (0,0,0,1).
  Out: vf20..vf23 = Rx * Ry * Rz (row-vector convention). Clobbers vf1, vf2..6, vf11..16, vf24..26,
  vf31, vi10..13, vi15 (bal link), I, ACC, flags.

Callers (EE side; wrapper pattern `qmtc2.ni vf1; vcallms; qmfc2.i` so the EE waits for the result):
- FastCos func_001F9F90 (0xC80; L03 0x1F90B0) and FastSin func_001F9FA8 (0xC90; L03 0x1F90C8);
- func_001FA1F8 (3 rows out) / func_001FA218 (4 rows) (0xD18; L03 0x1F9318 / 0x1F9338): matrix from
  Euler angles (mobyutil.c, pause.c);
- moby matrix update func_0020ED48 (exe, 0xD18 inline, then VU0 macro code; called from pause.c,
  transition.c) and its level counterpart func_L00_00251E30 (L03 0x240908);
- shadow volumes func_00229098 (L03 0x276028) and func_002291E8 (L03 0x276178; 5 sin/cos pairs);
- sky func_0022D028 (exe), camera shake (camera.c).
Which of these make the ~1280 runs a send is **verify**; per-moby 0xD18 and the shadow sin/cos are
the likely bulk. A count per call site needs the EE pc at rc_vcallms (recomp.py emits
`rc_vcallms(c, 0xC80)`; add the instruction address as an argument under a testing switch).

Native version (simpler than native_mobyskin.c: no coroutine, no parking):
- in vu.c vu_run_(0, pc): for image 28259 at 0xC80 / 0xC90 / 0xD18 call native code that does the
  ops in the program's order with the VU arithmetic (round toward zero, each product and sum clamped
  to +-MAX, as native_fx221571.c's add/mul/madd), and writes every register the program writes
  (vf1..6, vf11..16, vf20..26, vf31, ACC, I, vi10..13, vi15, MAC/status as vu_settle leaves them,
  TPC) so EE code reading leftovers sees the same; keep the "zero angle skips the axis" test
  bit-exact (+0 and -0 both set the zero flag);
- later, optionally, recomp.py can call it directly instead of rc_vcallms at the known sites;
- check mode: run the translated image on a copy of rc_vu0 and compare the whole RcVu0 (UYA_NVU0TRIG
  0/1/2); fuzz offline over angles in and beyond [-3pi/2, 3pi/2], +-0, denormals, inf/NaN.

Also: **436083** (VU0, lighting) is uploaded every frame and started by LightShrubs func_0022B8F8 (L03
0x278888: vcallms 0x0000 / 0x0160 per entry of the shrub light list, nothing when the list is empty)
and LightTies func_00238688 (0x2C0/0x420), which PAL does not call (D_0015EE80 != 0 takes
func_00236BB0). None showed in the level 3 counts; **verify** on levels with lit shrubs before
dropping VU0 micro memory.

## 4. The VU1 chain handler, MPG and the rest of the chain

### 4.1 func_00235118 (DMA channel 1 handler; L03 0x28ED18; installed by DMAC_VIF1_Enable func_00235018)

Hand-written; reads D1_CHCR (a1) and D1_TADR:
- TADR == 0, or CHCR MOD (bits 2-3) == 0 (normal mode): return, no restart;
- CHCR TAG ID == 7 (END): `D_00160FE0 &= ~0x1F` (gp-0x5D20), return, no restart;
- else, if TAG bit 25 is set (the tag's bits 16-25 = 0x201/0x202/0x204/0x208): clear bit 1/2/3/4
  (0x2/0x4/0x8/0x10) of D_00160FE0; other values clear nothing;
- then restart: CHCR | 0x100 (STR), sync, ei, return 0.

The stage tags come from **Vif1ChainCmd func_00235290** (CNT qwc 0 with IRQ, tag | 0x90000000, VIF
NOPs): DrawFrame writes 0x02010000 after DrawTfrag, 0x02020000 after DrawTies, 0x02040000 after
DrawShrubs, 0x02080000 after DrawMobys (also transition.c, space.c's other draw path).
VU1_sendChain func_002349B8 sets D_00160FE0 |= 0x1F, closes the chain with END, sets CHCR TTE|TIE
(0xC0) and sends; on overflow it clears D_00160FE0 instead.

Waiters, **VU1_syncChain func_00234AC8(mask)**: spins on func_001F9988 (the
busy-wait that polls DMA on the host) while D_00160FE0 & mask; **after 100000 tries it prints and
calls ResetVideoPipeline**. Users: DrawFrame's tail waits 2 / 4 / 8 / 0x10 before LightTfrags +
PatchTfragGifs, func_00236BB0 + copy_render_buffer_pair (PAL ties), LightShrubs + PatchShrubGifs,
PatchMobyGifs (they patch data the chain in flight still references); the main loops wait 1 before
sceGsSyncV and VU1_sendChain/VU1_swapChain (space.c, bmain.c, FadeToBlack func_001F4E08, pause.c,
transition.c, vendor overlays).

On the host the walk is synchronous inside the send, so every bit is clear when it returns. **A stub
must keep clearing them**, either by calling the game's handler at each IRQ tag with CHCR's TAG field
and TADR set (as dma.c does now: no level addresses needed) or by clearing the low 5 bits of the
level's D_00160FE0 at the end of the walk; otherwise every syncChain times out and resets the video
pipeline. It must also: clear STR and QWC at the end, keep TIE semantics (stop at an IRQ tag until the
handler restarts it), keep walking CNT/NEXT/REF/REFS/CALL/RET/END for the markers and the DIRECT
data, and accept chains with no markers at all (boot screens, FadeToBlack, menus).

### 4.2 MPG

MPG is only how the host learns which image is in micro memory (vu.c vu_mpg + find_image: copy,
memcmp of every overlapping block). The game's own bookkeeping is EE-side (D_0015F704: 6 moby, 7
57843, 8 particles; -1 at the frame's start), so nothing on the EE depends on the upload. Every
upload is a REF to a fixed .vutext packet (table above), so MPG can be replaced by "REF source
address -> image, VU address range": keep a per-block residency table (an upload of 104691 over
0x000-0xA40 must still leave 28259's 0xC80 block resident) and drop the copy and the compare. Keep
the real copy under a testing switch for UYA_NATIVE=0 (translated programs need the bytes only as a
fallback; vu_images has its own copy). Unknown REF sources (none known) must fall back to the copy.
Once every start is native and comes from a marker, the MPG REFs can be left out of the chain by the
hooks (the records name the program).

### 4.3 Other VIF state in the chain

Commands still produced (from the templates read above): STCYCL (4/4 everywhere; 3/1 in strips),
STMOD 0/1 (tie colours use offset mode with **STROW** from TieProc's constants), **STMASK + STCOL**
(57843 strips), BASE/OFFSET (tie 0/0x2C, particles 0/0x200, strips 0/0x1D2), MSCAL/MSCALF, FLUSH /
FLUSHE / FLUSHA, NOP, DIRECT (texture uploads, VU1_addGSregister 0x50000002 packets, ResetGsRegisters
D_0013D010, VU1_texFlush, VU1_gsRegsNormal, the 221571 setup's DIRECT 3), MARK in the markers' TTE
words. Programs read TOP (vu_vif_top) and their own vi registers; native_mobyseg.c reads/sets the VIF
mirror (uya_vif1_get_state / set_state). State carries across parts and across sends (VIF registers
are never reset per chain), so the stub must keep a mirror of BASE/OFFSET/TOPS/TOP/DBF/ITOP/STCYCL/
STMOD/ROW/COL/MASK and advance it for every left-out command (as the moby direct records do), until no
UNPACK remains anywhere. The EE never reads VIF registers back except the DMA ones (CHCR STR in
waits, D_STAT).

## 5. Recommended order, risks, verification

0. **Census first** (no behaviour change): extend UYA_VIFSTATS to attribute each UNPACK/MPG/MSCAL to
   the chain part it came from (the last REF source / template seen, or the nearest marker), log MPG
   sources by address, count vcallms per EE call site and VU0 image (436083 included). Run levels 3,
   13 and a few with lit shrubs and 57843 users (levels 1, 2, 6, 9, 12). Gives the real producer list.
1. **VU0 28259 native** (independent of the chain, small): native trig in vu.c with check mode
   (whole RcVu0 compared, every run) and an offline fuzzer; then 0 px on 19 levels walking. Risk:
   VU float semantics and the MAC zero flag test at 0xD18; registers EE code reads later.
2. **MPG by address** (small): residency table from REF sources; check mode compares find_image's
   answer with the table's at every start for whole runs. Risk: partial overwrites, the never-uploaded
   28259 stub, boot screens (exe copies).
3. **Tie/shrub/tie-clip chain starts into the records**: VU 966..975 / 0..2 constants written at
   the marker, 224979's 0x40 run there, STROW/BASE/OFFSET applied to the mirror. Check: VU1 memory
   image after each part compared between chain and records (the existing UYA_SHRUBDCHK style), 0 px
   software on 19 levels standing and walking, GL replay A/B. Risk: the shrub quirk (stale tie-clip
   strip tags), VIF mirror drift (TOPS parity), the GL-only clip-as-mesh path (cdir).
4. **PartProc native + direct (221571)**: largest single EE port left in this piece (3060 bytes of
   asm, VU0 macro, two DMA channels, bucket sort, paging). UYA_NPARTPROC 0/1/2 with check mode
   comparing chain bytes, SPR 0x0000-0x3FFF, the particle array, the paging table and gp words, and
   rc_vu0 registers; then direct with the marker. Risks: the paging slots (texture uploads in the same
   frame), the per-block count at +0x3C, the walk's exact termination in the program, particle byte
   writes (game state), per-level addresses, VU1 register carry-over into 57843.
5. **57843 producers**: hook func_001F7868, func_L00_001FDE48, func_001F8B6C; leave the level-specific
   builders in the chain (or add them if the census says so). Check mode: VU1 memory at each start
   and the packets compared with the chain path. Risk: stale reads past the strip end and the clip
   flags between runs (real VU1 memory, chain order); STMASK/STCOL lanes.
6. **The stub**: dma.c keeps the walker, markers, IRQ/handler and DIRECT -> native_gif; vif.c shrinks
   to DIRECT/DIRECTHL, NOPs, FLUSHes, MARK and the mirror, with the full interpreter kept behind a
   switch (UYA_VIFFULL=1) and a one-time log of any command that still needs it (per producer). Done
   when the census shows 0 UNPACK, 0 MPG, 0 MSCAL in gameplay on all levels. Verify: 0 px software on
   19 levels standing and walking against UYA_VIFFULL=1, GL replay A/B on levels 0, 3, 5, 13, the boot
   screens and FadeToBlack (chains with no markers), and the syncChain timeout log never firing.

Cross-cutting risks: native runs must happen at the marker, in chain order, on rc_vu1 and real VU1
memory (register and memory carry-over between programs); the one-frame lag is kept by drawing
records only when the chain is walked; level copies need the level's own data addresses (the hook's
lists), while microcode addresses are constant.
