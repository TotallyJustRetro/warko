/* gb.c - hardware runtime: memory map, MBC1/3/5, timer, PPU (DMG+CGB), APU, DMA, dispatcher. */
#include "gb.h"

CPU gb;
uint8_t *g_rom; uint32_t g_rom_size, g_nbanks; const uint8_t *g_romsw;
int g_cgb, g_interp_only;
uint32_t g_fb[160 * 144];
uint8_t *g_trace;

static uint8_t vram[2][0x2000], wram[8][0x1000], oam[0xA0], hram[0x80], io[0x80];
static uint8_t *sram; static uint32_t sram_size; static int has_battery, sram_dirty;
static int mbc, ram_en, rb_lo, rb_hi, rambank, mbc1_mode, vbk, wbk = 1, dbl;
static uint8_t bcp[64], ocp[64];
static uint8_t g_joy, ie;
static char rom_path[1024];

/* ---------------------------------------------------------------- MBC / loading */
static void mbc_update(void) {
    uint32_t b;
    if (mbc == 1) { b = rb_lo & 0x1F; if (!b) b = 1; b |= (rb_hi & 3) << 5; }
    else if (mbc == 3) { b = rb_lo & 0x7F; if (!b) b = 1; }
    else if (mbc == 5) { b = rb_lo | ((rb_hi & 1) << 8); }
    else b = 1;
    b %= g_nbanks;
    gb.rom_bank = b;
    g_romsw = (const uint8_t *)((uintptr_t)(g_rom + (size_t)b * 0x4000) - 0x4000);
}

static uint32_t fnv(const uint8_t *d, size_t n) { uint32_t h = 2166136261u; while (n--) { h ^= *d++; h *= 16777619u; } return h ? h : 1; }

int gb_load(const char *path) {
    FILE *f = fopen(path, "rb"); if (!f) return -1;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n < 0x8000) { fclose(f); return -2; }
    g_rom_size = n; g_rom = calloc(1, n + 0x4000);
    if (fread(g_rom, 1, n, f) != (size_t)n) { fclose(f); return -3; }
    fclose(f);
    g_nbanks = n / 0x4000;
    strncpy(rom_path, path, sizeof rom_path - 5);
    uint8_t t = g_rom[0x147];
    mbc = (t >= 1 && t <= 3) ? 1 : (t >= 0x0F && t <= 0x13) ? 3 : (t >= 0x19 && t <= 0x1E) ? 5 : 0;
    has_battery = (t == 3 || t == 9 || t == 0x0F || t == 0x10 || t == 0x13 || t == 0x1B || t == 0x1E);
    static const uint32_t rs[] = {0, 0, 0x2000, 0x8000, 0x20000, 0x10000};
    sram_size = g_rom[0x149] < 6 ? rs[g_rom[0x149]] : 0;
    if (sram_size) {
        sram = calloc(1, sram_size); memset(sram, 0xFF, sram_size);
        if (has_battery) { char p[1100]; snprintf(p, sizeof p, "%s.sav", path); FILE *s = fopen(p, "rb"); if (s) { if (fread(sram, 1, sram_size, s)) {} fclose(s); } }
    }
    g_cgb = (g_rom[0x143] & 0x80) != 0;
    if (gb_gen_hash && gb_gen_hash != fnv(g_rom, n)) {
        fprintf(stderr, "warning: ROM does not match the one this binary was generated from; using interpreter only\n");
        g_interp_only = 1;
    }
    return 0;
}

void gb_save(void) {
    if (!sram || !has_battery || !sram_dirty) return;
    char p[1100]; snprintf(p, sizeof p, "%s.sav", rom_path);
    FILE *s = fopen(p, "wb"); if (s) { fwrite(sram, 1, sram_size, s); fclose(s); sram_dirty = 0; }
}

