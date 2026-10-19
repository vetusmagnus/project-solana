# Piece 4 research notes: EE-built GIF (VIF1 DIRECT), HUD/text, 2D batch API

Read-only research. Addresses: "exe" = executable/decomp name (func_X); "L00" = where level 00's copy sits
(from kit/decomp/config/overlays/functions.tsv). Each level carries its own copy at its own address.
Scanner scripts used (in this scratchpad): scan_direct.py (lui 0x5000/0x5100 + chain-pointer refs),
scan_fbmsk.py, scan_ref.py, callers.py, gifdec.py, dumpq.py.

## 0. Facts that shape everything

- In a level, the level's own program runs (ParseBin copies it over the exe main segment). Per
  docs/LEVELS.md, every exe-kind function (func_X) in a level is translated from the level's bytes, not
  compiled from src/game C. Shared level functions use src/overlays C only in their canonical level
  (L00), and not even there when the C has inline asm (func_L00_0023C458 and func_L00_002A2258 are
  translated from bytes, see gen/l00/overlays_shared_*.c comments). So the decomp C mostly helps you
  read the code. You cannot "hook the compiled C".
- The hook route already exists: tools/recomp.py HOST_HOOKS (line 53) puts an entry hook into the
  translated function, `uya_X_enter(c, gp_offsets[], n, abs_addrs[], n)`, with the level's relocations
  read from the routine's own bytes. ShrubProc, TieProc, TfragProc and MobyProc use it. Use the same
  route for the 2D/HUD/shadow builders (hook by exe name; it covers every level).
- Chain packet pointer: the exe has D_00161000 at 0x161000; L00 has D_L00_00161280 at 0x161280
  (lui 0x16 + 0x1280, or gp-0x5A80); L02/L09/L11/L12/L13 have 0x161240. It is per level, so always take
  it from the hook's relocations.
- Every EE packet is built the same way: a DMA tag + VIF pair `{0x1000000n (cnt) | 0x3000000n (ref), addr,
  0 or 0x13000000 (FLUSHA), 0x5000000n (DIRECT n)}` followed inline by GIF data (cnt), or pointing at a
  static GIF table (ref). host/src/gfx/vif.c:205 sends all of it as "PATH2 (VIF1 DIRECT)" through
  uya_gif_packet, so it is never converted to a dlist.
- Chain markers already exist: a CNT tag with qwc 0 whose VIF0 word is a NOP with a magic immediate
  (NATIVE_TIE_MARK 0x4954, NATIVE_SHRUB_MARK 0x5253, NATIVE_TERRAIN_MARK 0x4D4B), seen by the DMA walker
  (host/src/sdk/dma.c:132). Native records are drawn where the chain meets the marker, so the order
  against PATH1 stays as on the PS2.

## 1. Every EE function that writes GIF data with VIF DIRECT (level 00 addresses)

PACKED/REGLIST/IMAGE refer to the GIF tag mode. "C" means the decomp has C for it (exe C in src/game or
shared C); in a level it still runs translated from bytes.

