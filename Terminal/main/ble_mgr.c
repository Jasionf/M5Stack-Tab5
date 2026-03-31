/*
 * ble_mgr.c — BLE scan and connection manager (NimBLE host)
 *
 * On ESP32-P4 the BLE controller lives in the ESP32-C6 coprocessor.
 * NimBLE runs as host-only on the P4 and communicates with the C6
 * via HCI over the same transport used by esp_wifi_remote (UART/SPI).
 *
 * Compile-time guard: all BLE code is compiled only when
 * CONFIG_BT_NIMBLE_ENABLED=y.  When that flag is absent the public
 * functions return ESP_ERR_NOT_SUPPORTED so the rest of the code can
 * call them unconditionally.
 *
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: CC0-1.0
 */

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "wireless_mgr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

static const char *TAG = "ble_mgr";

/* ── Forward declarations from wireless_mgr.c ──────────────────── */
extern esp_err_t _wireless_add_node(const wireless_node_t *n);
extern wireless_recv_cb_t   g_wireless_recv_cb;
extern wireless_status_cb_t g_wireless_status_cb;

/* ═══════════════════════════════════════════════════════════════════
 *  Implementation — only when CONFIG_BT_NIMBLE_ENABLED=y
 * ═══════════════════════════════════════════════════════════════════ */
#if CONFIG_BT_NIMBLE_ENABLED

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/util/util.h"
/* ble_svc_gap/gatt are GATT-server services; not needed for BLE central/scan */

/* ── Module state ────────────────────────────────────────────────── */
static wireless_result_cb_t s_scan_cb      = NULL;
static bool                  s_scanning     = false;
static uint16_t              s_conn_handle  = BLE_HS_CONN_HANDLE_NONE;
static uint16_t              s_hid_svc_start = 0;   /* start handle of HID service */
/* ── Auto-reconnect state ─────────────────────────────────────── */
static uint8_t       s_peer_addr[6]    = {0};
static uint8_t       s_peer_addr_type  = 0;
static bool          s_auto_reconnect  = false;
static TimerHandle_t s_reconnect_tmr   = NULL;

static void reconnect_timer_cb(TimerHandle_t tmr)
{
    (void)tmr;
    if (!s_auto_reconnect) return;
    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE) return;
    ESP_LOGI(TAG, "BLE auto-reconnect → %02X:%02X:%02X:%02X:%02X:%02X",
             s_peer_addr[5], s_peer_addr[4], s_peer_addr[3],
             s_peer_addr[2], s_peer_addr[1], s_peer_addr[0]);
    ble_connect(s_peer_addr, s_peer_addr_type);
}
/* ── HID GATT subscription state ────────────────────────────────── */
static uint16_t s_hid_svc_end        = 0;   /* end handle of HID service       */
static uint16_t s_report_val         = 0;   /* value handle of Report chr      */
static uint16_t s_cccd_handle        = 0;   /* CCCD descriptor handle          */
static bool     s_notifications_ready = false; /* CCCD write confirmed          */

static const ble_uuid16_t s_uuid_hid   = BLE_UUID16_INIT(0x1812);
static const ble_uuid16_t s_uuid_rep   = BLE_UUID16_INIT(0x2A4D);
static const ble_uuid16_t s_uuid_cccd  = BLE_UUID16_INIT(0x2902);

/* CardKB2 public MAC in NimBLE little-endian byte order. */
static const uint8_t s_target_kb2_mac[6] = {0x56, 0x02, 0xC3, 0xA6, 0x56, 0x88};