/* ---------------------------------------------------------------- tracing */
int gb_trace_load(const char *path) {
    if (!g_trace) g_trace = calloc(1, g_rom_size / 8 + 1);
    FILE *f = fopen(path, "rb"); if (!f) return 0;
    uint8_t *tmp = malloc(g_rom_size / 8 + 1); size_t n = fread(tmp, 1, g_rom_size / 8 + 1, f); fclose(f);
    for (size_t i = 0; i < n; i++) g_trace[i] |= tmp[i];
    free(tmp); return 1;
}
void gb_trace_save(const char *path) { if (!g_trace) return; FILE *f = fopen(path, "wb"); if (f) { fwrite(g_trace, 1, g_rom_size / 8 + 1, f); fclose(f); } }
void gb_trace_mark(uint16_t pc) {
    if (!g_trace || pc >= 0x8000) return;
    uint32_t off = pc < 0x4000 ? pc : gb.rom_bank * 0x4000u + (pc - 0x4000);
    g_trace[off >> 3] |= 1 << (off & 7);
}

/* ---------------------------------------------------------------- APU */
#define CPUHZ 4194304
#define SRATE 44100
#define RING 16384
static int16_t ring[RING * 2]; static volatile uint32_t rhead, rtail;
static void audio_push(int16_t l, int16_t r) { uint32_t h = rhead; if (((h + 1) % RING) == rtail) return; ring[h * 2] = l; ring[h * 2 + 1] = r; rhead = (h + 1) % RING; }
void gb_audio_read(int16_t *out, int frames) {
    for (int i = 0; i < frames; i++) {
        if (rtail == rhead) { out[i * 2] = out[i * 2 + 1] = 0; continue; }
        out[i * 2] = ring[rtail * 2]; out[i * 2 + 1] = ring[rtail * 2 + 1]; rtail = (rtail + 1) % RING;
    }
}

static struct { int on, dac, len, len_en, vol, env_dir, env_per, env_t, freq, timer, step, duty; } ch[4];
static int sw_per, sw_dir, sw_shift, sw_t, sw_en, sw_shadow, lfsr = 0x7FFF, noise_w7, noise_div, noise_sh;
static uint8_t wave[16], apu_regs[0x30]; static int apu_on, fs_acc, fs_step, vol_code3;
static int64_t smp_acc;
static const uint8_t duty_tab[4][8] = {{0,0,0,0,0,0,0,1},{1,0,0,0,0,0,0,1},{1,0,0,0,0,1,1,1},{0,1,1,1,1,1,1,0}};
static const uint8_t apu_mask[0x16 + 1] = {0x80,0x3F,0x00,0xFF,0xBF, 0xFF,0x3F,0x00,0xFF,0xBF, 0x7F,0xFF,0x9F,0xFF,0xBF, 0xFF,0xFF,0x00,0x00,0xBF, 0x00,0x00, 0x70};

