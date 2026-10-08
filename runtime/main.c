/* main.c - frontend. SDL2 window/audio/input by default; -DHEADLESS builds a test harness. */
#include "gb.h"
#ifndef HEADLESS
#include <SDL2/SDL.h>
#endif

static const char *trace_path;
int gb_pick_rom(char *buf, int n);

static void usage(const char *p) {
    fprintf(stderr,
        "usage: %s <rom> [options]\n"
        "  --interp         ignore compiled code, interpret only (reference / tracing)\n"
        "  --trace FILE     record executed instructions into FILE (merged with existing); implies --interp\n"
        "  --headless N     run N frames without a window (also: --mash, --shot FILE.ppm)\n"
        "  --mash           pseudo-random button input (for tracing/testing)\n"
        "  --shot FILE      write final frame as PPM\n", p);
}

static uint32_t rng = 0x1234567;
static uint32_t xr(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static uint8_t mash_input(int frame) {
    static uint8_t cur;
    if (frame % 6 == 0) {
        cur = 0; uint32_t r = xr();
        if (r & 1) cur |= 16;                 /* A */
        if ((r >> 1) & 1) cur |= 32;          /* B */
        if (((r >> 2) & 15) == 0) cur |= 128; /* Start */
        if (((r >> 6) & 3) == 0) cur |= 64;   /* Select */
        cur |= (r >> 8) & 1 ? 1 : 0; cur |= ((r >> 9) & 1) ? 2 : 0; cur |= ((r >> 10) & 3) == 0 ? 4 : 0; cur |= ((r >> 12) & 3) == 0 ? 8 : 0;
        if ((cur & 3) == 3) cur &= ~2;
    }
    return cur;
}
static void write_ppm(const char *p) {
    FILE *f = fopen(p, "wb"); if (!f) return;
    fprintf(f, "P6\n160 144\n255\n");
    for (int i = 0; i < 160 * 144; i++) { uint32_t c = g_fb[i]; fputc(c >> 16, f); fputc(c >> 8, f); fputc(c, f); }
    fclose(f);
}

#ifndef HEADLESS
static void audio_cb(void *u, Uint8 *stream, int len) { (void)u; gb_audio_read((int16_t *)stream, len / 4); }
static uint8_t read_keys(void) {
    const Uint8 *k = SDL_GetKeyboardState(NULL); uint8_t j = 0;
    if (k[SDL_SCANCODE_RIGHT]) j |= 1; if (k[SDL_SCANCODE_LEFT]) j |= 2;
    if (k[SDL_SCANCODE_UP]) j |= 4;    if (k[SDL_SCANCODE_DOWN]) j |= 8;
    if (k[SDL_SCANCODE_Z]) j |= 16;    if (k[SDL_SCANCODE_X]) j |= 32;
    if (k[SDL_SCANCODE_BACKSPACE] || k[SDL_SCANCODE_RSHIFT]) j |= 64;
    if (k[SDL_SCANCODE_RETURN]) j |= 128;
    return j;
}
#endif

int main(int argc, char **argv) {
    const char *rom = NULL, *shot = NULL; int headless = 0, nframes = 0, mash = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--interp")) g_interp_only = 1;
        else if (!strcmp(argv[i], "--trace") && i + 1 < argc) { trace_path = argv[++i]; g_interp_only = 1; }
        else if (!strcmp(argv[i], "--headless") && i + 1 < argc) { headless = 1; nframes = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--mash")) mash = 1;
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (argv[i][0] != '-' && !rom) rom = argv[i];
        else { usage(argv[0]); return 1; }
    }
    static char pick[1024];
#ifndef HEADLESS
    if (!rom && !headless && gb_pick_rom(pick, sizeof pick)) rom = pick;
#endif
    if (!rom) { usage(argv[0]); return 1; }
    int e = gb_load(rom);
    if (e) {
        fprintf(stderr, "cannot load %s (%d)\n", rom, e);
#ifndef HEADLESS
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "gbrecomp", "Could not load that ROM file.", NULL);
#endif
        return 1;
    }
    if (!gb_blk0p) g_interp_only = 1;
    if (trace_path) gb_trace_load(trace_path);
    gb_reset();
    fprintf(stderr, "%s: %u banks, %s mode%s\n", rom, g_nbanks, g_cgb ? "CGB" : "DMG", g_interp_only ? ", interpreter only" : ", recompiled");

#ifdef HEADLESS
    headless = 1;
#endif
    if (headless) {
        for (int f = 0; f < nframes; f++) { if (mash) gb_set_joy(mash_input(f)); gb_run_frame(); }
        if (shot) write_ppm(shot);
        if (trace_path) gb_trace_save(trace_path);
        gb_save(); return 0;
    }
#ifndef HEADLESS
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS)) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    SDL_Window *w = SDL_CreateWindow("gbrecomp", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 576, SDL_WINDOW_RESIZABLE);
    SDL_Renderer *r = SDL_CreateRenderer(w, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!r) r = SDL_CreateRenderer(w, -1, 0);
    SDL_RenderSetLogicalSize(r, 160, 144);
    SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 160, 144);
    SDL_AudioSpec want = {0}, have; want.freq = 44100; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024; want.callback = audio_cb;
    SDL_AudioDeviceID ad = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (ad) SDL_PauseAudioDevice(ad, 0);
    int run = 1; uint64_t freq = SDL_GetPerformanceFrequency(), next = SDL_GetPerformanceCounter(), frame = 0;
    const double frame_s = 70224.0 / 4194304.0;
    while (run) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) run = 0;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) run = 0;
        }
        gb_set_joy(read_keys());
        gb_run_frame();
        SDL_UpdateTexture(t, NULL, g_fb, 160 * 4); SDL_RenderClear(r); SDL_RenderCopy(r, t, NULL, NULL); SDL_RenderPresent(r);
        if ((++frame & 255) == 0) gb_save();
        next += (uint64_t)(frame_s * freq); uint64_t now = SDL_GetPerformanceCounter();
        if (next > now) SDL_Delay((Uint32)((next - now) * 1000 / freq)); else if (now - next > freq / 4) next = now;
    }
    if (trace_path) gb_trace_save(trace_path);
    gb_save(); if (ad) SDL_CloseAudioDevice(ad); SDL_Quit();
#endif
    return 0;
}
