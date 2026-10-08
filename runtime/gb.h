/* gb.h - shared definitions for the Game Boy static-recompilation runtime.
 * Used by the runtime (gb.c, main.c) AND by the code emitted by gbrecomp.py. */
#ifndef GB_H
#define GB_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FZ 0x80
#define FN 0x40
#define FH 0x20
#define FC 0x10
#define GB_MAXBANKS 512

typedef struct {
    uint8_t a, f, b, c, d, e, h, l;
    uint16_t sp, pc;
    uint8_t ime, halted;
    uint32_t rom_bank;          /* bank currently mapped at 0x4000-0x7FFF */
    uint64_t cyc, deadline;     /* T-cycles; next point where hardware must be synced */
    int frame_ready;
} CPU;

extern CPU gb;
extern uint8_t *g_rom;
extern uint32_t g_rom_size, g_nbanks;
extern const uint8_t *g_romsw;  /* g_rom + bank*0x4000 - 0x4000 */
extern int g_cgb, g_interp_only;
extern uint32_t g_fb[160 * 144];
extern uint8_t *g_trace;        /* optional executed-instruction bitmap (1 bit per ROM byte) */

uint8_t gb_rd(uint16_t a);
void gb_wr(uint16_t a, uint8_t v);
int  gb_sync(void);             /* run hardware, service interrupts; returns frame_ready */
void gb_halt(void);
void gb_stop(void);
void gb_invalid(uint8_t op);
void gb_trace_mark(uint16_t pc);
void gb_interp_step(void);      /* generated */

static inline uint8_t RD(uint16_t a) {
    if (a < 0x4000) return g_rom[a];
    if (a < 0x8000) return g_romsw[a];
    return gb_rd(a);
}
#define WR(a, v) gb_wr((uint16_t)(a), (uint8_t)(v))

static inline uint16_t BC(void) { return (gb.b << 8) | gb.c; }
static inline uint16_t DE(void) { return (gb.d << 8) | gb.e; }
static inline uint16_t HL(void) { return (gb.h << 8) | gb.l; }
static inline void SETBC(uint16_t v) { gb.b = v >> 8; gb.c = v; }
static inline void SETDE(uint16_t v) { gb.d = v >> 8; gb.e = v; }
static inline void SETHL(uint16_t v) { gb.h = v >> 8; gb.l = v; }
#define SETSP(v) (gb.sp = (uint16_t)(v))

static inline void PUSH16(uint16_t v) { gb.sp--; WR(gb.sp, v >> 8); gb.sp--; WR(gb.sp, v & 0xFF); }
static inline uint16_t POP16(void) { uint16_t lo = RD(gb.sp++); uint16_t hi = RD(gb.sp++); return lo | (hi << 8); }

