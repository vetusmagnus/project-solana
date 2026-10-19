/* the native mesh layer, for the harness: nothing drawn; the native renderers' mesh paths off
 * (the harness compares their GS packets with the programs') */
#include <stdlib.h>
#include "uya_host.h"
void native_mesh_create(u32 id, const float *v6, const u8 *rgba, u32 nv, const u32 *idx, u32 ni) { (void)id; (void)v6; (void)rgba; (void)nv; (void)idx; (void)ni; }
void native_mesh_colors(u32 id, const u8 *rgba, u32 nv) { (void)id; (void)rgba; (void)nv; }
void native_mesh_update(u32 id, const float *v6, const u8 *rgba, u32 nv) { (void)id; (void)v6; (void)rgba; (void)nv; }
void native_mesh_free(u32 id) { (void)id; }
void native_regs(const u32 *addr, const u64 *val, int n) { (void)addr; (void)val; (void)n; }
void native_mesh_draw(u32 id, u32 first, u32 count, const float xf[32]) { (void)id; (void)first; (void)count; (void)xf; }
__attribute__((constructor)) static void mesh_off(void) { setenv("UYA_NTIEMESH", "0", 1); setenv("UYA_NSHRUBMESH", "0", 1); setenv("UYA_NMOBYMESH", "0", 1); }
#include <stdarg.h>
#include <stdio.h>
__attribute__((weak)) void uya_log(const char *fmt, ...) { va_list a; va_start(a, fmt); vfprintf(stderr, fmt, a); va_end(a); fputc('\n', stderr); }
__attribute__((weak)) void native_kick(const u32 *pk, u32 qwc) { (void)pk; (void)qwc; }
__attribute__((weak)) const char *uya_gs_src;
void native_mesh_update_idx(u32 id, const float *v6, const u8 *rgba, u32 nv, const u32 *idx, u32 ni) { (void)id; (void)v6; (void)rgba; (void)nv; (void)idx; (void)ni; }
__attribute__((weak)) int uya_gs_renderer_gl(void) { return 0; }
