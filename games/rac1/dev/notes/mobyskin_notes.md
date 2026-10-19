# Moby skinning (func_L00_001EEBD8 + VU0 image 104691): notes for a native version

Sources: level-0 disassembly (dev/tests/disasm.py) of 0x1EEBD8..0x1EFF50 (scratchpad skin.txt,
skin2.txt), 0x252018 (eval.txt), DrawMobysCleanUp / ProcessMobyAnimData / MobyAnimProc / 0x2568A8
(callers.txt); the translation build/linux/gen/l00/rc_funcs_003.c lines 183..1389
(`rc00_region_func_L00_001EEBD8`); the VU0 translation build/linux/gen/vup_104691_vu0.c (773
lines) and a pair listing made with `tools/vu_recomp.py /tmp/vudis/gen ~/solana-dev/kit/decomp
--dis /tmp/vudis/dis` (scratchpad vu104691.txt, 328 pairs); host/src/gfx/vu.c, host/include/vu.h;
host/src/native/native_moby.c, native_mobyproc.c; decomp src/game/mobyfunc.c. Level 0 (PAL)
addresses throughout. Nothing in either tree was edited or built; the only thing written outside
this file was a throwaway translation/listing under WSL /tmp/vudis.

---------------------------------------------------------------------------------------------------
## 0. Key facts up front

* **The skin driver is one region of two catalogue entries**: `func_L00_001EEBD8` (3436 bytes, 859
  instructions, 0x1EEBD8..0x1EF944) and `func_L00_001EF944` (1548 bytes, 387 instructions,
  0x1EF944..0x1EFF50); recomp.py merges them (`rc00_region_func_L00_001EEBD8(c, entry)`, entries
  0x1EEBD8 / 0x1EF944). **Whole thing 4984 bytes = 1246 instructions.** Only 0x1EEBD8 is entered
  from outside. The catalogue name is shared by all 19 levels (functions.tsv: 00:001EEBD8,
  01:00210758, 02:001FFA40, 03:001E84C0, ... 18:001F7840). **It has no gp-relative and no lui/addiu
  data references**: everything is SPR (0x7000xxxx), DMA registers (0x1000Dxxx) and its two
  arguments. A native version needs no per-level data.
* **It is an EE <-> VU0 coroutine, not "DMA + one vcallms".** One `vcallms 0` (0x1EEF04) per
  *packet* starts a software-pipelined VU0 loop that runs alongside the EE for every vertex of the
  packet. The EE feeds each vertex by writing VU0 registers vf1..vf8 (`qmtc2.ni`, `lqc2`), releases
  the VU0 with `ctc2.ni $zero, vi1` / `vi2` (alternating), and reads the results of an earlier
  vertex from VU0 registers (`qmfc2.ni` of vf11/vf12, vf9/vf10). There is no `.i` interlock in this
  path: synchronisation is a handshake on vi1/vi2 plus a sentinel (-1) in the output register that
  the EE polls. The extra (environment-map) pass at 0x1EFBE4 is the conventional kind: one
  `vcallmsr vi27` per vertex and `qmfc2.i` (the only interlocked instruction, 0x1EFE64).
* **How the project runs it now**: tools/vu_recomp.py turns each VU0 wait loop
  (`ibeq viN, vi0, +3 / b -3`) into a park (`v->tpc = ...; v->susp = 1; goto done;`), 8 of them;
  `rc_vcallms` (host/src/gfx/vu.c:541) runs the image from the start address until it parks or
  ends; every translated `ctc2` to a VI register calls `rc_vu0_resume()` (vu.c:550), which runs the
  parked program on from `tpc` to its next park. So each `ctc2 vi1/vi2 = 0` runs exactly one
  half-iteration (one vertex) of the VU0 loop synchronously, and `vu_settle` at every park lands
  any pending Q. Under these semantics the EE never sees the sentinel, the spin code 0x1EF944..
  0x1EFBE0 (168 instructions) never runs, and the result is fully deterministic.