static int ch_period(int c) {
    if (c < 2) return (2048 - ch[c].freq) * 4;
    if (c == 2) return (2048 - ch[2].freq) * 2;
    static const int dv[8] = {8,16,32,48,64,80,96,112};
    return dv[noise_div] << noise_sh;
}
static void trigger(int c) {
    ch[c].on = ch[c].dac;
    if (!ch[c].len) ch[c].len = (c == 2) ? 256 : 64;
    ch[c].timer = ch_period(c);
    if (c != 2) { ch[c].env_t = ch[c].env_per ? ch[c].env_per : 8; ch[c].vol = (apu_regs[c == 0 ? 2 : c == 1 ? 7 : 0x11] >> 4); }
    if (c == 0) {
        sw_shadow = ch[0].freq; sw_t = sw_per ? sw_per : 8; sw_en = sw_per || sw_shift;
        if (sw_shift) { int n = sw_shadow >> sw_shift; n = sw_dir ? sw_shadow - n : sw_shadow + n; if (n > 2047) ch[0].on = 0; }
    }
    if (c == 2) ch[2].step = 0;
    if (c == 3) lfsr = 0x7FFF;
}
static void apu_wr(int a, uint8_t v) {  /* a = 0x10..0x3F */
    if (a >= 0x30) { wave[a - 0x30] = v; return; }
    if (a == 0x26) {
        int on = (v & 0x80) != 0;
        if (!on && apu_on) { memset(apu_regs, 0, sizeof apu_regs); memset(ch, 0, sizeof ch); sw_per = sw_shift = 0; }
        apu_on = on; return;
    }
    if (!apu_on) return;
    apu_regs[a - 0x10] = v;
    int c;
    switch (a) {
    case 0x10: sw_per = (v >> 4) & 7; sw_dir = (v >> 3) & 1; sw_shift = v & 7; break;
    case 0x11: ch[0].duty = v >> 6; ch[0].len = 64 - (v & 63); break;
    case 0x16: ch[1].duty = v >> 6; ch[1].len = 64 - (v & 63); break;
    case 0x12: case 0x17: case 0x21: c = a == 0x12 ? 0 : a == 0x17 ? 1 : 3;
        ch[c].dac = (v & 0xF8) != 0; if (!ch[c].dac) ch[c].on = 0;
        ch[c].env_dir = (v >> 3) & 1; ch[c].env_per = v & 7; break;
    case 0x13: ch[0].freq = (ch[0].freq & 0x700) | v; break;
    case 0x18: ch[1].freq = (ch[1].freq & 0x700) | v; break;
    case 0x14: ch[0].freq = (ch[0].freq & 0xFF) | ((v & 7) << 8); ch[0].len_en = (v >> 6) & 1; if (v & 0x80) trigger(0); break;
    case 0x19: ch[1].freq = (ch[1].freq & 0xFF) | ((v & 7) << 8); ch[1].len_en = (v >> 6) & 1; if (v & 0x80) trigger(1); break;
    case 0x1A: ch[2].dac = (v & 0x80) != 0; if (!ch[2].dac) ch[2].on = 0; break;
    case 0x1B: ch[2].len = 256 - v; break;
    case 0x1C: vol_code3 = (v >> 5) & 3; break;
    case 0x1D: ch[2].freq = (ch[2].freq & 0x700) | v; break;
    case 0x1E: ch[2].freq = (ch[2].freq & 0xFF) | ((v & 7) << 8); ch[2].len_en = (v >> 6) & 1; if (v & 0x80) trigger(2); break;
    case 0x20: ch[3].len = 64 - (v & 63); break;
    case 0x22: noise_sh = v >> 4; noise_w7 = (v >> 3) & 1; noise_div = v & 7; break;
    case 0x23: ch[3].len_en = (v >> 6) & 1; if (v & 0x80) trigger(3); break;
    }
}
static uint8_t apu_rd(int a) {
    if (a >= 0x30) return wave[a - 0x30];
    if (a == 0x26) return (apu_on << 7) | 0x70 | ch[0].on | (ch[1].on << 1) | (ch[2].on << 2) | (ch[3].on << 3);
    if (a > 0x26) return 0xFF;
    if (a >= 0x24) return apu_regs[a - 0x10];
    return apu_regs[a - 0x10] | apu_mask[a - 0x10];
}
static void fs_tick(void) {
    if (!(fs_step & 1)) for (int c = 0; c < 4; c++) if (ch[c].len_en && ch[c].len > 0 && --ch[c].len == 0) ch[c].on = 0;
    if ((fs_step == 2 || fs_step == 6) && sw_en && sw_per && ch[0].on) {
        if (--sw_t <= 0) {
            sw_t = sw_per ? sw_per : 8;
            int n = sw_shadow >> sw_shift; n = sw_dir ? sw_shadow - n : sw_shadow + n;
            if (n > 2047) ch[0].on = 0; else if (sw_shift) { sw_shadow = n; ch[0].freq = n; }
        }
    }
    if (fs_step == 7) for (int c = 0; c < 4; c++) if (c != 2 && ch[c].env_per && ch[c].on && --ch[c].env_t <= 0) {
        ch[c].env_t = ch[c].env_per;
        if (ch[c].env_dir) { if (ch[c].vol < 15) ch[c].vol++; } else if (ch[c].vol > 0) ch[c].vol--;
    }
    fs_step = (fs_step + 1) & 7;
}
static void apu_sample(void) {
    int out[4];
    for (int c = 0; c < 4; c++) {
        int d = 0;
        if (ch[c].on && ch[c].dac) {
            if (c < 2) d = duty_tab[ch[c].duty][ch[c].step & 7] ? ch[c].vol : 0;
            else if (c == 2) { int s = (wave[ch[2].step >> 1] >> ((ch[2].step & 1) ? 0 : 4)) & 15; d = vol_code3 ? s >> (vol_code3 - 1) : 0; }
            else d = (lfsr & 1) ? 0 : ch[3].vol;
            out[c] = d * 2 - 15;
        } else out[c] = 0;
    }
    uint8_t nr50 = apu_regs[0x14], nr51 = apu_regs[0x15];
    int l = 0, r = 0;
    for (int c = 0; c < 4; c++) { if (nr51 & (0x10 << c)) l += out[c]; if (nr51 & (1 << c)) r += out[c]; }
    l *= ((nr50 >> 4) & 7) + 1; r *= (nr50 & 7) + 1;
    audio_push(apu_on ? l * 50 : 0, apu_on ? r * 50 : 0);
}
static void apu_step(uint32_t n) {
    while (n) {
        uint32_t p = n > 64 ? 64 : n; n -= p;
        if (apu_on) for (int c = 0; c < 4; c++) if (ch[c].on) {
            ch[c].timer -= p;
            while (ch[c].timer <= 0) {
                int per = ch_period(c); if (per < 1) per = 1;
                ch[c].timer += per;
                if (c == 3) { int x = (lfsr & 1) ^ ((lfsr >> 1) & 1); lfsr = (lfsr >> 1) | (x << 14); if (noise_w7) lfsr = (lfsr & ~0x40) | (x << 6); }
                else if (c == 2) ch[2].step = (ch[2].step + 1) & 31;
                else ch[c].step = (ch[c].step + 1) & 7;
            }
        }
        fs_acc += p; while (fs_acc >= 8192) { fs_acc -= 8192; if (apu_on) fs_tick(); }
        smp_acc += (int64_t)p * SRATE;
        while (smp_acc >= CPUHZ) { smp_acc -= CPUHZ; apu_sample(); }
    }
}

