#include "comms/wifi_conn.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "core/nvs_config.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static const char *TAG = "WIFI_CONN";

static EventGroupHandle_t s_wifi_event_group;
static uint8_t retry_num = 0;

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data){
    if(event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START){
        esp_wifi_connect();
    }
    else if(event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED){
        if(retry_num < WIFI_MAX_RETRY){
            esp_wifi_connect();
            retry_num++;
            ESP_LOGW(TAG, "Fallo conexion WiFi, reintentando... (%d)", retry_num);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "Max reintentos alcanzados, marcando FAIL_BIT");
        }
    }
    else if(event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Obtuvo IP: " IPSTR, IP2STR(&event->ip_info.ip));
        retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_conn_init(){

    if(!nvs_is_init()){
        ESP_LOGE(TAG, "NVS no esta inicializado. No se puede continuar.");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    s_wifi_event_group = xEventGroupCreate();
    if(s_wifi_event_group == NULL){
        ESP_LOGE(TAG, "No se pudo crear el event group.");
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = { 0 };

    strncpy((char*)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password));

    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init: esperando conexión...");

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    esp_err_t ret = ESP_FAIL;

    if(bits & WIFI_CONNECTED_BIT){
        ESP_LOGI(TAG, "Conectado a SSDI: %s", WIFI_SSID);
        ret = ESP_OK;
    } else if(bits & WIFI_FAIL_BIT){
        ESP_LOGE(TAG, "No se pudo conectar a SSID: %s", WIFI_SSID);
        ret = ESP_FAIL;
    } else {
        ESP_LOGE(TAG, "Evento inesperado intentando conectarse");
        ret = ESP_FAIL;
    }

    return ret;
}