* **Output**: per packet, the skinned vertices as two VIF unpack payloads in one block -
  [n colour words (RGBA bytes) padded to a qword][n positions (3 x s16, 6 bytes) padded to a qword]
  - built in SPR 0x3000/0x3400 and sent with **DMA ch8 (fromSPR)** to the chain hole MobyProc
  reserved (the hole's address is in the record list). VU1 13859 receives them through the REF
  tags MobyProc wrote in front of the hole: positions at TOP+0x000+(i-1), colours at TOP+0x060+i
  (native_moby.c's layout). **Lighting is done here** (VU0), not on VU1: VU1 only transforms,
  projects and fogs.
* VU0 image 104691 is **328 instruction pairs** (0x000..0xA40): the skin loop 0x000..0x880 (273
  pairs, E bit at 0x878) and the extra-pass kernel 0x888..0xA40 (55 pairs, starts 0x888 / 0x908 /
  0x968, E bit at 0xA30). It uses no MAC/status/clip flags (no flag op in the translation), one
  `loi` (16.0), DIV once (0x010) and RSQRT per vertex.

---------------------------------------------------------------------------------------------------
## 1. From DrawMobysCleanUp to every write into the moby chain

DrawMobys 0x251070 (func_0020E2B0) = DrawMobysSetup 0x250E88 (VU0_loadMicroProgram(0x100080):
**image 104691 into VU0 micro memory**; reserves the texture tag slot [0x16008C]; chain limit
[0x161288] = [0x15F6DC] - 0x10000; record pointer [0x160094] = [0x15F6D8]) -> MobyProc 0x2569F8
(native_mobyproc.c: chain with holes + records) -> **DrawMobysCleanUp 0x250FC0 (func_0020E200)**:

| step | address (L0) | kind | what it writes into the moby chain |
|---|---|---|---|
| profiling marker | 0x1F7120 / 0x1F7118 | EE | nothing (empty) |
| DmaMobyTextures | 0x250A00 (func_0020DC40) | EE, translated | the tag slot DrawMobysSetup reserved ([0x16008C]) becomes a NEXT to the texture uploads it appends at the packet pointer (func_00212258 = L0 0x2565F8, which reads SPR 0x3A00 class distances), ending with a NEXT back to slot+0x10 (the moby chain proper). Only if [0x16A118] and its sibling flag are set. |
| if [0x16A118] (D_0018A3B0[10]): ProcessMobyAnimData | 0x250DB8 (func_0020DFF8) | EE | func_118D80(0); FastMemCopy (0x1FF0D8) **0x166180 -> SPR 0x3800, 0x800 bytes** (the normal table, see §2.3); MobyAnimProc(a0 = [0x15F6D8], a1 = [0x15F6DC]) |
| MobyAnimProc | 0x256918 (func_00212578) | EE | saves s0..s7, gp, sp, fp, ra at 0x19BB00 (they are all clobbered below; a0/a1 are kept in $f0/$f1); per record while word 0 != 0: **func_L00_00252018(a0, a1)** then **func_L00_001EEBD8(a0, a1)**, which returns the next record; at the end waits for **ch8** (0x1000D000) idle, restores, returns. |
| joint evaluation | 0x252018 (3284 bytes) | EE macro mode only (no vcallms) | nothing in the chain; joint matrices into SPR 0x0000.. (§4) |
| **skin driver** | 0x1EEBD8 (+0x1EF944) | EE + **VU0 micro mode** (`vcallms 0` 0x1EEF04; `vcallmsr vi27` 0x1EFE80) | **every hole**: per packet one ch8 DMA SPR -> hole (0x1EF8CC..0x1EF8E4 normal pass, 0x1EFED0..0x1EFEE4 extra pass) |
| if [0x1600B8] (g11) != 0: colour patch | 0x2568A8 (func_00212508) | EE | overwrites colour words inside holes (below) |
| profiling marker | 0x1F7118 | EE | nothing |
| if flag && [0x1600C0] (g14) != 0: shadow splice | 0x250C70 (func_0020DEB0) | EE | the qword at g14 (pass 2's reserved slot) becomes CNT (no shadows, [0x1600BC] == 0) or NEXT to the shadow chain built by shadowproc func_00228A58 (L0 0x286D40) and func_00228860, which NEXTs back. |

**Colour patch 0x2568A8** (EE, 28 instructions): list at 0x1AC580 (MobyProc a8abs), size in bytes
at [0x1600B8]; 16-byte entries {w0 colour word (moby+0x90, byte 3 = rec+0xBC), w1 pointer to the
class packet-table entries after the first N, w2 pointer into that record's list *in main memory*
(t9 + N*8 + 0xC0), w3 packet count}. Per packet: hole = lw 4(w2 entry); n = lbu 0xF(packet entry)
(byte 3 of the entry's +0xC word = the unpack num = vertex count); **n words at the hole's start
(= the whole colour section) := w0**. It needs the skin output already in the hole, so it relies on
MobyAnimProc's final ch8 wait (0x256990).

So the only VU0-micro-mode code on the path is the skin driver's; everything else is EE code that
stays translated. (Another level function, func_L00_00251E30 region, has `vcallms 0xD18` - a
different image's start, not on this path; FastSin/FastCos 0x1F9F90/0x1F9FA8 also use VU0 micro
mode.)

---------------------------------------------------------------------------------------------------
## 2. The skin driver, block by block

Arguments: a0 = the record (main memory, as MobyProc's ch8 wrote it), a1 = [0x15F6DC] (a main
memory area 64 KB above the chain limit; used as an 8 KB, 8 KB-aligned table of 512 x 16-byte
"shared vertex" entries; the joint evaluation also builds a small DMA chain at a1+0x1FC0, i.e. in
entries 508..511). Returns v0 = the next record. Clobbers every GPR (gp, sp, fp, s0..s7 used as
temporaries; gp is loaded with 0x10010000 and used as a base at 0x1EF57C / 0x1EF5EC - **a HOST_HOOKS
gp-relative scan would wrongly collect these two loads**), ra saved in SPR 0x1FF0 (sq/lq).

SPR locals: 0x1F00 list cursor, 0x1F04 a1, 0x1F08 packet buffer (0x2000 / 0x2800), 0x1F0C output
buffer (0x3000 / 0x3400), 0x1F10 output qwc, 0x1F14 next record, 0x1F18 record+0x14 (extra pass),
0x1FF0 ra.

### 2.1 Setup 0x1EEBD8..0x1EED10

* [0x1F18] = lw 0x14(a0); [0x1F08] = 0x70002000; [0x1F0C] = 0x70003000; [0x1F00] = 0x70001C80.
* **DMA ch9 (toSPR, 0x1000D400)**: MADR = a0+0x40, QWC = (count>>1)+9 (count = lhu 0(a0) packets),
  SADR = 0x1C00, CHCR = 0x100. So SPR 0x1C00.. = record+0x40..: lights/colours at 0x1C00..0x1C7F,
  the list (8 bytes per packet, 0 terminated) from 0x1C80. Before it: sd 0 -> SPR 0x1C78+count*8
  (the last entry); 0x1EEC50 polls that dword until non-zero (arrival). [0x1F14] =
  a0+0x40+QWC*16 = the next record.
* First packet: t0 = [0x1C80] (entry 0, w0 = source<<4 | qwc): **ch9 MADR = (w0>>8)<<4, QWC = w0 &
  0xFF, SADR = 0x2000, CHCR 0x100**.
* Light block into VU0: vf1 = SPR 0x1C60 (rec+0xA0, the two "light words" +0xA0/+0xA4, the rest 0);
  vf25..27 = rec+0x40/0x50/0x60 (the three light direction rows: lanes = light 0, light 1, point
  light, w); vf28 = rec+0x70 (light 0 colour), vf29 = rec+0x80 (light 1), vf30 = rec+0x90 (point
  colour), vf31 = rec+0xB0 (the moby colour as 65536.0 + byte, alpha faded). vf1 = 0 - |vf1|.
  `ctc2 vi1 = 0xFB`, `vf2 = qmtc2 v0` with v0 = -1 (**low dword -1; vf2.zw = v0's stale upper
  half**), `ctc2 vi10 = 0xFE`; vsqi vf1, vf25, vf26, vf27, vf2 -> **VU0 mem 0xFB..0xFF**.
  (vf28..vf31 stay in VU0 registers for the microprogram.) t4 = {0, 0x0707} and t5 = 0x0E000E00
  are byte/halfword patterns used to turn index bytes into SPR addresses.

### 2.2 Per packet 0x1EED14..0x1EF900 (loop on the list, `bnez t0` at 0x1EF8FC back to 0x1EED34)

* Wait ch9 idle (0x1EED14 loop). t0 = next entry's w0 (lw 8(a3)); t9 = [0x1F08] (this packet's
  buffer); if t0 != 0: **prefetch the next packet: ch9 MADR = (t0>>8)<<4, QWC = t0&0xFF, SADR =
  t9^0x800**, [0x1F08] = t9^0x800 (double buffer).
* Packet header at t9: +0 matrix uploads (t2), **+4 / +8 / +0xC vertex counts -> `ctc2` vi3, vi4,
  vi5** (2-joint, 3-joint, 1-joint vertices; each ctc2 calls rc_vu0_resume, a no-op while VU0 is
  idle), +0x10 shared-vertex copy count, +0x14 output vertex count n (t3), +0x18 vertex data
  offset (at = t9 + ..), +0x1C per-vertex colour-scale table offset (v0 = t9 + ..). Output layout:
  a0 = [0x1F0C], v1 = a0 + round16(n*4) (positions), gp = v1 + round16(n*6) (end); **sq 0 at v1-0x10
  and gp-0x10** (the padding qwords), [0x1F10] = (gp - a0) >> 4.
* Initial matrix uploads (0x1EEE1C loop, t2 times): bytes {SPR matrix index j, VU0 address} at
  t9+0x20+2k: `ctc2 vi1 = addr`; lqc2 vf1..vf4 = SPR j*64 + 0..0x30; 4 x vsqi -> VU0 mem.
* Pipeline prologue 0x1EEE5C..0x1EF260: lq t0, t1, t2 (vertices 0..2, one qword each); for vertex 0
  build vf1..vf4 (below) and vf5..vf8 = SPR matrix (h0>>9)*64; **`vcallms 0` (0x1EEF04)**; then
  per vertex: build the next vertex's vf1..vf4 + lqc2 vf5..vf8, `ctc2 vi1/vi2 = 0`, `qmfc2`
  results, `bltz` sentinel check. 4 vertices per loop iteration (A, B, A, B halves), `beq v0, t8`
  (0x1EF264) on the colour-scale pointer; tail 0x1EF504..0x1EF7B8 for the last (n-3)&3 vertices
  (fp) and the drain resumes.
* Per vertex, EE -> VU0 (`qmtc2.ni`/`lqc2`), from the vertex qword (bytes b0..b15, halfwords h0..h7):
  * vf1 = (s32)h4, h5, h6, h7 (pextuh + psraw 16): **h5, h6, h7 = position x, y, z (s16)**; h4 is
    junk in vf1.x.
  * vf2 = {table[b8], table[b9]} (two 8-byte entries of the SPR 0x3800 table, via pextub/pextlh
    with t4/t5, psllw 3, ld, pcpyld): **b8, b9 = normal azimuth / elevation indices**.
  * vf3 = (b0, b1, b2, b3), vf4 = (b4, b5, b6, b7) as words (pextlb/pextub). Meaning (from the
    microprogram): h0 bits 0..8 = shared-vertex slot, bits 9..15 = SPR matrix to upload (vf5..vf8);
    1-joint: b2 = VU0 address of its matrix, b3 = VU0 slot the upload goes to; 2-joint: b2, b3 =
    the two matrices, b4, b5 = weights (/256), b6 = upload slot, b7 = slot where the blended matrix
    is stored; 3-joint: b2, b3, b1 & 0xFE (vi10 mask) = the three matrices, b4..b6 weights, b7 the
    blend slot (no upload).
  * vf5..vf8 = SPR (h0 >> 9 & 0x7F) * 64 + 0..0x30 (lqc2), stored by VU0 into its memory at the
    upload slot before it reads its own matrices: VU0 memory 0x00..0xFA is a **matrix cache whose
    schedule is baked into the vertex data** (uploads and blended matrices are reused by later
    vertices).
* VU0 -> EE: A-half outputs vf11 (colour floats) and vf12 (position ints), B-half vf9 / vf10. Each
  half first loads the sentinel (VU0 mem 0xFF = -1) into its colour register (0x050 `lq vf11, 0xFF`,
  0x128 `lq vf9`) and writes the result at 0x0C8 / 0x1A0; the EE's `bltz` on the qmfc2'd low dword
  (sign of lane y) jumps to the spin stubs at 0x1EF944.. which re-read until `bgez` and jump back.
  The pipeline depth is 2 halves: the values read after half k+2 belong to the vertex fed at half
  k; the first two reads (0x1EF004, 0x1EF0A4) are only checked, never stored.
* Results, per pair of vertices (0x1EF39C.. pattern): colours: s7/s6 = ppach of two colour
  registers (low halfwords of the float bits), **pmulth by the per-vertex colour-scale bytes
  (pextlb of the table at v0), pmfhl.sh (saturate to s16), psrah 7, ppacb** -> RGBA bytes; sq to a0
  (4 vertices per qword). Positions: ppach of the ftoi0 results (low 16 bits, wrapping) -> x y z
  (+ a junk w halfword), stored 6 bytes apart with sdr/sdl into v1 (each vertex's 8-byte store
  writes the junk w halfword, which the next vertex overwrites; the last one's lands in the
  zeroed padding when it fits inside the section). Every skinned vertex also goes to the shared
  table: **sd position -> a1 + (word0 & 0x1FF)*16, sw colour -> +8**.
* 0x1EF7BC..0x1EF860 shared-vertex copies (header +0x10 count): halfword list after the upload
  pairs; each names an a1 entry (pextlh with a1>>13, psrlw 3); its position (sdr/sdl, 6 bytes) and
  colour word are appended to the two sections. So a packet's vertex list = the skinned vertices in
  processing order (2-joint, 3-joint, 1-joint) followed by copies of vertices skinned earlier.
* 0x1EF864..0x1EF8F8: wait ch9 if it was busy, wait ch8 idle, **ch8 (fromSPR, 0x1000D000): QWC =
  [0x1F10], MADR = entry w1 (the hole), SADR = [0x1F0C], CHCR = 0x100**; [0x1F0C] ^= 0x400; cursor
  += 8.
* 0x1EF904: wait ch8 idle; v0 = [0x1F14]; `bnez [0x1F18]` -> extra pass 0x1EFBE4; else `jr ra`.

So: DMA in = ch9 (record tail -> 0x1C00, each packet's skin data -> 0x2000/0x2800, prefetched),
DMA out = ch8 (0x3000/0x3400 -> hole). The hole content is **unpack-ready VIF payload** (no VIF
codes, no DMA tags): the REF tags MobyProc wrote in front of it carry the UNPACK codes (num = byte
3 of the packet entry's +0xC word) and point into it (colours qwc = byte 1, positions qwc = byte
2).

### 2.3 VU0 memory and registers the driver sets up

| VU0 mem | content |
|---|---|
| 0x00..0xFA | matrix cache (4 qwords per matrix; packet start uploads, per-vertex uploads, blends) |
| 0xFB | 0 - abs(rec+0xA0) (per-lane "back light" factor k: lanes light 0, light 1, ..) |
| 0xFC, 0xFD, 0xFE | light direction rows (rec+0x40, +0x50, +0x60, object space) |
| 0xFF | sentinel {0xFFFFFFFF, 0xFFFFFFFF, stale v0.hi words} |

Registers: vf25..vf27 = the light rows (also reloaded from 0xFC..0xFE by the 2/3-joint loops),
vf28..vf30 light colours, vf31 base colour, vi3/vi4/vi5 counts, vi10 = 0xFE, vf1..vf8 per vertex,
vi1/vi2 handshake. SPR 0x3800 table (0x166180, 256 x 8 bytes, two floats each): normal =
(A0*B0, A1*B0, B1) with A = table[b8], B = table[b9] (a spherical-coordinate encoding).

### 2.4 Extra pass 0x1EFBE4..0x1EFF4C (record+0x14 != 0: the second, environment-mapped draw)

* ch9: the block after the record (4-qword turned matrix + its own list; QWC = record+0x16) ->
  SPR 0x1C00; gp = that block's end (the return value). vf28..vf30 *= 0.25; vf31.xyz = 65536.875,
  vf31.w = 65537.0 (0x47800070 / 0x47800080); vf22..vf24 = the turned matrix (0x1C00..0x1C20).
* Per list entry: packet -> SPR 0x2000/0x2800 by ch9 (sd 0 sentinel polled at the end), header +0
  vertex count, +4/+8/+0xC offsets of the position, colour and ST sections from 0x3000 (+0xC also
  the qwc). Per vertex (16 bytes): h0..h2 position, h3 bytes = normal indices (vf8 via the 0x3800
  table), b8..b10 SPR joint indices (vf10..vf21 = up to three matrices, lqc2 directly), b11 = joint
  count + 1 (-> start 0x968 / 0x908 / 0x888 via `ctc2 vi27 = 0x12D / 0x121 / 0x111`), b12..b14
  weights (vf9, psllw 4). **`qmfc2.i a2, vf1` (0x1EFE64) then `vcallmsr vi27` (0x1EFE80)**: each
  call returns the previous vertex's results (the first is skipped, one extra call at the end).
  Out: position 3 x sh (6 bytes) -> a0, colour ppach/ppacb word -> a1 with **byte 3 = rec+0x14's low
  byte (the fade)**, ST = psraw 1 / ppach (2 x s16) -> v1. ch8 SADR 0x3000 -> entry w1, then waits.
* MobyProc's extra-pass holes are three sections (ST, positions, colours: a1, a0, a1 qwords) to
  match.

---------------------------------------------------------------------------------------------------
## 3. VU0 image 104691

### 3.1 What it computes (skin loop, per vertex; A and B halves are the same code with vi1/vf11/vf12
vs vi2/vf9/vf10)

```
matrix:   1 joint  R = mem[b2]                          (after mem[b3] := vf5..vf8)
          2 joints R = w0*mem[b2] + w1*mem[b3], stored to mem[b7]   (w = itof12(b4,b5) * 16.0 = b/256)
          3 joints R = w0*mem[b2] + w1*mem[b3] + w2*mem[b1&0xFE], stored to mem[b7]
position: p' = R0*x + R1*y + R2*z + R3      (ACC chain in that order, itof0 inputs)  -> ftoi0
normal:   n0 = (A0*B0, A1*B0, B1);  n = R0*n0.x + R1*n0.y + R2*n0.z  (not normalised)
          Q = 1/sqrt(n.x^2 + n.y^2 + n.z^2)     (vmul, then add.x y, add.x z; RSQRT)
light:    d = L0*n.x + L1*n.y + L2*n.z          (vf25..vf27: one lane per light)
          i = max(d, d * (-|k|))                 (mem 0xFB)
          c = (C0*i.x + C1*i.y + C2*i.z) * Q + base      (vf28..vf30, vf31)
```
Every product and partial sum through vu_fc (clamp to +-MAX), ACC chains in the translator's
order. `c` is a float near 65536 whose low mantissa bits are the 8.7 fixed-point colour; the EE
does the rest (low halfword x vertex colour byte, saturate, >> 7). Positions stay in model space
(VU1's matrix does object -> screen).

Code map (pairs): 0x000..0x030 entry (vf15/16/19/20 := 1, Q := 0/1 = 0, vi1 := 1, vi11 := 2;
`ibgez vi3-1` -> 0x1E8, `ibne vi4` -> 0x438, else 0x038); **1-joint loop 0x038..0x1E8** (waits
0x0F0 on vi1, 0x1C8 on vi2; `ibeq vi5` -> drain); **2-joint loop 0x1E8..0x438** (waits 0x2F0, 0x418;
`ibeq vi3` -> 0x540 / 0x688, the 3-joint loop's tails); **3-joint loop 0x438..0x6C8** (waits 0x560,
0x6A8; `ibeq vi4` -> 0x0E0 / 0x1B8, the 1-joint loop's tails); **drain 0x6C8..0x878** (two halves
without new input, vi11 2 -> 0, waits 0x780, 0x858; E at 0x878). Per packet the program runs
1 + n + 2 halves (n = vi3+vi4+vi5), and the EE must issue exactly the matching ctc2 releases.
Each loop's first two upper ops (madda vf23*vf13.w / madd vf17 = ACC + vf24) finish the previous
vertex's position, so the pipeline carries across the 2 -> 3 -> 1-joint transitions with
identical stage code.

Extra kernel 0x888 (3 joints) / 0x908 (2) / 0x968 (1): blend vf10..vf21 rows with itof12(vf9)
into vf18..vf21, position = itof0(vf7) through them -> vf1 = ftoi0; normal from vf8 the same way;
vf2 = max(L*n, 0) through vf25..27, then vf2 = vf31*w + vf28*x + vf29*y + vf30*z (colour);
vf3 = turned-matrix (vf22..24) * n, xy + 1, ftoi12 in the E delay slot (0xA38): sphere-map ST.

### 3.2 Quirks

* Uses registers set by the EE and by earlier runs: vf25..vf31, vi10, and in the first half the
  stale ACC / vf23 / vf24 / vf18 (the "vertex -2/-1" pipeline slots: 0x038/0x040 and 0x0D0 read
  them before anything this run wrote them). By the trace in §2.2 those slots' outputs are only
  sentinel-checked, never stored - **to be confirmed by the check mode**, since a stored stale
  value would make the output depend on whatever ran on VU0 before.
* Pair semantics matter: e.g. 0x0C0 upper `mul vf20 = vf19 * vf27` reads vf27 = mem 0xFB while the
  lower `lq vf27 = mem 0xFE` replaces it; 0x0A0 the reverse (loads 0xFB).
* Q: the RSQRT of a vertex (0x0D8 / 0x1B0 ...) is read two halves later (0x0B8 / 0x190); a park
  always lies between, and vu_settle lands Q there, so each read sees the latest settled RSQRT.
  The 13-cycle latency never shows inside a segment (no in-segment read of a pending Q found on
  the paths traced).
* Data-dependent control: the three counts and the per-vertex addresses only; no flag branches.
* The sentinel protocol assumes colour lane y >= 0 (true: base >= 65536, i >= 0). If a result ever
  had lane y negative (NaN/huge input), the translated EE would spin forever in 0x1EF944.. (VU0 is
  parked and never changes vf11).
* Size: 328 pairs; main loop bodies ~25 pairs per half; extra kernel 55 pairs.

### 3.3 Determinism

Yes, bit for bit, under the translator's semantics: every op is an IEEE single op on x86 SSE
(-march=nehalem, no FMA contraction possible) followed by vu_fc; itof0/itof12 = (float)(s32)x *
scale; ftoi0/ftoi12 = truncation with the vu_ftoi saturation; RSQRT = vu_rsqrt (a/sqrt|b|, +-MAX on
0); max = fmaxf; no flags; no timing-dependent reads (sequential coroutine). A native C version
that performs the same ops in the same order (and the same pair read-before-write rules) reproduces
it exactly. Things to carry for a full-state compare: the VU0 memory image (cache slots, 0xFB..0xFF
with the stale v0.hi words), the VU0 registers at exit (vf1..vf31, ACC, Q, I = 16.0, vi1..vi11,
vi27), the junk w halfwords in SPR/hole padding, and the shared-vertex table.

---------------------------------------------------------------------------------------------------
## 4. Joint evaluation func_L00_00252018 (EE, stays translated)

3284 bytes (0x252018..0x252CEC); returns at once when the record's joint count (lh 0x10(a0) =
class byte 8/9 by LOD) is 0. Builds a small DMA chain at a1+0x1FC0 (REF tags 0x30000000|n from the
record's +0x18/+0x20/+0x24/+0x30/+0x34 words: the class's joint data and the current/previous
frame), writes back the cache line, runs ch9 into SPR 0x2000..; per joint: quaternion -> rotation
(0x252A64..), scale (+0x10), translation (+0x20), times the parent's matrix (or identity), into SPR
0x0000 + j*64; then the moby's manipulators (list at record+4 = moby+0x54) scale rows in place;
waits for ch9 bringing the class's inverse bind matrices (record+0x1C) to SPR 0x1C00 (sentinel -1
in the last word) and finally **SPR j*64 := SPR j*64 x invbind[j]** for j < count (vf20..vf23 x
vf12..vf15, 0x252C50..0x252CCC). It keeps a1/s0..s2 in vf28..vf31 (qmtc2/qmfc2 spill).
**For the skin driver it leaves: SPR 0x0000.. = count skinning matrices (4 float rows, row 3 the
translation, model space).** SPR 0x1C00.. is then overwritten by the skin driver's record DMA.
(With count 0, SPR 0x0000.. still holds the previous moby's matrices.)

---------------------------------------------------------------------------------------------------
## 5. What VU1 13859 (native_moby.c) reads from the skin output

Per packet, double buffered at TOP (0x000 / 0x164):
* TOP+0x000+(i-1): vertex i x y z (16-bit) <- the hole's position section (V3-16 unpack, num =
  vertex count).
* TOP+0x060+i: vertex i r g b a, one word each <- the colour section (V4-8 unpack). "The colour as
  it goes out": VU1 does no lighting; after the colour patch the words may be the flat w0.
* TOP+0x0C1+i: s t q (4.12) from the class packet's static data (MobyProc's REF at template
  0x3EC0, packet-table +0 dword) - or, in the extra pass, from the hole's ST section.
* TOP+0x12D header, 0x12E.. strip words: static (class packet).
* 0x123..0x12C per moby: constants from the chain start; matrix rows vf15..vf18 (object to screen,
  MobyProc) uploaded with the first two packets (0x129 / 0x28D). Start 0x0050 (clip) / 0x0070.
The exact UNPACK codes (addresses, signedness, masks) are in MobyProc's tag templates (gp words at
L0 0x160930.. -> SPR 0x3E80..0x3ED0); read them from a RAM dump to pin down the 0x060 vs 0x061
start of the colour unpack. native_moby's mesh path (mm_draw) needs per entry: the strip word, the
transformed position before projection, s t (q = 1), rgba - i.e. the skinned positions/colours,
the class's ST/strip/texture table, and the moby matrix.