/* ── USB HID keycode → ASCII tables ─────────────────────────────── */
/*  Index = HID keycode (0x04 = 'a' … 0x65 = F9)                    */
static const char s_hid_normal[0x66] = {
    /* 0x00 */ 0,0,0,0,
    /* 0x04 a-z */ 'a','b','c','d','e','f','g','h','i','j','k','l','m',
                   'n','o','p','q','r','s','t','u','v','w','x','y','z',
    /* 0x1E 1-0 */ '1','2','3','4','5','6','7','8','9','0',
    /* 0x28 */ '\r', 0x1B, '\b', '\t', ' ',
    /* 0x2D */ '-','=','[',']','\\', 0, ';','\'','`',',','.','/',
    /* 0x39 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    /* 0x49 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    /* 0x58 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0
};
static const char s_hid_shifted[0x66] = {
    /* 0x00 */ 0,0,0,0,
    /* 0x04 A-Z */ 'A','B','C','D','E','F','G','H','I','J','K','L','M',
                   'N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
    /* 0x1E !@#... */ '!','@','#','$','%','^','&','*','(',')',
    /* 0x28 */ '\r', 0x1B, '\b', '\t', ' ',
    /* 0x2D */ '_','+','{','}','|', 0, ':','"','~','<','>','?',
    /* 0x39 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    /* 0x49 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    /* 0x58 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static uint8_t hid_to_ascii(const uint8_t *rep, size_t len)
{
    /* KB2 uses Report ID=1, so GATT notify payload is:
     *   [Report ID=0x01][modifier][reserved][kc0][kc1][kc2][kc3][kc4][kc5]
     * = 9 bytes.  Older firmwares without Report ID send 8 bytes directly.
     * Detect by checking if rep[0] == 0x01 and len == 9.               */
    const uint8_t *r = rep;
    if (len == 9 && rep[0] == 0x01) {
        r = rep + 1;   /* skip Report ID byte */
        len = 8;
    }
    if (len < 3) return 0;
    bool shift = (r[0] & 0x22) != 0;   /* Left/Right Shift bits */
    uint8_t kc  = r[2];
    if (kc == 0 || kc >= sizeof(s_hid_normal)) return 0;
    return (uint8_t)(shift ? s_hid_shifted[kc] : s_hid_normal[kc]);
}

/* ── GATT discovery callbacks (forward decls) ────────────────────── */
static int gatt_chr_disc_cb(uint16_t conn_handle,
                            const struct ble_gatt_error *err,
                            const struct ble_gatt_chr *chr, void *arg);
static int gatt_dsc_disc_cb(uint16_t conn_handle,
                            const struct ble_gatt_error *err,
                            uint16_t chr_val_handle,
                            const struct ble_gatt_dsc *dsc, void *arg);

static int gatt_svc_disc_cb(uint16_t conn_handle,
                            const struct ble_gatt_error *err,
                            const struct ble_gatt_svc *svc, void *arg)
{
    if (err->status == BLE_HS_EDONE) return 0;
    if (err->status != 0 || svc == NULL) return 0;
    if (ble_uuid_cmp(&svc->uuid.u, &s_uuid_hid.u) == 0) {
        s_hid_svc_start = svc->start_handle;
        s_hid_svc_end   = svc->end_handle;
        ESP_LOGI(TAG, "HID service found (start=%u end=%u), discovering chars…",
                 s_hid_svc_start, s_hid_svc_end);
        ble_gattc_disc_all_chrs(conn_handle, s_hid_svc_start,
                                s_hid_svc_end, gatt_chr_disc_cb, NULL);
    }
    return 0;
}

static int gatt_write_cccd_cb(uint16_t conn_handle,
                              const struct ble_gatt_error *err,
                              struct ble_gatt_attr *attr, void *arg)
{
    (void)conn_handle; (void)attr; (void)arg;
    if (err->status == 0) {
        s_notifications_ready = true;
        ESP_LOGI(TAG, "HID notifications enabled for CardKB2 ✓");
        /* Notify UI that keyboard is fully ready */
        if (g_wireless_status_cb) g_wireless_status_cb(false);
    } else {
        /* ATT error 0x05 = Insufficient Authentication — pairing required.
         * ble_on_enc_change() will retry after encryption is established. */
        ESP_LOGW(TAG, "CCCD write failed (ATT err 0x%02x) — awaiting pairing/encryption",
                 err->status);
    }
    return 0;
}

static int gatt_dsc_disc_cb(uint16_t conn_handle,
                            const struct ble_gatt_error *err,
                            uint16_t chr_val_handle,
                            const struct ble_gatt_dsc *dsc, void *arg)
{
    (void)chr_val_handle; (void)arg;
    if (err->status == BLE_HS_EDONE) return 0;
    if (err->status != 0 || dsc == NULL) return 0;
    if (ble_uuid_cmp(&dsc->uuid.u, &s_uuid_cccd.u) == 0) {
        s_cccd_handle = dsc->handle;   /* save for retry after pairing */
        /* Enable both Notify (0x01) and Indicate (0x02) = 0x03 */
        uint8_t val[2] = {0x03, 0x00};  /* enable notify + indicate */
        ESP_LOGI(TAG, "Writing CCCD (handle=%u) with Notify+Indicate…", dsc->handle);
        ble_gattc_write_flat(conn_handle, dsc->handle, val, 2,
                             gatt_write_cccd_cb, NULL);
    }
    return 0;
}

