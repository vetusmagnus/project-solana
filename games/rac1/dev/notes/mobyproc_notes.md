# MobyProc (func_L00_002569F8): notes for native_mobyproc.c

Sources: disassembly of level 0 (0x2569F8..0x257E18) and level 3 (0x2454D0..0x2468F0) with
dev/tests/disasm.py; build/linux/gen/l00/rc_funcs_052.c (the translation); the level-0 copies of
DrawMobys / DrawMobyList / DrawMobysSetup / DrawMobysCleanUp / ProcessMobyAnimData / MobyAnimProc;
decomp src/game/mobyfunc.c, include/moby.h; host/src/native/native_moby.c (VU1 13859).
All addresses below are level 0 (PAL) unless marked. Nothing in either tree was edited.

---------------------------------------------------------------------------------------------------
## 0. Key facts up front

* **The routine is two catalogue entries.** `func_L00_002569F8` (3664 bytes, 0x2569F8..0x257848) and
  `func_L00_00257848` (1488 bytes, 0x257848..0x257E18). The first jumps into the middle of the
  second (0x257878, 0x2578C8, 0x257AE8, 0x257B58, 0x257B88) and the second jumps back into the
  first (0x256D50, 0x256E80, 0x257008, 0x257500, 0x2576A0, 0x257720), so tools/recomp.py's region
  pass merges them: gen/l00/rc_funcs_052.c has `rc00_region_func_L00_002569F8(c, entry)` with
  entries 0x2569F8 / 0x257848; `rc00_func_L00_002569F8` and `rc00_func_L00_00257848` are
  one-line wrappers. **Whole routine: 0x2569F8..0x257E18 = 0x1420 = 5152 bytes = 1288
  instructions.** Only 0x2569F8 is ever entered from outside (0x257848 is reached only from
  0x257718 inside the routine).
* With `"func_L00_002569F8": "uya_mobyproc"` in HOST_HOOKS, translate() is called with the region
  range, so the hook line is `if (entry == 0x002569F8) { ... uya_mobyproc_enter(...) }` and the
  gp/abs lists are collected over **all 1288 instructions** (both pieces). The name
  func_L00_002569F8 is the catalogue's shared name in all 19 levels (level 3's gen uses it too),
  so one HOST_HOOKS entry covers every level. The single `jr $ra` (0x257840) gets the
  `uya_mobyproc_exit` call.
* Level copies are identical instruction for instruction except `j` targets and the data
  references (diffed level 0 against level 3: 40 differing words, all lui/addiu/gp-offset/j).
* **No jal/jalr. No vcallms / VU0 micro mode. No cfc2/ctc2** (so the translation is generated with
  NEED_VU0_FLAGS false: the MAC/status flags are neither computed nor read; a native version
  should not touch rc_vu0 flags either). No computed jumps (only `jr $ra`). No self-modifying
  code. VU0 micro mode is used by the *skin driver* (func_L00_001EEBD8, `vcallms 0` at 0x1EEF04),
  which runs later from DrawMobysCleanUp, not by MobyProc.
* Calling convention: `MobyProc(a0 = first moby, a1 = anim-record write pointer (t9),
  a2 = moby count (-1: until the 0xFF sentinel), a3 = shadow/deferral pass enable)`, returns
  `v0 = t9_end + 0x10`.

---------------------------------------------------------------------------------------------------
## 1. Address, size, hook arguments

| level | piece 1 (3664 B) | piece 2 (1488 B) |
|---|---|---|
| 00 | 002569F8 | 00257848 |
| 01 | 0026B750 | 0026C5A0 |
| 02 | 00259008 | 00259E58 |
| 03 | 002454D0 | 00246320 |
| 04 | 002496B0 | 0024A500 |
| 05 | 00280C58 | 00281AA8 |
| 06 | 00265688 | 002664D8 |
| 07 | 0027F3A0 | 002801F0 |
| 08 | 00260E70 | 00261CC0 |
| 09 | 00276280 | 002770D0 |
| 10 | 002493F0 | 0024A240 |
| 11 | 0027C798 | 0027D5E8 |
| 12 | 0026F6B8 | 00270508 |
| 13 | 00262600 | 00263450 |
| 14 | 0025EEF8 | 0025FD48 |
| 15 | 00245FF0 | 00246E40 |
| 16 | 00252038 | 00252E88 |
| 17 | 00251228 | 00252078 |
| 18 | 00258C70 | 00259AC0 |

Piece 2 always starts at piece 1 + 0xE50.

### gp-relative accesses, in order (as HOST_HOOKS collects them: base $gp, opcodes lw/sw/lq/sq/...;
`addiu x, gp` would not count, there is none): **ng = 15**