static inline void add8(uint8_t v) { unsigned r = gb.a + v; gb.f = ((r & 0xFF) == 0 ? FZ : 0) | (((gb.a & 15) + (v & 15)) > 15 ? FH : 0) | (r > 0xFF ? FC : 0); gb.a = r; }
static inline void adc8(uint8_t v) { unsigned c = (gb.f & FC) ? 1 : 0; unsigned r = gb.a + v + c; gb.f = ((r & 0xFF) == 0 ? FZ : 0) | (((gb.a & 15) + (v & 15) + c) > 15 ? FH : 0) | (r > 0xFF ? FC : 0); gb.a = r; }
static inline void sub8(uint8_t v) { unsigned r = gb.a - v; gb.f = FN | ((r & 0xFF) == 0 ? FZ : 0) | ((gb.a & 15) < (v & 15) ? FH : 0) | (gb.a < v ? FC : 0); gb.a = r; }
static inline void sbc8(uint8_t v) { int c = (gb.f & FC) ? 1 : 0; int r = gb.a - v - c; gb.f = FN | ((r & 0xFF) == 0 ? FZ : 0) | (((gb.a & 15) - (v & 15) - c) < 0 ? FH : 0) | (r < 0 ? FC : 0); gb.a = r; }
static inline void and8(uint8_t v) { gb.a &= v; gb.f = FH | (gb.a ? 0 : FZ); }
static inline void xor8(uint8_t v) { gb.a ^= v; gb.f = gb.a ? 0 : FZ; }
static inline void or8(uint8_t v) { gb.a |= v; gb.f = gb.a ? 0 : FZ; }
static inline void cp8(uint8_t v) { unsigned r = gb.a - v; gb.f = FN | ((r & 0xFF) == 0 ? FZ : 0) | ((gb.a & 15) < (v & 15) ? FH : 0) | (gb.a < v ? FC : 0); }
static inline uint8_t inc8(uint8_t v) { v++; gb.f = (gb.f & FC) | (v == 0 ? FZ : 0) | ((v & 15) == 0 ? FH : 0); return v; }
static inline uint8_t dec8(uint8_t v) { v--; gb.f = (gb.f & FC) | FN | (v == 0 ? FZ : 0) | ((v & 15) == 15 ? FH : 0); return v; }
static inline void add_hl(uint16_t v) { uint16_t hl = HL(); unsigned r = hl + v; gb.f = (gb.f & FZ) | (((hl & 0xFFF) + (v & 0xFFF)) > 0xFFF ? FH : 0) | (r > 0xFFFF ? FC : 0); SETHL(r); }
static inline uint16_t add_sp(int8_t e) { uint16_t r = gb.sp + e; gb.f = (((gb.sp & 15) + ((uint8_t)e & 15)) > 15 ? FH : 0) | (((gb.sp & 0xFF) + (uint8_t)e) > 0xFF ? FC : 0); return r; }
static inline void daa(void) {
    int a = gb.a;
    if (!(gb.f & FN)) { if ((gb.f & FC) || a > 0x99) { a += 0x60; gb.f |= FC; } if ((gb.f & FH) || (a & 15) > 9) a += 6; }
    else { if (gb.f & FC) a -= 0x60; if (gb.f & FH) a -= 6; }
    gb.a = a; gb.f = (gb.f & (FN | FC)) | (gb.a ? 0 : FZ);
}
static inline void rlca(void) { uint8_t c = gb.a >> 7; gb.a = (gb.a << 1) | c; gb.f = c ? FC : 0; }
static inline void rrca(void) { uint8_t c = gb.a & 1; gb.a = (gb.a >> 1) | (c << 7); gb.f = c ? FC : 0; }
static inline void rla(void) { uint8_t c = gb.a >> 7; gb.a = (gb.a << 1) | ((gb.f & FC) ? 1 : 0); gb.f = c ? FC : 0; }
static inline void rra(void) { uint8_t c = gb.a & 1; gb.a = (gb.a >> 1) | ((gb.f & FC) ? 0x80 : 0); gb.f = c ? FC : 0; }
static inline uint8_t cb_rlc(uint8_t v) { uint8_t c = v >> 7; v = (v << 1) | c; gb.f = (v ? 0 : FZ) | (c ? FC : 0); return v; }
static inline uint8_t cb_rrc(uint8_t v) { uint8_t c = v & 1; v = (v >> 1) | (c << 7); gb.f = (v ? 0 : FZ) | (c ? FC : 0); return v; }
static inline uint8_t cb_rl(uint8_t v) { uint8_t c = v >> 7; v = (v << 1) | ((gb.f & FC) ? 1 : 0); gb.f = (v ? 0 : FZ) | (c ? FC : 0); return v; }
static inline uint8_t cb_rr(uint8_t v) { uint8_t c = v & 1; v = (v >> 1) | ((gb.f & FC) ? 0x80 : 0); gb.f = (v ? 0 : FZ) | (c ? FC : 0); return v; }
static inline uint8_t cb_sla(uint8_t v) { uint8_t c = v >> 7; v <<= 1; gb.f = (v ? 0 : FZ) | (c ? FC : 0); return v; }
static inline uint8_t cb_sra(uint8_t v) { uint8_t c = v & 1; v = (v >> 1) | (v & 0x80); gb.f = (v ? 0 : FZ) | (c ? FC : 0); return v; }
static inline uint8_t cb_swap(uint8_t v) { v = (v >> 4) | (v << 4); gb.f = v ? 0 : FZ; return v; }
static inline uint8_t cb_srl(uint8_t v) { uint8_t c = v & 1; v >>= 1; gb.f = (v ? 0 : FZ) | (c ? FC : 0); return v; }
static inline void bit8(int n, uint8_t v) { gb.f = (gb.f & FC) | FH | (((v >> n) & 1) ? 0 : FZ); }

/* Block-function protocol (generated code).
 * Each gb_blk* returns: 1 = yield to the frontend/dispatcher (gb.pc valid),
 *   2 = gb.pc is outside this function's region (re-dispatch), 0 = pc is in region but not compiled. */
#define SYNC_GO(T) do { gb.pc = (uint16_t)(T); if (gb.cyc >= gb.deadline) { if (gb_sync()) return 1; } goto top; } while (0)
#define SYNC_LOCAL(L, T) do { if (gb.cyc >= gb.deadline) { gb.pc = (T); if (gb_sync()) return 1; goto top; } goto L; } while (0)

/* tables emitted into gen_tbl.c */
extern int (*const gb_blk0p)(void);
extern int (*const gb_blkwin[GB_MAXBANKS])(void);
extern const uint32_t gb_gen_hash;   /* FNV-1a of the ROM the code was generated from (0 = none) */

/* runtime API */
int  gb_load(const char *path);
void gb_reset(void);
void gb_run_frame(void);
void gb_set_joy(uint8_t bits);   /* 1=R 2=L 4=U 8=D 16=A 32=B 64=Select 128=Start */
void gb_save(void);
void gb_audio_read(int16_t *out, int frames);
int  gb_audio_pull(int16_t *out, int max_frames);   /* queue-style audio: returns frames produced */
extern float g_volume;                              /* 0..1, default 0.5 */
int  gb_trace_load(const char *path);
void gb_trace_save(const char *path);
#endif
