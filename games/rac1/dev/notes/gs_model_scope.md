# The GS model: what draws depend on it, and what replacing it takes (19 October)

The objection: the GL renderer is a model of the PS2's Graphics Synthesizer running on the GPU, and the
native renderers feed it instead of bypassing it. That is correct. gs_gl.c's own header says "the GS's
drawing on the GPU, PCSX2-hardware-renderer style": gs.c keeps GIF, registers, GS local memory,
transfers, palettes and the texture cache, and gs_gl.c runs the GS pixel pipeline in a shader, with
targets standing for GS buffers by block address and page-level coherence with GS memory.

## How it was measured

UYA_GSCENSUS=a,b (gs.c, testing aid, off by default): for each draw source (the GIF/draw-list source
name), from vsync a to b: primitives (vertex kicks), native mesh triangles, the share using each GS
feature, distinct materials (draw state less addresses and scissor), blend equations, targets, and
transfers by direction. Run on the GPU (Windows build, UYA_MTGS=0, UYA_MEMSURVEY=200), levels 1, 3, 9
and 13, walking, vsyncs 1400-1600.

## Results (per vsync)

| source | level 3 | level 9 | materials | GS features used |
|---|---|---|---|---|
| native tie (+ clip) | 25.9k tris | 29.5k tris | 4 | paletted, mipmapped, bilinear, fog, standard blend, alpha test with AFAIL |
| native moby (+ clip) | 22.7k tris | 26.5k tris | 5 | the same (mip 73-82%) |
| native shrub (+ clip, billboards) | 16.8k tris + 78 prims | 8.2k tris + 253 prims | 2 | the same |
| native terrain | 7.3k tris | 3.8k tris | 4 | the same |
| native fx 57843 (strips) | 412 prims | 3,841 prims | 2-4 | bilinear, fog, additive blend (50-94%), some paletted |
| native fx 221571 (effects) | 467 prims | 470 prims | 2 | paletted, bilinear, blend (9-57% additive), alpha test |
| game GIF (PATH2: HUD, 2D, screen effects) | 952 prims | 1,379 prims | 12-16 | everything below |

The game's own GIF data is where the GS semantics really sit: the frame used as a texture (79-86% of its
prims), frame masks (76-83%: writing some channels only), destination alpha test (1%), no Z test
(13-16%), other blend equations (2-9%), UV coordinates, region clamp.

Across all sources: 7-9 blend equations, all of the form (Cs - Cd or 0) * (As or FIX) + Cd; 2 targets in
gameplay (FBP 0000 and 1000, PSMCT32, FBW 8, with Z). Transfers: 66-140 uploads a vsync into GS memory
(0.6-1.3 MB: the texture pager re-uploads each frame); nothing read back to the EE, no copies within
GS memory.

Memory survey: most texture reads are "mixed" blocks. The game uploads paletted textures as 32-bit
transfers and reads them as 8-bit (2-7 million draw states over the run: uploaded as format 00, read as
13), and points level 0 of distant mip chains at the frame buffer (TBP 0000), sampling only the lower
levels.

Caveats: "texture not an intact upload" in the census checks level 0 only, so it overstates sampling
from GS memory for mipmapped textures (level 0 is often the frame buffer). Piece 5's figure stands:
about 38 GS-memory lookups a vsync, and 0-774 px per level that really depend on it (stale TBPs). The
"GL GS (per vsync)" line the census prints is averaged over the whole run, not the window: ignore it.
Four levels and one walking window: menus, cutscenes and boss fights are not covered.

## What replacing it takes

1. **A native draw API for the native renderers (the bulk; medium).** About 97% of what is drawn
   (65-73k mesh triangles a vsync against ~2-6k primitives) comes from ten native sources using 2-5
   materials each and a narrow feature set. Replace native_regs + GS state with a material description
   (texture and palette, mip levels, filter, fog, blend mode, alpha test and its fail mode, Z test and
   write) that the GL side draws directly, with a shader per material rather than the general GS pipeline
   shader. The meshes already are host objects. Textures are still resolved from the uploads (the urec
   store), with GS memory as the fallback for the stale-TBP case, so the output can stay 0 px against
   today's. Verifiable as now (software, GL A/B).
2. **The game's own 2D and screen effects (large).** The HUD and text, the sprite batches, the copy strips
   and the full-screen effects that read the frame back as a texture, with frame masks and destination
   alpha. Each has to be found and ported as a native draw, much as piece 4's notes planned for the 2D
   batch API. About a thousand prims a vsync, 12-16 materials, the most GS-specific use.
3. **Host-owned render targets (small, after 2).** Only two targets in gameplay. Once the effects that
   read the frame are native, targets become host objects instead of GS buffers by address. Boot and
   level load still need the loading-screen / Z-buffer overlap handled (about 45 readbacks each).
4. **Host-owned textures, no GS memory (large; exactness risk).** Hook the texture pager so each upload
   becomes a host texture by id, decoded once (including the 32-bit-upload / 8-bit-read layout), and
   materials refer to ids instead of TBPs. Three things are GS-memory behaviour and must be reproduced
   on purpose or given up: the stale-TBP glitch (0-774 px per level), level 0 of mip chains pointing at
   the frame buffer, and textures read in another format than uploaded.

What stays in any version that keeps the original look: the PS2's pixel arithmetic (the blend
(A-B)*C>>7+D, alpha test, integer texel filtering, fog, the VU programs' projection). That is the look,
not a model of the hardware's memory or registers; it would live in per-material shaders.

Suggested order: 1 (largest share, narrowest features, cleanest to verify), then 2 and 3, then 4.