/* ---------------------------------------------------------------- timer */
static uint32_t div16, tima_acc;
static const uint32_t tac_thr[4] = {1024, 16, 64, 256};
static void timer_step(uint64_t d) {
    div16 += d;
    if (io[0x07] & 4) {
        tima_acc += d; uint32_t t = tac_thr[io[0x07] & 3];
        while (tima_acc >= t) { tima_acc -= t; if (++io[0x05] == 0) { io[0x05] = io[0x06]; io[0x0F] |= 4; } }
    }
}

/* ---------------------------------------------------------------- PPU */
static int ppu_mode, ppu_dot, win_line, stat_line, off_acc;
static int hdma_active, hdma_len; static uint16_t hdma_src, hdma_dst;
static const uint32_t dmg_pal[4] = {0xFFE0F8D0, 0xFF88C070, 0xFF346856, 0xFF081820};

static void stat_check(void) {
    uint8_t s = io[0x41]; int c = 0;
    if ((s & 0x40) && io[0x44] == io[0x45]) c = 1;
    if ((s & 0x08) && ppu_mode == 0) c = 1;
    if ((s & 0x10) && ppu_mode == 1) c = 1;
    if ((s & 0x20) && ppu_mode == 2) c = 1;
    if (c && !stat_line) io[0x0F] |= 2;
    stat_line = c;
}
static uint32_t cgb_col(const uint8_t *pal, int idx) {
    uint16_t c = pal[idx * 2] | (pal[idx * 2 + 1] << 8);
    int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
    return 0xFF000000u | ((r * 255 / 31) << 16) | ((g * 255 / 31) << 8) | (b * 255 / 31);
}
static void render_line(int ly) {
    uint8_t lcdc = io[0x40]; uint32_t *row = &g_fb[ly * 160]; uint8_t bgi[160], bgp[160];
    int win_on = (lcdc & 0x20) && ly >= io[0x4A] && (g_cgb || (lcdc & 1)); int win_used = 0;
    for (int x = 0; x < 160; x++) {
        int idx = 0, attr = 0;
        if (!g_cgb && !(lcdc & 1)) { row[x] = dmg_pal[0]; bgi[x] = 0; bgp[x] = 0; continue; }
        int px, py, base; int wx = io[0x4B] - 7;
        if (win_on && x >= wx) { px = x - wx; py = win_line; base = (lcdc & 0x40) ? 0x1C00 : 0x1800; win_used = 1; }
        else { px = (x + io[0x43]) & 255; py = (ly + io[0x42]) & 255; base = (lcdc & 8) ? 0x1C00 : 0x1800; }
        int mo = base + (py >> 3) * 32 + (px >> 3);
        int tile = vram[0][mo]; if (g_cgb) attr = vram[1][mo];
        int tx = px & 7, ty = py & 7; if (attr & 0x20) tx = 7 - tx; if (attr & 0x40) ty = 7 - ty;
        int addr = (lcdc & 0x10) ? tile * 16 : 0x1000 + (int8_t)tile * 16;
        const uint8_t *td = vram[(attr >> 3) & 1] + addr + ty * 2; int bit = 7 - tx;
        idx = ((td[0] >> bit) & 1) | (((td[1] >> bit) & 1) << 1);
        row[x] = g_cgb ? cgb_col(bcp, (attr & 7) * 4 + idx) : dmg_pal[(io[0x47] >> (idx * 2)) & 3];
        bgi[x] = idx; bgp[x] = attr & 0x80;
    }
    if (win_used) win_line++;
    if (lcdc & 2) {
        int h = (lcdc & 4) ? 16 : 8, sel[10], ns = 0;
        for (int i = 0; i < 40 && ns < 10; i++) { int y = oam[i * 4] - 16; if (ly >= y && ly < y + h) sel[ns++] = i; }
        if (!g_cgb) for (int i = 1; i < ns; i++) { int k = sel[i], j = i - 1; while (j >= 0 && oam[sel[j] * 4 + 1] > oam[k * 4 + 1]) { sel[j + 1] = sel[j]; j--; } sel[j + 1] = k; }
        for (int k = ns - 1; k >= 0; k--) {
            int i = sel[k], y = oam[i * 4] - 16, x = oam[i * 4 + 1] - 8, tile = oam[i * 4 + 2], at = oam[i * 4 + 3];
            int ty = ly - y; if (at & 0x40) ty = h - 1 - ty;
            if (h == 16) { tile &= 0xFE; tile += ty >> 3; ty &= 7; }
            const uint8_t *td = vram[g_cgb ? ((at >> 3) & 1) : 0] + tile * 16 + ty * 2;
            for (int px = 0; px < 8; px++) {
                int sx = x + px; if (sx < 0 || sx >= 160) continue;
                int bit = (at & 0x20) ? px : 7 - px;
                int idx = ((td[0] >> bit) & 1) | (((td[1] >> bit) & 1) << 1); if (!idx) continue;
                if (g_cgb) { if ((lcdc & 1) && ((at & 0x80) || bgp[sx]) && bgi[sx]) continue; }
                else if ((at & 0x80) && bgi[sx]) continue;
                row[sx] = g_cgb ? cgb_col(ocp, (at & 7) * 4 + idx) : dmg_pal[(io[(at & 0x10) ? 0x49 : 0x48] >> (idx * 2)) & 3];
            }
        }
    }
}
static void hdma_copy16(void) {
    for (int i = 0; i < 16; i++) vram[vbk][(hdma_dst + i) & 0x1FFF] = RD(hdma_src + i);
    hdma_src += 16; hdma_dst += 16;
}
static int ppu_limit(void) { if (io[0x44] >= 144) return 456; return ppu_dot < 80 ? 80 : ppu_dot < 252 ? 252 : 456; }
static void ppu_step(uint32_t n) {
    if (!(io[0x40] & 0x80)) { off_acc += n; if (off_acc >= 70224) { off_acc -= 70224; gb.frame_ready = 1; } return; }
    while (n) {
        int ly = io[0x44], lim = ppu_limit(); uint32_t st = lim - ppu_dot; if (st > n) st = n;
        ppu_dot += st; n -= st;
        if (ppu_dot < lim) break;
        if (ly < 144) {
            if (lim == 80) ppu_mode = 3;
            else if (lim == 252) { ppu_mode = 0; render_line(ly); if (hdma_active) { hdma_copy16(); if (--hdma_len == 0) hdma_active = 0; } }
            else { ppu_dot = 0; ly++; io[0x44] = ly; if (ly == 144) { ppu_mode = 1; io[0x0F] |= 1; gb.frame_ready = 1; } else ppu_mode = 2; }
        } else { ppu_dot = 0; ly++; if (ly > 153) { ly = 0; ppu_mode = 2; win_line = 0; } io[0x44] = ly; }
        stat_check();
    }
}