static int gatt_chr_disc_cb(uint16_t conn_handle,
                            const struct ble_gatt_error *err,
                            const struct ble_gatt_chr *chr, void *arg)
{
    (void)arg;
    if (err->status == BLE_HS_EDONE) return 0;
    if (err->status != 0 || chr == NULL) return 0;
    /* Find every Report characteristic (0x2A4D) that supports Notify or Indicate.
     * KB2 has Report ID=1; subscribe to the first one found.          */
    if (ble_uuid_cmp(&chr->uuid.u, &s_uuid_rep.u) == 0 &&
        (chr->properties & (BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_INDICATE)) &&
        s_report_val == 0) {          /* first notifiable/indicatable Report chr  */
        s_report_val = chr->val_handle;
        ESP_LOGI(TAG, "Report chr found (val_handle=%u props=0x%02x), discovering CCCD…",
                 s_report_val, chr->properties);
        /* Descriptors live between chr->val_handle+1 and the next chr or end */
        ble_gattc_disc_all_dscs(conn_handle, chr->val_handle + 1,
                                s_hid_svc_end, gatt_dsc_disc_cb, NULL);
    } else if (chr != NULL) {
        ESP_LOGD(TAG, "  chr uuid=%04x val_handle=%u props=0x%02x (skipped)",
                 ble_uuid_u16(&chr->uuid.u), chr->val_handle, chr->properties);
    }
    return 0;
}

