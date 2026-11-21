// SDL2 front-end: window, input, frame pacing, save-RAM persistence.
#include "perf.h"
#include "parallel.h"
#include "nes.h"
#include "widescreen.h"
#include "shadow.h"
#include "zelda_ram.h"
#include <SDL.h>
#include <cstdio>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

static const int kFadeFrames = 60;
static int saveFade = 0, fadeIn = 0, holdBlack = 0;      // frames of the fade to black before / from black after SAVE AND QUIT
static uint8_t prevInput = 0;

static std::string savePathFor(const std::string& rom) {
    size_t dot = rom.find_last_of('.');
    size_t slash = rom.find_last_of("/\\");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return rom + ".sav";
    return rom.substr(0, dot) + ".sav";
}

static void loadSave(Nes& nes, const std::string& path) {
    if (!nes.cart.hasBattery()) return;
    std::ifstream f(path, std::ios::binary);
    if (!f) return;
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto& ram = nes.cart.saveRam();
    if (data.size() == ram.size()) { ram = data; std::printf("Loaded save: %s\n", path.c_str()); }
    else std::fprintf(stderr, "Ignoring save file %s (%zu bytes, expected %zu)\n", path.c_str(), data.size(), ram.size());
}

static void writeSave(Nes& nes, const std::string& path) {
    if (!nes.cart.hasBattery() || !nes.cart.saveRamDirty) return;
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) { std::fprintf(stderr, "Could not write save file %s\n", path.c_str()); return; }
    auto& ram = nes.cart.saveRam();
    f.write(reinterpret_cast<const char*>(ram.data()), (std::streamsize)ram.size());
    f.flush();
    if (f) nes.cart.saveRamDirty = false;                                 // (a failed write, a full disk say, is retried next time)
    else std::fprintf(stderr, "Writing save file %s failed\n", path.c_str());
}

static uint8_t readInput(SDL_GameController* pad) {
    const Uint8* k = SDL_GetKeyboardState(nullptr);
    uint8_t b = 0;
    if (k[SDL_SCANCODE_X])                                   b |= BTN_A;
    if (k[SDL_SCANCODE_Z])                                   b |= BTN_B;
    if (k[SDL_SCANCODE_RSHIFT] || k[SDL_SCANCODE_TAB] || k[SDL_SCANCODE_BACKSPACE]) b |= BTN_SELECT;
    if (k[SDL_SCANCODE_RETURN])                              b |= BTN_START;
    if (k[SDL_SCANCODE_UP]    || k[SDL_SCANCODE_W])          b |= BTN_UP;
    if (k[SDL_SCANCODE_DOWN]  || k[SDL_SCANCODE_S])          b |= BTN_DOWN;
    if (k[SDL_SCANCODE_LEFT]  || k[SDL_SCANCODE_A])          b |= BTN_LEFT;
    if (k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D])          b |= BTN_RIGHT;

    if (pad) {
        auto btn = [&](SDL_GameControllerButton x) { return SDL_GameControllerGetButton(pad, x) != 0; };
        if (btn(SDL_CONTROLLER_BUTTON_B))          b |= BTN_A;       // east button  -> NES A
        if (btn(SDL_CONTROLLER_BUTTON_A))          b |= BTN_B;       // south button -> NES B
        if (btn(SDL_CONTROLLER_BUTTON_BACK))       b |= BTN_SELECT;
        if (btn(SDL_CONTROLLER_BUTTON_START))      b |= BTN_START;
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_UP))    b |= BTN_UP;
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_DOWN))  b |= BTN_DOWN;
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_LEFT))  b |= BTN_LEFT;
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) b |= BTN_RIGHT;
        const int dead = 12000;
        int ax = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
        int ay = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
        if (ax < -dead) b |= BTN_LEFT;
        if (ax > dead)  b |= BTN_RIGHT;
        if (ay < -dead) b |= BTN_UP;
        if (ay > dead)  b |= BTN_DOWN;
    }
    // Real pads can't press opposite directions at once; keyboards can, and Zelda doesn't like it.
    if ((b & BTN_LEFT) && (b & BTN_RIGHT)) b &= ~(BTN_LEFT | BTN_RIGHT);
    if ((b & BTN_UP) && (b & BTN_DOWN))    b &= ~(BTN_UP | BTN_DOWN);
    return b;
}

