/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * Zigbee HA_on_off_light Example
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */
#include "nvs_flash.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "esp_zb_light.h"

#define LIGHT_ENDPOINTS 35

/**
 * @note Make sure set idf.py menuconfig in zigbee component as zigbee end device!
*/
#if !defined ZB_ED_ROLE
#error Define ZB_ED_ROLE in idf.py menuconfig to compile light (End Device) source code.
#endif

static const char *TAG = "ESP_ZB_ON_OFF_LIGHT";
/********************* Define functions **************************/
static void bdb_start_top_level_commissioning_cb(uint8_t mode_mask)
{
    ESP_ERROR_CHECK(esp_zb_bdb_start_top_level_commissioning(mode_mask));
}

static uint8_t endpoint_values[LIGHT_ENDPOINTS] = { 0 };

static void toggle_endpoint(uint8_t endpoint)
{
    uint8_t value = endpoint_values[endpoint - HA_ESP_LIGHT_ENDPOINT] ? 0 : 1;

    ESP_LOGI(TAG, "setting endpoint %u: %d -> %d",
        endpoint, endpoint_values[endpoint - HA_ESP_LIGHT_ENDPOINT], value);

    endpoint_values[endpoint - HA_ESP_LIGHT_ENDPOINT] = value;

    esp_zb_zcl_set_attribute_val(endpoint, ESP_ZB_ZCL_CLUSTER_ID_ON_OFF,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID,
        &endpoint_values[endpoint - HA_ESP_LIGHT_ENDPOINT], false);
}

void attr_cb(uint8_t status, uint8_t endpoint, uint16_t cluster_id, uint16_t attr_id, void *new_value)
{
    if (cluster_id == ESP_ZB_ZCL_CLUSTER_ID_ON_OFF) {
        uint8_t value = *(uint8_t *)new_value;
        if (attr_id == ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID) {
            /* store the value for this endpoint, and toggle the next endpoint */
            uint8_t next_endpoint = (((endpoint - HA_ESP_LIGHT_ENDPOINT) + 1) % LIGHT_ENDPOINTS) + HA_ESP_LIGHT_ENDPOINT;

            ESP_LOGI(TAG, "endpoint %u on/off light set %d -> %d",
                endpoint, endpoint_values[endpoint - HA_ESP_LIGHT_ENDPOINT], value);
            esp_zb_scheduler_alarm(toggle_endpoint, next_endpoint, 0);

            endpoint_values[endpoint - HA_ESP_LIGHT_ENDPOINT] = value;
        }
    } else {
        /* Implement some actions if needed when other cluster changed */
        ESP_LOGI(TAG, "cluster:0x%x, attribute:0x%x changed ", cluster_id, attr_id);
    }
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p       = signal_struct->p_app_signal;
    esp_err_t err_status = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = *p_sg_p;
    switch (sig_type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Zigbee stack initialized");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;
    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (err_status == ESP_OK) {
            ESP_LOGI(TAG, "Start network steering");
            esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
        } else {
            /* commissioning failed */
            ESP_LOGW(TAG, "Failed to initialize Zigbee stack (status: %s)", esp_err_to_name(err_status));
        }
        break;
    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err_status == ESP_OK) {
            esp_zb_ieee_addr_t extended_pan_id;
            esp_zb_get_extended_pan_id(extended_pan_id);
            ESP_LOGI(TAG, "Joined network successfully (Extended PAN ID: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x, PAN ID: 0x%04hx, Channel:%d)",
                     extended_pan_id[7], extended_pan_id[6], extended_pan_id[5], extended_pan_id[4],
                     extended_pan_id[3], extended_pan_id[2], extended_pan_id[1], extended_pan_id[0],
                     esp_zb_get_pan_id(), esp_zb_get_current_channel());
        } else {
            ESP_LOGI(TAG, "Network steering was not successful (status: %s)", esp_err_to_name(err_status));
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb, ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
        }
        break;
    default:
        ESP_LOGI(TAG, "ZDO signal: %s (0x%x), status: %s", esp_zb_zdo_signal_to_string(sig_type), sig_type,
                 esp_err_to_name(err_status));
        break;
    }
}

static void esp_zb_task(void *pvParameters)
{
    /* initialize Zigbee stack with Zigbee end-device config */
    esp_zb_cfg_t zb_nwk_cfg = ESP_ZB_ZED_CONFIG();
    esp_zb_init(&zb_nwk_cfg);
    /* set the on-off light device config */
    esp_zb_on_off_cluster_cfg_t light_cfg = {
		.on_off = ESP_ZB_ZCL_ON_OFF_ON_OFF_DEFAULT_VALUE,
	};

    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();
    esp_zb_attribute_list_t *basic_cluster = esp_zb_basic_cluster_create(NULL);

    for (uint i = 0; i < LIGHT_ENDPOINTS; i++) {
        esp_zb_cluster_list_t *cluster_list = esp_zb_zcl_cluster_list_create();

        esp_zb_cluster_list_add_basic_cluster(cluster_list, basic_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
        esp_zb_cluster_list_add_on_off_cluster(cluster_list, esp_zb_on_off_cluster_create(&light_cfg), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
        esp_zb_ep_list_add_ep(ep_list, cluster_list, HA_ESP_LIGHT_ENDPOINT + i, ESP_ZB_AF_HA_PROFILE_ID, ESP_ZB_HA_ON_OFF_LIGHT_DEVICE_ID);
    }

    esp_zb_device_register(ep_list);
    esp_zb_device_add_set_attr_value_cb(attr_cb);
    esp_zb_set_primary_network_channel_set(ESP_ZB_PRIMARY_CHANNEL_MASK);
    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_main_loop_iteration();
}

void app_main(void)
{
    esp_zb_platform_config_t config = {
        .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_ZB_DEFAULT_HOST_CONFIG(),
    };
    ESP_ERROR_CHECK(nvs_flash_init());
    /* load Zigbee light_bulb platform config to initialization */
    ESP_ERROR_CHECK(esp_zb_platform_config(&config));
    /* hardware related and device init */
    light_driver_init(LIGHT_DEFAULT_OFF);
    xTaskCreate(esp_zb_task, "Zigbee_main", 4096, NULL, 5, NULL);
}