/* ── Human-readable BLE address string ─────────────────────────────*/
static void mac_to_str(const uint8_t addr[6], char *buf, size_t sz)
{
    /* NimBLE stores BD_ADDR little-endian; display big-endian */
    snprintf(buf, sz, "%02X:%02X:%02X:%02X:%02X:%02X",
             addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
}

/* ── Advertisement data parser ───────────────────────────────────── */
/*
 * Fills name (device name from AD) and detail (appearance or primary
 * service UUID).  Both strings are pre-zeroed by the caller.
 */
static void parse_adv(const uint8_t *data, uint8_t data_len,
                      char *name_out, size_t name_sz,
                      char *type_tag_out, size_t tag_sz,
                      char *detail_out, size_t detail_sz)
{
    struct ble_hs_adv_fields fields;
    if (ble_hs_adv_parse_fields(&fields, data, data_len) != 0) return;

    /* Device name (complete or shortened) */
    if (fields.name_len > 0 && fields.name) {
        size_t n = fields.name_len < (name_sz - 1) ? fields.name_len : (name_sz - 1);
        memcpy(name_out, fields.name, n);
        name_out[n] = '\0';
    }

    /* Appearance → type_tag and detail */
    if (fields.appearance_is_present) {
        uint16_t ap = fields.appearance;
        /*
         * Common HID appearances:
         *   0x03C1 Keyboard
         *   0x03C2 Mouse
         *   0x03C4 Gamepad
         */
        if (ap == 0x03C1) {
            snprintf(type_tag_out, tag_sz, "BLE HID");
            snprintf(detail_out, detail_sz, "Keyboard");
        } else if (ap == 0x03C2) {
            snprintf(type_tag_out, tag_sz, "BLE HID");
            snprintf(detail_out, detail_sz, "Mouse");
        } else if (ap == 0x03C4) {
            snprintf(type_tag_out, tag_sz, "BLE HID");
            snprintf(detail_out, detail_sz, "Gamepad");
        } else {
            snprintf(detail_out, detail_sz, "APR:0x%04X", (unsigned)ap);
        }
        return;
    }

    /* 16-bit service UUIDs */
    if (fields.num_uuids16 > 0 && fields.uuids16) {
        uint16_t uuid = fields.uuids16[0].value;
        if (uuid == 0x1812) {          /* HID over GATT */
            snprintf(type_tag_out, tag_sz, "BLE HID");
            snprintf(detail_out, detail_sz, "HID");
        } else if (uuid == 0x180F) {   /* Battery Service */
            snprintf(detail_out, detail_sz, "BAT+SVC");
        } else if (uuid == 0x180A) {   /* Device Information */
            snprintf(detail_out, detail_sz, "DIS");
        } else {
            snprintf(detail_out, detail_sz, "SVC:0x%04X", (unsigned)uuid);
        }
        return;
    }

    /* 128-bit service UUID */
    if (fields.num_uuids128 > 0 && fields.uuids128) {
        /* Show only the 16-bit range of the well-known base */
        uint16_t u16 = (fields.uuids128[0].value[13] << 8) |
                        fields.uuids128[0].value[12];
        snprintf(detail_out, detail_sz, "SVC:%04X…", (unsigned)u16);
    }
}

/* ── GAP event handler ───────────────────────────────────────────── */
static int gap_event_cb(struct ble_gap_event *ev, void *arg)
{
    (void)arg;

    switch (ev->type) {

    /* ── Encryption / pairing result ── */
    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI(TAG, "ENC_CHANGE conn=%u status=%d",
                 ev->enc_change.conn_handle, ev->enc_change.status);
        if (ev->enc_change.conn_handle == s_conn_handle) {
            if (ev->enc_change.status == 0) {
                /* Encrypted — now discover HID service or retry CCCD */
                if (!s_notifications_ready) {
                    if (s_cccd_handle != 0) {
                        uint8_t val[2] = {0x03, 0x00};  /* enable notify + indicate */
                        ESP_LOGI(TAG, "Encryption up — retrying CCCD write (handle=%u)", s_cccd_handle);
                        ble_gattc_write_flat(s_conn_handle, s_cccd_handle, val, 2,
                                             gatt_write_cccd_cb, NULL);
                    } else {
                        ESP_LOGI(TAG, "Encryption up — starting HID GATT discovery");
                        ble_gattc_disc_svc_by_uuid(s_conn_handle, &s_uuid_hid.u,
                                                   gatt_svc_disc_cb, NULL);
                    }
                }
            } else {
                /* Encryption failed — still try discovery without encryption */
                ESP_LOGW(TAG, "Encryption failed (status=%d) — trying discovery anyway",
                         ev->enc_change.status);
                if (s_hid_svc_end == 0) {
                    ble_gattc_disc_svc_by_uuid(s_conn_handle, &s_uuid_hid.u,
                                               gatt_svc_disc_cb, NULL);
                }
            }
        }
        break;

    /* ── Passkey / numeric comparison (Just Works — auto-accept) ── */
    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        struct ble_sm_io pkey = {0};
        pkey.action = ev->passkey.params.action;
        if (pkey.action == BLE_SM_IOACT_NUMCMP) {
            pkey.numcmp_accept = 1;
        } else if (pkey.action == BLE_SM_IOACT_INPUT) {
            pkey.passkey = 0;
        }
        ble_sm_inject_io(ev->passkey.conn_handle, &pkey);
        break;
    }

    /* ── Discovery report ── */
    case BLE_GAP_EVENT_DISC: {
        if (!s_scanning) break;
        struct ble_gap_disc_desc *d = &ev->disc;

        wireless_node_t n = {0};
        n.is_bt         = true;
        n.valid         = true;
        n.ble_addr_type = d->addr.type;
        memcpy(n.mac, d->addr.val, 6);
        snprintf(n.signal, sizeof(n.signal), "%d dBm", (int)d->rssi);
        snprintf(n.type_tag, sizeof(n.type_tag), "BLE");   /* default */

        /* Parse advertisement payload */
        if (d->length_data > 0 && d->data) {
            parse_adv(d->data, d->length_data,
                      n.name,     sizeof(n.name),
                      n.type_tag, sizeof(n.type_tag),
                      n.detail,   sizeof(n.detail));
        }

        /* Use address as name fallback */
        if (n.name[0] == '\0') {
            mac_to_str(d->addr.val, n.name, sizeof(n.name));
        }

        /* Verbose diagnostics: log every discovered device */
        {
            char _mac[18];
            mac_to_str(d->addr.val, _mac, sizeof(_mac));
            ESP_LOGD(TAG, "BLE disc: %s addrtype=%d rssi=%d '%s'",
                     _mac, d->addr.type, (int)d->rssi, n.name);
            if (memcmp(d->addr.val, s_target_kb2_mac, 6) == 0) {
                ESP_LOGI(TAG, "Target CardKB2 discovered: %s addrtype=%d rssi=%d",
                         _mac, d->addr.type, (int)d->rssi);
            }
        }

        if (_wireless_add_node(&n) == ESP_OK && s_scan_cb) {
            s_scan_cb();
        }
        break;
    }

    /* ── Scan complete ── */
    case BLE_GAP_EVENT_DISC_COMPLETE:
        ESP_LOGI(TAG, "BLE scan complete (reason=%d)", ev->disc_complete.reason);
        s_scanning = false;
        /* Notify UI so it can hide the scanning animation and show Refresh */
        if (s_scan_cb) s_scan_cb();
        break;

    /* ── Connection established ── */
    case BLE_GAP_EVENT_CONNECT:
        if (ev->connect.status == 0) {
            s_conn_handle   = ev->connect.conn_handle;
            s_hid_svc_start = 0;
            s_hid_svc_end   = 0;
            s_report_val    = 0;
            s_cccd_handle   = 0;
            s_notifications_ready = false;
            ESP_LOGI(TAG, "BLE connected handle=%u — updating connection params…",
                     s_conn_handle);
            
            /* Update connection parameters for better stability */
            struct ble_gap_upd_params params = {
                .itvl_min = 24,   // 30ms (24 * 1.25ms)
                .itvl_max = 40,   // 50ms (40 * 1.25ms)
                .latency = 0,
                .supervision_timeout = 500,  // 5000ms (500 * 10ms)
                .min_ce_len = 0,
                .max_ce_len = 0,
            };
            int upd_rc = ble_gap_update_params(s_conn_handle, &params);
            ESP_LOGI(TAG, "Connection params update: %s", 
                     upd_rc == 0 ? "OK" : esp_err_to_name(upd_rc));
            
            /* Skip pairing and directly discover HID service.
             * CardKB2 firmware expects pairing, but we'll try without it first. */
            ESP_LOGI(TAG, "Skipping pairing — discovering HID service directly");
            ble_gattc_disc_svc_by_uuid(s_conn_handle, &s_uuid_hid.u,
                                       gatt_svc_disc_cb, NULL);
        } else {
            ESP_LOGE(TAG, "BLE connect failed: %d", ev->connect.status);
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            if (g_wireless_status_cb) {
                ESP_LOGI(TAG, "UI status notify: disconnected (connect failed)");
                g_wireless_status_cb(true);
            }
        }
        break;

    /* ── Disconnected ── */
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "BLE disconnected (reason=%d)", ev->disconnect.reason);
        s_conn_handle         = BLE_HS_CONN_HANDLE_NONE;
        s_hid_svc_start       = 0;
        s_hid_svc_end         = 0;
        s_report_val          = 0;
        s_cccd_handle         = 0;
        s_notifications_ready = false;
        /* Notify UI (red dot) and schedule reconnect */
        if (g_wireless_status_cb) {
            ESP_LOGI(TAG, "UI status notify: disconnected (link lost)");
            g_wireless_status_cb(true);
        }
        /* Avoid reconnect storms that can starve UI/LVGL and trigger watchdog. */
        if (s_reconnect_tmr) {
            xTimerStop(s_reconnect_tmr, 0);
        }
        break;

    /* ── Incoming notification / indication ── */
    case BLE_GAP_EVENT_NOTIFY_RX: {
        uint16_t pkt_len = OS_MBUF_PKTLEN(ev->notify_rx.om);
        if (pkt_len == 0) break;
        uint8_t buf[9] = {0};   /* 9 = Report ID(1) + report data(8) */
        if (pkt_len > sizeof(buf)) pkt_len = sizeof(buf);
        os_mbuf_copydata(ev->notify_rx.om, 0, pkt_len, buf);

        printf("+BLE:RX,%u", (unsigned)pkt_len);
        for (unsigned i = 0; i < pkt_len; i++) {
            printf(",%02X", buf[i]);
        }
        printf("\r\n");
        fflush(stdout);

        ESP_LOGI(TAG, "HID notify: len=%u mod=0x%02x key=0x%02x",
                 (unsigned)pkt_len, buf[0], (pkt_len >= 3 ? buf[2] : 0));
        /* If Report ID present (9-byte payload), dump full buffer for debug */
        if (pkt_len == 9) {
            ESP_LOGI(TAG, "  [reportID=%u mod=%02x rsv=%02x kc=%02x %02x %02x %02x %02x %02x]",
                     buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7], buf[8]);
        }

        if (!g_wireless_recv_cb) break;
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(ev->notify_rx.conn_handle, &desc) != 0) break;

        /* Decode HID keyboard report → single ASCII byte.
         * KB2 sends: [Report ID=1][modifier][reserved][kc0..kc5] = 9 bytes
         * Some firmwares send:      [modifier][reserved][kc0..kc5] = 8 bytes
         * Release report (all keycodes = 0) is silently dropped.          */
        if (pkt_len >= 3) {
            uint8_t ch = hid_to_ascii(buf, pkt_len);
            if (ch != 0) {
                  printf("+BLE:ASCII,0x%02X,'%c'\r\n",
                      ch, (ch >= 0x20 && ch < 0x7F) ? ch : '?');
                  fflush(stdout);
                ESP_LOGI(TAG, "HID key → ASCII 0x%02x '%c'", ch,
                         (ch >= 0x20 && ch < 0x7F) ? ch : '?');
                g_wireless_recv_cb(true, desc.peer_ota_addr.val, &ch, 1);
            }
        } else {
            g_wireless_recv_cb(true, desc.peer_ota_addr.val, buf, pkt_len);
        }
        break;
    }

    /* ── MTU exchange ── */
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU updated: %u", ev->mtu.value);
        break;

    /* ── Connection parameters updated ── */
    case BLE_GAP_EVENT_CONN_UPDATE:
        ESP_LOGI(TAG, "Connection params updated: status=%d", ev->conn_update.status);
        break;

    default:
        break;
    }
    return 0;
}

