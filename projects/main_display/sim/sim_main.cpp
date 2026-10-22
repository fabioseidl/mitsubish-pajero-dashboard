// Native simulator for main_display: renders the dashboard through the real
// app_ui bridge on the host, with the device's lv_conf.h, fonts and RGB565 depth.
//
//   program --fixture f.txt --out shot.ppm   headless, one frame to a PPM file
//   program --fixture f.txt --window         SDL window (env:sim_sdl only)
//
// Fixture: key=value lines, '#' comments. Numeric keys fill a Payload; gps_time,
// altitude and course are the GPS strings; brightness steps the backlight cycle to
// that percent; elapsed_ms runs LVGL timers that long.

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lvgl.h>

#include "app_ui.h"
#include "payload.h"

#ifdef SIM_SDL
#include <SDL.h>
#endif

// Panel geometry, as registered with lv_display_create() in src/main.cpp.
static constexpr int32_t SCREEN_W = 1024;
static constexpr int32_t SCREEN_H = 600;

/** Everything a fixture can set: the payload plus the client-side sensor inputs. */
struct SimState {
    Payload payload    = {};
    bool    online     = true;
    char    gps_time[24] = "2026-10-16 14:35:00";
    char    altitude[12] = "812";
    char    course[12]   = "47";
    float   env_temp_c   = 24.5f;
    uint8_t  brightness  = 50;   // StepBrightness boot level
    uint32_t elapsed_ms  = 0;
};

static uint16_t s_fb[SCREEN_W * SCREEN_H];      // full RGB565 frame, as on the panel
static uint16_t s_draw_buf[SCREEN_W * 80];      // 80-line partial buffer, as on the board
static uint32_t s_tick_ms = 0;

static int32_t s_mouse_x = 0;
static int32_t s_mouse_y = 0;
static bool    s_mouse_down = false;

static SimState s_state;

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

/** Copies a fixture string value into a fixed buffer. */
static void copyText(char* dst, size_t n, const char* src) {
    snprintf(dst, n, "%s", src);
}

/** Applies one key=value pair; returns false for an unknown key. */
static bool applyKey(SimState& st, const char* key, const char* val) {
    Payload& p = st.payload;
    const float f = strtof(val, nullptr);
    if      (!strcmp(key, "speed"))           p.speed_kmh_x128 = (uint16_t)lroundf(f * 128.0f);
    else if (!strcmp(key, "rpm"))             p.rpm = (uint16_t)f;
    else if (!strcmp(key, "fuel_rate"))       p.fuel_rate_l_per_h = f;
    else if (!strcmp(key, "consumption"))     p.consumption_km_per_l = f;
    else if (!strcmp(key, "avg_consumption")) p.avg_consumption_km_per_l = f;
    else if (!strcmp(key, "distance"))        p.distance_km = f;
    else if (!strcmp(key, "trip_time_s"))     p.trip_time_s = (uint32_t)f;
    else if (!strcmp(key, "baro"))            p.baro_pressure_kpa = (uint8_t)f;
    else if (!strcmp(key, "ambient"))         p.ambient_temp_c = f;
    else if (!strcmp(key, "boost"))           p.boost_pres = f;
    else if (!strcmp(key, "load"))            p.engine_load_pct = f;
    else if (!strcmp(key, "throttle"))        p.throttle_pct = f;
    else if (!strcmp(key, "iat"))             p.intake_air_temp_c = f;
    else if (!strcmp(key, "cat"))             p.catalyst_temp_c = f;
    else if (!strcmp(key, "mil"))             p.mil_on = f != 0.0f;
    else if (!strcmp(key, "dtc"))             p.dtc_count = (uint8_t)f;
    else if (!strcmp(key, "body"))            p.body_flags = (uint8_t)f;
    else if (!strcmp(key, "climate"))         p.climate_flags = (uint8_t)f;
    else if (!strcmp(key, "fan"))             p.climate_fan = (uint8_t)f;
    else if (!strcmp(key, "set_temp"))        p.climate_set_temp_c = (uint8_t)f;
    else if (!strcmp(key, "coolant"))         p.coolant_temp_c = f;
    else if (!strcmp(key, "voltage"))         p.module_voltage_v = f;
    else if (!strcmp(key, "gear"))            p.at_gear_pos = f;
    else if (!strcmp(key, "target_gear"))     p.at_target_gear = f;
    else if (!strcmp(key, "ax"))              p.imu_accel_x_ms2 = f;
    else if (!strcmp(key, "ay"))              p.imu_accel_y_ms2 = f;
    else if (!strcmp(key, "az"))              p.imu_accel_z_ms2 = f;
    else if (!strcmp(key, "gx"))              p.imu_gyro_x_rads = f;
    else if (!strcmp(key, "gy"))              p.imu_gyro_y_rads = f;
    else if (!strcmp(key, "gz"))              p.imu_gyro_z_rads = f;
    else if (!strcmp(key, "env_temp"))        st.env_temp_c = f;
    else if (!strcmp(key, "online"))          st.online = f != 0.0f;
    else if (!strcmp(key, "elapsed_ms"))      st.elapsed_ms = (uint32_t)f;
    else if (!strcmp(key, "brightness"))      st.brightness = (uint8_t)f;
    else if (!strcmp(key, "gps_time"))        copyText(st.gps_time, sizeof st.gps_time, val);
    else if (!strcmp(key, "altitude"))        copyText(st.altitude, sizeof st.altitude, val);
    else if (!strcmp(key, "course"))          copyText(st.course, sizeof st.course, val);
    else return false;
    return true;
}