---------------------------------------------------------------------------------------------------
## 6. Plan for "no emulation"

### (a) Native skin driver + microprogram, run synchronously

Hook `func_L00_001EEBD8` (one HOST_HOOKS entry covers all 19 levels; hook at entry 0x1EEBD8 only;
no data arguments needed; tell the hook generator to ignore the two gp-based DMA-register loads at
0x1EF57C / 0x1EF5EC). `native_mobyskin.c`:
1. Do the DMAs as memcpy with the same DMA register end states (MADR/QWC/SADR/CHCR of ch8 and ch9)
   and the same SPR contents (0x1C00.., 0x2000/0x2800, 0x3000/0x3400, 0x1F00..0x1F18, 0x1FF0) - as
   tieproc/mobyproc's flush() do.
2. Keep VU0 memory as the real image (VU0_MEM / 0x11004000): write 0xFB..0xFF, the uploads and the
   blends exactly where the program does; read matrices from it (the cache schedule is in the data).
3. Per vertex, straight-line C: matrix (1/2/3 joints, the program's op order), position, normal,
   RSQRT, lights, colour - no pipeline, no vi1/vi2 handshake, no sentinel; then the EE's integer
   colour path (pmulth / pmfhl.sh / psrah / ppacb semantics) and the position byte layout (sdr/sdl
   including the junk w halfword), the shared table writes, the copy list.
4. Extra pass: the three kernels as functions; same per-vertex output.
5. At the end, set the VU0 registers to what the translated pipeline leaves (needed only for the
   check; derive them from the last 2 vertices + drain), and v0.
6. UYA_NMOBYSKIN = 0 translated / 1 check / 2 native, like UYA_NMOBYPROC: native first, put the
   results aside, restore, run the translated region from the same state, compare holes (whole
   chain range), the a1 table (8 KB), SPR 0x0000..0x3FFF, VU0 memory 4 KB, VU0 registers, DMA
   registers, v0. Also a capture of (record, SPR, VU0 state, a1 table) per call for an offline
   harness/fuzzer in dev/vu_harness (the VU1 capture tool does not cover VU0 coroutines).

Size: ~900..1300 lines of C (driver/DMA ~400, kernels ~300, extra pass ~150, check/compare/capture
~300) - about native_mobyproc.c's size. Hard parts:
* the matrix cache in VU0 memory and the vertex byte meanings (b1&0xFE third matrix, b6/b7 slots)
  - get them exactly from vup_104691_vu0.c, not from this summary;
* proving the stale first-half slots (ACC, vf18, vf23, vf24, vf13.x junk) never reach stored output
  (the check mode will tell; if one does, it must be seeded from the real VU0 state);
* exact pair read/write ordering and vu_fc placement per stage (copy expression by expression);
* MMI saturation/sign details of the colour path and the unaligned sdr/sdl position stores;
* the end-of-call VU0 register state for the full compare (pipeline leftovers, Q, I, ACC);
* the shared-vertex table overlapping the joint evaluation's DMA chain at a1+0x1FC0.
Gains: removes ~n+3 VU0 re-entries per packet (switch dispatch, cycle bookkeeping, vu_settle),
~25% of the driver's instructions (the spin stubs are dead), and the micro-mode dependency.

### (b) Direct draw: records + skinned vertices -> native_moby's mesh layer

Once (a) exists, it can also hand each packet's skinned positions/colours to native_moby directly
instead of only writing the hole. Inputs per drawn moby: the record (MobyProc), its chain segment's
per-packet pieces (matrix rows, start 0x50/0x70, GS-state blocks for flags 0x208, the class packet
REF for ST/strip/texture table), the skinned vertices (after the colour patch), and for flags with
a fade the extra pass. Steps: a native_moby entry that takes a packet as arrays (positions,
colours, the static packet's decoded ST/header/strip/texture table, the matrix and constants)
rather than VU1 memory; drive it at kick time from the chain (walk the moby chain's REF/MSCAL
pattern, which native_mobyproc knows) so ordering with textures (DmaMobyTextures), GS state,
shadows (pass-2 mobys after the shadow splice) and the colour patch stays right.
Size: ~500..800 lines + ~200 in native_moby.c. Hard parts: decoding the class packet's own VIF
codes (ST, header, strip) without the VIF; the VU1 double-buffer parity and native_moby's carried
state (S.vf27 output buffer rotation, vf12w, edge/clip state) which affect its GIF output word for
word; the colour patch and extra pass; keeping a packet path for the clipping start (0x0050) where
mm_draw_clip fails; and mobys drawn by DrawMobyList (a3 = 0) in other chains. Recommended order:
(a) with check mode first (keep the chain and VIF as the interface), then (b) behind its own switch.