In level 0 gp = 0x166D00 (checked: DrawMobys' `sw v0, -0x6C6C(gp)` and `lw 0x94(0x160000)` are the
same word; DrawMobysSetup's `sw v0, -0x5A80(gp)` is the packet pointer 0x161280).

| # | pc (L0) | L0 off | L3 off | op | L0 addr | use |
|---|---|---|---|---|---|---|
| g0 | 00256B00 | -0x5A80 | -0x5AC0 | lw  | 0x161280 | packet (chain) write pointer -> sp.lo |
| g1 | 00256B08 | -0x6430 | -0x6470 | lq  | 0x1608D0 | VIF unpack-tag template -> SPR 0x3F40 (also at.hi, see §2.1) |
| g2 | 00256B10 | -0x5A78 | -0x5AB8 | lw  | 0x161288 | packet limit -> sp.hi (pcpyld) |
| g3 | 00256B28 | -0x6C50 | -0x6C90 | lw  | 0x1600B0 | s7: cap on a moby's draw distance (pminw) |
| g4 | 00256BC0 | -0x63D0 | -0x6410 | lq  | 0x160930 | chain tag template -> SPR 0x3E80 |
| g5 | 00256BC4 | -0x63C0 | -0x6400 | lq  | 0x160940 | -> SPR 0x3E90 |
| g6 | 00256BC8 | -0x63B0 | -0x63F0 | lq  | 0x160950 | -> SPR 0x3EA0 |
| g7 | 00256BCC | -0x63A0 | -0x63E0 | lq  | 0x160960 | -> SPR 0x3EB0 |
| g8 | 00256BD0 | -0x6390 | -0x63D0 | lq  | 0x160970 | -> SPR 0x3EC0 |
| g9 | 00256BD4 | -0x6380 | -0x63C0 | lq  | 0x160980 | -> SPR 0x3ED0 |
| g10 | 00256BF0 | -0x6370 | -0x63B0 | lq | 0x160990 | offset vector -> SPR 0x3EE0 (added to vf18 in the extra pass) |
| g11 | 00257734 | -0x6C48 | -0x6C88 | sw | 0x1600B8 | colour-patch list size in bytes (read by DrawMobysCleanUp, func_L00 0x2568A8) |
| g12 | 002577A0 | -0x6C44 | -0x6C84 | sw | 0x1600BC | shadow list count (read by func_0020DEB0 = 0x250C70 and shadowproc 0x286D40) |
| g13 | 002577C0 | -0x5A80 | -0x5AC0 | sw | 0x161280 | packet pointer saved |
| g14 | 00257860 | -0x6C40 | -0x6C80 | sw | 0x1600C0 | chain address of the reserved shadow splice slot (pass 2 only) |

### lui/addiu absolute pairs, in order: **na = 10**

| # | pc (L0) | L0 | L3 | use |
|---|---|---|---|---|
| a0 | 00256A4C | 0x0017FFC0 | 0x00180040 | light data, DMA ch9 (toSPR) 0x50 qwords -> SPR 0x2000 |
| a1 | 00256AA4 | 0x00173E00 | 0x00173E80 | 0x80-byte visibility mask table -> SPR 0x3800 |
| a2 | 00256AD4 | 0x001AEF00 | 0x001AEF80 | 0x40 bytes shadow planes -> SPR 0x3300 |
| a3 | 00256B40 | 0x00166D80 | 0x00166E00 | camera block (vf24..vf31) |
| a4 | 00256B78 | 0x0016CB40 | 0x0016CBC0 | frustum constants (+0xA0,+0xB0,+0x1D0,+0x1E0,+0x1F0 -> vf19..vf23) |
| a5 | 00256B98 | 0x001C3240 | 0x001C32C0 | 4 GS-state qwords -> SPR 0x3F00..0x3F30 |
| a6 | 00256C1C | 0x001C2800 | 0x001C2880 | address of the chain's start REF (qwc 4) |
| a7 | 00256C44 | 0x0016CB40 | 0x0016CBC0 | same block again: +0..+0x50 = 6 VU1 constant qwords |
| a8 | 00257738 | 0x001AC580 | 0x001AC600 | colour-patch list destination |
| a9 | 00257764 | 0x001AC980 | 0x001ACA00 | shadow list destination |

(Every other lui is paired with ori, or is a float constant, or 0x7000 scratchpad: 0x2569F8,
0x256A48 ori 0x1000D400, 0x256B34 0x4480, 0x256BFC 0x1000, 0x256C24 0x3000, 0x256CE0 0x4480,
0x256CE8 0x3A80, 0x256DF4 0x4000, 0x256E8C 0x1400, 0x256EE4 0x4780, 0x2575E4/EC, 0x25764C/54,
0x257690 ori D000, 0x2577AC/B4, 0x2577C8, 0x2577F8 ori, 0x25781C, 0x25784C/54, 0x257984 0x3F00,
0x257A0C 0x4000, 0x257D00 0x1400 ori, 0x257E04 ori D000. None counted.)

Level 3's data are all +0x80 (abs) / -0x40 (gp offsets) from level 0's: the hook must take them
from the arguments, never hard-code.

---------------------------------------------------------------------------------------------------
## 2. Block by block, in execution order

Register roles for the whole routine:

| reg | lo 64 | hi 64 |
|---|---|---|
| fp | current moby (0x100 bytes each) | - |
| s5 | end moby (a0 + a2*256) | shadow/deferral list write pointer (SPR 0x3000..0x3300) |
| s6 | 0x70000000 (SPR base) | point lights in range of the level (count, after setup) |
| sp | chain write pointer | packet limit (g2) |
| t7 | per moby: extra-pass fade (moby+0x73 test) | colour-patch list write pointer (SPR 0x3400..0x3800) |
| t6 | per moby: GS-state flags (moby flags & 0x208, \| 8 when fading) | LOD index (0/1) |
| t8 | current record in SPR (0x2800 / 0x2C00 halves) | - |
| t9 | record destination in main memory (a1, advances) | - |
| s7 | draw-distance cap (g3) | - |
| s4 | VU1 double-buffer parity (+1/-1, flipped per packet) | - |
| s3 | per moby: 0x1400000A (VU1 start 0x50, clip) or 0x1400000E (0x70, no clip); 0 = culled | - |
| s2 | per moby: matrix uploads left (2) | - |
| s1/s0 | packet table cursor / end | - |
| ra | mode: 0 (no deferral), 0xC00 (a3 != 0: defer flags 0xC00), -1 (pass 2) | - |

VU registers kept through the routine: vf0 (never written: `qmtc2 $zero, $vf0` at 0x256EDC,
0x256F34/3C/44/4C, 0x257724, 0x257B7C are no-ops; the translator emits `;`), vf11 = (1,-1,..),
vf19..vf23 frustum, vf24 = camera*1024 (w = 1024), vf25..27 camera rotation, vf28..31 view
projection.

### 2.1 Setup, 0x2569F8..0x256D4C

* 0x2569F8: `sd` ra, fp, sp, s7..s0 to SPR 0x3FF8, 0x3FF0, 0x3FE8, 0x3FE0..0x3FA8 (64-bit only).
* 0x256A28: fp = a0 - 0x100; s5 = (a2 << 8) + a0 (32-bit add); t9 = a1; ra = a3 ? 0xC00 : 0
  (`movn`).
* 0x256A48: **DMA ch9 (toSPR, 0x1000D400)**: MADR(+0x10) = a0abs (0x17FFC0), SADR(+0x80) = 0x2000,
  QWC(+0x20) = 0x50, CHCR(+0) = 0x100. (SPR 0x2000..0x24FF: 16 light sets of 64 bytes, then
  8 point lights of 32 bytes.)
* 0x256A74: s6 = 0x70000000; `sq $zero` SPR 0x2600..0x27FF (4 per pass, loop `bne at, v0` with
  at += 0x40 before the test; 8 passes). = per-class LOD counters, 2 bytes per class slot.
* 0x256AA0: copy 0x80 bytes a1abs -> SPR 0x3800 (lq/lq/sq/sq, 4 passes of 0x20).
* 0x256AD0: copy 0x40 bytes a2abs -> SPR 0x3300 (2 passes).
* 0x256B00: sp = [g0]; t7 = 0x70003400; at = lq [g1]; t7 = pcpyld(t7, zero) -> t7.hi = 0x70003400,
  t7.lo = 0; v0 = [g2]; sp = pcpyld(v0, sp) -> sp.hi = limit; `sq at` -> SPR 0x3F40.
* 0x256B20: vf11.x = vf0.x + vf0.w = 1; vf11.y = vf0.y - vf0.w = -1.
* s7 = [g3]; v0 = 0x70003000; s4 = 1; at.lo = 0x44800000 (1024.0f; lui writes only the low 64 bits:
  **at.hi is still g1's upper half**); `qmtc2 at, vf1` (vf1.zw = g1 qword's words 2,3 — dead, only
  vf1.x read); s5 = pcpyld(v0, s5) -> s5.hi = 0x70003000.
* 0x256B40 camera (a3abs): vf24 = +0x140, vf25 = +0, vf26 = +0x10, vf27 = +0x20, vf28..31 =
  +0x40..+0x70; vf24.xyz *= vf1.x (1024); vf24.w = vf0.w * vf1.x = 1024. t8 = 0x70002C00.
* 0x256B78 (a4abs): vf19 = +0xA0, vf20 = +0xB0, vf21 = +0x1D0, vf22 = +0x1E0, vf23 = +0x1F0.
* 0x256B98: 4 qwords a5abs -> SPR 0x3F00, 0x3F10, 0x3F20, 0x3F30.
* 0x256BC0: g4..g9 -> SPR 0x3E80..0x3ED0; g10 -> SPR 0x3EE0 (at.hi now = g10's upper half).
* 0x256BF8 chain start at sp:
  * q0: sq zero; w0 = 0x10000000; h+0xE = 0x1400; h+0xC = 0 -> **CNT qwc 0, VIF1 MSCAL 0x0000**
    (VU1 13859's buffer setup); sp += 0x10.
  * q1: sq zero; w1 = a6abs (0x1C2800); w0 = 0x30000004 -> **REF qwc 4**; sp += 0x10.
  * 0x256C38: v0 = SPR 0x3F40; sq v0 -> sp+0 and sp+0x70; sb 6 -> sp+0, +0xE, +0x70, +0x7E (qwc 6,
    unpack num 6); sh 0x123 -> sp+0xC, sh 0x287 -> sp+0x7C (VU1 addresses 0x123 and 0x123+0x164);
    sp += 0xE0; 6 qwords a7abs+0..+0x50 -> sp-0xD0..-0x80 and again -> sp-0x60..-0x10.
    (= native_moby.c's per-buffer constants 0x123..0x128: A+D tag, strip tag, guard band, clip
    scale, unclip scale, screen offset.)
* 0x256CC0: wait ch9 CHCR bit 8 clear (`lw 0(t2)`, `andi 0x100`, `bnez`).
* 0x256CE0: vf3 <- qmtc2 at (lo 0x44800000 = 1024; **zw = g10's upper words**), vf4 <- qmtc2 at
  (lo 0x3A800000 = 1/1024; zw same).
* **Point-light compaction, 0x256CF0..0x256D44**: at = v1 = SPR 0x2400, v0 = 0x2500, a0 = 0.
  0x256D00: `beq at, v0 -> 0x256D48` [delay `ld a1, 0x10(at)`, runs both ways]; `beqz a1 ->
  0x256D00` [delay at += 0x20]; vf1 = [at-0x10], vf2 = [at-0x20]; Q = vf4.x / vf1.w (vdiv,
  translator: rc_fc(rc_fdiv)); vf1.xyzw *= vf3.x; sqc2 vf1 -> v1+0x10; vf2.w = vf0.w * Q;
  sqc2 vf2 -> v1+0; a0++; `j 0x256D00` [delay v1 += 0x20]. (Entries whose dword +0x10 is 0 are
  dropped; the rest are packed in place: +0 colour with w = (1/1024)/range, +0x10 pos*1024,
  range*1024.)
* 0x256D48: `bnez $zero` (never) [delay s6 = pcpyld(a0, s6) -> s6.hi = light count]. Falls into
  the loop.

### 2.2 Per-moby loop head (pass 1), 0x256D50

* 0x256D50: v0 = lh 0x132(fp) (the *next* moby's +0x32 draw distance, before fp moves);
  fp += 0x100; **`beq fp, s5 -> 0x257718`** (end of pass 1) [delay vf1 = lqc2 0(fp): bsphere].
* at = lb 0x20(fp) (state, signed); v0 = pminw(v0, s7) (4 lanes); v0.lo <<= 42 (`dsll32 10`);
  vf2.xyz = vf1 - vf24; **`bltz at -> 0x257708`** [delay `qmtc2 v0, vf4`: vf4.x = 0,
  vf4.y = dist<<10, **vf4.zw = v0.hi lanes = min(v0.hi_prev, s7.hi)**].
  * 0x257708 (state >= 0x80): pref; at += 1; `bnez at -> 0x256D50` (skip) ; state == 0xFF
    (sentinel) falls to 0x257718.
* Culling arithmetic (all in the translator's clamped float order, see rc_funcs_052.c ~l.225):
  vf3.xy = vf11.xy * vf1.w; vf19.y = itof0(vf4.y); a2 = lbu 0x37(fp); ACC.xyz = vf25*vf2.x;
  a3 = lbu 0x36(fp); ACC += vf26*vf2.y; a2 += s6; vf2.xyz = ACC + vf27*vf2.z (camera space);
  a2 = lbu 0x3800(a2); vf19.y -= vf1.w; t0 = lbu 0x31(fp) (old drawn flag);
  vf8.xy = vf22.xy * vf1.w; a3 &= a2;
  **`beqz a3 -> 0x2576F8`** (mask byte moby+0x36 & table[moby+0x37] == 0) [delay t6 = lhu 0x34(fp)].
* vf1.xy = vf3.xy + vf2.z (z+r, z-r); a3 = lbu 0x22(fp) (class slot); vf5.xy = vf20.xy * vf2.z;
  v0 = t6 & 0x81; **`bnez v0 -> 0x2576F8`** (flags 0x81: hidden) [delay vf2.xy = |vf2.xy|].
* vf7.xy = vf8.xy * vf22.z; t1 = slot<<1; vf3.xy = vf19.xy - vf1.xy; a3 = slot<<2;
  vf9.xy = vf5.xy * vf21.xy; a3 += s6; vf6.xy = vf2.xy - vf8.xy; vf8.xy = vf2.xy + vf7.xy;
  a1 = 0x40000000; vf4.xy = vf1.xy - vf23.xy; a1 = a1 + a1 (daddu: 0x80000000);
  vf1.z = max(vf0.z, vf2.z); s3 = 0xA; at = qmfc2 vf3 (128); a2 = t6 & ra;
  vf7.xy = vf5.xy - vf6.xy; vf8.xy = vf9.xy - vf8.xy; v0 = at.lo << 32;
  * **`bltz at -> 0x2576F8`**: sign of vf3.y = (dist*1024 - r) - (z - r) = beyond draw distance
    [delay a0 = qmfc2 vf4].
  * **`bgez v0 -> 0x2576F8`**: sign of vf3.x = vf19.x - (z + r) >= 0: in front of the near plane
    [delay vmr32.xy vf1: vf1.x = vf1.y (z-r), vf1.y = vf1.z (max(0,z))].
  * **`bnez a2 -> 0x2578C8`** (flags & 0xC00 with deferral on: §2.9) [delay v0 = qmfc2 vf7].
  * v1 = qmfc2 vf8; a0 ^= a1 (flip sign of vf4.x);
    **`bltz v0 -> 0x2576F8`** (vf7.y < 0) [delay v0 <<= 32];
    **`bltz v0 -> 0x2576F8`** (vf7.x < 0) [delay vf1.xy = ftoi0(vf1.xy)] — side planes.
  * v1 |= a0; vf3.y = ftoi0(vf3.y); `bltz v1 -> 0x256E70` [delay v1 <<= 32]; `bltz v1 -> 0x256E70`;
    s3 = 0xE (sphere wholly inside the guard band: VU1 start 0x70, else 0x50 with clipping).
  * 0x256E70: `bnez t0 -> 0x256E80` [delay at = 1]; **sb 1 -> moby+0x31** (drawn flag, only when it
    was 0).
* 0x2576F8 (culled): `beqz t0 -> 0x256D50`; else `j 0x256D50` [delay **sb 0 -> moby+0x31**].

### 2.3 Common draw body, 0x256E80 (also entered from pass 2 and from the deferral path)

Entry state: vf1.x = ftoi(z - r) (int), vf3.y = int "distance left", s3, t1 = slot*2,
a3 = SPR + slot*4, t6 = flags, t0 = drawn flag (dead here).
* at = lw 0x3A00(a3); s2 = 2; vf18 = lqc2 0x10(fp) (pos); v0 = 0x14000000; a0 = lw 0x2C(fp)
  (scale); s3 |= v0 (MSCAL word); v0 = qmfc2 vf1; vf18.xyz *= vf24.w; **t8 ^= 0x400** (other SPR
  half); `qmtc2 a0, vf10` (vf10.x = scale bits, vf10.y = sign word of the lw, **vf10.zw = a0.hi =
  vf4.zw from 0x256E24 = v0.hi chain**; only vf10.x is read); v0 = pminw(v0, at);
  **sw v0.w0 -> SPR 0x3A00 + slot*4** (class nearest distance = min).
* t2 = ld 0x38(fp) (bytes 0x38 light set A, 0x39 set B, 0x3A blend, 0x3C..0x3F colour word);
  at = qmfc2 vf3; vf18.xyz -= vf24.xyz; v1 = lbu 0x23(fp); at = at.lo >> 39 (vf3.y >> 7, logical);
  v0 = 0x80; at = pminw(at, v0) (lane 0: min(.., 128)); LO/HI = at.w0 * v1.w0 (`mult $zero`);
  a0 = 0x47800000; **sq zero -> t8+0x40**; t6 &= 0x208; **sq zero -> t8+0x50**;
  at = (at < 128); **sq zero 0x60**; at <<= 3; **sq zero 0x70**; t6 |= at (bit 3: fading);
  **sq zero 0x80**; a0 = pextlw(a0,a0) twice (4 x 0x47800000); **sq zero 0x90**; v1 = LO;
  v0 = t2 >> 32 (dsrl32: colour word); v1 = (v1.w0 >> 7) << 24 (srl/sll 32-bit, sign-extended);
  **sq zero 0xA0**; v0 = por(v0, v1); v0 = pextlb(zero, v0); v0 = pextlh(zero, v0) (4 bytes ->
  4 words, upper halves discarded by pextlb); v0 = por(v0, a0); **sq v0 -> t8+0xB0** (r,g,b,a as
  65536.0f + byte; a = byte3 | (fade*moby+0x23 >> 7)).
* 0x256F54: at = pcpyud(sp, zero) (limit); vf15 = 0xC0(fp); vf16 = 0xD0(fp); at = at - sp (32-bit);
  **`bltz at -> 0x257718`** (chain full: the pass ends) [delay vf17 = 0xE0(fp)].
* 0x256F70 object matrix: vf12.x=-vf15.x, vf13.y=-vf16.y, vf14.z=-vf17.z, vf13.x=-vf15.y,
  vf14.x=-vf15.z, vf12.y=-vf16.x, vf14.y=-vf16.z, vf12.z=-vf17.x, vf13.z=-vf17.y (negated transpose;
  **vf12..14 .w are never written — stale**); vf15..17.xyz *= vf10.x; vf15 = vf28*x + vf29*y +
  vf30*z + vf31*w (ACC chain, xyzw; vf15.w is moby+0xCC unscaled), same vf16, vf17;
  vf18 = vf28*x + vf29*y + vf30*z + vf31*vf0.w. (vf15..18 = object-to-screen rows sent to VU1
  0x129..0x12C.)
* 0x256FE8: s1 = lw 0x24(fp) (class); t7.lo = lbu 0x73(fp); t1 += s6; at = lbu 0x72(fp);
  t1 += 0x2600; **`bnez t7 -> 0x257B58`** [delay t0 = qmfc2 vf1].
  * 0x257B58: v1 = (t7 << 10) - sra(t0,0); `blez v1 -> 0x257008` [delay t7 = 0];
    v1 = srl(v1, 7); v1 = pminw(v1, 0x80); `j 0x257008` [delay t7 = v1] (t7.hi kept: addi/lbu
    write only the low 64).
* 0x257008: v1 = lw 0x54(fp); a0 = 0x30000; t0 = pminw(t0, a0); a0 = lw 0x68(fp) (cur frame data);
  at <<= 10 (LOD distance); a1 = lw 0x6C(fp) (prev frame data); t0 = pmaxw(t0, 0);
  a2 = lw 0x60(fp); t0 = sra(t0, 0); a3 = lw 0x64(fp); at = t0 - at;
  **sw v1 -> t8+4; sw a2 -> t8+8; sw a3 -> t8+0xC**; **`bgtz at -> 0x257070`** (far)
  [delay t3 = lwu 0x50(fp): frame, prev_frame, seq, prev_seq].
  * near (0x257050): t0 = lbu 0(t1); t6 = pcpyld(zero, t6) (hi = 0); at = 0; v0 = lbu 4(s1);
    t0++; a2 = lbu 8(s1); `j 0x257090` [delay **sb t0 -> SPR 0x2600+slot*2**].
  * far (0x257070): t0 = lbu 1(t1); a3 = 1; at = lbu 4(s1); v0 = lbu 5(s1); t0++; a2 = lbu 9(s1);
    **sb t0 -> SPR 0x2601+slot*2**; t6 = pcpyld(a3, t6) (hi = 1).
* 0x257090: t4 = (t3 >> 16) & 0xFF; t0 = lw 0x18(s1); t1 = lw 0x14(s1); t5 = t3 >> 24;
  s1 = lw 0(s1) (packet table); **sw v0 -> t8+0** (packet count); at <<= 4; **sw a2 -> t8+0x10**;
  v0 <<= 4; **sw t7 -> t8+0x14**; s1 += at; **sw t0 -> t8+0x18**; s0 = s1 + v0;
  **sw t1 -> t8+0x1C**; **`beqz a2 -> 0x257138`** [delay a2 = t3 & 0xFF].
  * a2 != 0: `bne t4, t5 -> 0x2570F8` [delay a3 = t3 >> 8]; a3 &= 0xFF; a2++;
    `bne a2, a3 -> 0x2570F8` [delay a2 = 0x400]; **sh 0x400 -> t8+0x12**.
    0x2570F8: **t8+0x20 = cur+0x10, +0x24 = lhu cur+6, +0x28 = lw cur+8, +0x2C = lw cur+0xC**;
    same from prev -> **t8+0x30..0x3C**. (When a2 == 0, t8+0x20..0x3F keep whatever that SPR half
    held: stale.)
* 0x257138 lights: t5 = lhu 0x34(fp) & 0x10; v1 = (t2 >> 16) & 0xFF (blend); at = (t2 & 0xFF) << 6
  + s6; **`bnez v1 -> 0x2571F0`**.
  * one set (0x257158): vf1 = [at+0x2010], vf2 = [at+0x2030], vf3 = [at+0x2000], vf4 = [at+0x2020];
    vf1 = vf12*x + vf13*y + vf14*z (xyzw, uses the stale .w); **sqc2 vf3 -> t8+0x70, vf4 -> t8+0x80**;
    vf2 the same; **sw [at+0x200C] -> t8+0xA0, [at+0x202C] -> t8+0xA4; sw 0 -> t8+0x7C, t8+0x8C**;
    **vf1.x,y,z -> t8+0x40, 0x50, 0x60; vf2.x,y,z -> t8+0x44, 0x54, 0x64** (qmfc2 + sw / dsrl32 /
    pcpyud); `j 0x257330`.
  * blend (0x2571F0): v1 <<= 4; v0 = ((t2 & 0xFF00) >> 2) + s6; a0 = 0x1000 - v1;
    vf10.xyz = 1; v1 = pcpyld(a0, v1); `qmtc2 v1, vf9` (fully known: {b*16, 0, 0x1000-b*16, 0});
    vf1..4 = set A +0x10,+0x30,+0,+0x20; vf9.xz = itof12; vf5..8 = set B; vf1,2 .xyz *= vf9.z;
    vf3,4 .xyzw *= vf9.z; vf5,6 .xyz, vf7,8 .xyzw *= vf9.x; vf1 += vf5 ... vf4 += vf8;
    vf5.xyz = vf1²; vf6.xyz = vf2²; **sqc2 vf3 -> 0x70, vf4 -> 0x80**; vf5.x = |vf1|², vf6.x = |vf2|²
    (vadday.x + vmaddz.x with vf10); Q = rsqrt (translator: rc_fdiv(vf0.w, rc_fsqrt(x)), no fc);
    vf1 -> object space; **0xA0 = word 0x7C, 0xA4 = word 0x8C, then 0x7C = 0x8C = 0** (lw/sw/sw/sw);
    vf1.xyz *= Q; Q = rsqrt(vf6.x); vf2 -> object space; **vf1 -> 0x40/0x50/0x60**; vf2 *= Q;
    **vf2 -> 0x44/0x54/0x64**.
* 0x257330 point lights: vf1 = 0(fp); v1 = pcpyud(s6) (count); **`beqz v1 -> 0x2574F8`**
  [delay **t8 += 0xC0**].
  * vf2 = SPR 0x2410; v0 = 0x2400; at = 0x2500 (in-range list); a0 = 0; vf3.xyz = vf2 - vf1;
    vf6.xyz = 1; vf7.xyz = -1.
  * loop 0x257368: `beqz v1 -> 0x257440` [delay vf3.xyz = vf3²]; vf4.w = vf2.w²; v0 += 0x20;
    vf2 = [v0+0x10]; v1--; ACC.y = vf3.y + vf3.x; ACC.y += vf6.y*vf3.z; vf5.y = ACC.y + vf7.y*vf4.w
    (d² - range²); vf8.y = ACC.y + vf0.y*vf0.x (= d²); vf3.xyz = vf2 - vf1 (next); a1 = qmfc2 vf5;
    Q = sqrt(vf8.y) (0x2573C8, `.word 0x4A8803BD` = vsqrt, translator rc_fsqrt);
    **`bgtz a1 -> 0x257368`** — a1.lo = {vf5.x bits, vf5.y bits}: out of range when the 64-bit
    value is > 0, i.e. vf5.y's sign clear **and the dword nonzero; vf5.x is a stale lane** (last
    set by 0x256DC0 vf20.x*z, or |A|² in the blend path, or 0x257990 in the deferral path).
  * in range (0x2573D8): vf8 = [v0-0x10]; vf9 = [v0-0x20]; vf8.xyz = vf1 - vf8; vf5.w = Q; vf9.w *= Q;
    Q = vf0.w / vf5.w (vdiv, fc); vf9.w = vf0.w - vf9.w; vf8 -> object space (xyzw);
    vf9.xyz *= vf9.w; vf8.xyz *= Q; **sqc2 vf9 -> at, vf8 -> at+0x10**; a0++; at += 0x20; `j 0x257368`.
  * 0x257440: `beqz a0 -> 0x2574F8` [delay at = 1]; `bne a0, at -> 0x257480` [delay at = 0x2500].
    * one: **sq [0x2500] -> t8-0x30 (rec+0x90); sw 0 -> rec+0x9C; dir x,y,z -> rec+0x48, 0x58,
      0x68**; `j 0x2574F8`.
    * several (0x257480): vf1.xyz = 0; ACC.xyz = 0; loop: vf3 = [at], vf4 = [at+0x10]; vf1 += vf3;
      at += 0x20; ACC += vf4 * vf3.w; a0--; `bnez a0` [delay vf2.xyz = ACC + vf0*vf0.x];
      vf3 = vf2²; vf3.x = |vf2|² (vf6 = 1); Q = rsqrt; vf2 *= Q; **vf1 -> rec+0x90 (sqc2), 0 ->
      rec+0x9C, vf2 -> rec+0x48/0x58/0x68**.
* 0x2574F8: **`bnez t5 -> 0x257AE8`** (flags & 0x10: colour-patch entry, §2.6) [delay
  t1 = SPR 0x3F40].
* 0x257500: t2 = SPR 0x3E80, t3 = 0x3E90, t4 = 0x3EC0, t5 = 0x3ED0.

### 2.4 Per-packet chain loop, 0x257510..0x25763C

Per packet (s1 += 0x10 until s0):
* `blez s2 -> 0x257548` [delay **s4 = -s4**, every packet]. If s2 > 0 (first two packets):
  **sq t1 -> sp; s2--; sqc2 vf15..vf18 -> sp+0x10..0x40; sh (s4 > 0 ? 0x28D : 0x129) -> sp+0xC;
  sp += 0x50** (matrix into VU1 0x129..0x12C of the buffer in use).
* 0x257548: at = lw 8(s1); v0 = lw 0xC(s1); a2 = sp + 0x20; **sq t2 -> sp; sq t3 -> sp+0x10**;
  **record: sw ((at << 4) | (v0 & 0xFF)) -> t8+0, sw a2 -> t8+4**;
  **sb v0>>24 -> sp+0xE, sp+0x1E; sh (v0>>8)&0xFF -> sp+0; sh (v0>>16)&0xFF -> sp+0x10**; t8 += 8;
  a2 += a1*16; **sw a2 -> sp+4**; a2 += a0*16; **sw a2 -> sp+0x14**; sp = a2.
  (A hole of (a1 + a0) qwords at old sp+0x20 is reserved and left unwritten: the skin driver
  fills it later from the record's address list.)
* v0 = ld 0(s1); s1 += 0x10; **sq t4 -> sp**; sp += 0x10; **sw v0.w0 -> sp-0xC; sh v0.w1 -> sp-0x10**.
* `blez s2 -> 0x257630` (only the first packet goes on) [delay sh above];
  `beqz t6 -> 0x257630` [delay at = t6 & 0x200]; GS-state block:
  **sq zero; sw 0x10000000 -> sp; sw 0x13000000 -> sp+0xC** (CNT + FLUSHA); sp += 0x10;
  **sq SPR 0x3F00 -> sp, 0x3F10 -> sp+0x10**; a0 = lq SPR 0x3F20; `bnez at -> 0x257628`
  [delay `ori a0, zero, 0x48` — **writes a0.lo = 0x48 (word1 = 0), a0.hi stays 0x3F20's upper
  half**]; else a0 = lq SPR 0x3F30; v1 = (t6 << 4) - 0x600; a0.lo = SE32(a0.w0 + v1) (word1 =
  sign); **sq a0 -> sp+0x20** (128-bit store mixing a fresh low dword with the lq'd upper);
  sp += 0x30.
* 0x257630: **sq t5 -> sp**; sp += 0x10; `bne s1, s0 -> 0x257510` [delay **sh s3 -> sp-4**
  (MSCAL 0x0A / 0x0E)].
* 0x257640 after the packets: `beqz t6 -> 0x257688` [delay at = t6 & 0x200]; restore block:
  CNT + FLUSHA qword; SPR 0x3F00, 0x3F10; **sq (at ? lq 0x3F20 : lq 0x3F30) -> sp+0x20**; sp += 0x30.
* 0x257688: **`bnez t7 -> 0x257B88`** (extra pass, §2.7) [delay **sd zero -> t8** (list end)].

### 2.5 Record out, 0x257690..0x2576F4

* at = 0x1000D000 (**DMA ch8, fromSPR**); t8 += 0x10; wait CHCR bit 8 (0x2576A0 loop; delay
  a0 = t8 & 0x3FF); **sd zero -> t8-8**; t8 &= 0x2C00; a0 >>= 4; **SADR(+0x80) = t8, QWC(+0x20) =
  a0, MADR(+0x10) = t9, CHCR = 0x100**; t9 += a0*16; **`bgez ra -> 0x256D50`** (pass 1: next
  moby) [delay t8 += s6]; else `j 0x257878` (pass 2: next list entry).
  The record (0xC0 + 8*packets + 8, rounded up to a qword, in the SPR half t8 points at) is the
  "anim job" MobyAnimProc walks.

### 2.6 Colour-patch entry (moby flags & 0x10), 0x257AE8

at = pcpyud(t7) (list pointer); v1 = lw 0x24(fp); v0 = 0x3800; a2 = lw 0x90(fp);
`beq at, v0 -> 0x257500` (full, 64 entries) [delay a0 = pcpyud(t6) = LOD]; a3 = lbu -4(t8)
(rec+0xBC low byte); v1 = lbu 0xA(class + LOD); a1 = (s0 - s1) - v1*16; v1 = s1 + v1*16;
`blez a1 -> 0x257500` [delay a1 >>= 4]; **entry at SPR at: w0 = moby+0x90, byte 3 = a3,
w1 = v1 (packet entries after the first N), w2 = t9 + N*8 + 0xC0 (their record address list
in main memory), w3 = a1 (count)**; at += 0x10; `j 0x257500` [delay t7 = pcpyld(at, t7)].

### 2.7 Extra pass (moby+0x73 near distance, t7 != 0), 0x257B88..0x257E14

vf9 = 0(fp); a0 = t8 & 0x3FF; at = lw 0x24(fp); a0 = t8 - a0 (record base); v0 = lbu 7(at);
v1 = lbu 6(at); t8 += 0x10; s1 = lw 0(at) + v0*16; a1 = (v1 + 0xA) >> 1; vf9.xyz -= vf24;
**sh a1 -> record+0x16**; s0 = s1 + v1*16; t8 &= ~0xF; t2 = SPR 0x3E80, t3 = 0x3EA0, t4 = 0x3EB0,
t5 = 0x3EC0, t6 = 0x3ED0 (the 128-bit t6 overwrites the LOD/state); Q = rsqrt(|vf9|²);
vf1..3 = moby rows 0xC0..0xE0 through vf25..27; vf4 = vf0; vf9 normalised, through vf25..27;
two rotations built in vf5..7 (xyz only; **vf5..7 .w stale**) applied to vf1..3 (xyzw);
**sqc2 vf1..vf4 -> t8+0..0x30**; t8 += 0x40; vf18 += SPR 0x3EE0 (xyzw); t1 = SPR 0x3F40;
s2 = 2; **s3 = 0x1400000A**. Per packet (0x257D08): matrix upload as §2.4 (s4 flip, 0x129/0x28D);
three tags t2, t3, t4 at sp, +0x10, +0x20 (unpack num v0>>24 in bytes 0xE/0x1E/0x2E; qwc (v0>>8)&0xFF
in h+0, (v0>>16)&0xFF in h+0x10 and h+0x20; addresses sp+0x30+a1*16, +a0*16, +a1*16 in w+4, w+0x14,
w+0x24), record entry (w0, w1 = sp+0x30), hole of a1+a0+a1 qwords, t5 tag with the packet's
address/qwc, t6 tag with `sh s3`. Then **sd zero -> t8**; t8 += 0x10; `j 0x2576A0` (the record DMA,
skipping the t8 += 0x10 at 0x25769C).

### 2.8 End of pass 1, 0x257718

**`bgtz ra -> 0x257848`** (pass 2, only when a3 != 0); else 0x257720:
* v0 = pcpyud(t7); v1 = v0 - 0x3400; `blez v1 -> 0x257760` [delay **sw v1 -> g11**]; copy SPR
  0x3400..v0 -> a8abs (qword loop, sq in the delay slot).
* 0x257760: v0 = pcpyud(s5); a0 = a9abs; at = 0x3000; `beq at, v0 -> 0x2577A0` [delay v1 = 0];
  per 16-byte entry: a1 = ld 0(at); **sd a1 -> a0**; if a1 >> 32 != 0: a0 += 8, v1++.
  (Shadow list: {moby, fade (h4), edge flag (h6)} for entries with a nonzero second word.)
* 0x2577A0: **sw v1 -> g12**; chain: **CNT + FLUSHA qword at sp**; sp += 0x10; **sw sp -> g13**;
  v1 = t9; ld s0..s7, sp, fp, ra from SPR (64 bits only); wait ch8; **sq zero -> SPR 0**;
  **ch8: SADR = 0, QWC = 1, MADR = t9, CHCR = 0x100** (zero terminator record); `jr ra`
  [delay v0 = t9 + 0x10].

### 2.9 Deferral (flags & 0xC00, a3 != 0), 0x2578C8 (pass 1)

s3 = 0; vf3.y = ftoi0; v1 = qmfc2 vf8; a0 ^= a1; side tests as in §2.2 but to 0x257918;
s3 = 0xA; `bnez t0` else **sb 1 -> moby+0x31**; guard-band test to 0x257928 else s3 = 0xE.
0x257918 (outside a side plane): if t0 **sb 0 -> moby+0x31** (s3 stays 0; vf1 not converted).
0x257928: t5 = pcpyud(s5); t3 = qmfc2 vf1; `beq t5, 0x3300 -> 0x2576F8` (list full: as culled)
[delay v0 = lhu 0x34(fp)]; **entry at t5: w0 = fp, h8 = t1, hA = s3, wC = t3**;
`bnez (flags & 0x800) -> 0x257AB0`.
* 0x257AB0: if s3 == 0 next moby (entry not kept); else **h4 = h6 = 0**, t5 += 0x10, s5.hi = t5.
* 0x257958: a0 = lw 0x88(fp); v1 = lw 0x84(fp); v0 = lbu 0xBD(fp); `blez a0 -> 0x257AC8`
  [delay a0 = pextlw(a0, v1)].
  * 0x257AC8: if s3 == 0 next moby; else at = ((lh 0x32(fp) << 10) - t3) << 32;
    `qmtc2 t3, vf1` (**vf1.zw = t3.hi: from lq SPR 0x3E90 / 0x3EA0 or qmfc2 vf1**);
    `j 0x256E80` [delay `qmtc2 at, vf3`] — drawn now, in pass 1.
  * shadow sphere (0x257970): vf4 = qmtc2 a0 (m84, m88, sign words); plane vf5 = SPR 0x3300 +
    moby+0xBD*16; vf8 = 0.5 (`qmtc2 v1`, **vf8.zw = v1.hi = vf8.zw at 0x2578D0**); vf6 = bsphere;
    projects the sphere onto the plane (vdiv Q = vf4.x / vf5.z, vmulaq, vmaddw) giving vf1;
    at = moby+0x7F << 42 -> vf4 (**zw = at.hi**); the §2.2 culling repeated (0x2579C4..0x257A70)
    with t2 = 1 / 0 for "crossing the edge" / "inside"; culled -> 0x257AC8.
    0x257A78: v1 = min(srl((moby+0x7F << 10) - ftoi(vf1.x), 1), 0x1000); **h6 = t2, h4 = v1**;
    t5 += 0x10; `j 0x256D50` [delay s5 = pcpyld(t5, s5)].

### 2.10 Pass 2, 0x257848 (after pass 1 with a3 != 0)

**CNT + FLUSHA qword at sp**; sp += 0x10; **sw sp -> g14** (the qword at sp is the slot
func_0020DEB0 later fills); sp += 0x10; ra = -1; **sw 0x70003000 -> SPR 0x3EF0** (cursor).
0x257878: t5 = SPR 0x3EF0; `beq t5, s5.hi -> 0x257720` (done) [delay fp = lw 0(t5)];
t1 = lh 8(t5); t3 = lw 0xC(t5); t6 = lhu 0x34(fp); a3 = SPR + t1*2; at = lh 0x32(fp);
s3 = lh 0xA(t5); **sw t5+0x10 -> SPR 0x3EF0**; `beqz s3 -> 0x257878` (culled entries skipped)
[delay at = (at << 10) - t3]; at <<= 32; `qmtc2 t3, vf1`; `j 0x256E80` [delay `qmtc2 at, vf3`].
Each entry then runs §2.3..2.5 and returns to 0x257878 (ra < 0). Chain overflow in pass 2 ends
at 0x257718 -> 0x257720.

---------------------------------------------------------------------------------------------------
## 3. Calls

None. MobyProc has no jal/jalr; its only exit is `jr $ra` at 0x257840 (and the hook's exit).
Its callers (level-0 copies of executable code):
* **DrawMobys** 0x251070 (func_0020E2B0): DrawMobysSetup; if [0x16A118] != 0 (D_0018A3D8):
  InitMobyClassDists (SPR 0x3A00..0x3D7F = 0x40000000); **MobyProc(a0 = [0x160098] moby list,
  a1 = [0x160094] record pointer, a2 = -1, a3 = 1)**; v0 -> [0x160094] (gp-0x6C6C); if
  [0x161280] > [0x161288] printf (overflow); DrawMobysCleanUp.
* **DrawMobyList** 0x250F40 (func_0020E180, a0 list, a1 count): GS reg, func 0x118D80(0),
  RestoreMobyClassDists, **MobyProc(a0, [0x160094], a1, 0)**, v0 -> [0x160094],
  StashMobyClassDists, [0x160094] -= 0x10. (a3 = 0: no deferral / pass 2; it still writes g11,
  g12 = 0 and g13.)
* DrawMobysSetup 0x250E88: VU1 data ref, VU0_loadMicroProgram(0x100080) (the skin micro
  program), GS reg 0x47, reserves a tag slot ([0x16008C] = packet ptr; ptr += 0x10),
  [0x1600C0] = 0, [0x161288] = [0x15F6DC] - 0x10000 (limit), [0x160094] = [0x15F6D8].

---------------------------------------------------------------------------------------------------
## 4. What later code reads

* **moby+0x31** (drawn flag): set to 1 when drawn and it was 0 (0x256E78, 0x2578FC), cleared
  when culled and it was non-zero (0x257704, 0x257920). Gameplay reads it (e.g. vendor code
  in l05/l07 overlays: `if (m->unk31 != 0 ...)`).
* **SPR 0x3A00 + slot*4** (min camera distance per class): read by the moby texture driver
  func_00212258 (level copy 0x2565F8, `addi t7, at, 0x3A00`) from DmaMobyTextures;
  Stash/Restore copy it to/from 0x1B3200 around DrawMobyList.
* **SPR 0x2600 + slot*2** (per-class LOD0/LOD1 counts): not traced further.
* **Anim records at t9** (from [0x15F6D8]; zero qword terminator): DrawMobysCleanUp ->
  ProcessMobyAnimData (0x250DB8: SPR 0x3800 <- 0x166180, 0x800 bytes; MobyAnimProc
  0x256918([0x15F6D8], [gp-0x7624])). MobyAnimProc per record calls func_L00_00252018 (joint
  evaluation: frame data at +0x20/+0x30, +0x8 manips) and func_L00_001EEBD8 (skin driver: DMA
  ch9 record+0x40.. to SPR 0x1C00, polls SPR 0x1C78 + count*8 (the last list entry) for arrival,
  per packet entry at +0xC0: DMA (w0 >> 8) << 4 qwc w0 & 0xFF to SPR 0x2000, VU0 micro
  `vcallms 0`, results to the chain hole w1; returns the next record).
  Record layout: +0 packet count, +4 moby+0x54, +8 +0x60, +0xC +0x64, +0x10 class+8/+9 byte
  (+0x12 = 0x400 when same seq and prev_frame == frame+1), +0x14 extra-pass fade (+0x16 =
  (class+6 + 10) >> 1 in the extra pass), +0x18 class+0x18, +0x1C class+0x14, +0x20..0x2C current
  frame, +0x30..0x3C previous frame, +0x40/0x50/0x60 light 0 dir, +0x44/0x54/0x64 light 1 dir,
  +0x48/0x58/0x68 point light dir, +0x70/+0x80 light colours (w 0), +0x90 point colour (w 0),
  +0xA0/+0xA4 light words, +0xB0 colour floats, +0xC0 8 bytes per packet {source<<4 | qwc,
  chain hole}, 0 terminator; extra pass: aligned 4-qword matrix then its list.
* **Chain holes** reserved in §2.4/2.7: filled by the skin driver; the first words of each
  colour-patch packet's hole are overwritten by func_L00 0x2568A8 (DrawMobysCleanUp, if g11 != 0)
  from the list at a8abs.
* **g12 / a9abs shadow list** and **g14 slot**: DrawMobysCleanUp -> func_0020DEB0 (0x250C70): with
  count 0 the slot becomes a CNT, else NEXT splices func_00228A58 (shadowproc, level copy
  0x286D40: `lw s7, -0x6C44(gp)`, s4 = 0x1AC980, s6 = 0x1AEF00) and func_00228860 into the
  chain. So pass-2 mobys are drawn after the shadows.
* g13 packet pointer (DrawMobys' overflow check; following draws), v0 -> [0x160094].
* VU1 13859 (native_moby.c) gets: 0x123..0x128 constants (chain start, both buffers),
  0x129..0x12C matrix rows vf15..vf18 (first two packets of each moby), the per-packet unpacks
  from the templates, MSCAL 0x50 (clip) / 0x70 (no clip); start 0x0000 once at the chain start.

---------------------------------------------------------------------------------------------------
## 5. Risks for a word-for-word native version

1. **Two catalogue functions, one routine (region).** Hook at entry 0x2569F8 only; arguments
   come from all 1288 instructions (15 gp, 10 abs). Control flow crosses between pieces 13 times;
   pass 2 and the deferral path re-enter the middle of the draw body (0x256E80) with different
   register contents, and `ra`'s sign picks where 0x2576E8 returns. Best written as one C
   function with gotos/loops mirroring the labels.
2. **GPR upper halves reaching VU registers** (must be carried like tieproc's v0h/a0h/t3h if the
   VU0 registers are compared at the end): 0x256B38 (at.hi = g1 hi -> vf1.zw), 0x256CE4/0x256CEC
   (at.hi = g10 hi -> vf3.zw, vf4.zw; vf3.w survives to the first light load), 0x256D74 (v0.hi =
   pminw(v0.hi, s7.hi) -> vf4.zw, then via qmfc2 a0 at 0x256E24 -> **vf10.zw at 0x256EB0**),
   0x2578BC/0x257ADC (t3.hi -> vf1.zw), 0x2578C4/0x257AE4/0x2579C8 (at.hi -> vf3.zw / vf4.zw),
   0x257988 (v1.hi -> vf8.zw). All reach only lanes no later instruction reads, but they are what
   the VU registers hold at exit. s7.hi is whatever the caller left.
3. **128-bit store mixing halves**: 0x257628 `sq a0` (low dword from `ori 0x48` or a 32-bit add,
   sign-extended; high dword from `lq` SPR 0x3F20/0x3F30). Also the por/pextlb/pextlh colour
   build at 0x256F30 (fully determined).
4. **Stale VU lane in a branch**: 0x2573D0 `bgtz a1` compares {vf5.x, vf5.y} as one 64-bit
   integer; vf5.x comes from earlier unrelated code. Also vf12..14.w (never written) feed the w
   lanes of every object-space transform (not stored, but left in registers).
5. **DMA as part of the algorithm**: ch9 toSPR at setup (lights), ch8 fromSPR per moby (record
   -> t9, double-buffered SPR halves via `xori 0x400`) and at the end (terminator), with busy-wait
   loops. The native version must issue the same register writes (as tieproc's flush()).
6. **Stale SPR contents become output**: record words +0x20..0x3F when class+8/+9 is 0, and
   whatever else the record half held; build the record in SPR memory itself, not in a struct.
7. **Holes filled later**: the chain is incomplete when MobyProc returns (skin driver + VU0 micro
   mode, colour patch, shadow splice). A "direct draw" mode like tieproc's mode 2 cannot draw
   from inside MobyProc; it would have to happen at kick time or after DrawMobysCleanUp.
8. Long paths: ~1288 instructions, 3 nested per-moby paths (normal, deferral with shadow
   projection, pass 2), per-packet loops (main, extra pass), point-light loop, colour-patch and
   shadow list builders. No computed jumps, no micro mode, no self-modifying code.
9. Pass 1 with a2 = -1 ends only at a state-0xFF moby (or chain overflow) — no bound.
10. Exit state: s5.hi, s6.hi, sp.hi, t6, t7 hi are left modified (callee-saved registers are
    restored with 64-bit `ld`); LO/HI hold the last moby's `mult`; Q/ACC/vf1..vf18 the last
    moby's values. Needed only for an exact check mode.
11. Float semantics must match the translator: every FMAC result and partial sum rc_fc-clamped;
    vdiv = rc_fc(rc_fdiv), vrsqrt = rc_fdiv(vf0.w, rc_fsqrt(x)) (no clamp), vsqrt = rc_fsqrt,
    vitof0/vitof12/vftoi0 via rc_itof/rc_ftoi, vabs = fabsf, vmax = fmaxf.