/** Parses a key=value fixture file into state; unknown keys are reported. */
static bool loadFixture(const char* path, SimState& st) {
    FILE* f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "cannot open fixture %s\n", path);
        return false;
    }
    st = SimState{};
    st.payload.version = PAYLOAD_VERSION;
    st.payload.flags   = PAYLOAD_FLAG_DATA_VALID;
    char line[160];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '#' || line[0] == '\0') continue;
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        if (!applyKey(st, line, eq + 1)) fprintf(stderr, "fixture: unknown key '%s'\n", line);
    }
    fclose(f);
    return true;
}

/** Pushes state through app_ui, formatting the sensor strings as src/main.cpp does. */
static void applyState(const SimState& st) {
    app_ui::update(st.payload);
    app_ui::set_server_status(st.online);
    app_ui::set_gps_datetime(st.gps_time);
    app_ui::set_gps_altitude(st.altitude);
    app_ui::set_gps_compass(st.course);

    char buf[12];
    if (isnan(st.env_temp_c)) snprintf(buf, sizeof buf, "--");
    else                      snprintf(buf, sizeof buf, "%.1f C", st.env_temp_c);
    app_ui::set_ambient_temperature(buf);
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

#ifdef SIM_SDL
/** Interactive loop: mouse = touch, ↑/↓ speed, ←/→ rpm, L backlight, O online, R reload. */
static int runWindow(const char* fixture, int zoom) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* win = SDL_CreateWindow("main_display sim", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       SCREEN_W * zoom, SCREEN_H * zoom, 0);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_RenderSetLogicalSize(ren, SCREEN_W, SCREEN_H);
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
                                         SCREEN_W, SCREEN_H);

    bool quit = false;
    while (!quit) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            Payload& p = s_state.payload;
            switch (ev.type) {
            case SDL_QUIT: quit = true; break;
            case SDL_MOUSEBUTTONDOWN: s_mouse_down = true;  break;
            case SDL_MOUSEBUTTONUP:   s_mouse_down = false; break;
            case SDL_MOUSEMOTION:     s_mouse_x = ev.motion.x; s_mouse_y = ev.motion.y; break;
            case SDL_KEYDOWN:
                switch (ev.key.keysym.sym) {
                case SDLK_ESCAPE: quit = true; break;
                case SDLK_UP:     if (p.speed_kmh_x128 <= 245 * 128) p.speed_kmh_x128 += 5 * 128; break;
                case SDLK_DOWN:   if (p.speed_kmh_x128 >= 5 * 128)   p.speed_kmh_x128 -= 5 * 128; break;
                case SDLK_RIGHT:  if (p.rpm <= 4900) p.rpm += 100; break;
                case SDLK_LEFT:   if (p.rpm >= 100)  p.rpm -= 100; break;
                case SDLK_l:      app_ui::cycle_backlight(); break;
                case SDLK_o:      s_state.online = !s_state.online; break;
                case SDLK_r:      if (fixture) loadFixture(fixture, s_state); break;
                }
                applyState(s_state);
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
    int  zoom   = 1;
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

    app_ui::create();
    applyState(s_state);

    // Step the 10-level cycle (boot 50%, +10, wraps 100 → 10) up to the fixture's level.
    for (int steps = ((s_state.brightness - 50) / 10 + 10) % 10; steps > 0; steps--) {
        app_ui::cycle_backlight();
    }

    if (window) {
#ifdef SIM_SDL
        return runWindow(fixture, zoom);
#else
        fprintf(stderr, "--window needs the sim_sdl env\n");
        return 2;
#endif
    }

    // Run LVGL for elapsed_ms (timers such as the backlight auto-hide), at least a few frames.
    const uint32_t frames = s_state.elapsed_ms / LV_DEF_REFR_PERIOD + 10;
    for (uint32_t i = 0; i < frames; i++) {
        s_tick_ms += LV_DEF_REFR_PERIOD;
        lv_timer_handler();
    }
    lv_refr_now(disp);
    return (out && writePpm(out)) ? 0 : (out ? 1 : 0);
}
