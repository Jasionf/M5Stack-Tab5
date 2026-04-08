#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIRELESS_MAX_NODES   32   

typedef struct {
    char    name[40];         
    char    type_tag[12];     
    bool    is_bt;            
    char    signal[12];       
    char    detail[24];       
    uint8_t mac[6];           
    uint8_t ble_addr_type;    
    bool    valid;            
} wireless_node_t;

typedef void (*wireless_result_cb_t)(void);

typedef void (*wireless_recv_cb_t)(bool is_bt,
                                   const uint8_t mac[6],
                                   const uint8_t *data,
                                   size_t len);

esp_err_t wireless_init(void);
i
int  wireless_get_results(wireless_node_t *out, int max_count);

int  wireless_node_count(void);

void wireless_clear_results(void);

void wireless_register_recv_cb(wireless_recv_cb_t cb);

typedef void (*wireless_status_cb_t)(bool is_disconnected);
void wireless_register_status_cb(wireless_status_cb_t cb);

esp_err_t espnow_scan_start(wireless_result_cb_t cb);

esp_err_t espnow_scan_stop(void);

bool espnow_scan_is_active(void);

esp_err_t espnow_connect(const uint8_t peer_mac[6]);

esp_err_t espnow_send(const uint8_t *data, size_t len);

void espnow_disconnect(void);

esp_err_t ble_scan_start(wireless_result_cb_t cb);

esp_err_t ble_scan_stop(void);

bool ble_scan_is_active(void);

esp_err_t ble_connect(const uint8_t addr[6], uint8_t addr_type);

void ble_disconnect(void);

#ifdef __cplusplus
}
#endif