### 1a. GS state setup (A+D register writes)
| exe func (L00) | what | format | C/asm |
|---|---|---|---|
| func_00234C98 VU1_addGSregister (0x2A1E38) | one GS register: `cnt 2 / DIRECT 2`, tag NLOOP1 A+D, {val, reg} | PACKED A+D | C vuchain.c:497 |
| func_00234D58 VU1_setScissor (0x2A1EF8) | SCISSOR_1, clamped | PACKED A+D | C vuchain.c:522 |
| func_00234E80 VU1_texFlush, func_00234EE0 VU1_gsRegsNormal, func_00234F40, func_00234FA8 (0x2A2020, 0x2A2080, 0x2A20E0, 0x2A2148: the catalogue names all four func_001FB848 because their bytes match) | ref tags to static A+D tables (D_001DF180 TEXFLUSH, D_001DE740, D_001DE770, D_0013D010) | PACKED A+D | C vuchain.c:569.. |
| func_001F3C10 ResetGsRegisters (0x1F87D0) | ref D_0013D0C0 (18 A+D: ALPHA_1/2 0x44, CLAMP, COLCLAMP 1, DIMX/DTHE/FBA/PABE/PRMODE 0, PRMODECONT 1, SCANMSK 0, TEST_1/2 0x5360B, TEXA, TEXCLUT, TEXFLUSH) + ref D_0013D010 (FRAME_1/2 FBP 0x70 FBW 8, ZBUF_1/2 0xD8 PSMZ24, XYOFFSET 0x7000/0x7300, SCISSOR 511x415, FBA) + FOGCOL | PACKED A+D | C draw.c:651 |
| func_001FB498 PutDrawBufferLarge, func_001FB598 PutDrawBufferSmall (0x200EF0, 0x200FF0) | ref to draw env D_0015EFB8+0x30 / +0xC0 (9 qw, the sceGs draw env) | PACKED A+D | C framebuf.c:78/105 |
| func_001FB530 framebuf_appendLargeSetup (0x200F88) | ref D_00152140 (0x15 qw, built at run time, likely by asm func_001FAB40) | ? (bss) | C framebuf.c:94 |
| func_001F4630 SetupGifPaging / func_001F4748 DoGifPaging (0x1F93E0 / 0x1F94F8); sky twins func_0022C7E0 / func_0022C870 (0x28C958 / 0x28C9E8) | reserve a slot, then splice the texture upload chain in with NEXT tags (0x20000000) | DMA only | C |

Count per frame: tens of VU1_addGSregister packets (ALPHA_1, TEST_1, ZBUF_1, CLAMP, FRAME...) around
every 2D and effect draw, plus ResetGsRegisters, PutDrawBuffer* and appendLargeSetup once each. The
main frame function is func_001F3D78 (L00 0x1F8938). It calls ResetGsRegisters, the letterbox, the
billboards, AA_BlurPass and gsRegsNormal.

### 1b. HUD / 2D sprites (screen space, FST UV, Z from the HUD arena +0xC)
All are `cnt n / DIRECT n` + one REGLIST tag (EOP, NLOOP 1). These are the "~65 sprites".
| exe func (L00) | what | GIF |
|---|---|---|
| func_00200468 HudSprite (0x23BE70) | sprite at (x,y) w×h, alpha, TEX0 from GetFrameTex, UV full tex | REGLIST {TEX0_1,PRIM 0x156,RGBAQ,UV,XYZ2,UV,XYZ2}, 0x7400..8001 / 0x5353106 |
| func_002008B8 (0x23C6B8) | same, coords already in 1/16 px | same |
| func_00200A90 (0x23C890) | same, explicit UV window | same |
| func_00200CA0 (0x23CAA0) | same, TEX0 passed in | same |
| func_00201190 (0x23CF90) | explicit UV corners | same |
| func_L00_0023C058 (shared C, 0x23C058) | HudSprite with UV = (w,h) | same |
| func_00200650 (0x23C1F8), func_L00_0023C458 (0x23C458) | quarter-turned quad | REGLIST {TEX0,PRIM 0x154 (tristrip, TME, ABE, FST),RGBAQ,(UV,XYZ2)×4}, 0xB400..8001 / 0x53535353106 |
| func_00200E38 (0x23CC38) | rotated quad | same as above |
| func_002014B8 / func_00201640 / func_002017C8 (0x23D2B8 / 0x23D440 / 0x23D5C8) | flat untextured: tag 0x4400..0001 / 0x4410 = REGLIST {PRIM, RGBAQ, XYZF2, XYZF2}; PRIM 0x41 (line+ABE, 002014B8) or 0x46 (sprite+ABE) | REGLIST, Z/F 0xFFFFF000.. or arg |
| func_001F5650 DrawRectOverlay (0x1FA460) | flat sprite pair (PRIM 0x144), descriptors D_00160920 {PRIM,RGBAQ} + D_00160930 {XYZF2}×NLOOP | REGLIST |
| func_001F62C8 DrawUIFrame, func_001F6410, func_001F5368 (stripes), func_001F54E8 (overlay with ZBUF toggles) | callers of DrawRectOverlay | – |
| func_001F5148 (0x1F9F58) letterbox | 2 strips PRIM 0x104 (tristrip FST, untextured), 8 XYZF2 | REGLIST |
| func_001F5E60 (0x1FAC70) | rotated textured sprite with pivot/flip | REGLIST TEX0,PRIM 0x154,RGBAQ,(UV,XYZ2)×4 |
| func_00232A00 (space.c, 0x2937D8) | float-ST quad (PRIM 0x54, Q 1.0), D_00160950 | REGLIST {TEX0,PRIM,RGBAQ,(ST,XYZ2)×4} |
| func_001FBAB8 (0x201510) | alpha sprite (ALPHA_1 0x64 around it, TEST 0x33003, FLUSH 0x13000000 each side) | PACKED A+D + PACKED |
| func_00205E70 map screen (0x247BC0), func_00232EF0 loading slides (0x293CC8) | menus/pause: sprites as above | – |
Callers: many HUD and menu functions (0x237B90..0x23BAB8, 0x278B00..0x282838; see callers.py output).

