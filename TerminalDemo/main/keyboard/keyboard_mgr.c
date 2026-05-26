#include "keyboard_mgr.h"

#include <stdbool.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAB5_KEYBOARD_I2C_PORT      I2C_NUM_0
#define TAB5_KEYBOARD_I2C_SDA_PIN   GPIO_NUM_0
#define TAB5_KEYBOARD_I2C_SCL_PIN   GPIO_NUM_1
#define TAB5_KEYBOARD_I2C_CLK_HZ    400000
#define TAB5_KEYBOARD_POLL_MS       10
#define TAB5_KEYBOARD_READ_TIMEOUT  50

#define TAB5_KEYBOARD_ADDR          0x6D
#define TAB5_KEYBOARD_REG_INT_CFG   0x00
#define TAB5_KEYBOARD_REG_INT_STAT  0x01
#define TAB5_KEYBOARD_REG_EVENT_NUM 0x02
#define TAB5_KEYBOARD_REG_MODE      0x10
#define TAB5_KEYBOARD_REG_KEY_EVENT 0x20
#define TAB5_KEYBOARD_REG_FW        0xFE
#define TAB5_KEYBOARD_REG_I2C_ADDR  0xFF
#define TAB5_KEYBOARD_MODE_NORMAL   0x00
#define TAB5_KEYBOARD_EMPTY         0xFF
#define TAB5_KEYBOARD_ROWS          5
#define TAB5_KEYBOARD_COLS          14
#define TAB5_KEYBOARD_KEY_COUNT     (TAB5_KEYBOARD_ROWS * TAB5_KEYBOARD_COLS)

#define HID_MOD_CTRL_MASK           0x11
#define HID_MOD_SHIFT_MASK          0x22
#define HID_MOD_ALT_MASK            0x44

#define SCHAR_LEFT                  180
#define SCHAR_UP                    181
#define SCHAR_DOWN                  182
#define SCHAR_RIGHT                 183

static const char *TAG = "keyboard_mgr";

typedef enum {
    KEYBOARD_BUS_NONE = 0,
    KEYBOARD_BUS_I2C,
} keyboard_bus_t;

typedef struct {
    uint8_t keycode;
    uint8_t modifier;
} hid_mapping_t;

static keyboard_bus_t s_bus = KEYBOARD_BUS_NONE;
static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2c_master_dev_handle_t s_i2c_dev = NULL;
static keyboard_recv_cb_t s_recv_cb = NULL;
static keyboard_status_cb_t s_status_cb = NULL;
static TaskHandle_t s_task = NULL;
static bool s_initialized = false;
static bool s_active = false;
static bool s_mod_sym = false;
static bool s_mod_aa = false;
static bool s_mod_ctrl = false;
static bool s_mod_alt = false;

// A164 UnitTab5Keyboard Normal-mode matrix -> HID mapping from M5Unit-KEYBOARD develop.
static const hid_mapping_t s_hid_base[TAB5_KEYBOARD_KEY_COUNT] = {
    {0x29, 0x00}, {0x1E, 0x00}, {0x1F, 0x00}, {0x20, 0x00}, {0x21, 0x00}, {0x22, 0x00}, {0x23, 0x00}, {0x24, 0x00}, {0x25, 0x00}, {0x26, 0x00}, {0x27, 0x00}, {0x2D, 0x00}, {0x2E, 0x02}, {0x4C, 0x00},
    {0x35, 0x00}, {0x1E, 0x02}, {0x1F, 0x02}, {0x20, 0x02}, {0x21, 0x02}, {0x22, 0x02}, {0x23, 0x02}, {0x24, 0x02}, {0x25, 0x02}, {0x26, 0x02}, {0x27, 0x02}, {0x2F, 0x00}, {0x30, 0x00}, {0x31, 0x00},
    {0x2B, 0x00}, {0x14, 0x00}, {0x1A, 0x00}, {0x08, 0x00}, {0x15, 0x00}, {0x17, 0x00}, {0x1C, 0x00}, {0x18, 0x00}, {0x0C, 0x00}, {0x12, 0x00}, {0x13, 0x00}, {0x33, 0x00}, {0x34, 0x00}, {0x2A, 0x00},
    {0x00, 0x00}, {0x00, 0x00}, {0x04, 0x00}, {0x16, 0x00}, {0x07, 0x00}, {0x09, 0x00}, {0x0A, 0x00}, {0x0B, 0x00}, {0x0D, 0x00}, {0x0E, 0x00}, {0x0F, 0x00}, {0x52, 0x00}, {0x2D, 0x02}, {0x28, 0x00},
    {0x00, 0x00}, {0x00, 0x00}, {0x1D, 0x00}, {0x1B, 0x00}, {0x06, 0x00}, {0x19, 0x00}, {0x05, 0x00}, {0x11, 0x00}, {0x10, 0x00}, {0x37, 0x00}, {0x50, 0x00}, {0x51, 0x00}, {0x4F, 0x00}, {0x2C, 0x00},
};

