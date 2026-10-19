# Piece 5 research: where every texture comes from

Read-only research (decomp + disassembly of SCES_509.16 and the level overlays), 2026-10-18.
Addresses are the exe copies unless marked; each level has its own copy (config/overlays/functions.tsv,
e.g. level 3: func_00203958 = 0x22F070, BuildTieTextureDma = 0x284FB0, level loader func_L00_00244AE0 =
0x232D20). Data symbols are exe names; level copies move (level 3 tables noted where checked).
"Block" = GS 256-byte block (TBP/CBP units). Confidence marks: (verified) = read in code;
(inferred) = follows from code but not checked at run time; (check) = needs a host log to confirm.

## 0. Summary

Every world texture (tfrag, tie, shrub, moby) is described by a 16-byte **texture descriptor** in the
level file and a 32-bit **texture table entry** (EE address of the pixels | log2(width) << 28). GS memory
holds three things:

| GS area | written | content |
|---|---|---|
| D_0015EF8C .. D_0015EF78 ("resident") | once, at level load, func_00203958 | palettes (16x16 CT32 = 256 colours, CSM1) and small mip levels (2, 3) as PSMT8; shrub billboard chains whole |
| D_0015EF78 .. 0x3FFB ("window", Kerwan 0x3717..0x3AFF) | every pass, every frame | level 0 and level 1 (PSMT8) of the textures near enough, streamed from EE by REF; effect textures + their palettes (GIF paging) |
| 0x3FFB (8x8 CT32) | once, initonce.c | constant 0x80808080 for untextured materials |

TEX0's TBP0 and MIPTBP1's TBP1 are **0 in the class data**; each frame the builder writes the window
blocks it chose into a remap table and Patch*Gifs ORs them into the low 14 bits. A texture whose level 0
is not streamed keeps TBP0 = 0 (never patched) or a stale value from an earlier frame: that is the
"TBP 0" class. The level actually needed is decided from the object's distance, consistent with the
LOD the GS computes (section 2).

## 1. Resident mip chains

### 1a. Level load
- Level loader func_L00_00244AE0 (asm; level 3 0x232D20) does what the space-transition loader
  func_001EABE8 (transition.c, readable C) does:
  1. `func_00203958(base, count, list)` (loaders.c, C): `D_0015EF78 = D_0015EF74 = D_0015EF8C`, then for
     each 16-byte Chunk `{int type; int w | h<<16; int pad; int offset}` uploads base+offset with
     sceGsSetDefLoadImage/ExecLoadImage (func_00122630/func_00122958) at block `D_0015EF74 >> 8`:
     - type 0x13: PSMT8 w x h, DBW = max(w/64, 1), cursor += max(w*h, 0x100);
     - type 2: 16x16 PSMCT16 (palette), cursor += 0x200;
     - type 0: 16x16 PSMCT32 (256-colour palette), cursor += 0x400.
     Finally `D_0015EF78 = cursor` = start of the streaming window. (verified)
  2. Four texture tables, one per kind, entry = `D_0015F50C(texture data base) + desc.offset +
     (log2(desc.width) << 28)` (log2 = func_001F9968):

     | kind | table (exe) | count | descriptors in level file | level 3 table |
     |---|---|---|---|---|
     | tfrag | D_001E0C00 | D_00160F94 | hdr+0x34 (n hdr+0x30) | 0x1C4D80 |
     | moby | D_001B5D00 | D_00160008 | hdr+0x3C (n +0x38) | 0x199E80 |
     | tie | D_001E2900 | D_00161064 | hdr+0x44 (n +0x40) | 0x1C6A80 |
     | shrub | D_001D8840 | D_001604EC | hdr+0x4C (n +0x48) | 0x1BC9C0 |
     (verified in the transition loader and the level-3 disassembly at 0x232F34..0x2330E4)
  3. Class setup writes the material GIF words once: func_00204918 (tfrag), func_00203E78 ->
     func_00203B70 -> func_002035B0 (moby), func_00203F68 (tie), func_00204340 (shrub).