### 1c. Text / fonts
- FontPrint func_001F6668 (L00 0x1FB478, asm in the decomp's view: draw.c:2014 has C) calls
  DrawTexturedQuad **func_001F5800** (0x1FA610) once per glyph: REGLIST D_00160940
  {TEX0_1, PRIM 0x154, RGBAQ, (UV, XYZ2)×4}, 7 qw. That is 2 triangles per glyph.
- FontPrintWindow func_001F7070 (0x1FBEF8, asm) uses VU1_setScissor, then func_001F69F0 (0x1FB878)
  calls **func_001F5BB8** (0x1FA9C8) per glyph: the same with CLAMP_1 REGION_CLAMP to the glyph cell,
  then CLAMP_1 = 5 (D_00160960), and an off-screen reject.
- func_001F5988 (0x1FA798): the float version (callers 0x2A01F8, 0x2A0650).
- func_L00_001F3AF0 (0x1F3AF0, asm, "rendering_debug"): the debug text printer (A+D + REGLIST
  0x5353 sprites). Probably not drawn in normal play.
- Subtitles: func_001F4F90 calls FontSetWindow/FontPrintWindow + DrawUIFrame.

### 1d. Particles / effects / billboards (EE-built)
- func_001F9478 (0x1FEA88, drawquad.c:117) "append_billboard_batch": up to 16 glow billboards from
  D_0018EE00, projected on the EE, ALPHA_1 0x48 (additive) around them; REGLIST D_001609E0
  {CLAMP, TEX0 (GetEffectTex 0x13), PRIM 0x154, (RGBAQ, UV, XYZ2)×4}, Z from projection (Z-tested, not 2D).
- Sky shells (C skyfunc.c:666/760, SkyDrawShellTextured func_0022CA00 (0x28CB78) / SkyDrawShellGouraud
  func_0022CC40 (0x28CDB8)): a ref to D_0013D260 / D_0013D1F0 (ZBUF ZMSK, TEST, CLAMP, TEX1, ALPHA,
  PRIM 0x5B / 0x4B: triangles, textured or gouraud), then the asm emitters **func_0022D520** (0x28D698)
  and **func_0022D3F8** (0x28D570), writing through gp-0x5A80. Format: one PACKED NLOOP n A+D tag; per
  triangle TEX0 (when it changes) + 3×(ST (vitof12), RGBAQ (alpha per vertex, Q 1.0), XYZ2 (Z 0)). The
  vertices are transformed in SPR (0x70002000) by func_0022D2AC; tiles are DMA'd into SPR by
  func_0020C210. Per frame: the level's whole visible sky (it can be hundreds of triangles). These are
  probably part of the "940 EE triangles".
- func_L00_001EFFF0 (effects shared asm): a **false positive**. Its 0x50000000 is a sentinel in a
  collision ray walk (SPR, DMA ch 8). Not a GIF writer.
- func_001F4C30 (light quads): builds a packet on the stack, never sends it. func_001F7EF8 (drawquad)
  is not DIRECT (it has a recomp debug hook, UYA_QUADCHECK).

### 1e. Screen effects, the 662 alpha-only triangles, and what reads destination alpha
**Shadow volumes, with destination alpha as a 1-bit stencil.**
- Entry: func_0020DEB0 (L00 0x250C70, mobyfunc.c:741), called from func_0020E200. With D_0016003C set,
  it splices into the moby chain slot D_00160040 with NEXT tags: func_00228A58 (0x286D40, builds the
  command stream D_001C8A00), then func_00228860 (0x286B48, C pause.c:8986,
  "submit_graphics_setup_command_stream").
- func_00228860:
  1. Calls func_002284E8 (0x2867D0, asm).
  2. Sends a ref to the **FBMSK table**: D_001D81E0 or D_001D8250, chosen by D_0015EE80 (in L00
     0x1BC2E0 / 0x1BC350). The table holds FRAME_1 FBP 0x70 or 0x80, FBW 8, **FBMSK 0x00FFFFFF**
     (alpha only); FBA 0; CLAMP 0; TEX1 1; TEX0 0x664020E00 (or ...21000); ALPHA_1 0x8000000044.
  3. Per record (light direction*1000, two depths): func_00228400 calls func_00229098 /
     func_002291E8 (0x287380 / 0x2874D0); func_00229838; func_00228D20(0x70000000, ...) (0x287008,
     geometry into scratchpad).
  4. Two passes: func_00228458(2,1,2) and (1,0,2) (0x286740). Each sends a ref to TEST tables D_001D8120
     (L00 0x1BC280/0x1BC2B0): TEST_1 **0x51001** (ATE, ATST NEVER, AFAIL FB_ONLY, ZTE, ZTST GEQUAL) or
     **0x71001** (ZTST GREATER), then TEXFLUSH. Then **func_002298B0** (0x287B98, hand-written asm)
     draws from SPR, with colour = D_00160460[idx]:
     - side quads: PACKED {RGBAQ} + REGLIST NREG 10 {NOP, PRIM 0x54 (tristrip, TME, ABE), (ST, XYZ2)×4},
       one quad per silhouette edge (faces flagged at SPR 0x70003000);
     - caps: REGLIST {PRIM 0x55 fan} + {ST, XYZ2}×n, front and back (flags at 0x70003800 / 0x70003801).
     These are the ~662 alpha-only triangles. The alpha test always fails with AFAIL FB_ONLY, and
     FBMSK keeps RGB, so only destination alpha gets written, Z-tested.
  5. Restore FRAME_1 (FBMSK 0) via VU1_addGSregister(0x4C), ALPHA_1 = 0x2000000064 ((Cs-Cd)*0x20/128+Cd,
     i.e. 25% toward the source colour, black).
  6. **func_00228690** (0x286978, C pause.c:8937) is the pass that **reads destination alpha**:
     display_width/32 (16) full-height sprite strips PRIM 0x146, colour 0, TEST_1 **0x3D801** (DATE=1,
     DATM=1: only where dest alpha bit 7 is set; ZTST ALWAYS). It darkens the shadowed pixels.
  7. Restores TEST_1 0x5360B and ALPHA_1 0x44.
- So the destination alpha is consumed at once by func_00228690, inside the same splice, before
  anything else draws. Other writers of alpha (HUD alpha, FBA) do not seem to depend on it.

Other screen effects:
- FadeToBlack func_001F4E08 (0x1F9BB8): ALPHA via VU1_addGSregister(1, RGBAQ) + ref D_0013CED0 (TEST
  0x30000, PRIM 0x146, 16 sprite strips covering the screen), over several frames (transitions).
- emit_rgba_draw_packet func_001F55C0 (0x1FA3D0): RGBAQ + ref D_0013CD90 (the same strips, inset):
  a full-screen tint/clear.
- func_001FB908 (0x201360) clear w×h in 32-px strips (PRIM 0x146, ALPHA 0x4A), and func_00228690
  (the same shape).
- func_001FB608 (0x201060): **render-to-texture** setup. FRAME FBP = c>>13 of a (1<<a)×(1<<b) target,
  the TEX0 of it recorded in D_0015EFD0, a default draw env, TEST 0x30003, a black clear sprite.
  Callers func_001F7A50 (0x1FC8D8), func_0023A948 (0x2A0560); used with func_001FBAB8/func_001FB908 by
  the 0x239xxx-0x23Axxx menu functions.

### 1f. Display / back-buffer copy
- AA_BlurPass func_001FB848 (L00 0x2012A0): ref D_00151A00 (0x26 qw); func_001FB8A8 (L00 0x201300):
  ref D_00151C60 (0x29 qw). Both are bss tables built at run time (probably asm func_001FAB40). They
  are the frame copy / blur sprites (copying the drawn buffer with offset sprites). Callers:
  func_001F3D78 (the main frame) and func_00233308 / func_L00_002465F8.
- func_L00_002A21A8 (shared C): copies the frame buffer **out to EE** in 0x4000-byte strips
  (sceGsSetDefStoreImage func_00122818 + func_00122AD8: PATH3/GS readback, not DIRECT). Used by the
  pause/menu code (func_L00_002A1540, func_L00_002781D8).

### 1g. Texture uploads (IMAGE mode), which piece 5 replaces
All are BITBLTBUF/TRXPOS/TRXREG/TRXDIR as A+D, then an IMAGE tag (0x08000000_0000800n), then a ref with
DIRECT n to the pixel data.
- GIF paging (the texture cache), spliced by DoGifPaging/DoSkyGifPaging: func_0020C2F8 + the list
  uploader **func_L00_0024E798** (0x24E798, asm, patches 0x70-byte templates from a list at 0x16CE80).
  Called from DoGifPaging, DoSkyGifPaging, func_0021CE60, func_002209A0.
- DmaMobyTextures func_0020DC40 (0x250A00) calls func_00212258 (0x2565F8); DmaShrubTextures
  func_00229C08 (0x287EF0/0x296CD0); DmaTfragTextures func_002344D8 (0x295330);
  BuildShrubTextureDma func_0022B648 (0x289930); BuildTfragTextureDma func_L00_002963D8;
  BuildTieTextureDma func_002383D8 (0x298750). Each is followed by VU1_texFlush.
- Hud_sendTexture func_00201348 (0x23D148): sceGsSetDefLoadImage (func_00122630) into the chain + a
  ref of the image (caller func_001FF958).
- func_001F7680 (0x1FC508, asm): a full-screen 512-wide image in 0x80-row chunks to D_0015EF84>>8
  (caller func_L00_001F92E8). func_00201AF0 draw_boot_image is the C twin.
- func_L00_002A2258 (shared C, translated): 64-wide strips in 0x4000 chunks (pause/menu restore of
  the saved frame).
- setImageTag func_0023C5E0 (0x2F0148, movie). The FMV path already bypasses this
  (gsgl_present_bgra).

## 2. C vs asm
- C in the decomp (src/game, the exe copy, which in levels runs translated from the level's bytes):
  everything in 1a, 1b and 1c except FontPrintWindow (asm func_001F7070); 1d billboards and the sky
  shell drivers; shadow func_00228860, func_00228458, func_00228690, func_0020DEB0; FadeToBlack,
  emit_rgba, framebuf.c, map, space.
- Shared overlay C: func_L00_0023C058 (compiled from C in L00); func_L00_0023C458 and func_L00_002A2258
  (inline asm, so translated); func_L00_002A21A8.
- asm only (INCLUDE_ASM / no C found): FontPrintWindow func_001F7070, func_001F7680, func_002284E8,
  func_002298B0 (shadow emitter, hand-written, SPR), func_0022D3F8/func_0022D520 (sky emitters),
  func_00212258, func_L00_001F3AF0, func_L00_0024E798, BuildTfragTextureDma, func_0023C5E0,
  func_001FAB40. func_00228D20, func_00228A58, func_00229098 and func_002291E8 were not checked.
- For hooking this makes no difference: HOST_HOOKS works on any catalogued function by name, in every
  level. The C gives a readable spec for writing the host version.

## 3. What host/src already offers
- native_regs(addr[], val[], n) sends dlist op 1 (GS register writes) = the material (TEX0, TEX1,
  CLAMP, ALPHA, TEST, FRAME, ZBUF, PRIM...).
- native_kick(pk, qwc) converts a PATH1 packet (native_dlist.c convert()) into dlist ops 1/2/3. It
  only accepts:
  - PACKED tags whose registers are all A+D (to op 1 writes; vertices kicked by the XYZ2 writes);
  - PACKED NREG 3 {ST, RGBAQ} (either order) + XYZF2/XYZ2 (op 2 strip vertices: x,y 12.4, z, rgba,
    fog, ADC, S T Q floats).
  - Anything else (REGLIST, UV, other register orders, IMAGE) goes as GIF, logged once. **None of the
    EE 2D packets fit**: they are REGLIST with UV (FST) or ST, and RGBAQ once per primitive.
- Op 2 has no UV (FST integer texel) field; the vertex carries ST/Q only.
- native meshes (ops 4-9: create/update/draw with a 32-float transform) are 3D, kept geometry. Not
  meant for per-frame 2D.
- gs.c gs_dlist_ (gs.c:2516) feeds ops into the same GS state machine (uya_gs_write, vertex_kick).
  The GL renderer (gs_gl.c) batches by state and already handles FBMSK (colour mask), DATE/DATM
  (copy_rect of the target for reads), FBA, ABE.
- UYA_SRCSTATS (gs.c:2325) keys by uya_gs_src. All EE packets currently show as one row,
  "PATH2 (VIF1 DIRECT)" (vif.c:206).

What a small native 2D batch API needs, to cover the call sites exactly:
- Primitives: SPRITE (6), TRISTRIP of 4 (quad), TRI/FAN lists (sky, shadow caps), LINE optional.
- Vertex: x,y in 12.4 GS window coords (already offset by D_0013E600[4]/[5], minus 8), Z 24/32-bit
  (HUD arena +0xC, 0xFFFFF0, 0xFFFFF3, projected for billboards, 0 for sky), RGBA per vertex or one per
  primitive, texture as **UV (FST, 12.4 texels)** or **ST+Q floats**, optional fog.
- Material per batch = a set of GS regs: TEX0_1 (TBP/TBW/PSM/TW/TH/TCC/TFX/CBP/CPSM/CSM/CLD), TEX1_1,
  CLAMP_1 (incl. REGION_CLAMP for glyphs), ALPHA_1, TEST_1 (incl. ATE/AFAIL, DATE/DATM), FRAME_1
  FBMSK, ZBUF_1 ZMSK, SCISSOR_1, FBA, COLCLAMP, PRIM bits (TME, ABE, FST, IIP, FGE, AA1). The simplest
  exact form is "the GS registers the call site writes, then vertices": a dlist op like
  `{10, prim, nverts, flags}` + vertices {x|y<<16, z, rgba, uv or s,t,q}, after op-1 writes for the
  material. That is op 2 plus a UV/FST variant and an explicit PRIM.
- Placement: drawn at the right point of the chain, via a marker CNT tag (like NATIVE_SHRUB_MARK)
  naming a run of recorded 2D commands.
- Textures: TEX0 addresses into GS memory until piece 5 (TEX0 from GetFrameTex/GetEffectTex/
  D_0015EFD0). Render targets (func_001FB608) and frame copies (AA_BlurPass) need "draw into FBP x" /
  "sample FBP x" in the API, or must stay GS-level for now.

## 4. Recommended order (with rough estimates)
1. **Source tags for DIRECT** (0.5-1 day). In vif.c's DIRECT case, look the data pointer up in
   (a) a small table of static GS tables (D_0013D0C0, D_0013CED0, the FBMSK and TEST tables, D_00151A00...,
   resolved per level), and (b) ranges recorded by a recomp entry/exit hook. Add a TRACE-like list in
   recomp.py that records `(chain ptr on entry, on exit, name)` for the functions in section 1. Set
   uya_gs_src = "PATH2 <name>". This gives a per-call-site UYA_SRCSTATS row and confirms the counts
   (662 shadow triangles, ~65 HUD sprites, glyphs, sky).
   - Cheaper first cut: classify by the first GIF tag's REGLIST signature (0x5353106 HUD sprite,
     0x53535353106 quad/glyph, 0x5252520F shadow side quads, NREG 1 A+D VU1_addGSregister...).
2. **Extend convert() and use it at the DIRECT sink** (1-2 days): REGLIST tags, UV/FST, RGBAQ once per
   primitive, PRIM written via the tag or A+D, sprites, and an "op 2 with UV" form. With vif.c DIRECT
   calling native_kick instead of uya_gif_packet, almost all EE 2D packets become dlists with no game
   hooks (A+D state writes become op 1). This is the safe, large first win, and testable with
   cmp_frames / gl_compare.
3. **Native 2D batch API + hooks for HUD and text** (2-4 days): host versions of HudSprite family
   (~12 functions), func_001F5800 / func_001F5BB8 / func_001F5988 / func_001F5E60, DrawRectOverlay,
   VU1_addGSregister / VU1_setScissor (record the state change). Use HOST_HOOKS (hook by exe name,
   every level), which writes one marker into the chain per run and records commands in a host ring.
   Batch adjacent glyphs/sprites that share a material.
4. **fx 221571 / 57843 to batches** (1-2 days): they already compute everything natively. Emit 2D/3D
   batch ops instead of building GIF and calling native_kick. First check whether convert() accepts
   their packets today (221571 particle tags carry A+D ALPHA/TEX0 + RGBA + (ST, XYZ)×4, mixed registers,
   which likely fails convert(); look for the "packet of another shape" log line).
5. **Sky** (1 day): hook func_0022D520 / func_0022D3F8 (asm, SPR input) or convert their A+D triangles
   in step 2. A native version needs the SPR transform output (func_0022D2AC).
6. **Shadow volumes** (2-4 days, hardest): replace func_00228860's whole splice with a native pass.
   Draw the volume triangles (from func_00228D20's SPR output, or re-derive them) into a stencil (on
   GL: a real stencil buffer, or keep dest alpha), then the darken pass. Keep the GS form first (step 2
   converts them), and only then move to a GPU stencil path.
7. Leave IMAGE uploads (1g) as they are for piece 5. The 2D API must accept TEX0 as-is until then.

Total: about 8-14 working days for 1-6.

## Hardest parts
- **GS state between packets**: the 2D packets rely on state set elsewhere, in chain order:
  ResetGsRegisters (D_0013D0C0/D_0013D010), the draw env refs, VU1 microprograms' own A+D writes, and
  VU1_addGSregister calls (ALPHA_1 0x48/0x64/0x44, TEST_1 0x5360B/0x360B/0x3D801/0x51001, ZBUF ZMSK
  toggles 0x4E, CLAMP 8, SCISSOR via setScissor in FontPrintWindow, FBA). A native batch must snapshot
  the material it needs, or the marker approach must keep the exact interleave with PATH1. PRIM's
  context bit and PRMODECONT 1 also matter.
- **Destination alpha**: the shadow pass writes alpha through FBMSK 0xFFFFFF with an always-failing
  alpha test (AFAIL FB_ONLY), with Z GEQUAL then GREATER, in two passes with different colours
  (D_00160460). Then DATE/DATM strips read bit 7. On the GL side this needs the target's alpha kept
  exactly between the passes, and DATE emulation (gs_gl.c copies the rect). A clean native version
  wants a stencil buffer. The FRAME FBP (0x70 / 0x80) is chosen by the double-buffer flag D_0015EE80.
- **Texture uploads into GS memory**: HUD/glyph/effect textures arrive via GIF paging uploads in the
  same chain just before use (DoGifPaging splices). TEX0 values (TBP/CBP) are GS-memory addresses.
  Render-to-texture (func_001FB608, TEX0 in D_0015EFD0) and the frame-buffer readback
  (func_L00_002A21A8) and restore (func_L00_002A2258, func_001F7680) tie piece 4 to piece 5.
- Per-level addresses: the chain pointer and all data move per level (0x161280 vs 0x161240); the
  HOST_HOOKS relocation lists handle it.