/* ---------------------------------------------------------------- hardware sync */
static uint64_t hwcyc; static uint32_t ppu_rem;
static void hw_catchup(void) {
    uint64_t d = gb.cyc - hwcyc; if (!d) return; hwcyc = gb.cyc;
    timer_step(d);
    ppu_rem += d; uint32_t pd = ppu_rem >> dbl; ppu_rem &= (1u << dbl) - 1;
    ppu_step(pd); apu_step(pd);
}
static uint64_t next_event(void) {
    uint64_t ev = 4096;
    uint64_t r = (io[0x40] & 0x80) ? (uint64_t)(ppu_limit() - ppu_dot) << dbl : (uint64_t)(70224 - off_acc) << dbl;
    if (r < ev) ev = r;
    if (io[0x07] & 4) { uint32_t t = tac_thr[io[0x07] & 3]; r = (uint64_t)(255 - io[0x05]) * t + (t - tima_acc); if (r < ev) ev = r; }
    return ev ? ev : 1;
}
int gb_sync(void) {
    hw_catchup();
    uint8_t pend = io[0x0F] & ie & 0x1F;
    if (gb.ime && pend) {
        int i = __builtin_ctz(pend);
        gb.ime = 0; io[0x0F] &= ~(1 << i); PUSH16(gb.pc); gb.pc = 0x40 + i * 8; gb.cyc += 20;
    }
    gb.deadline = gb.cyc + next_event();
    return gb.frame_ready;
}
void gb_halt(void) {
    hw_catchup();
    for (int guard = 0; !(io[0x0F] & ie & 0x1F) && guard < 200000; guard++) { gb.cyc += next_event(); hw_catchup(); }
    gb.deadline = gb.cyc;
}
void gb_stop(void) {
    if (g_cgb && (io[0x4D] & 1)) { hw_catchup(); dbl ^= 1; io[0x4D] &= ~1; div16 = 0; }
}
void gb_invalid(uint8_t op) { fprintf(stderr, "invalid opcode %02X at %04X\n", op, gb.pc); gb_save(); exit(1); }