int main(int argc, char** argv) {
    std::string romPath;
    bool optNoWide = false, optNoFog = false, optVerify = false, optTrace = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--no-wide") optNoWide = true;
        else if (a == "--no-fog") optNoFog = true;
        else if (a == "--verify") optVerify = true;
        else if (a == "--trace") optTrace = true;
        else if (a == "--perf") perf::enabled = true;
        else romPath = a;
    }
    if (romPath.empty()) {
        std::fprintf(stderr, "Usage: %s [--no-wide] [--no-fog] [--verify] [--trace] [--perf] <path-to-zelda.nes>\n", argv[0]);
        return 1;
    }
    const std::string savePath = savePathFor(romPath);
    std::string fogPath = savePath.substr(0, savePath.size() - 4) + ".fog";

    static Nes nes;                                   // large; keep off the stack
    std::string err;
    if (!nes.cart.load(romPath, err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    loadSave(nes, savePath);
    nes.reset();

    static Widescreen wide;
    static ShadowSet shadows;
    wide.shadows = &shadows;
    {
        std::string wideErr;
        if (!wide.init(nes, wideErr)) {
            std::fprintf(stderr, "Widescreen disabled: %s\n", wideErr.c_str());
            wide.enabled = false;
        } else {
            std::printf("Widescreen ready. F2 = fog of war, F3 = widescreen, F4 or +/- = zoom.\n");
        }
    }
    if (optNoWide) wide.enabled = false;
    if (optNoFog) wide.fog = false;
    wide.verify = optVerify;
    wide.trace = optTrace;
    wide.loadFog(fogPath);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");   // crisp pixels

    SDL_Window* win = SDL_CreateWindow("Zelda Wide", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       1280, 720, SDL_WINDOW_RESIZABLE);
    SDL_Renderer* ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED) : nullptr;
    if (win && !ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!win || !ren) { std::fprintf(stderr, "Window/renderer failed: %s\n", SDL_GetError()); return 1; }
    SDL_Texture* tex = nullptr;
    int texW = 0, texH = 0;
    // --perf: where does the time of a frame go on this machine? A line per second in the title and in <rom>.perf.txt (with what this machine is).
    std::FILE* perfFile = nullptr;
    if (perf::enabled) {
        perfFile = std::fopen((savePath.substr(0, savePath.size() - 4) + ".perf.txt").c_str(), "w");
        SDL_RendererInfo ri{}; SDL_GetRendererInfo(ren, &ri); SDL_version sv{}; SDL_GetVersion(&sv);
        std::FILE* o = perfFile ? perfFile : stderr;
        std::fprintf(o, "Zelda Wide performance log\nSDL %d.%d.%d, video driver %s, renderer %s (flags 0x%x: %s, %s), max texture %dx%d\n", sv.major, sv.minor, sv.patch,
                     SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "?", ri.name ? ri.name : "?", (unsigned)ri.flags,
                     (ri.flags & SDL_RENDERER_ACCELERATED) ? "accelerated" : "NOT accelerated", (ri.flags & SDL_RENDERER_PRESENTVSYNC) ? "vsync" : "no vsync", ri.max_texture_width, ri.max_texture_height);
        std::fprintf(o, "threads for the per-pixel passes: %d\n", par::maxThreads());
#if defined(_MSC_VER)
        std::fprintf(o, "compiler: MSVC %d\n", (int)_MSC_VER);
#elif defined(__VERSION__)
        std::fprintf(o, "compiler: %s\n", __VERSION__);
#endif
#ifdef NDEBUG
        std::fprintf(o, "build: optimized (NDEBUG)\n");
#else
        std::fprintf(o, "build: NOT optimized (no NDEBUG: a Debug build?)\n");
#endif
        std::fprintf(o, "times are ms per frame, averaged over one second: emu = the game, render = the compositor (world includes waterA), prep = clearing the canvas, room = the game's own picture of the screen (sprites, effects), backdrop = the copy kept for the cave box, upload = canvas to the GPU + drawing, present, wait = pacing sleep\n");
        std::fflush(o);
    }
    Canvas canvas;
    constexpr size_t kMaxOverlays = 8;
    struct OverlayTexture { SDL_Texture* tex = nullptr; int w = 0, h = 0; bool linear = false; };
    std::vector<uint32_t> overlayScratch;
    OverlayTexture overlayTex[kMaxOverlays];
    SDL_Texture* mistTex = nullptr; int mistTexW = 0, mistTexH = 0;      // the maze's mist: one window-sized layer, stretched smoothly over everything

    SDL_GameController* pad = nullptr;
    for (int i = 0; i < SDL_NumJoysticks() && !pad; i++)
        if (SDL_IsGameController(i)) pad = SDL_GameControllerOpen(i);

    const Uint64 freq = SDL_GetPerformanceFrequency();
    const double frameSeconds = 1.0 / 60.0988;
    Uint64 next = SDL_GetPerformanceCounter();
    Uint64 fpsMark = next;
    int fpsFrames = 0;
    Uint64 lastSave = next;
    bool running = true, warned = false;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                switch (e.key.keysym.scancode) {
                    case SDL_SCANCODE_ESCAPE: running = false; break;
                    case SDL_SCANCODE_F5:     nes.reset(); wide.resetLag(); break;
                    case SDL_SCANCODE_F2:     wide.fog = !wide.fog; std::printf("Fog of war: %s\n", wide.fog ? "on" : "off (map revealed)"); break;
                    case SDL_SCANCODE_F6:     wide.interiorsBig = !wide.interiorsBig; std::printf("Caves and dungeons: %s\n", wide.interiorsBig ? "big picture" : "same view as the overworld"); break;
                    case SDL_SCANCODE_F7:     wide.hudOverlay = !wide.hudOverlay; std::printf("HUD: %s\n", wide.hudOverlay ? "overlay in the corners" : "original bar on top"); break;
                    case SDL_SCANCODE_F8: {
                        wide.minimapStyle = (wide.minimapStyle + 1) % 3;
                        static const char* names[] = {"original (grey)", "discovered screens in blue", "scaled-down map"};
                        std::printf("Minimap: %s\n", names[wide.minimapStyle]);
                        break;
                    }
                    case SDL_SCANCODE_F10:    if (nes.ram[zelda::kGameMode] == zelda::kModePlay && saveFade == 0) { saveFade = 1; std::printf("Saving the game...\n"); } break;
                    case SDL_SCANCODE_F1:     wide.lighting = !wide.lighting; std::printf("Day/night and lights: %s\n", wide.lighting ? "on" : "off"); break;
                    case SDL_SCANCODE_T:      wide.dayClock.skip(0.10); std::printf("Time of day: %02d:%02d\n", (int)wide.dayClock.hour(), (int)(std::fmod(wide.dayClock.hour(), 1.0) * 60)); break;
                    case SDL_SCANCODE_L:      wide.dayClock.bolt = 0; std::printf("Lightning (visible when it is dark)\n"); break;
                    case SDL_SCANCODE_N:      shadows.enabled = !shadows.enabled; if (!shadows.enabled) shadows.clear(); std::printf("Neighbour screens running (shadow games): %s\n", shadows.enabled ? "on" : "off"); break;
                    case SDL_SCANCODE_PAGEUP: if (shadows.dump("shadow_debug.txt")) std::printf("shadow_debug.txt written: %s\n", shadows.summary().c_str()); break;
                    case SDL_SCANCODE_HOME:   wide.dumpWaterDebug("water_debug.txt"); std::printf("water_debug.txt written\n"); break;
                    case SDL_SCANCODE_END:    wide.waterReflections = !wide.waterReflections; std::printf("Water reflections: %s\n", wide.waterReflections ? "on" : "off"); break;
                    case SDL_SCANCODE_PAGEDOWN: wide.waterWaves = !wide.waterWaves; std::printf("Water waves: %s\n", wide.waterWaves ? "on" : "off"); break;
                    case SDL_SCANCODE_INSERT: wide.nesLimits = !wide.nesLimits; std::printf("NES limits (sprite flicker and slowdown): %s\n", wide.nesLimits ? "on" : "off"); break;
                    case SDL_SCANCODE_F12:    wide.cloudShadow = !wide.cloudShadow; std::printf("Cloud shadows: %s\n", wide.cloudShadow ? "on" : "off"); break;
                    case SDL_SCANCODE_F9:     wide.caveBox = !wide.caveBox; std::printf("Caves: %s\n", wide.caveBox ? "in a box over the overworld" : "replace the picture"); break;
                    case SDL_SCANCODE_F4:     wide.cycleZoom(); std::printf("View height: %d px\n", wide.viewHeight); break;
                    case SDL_SCANCODE_EQUALS: case SDL_SCANCODE_KP_PLUS:  wide.adjustZoom(-16); break;   // smaller view = bigger picture
                    case SDL_SCANCODE_MINUS:  case SDL_SCANCODE_KP_MINUS: wide.adjustZoom(+16); break;
                    case SDL_SCANCODE_F3:     if (wide.available()) { wide.enabled = !wide.enabled; std::printf("Widescreen: %s\n", wide.enabled ? "on" : "off"); } break;
                    case SDL_SCANCODE_F11: {
                        bool full = SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP;
                        SDL_SetWindowFullscreen(win, full ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                        break;
                    }
                    default: break;
                }
            } else if (e.type == SDL_CONTROLLERDEVICEADDED && !pad) {
                pad = SDL_GameControllerOpen(e.cdevice.which);
            }
        }

        // While Link glides across a screen border the game stays paused (the compositor draws the glide).
        // SAVE AND QUIT: SELECT while the inventory is open fades the picture to black (the game stays paused), saves, and the
        // title/file select then fades in again. SELECT is not passed to the game in the menu.
        uint8_t input = readInput(pad);
        const uint8_t pressed = (uint8_t)(input & ~prevInput);
        prevInput = input;
        if (Widescreen::menuOpenForSave(nes)) {
            // SELECT asks "SAVE AND QUIT?" (YES / NO); the game sees no buttons while the question is up.
            if (saveFade == 0 && wide.saveConfirmInput(nes, pressed)) { saveFade = 1; std::printf("Saving the game...\n"); }
            input &= (uint8_t)~BTN_SELECT;
            if (wide.saveConfirm || saveFade > 0) input = 0;
        } else wide.saveConfirm = false;
        const auto pEmu = perf::now();
        if (saveFade > 0 && saveFade < kFadeFrames) {
            saveFade++;
        } else if (wide.gliding()) {
            shadows.animate(&nes);                                   // the monsters of the neighbouring screens keep moving while the camera glides
        } else if (!wide.mazeFading()) {
            nes.pad1 = input;
            if (saveFade >= kFadeFrames) { saveFade = 0; holdBlack = 1; wide.requestSave(nes); }
            wide.prepareFrame(nes);
            nes.runFrame();
            wide.afterFrame(nes);
            shadows.update(nes);                                 // the shadow games of the neighbouring screens (N)

            // Scrolling to the next overworld screen (mode 7) is skipped: fast-forward the game's own scroll
            // animation without showing it, then let Link walk the few pixels the game moved him (startGlide).
            // Everything else (walking out of a cave, dungeon scrolls) plays normally.
            if (wide.enabled && wide.available()) {
                // The inventory screen (MenuState: 8 = open on the overworld, 7 in dungeons) normally slides down over
                // 78 frames and back up again. Skip both slides: it simply appears, and disappears, whole.
                const int mode0 = nes.ram[zelda::kGameMode];
                if (mode0 == zelda::kModePlay || mode0 == zelda::kModeCellar || mode0 == zelda::kModeCave || mode0 == zelda::kModeShortcut) {
                    const int openState = nes.ram[zelda::kCurLevel] == 0 ? 8 : 7;
                    int guard = 0;
                    bool skipped = false;
                    nes.ppu.lite = true;       // (the frames nobody sees are not drawn: the game runs the same, much faster)
                    while (nes.ram[zelda::kMenuState] != 0 && nes.ram[zelda::kMenuState] != openState && guard++ < 400) {
                        nes.runFrame();
                        skipped = true;
                    }
                    nes.ppu.lite = false;
                    if (skipped) {
                        nes.runFrame();            // show the first frame of the final picture
                        wide.resetLag();
                    }
                }
                int guard = 0;
                bool skipped = false;
                nes.ppu.lite = true;           // (the scroll nobody sees is not drawn: about 60 frames of game logic without the picture; the last frame below is drawn)
                while (nes.ram[zelda::kCurLevel] == 0 && nes.ram[zelda::kGameMode] == zelda::kModeScroll && !wide.keepScroll(nes) && guard++ < 600) {
                    nes.runFrame();
                    skipped = true;
                }
                nes.ppu.lite = false;
                if (skipped) {
                    shadows.carryTiles(nes);   // the statues of armos that woke here are gone before the first picture of the screen (N)
                    nes.runFrame();            // let the game finish its first frame on the new screen
                    wide.resetLag();
                    shadows.armHandOff(nes);  // the monsters of the shadow of this screen take over once the game has made its own (N5)
                    wide.startGlide(nes);
                }
            }
        }

        perf::add(perf::kEmu, pEmu);
        if (nes.cpu.badOpcode >= 0 && !warned) {
            std::fprintf(stderr, "Warning: CPU hit unofficial opcode $%02X at PC=$%04X\n", nes.cpu.badOpcode, nes.cpu.pc);
            warned = true;
        }

        // The compositor decides the canvas size (it depends on the view mode and the window's shape).
        int ow = 0, oh = 0;
        SDL_GetRendererOutputSize(ren, &ow, &oh);
        const auto pRender = perf::now();
        wide.render(nes, oh > 0 ? (double)ow / oh : 4.0 / 3.0, canvas);
        perf::add(perf::kRender, pRender);
        const auto pUpload = perf::now();
        if (canvas.w != texW || canvas.h != texH || !tex) {
            texW = canvas.w; texH = canvas.h;
            if (tex) SDL_DestroyTexture(tex);
            // Crisp pixels at the normal size; gentle smoothing when zoomed out (scale is rarely a whole number).
            SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, texH > Ppu::H ? "1" : "0");
            tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, texW, texH);
            SDL_RenderSetLogicalSize(ren, texW, texH);
        }
        SDL_UpdateTexture(tex, nullptr, canvas.pixels.data(), texW * 4);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, nullptr, nullptr);

        // Overlay pictures (inventory box, GAME OVER text, later the HUD): drawn in window pixels, each at the
        // largest whole-number scale that fits, so they stay crisp and don't change size with the picture zoom.
        const Overlay& ov = wide.overlay;
        if (ov.active()) {
            SDL_RenderSetLogicalSize(ren, 0, 0);
            if (ov.dim > 0.0f) {
                SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(ren, 0, 0, 0, (Uint8)(ov.dim * 255.0f));
                SDL_RenderFillRect(ren, nullptr);
            }
            for (size_t i = 0; i < ov.images.size() && i < kMaxOverlays; i++) {
                const OverlayImage& img = ov.images[i];
                OverlayTexture& ot = overlayTex[i];
                // Scale: whole numbers, except for pieces that ask for a fractional size (heightPerScale + sharpScale):
                // those are blown up by the next whole number and shrunk smoothly, which stays nearly as crisp.
                float scale;
                if (img.heightPerScale > 0 && img.sharpScale) scale = (float)oh / img.heightPerScale;
                else if (img.heightPerScale > 0)              scale = (float)(oh / img.heightPerScale);
                else scale = (float)(int)std::min(img.maxWidthFrac * ow / img.w, img.maxHeightFrac * oh / img.h);
                if (scale < 1.0f) scale = 1.0f;
                const bool sharp = img.heightPerScale > 0 && img.sharpScale && std::fabs(scale - std::round(scale)) > 0.01f;
                const int k = sharp ? (int)std::ceil(scale) : 1;
                const int tw = img.w * k, th = img.h * k;
                if (!ot.tex || ot.w != tw || ot.h != th || ot.linear != sharp) {
                    if (ot.tex) SDL_DestroyTexture(ot.tex);
                    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, sharp ? "1" : "0");
                    ot.tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, tw, th);
                    SDL_SetTextureBlendMode(ot.tex, SDL_BLENDMODE_BLEND);
                    ot.w = tw; ot.h = th; ot.linear = sharp;
                }
                if (k == 1) {
                    SDL_UpdateTexture(ot.tex, nullptr, img.pixels.data(), img.w * 4);
                } else {
                    overlayScratch.resize((size_t)tw * th);
                    for (int y = 0; y < th; y++)
                        for (int x = 0; x < tw; x++) overlayScratch[(size_t)y * tw + x] = img.pixels[(size_t)(y / k) * img.w + (x / k)];
                    SDL_UpdateTexture(ot.tex, nullptr, overlayScratch.data(), tw * 4);
                }
                SDL_Rect dst;
                dst.w = (int)std::lround(img.w * scale); dst.h = (int)std::lround(img.h * scale);
                dst.x = (int)(img.anchorX * ow - img.pivotX * dst.w);
                dst.y = (int)(img.anchorY * oh - img.pivotY * dst.h);
                SDL_RenderCopy(ren, ot.tex, nullptr, &dst);
            }
            if (ov.mistW > 0 && ov.mist.size() == (size_t)ov.mistW * ov.mistH) {
                if (!mistTex || mistTexW != ov.mistW || mistTexH != ov.mistH) {
                    if (mistTex) SDL_DestroyTexture(mistTex);
                    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
                    mistTex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, ov.mistW, ov.mistH);
                    SDL_SetTextureBlendMode(mistTex, SDL_BLENDMODE_BLEND);
                    mistTexW = ov.mistW; mistTexH = ov.mistH;
                }
                SDL_UpdateTexture(mistTex, nullptr, ov.mist.data(), ov.mistW * 4);
                SDL_RenderCopy(ren, mistTex, nullptr, nullptr);                 // (over the picture and the HUD alike)
            }
            SDL_RenderSetLogicalSize(ren, texW, texH);
        }
        {   // fade to / from black around the save
            // stay fully black while the game runs its save frames (mode $0D shows the frozen play area), then fade in
            if (holdBlack > 0 && (++holdBlack > 8) && nes.ram[zelda::kGameMode] != zelda::kModeSave) { holdBlack = 0; fadeIn = kFadeFrames; }
            const int f = saveFade > 0 ? saveFade : holdBlack > 0 ? kFadeFrames : fadeIn;
            if (f > 0) {
                SDL_RenderSetLogicalSize(ren, 0, 0);
                SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(ren, 0, 0, 0, (Uint8)(std::min(f, kFadeFrames) * 255 / kFadeFrames));
                SDL_RenderFillRect(ren, nullptr);
                SDL_RenderSetLogicalSize(ren, texW, texH);
            }
            if (saveFade == 0 && holdBlack == 0 && fadeIn > 0) fadeIn--;
        }
        perf::add(perf::kUpload, pUpload);
        const auto pPresent = perf::now();
        SDL_RenderPresent(ren);
        perf::add(perf::kPresent, pPresent);
        const auto pWait = perf::now();

        // Frame pacing (hold ` for fast-forward).
        Uint64 now = SDL_GetPerformanceCounter();
        bool turbo = SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_GRAVE];
        if (turbo) {
            next = now;
        } else {
            next += (Uint64)(frameSeconds * (double)freq);
            if (next > now) {
                double ms = (double)(next - now) * 1000.0 / (double)freq;
                if (ms > 2.0) SDL_Delay((Uint32)(ms - 1.5));
                while (SDL_GetPerformanceCounter() < next) {}
            } else if (now - next > freq / 10) {
                next = now;                           // fell far behind; resync instead of racing to catch up
            }
        }

        perf::add(perf::kWait, pWait);
        fpsFrames++;
        if (now - fpsMark >= freq) {
            char title[96];
            const int scr = wide.currentScreen();
            if (scr >= 0) std::snprintf(title, sizeof title, "Zelda Wide - %d fps - screen column %d, row %d", fpsFrames, scr & 15, scr >> 4);
            else std::snprintf(title, sizeof title, "Zelda Wide - %d fps", fpsFrames);
            SDL_SetWindowTitle(win, title);
            if (perf::enabled) {
                const double f = fpsFrames, *t = perf::total;
                char line[480];
                std::snprintf(line, sizeof line, "%d fps | emu %.1f | render %.1f (world %.1f, waterA %.1f, waterB %.1f, clouds %.1f, light %.1f) | prep %.1f, room %.1f (water %.1f), backdrop %.1f | upload %.1f | present %.1f | wait %.1f | canvas %dx%d | screen %d",
                              fpsFrames, t[perf::kEmu] / f, t[perf::kRender] / f, t[perf::kWorld] / f, t[perf::kWaterA] / f, t[perf::kWaterB] / f, t[perf::kClouds] / f, t[perf::kLight] / f,
                              t[perf::kPrep] / f, t[perf::kRoom] / f, t[perf::kRoomWater] / f, t[perf::kBackdrop] / f, t[perf::kUpload] / f, t[perf::kPresent] / f, t[perf::kWait] / f, canvas.w, canvas.h, scr);
                SDL_SetWindowTitle(win, (std::string("Zelda Wide - ") + line).c_str());
                if (perfFile) { std::fprintf(perfFile, "%s\n", line); std::fflush(perfFile); }
                for (double& d : perf::total) d = 0;
            }
            fpsFrames = 0; fpsMark = now;
        }
        if (now - lastSave >= 3 * freq) { writeSave(nes, savePath); wide.saveFogIfDirty(fogPath); lastSave = now; }
    }

    writeSave(nes, savePath);
    wide.saveFogIfDirty(fogPath);
    if (pad) SDL_GameControllerClose(pad);
    if (tex) SDL_DestroyTexture(tex);
    for (auto& ot : overlayTex) if (ot.tex) SDL_DestroyTexture(ot.tex);
    if (mistTex) SDL_DestroyTexture(mistTex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