/* ── NimBLE sync callback ────────────────────────────────────────── */
static void ble_on_sync(void)
{
    /* Ensure we have a valid public BD_ADDR */
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGW(TAG, "ble_hs_util_ensure_addr: %d", rc);
    }
    ESP_LOGI(TAG, "BLE host synchronized with controller");

    /* If a scan was requested before sync completed, start it now */
    if (s_scan_cb != NULL && !s_scanning) {
        ESP_LOGI(TAG, "BLE synced — starting deferred scan");
        ble_scan_start(s_scan_cb);
    }
}

static void ble_on_reset(int reason)
{
    ESP_LOGW(TAG, "BLE host reset (reason=%d)", reason);
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_scanning    = false;
}

/* ── NimBLE host task ────────────────────────────────────────────── */
static void nimble_host_task(void *arg)
{
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();                   /* blocks until nimble_port_stop() */
    nimble_port_freertos_deinit();
    vTaskDelete(NULL);
}

/* ── Stack init (called from wireless_mgr.c) ─────────────────────── */
esp_err_t ble_stack_init(void)
{
    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init: %s", esp_err_to_name(ret));
        return ret;
    }

    ble_hs_cfg.sync_cb  = ble_on_sync;
    ble_hs_cfg.reset_cb = ble_on_reset;

    /* ── SMP security: Just Works bonding (matches KB2's ESP_LE_AUTH_BOND) ── */
    ble_hs_cfg.sm_io_cap          = BLE_SM_IO_CAP_NO_IO;   /* no input/output → Just Works */
    ble_hs_cfg.sm_bonding         = 1;                      /* store LTK in NVS             */
    ble_hs_cfg.sm_mitm            = 0;                      /* no MITM protection           */
    ble_hs_cfg.sm_sc              = 0;                      /* disable Secure Connections   */
    ble_hs_cfg.sm_our_key_dist    = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist  = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    nimble_port_freertos_init(nimble_host_task);
    return ESP_OK;
}