/* ---------------------------------------------------------------- memory map */
uint8_t gb_rd(uint16_t a) {
    if (a < 0x4000) return g_rom[a];
    if (a < 0x8000) return g_romsw[a];
    if (a < 0xA000) return vram[vbk][a - 0x8000];
    if (a < 0xC000) {
        if (!sram || !ram_en) return 0xFF;
        if (mbc == 3 && rambank >= 8) return 0;
        uint32_t bank = mbc == 1 ? (mbc1_mode ? rambank : 0) : rambank;
        return sram[((bank & 15) * 0x2000 + (a - 0xA000)) % sram_size];
    }
    if (a < 0xD000) return wram[0][a - 0xC000];
    if (a < 0xE000) return wram[wbk][a - 0xD000];
    if (a < 0xF000) return wram[0][a - 0xE000];
    if (a < 0xFE00) return wram[wbk][a - 0xF000];
    if (a < 0xFEA0) return oam[a - 0xFE00];
    if (a < 0xFF00) return 0xFF;
    if (a == 0xFFFF) return ie;
    if (a >= 0xFF80) return hram[a - 0xFF80];
    int r = a - 0xFF00;
    hw_catchup();
    switch (r) {
    case 0x00: { uint8_t v = 0xCF | (io[0] & 0x30); if (!(io[0] & 0x10)) v &= ~(g_joy & 15); if (!(io[0] & 0x20)) v &= ~(g_joy >> 4); return v; }
    case 0x04: return div16 >> 8;
    case 0x0F: return io[0x0F] | 0xE0;
    case 0x41: return 0x80 | (io[0x41] & 0x78) | (io[0x44] == io[0x45] ? 4 : 0) | ((io[0x40] & 0x80) ? ppu_mode : 0);
    case 0x4D: return (dbl << 7) | 0x7E | (io[0x4D] & 1);
    case 0x4F: return 0xFE | vbk;
    case 0x55: return hdma_active ? ((hdma_len - 1) & 0x7F) : 0xFF;
    case 0x69: return bcp[io[0x68] & 0x3F];
    case 0x6B: return ocp[io[0x6A] & 0x3F];
    case 0x70: return 0xF8 | wbk;
    }
    if (r >= 0x10 && r <= 0x3F) return apu_rd(r);
    return io[r];
}