static const hid_mapping_t s_hid_sym[TAB5_KEYBOARD_KEY_COUNT] = {
    {0x29, 0x00}, {0x1E, 0x00}, {0x1F, 0x00}, {0x20, 0x00}, {0x21, 0x00}, {0x22, 0x00}, {0x23, 0x00}, {0x24, 0x00}, {0x25, 0x00}, {0x26, 0x00}, {0x27, 0x00}, {0x2D, 0x00}, {0x2E, 0x02}, {0x4C, 0x00},
    {0x35, 0x02}, {0x38, 0x02}, {0x1F, 0x02}, {0x20, 0x02}, {0x21, 0x02}, {0x22, 0x02}, {0x23, 0x02}, {0x24, 0x02}, {0x38, 0x00}, {0x36, 0x02}, {0x37, 0x02}, {0x2F, 0x02}, {0x30, 0x02}, {0x31, 0x02},
    {0x2B, 0x00}, {0x14, 0x00}, {0x1A, 0x00}, {0x08, 0x00}, {0x15, 0x00}, {0x17, 0x00}, {0x1C, 0x00}, {0x18, 0x00}, {0x0C, 0x00}, {0x12, 0x00}, {0x13, 0x00}, {0x33, 0x02}, {0x34, 0x02}, {0x2A, 0x00},
    {0x00, 0x00}, {0x00, 0x00}, {0x04, 0x00}, {0x16, 0x00}, {0x07, 0x00}, {0x09, 0x00}, {0x0A, 0x00}, {0x0B, 0x00}, {0x0D, 0x00}, {0x0E, 0x00}, {0x0F, 0x00}, {0x52, 0x00}, {0x2E, 0x00}, {0x28, 0x00},
    {0x00, 0x00}, {0x00, 0x00}, {0x1D, 0x00}, {0x1B, 0x00}, {0x06, 0x00}, {0x19, 0x00}, {0x05, 0x00}, {0x11, 0x00}, {0x10, 0x00}, {0x36, 0x00}, {0x50, 0x00}, {0x51, 0x00}, {0x4F, 0x00}, {0x2C, 0x00},
};

static void notify_status(bool connected)
{
    if (s_status_cb) {
        s_status_cb(connected);
    }
}

static esp_err_t read_register(uint8_t reg, uint8_t *value)
{
    if (!s_i2c_dev || !value) {
        return ESP_ERR_INVALID_ARG;
    }
    return i2c_master_transmit_receive(s_i2c_dev, &reg, 1, value, 1,
                                       TAB5_KEYBOARD_READ_TIMEOUT);
}

static esp_err_t write_register(uint8_t reg, uint8_t value)
{
    if (!s_i2c_dev) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t buf[2] = {reg, value};
    return i2c_master_transmit(s_i2c_dev, buf, sizeof(buf),
                               TAB5_KEYBOARD_READ_TIMEOUT);
}

