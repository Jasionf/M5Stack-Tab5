#include "ui_nokia.h"
#include "ui_common.h"

#define W 720
#define H 1280
#define HEADER_H 68
#define NAV_H 120
#define NAV_Y (H - NAV_H)
#define COLS 4
#define GAP 8
#define UNIT ((W - GAP * (COLS - 1)) / COLS)
#define NAV_BTN_W (W / 3)

static int32_t lerp_i(int32_t a, int32_t b, int32_t n, int32_t d)
{
    return a + (b - a) * n / d;
}

static void jelly_scale_exec(void *var, int32_t p)
{
    lv_obj_t *obj = (lv_obj_t *)var;
    int32_t sx;
    int32_t sy;

    if (p < 28) {
        sx = lerp_i(256, 286, p, 28);
        sy = lerp_i(256, 240, p, 28);
    } else if (p < 58) {
        sx = lerp_i(286, 246, p - 28, 30);
        sy = lerp_i(240, 274, p - 28, 30);
    } else if (p < 80) {
        sx = lerp_i(246, 266, p - 58, 22);
        sy = lerp_i(274, 250, p - 58, 22);
    } else {
        sx = lerp_i(266, 256, p - 80, 20);
        sy = lerp_i(250, 256, p - 80, 20);
    }

    lv_obj_set_style_transform_scale_x(obj, sx, 0);
    lv_obj_set_style_transform_scale_y(obj, sy, 0);
}

static void jelly_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    lv_obj_t *obj = lv_event_get_user_data(e);
    if (obj == NULL) obj = lv_event_get_current_target(e);
    lv_anim_delete(obj, jelly_scale_exec);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, 0, 100);
    lv_anim_set_duration(&a, 360);
    lv_anim_set_exec_cb(&a, jelly_scale_exec);
    lv_anim_start(&a);
}

static void make_touchable(lv_obj_t *obj, int extra_pad)
{
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_ext_click_area(obj, extra_pad);
    lv_obj_set_style_transform_width(obj, 16, 0);
    lv_obj_set_style_transform_height(obj, 16, 0);
    lv_obj_set_style_transform_pivot_x(obj, lv_obj_get_width(obj) / 2, 0);
    lv_obj_set_style_transform_pivot_y(obj, lv_obj_get_height(obj) / 2, 0);
    lv_obj_add_event_cb(obj, jelly_event_cb, LV_EVENT_CLICKED, NULL);
}

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t color)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE);
    return o;
}

static lv_obj_t *blank(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE);
    return o;
}

static lv_obj_t *circle_obj(lv_obj_t *parent, int x, int y, int d, lv_color_t color)
{
    lv_obj_t *o = box(parent, x, y, d, d, color);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    return o;
}

static lv_obj_t *line_shape(lv_obj_t *parent, const lv_point_precise_t *pts,
                            uint32_t count, int x, int y, int width,
                            lv_color_t color)
{
    lv_obj_t *l = lv_line_create(parent);
    lv_line_set_points(l, pts, count);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_style_line_width(l, width, 0);
    lv_obj_set_style_line_color(l, color, 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE);
    return l;
}

static lv_obj_t *label(lv_obj_t *parent, const char *txt, int x, int y,
                       const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE);
    return l;
}

static lv_obj_t *tile(lv_obj_t *scr, int col, int row, int span,
                      lv_color_t color, const char *name)
{
    int size = span * UNIT + (span - 1) * GAP;
    int x = col * (UNIT + GAP);
    int y = HEADER_H + row * (UNIT + GAP);
    lv_obj_t *t = box(scr, x, y, size, size, color);
    make_touchable(t, 24);
    lv_obj_t *hi = box(t, 0, 0, size, 3, lv_color_white());
    lv_obj_set_style_bg_opa(hi, LV_OPA_10, 0);
    if (name) label(t, name, 18, size - 50, &lv_font_inter_bold_24, lv_color_white());
    return t;
}

static void make_small_word(lv_obj_t *t, const char *txt)
{
    lv_obj_t *l = label(t, txt, 0, 55, &lv_font_inter_bold_48, lv_color_white());
    lv_obj_center(l);
}