void gb_wr(uint16_t a, uint8_t v) {
    if (a < 0x8000) {
        if (mbc == 1) { if (a < 0x2000) ram_en = (v & 15) == 10; else if (a < 0x4000) rb_lo = v; else if (a < 0x6000) { rb_hi = v & 3; rambank = v & 3; } else mbc1_mode = v & 1; }
        else if (mbc == 3) { if (a < 0x2000) ram_en = (v & 15) == 10; else if (a < 0x4000) rb_lo = v; else if (a < 0x6000) rambank = v; }
        else if (mbc == 5) { if (a < 0x2000) ram_en = (v & 15) == 10; else if (a < 0x3000) rb_lo = v; else if (a < 0x4000) rb_hi = v; else if (a < 0x6000) rambank = v & 15; }
        mbc_update(); return;
    }
    if (a < 0xA000) { vram[vbk][a - 0x8000] = v; return; }
    if (a < 0xC000) {
        if (!sram || !ram_en) return;
        if (mbc == 3 && rambank >= 8) return;
        uint32_t bank = mbc == 1 ? (mbc1_mode ? rambank : 0) : rambank;
        uint32_t i = ((bank & 15) * 0x2000 + (a - 0xA000)) % sram_size;
        if (sram[i] != v) { sram[i] = v; sram_dirty = 1; }
        return;
    }
    if (a < 0xD000) { wram[0][a - 0xC000] = v; return; }
    if (a < 0xE000) { wram[wbk][a - 0xD000] = v; return; }
    if (a < 0xF000) { wram[0][a - 0xE000] = v; return; }
    if (a < 0xFE00) { wram[wbk][a - 0xF000] = v; return; }
    if (a < 0xFEA0) { oam[a - 0xFE00] = v; return; }
    if (a < 0xFF00) return;
    if (a == 0xFFFF) { ie = v; gb.deadline = gb.cyc; return; }
    if (a >= 0xFF80) { hram[a - 0xFF80] = v; return; }
    int r = a - 0xFF00;
    hw_catchup();
    switch (r) {
    case 0x00: io[0] = v & 0x30; return;
    case 0x04: div16 = 0; return;
    case 0x07: io[0x07] = v & 7; return;
    case 0x0F: io[0x0F] = v & 0x1F; gb.deadline = gb.cyc; return;
    case 0x40: {
        uint8_t old = io[0x40]; io[0x40] = v;
        if ((old & 0x80) && !(v & 0x80)) { io[0x44] = 0; ppu_dot = 0; ppu_mode = 0; stat_line = 0; win_line = 0; off_acc = 0; }
        if (!(old & 0x80) && (v & 0x80)) { ppu_dot = 0; io[0x44] = 0; ppu_mode = 0; win_line = 0; ppu_rem = 0; stat_check(); }
        gb.deadline = gb.cyc; return; }
    case 0x41: io[0x41] = v & 0x78; stat_check(); return;
    case 0x44: return;
    case 0x45: io[0x45] = v; stat_check(); return;
    case 0x46: for (int i = 0; i < 0xA0; i++) oam[i] = RD(((uint16_t)v << 8) + i); io[0x46] = v; return;
    case 0x4D: io[0x4D] = (io[0x4D] & 0xFE) | (v & 1); return;
    case 0x4F: if (g_cgb) vbk = v & 1; return;
    case 0x51: hdma_src = (hdma_src & 0xFF) | (v << 8); return;
    case 0x52: hdma_src = (hdma_src & 0xFF00) | (v & 0xF0); return;
    case 0x53: hdma_dst = (hdma_dst & 0xFF) | ((v & 0x1F) << 8); return;
    case 0x54: hdma_dst = (hdma_dst & 0xFF00) | (v & 0xF0); return;
    case 0x55:
        if (!g_cgb) return;
        if (v & 0x80) { hdma_active = 1; hdma_len = (v & 0x7F) + 1; }
        else if (hdma_active) hdma_active = 0;
        else { int n = (v & 0x7F) + 1; while (n--) hdma_copy16(); }
        return;
    case 0x68: io[0x68] = v; return;
    case 0x69: bcp[io[0x68] & 0x3F] = v; if (io[0x68] & 0x80) io[0x68] = 0x80 | ((io[0x68] + 1) & 0x3F); return;
    case 0x6A: io[0x6A] = v; return;
    case 0x6B: ocp[io[0x6A] & 0x3F] = v; if (io[0x6A] & 0x80) io[0x6A] = 0x80 | ((io[0x6A] + 1) & 0x3F); return;
    case 0x70: if (g_cgb) { wbk = v & 7; if (!wbk) wbk = 1; } return;
    }
    if (r >= 0x10 && r <= 0x3F) { apu_wr(r, v); return; }
    io[r] = v;
}

