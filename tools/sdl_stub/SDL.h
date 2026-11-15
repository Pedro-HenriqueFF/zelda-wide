#pragma once
#include <cstdint>
struct SDL_Rect { int x, y, w, h; };
enum SDL_BlendMode { SDL_BLENDMODE_BLEND = 1 };
typedef uint8_t Uint8; typedef uint32_t Uint32; typedef uint64_t Uint64;
struct SDL_Window; struct SDL_Renderer; struct SDL_Texture; struct SDL_GameController;
enum { SDL_INIT_VIDEO=1, SDL_INIT_GAMECONTROLLER=2, SDL_WINDOWPOS_CENTERED=0, SDL_WINDOW_RESIZABLE=1,
  SDL_WINDOW_FULLSCREEN_DESKTOP=2, SDL_RENDERER_ACCELERATED=1, SDL_RENDERER_SOFTWARE=2, SDL_RENDERER_PRESENTVSYNC=4,
  SDL_PIXELFORMAT_ARGB8888=1, SDL_TEXTUREACCESS_STREAMING=1, SDL_QUIT=1, SDL_KEYDOWN=2, SDL_CONTROLLERDEVICEADDED=3 };
#define SDL_HINT_RENDER_SCALE_QUALITY "x"
enum SDL_Scancode { SDL_SCANCODE_X, SDL_SCANCODE_Z, SDL_SCANCODE_F1, SDL_SCANCODE_T, SDL_SCANCODE_L, SDL_SCANCODE_RSHIFT, SDL_SCANCODE_TAB, SDL_SCANCODE_BACKSPACE,
  SDL_SCANCODE_RETURN, SDL_SCANCODE_UP, SDL_SCANCODE_W, SDL_SCANCODE_DOWN, SDL_SCANCODE_S, SDL_SCANCODE_LEFT,
  SDL_SCANCODE_A, SDL_SCANCODE_RIGHT, SDL_SCANCODE_D, SDL_SCANCODE_ESCAPE, SDL_SCANCODE_F5, SDL_SCANCODE_F2, SDL_SCANCODE_F3, SDL_SCANCODE_F4, SDL_SCANCODE_F6, SDL_SCANCODE_F7, SDL_SCANCODE_F8, SDL_SCANCODE_F9, SDL_SCANCODE_F10, SDL_SCANCODE_F11, SDL_SCANCODE_F12, SDL_SCANCODE_END, SDL_SCANCODE_PAGEDOWN, SDL_SCANCODE_PAGEUP, SDL_SCANCODE_N, SDL_SCANCODE_INSERT, SDL_SCANCODE_HOME, SDL_SCANCODE_GRAVE,
  SDL_SCANCODE_EQUALS, SDL_SCANCODE_MINUS, SDL_SCANCODE_KP_PLUS, SDL_SCANCODE_KP_MINUS };
enum SDL_GameControllerButton { SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_START,
  SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT };
enum SDL_GameControllerAxis { SDL_CONTROLLER_AXIS_LEFTX, SDL_CONTROLLER_AXIS_LEFTY };
struct SDL_Keysym { SDL_Scancode scancode; };
struct SDL_KeyboardEvent { Uint32 type; Uint8 repeat; SDL_Keysym keysym; };
struct SDL_ControllerDeviceEvent { Uint32 type; int which; };
union SDL_Event { Uint32 type; SDL_KeyboardEvent key; SDL_ControllerDeviceEvent cdevice; };
int SDL_Init(Uint32); const char* SDL_GetError(); int SDL_SetHint(const char*, const char*);
SDL_Window* SDL_CreateWindow(const char*, int, int, int, int, Uint32);
SDL_Renderer* SDL_CreateRenderer(SDL_Window*, int, Uint32);
struct SDL_RendererInfo { const char* name; Uint32 flags; Uint32 num_texture_formats; Uint32 texture_formats[16]; int max_texture_width; int max_texture_height; };
struct SDL_version { Uint8 major, minor, patch; };
int SDL_GetRendererInfo(SDL_Renderer*, SDL_RendererInfo*); void SDL_GetVersion(SDL_version*); const char* SDL_GetCurrentVideoDriver();
int SDL_RenderSetLogicalSize(SDL_Renderer*, int, int); int SDL_GetRendererOutputSize(SDL_Renderer*, int*, int*);
SDL_Texture* SDL_CreateTexture(SDL_Renderer*, Uint32, int, int, int);
int SDL_NumJoysticks(); int SDL_IsGameController(int); SDL_GameController* SDL_GameControllerOpen(int);
void SDL_GameControllerClose(SDL_GameController*);
int SDL_GameControllerGetButton(SDL_GameController*, SDL_GameControllerButton);
int16_t SDL_GameControllerGetAxis(SDL_GameController*, SDL_GameControllerAxis);
const Uint8* SDL_GetKeyboardState(int*); int SDL_PollEvent(SDL_Event*);
Uint32 SDL_GetWindowFlags(SDL_Window*); int SDL_SetWindowFullscreen(SDL_Window*, Uint32);
int SDL_UpdateTexture(SDL_Texture*, const void*, const void*, int);
int SDL_RenderClear(SDL_Renderer*); int SDL_RenderCopy(SDL_Renderer*, SDL_Texture*, const SDL_Rect*, const SDL_Rect*);
void SDL_RenderPresent(SDL_Renderer*); Uint64 SDL_GetPerformanceCounter(); Uint64 SDL_GetPerformanceFrequency();
void SDL_Delay(Uint32); void SDL_SetWindowTitle(SDL_Window*, const char*);
void SDL_DestroyTexture(SDL_Texture*); void SDL_DestroyRenderer(SDL_Renderer*); void SDL_DestroyWindow(SDL_Window*); void SDL_Quit();

int SDL_SetRenderDrawBlendMode(SDL_Renderer*, SDL_BlendMode); int SDL_SetRenderDrawColor(SDL_Renderer*, Uint8, Uint8, Uint8, Uint8);
int SDL_RenderFillRect(SDL_Renderer*, const SDL_Rect*); int SDL_SetTextureBlendMode(SDL_Texture*, SDL_BlendMode);
