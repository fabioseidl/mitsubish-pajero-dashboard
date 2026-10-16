// Native simulator for main_hud: renders HudUi on the host with the device's
// lv_conf.h, fonts and RGB565 colour depth.
//
//   program --fixture f.txt --out shot.ppm   headless, one frame to a PPM file
//   program --fixture f.txt --window         SDL window (env:sim_sdl only)
//
// Fixture: key=value lines — speed, online, brightness, buttons. '#' comments.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lvgl.h>

#include "pin_config.h"
#include "hud_ui.h"

#ifdef SIM_SDL
#include <SDL.h>
#endif

/** UI state a fixture describes. */
struct SimState {
    uint8_t speed      = 0;
    bool    online     = true;
    uint8_t brightness = 50;
    bool    buttons    = true;
};

static uint16_t s_fb[SCREEN_W * SCREEN_H];      // full RGB565 frame, as on the canvas
static uint16_t s_draw_buf[SCREEN_W * SCREEN_H / 10];
static uint32_t s_tick_ms = 0;

static int32_t s_mouse_x = 0;
static int32_t s_mouse_y = 0;
static bool    s_mouse_down = false;

/** Fake millisecond clock; headless runs advance it by hand for repeatable frames. */
static uint32_t tickCb() { return s_tick_ms; }

/** Copies a rendered area into the full framebuffer. */
static void flushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    const int32_t w = lv_area_get_width(area);
    const uint16_t* src = (const uint16_t*)px_map;
    for (int32_t y = area->y1; y <= area->y2; y++) {
        memcpy(&s_fb[y * SCREEN_W + area->x1], src, (size_t)w * sizeof(uint16_t));
        src += w;
    }
    lv_display_flush_ready(disp);
}

/** Reports the SDL mouse as the touch panel. */
static void pointerReadCb(lv_indev_t* indev, lv_indev_data_t* data) {
    LV_UNUSED(indev);
    data->point.x = s_mouse_x;
    data->point.y = s_mouse_y;
    data->state   = s_mouse_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

/** Parses a key=value fixture file into state; unknown keys are reported. */
static bool loadFixture(const char* path, SimState& st) {
    FILE* f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "cannot open fixture %s\n", path);
        return false;
    }
    char line[128];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char key[32];
        int  val;
        if (sscanf(line, " %31[^=]=%d", key, &val) != 2) continue;
        if      (!strcmp(key, "speed"))      st.speed      = (uint8_t)val;
        else if (!strcmp(key, "online"))     st.online     = val != 0;
        else if (!strcmp(key, "brightness")) st.brightness = (uint8_t)val;
        else if (!strcmp(key, "buttons"))    st.buttons    = val != 0;
        else fprintf(stderr, "fixture: unknown key '%s'\n", key);
    }
    fclose(f);
    return true;
}

/** Pushes state into the view. */
static void applyState(HudUi& ui, const SimState& st) {
    ui.setSpeed(st.speed, st.online);
    ui.setBrightnessPercent(st.brightness);
    ui.setButtonsVisible(st.buttons);
}