static void make_phone_icon(lv_obj_t *t)
{
    static const lv_point_precise_t pts[] = {
        {76, 16}, {42, 47}, {34, 82}, {54, 119}, {93, 145}, {126, 145}, {152, 119}
    };
    line_shape(t, pts, 7, 43, 72, 42, lv_color_white());
}

static void make_message_icon(lv_obj_t *t)
{
    box(t, 50, 42, 78, 48, lv_color_white());
    static const lv_point_precise_t tail[] = { {0, 0}, {0, 20}, {18, 0} };
    lv_obj_t *tri = lv_line_create(t);
    lv_line_set_points(tri, tail, 3);
    lv_obj_set_pos(tri, 50, 82);
    lv_obj_set_style_line_width(tri, 14, 0);
    lv_obj_set_style_line_color(tri, lv_color_white(), 0);
    lv_obj_set_style_line_rounded(tri, false, 0);
    circle_obj(t, 68, 55, 7, lv_color_hex(0xD62B28));
    circle_obj(t, 85, 55, 7, lv_color_hex(0xD62B28));
}

static void make_ie_icon(lv_obj_t *t)
{
    lv_obj_t *e = label(t, "e", 67, 18, &lv_font_inter_bold_48, lv_color_white());
    lv_obj_set_style_transform_scale(e, 460, 0);
    lv_obj_set_style_transform_width(e, 80, 0);
    lv_obj_set_style_transform_height(e, 80, 0);
    lv_obj_set_style_transform_pivot_x(e, 18, 0);
    lv_obj_set_style_transform_pivot_y(e, 30, 0);
    static const lv_point_precise_t orbit[] = { {0, 43}, {25, 22}, {62, 12}, {106, 36} };
    line_shape(t, orbit, 4, 34, 88, 8, lv_color_white());
}

static void make_mail_icon(lv_obj_t *t)
{
    lv_obj_t *env = blank(t, 31, 54, 62, 42);
    lv_obj_set_style_border_width(env, 7, 0);
    lv_obj_set_style_border_color(env, lv_color_white(), 0);
    static const lv_point_precise_t flap[] = { {0, 0}, {31, 24}, {62, 0} };
    line_shape(t, flap, 3, 31, 58, 6, lv_color_white());
    label(t, "2", 96, 47, &lv_font_inter_bold_48, lv_color_white());
}

static void make_store_icon(lv_obj_t *t)
{
    lv_obj_t *bag = box(t, 48, 68, 79, 59, lv_color_white());
    lv_obj_set_style_radius(bag, 0, 0);
    lv_obj_t *handle = blank(t, 65, 45, 45, 42);
    lv_obj_set_style_border_width(handle, 7, 0);
    lv_obj_set_style_border_color(handle, lv_color_white(), 0);
    lv_obj_set_style_radius(handle, 18, 0);
    box(t, 58, 73, 60, 20, lv_color_hex(0xD62B28));
}

static void make_people(lv_obj_t *t)
{
    int s = 2 * UNIT + GAP;
    int third = s / 3;
    box(t, 0, 0, third, s / 2, lv_color_hex(0xF2D7C8));
    box(t, third, 0, third, s / 2, lv_color_hex(0xBBC3BA));
    box(t, third * 2, 0, s - third * 2, s / 2, lv_color_hex(0xEDBD5A));
    box(t, 0, s / 2, third, s / 2, lv_color_hex(0xF0655E));
    box(t, third, s / 2, third, s / 2, lv_color_hex(0x78806D));
    box(t, third * 2, s / 2, s - third * 2, s / 2, lv_color_hex(0x866D60));

    circle_obj(t, 43, 42, 24, lv_color_hex(0x1F1F22));
    box(t, 38, 62, 34, 5, lv_color_hex(0xF0655E));
    lv_obj_t *body_a = box(t, 37, 79, 34, 13, lv_color_white());
    lv_obj_set_style_radius(body_a, 14, 0);
    circle_obj(t, 250, 43, 25, lv_color_hex(0x202020));
    circle_obj(t, 258, 52, 5, lv_color_hex(0xBBC3BA));
    lv_obj_t *body_b = box(t, 213, 91, 37, 13, lv_color_hex(0x202020));
    lv_obj_set_style_radius(body_b, 14, 0);
    circle_obj(t, 145, 164, 40, lv_color_hex(0x2B2422));
    box(t, 118, 179, 92, 6, lv_color_hex(0xD5D8D0));
    lv_obj_t *body_c = box(t, 137, 240, 58, 22, lv_color_hex(0xCFD1C9));
    lv_obj_set_style_radius(body_c, 22, 0);
    label(t, "People", 18, s - 48, &lv_font_inter_bold_24, lv_color_white());
}