static char hid_usage_to_char(uint8_t keycode, uint8_t modifier)
{
    const bool shift = (modifier & HID_MOD_SHIFT_MASK) != 0;

    if (keycode >= 0x04 && keycode <= 0x1D) {
        char ch = (char)('a' + keycode - 0x04);
        return shift ? (char)(ch - ('a' - 'A')) : ch;
    }
    if (keycode >= 0x1E && keycode <= 0x27) {
        static const char unshifted[] = "1234567890";
        static const char shifted[] = "!@#$%^&*()";
        return shift ? shifted[keycode - 0x1E] : unshifted[keycode - 0x1E];
    }

    switch (keycode) {
    case 0x28: return '\n';
    case 0x29: return 0x1B;
    case 0x2A: return '\b';
    case 0x2B: return '\t';
    case 0x2C: return ' ';
    case 0x2D: return shift ? '_' : '-';
    case 0x2E: return shift ? '+' : '=';
    case 0x2F: return shift ? '{' : '[';
    case 0x30: return shift ? '}' : ']';
    case 0x31: return shift ? '|' : '\\';
    case 0x33: return shift ? ':' : ';';
    case 0x34: return shift ? '"' : '\'';
    case 0x35: return shift ? '~' : '`';
    case 0x36: return shift ? '<' : ',';
    case 0x37: return shift ? '>' : '.';
    case 0x38: return shift ? '?' : '/';
    default: return 0;
    }
}

static void dispatch_key(uint8_t ch)
{
    if (!s_recv_cb) {
        return;
    }

    switch (ch) {
    case SCHAR_UP: {
        const uint8_t seq[] = {0x1B, '[', 'A'};
        s_recv_cb(seq, sizeof(seq));
        break;
    }
    case SCHAR_DOWN: {
        const uint8_t seq[] = {0x1B, '[', 'B'};
        s_recv_cb(seq, sizeof(seq));
        break;
    }
    case SCHAR_RIGHT: {
        const uint8_t seq[] = {0x1B, '[', 'C'};
        s_recv_cb(seq, sizeof(seq));
        break;
    }
    case SCHAR_LEFT: {
        const uint8_t seq[] = {0x1B, '[', 'D'};
        s_recv_cb(seq, sizeof(seq));
        break;
    }
    default:
        s_recv_cb(&ch, 1);
        break;
    }
}

static bool update_modifier(uint8_t row, uint8_t col, bool pressed)
{
    if (row == 3 && col == 0) {
        s_mod_sym = pressed;
        return true;
    }
    if (row == 3 && col == 1) {
        s_mod_aa = pressed;
        return true;
    }
    if (row == 4 && col == 0) {
        s_mod_ctrl = pressed;
        return true;
    }
    if (row == 4 && col == 1) {
        s_mod_alt = pressed;
        return true;
    }
    return false;
}

static void dispatch_hid_mapping(hid_mapping_t mapping)
{
    uint8_t modifier = mapping.modifier;
    if (s_mod_aa) {
        modifier |= 0x02;
    }
    if (s_mod_ctrl) {
        modifier |= 0x01;
    }
    if (s_mod_alt) {
        modifier |= 0x04;
    }

    switch (mapping.keycode) {
    case 0x4F:
        dispatch_key(SCHAR_RIGHT);
        return;
    case 0x50:
        dispatch_key(SCHAR_LEFT);
        return;
    case 0x51:
        dispatch_key(SCHAR_DOWN);
        return;
    case 0x52:
        dispatch_key(SCHAR_UP);
        return;
    case 0x4C:
        dispatch_key(0x7F);
        return;
    default:
        break;
    }

    char ch = hid_usage_to_char(mapping.keycode, modifier);
    if (ch == 0) {
        return;
    }

    if ((modifier & HID_MOD_CTRL_MASK) != 0) {
        if (ch >= 'a' && ch <= 'z') {
            ch = (char)(ch - 'a' + 1);
        } else if (ch >= 'A' && ch <= 'Z') {
            ch = (char)(ch - 'A' + 1);
        }
    }

    if ((modifier & HID_MOD_ALT_MASK) != 0 && ch >= 0x20 && ch <= 0x7E && s_recv_cb) {
        const uint8_t seq[] = {0x1B, (uint8_t)ch};
        s_recv_cb(seq, sizeof(seq));
        return;
    }

    dispatch_key((uint8_t)ch);
}

