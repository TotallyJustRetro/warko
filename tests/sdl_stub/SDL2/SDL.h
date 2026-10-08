/* Minimal stand-in for SDL2 (only what runtime/main.c uses) so the frontend can be compiled
 * and smoke-tested without SDL installed. NOT a real SDL; the real build uses sdl2-config. */
#ifndef SDL_STUB_H
#define SDL_STUB_H
#include <stdint.h>
typedef uint8_t Uint8; typedef uint32_t Uint32; typedef uint64_t Uint64;
typedef struct SDL_Window SDL_Window; typedef struct SDL_Renderer SDL_Renderer; typedef struct SDL_Texture SDL_Texture;
typedef Uint32 SDL_AudioDeviceID;
#define SDL_INIT_VIDEO 0x20u
#define SDL_INIT_AUDIO 0x10u
#define SDL_INIT_EVENTS 0x4000u
#define SDL_INIT_GAMECONTROLLER 0x2000u
#define SDL_WINDOWPOS_CENTERED 0x2FFF0000
#define SDL_WINDOW_RESIZABLE 0x20
#define SDL_RENDERER_ACCELERATED 2
#define SDL_RENDERER_PRESENTVSYNC 4
#define SDL_PIXELFORMAT_ARGB8888 0x16362004u
#define SDL_TEXTUREACCESS_STREAMING 1
#define AUDIO_S16SYS 0x8010
#define SDL_QUIT 0x100
#define SDL_KEYDOWN 0x300
#define SDL_CONTROLLERDEVICEADDED 0x653
#define SDL_CONTROLLERDEVICEREMOVED 0x654
typedef struct _SDL_GameController SDL_GameController;
enum { SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y, SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_GUIDE, SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_LEFTSTICK, SDL_CONTROLLER_BUTTON_RIGHTSTICK, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT };
enum { SDL_CONTROLLER_AXIS_LEFTX, SDL_CONTROLLER_AXIS_LEFTY };
#define SDLK_ESCAPE 27
#define SDLK_MINUS 45
#define SDLK_EQUALS 61
#define SDL_MESSAGEBOX_ERROR 0x10
typedef enum { SDL_SCANCODE_RETURN = 40, SDL_SCANCODE_BACKSPACE = 42, SDL_SCANCODE_X = 27, SDL_SCANCODE_Z = 29,
  SDL_SCANCODE_A = 4, SDL_SCANCODE_D = 7, SDL_SCANCODE_J = 13, SDL_SCANCODE_K = 14, SDL_SCANCODE_S = 22, SDL_SCANCODE_W = 26, SDL_SCANCODE_SPACE = 44, SDL_SCANCODE_TAB = 43, SDL_SCANCODE_RIGHT = 79, SDL_SCANCODE_LEFT = 80, SDL_SCANCODE_DOWN = 81, SDL_SCANCODE_UP = 82, SDL_SCANCODE_RSHIFT = 229 } SDL_Scancode;
typedef struct { int sym; } SDL_Keysym;
typedef struct { Uint32 type; struct { SDL_Keysym keysym; } key; } SDL_Event;
typedef void (*SDL_AudioCallback)(void *, Uint8 *, int);
typedef struct { int freq; uint16_t format; Uint8 channels; Uint8 silence; uint16_t samples; Uint32 size; SDL_AudioCallback callback; void *userdata; } SDL_AudioSpec;
int SDL_Init(Uint32); void SDL_Quit(void); const char *SDL_GetError(void);
SDL_Window *SDL_CreateWindow(const char *, int, int, int, int, Uint32);
SDL_Renderer *SDL_CreateRenderer(SDL_Window *, int, Uint32);
int SDL_RenderSetLogicalSize(SDL_Renderer *, int, int);
SDL_Texture *SDL_CreateTexture(SDL_Renderer *, Uint32, int, int, int);
SDL_AudioDeviceID SDL_OpenAudioDevice(const char *, int, const SDL_AudioSpec *, SDL_AudioSpec *, int);
void SDL_PauseAudioDevice(SDL_AudioDeviceID, int); void SDL_CloseAudioDevice(SDL_AudioDeviceID);
Uint64 SDL_GetPerformanceFrequency(void); Uint64 SDL_GetPerformanceCounter(void);
int SDL_PollEvent(SDL_Event *); const Uint8 *SDL_GetKeyboardState(int *);
int SDL_UpdateTexture(SDL_Texture *, const void *, const void *, int);
int SDL_RenderClear(SDL_Renderer *); int SDL_RenderCopy(SDL_Renderer *, SDL_Texture *, const void *, const void *);
void SDL_RenderPresent(SDL_Renderer *); void SDL_Delay(Uint32);
int SDL_NumJoysticks(void); int SDL_IsGameController(int); SDL_GameController *SDL_GameControllerOpen(int); void SDL_GameControllerClose(SDL_GameController *);
Uint8 SDL_GameControllerGetButton(SDL_GameController *, int); int16_t SDL_GameControllerGetAxis(SDL_GameController *, int);
int SDL_QueueAudio(SDL_AudioDeviceID, const void *, Uint32); Uint32 SDL_GetQueuedAudioSize(SDL_AudioDeviceID);
int SDL_ShowSimpleMessageBox(Uint32, const char *, const char *, SDL_Window *);
#endif