### 1b. Texture descriptor (16 bytes, level file) (verified from func_00203F68 / func_002035B0)
| off | type | meaning |
|---|---|---|
| +0x0 | u32 | pixel offset from the texture data base (level 0, then level 1 right after, w*w bytes) |
| +0x4 | s16 | width (TBW0 = max(w>>6,1), TBW1 = max(w>>7,1)) |
| +0x6 | s16 | height |
| +0x8 | s16 | number of mip levels (MXL = n-1, so at most 3: MIPTBP2 is never used) |
| +0xA | s16 | palette block, relative to D_0015EF8C>>8 (resident CT32 16x16) |
| +0xC | s16 | level 2 block, relative to D_0015EF8C>>8 (resident PSMT8) |
| +0xE | s16 | level 3 block, relative to D_0015EF8C>>8 (resident PSMT8) |

### 1c. GS words the class setup writes (verified)
TEX0 (all four kinds): `TBP0 0 | TBW0<<14 | PSM 0x13 | log2(w)<<26 | log2(h)<<30 | TCC 1 | CBP=(EF8C>>8)+pal
<<37 | CPSM 0 | CSM 0 | CSA 0 | CLD 4` (CLD 4 = load the CLUT only if CBP != CBP0).
MIPTBP1: `TBP1 0 | TBW1<<14 | TBP2=(EF8C>>8)+lv2 <<20 | TBW2 1 | TBP3=(EF8C>>8)+lv3 <<40 | TBW3 1`.
TEX1: `LCM 0 | MXL n-1 | MMAG 1 | MMIN, MTBA, L from the record | K from the record (bits 32..43)`.
CLAMP-like qword: the record's wrap word, with the **global texture index in bits 24..31** (MINV field,
harmless in REPEAT).

Per kind, where the words are and which halves Patch*Gifs writes (remap table entry = {short tbp0,
short tbp1}, 0 = leave as is):
- **tie** (tiefunc.c PatchTieGifs func_00236A98; 0x50-byte records at tie+0x2C, count tie+0x23):
  +0x00 TEX0 (<- tbp0), +0x10 TEX1 (K at +0x14.. see func_00203F68: K = rec+0x10 word, MMIN/L = rec+0x14
  word << 6), +0x20 MIPTBP1 (<- tbp1), +0x30 CLAMP, **index byte +0x33**. Remap D_001E2D00. The class's
  local->global index list is copied to D_001E3900 + 16*slot (0xFF-terminated).
- **tfrag** (PatchTfragGifs func_00234620; 0x50-byte records from the chunk list D_001E1200):
  +0x00 TEX0 (<- tbp0), +0x30 MIPTBP1 (<- tbp1), **index byte +0x23**. Remap D_001E0F00, which
  BuildTfragTextureDma builds in scratchpad 0x70002000 and DMAs out (fromSPR, 0x30 qwords).