static void handle_key_event(uint8_t raw)
{
    if (raw == TAB5_KEYBOARD_EMPTY) {
        return;
    }

    const bool pressed = (raw & 0x80) != 0;
    const uint8_t row = (raw >> 4) & 0x07;
    const uint8_t col = raw & 0x0F;

    if (row >= TAB5_KEYBOARD_ROWS || col >= TAB5_KEYBOARD_COLS) {
        ESP_LOGW(TAG, "invalid A164 key event raw:0x%02X row:%u col:%u", raw, row, col);
        return;
    }

    ESP_LOGI(TAG, "A164 key raw:0x%02X %s row:%u col:%u", raw,
             pressed ? "press" : "release", row, col);

    if (update_modifier(row, col, pressed)) {
        ESP_LOGI(TAG, "A164 modifiers Sym:%d Aa:%d Ctrl:%d Alt:%d",
                 s_mod_sym, s_mod_aa, s_mod_ctrl, s_mod_alt);
        return;
    }

    if (!pressed) {
        return;
    }

    const uint8_t index = (uint8_t)(row * TAB5_KEYBOARD_COLS + col);
    hid_mapping_t mapping = s_mod_sym ? s_hid_sym[index] : s_hid_base[index];
    if (mapping.keycode == 0) {
        return;
    }
    dispatch_hid_mapping(mapping);
}

static esp_err_t scan_i2c_bus(void)
{
    uint8_t found = 0;
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        esp_err_t ret = i2c_master_probe(s_i2c_bus, addr, TAB5_KEYBOARD_READ_TIMEOUT);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "I2C scan found addr 0x%02X", addr);
            found++;
        }
    }
    return found > 0 ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static void keyboard_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "A164 Tab5 Keyboard task started on ExtPort1 I2C addr 0x%02X",
             TAB5_KEYBOARD_ADDR);
    while (1) {
        uint8_t count = 0;
        esp_err_t ret = read_register(TAB5_KEYBOARD_REG_EVENT_NUM, &count);
        if (ret == ESP_OK) {
            if (!s_active) {
                s_active = true;
                notify_status(true);
            }

            if (count > 0) {
                ESP_LOGI(TAG, "A164 event count: %u", count);
            }

            for (uint8_t i = 0; i < count && i < 32; i++) {
                uint8_t raw = TAB5_KEYBOARD_EMPTY;
                ret = read_register(TAB5_KEYBOARD_REG_KEY_EVENT, &raw);
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "A164 key event read failed: %s", esp_err_to_name(ret));
                    break;
                }
                handle_key_event(raw);
            }
            if (count > 0) {
                (void)write_register(TAB5_KEYBOARD_REG_INT_STAT, 0x00);
            }
        } else if (ret != ESP_ERR_TIMEOUT && s_active) {
            ESP_LOGW(TAG, "A164 keyboard read failed: %s", esp_err_to_name(ret));
            s_active = false;
            notify_status(false);
        }
        vTaskDelay(pdMS_TO_TICKS(TAB5_KEYBOARD_POLL_MS));
    }
}