/* ── Public API ──────────────────────────────────────────────────── */

esp_err_t ble_scan_start(wireless_result_cb_t cb)
{
    if (s_scanning) return ESP_OK;
    s_scan_cb  = cb;
    s_scanning = true;

    struct ble_gap_disc_params params = {
        .itvl              = 0x00A0,   /* 100 ms interval */
        .window            = 0x0010,   /* 10 ms window (reduce RX pressure) */
        .filter_policy     = BLE_HCI_SCAN_FILT_NO_WL,
        .limited           = 0,
        .passive           = 1,        /* passive scan to reduce adv report flood */
        .filter_duplicates = 1,        /* HCI-level dedup prevents NimBLE OOM */
    };

    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, 5000, &params, /* 5 s scan */
                          gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_disc: %d", rc);
        s_scanning = false;
        if (rc == BLE_HS_ENOTSYNCED) {
            /* Host not yet synced — keep s_scan_cb so ble_on_sync() retries */
            ESP_LOGW(TAG, "BLE not synced yet, scan queued for auto-retry");
            return ESP_OK;   /* don't surface error to UI */
        }
        s_scan_cb = NULL;
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "BLE scan started (passive, low-duty, no-filter, dedup)");
    return ESP_OK;
}

esp_err_t ble_scan_stop(void)
{
    if (!s_scanning) return ESP_OK;
    ble_gap_disc_cancel();
    s_scanning = false;
    ESP_LOGI(TAG, "BLE scan stopped");
    return ESP_OK;
}

