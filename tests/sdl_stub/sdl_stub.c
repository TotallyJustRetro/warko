/* Stub implementations: quit after N polled frames, press nothing, count presents. */
#include <SDL2/SDL.h>
#include <stdio.h>
#include <time.h>
static int polls, presents; static Uint8 keys[512];
int SDL_Init(Uint32 f) { (void)f; return 0; } void SDL_Quit(void) { printf("stub: %d frames presented\n", presents); }
const char *SDL_GetError(void) { return "stub"; }
SDL_Window *SDL_CreateWindow(const char *t, int a, int b, int c, int d, Uint32 f) { return (SDL_Window *)1; }
SDL_Renderer *SDL_CreateRenderer(SDL_Window *w, int i, Uint32 f) { return (SDL_Renderer *)1; }
int SDL_RenderSetLogicalSize(SDL_Renderer *r, int w, int h) { return 0; }
SDL_Texture *SDL_CreateTexture(SDL_Renderer *r, Uint32 f, int a, int w, int h) { return (SDL_Texture *)1; }
SDL_AudioDeviceID SDL_OpenAudioDevice(const char *n, int c, const SDL_AudioSpec *w, SDL_AudioSpec *h, int a) { return 0; }
void SDL_PauseAudioDevice(SDL_AudioDeviceID d, int p) {} void SDL_CloseAudioDevice(SDL_AudioDeviceID d) {}
Uint64 SDL_GetPerformanceFrequency(void) { return 1000000000ull; }
Uint64 SDL_GetPerformanceCounter(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000000000ull + t.tv_nsec; }
int SDL_PollEvent(SDL_Event *e) { if (++polls > 180) { e->type = SDL_QUIT; polls = 0; return 1; } return 0; }
const Uint8 *SDL_GetKeyboardState(int *n) { return keys; }
int SDL_UpdateTexture(SDL_Texture *t, const void *r, const void *p, int pitch) { return 0; }
int SDL_RenderClear(SDL_Renderer *r) { return 0; } int SDL_RenderCopy(SDL_Renderer *r, SDL_Texture *t, const void *a, const void *b) { return 0; }
void SDL_RenderPresent(SDL_Renderer *r) { presents++; } void SDL_Delay(Uint32 ms) {}
int SDL_ShowSimpleMessageBox(Uint32 f, const char *t, const char *m, SDL_Window *w) { return 0; }
