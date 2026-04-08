#pragma once

#include "lvgl.h"
#include "wireless_mgr.h"   
#include <stdbool.h>
#include <stdio.h>

extern const lv_image_dsc_t choose_gif_dsc;
LV_IMG_DECLARE(logo_espnow)
LV_IMG_DECLARE(bluetooth_logo)
LV_IMG_DECLARE(arrow_orange)
LV_IMG_DECLARE(arrow_blue)
LV_IMG_DECLARE(finder)
LV_IMG_DECLARE(node)

LV_FONT_DECLARE(lv_font_inter_bold_48)
LV_FONT_DECLARE(lv_font_inter_light_14)
LV_FONT_DECLARE(lv_font_inter_bold_32)
LV_FONT_DECLARE(lv_font_inter_regular_16)
LV_FONT_DECLARE(lv_font_inter_bold_13)
LV_FONT_DECLARE(lv_font_inter_regular_14)
LV_FONT_DECLARE(lv_font_inter_bold_14)
LV_FONT_DECLARE(lv_font_inter_bold_10)
LV_FONT_DECLARE(lv_font_inter_bold_24)
LV_FONT_DECLARE(lv_font_inter_regular_12)
LV_FONT_DECLARE(lv_font_inter_medium_10)
LV_FONT_DECLARE(lv_font_inter_regular_24)
LV_FONT_DECLARE(lv_font_inter_regular_20)
LV_FONT_DECLARE(lv_font_inter_regular_32)

#define C_ORANGE        lv_color_hex(0xFFB786)
#define C_WHITE         lv_color_hex(0xE5E2E1)

#define C_BG            lv_color_hex(0x0E0E11)
#define C_SURFACE       lv_color_hex(0x16161F)
#define C_BORDER        lv_color_hex(0x2A2A3A)
#define T_PROMPT        lv_color_hex(0xE07B39)
#define T_CMD           lv_color_hex(0xF0C27F)
#define T_DEFAULT       lv_color_hex(0xA0A0B8)
#define T_SUCCESS       lv_color_hex(0x4ADE80)
#define T_ERROR         lv_color_hex(0xF87171)
#define T_INFO          lv_color_hex(0x60A5FA)
#define T_WARN          lv_color_hex(0xFBBF24)
#define T_DIM           lv_color_hex(0x4A4A6A)
#define T_HIGHLIGHT     lv_color_hex(0xE879F9)
#define C_GRAY          lv_color_hex(0x9B877B)
#define C_CARD_BG       lv_color_hex(0x1C1B1B)
#define C_ICON_ESPNOW   lv_color_hex(0x332B26)
#define C_ICON_BT       lv_color_hex(0x1F2732)
#define C_BLUE          lv_color_hex(0x3D90FF)
#define C_BLACK         lv_color_hex(0x000000)
#define C_PURE_WHITE    lv_color_hex(0xFFFFFF)
#define C_PROGRESS_BG   lv_color_hex(0xD9D9D9)
#define C_NODE_BG       lv_color_hex(0x2A2A2A)
#define C_WARM          lv_color_hex(0xDEC1AF)
#define C_BADGE_BT_BG   lv_color_hex(0x3D90FF)
#define C_DARK_ORANGE   lv_color_hex(0x572800)
#define C_DEEP_ORANGE   lv_color_hex(0xF57C00)
#define C_CONNECTED     lv_color_hex(0x2E7D32)

#define SCREEN_W        1280
#define SCREEN_H        720

#define CARD_W          500
#define CARD_H          325
#define CARD_RADIUS     24
#define CARD_PAD_H      28
#define CARD_PAD_V      28
#define CARD_GAP        56
#define CARD_MARGIN     ((SCREEN_W - CARD_W * 2 - CARD_GAP) / 2)   
#define CARD_Y          295
#define ICON_BG_SIZE    64
#define ICON_BG_RADIUS  16
#define CARD_LIFT_PX    24
#define CARD_ANIM_MS    280

#define P2_GLOW_X       93
#define P2_GLOW_Y       43
#define P2_GLOW_SIZE    69
#define P2_SCAN_X       185
#define P2_SCAN_Y       43
#define P2_PROG_X       185
#define P2_PROG_Y       99
#define P2_PROG_W       540
#define P2_PROG_CHUNK_W 98
#define P2_HEAD_X       105
#define P2_HEAD_Y       170
#define P2_PANEL_X      93
#define P2_PANEL_Y      218
#define P2_NODE_W       1070

typedef struct {
    wireless_node_t node;    
    lv_obj_t       *scr;    
} connect_btn_ctx_t;

extern int        g_selected;           
extern lv_obj_t  *g_cards[2];           
extern lv_obj_t  *g_p2_overlay;         
extern lv_obj_t  *g_p2_spinner;         
extern lv_obj_t  *g_p2_result;          
extern lv_obj_t  *g_connect_btn;        
extern bool       g_p2_is_bt;           

void example_lvgl_demo_ui(lv_obj_t *scr);  
void build_page2(bool is_bt);               
void connect_click_cb(lv_event_t *e);       

void build_page4(bool is_bt);               
void build_page5(bool is_bt);               