static void make_xbox_icon(lv_obj_t *t)
{
    circle_obj(t, 145, 76, 66, lv_color_white());
    static const lv_point_precise_t x1[] = { {0, 0}, {28, 22}, {56, 0} };
    static const lv_point_precise_t x2[] = { {0, 46}, {28, 22}, {56, 46} };
    line_shape(t, x1, 3, 150, 88, 9, lv_color_hex(0x058C35));
    line_shape(t, x2, 3, 150, 88, 9, lv_color_hex(0x058C35));
    lv_obj_t *txt = label(t, "XBOX", 87, 135, &lv_font_inter_regular_32, lv_color_white());
    lv_obj_set_style_transform_scale(txt, 445, 0);
    lv_obj_set_style_transform_width(txt, 130, 0);
    lv_obj_set_style_transform_height(txt, 50, 0);
    lv_obj_set_style_transform_pivot_x(txt, 88, 0);
    lv_obj_set_style_transform_pivot_y(txt, 18, 0);
}

static void make_maps_icon(lv_obj_t *t)
{
    circle_obj(t, 83, 55, 174, lv_color_white());
    circle_obj(t, 98, 70, 144, lv_color_hex(0xEC302E));
    static const lv_point_precise_t north[] = { {60, 0}, {89, 86}, {60, 120}, {31, 86}, {60, 0} };
    line_shape(t, north, 5, 82, 86, 20, lv_color_white());
    circle_obj(t, 135, 132, 14, lv_color_hex(0xEC302E));
    label(t, "N", 162, 91, &lv_font_inter_bold_24, lv_color_white());
}

static void make_news_icon(lv_obj_t *t)
{
    box(t, 45, 48, 30, 30, lv_color_white());
    box(t, 84, 48, 30, 30, lv_color_white());
    box(t, 45, 88, 30, 30, lv_color_white());
    box(t, 84, 88, 30, 30, lv_color_white());
    label(t, "*", 50, 50, &lv_font_inter_bold_32, lv_color_hex(0xD62B28));
}

static void make_music_icon(lv_obj_t *t)
{
    circle_obj(t, 40, 95, 40, lv_color_white());
    circle_obj(t, 91, 78, 40, lv_color_white());
    box(t, 70, 42, 10, 63, lv_color_white());
    box(t, 121, 30, 10, 64, lv_color_white());
    static const lv_point_precise_t beam[] = { {0, 10}, {51, 0} };
    line_shape(t, beam, 2, 72, 29, 11, lv_color_white());
}

static lv_obj_t *pill(lv_obj_t *parent, int x, int y, int w, int h, int rot)
{
    lv_obj_t *o = box(parent, x, y, w, h, lv_color_white());
    lv_obj_set_style_radius(o, h / 2, 0);
    lv_obj_set_style_transform_width(o, 12, 0);
    lv_obj_set_style_transform_height(o, 12, 0);
    lv_obj_set_style_transform_pivot_x(o, h / 2, 0);
    lv_obj_set_style_transform_pivot_y(o, h / 2, 0);
    if (rot) lv_obj_set_style_transform_rotation(o, rot, 0);
    return o;
}

static void add_nav_hit(lv_obj_t *nav, int index, lv_obj_t *icon)
{
    lv_obj_t *b = blank(nav, index * NAV_BTN_W, 0, NAV_BTN_W, NAV_H);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, jelly_event_cb, LV_EVENT_CLICKED, icon);
}

static lv_obj_t *make_back_icon(lv_obj_t *nav)
{
    lv_obj_t *g = blank(nav, 132, 35, 74, 52);
    lv_obj_set_style_transform_pivot_x(g, 37, 0);
    lv_obj_set_style_transform_pivot_y(g, 26, 0);
    static const lv_point_precise_t back_pts[] = { {53, 3}, {19, 26}, {53, 49} };
    line_shape(g, back_pts, 3, 0, 0, 10, lv_color_white());
    return g;
}