/* ---------------------------------------------------------------- reset / frame loop */
void gb_set_joy(uint8_t bits) { if ((bits & ~g_joy)) io[0x0F] |= 0x10; g_joy = bits; }

void gb_reset(void) {
    memset(&gb, 0, sizeof gb);
    if (g_cgb) { gb.a = 0x11; gb.f = 0x80; gb.b = 0; gb.c = 0; gb.d = 0xFF; gb.e = 0x56; gb.h = 0; gb.l = 0x0D; }
    else { gb.a = 0x01; gb.f = 0xB0; gb.b = 0; gb.c = 0x13; gb.d = 0; gb.e = 0xD8; gb.h = 0x01; gb.l = 0x4D; }
    gb.sp = 0xFFFE; gb.pc = 0x0100;
    rb_lo = 1; rb_hi = 0; ram_en = 0; mbc1_mode = 0; rambank = 0; mbc_update();
    memset(io, 0, sizeof io);
    io[0x40] = 0x91; io[0x47] = 0xFC; io[0x48] = io[0x49] = 0xFF; io[0x0F] = 0x01; io[0x44] = 0;
    ppu_mode = 1; ppu_dot = 0; div16 = 0xABCC;
    for (int i = 0; i < 64; i++) { bcp[i] = 0xFF; ocp[i] = 0xFF; }
    apu_on = 1; apu_regs[0x14] = 0x77; apu_regs[0x15] = 0xF3;
    ch[0].dac = 1;
    gb.deadline = 0;
}

void gb_run_frame(void) {
    gb.frame_ready = 0;
    while (!gb.frame_ready) {
        if (gb.cyc >= gb.deadline) { gb_sync(); if (gb.frame_ready) break; }
        if (!g_interp_only) {
            int r = 0; int (*fn)(void) = 0;
            if (gb.pc < 0x4000) fn = gb_blk0p; else if (gb.pc < 0x8000) fn = gb_blkwin[gb.rom_bank];
            if (fn) { r = fn(); if (r) continue; }
        }
        gb_interp_step();
    }
}