/** Writes the framebuffer as a binary PPM, expanding RGB565 to RGB888. */
static bool writePpm(const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "cannot write %s\n", path);
        return false;
    }
    fprintf(f, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
    for (int32_t i = 0; i < SCREEN_W * SCREEN_H; i++) {
        const uint16_t c = s_fb[i];
        const uint8_t r5 = (c >> 11) & 0x1F, g6 = (c >> 5) & 0x3F, b5 = c & 0x1F;
        const uint8_t rgb[3] = {(uint8_t)((r5 << 3) | (r5 >> 2)),
                                (uint8_t)((g6 << 2) | (g6 >> 4)),
                                (uint8_t)((b5 << 3) | (b5 >> 2))};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    return true;
}

static SimState s_state;
static HudUi    s_ui;

/** Button handlers: step brightness by 10 and clamp, like StepBrightness. */
static void onUp(void*)   { if (s_state.brightness < 100) s_state.brightness += 10; s_ui.setBrightnessPercent(s_state.brightness); }
static void onDown(void*) { if (s_state.brightness > 10)  s_state.brightness -= 10; s_ui.setBrightnessPercent(s_state.brightness); }

#ifdef SIM_SDL
/** Interactive loop: mouse = touch, arrows = speed, O = online, B = buttons, R = reload. */
static int runWindow(const char* fixture, int zoom) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* win = SDL_CreateWindow("main_hud sim", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       SCREEN_W * zoom, SCREEN_H * zoom, 0);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_RenderSetLogicalSize(ren, SCREEN_W, SCREEN_H);
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
                                         SCREEN_W, SCREEN_H);

    bool quit = false;
    while (!quit) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT: quit = true; break;
            case SDL_MOUSEBUTTONDOWN: s_mouse_down = true;  break;
            case SDL_MOUSEBUTTONUP:   s_mouse_down = false; break;
            case SDL_MOUSEMOTION:     s_mouse_x = ev.motion.x; s_mouse_y = ev.motion.y; break;
            case SDL_KEYDOWN:
                switch (ev.key.keysym.sym) {
                case SDLK_ESCAPE: quit = true; break;
                case SDLK_UP:     if (s_state.speed < 250) s_state.speed += 5; break;
                case SDLK_DOWN:   if (s_state.speed >= 5)  s_state.speed -= 5; break;
                case SDLK_o:      s_state.online  = !s_state.online;  break;
                case SDLK_b:      s_state.buttons = !s_state.buttons; break;
                case SDLK_r:      if (fixture) loadFixture(fixture, s_state); break;
                }
                applyState(s_ui, s_state);
                break;
            }
        }
        s_tick_ms = SDL_GetTicks();
        lv_timer_handler();

        SDL_UpdateTexture(tex, nullptr, s_fb, SCREEN_W * sizeof(uint16_t));
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, nullptr, nullptr);
        SDL_RenderPresent(ren);
        SDL_Delay(5);
    }
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
#endif

/** Entry point: parses args, builds the UI, then renders headless or opens a window. */
int main(int argc, char** argv) {
    const char* fixture = nullptr;
    const char* out     = nullptr;
    bool window = false;
    int  zoom   = 2;
    for (int i = 1; i < argc; i++) {
        if      (!strcmp(argv[i], "--fixture") && i + 1 < argc) fixture = argv[++i];
        else if (!strcmp(argv[i], "--out")     && i + 1 < argc) out     = argv[++i];
        else if (!strcmp(argv[i], "--zoom")    && i + 1 < argc) zoom    = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--window")) window = true;
        else {
            fprintf(stderr, "usage: %s [--fixture f.txt] (--out shot.ppm | --window [--zoom N])\n", argv[0]);
            return 2;
        }
    }
    if (fixture && !loadFixture(fixture, s_state)) return 1;

    lv_init();
    lv_tick_set_cb(tickCb);
    lv_display_t* disp = lv_display_create(SCREEN_W, SCREEN_H);
    lv_display_set_flush_cb(disp, flushCb);
    lv_display_set_buffers(disp, s_draw_buf, nullptr, sizeof s_draw_buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, pointerReadCb);

    s_ui.build(lv_screen_active(), onUp, onDown, nullptr);
    applyState(s_ui, s_state);

    if (window) {
#ifdef SIM_SDL
        return runWindow(fixture, zoom);
#else
        fprintf(stderr, "--window needs the sim_sdl env\n");
        return 2;
#endif
    }

    // A few ticks so layout, invalidation and any short animation settle.
    for (int i = 0; i < 10; i++) {
        s_tick_ms += LV_DEF_REFR_PERIOD;
        lv_timer_handler();
    }
    lv_refr_now(disp);
    return (out && writePpm(out)) ? 0 : (out ? 1 : 0);
}