static lv_obj_t *make_windows_icon(lv_obj_t *nav)
{
    lv_obj_t *g = blank(nav, 333, 39, 46, 46);
    const int s = 22;
    const int gap = 6;
    lv_color_t cream = lv_color_hex(0xF2EBC4);
    lv_obj_set_style_transform_pivot_x(g, 23, 0);
    lv_obj_set_style_transform_pivot_y(g, 23, 0);
    box(g, 0, 0, s, s, cream);
    box(g, s + gap, 0, s, s, cream);
    box(g, 0, s + gap, s, s, cream);
    box(g, s + gap, s + gap, s, s, cream);
    return g;
}

static lv_obj_t *make_search_icon(lv_obj_t *nav)
{
    lv_obj_t *g = blank(nav, 512, 31, 82, 82);
    lv_obj_set_style_transform_pivot_x(g, 41, 0);
    lv_obj_set_style_transform_pivot_y(g, 41, 0);
    lv_obj_t *ring = box(g, 5, 5, 45, 45, lv_color_white());
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ring, 7, 0);
    lv_obj_set_style_border_color(ring, lv_color_white(), 0);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    pill(g, 45, 43, 40, 8, 450);
    return g;
}

void build_nokia_page(lv_obj_t *scr)
{
    lv_display_set_rotation(lv_display_get_default(), LV_DISPLAY_ROTATION_0);
    lv_obj_clean(scr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x050505), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    const lv_color_t red = lv_color_hex(0xEC302E);
    const lv_color_t red2 = lv_color_hex(0xD62B28);
    const lv_color_t dark_red = lv_color_hex(0x8C1E1E);
    const lv_color_t deep_red = lv_color_hex(0x541414);
    const lv_color_t green = lv_color_hex(0x058C35);

    box(scr, 0, 0, W, HEADER_H, lv_color_hex(0x050505));
    label(scr, "NOKIA", 28, 19, &lv_font_inter_bold_24, lv_color_hex(0x8F9298));
    label(scr, "verizon", 525, 21, &lv_font_inter_regular_24, lv_color_hex(0x777A80));
    label(scr, "1:00", 642, 18, &lv_font_inter_bold_24, lv_color_white());
    box(scr, 0, HEADER_H - 3, W, 3, lv_color_hex(0x1A1A1A));

    lv_obj_t *phone = tile(scr, 0, 0, 2, red, "Verizon Wireless");
    make_phone_icon(phone);

    lv_obj_t *msg = tile(scr, 2, 0, 1, red2, NULL);
    make_message_icon(msg);
    lv_obj_t *ie = tile(scr, 3, 0, 1, red2, NULL);
    make_ie_icon(ie);
    lv_obj_t *mail = tile(scr, 2, 1, 1, red2, NULL);
    make_mail_icon(mail);
    lv_obj_t *store = tile(scr, 3, 1, 1, red2, NULL);
    make_store_icon(store);

    lv_obj_t *people = tile(scr, 0, 2, 2, lv_color_hex(0xB62A2A), NULL);
    make_people(people);

    lv_obj_t *xbox = tile(scr, 2, 2, 2, green, "Games");
    make_xbox_icon(xbox);

    lv_obj_t *maps = tile(scr, 0, 4, 2, red, "HERE Maps");
    make_maps_icon(maps);

    lv_obj_t *news = tile(scr, 2, 4, 1, red2, NULL);
    make_news_icon(news);
    lv_obj_t *cnn = tile(scr, 3, 4, 1, dark_red, NULL);
    make_small_word(cnn, "CNN");
    lv_obj_t *espn = tile(scr, 2, 5, 1, deep_red, NULL);
    make_small_word(espn, "ESPN");
    lv_obj_t *music = tile(scr, 3, 5, 1, red2, NULL);
    make_music_icon(music);

    lv_obj_t *nav = box(scr, 0, NAV_Y, W, NAV_H, lv_color_hex(0x050505));
    box(nav, 0, 0, W, 3, lv_color_hex(0x1A1A1A));

    lv_obj_t *back = make_back_icon(nav);
    lv_obj_t *home = make_windows_icon(nav);
    lv_obj_t *search = make_search_icon(nav);
    add_nav_hit(nav, 0, back);
    add_nav_hit(nav, 1, home);
    add_nav_hit(nav, 2, search);
}
