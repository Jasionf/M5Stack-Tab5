#include "ui_common.h"
#include "keyboard_mgr.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

#include "Jet.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

using namespace Renderer;

namespace {

// Keep Jet at its README-friendly native window size. The 320x240 RGB565
// framebuffer is shown 1:1 in the center so LVGL does not scale or redraw 720p.
constexpr int RENDER_W = 320;
constexpr int RENDER_H = 240;
constexpr int DISPLAY_W = 1280;
constexpr int DISPLAY_H = 720;
constexpr int LANE_COUNT = 5;
constexpr int LANE_STEP = 112;
constexpr int TRACK_HALF_W = 390;
constexpr int TRACK_SEGMENTS = 14;
constexpr int TRACK_SEG_LEN = 420;
constexpr int OBSTACLE_COUNT = 6;
constexpr int SHIELD_MAX = 3;
constexpr uint16_t COLOR_SKY = 0x04BF;

struct TrackSegment {
    Object *obj = nullptr;
    int ordinal = 0;
};

struct Obstacle {
    Object *obj = nullptr;
    int lane = 0;
    int32_t z = 0;
    int32_t spin = 0;
};

const char *TAG = "gamepage";
uint16_t *s_renderbuffer = nullptr;
uint16_t *s_gradient = nullptr;
lv_image_dsc_t s_image_dsc = {};
lv_obj_t *s_image = nullptr;
lv_obj_t *s_score_lbl = nullptr;
lv_obj_t *s_status_lbl = nullptr;
lv_timer_t *s_timer = nullptr;

Scene *s_scene = nullptr;
Camera *s_camera = nullptr;
Material *s_road_a = nullptr;
Material *s_road_b = nullptr;
Material *s_wall_blue = nullptr;
Material *s_wall_cyan = nullptr;
Material *s_wall_white = nullptr;
Material *s_ceiling = nullptr;
Material *s_ship_cyan = nullptr;
Material *s_ship_dark = nullptr;
Material *s_ship_orange = nullptr;
Material *s_obstacle_mat = nullptr;
Material *s_gate_mat = nullptr;
TrackSegment s_track[TRACK_SEGMENTS];
Object *s_ship = nullptr;
Obstacle s_obstacles[OBSTACLE_COUNT];

volatile int s_move_request = 0;
volatile int s_lane_request = -1;
volatile bool s_boost = false;
volatile bool s_reset_request = false;
uint8_t s_esc_state = 0;
uint32_t s_rng = 0xC0DEFACE;
uint32_t s_score = 0;
uint32_t s_best = 0;
uint32_t s_frame = 0;
uint32_t s_fps = 0;
int64_t s_fps_t0 = 0;
uint32_t s_fps_frames = 0;
uint64_t s_render_us_acc = 0;
uint64_t s_frame_us_acc = 0;
uint32_t s_profile_frames = 0;
int s_lane = LANE_COUNT / 2;
int32_t s_ship_x = 0;
int s_shields = SHIELD_MAX;
bool s_game_over = false;

int32_t lane_to_x(int lane)
{
    return (lane - LANE_COUNT / 2) * LANE_STEP;
}

uint32_t next_rand()
{
    s_rng = s_rng * 1664525u + 1013904223u;
    return s_rng;
}

int next_lane()
{
    return (int)(next_rand() % LANE_COUNT);
}

int32_t track_center_for(int ordinal)
{
    static const int16_t centers[] = {
        0, 28, 66, 108, 146, 166, 150, 102,
        36, -32, -88, -138, -166, -146, -88, -24,
    };
    return centers[ordinal & 15];
}

uint16_t lerp565(uint16_t a, uint16_t b, int t, int max_t)
{
    int ar = (a >> 11) & 0x1F;
    int ag = (a >> 5) & 0x3F;
    int ab = a & 0x1F;
    int br = (b >> 11) & 0x1F;
    int bg = (b >> 5) & 0x3F;
    int bb = b & 0x1F;
    int r = ar + (br - ar) * t / max_t;
    int g = ag + (bg - ag) * t / max_t;
    int bl = ab + (bb - ab) * t / max_t;
    return (uint16_t)((r << 11) | (g << 5) | bl);
}

void add_quad(Object *obj, const Vector3 &a, const Vector3 &b, const Vector3 &c, const Vector3 &d, Material *mat)
{
    uint16_t base = static_cast<uint16_t>(obj->vertices.size());
    obj->addVertex({a, {0, 0}, {0, 0, 0}});
    obj->addVertex({b, {0, 0}, {0, 0, 0}});
    obj->addVertex({c, {0, 0}, {0, 0, 0}});
    obj->addVertex({d, {0, 0}, {0, 0, 0}});
    obj->addFace(base, base + 1, base + 2, base + 3, mat);
}

Object *create_track_segment(int ordinal)
{
    Object *obj = new (std::nothrow) Object();
    if (!obj) return nullptr;

    const int32_t z0 = 0;
    const int32_t z1 = TRACK_SEG_LEN;
    const int32_t y_floor = -138;
    const int32_t y_rail0 = -72;
    const int32_t y_rail1 = -12;
    const int32_t y_wall_top = 126;
    const int32_t y_roof = 172;
    Material *road = (ordinal & 1) ? s_road_a : s_road_b;
    Material *stripe = (ordinal & 1) ? s_wall_white : s_wall_blue;

    add_quad(obj, {-TRACK_HALF_W, y_floor, z0}, {TRACK_HALF_W, y_floor, z0},
             {TRACK_HALF_W, y_floor, z1}, {-TRACK_HALF_W, y_floor, z1}, road);
    add_quad(obj, {-TRACK_HALF_W, y_floor, z0}, {-TRACK_HALF_W, y_wall_top, z0},
             {-TRACK_HALF_W, y_wall_top, z1}, {-TRACK_HALF_W, y_floor, z1}, s_wall_cyan);
    add_quad(obj, {TRACK_HALF_W, y_wall_top, z0}, {TRACK_HALF_W, y_floor, z0},
             {TRACK_HALF_W, y_floor, z1}, {TRACK_HALF_W, y_wall_top, z1}, s_wall_cyan);
    add_quad(obj, {-TRACK_HALF_W, y_rail0, z0}, {-TRACK_HALF_W, y_rail1, z0},
             {-TRACK_HALF_W, y_rail1, z1}, {-TRACK_HALF_W, y_rail0, z1}, stripe);
    add_quad(obj, {TRACK_HALF_W, y_rail1, z0}, {TRACK_HALF_W, y_rail0, z0},
             {TRACK_HALF_W, y_rail0, z1}, {TRACK_HALF_W, y_rail1, z1}, stripe);

    if ((ordinal & 3) != 1) {
        add_quad(obj, {-TRACK_HALF_W, y_wall_top, z0}, {TRACK_HALF_W, y_wall_top, z0},
                 {TRACK_HALF_W - 70, y_roof, z1}, {-TRACK_HALF_W + 70, y_roof, z1}, s_ceiling);
    }

    obj->cullingMode = CullingMode::NO_CULLING;
    obj->calculateBoundingBox();
    return obj;
}

Object *create_ship()
{
    Object *ship = new (std::nothrow) Object();
    if (!ship) return nullptr;

    ship->addVertex({{0, 22, 118}, {0, 0}, {0, 0, 0}});      // nose
    ship->addVertex({{-54, -8, -74}, {0, 0}, {0, 0, 0}});    // rear left
    ship->addVertex({{54, -8, -74}, {0, 0}, {0, 0, 0}});     // rear right
    ship->addVertex({{0, 44, -28}, {0, 0}, {0, 0, 0}});      // canopy
    ship->addVertex({{0, -30, -8}, {0, 0}, {0, 0, 0}});      // belly
    ship->addVertex({{-132, -16, -28}, {0, 0}, {0, 0, 0}});  // left wing
    ship->addVertex({{132, -16, -28}, {0, 0}, {0, 0, 0}});   // right wing
    ship->addVertex({{-42, -4, -120}, {0, 0}, {0, 0, 0}});   // left engine
    ship->addVertex({{42, -4, -120}, {0, 0}, {0, 0, 0}});    // right engine

    ship->addTriangle(0, 1, 3, s_ship_cyan);
    ship->addTriangle(0, 3, 2, s_ship_cyan);
    ship->addTriangle(0, 4, 1, s_ship_dark);
    ship->addTriangle(0, 2, 4, s_ship_dark);
    ship->addTriangle(1, 5, 7, s_ship_orange);
    ship->addTriangle(2, 8, 6, s_ship_orange);
    ship->addTriangle(1, 4, 7, s_ship_dark);
    ship->addTriangle(2, 8, 4, s_ship_dark);

    ship->cullingMode = CullingMode::NO_CULLING;
    ship->calculateBoundingBox();
    return ship;
}

void update_image_descriptor()
{
    s_image_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    s_image_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_image_dsc.header.w = RENDER_W;
    s_image_dsc.header.h = RENDER_H;
    s_image_dsc.data_size = RENDER_W * RENDER_H * sizeof(uint16_t);
    s_image_dsc.data = reinterpret_cast<const uint8_t *>(s_renderbuffer);
}

void update_hud()
{
    if ((s_frame & 7) != 0 && !s_game_over) return;

    char line[128];
    std::snprintf(line, sizeof(line), "SCORE %lu   BEST %lu   SHIELD %d   FPS %lu",
                  (unsigned long)s_score, (unsigned long)s_best, s_shields, (unsigned long)s_fps);
    if (s_score_lbl) lv_label_set_text(s_score_lbl, line);

    if (s_status_lbl) {
        if (s_game_over) {
            lv_label_set_text(s_status_lbl, "CRASHED - SPACE / ENTER / R TO RESTART");
            lv_obj_set_style_text_color(s_status_lbl, T_ERROR, 0);
        } else if (s_boost) {
            lv_label_set_text(s_status_lbl, "JET BOOST");
            lv_obj_set_style_text_color(s_status_lbl, T_WARN, 0);
        } else {
            lv_label_set_text(s_status_lbl, "JET NATIVE 320x240 - A164 READY");
            lv_obj_set_style_text_color(s_status_lbl, T_SUCCESS, 0);
        }
    }
}

void update_track_segments()
{
    const int32_t phase = (int32_t)(s_score % TRACK_SEG_LEN);
    const int base_ordinal = (int)(s_score / TRACK_SEG_LEN);
    for (int i = 0; i < TRACK_SEGMENTS; ++i) {
        int ordinal = base_ordinal + i;
        s_track[i].ordinal = ordinal;
        if (!s_track[i].obj) continue;
        int32_t z = 180 + i * TRACK_SEG_LEN - phase;
        int32_t x = track_center_for(ordinal);
        s_track[i].obj->setPosition(x, 0, z);
    }
}

void place_obstacle(int i, int32_t base_z)
{
    s_obstacles[i].lane = next_lane();
    s_obstacles[i].z = base_z + (int32_t)(next_rand() % 360);
    s_obstacles[i].spin = (int32_t)(next_rand() % 360);
    if (s_obstacles[i].obj) {
        int ordinal = (int)(s_obstacles[i].z / TRACK_SEG_LEN) + (int)(s_score / TRACK_SEG_LEN);
        int32_t center = track_center_for(ordinal);
        s_obstacles[i].obj->setPosition(center + lane_to_x(s_obstacles[i].lane), -92, s_obstacles[i].z);
        s_obstacles[i].obj->setRotation(0, s_obstacles[i].spin, 0);
        s_obstacles[i].obj->enabled = true;
    }
}

void reset_game()
{
    s_score = 0;
    s_frame = 0;
    s_lane = LANE_COUNT / 2;
    s_ship_x = lane_to_x(s_lane);
    s_shields = SHIELD_MAX;
    s_game_over = false;
    s_move_request = 0;
    s_lane_request = -1;
    s_boost = false;
    s_reset_request = false;
    s_rng ^= 0x9E3779B9u;
    s_fps_t0 = esp_timer_get_time();
    s_fps_frames = 0;
    s_render_us_acc = 0;
    s_frame_us_acc = 0;
    s_profile_frames = 0;

    update_track_segments();
    if (s_ship) {
        s_ship->setPosition(s_ship_x, -86, 280);
        s_ship->setRotation(-10, 0, 0);
        s_ship->enabled = true;
    }
    for (int i = 0; i < OBSTACLE_COUNT; ++i) {
        place_obstacle(i, 1050 + i * 650);
    }
    update_hud();
}

void stop_gamepage()
{
    if (s_timer) {
        lv_timer_del(s_timer);
        s_timer = nullptr;
    }
}

void terminal_btn_cb(lv_event_t *)
{
    stop_gamepage();
    build_page5(false);
}

void restart_btn_cb(lv_event_t *)
{
    reset_game();
}

void handle_control(uint8_t ch)
{
    if (s_esc_state == 1) {
        s_esc_state = (ch == '[') ? 2 : 0;
        return;
    }
    if (s_esc_state == 2) {
        if (ch == 'D') s_move_request = s_move_request - 1;
        if (ch == 'C') s_move_request = s_move_request + 1;
        if (ch == 'A') s_boost = true;
        if (ch == 'B') s_boost = false;
        s_esc_state = 0;
        return;
    }
    if (ch == 0x1B) {
        s_esc_state = 1;
        return;
    }

    switch (ch) {
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
        s_lane_request = ch - '1';
        break;
    case 'a':
    case 'A':
    case 'h':
    case 'H':
        s_move_request = s_move_request - 1;
        break;
    case 'd':
    case 'D':
    case 'l':
    case 'L':
        s_move_request = s_move_request + 1;
        break;
    case 'w':
    case 'W':
        s_boost = true;
        break;
    case 's':
    case 'S':
        s_boost = false;
        break;
    case ' ':
    case '\r':
    case '\n':
    case 'r':
    case 'R':
        s_reset_request = true;
        break;
    default:
        break;
    }
}

void game_keyboard_cb(const uint8_t *data, size_t len)
{
    if (!data) return;
    for (size_t i = 0; i < len; ++i) {
        handle_control(data[i]);
    }
}

bool init_buffers()
{
    if (!s_renderbuffer) {
        s_renderbuffer = static_cast<uint16_t *>(heap_caps_aligned_alloc(64, RENDER_W * RENDER_H * sizeof(uint16_t),
                                                                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        if (!s_renderbuffer) {
            ESP_LOGW(TAG, "internal RGB565 buffer failed, falling back to PSRAM");
            s_renderbuffer = static_cast<uint16_t *>(heap_caps_aligned_alloc(64, RENDER_W * RENDER_H * sizeof(uint16_t),
                                                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        }
    }
    if (!s_gradient) {
        s_gradient = static_cast<uint16_t *>(heap_caps_malloc(RENDER_H * sizeof(uint16_t), MALLOC_CAP_8BIT));
    }
    if (!s_renderbuffer || !s_gradient) {
        ESP_LOGE(TAG, "failed to allocate Jet framebuffers");
        return false;
    }

    for (int y = 0; y < RENDER_H; ++y) {
        s_gradient[y] = lerp565(0x067F, 0x0128, y, RENDER_H - 1);
    }
    std::memset(s_renderbuffer, 0, RENDER_W * RENDER_H * sizeof(uint16_t));
    update_image_descriptor();
    return true;
}

bool init_jet_scene()
{
    if (!init_buffers()) return false;

    if (!s_scene) {
        s_camera = new (std::nothrow) Camera();
        s_scene = new (std::nothrow) Scene(s_renderbuffer, nullptr, RENDER_W, RENDER_H);
        s_road_a = new (std::nothrow) Material(0x18C7);
        s_road_b = new (std::nothrow) Material(0x214A);
        s_wall_blue = new (std::nothrow) Material(0x023F);
        s_wall_cyan = new (std::nothrow) Material(0x45BF);
        s_wall_white = new (std::nothrow) Material(0xDFFF);
        s_ceiling = new (std::nothrow) Material(0x73AE);
        s_ship_cyan = new (std::nothrow) Material(0x07FF);
        s_ship_dark = new (std::nothrow) Material(0x096A);
        s_ship_orange = new (std::nothrow) Material(0xFD20);
        s_obstacle_mat = new (std::nothrow) Material(0xF9E4);
        s_gate_mat = new (std::nothrow) Material(0xF81F);
        if (!s_camera || !s_scene || !s_road_a || !s_road_b || !s_wall_blue || !s_wall_cyan ||
            !s_wall_white || !s_ceiling || !s_ship_cyan || !s_ship_dark || !s_ship_orange ||
            !s_obstacle_mat || !s_gate_mat) {
            ESP_LOGE(TAG, "failed to allocate Jet scene objects");
            return false;
        }

        s_scene->setBackcolor(COLOR_SKY);
        s_scene->backgroundGradientColors = s_gradient;
        s_scene->setClearBuffer(true);
        s_scene->getRenderer()->interlacedMode = false;
        s_camera->setPosition(0, -24, -360);
        s_camera->setRotation(0, 0, 0);
        s_camera->nearPlane = 24;
        s_camera->farPlane = 7200;
        s_camera->setFOV(78, RENDER_W);
        s_scene->setCamera(s_camera);

        for (int i = 0; i < TRACK_SEGMENTS; ++i) {
            s_track[i].obj = create_track_segment(i);
            if (!s_track[i].obj) return false;
            s_scene->addObject(s_track[i].obj);
        }

        s_ship = create_ship();
        if (!s_ship) return false;
        s_scene->addObject(s_ship);

        for (int i = 0; i < OBSTACLE_COUNT; ++i) {
            Material *mat = (i & 1) ? s_obstacle_mat : s_gate_mat;
            s_obstacles[i].obj = Primitives::createCube(82, 82, 82, mat);
            if (!s_obstacles[i].obj) return false;
            s_obstacles[i].obj->cullingMode = CullingMode::NO_CULLING;
            s_scene->addObject(s_obstacles[i].obj);
        }
    } else {
        s_scene->setFramebuffer(s_renderbuffer);
        s_scene->backgroundGradientColors = s_gradient;
        s_scene->getRenderer()->interlacedMode = false;
    }

    reset_game();
    return true;
}

void animate_game()
{
    if (!s_scene) return;
    int64_t frame_t0 = esp_timer_get_time();

    if (s_reset_request) {
        reset_game();
    }

    int lane_req = s_lane_request;
    if (lane_req >= 0) {
        s_lane_request = -1;
        s_lane = std::max(0, std::min(LANE_COUNT - 1, lane_req));
    } else {
        int req = s_move_request;
        if (req != 0) {
            s_move_request = 0;
            s_lane = std::max(0, std::min(LANE_COUNT - 1, s_lane + (req > 0 ? 1 : -1)));
        }
    }

    if (!s_game_over) {
        const int32_t speed = s_boost ? 88 : 54;
        s_score += speed / 3;
        s_frame++;
        update_track_segments();

        int front_ordinal = (int)(s_score / TRACK_SEG_LEN);
        int32_t track_center = track_center_for(front_ordinal);
        int32_t target_x = track_center + lane_to_x(s_lane);
        s_ship_x += (target_x - s_ship_x) / 3;
        if (s_ship) {
            const int32_t roll = (target_x - s_ship_x) / 8;
            s_ship->setPosition(s_ship_x, -86, 280);
            s_ship->setRotation(-10, 0, std::max<int32_t>(-22, std::min<int32_t>(22, -roll)));
        }

        for (int i = 0; i < OBSTACLE_COUNT; ++i) {
            Obstacle &ob = s_obstacles[i];
            ob.z -= speed;
            ob.spin += 5 + i;
            if (ob.z < 65) {
                place_obstacle(i, 3450 + i * 190);
            } else if (ob.obj) {
                int ordinal = (int)((ob.z + s_score) / TRACK_SEG_LEN);
                int32_t center = track_center_for(ordinal);
                ob.obj->setPosition(center + lane_to_x(ob.lane), -92, ob.z);
                ob.obj->setRotation(ob.spin / 3, ob.spin, ob.spin / 5);
            }

            if (ob.z > 210 && ob.z < 345 && ob.lane == s_lane) {
                place_obstacle(i, 3650 + i * 180);
                s_shields--;
                if (s_shields <= 0) {
                    s_game_over = true;
                    s_best = std::max(s_best, s_score);
                    if (s_ship) s_ship->setRotation(70, 0, 36);
                    break;
                }
            }
        }
    } else if (s_ship) {
        s_ship->rotate(0, 8, 0);
    }

    int64_t render_t0 = esp_timer_get_time();
    s_scene->render();
    int64_t render_t1 = esp_timer_get_time();
    if (s_image) lv_obj_invalidate(s_image);
    int64_t frame_t1 = esp_timer_get_time();
    s_render_us_acc += (uint64_t)(render_t1 - render_t0);
    s_frame_us_acc += (uint64_t)(frame_t1 - frame_t0);
    s_profile_frames++;

    s_fps_frames++;
    int64_t now = esp_timer_get_time();
    if (now - s_fps_t0 >= 1000000) {
        s_fps = (uint32_t)(s_fps_frames * 1000000ULL / (uint64_t)(now - s_fps_t0));
        uint32_t avg_render_us = s_profile_frames ? (uint32_t)(s_render_us_acc / s_profile_frames) : 0;
        uint32_t avg_cpu_us = s_profile_frames ? (uint32_t)(s_frame_us_acc / s_profile_frames) : 0;
        ESP_LOGI(TAG, "fps=%lu render=%luus cpu=%luus objs=%d tris=%d rast=%d score=%lu", (unsigned long)s_fps,
                 (unsigned long)avg_render_us, (unsigned long)avg_cpu_us,
                 s_scene->lastFrameDrawnObjects, s_scene->lastFrameDrawnTriangles,
                 s_scene->lastFrameRasterizedTriangles, (unsigned long)s_score);
        s_fps_frames = 0;
        s_fps_t0 = now;
        s_render_us_acc = 0;
        s_frame_us_acc = 0;
        s_profile_frames = 0;
    }
    update_hud();
}

void game_timer_cb(lv_timer_t *)
{
    animate_game();
}

lv_obj_t *make_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, lv_color_t color)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_set_size(btn, 150, 42);
    lv_obj_set_style_radius(btn, 21, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_50, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x08121C), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, color, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &lv_font_inter_bold_13, 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_center(lbl);
    return btn;
}

} // namespace

extern "C" void build_gamepage(void)
{
    lv_display_set_rotation(lv_display_get_default(), LV_DISPLAY_ROTATION_90);
    stop_gamepage();

    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    bool ok = init_jet_scene();
    if (ok) {
        animate_game();
        lv_obj_t *stage = lv_obj_create(scr);
        lv_obj_set_size(stage, RENDER_W + 12, RENDER_H + 12);
        lv_obj_center(stage);
        lv_obj_set_style_bg_color(stage, lv_color_hex(0x020912), 0);
        lv_obj_set_style_bg_opa(stage, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(stage, 3, 0);
        lv_obj_set_style_border_color(stage, lv_color_hex(0x64D2FF), 0);
        lv_obj_set_style_radius(stage, 2, 0);
        lv_obj_set_style_pad_all(stage, 6, 0);
        lv_obj_clear_flag(stage, LV_OBJ_FLAG_SCROLLABLE);

        s_image = lv_image_create(stage);
        lv_image_set_src(s_image, &s_image_dsc);
        lv_obj_set_pos(s_image, 6, 6);
        lv_obj_set_size(s_image, RENDER_W, RENDER_H);
    }

    lv_obj_t *top = lv_obj_create(scr);
    lv_obj_set_size(top, DISPLAY_W, 74);
    lv_obj_set_pos(top, 0, 0);
    lv_obj_set_style_bg_color(top, lv_color_hex(0x03111D), 0);
    lv_obj_set_style_bg_opa(top, LV_OPA_50, 0);
    lv_obj_set_style_border_width(top, 0, 0);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(top);
    lv_label_set_text(title, "JET // TAB5 RACE");
    lv_obj_set_style_text_font(title, &lv_font_inter_bold_32, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x64D2FF), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 32, -10);

    s_score_lbl = lv_label_create(top);
    lv_label_set_text(s_score_lbl, "SCORE 0   BEST 0   SHIELD 3   FPS 0");
    lv_obj_set_style_text_font(s_score_lbl, &lv_font_inter_regular_20, 0);
    lv_obj_set_style_text_color(s_score_lbl, lv_color_hex(0xE8F8FF), 0);
    lv_obj_align(s_score_lbl, LV_ALIGN_RIGHT_MID, -32, -12);

    s_status_lbl = lv_label_create(top);
    lv_label_set_text(s_status_lbl, ok ? "JET NATIVE 320x240 - A164 READY" : "JET INIT FAILED");
    lv_obj_set_style_text_font(s_status_lbl, &lv_font_inter_bold_14, 0);
    lv_obj_set_style_text_letter_space(s_status_lbl, 2, 0);
    lv_obj_set_style_text_color(s_status_lbl, ok ? T_SUCCESS : T_ERROR, 0);
    lv_obj_align(s_status_lbl, LV_ALIGN_LEFT_MID, 34, 22);

    lv_obj_t *help = lv_label_create(scr);
    lv_label_set_text(help, "Native 320x240 Jet window   A/D or arrows: move   1-5: lane   W/up: boost   Space/Enter: restart");
    lv_obj_set_style_text_font(help, &lv_font_inter_regular_16, 0);
    lv_obj_set_style_text_color(help, lv_color_hex(0xB9D6E8), 0);
    lv_obj_align(help, LV_ALIGN_BOTTOM_MID, 0, -18);

    lv_obj_t *restart = make_button(scr, "RESTART", restart_btn_cb, T_WARN);
    lv_obj_align(restart, LV_ALIGN_BOTTOM_LEFT, 32, -14);
    lv_obj_t *terminal = make_button(scr, "TERMINAL", terminal_btn_cb, T_INFO);
    lv_obj_align(terminal, LV_ALIGN_BOTTOM_RIGHT, -32, -14);

    if (ok) {
        keyboard_mgr_register_recv_cb(game_keyboard_cb);
        esp_err_t ret = keyboard_mgr_init();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "keyboard init failed: %s", esp_err_to_name(ret));
            lv_label_set_text(s_status_lbl, "JET NATIVE 320x240 READY - KEYBOARD NOT DETECTED");
            lv_obj_set_style_text_color(s_status_lbl, T_ERROR, 0);
        }
        s_timer = lv_timer_create(game_timer_cb, 16, nullptr);
    } else {
        lv_obj_t *err = lv_label_create(scr);
        lv_label_set_text(err, "Jet init failed: not enough PSRAM");
        lv_obj_set_style_text_font(err, &lv_font_inter_bold_24, 0);
        lv_obj_set_style_text_color(err, T_ERROR, 0);
        lv_obj_center(err);
    }

    lv_screen_load(scr);
}
