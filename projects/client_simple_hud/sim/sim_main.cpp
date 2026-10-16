// Native simulator for client_simple_hud: renders SimpleHudUi on the host with
// the device's lv_conf.h, fonts and RGB565 colour depth.
//
//   program --fixture f.txt --out shot.ppm   headless, one frame to a PPM file
//   program --fixture f.txt --window         SDL window (env:sim_sdl only)
//
// Fixture: key=value lines — speed, rpm, online, elapsed_ms. '#' comments.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lvgl.h>

#include "pin_config.h"
#include "simple_hud_ui.h"

#ifdef SIM_SDL
#include <SDL.h>
#endif

/** UI state a fixture describes. */
struct SimState {
    uint8_t  speed      = 0;
    uint16_t rpm        = 0;
    bool     online     = false;  // the board boots offline until the first payload
    uint32_t elapsed_ms = 0;      // time since boot; the HUD replaces the boot screen after ~3.2 s
};

static uint16_t s_fb[SCREEN_W * SCREEN_H];      // full RGB565 frame, as on the panel
static uint16_t s_draw_buf[SCREEN_W * SCREEN_H / 10];
static uint32_t s_tick_ms = 0;

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
        long val;
        if (sscanf(line, " %31[^=]=%ld", key, &val) != 2) continue;
        if      (!strcmp(key, "speed"))      st.speed      = (uint8_t)val;
        else if (!strcmp(key, "rpm"))        st.rpm        = (uint16_t)val;
        else if (!strcmp(key, "online"))     st.online     = val != 0;
        else if (!strcmp(key, "elapsed_ms")) st.elapsed_ms = (uint32_t)val;
        else fprintf(stderr, "fixture: unknown key '%s'\n", key);
    }
    fclose(f);
    return true;
}

/** Pushes state into the view. */
static void applyState(SimpleHudUi& ui, const SimState& st) {
    ui.setSpeed(st.speed);
    ui.setRpm(st.rpm);
    ui.setServerOnline(st.online);
}

/** Writes the framebuffer as a binary PPM, expanding RGB565 to RGB888. */
static bool writePpm(const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "cannot write %s\n", path);
        return false;
    }
    fprintf(f, "P6\n%d %d\n255\n", (int)SCREEN_W, (int)SCREEN_H);
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

static SimState    s_state;
static SimpleHudUi s_ui;

#ifdef SIM_SDL
/** Interactive loop: ↑/↓ speed, ←/→ rpm, O = online, R = reload, Esc quits. */
static int runWindow(const char* fixture, int zoom) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* win = SDL_CreateWindow("client_simple_hud sim", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       SCREEN_W * zoom, SCREEN_H * zoom, 0);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_RenderSetLogicalSize(ren, SCREEN_W, SCREEN_H);
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
                                         SCREEN_W, SCREEN_H);

    bool quit = false;
    while (!quit) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) quit = true;
            if (ev.type != SDL_KEYDOWN) continue;
            switch (ev.key.keysym.sym) {
            case SDLK_ESCAPE: quit = true; break;
            case SDLK_UP:     if (s_state.speed < 250) s_state.speed += 5; break;
            case SDLK_DOWN:   if (s_state.speed >= 5)  s_state.speed -= 5; break;
            case SDLK_RIGHT:  if (s_state.rpm < 6000)  s_state.rpm += 100; break;
            case SDLK_LEFT:   if (s_state.rpm >= 100)  s_state.rpm -= 100; break;
            case SDLK_o:      s_state.online = !s_state.online; break;
            case SDLK_r:      if (fixture) loadFixture(fixture, s_state); break;
            }
            applyState(s_ui, s_state);
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

    s_ui.build();
    applyState(s_ui, s_state);

    if (window) {
#ifdef SIM_SDL
        return runWindow(fixture, zoom);
#else
        fprintf(stderr, "--window needs the sim_sdl env\n");
        return 2;
#endif
    }

    // Run LVGL timers up to elapsed_ms (the boot fade), then a few more frames to settle.
    const uint32_t frames = s_state.elapsed_ms / LV_DEF_REFR_PERIOD + 10;
    for (uint32_t i = 0; i < frames; i++) {
        s_tick_ms += LV_DEF_REFR_PERIOD;
        lv_timer_handler();
    }
    lv_refr_now(disp);
    return (out && writePpm(out)) ? 0 : (out ? 1 : 0);
}