static esp_err_t try_i2c_init(void)
{
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = TAB5_KEYBOARD_I2C_PORT,
        .sda_io_num = TAB5_KEYBOARD_I2C_SDA_PIN,
        .scl_io_num = TAB5_KEYBOARD_I2C_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to create A164 ExtPort1 I2C bus: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "probing A164 UnitTab5Keyboard on ExtPort1 I2C SDA:%d SCL:%d addr:0x%02X",
             TAB5_KEYBOARD_I2C_SDA_PIN, TAB5_KEYBOARD_I2C_SCL_PIN, TAB5_KEYBOARD_ADDR);

    ret = i2c_master_probe(s_i2c_bus, TAB5_KEYBOARD_ADDR, TAB5_KEYBOARD_READ_TIMEOUT);
    ESP_LOGI(TAG, "probe A164 addr 0x%02X: %s", TAB5_KEYBOARD_ADDR, esp_err_to_name(ret));
    if (ret != ESP_OK) {
        (void)scan_i2c_bus();
        i2c_del_master_bus(s_i2c_bus);
        s_i2c_bus = NULL;
        return ret;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TAB5_KEYBOARD_ADDR,
        .scl_speed_hz = TAB5_KEYBOARD_I2C_CLK_HZ,
    };
    ret = i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_i2c_dev);
    if (ret != ESP_OK) {
        i2c_del_master_bus(s_i2c_bus);
        s_i2c_bus = NULL;
        return ret;
    }

    uint8_t fw = 0;
    uint8_t stored_addr = 0;
    ret = read_register(TAB5_KEYBOARD_REG_FW, &fw);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to read A164 firmware reg 0xFE: %s", esp_err_to_name(ret));
        return ret;
    }
    (void)read_register(TAB5_KEYBOARD_REG_I2C_ADDR, &stored_addr);
    ESP_LOGI(TAG, "A164 UnitTab5Keyboard firmware:0x%02X stored_addr:0x%02X", fw, stored_addr);

    ret = write_register(TAB5_KEYBOARD_REG_MODE, TAB5_KEYBOARD_MODE_NORMAL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to set A164 normal mode: %s", esp_err_to_name(ret));
        return ret;
    }
    (void)write_register(TAB5_KEYBOARD_REG_INT_CFG, 0x01);
    (void)write_register(TAB5_KEYBOARD_REG_INT_STAT, 0x00);
    (void)write_register(TAB5_KEYBOARD_REG_EVENT_NUM, 0x00);

    s_mod_sym = false;
    s_mod_aa = false;
    s_mod_ctrl = false;
    s_mod_alt = false;
    s_bus = KEYBOARD_BUS_I2C;
    return ESP_OK;
}

esp_err_t keyboard_mgr_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    esp_err_t ret = try_i2c_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "A164 UnitTab5Keyboard init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    BaseType_t task_ret = xTaskCreate(keyboard_task, "tab5_keyboard",
                                      4096, NULL, 6, &s_task);
    if (task_ret != pdPASS) {
        if (s_i2c_dev) {
            i2c_master_bus_rm_device(s_i2c_dev);
            s_i2c_dev = NULL;
        }
        if (s_i2c_bus) {
            i2c_del_master_bus(s_i2c_bus);
            s_i2c_bus = NULL;
        }
        s_bus = KEYBOARD_BUS_NONE;
        return ESP_ERR_NO_MEM;
    }

    s_initialized = true;
    s_active = true;
    notify_status(true);
    return ESP_OK;
}

esp_err_t keyboard_mgr_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }

    if (s_task) {
        vTaskDelete(s_task);
        s_task = NULL;
    }
    if (s_i2c_dev) {
        esp_err_t ret = i2c_master_bus_rm_device(s_i2c_dev);
        if (ret != ESP_OK) {
            return ret;
        }
        s_i2c_dev = NULL;
    }
    if (s_i2c_bus) {
        esp_err_t ret = i2c_del_master_bus(s_i2c_bus);
        if (ret != ESP_OK) {
            return ret;
        }
        s_i2c_bus = NULL;
    }

    s_initialized = false;
    s_active = false;
    s_bus = KEYBOARD_BUS_NONE;
    s_mod_sym = false;
    s_mod_aa = false;
    s_mod_ctrl = false;
    s_mod_alt = false;
    notify_status(false);
    return ESP_OK;
}

void keyboard_mgr_register_recv_cb(keyboard_recv_cb_t cb)
{
    s_recv_cb = cb;
}

void keyboard_mgr_register_status_cb(keyboard_status_cb_t cb)
{
    s_status_cb = cb;
}

bool keyboard_mgr_is_active(void)
{
    return s_active;
}