- **shrub** (PatchShrubGifs func_00229D48; 0x40-byte entries per shrub class item): +0x30 TEX0 (<- tbp0),
  +0x20 MIPTBP1 (<- tbp1), **index byte +0x13**. Remap D_001D8C40. Shrub **billboards** (the far impostor,
  func_00204340's 4th argument) use another descriptor: +0 w, +2 h, +4 levels, +6 palette, +8/+A/+C/+E
  level 0..3 blocks, all resident (whole chain uploaded at load, never streamed). (inferred from
  0x20448C..0x204530)
- **moby** (PatchMobyGifs func_0020DD48 via the class joint lists; 4-qword material from func_002035B0):
  q0 TEX1, q1 CLAMP + **index at byte +0x13 of the material**, q2 TEX0 (<- tbp0, entry+0x30),
  q3 MIPTBP1 (<- tbp1, entry+0x40). Remap D_001B6100. Material index -1/-2/-3 = SetUpVisGifViewer
  func_00203808 cases: -1 is the 8x8 CT32 at 0x3FFB (TEX0 0x80000004CC007FFB), -2/-3 are the 128x128
  PSMT8 rows D_0019E7C0/D_0019E7D8 (TBP/CBP from the level header +0x90..0x9C, built by the loader).

### 1d. (TBP, level) -> EE pixels and palette
For a draw of kind k with texture index t (the index byte above):
- level 0: `table_k[t] & 0x0FFFFFFF`, w = 1 << (table_k[t] >> 28), uploaded as a **square** w x w;
- level 1: level-0 address + w*w, (w/2)^2 bytes;
- levels 2, 3 and the palette: resident blocks `(EF8C>>8) + desc.{C,E,A}`; their bytes are the Chunk
  images func_00203958 uploaded (EE source = Chunk base + offset). Whether that EE copy survives the load
  is not established (check); capture it at the upload.
- Palette: 256 x 32-bit, CSM1 storage, so entry i is at chunk word `(i & 0xE7) | ((i & 8) << 1) |
  ((i & 0x10) >> 1)`.
Reverse (block -> texture) without the index: within a pass the remap table maps t -> {tbp0, tbp1};
resident blocks map back through the descriptors. On the "CT32 read as PSMT8" observation: per the
code, type 0 Chunks are exactly 16x16 CT32 (1 KB) = palettes; mips come as type 0x13. If the host sees
mip levels read from blocks last written by a CT32 upload, log func_00203958's Chunk list at level load
(type, w, h, block) and compare with the descriptors' +0xA/+0xC/+0xE (check).

## 2. The TBP 0 level-0 "trick"

What the draws carry: TEX0 TBP0 = 0 (class data never patched for that texture) or a stale block from an
earlier frame; MIPTBP1 TBP1 likewise when level 1 was not streamed. TEX1 is the class value: LCM 0
(LOD from Q), MXL = levels-1 (<= 3), MMIN from the record (mipmapping modes), K/L per record. CLD 4.

Which levels get streamed (Build*TextureDma, verified for tie/moby/shrub, tfrag uses its own test):
- per visible object i: `d = distance metric` (ties SPR 0x70003200[i], 0x7FFFFFFF = not visible;
  shrubs SPR 0x70003100[i]; moby classes SPR 0x70003A00[slot], 0x40000000 = none) and
  `T = threshold` (ties D_001E3100[i] = (int)(tie+0x48 * 1024.0) set in func_00203F68; shrubs D_001D9040;
  mobys D_001B6500 = class+0x2C).
- `2T - d <= 0`: nothing (levels >= 2 only); `T - d <= 0`: flag 1 = level 1 only; else flag 3 = levels 0
  and 1. Flags are ORed per texture into SPR 0x70003E00 (tie/moby) / 0x70003600 (shrub) / 0x70003C00
  (tfrag; ComputeTfragTextureUsage func_00235EF0 stores 1 or -1 from a VU0 screen-size test of each
  tfrag chunk).
The doubling (T for level 0, 2T for level 1) is exactly one LOD step: with L = 0, LOD = log2(1/Q) + K,
and each doubling of depth adds 1. So the game intends: level 0 is sampled only nearer than T, level 1
only nearer than 2T; K per record is set so LOD(T) ~ 0..1 (inferred; the K values are level data).

Can level 0 be sampled when TBP0 = 0? Only if K/T are inconsistent or the metric d is larger than the
nearest vertex's depth. LOD is per pixel, but Q is interpolated linearly, so the **minimum LOD of a
primitive is at one of its vertices** (log2 is monotone). A per-primitive check is exact:
`lodmin = min_v(-log2(Q_v)) * 2^L + K`; level 0 is reachable iff lodmin < 1 (MMIN 4/5, trilinear) or
< 0.5 (MMIN 2/3) or <= 0 (magnification). The host today decodes levels 0..MXL for every primitive
(gs.c ~1652), which is why 4382 lookups/vsync go to TBP 0 and 5710 to unstreamed level-1/resident
levels. A cheap first experiment: compute lodmin/lodmax per primitive, decode only levels in range, and
count primitives that would sample a level whose TBP is 0/stale (expect 0; any hit is a game edge case).
Also: when the window overflows (cursor would pass 0x3FFB) the builder stops and the rest keep 0/stale
TBPs (wrong textures on PS2 too); a native path would draw them correctly.

## 3. Streamed textures

### 3a. Path (verified)
Per pass the draw function sets `D_0015EF74 = D_0015EF78` (window start), runs the Proc, then
Dma*Textures (tfragfunc.c func_002344D8, tiefunc.c func_00236958, shrubfunc.c func_00229C08, mobyfunc.c
func_0020DC40) splices NEXT tags around a block built by the asm builder
(BuildTfragTextureDma func_L00_002963D8 / func_00236060, BuildTieTextureDma func_002383D8,
BuildShrubTextureDma func_0022B648, moby func_00212258), then VU1_texFlush. So every pass reuses the same
window; the window's contents are valid only between that pass's uploads and its draws.
Draw order of passes: DrawTfrag func_002346C0, DrawTies_1/2 func_00236BE0/func_00236CA8 (two passes, odd
and even ties, each its own uploads and a saved copy of the remap, func_00236B58), DrawShrubs
func_00229E50, mobys (DrawMobysCleanUp -> DmaMobyTextures), sky (Setup/DoSkyGifPaging), then
SetupGifPaging/DoGifPaging for effects/HUD.

Builder (all four are the same asm shape, 0x70-byte GIF templates from $gp-0x64A0..-0x6460):
1. clear the flag bytes; per visible object set the flag bits of each texture in its index list (sec. 2);
   write the visible list (-1 terminated) for Patch*Gifs.
2. per texture t with flags: clear remap[t]; `v = table[t]`, `log2w = v >> 28`, `ee = v & 0x0FFFFFFF`;
   - flag & 2: upload level 0: BITBLTBUF DBP = cursor, DBW = 1 << (log2w-6) (template value if
     log2w <= 6), DPSM 0x13; TRXREG w x w; TRXDIR 0; IMAGE NLOOP qwc = w*w/16, EOP; DMA REF
     (0x30000000 | qwc, ee) + VIF DIRECT qwc. remap[t].tbp0 = cursor; cursor += w*w/256.
   - flag & 1 (always set with 2): same for level 1 from `ee + w*w`, size w/2.
     remap[t].tbp1 = cursor; cursor += (w/2)^2/256.
   - stop when cursor > 0x3FFB. Returns the byte size; >0x400000 prints an overflow warning.
3. Patch*Gifs (end of the frame build, after VU1_syncChain for that class, draw.c ~930-962) ORs the
   remap blocks into the class records, which the next chain DMA reads (one-frame lag, as everywhere).
Uploads are square w x w; a texture with h < w uploads w rows (the extra rows are the following EE bytes)
while TEX0 TH = log2(h). This is the likely source of the 75 "PSMT8 upload of another size" lookups.

### 3b. Palettes (verified)
Palettes are never streamed: TEX0 CBP points at the resident CT32 16x16 block (desc+0xA), CPSM 0, CSM 0
(CSM1 layout), CSA 0, CLD 4. With CLD 4 the GS reloads the CLUT only when CBP changes.

### 3c. The "CBP at the frame buffer" trick (verified)
VU1_texFlush func_00234E80 REFs D_001DF180: A+D `TEXFLUSH 1` and `TEX0_1 = 0x8000000511304000`
= TBP 0, TBW 1, PSMT8, 16x16, TCC 1, **CBP 0, CLD 4**. No primitive follows. Since CBP 0 differs from
CBP0, the GS loads a CLUT from block 0 (the frame buffer) and sets CBP0 = 0, so the next real draw's
CLD 4 always reloads its palette. It is a CLUT-cache invalidation after each upload batch (one per
Dma*Textures / DoGifPaging, ~9 a vsync = the ~2,300 per 250 vsyncs). The loaded data is never used.
Native: treat "TEX0 with CBP 0 and no draw" as "forget the current CLUT"; nothing to read.

### 3d. Effects / HUD (GIF paging) (verified)
GetEffectTex func_001F4868(i): slot D_0018D540[i] (16 bytes: +0 cached TEX0 for this frame, +8 pixel
offset/16, +0xA palette offset/16, +0xC log2 w, +0xE log2 h, offsets from D_0015F560; filled by
func_00203038). If not yet this frame: CBP = cursor (palette 0x400), TBP = cursor + 0x400, PSMT8,
TBW = 1 << max(log2w-6, 0), CLD 4; queues {palette EE, CBP, pixels EE, w, h, TBP} in D_0018D140 (max 64).
func_0020C2F8 (DoGifPaging) emits the uploads: palette 16x16 (DPSM from the queued +4, 0 = CT32), then
pixels w x h PSMT8. SetupGifPaging func_001F4630 clears the slots each frame. Native key: slot i (or the
two EE addresses); the kept-upload path already finds these (exact uploads).

## 4. CT32 (PSM 0) lookups

Candidates found in code (the 250/vsync split between them needs a log by TBP):
1. **0x3FFB, 8x8 CT32**, uploaded once by initonce.c (func_00122630(buf, 0x3FFB, 1, 0, 0, 0, 8, 8)) from
   D_001942C0 filled with 0x80808080: the "no texture" material (SetUpVisGifViewer index -1, also used
   by the window limit: builders stop at 0x3FFB). Constant grey, alpha 0x80.
2. **Frame buffer read as a texture**: the AA/blur packets D_00151A00 (AA_BlurPass func_001FB848, 0x26
   qwords) and D_00151C60 (func_001FB8A8, 0x29 qwords), built at init by func_001FAB40 (asm) from the
   draw environment: TEX0 = FBP*32 | FBW | frame PSM | TW=TH=1024 | TFX decal, then 16 vertical strip
   sprites with offset UVs. AA_BlurPass runs when D_0018A3B0[0x12] is set (draw.c ~879, also
   transition.c, vendor). This reads what was just drawn (render-target semantics).
3. **Render to texture**: func_001FB608 (framebuf.c) sets a (1<<a) x (1<<b) CT32 target at GS address c
   and stores its TEX0 in D_0015EFD0 (TBW = 1 << max(a-6,1), PSM 0, TCC 1); callers func_0023A948
   (vendor.c, c = page-aligned below 0x3FF000) and draw.c ~2360 (D_001519EE). Read back by
   DrawTexturedQuad(..., D_0015EFD0) in pause_00277208.c and vendor_0029FD68.c.
4. Frame save/restore is not a texture read: func_L00_002A21A8 copies the frame to EE, func_L00_002A2258
   and func_001F7680 upload it back into the frame buffer (local -> host and host -> local transfers).
5. 16x16 CT32 Chunks are palettes; reading them as textures would be a bug (check none do).

## 5. Recommended design

### 5a. Native identities
| class | key | pixels | palette | invalidation |
|---|---|---|---|---|
| world (tfrag/tie/shrub/moby) | (kind, global index t) -> one object with the whole mip chain | L0 = table_k[t] & 0x0FFFFFFF (w x h, use h, not the square upload), L1 = L0 + w*w, L2/L3 from the captured resident Chunk at desc.+C/+E | captured Chunk at desc.+A (CSM1 unswizzle) | level load (func_00203958 / the loader rebuilding table_k); optionally a content hash of L0/L1 EE bytes per frame if animated textures exist (check: compare table_k entries and a hash across frames) |
| shrub billboards | (shrub class, billboard) | resident Chunks at +8..+E | +6 | level load |
| moby special materials | -1: constant 0x80808080 8x8; -2/-3: the header +0x90..0x9C images | resident | resident | level load |
| effects / HUD (GIF paging) | (slot i) or (pixel EE, palette EE, w, h) | EE at D_0015F560 + 16*slot.+8 | EE at 16*slot.+0xA | none per level; hash if the EE source can change |
| HUD images (Hud_sendTexture, sceGsSetDefLoadImage) | EE address + size + PSM | EE | as uploaded | per upload (content hash) |
| frame / render targets | (target page, PSM, width) | GPU target | - | every draw into it |

How a renderer gets the key:
- Preferred, for the native Tie/Shrub/Moby/Tfrag paths: read the index byte from the material qword it
  already walks (tie +0x33, tfrag +0x23, shrub/moby material +0x13) and the kind from the path; bind the
  whole chain, ignore TBP0/TBP1/CBP. TEX1 (MXL, MMIN, L, K) and CLAMP stay as the game wrote them.
- Fallback for anything still drawn through GS registers: a block -> key map maintained from the uploads
  instead of vram decoding: the Build*TextureDma REFs give (EE address, w, level, DBP) per pass (hook the
  four builders or read their 0x70-byte packets: DBP at +0x24, DBW +0x26, DPSM +0x27, TRXREG +0x30/0x34,
  REF address +0x64); the resident map comes from the Chunk list at load. A texture lookup then needs
  only (TBP, level, TBW, TW, TH) -> (EE pointer, crop). This is the current kept-upload path without the
  copy, plus resident records that never get evicted.

### 5b. Quick wins in the current host (no redesign)
1. Decode only the levels a primitive can reach (lodmin/lodmax from vertex Q, sec. 2): removes most of
   the 4382 TBP-0 and many of the 5710 level-1+ lookups.
2. Keep resident uploads (blocks below D_0015EF78, written at level load) in a separate store, not the
   256-entry ring, so levels 2/3, palettes and shrub billboards are found by upload.
3. Accept a texture that is a prefix crop of a kept PSMT8 upload: same TBP and TBW, tw <= upload w,
   th <= upload h (texel addresses depend only on TBP, TBW, x, y). Covers the non-square textures.
4. CLUT: take palettes from the kept CT32 16x16 uploads; treat the VU1_texFlush CBP-0 load as a no-op
   invalidation.

### 5c. What still needs GS-memory semantics
- Reads of rendered memory: the AA blur (frame buffer as texture), render-to-texture (D_0015EFD0),
  destination alpha / DATE in the shadow pass (piece 4 notes), and any texture whose TBP lands in a
  frame or Z page. These need GPU targets that can be sampled, keyed by (page, PSM, width), with
  aliasing rules only between targets and the textures that read them.
- Frame save/restore transfers (func_L00_002A21A8 out, func_L00_002A2258 / func_001F7680 in).
- The CLUT state machine only to the extent of CLD 4 (current CBP0); all palettes come from EE.

### 5d. Risks
- Stale/zero TBPs are harmless only if the game's LOD logic holds; binding the full chain natively can
  show level 0 where PS2 sampled garbage (an improvement, but pixel compares against the GS path will
  differ there). Window overflow (cursor past 0x3FFB) is the same case.
- EE pixel data assumed static during a level: unverified. Animated textures (water, screens) could
  rewrite EE pixels or table entries at run time; a per-frame hash of the bytes the builders REF would
  catch it cheaply (the game reads them every frame anyway).
- Resident Chunk EE source may be freed after load: capture at func_00203958 time.
- Level copies: every function and table above has per-level addresses; use functions.tsv and the
  HOST_HOOKS relocation, never exe addresses.
- The two tie passes (DrawTies_2, D_0015EE80 double-buffer mode) save and restore the remap/visible lists
  (func_00236B58): a TBP-based fallback must key by pass, not by frame.
- Shrub billboard descriptor and the tfrag usage test were read from disassembly only; confirm the field
  meanings with a load-time log before relying on them.