bool ble_scan_is_active(void)
{
    return s_scanning;
}

esp_err_t ble_connect(const uint8_t addr[6], uint8_t addr_type)
{
    /* Cancel any ongoing scan first */
    if (s_scanning) {
        ble_gap_disc_cancel();
        s_scanning = false;
    }

    /* Store peer for auto-reconnect */
    memcpy(s_peer_addr, addr, 6);
    s_peer_addr_type = addr_type;
    s_auto_reconnect = true;

    /* Create one-shot reconnect timer on first use (3 s delay) */
    if (s_reconnect_tmr == NULL) {
        s_reconnect_tmr = xTimerCreate("ble_recon",
                                       pdMS_TO_TICKS(3000),
                                       pdFALSE, NULL,
                                       reconnect_timer_cb);
    }

    ble_addr_t peer = { .type = addr_type };
    memcpy(peer.val, addr, 6);

    struct ble_gap_conn_params cp = {
        .scan_itvl           = 0x0060,
        .scan_window         = 0x0030,
        .itvl_min            = BLE_GAP_INITIAL_CONN_ITVL_MIN,   /* 30 ms */
        .itvl_max            = BLE_GAP_INITIAL_CONN_ITVL_MAX,   /* 50 ms */
        .latency             = 0,
        .supervision_timeout = 0x0100,   /* 2.56 s */
        .min_ce_len          = 0x0010,
        .max_ce_len          = 0x0300,
    };

    int rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &peer,
                             10000 /* 10 s timeout */, &cp,
                             gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_connect: %d", rc);
        /* Schedule retry even if connect call failed */
        if (s_auto_reconnect && s_reconnect_tmr)
            xTimerStart(s_reconnect_tmr, 0);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "BLE connecting to %02X:%02X:%02X:%02X:%02X:%02X (type=%u)",
             addr[5], addr[4], addr[3], addr[2], addr[1], addr[0], addr_type);
    return ESP_OK;
}

void ble_disconnect(void)
{
    s_auto_reconnect = false;
    if (s_reconnect_tmr) xTimerStop(s_reconnect_tmr, 0);
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) return;
    ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
}

/* ═══════════════════════════════════════════════════════════════════
 *  Stubs — CONFIG_BT_NIMBLE_ENABLED not enabled
 * ═══════════════════════════════════════════════════════════════════ */
#else /* !CONFIG_BT_NIMBLE_ENABLED */

esp_err_t ble_stack_init(void)                              { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t ble_scan_start(wireless_result_cb_t cb)           { (void)cb; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t ble_scan_stop(void)                               { return ESP_OK; }
bool      ble_scan_is_active(void)                          { return false; }
esp_err_t ble_connect(const uint8_t a[6], uint8_t t)        { (void)a; (void)t; return ESP_ERR_NOT_SUPPORTED; }
void      ble_disconnect(void)                              {}

#endif /* CONFIG_BT_NIMBLE_ENABLED */
