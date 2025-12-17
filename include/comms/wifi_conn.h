/**
 * @file wifi_conn.h
 * @brief Configuración WiFi en modo estación (STA) para conectividad IoT
 * 
 * Inicializa WiFi y se conecta a red local con reintentos automáticos.
 * Credenciales (SSID/password) configuradas en wifi_credentials.h.
 * 
 * @author Tomás Vovard
 * @date Diciembre 2025
 */

#ifndef WIFIF_CONN_H
#define WIFI_CONN_H

#include "esp_err.h"
#include "wifi_credentials.h"

/* ========================================================================== */
/*                      CONFIGURACIÓN WIFI                                    */
/* ========================================================================== */

/** @brief Número máximo de reintentos de conexión antes de declarar fallo */
#define WIFI_MAX_RETRY 5

/* ========================================================================== */
/*                      FUNCIONES PÚBLICAS                                    */
/* ========================================================================== */

/**
 * @brief Inicializa WiFi y conecta a red configurada
 * 
 * Configura WiFi en modo estación (STA), registra manejadores de eventos
 * y espera conexión exitosa o fallo tras WIFI_MAX_RETRY intentos.
 * 
 * @return ESP_OK si conectado exitosamente, ESP_FAIL en caso contrario
 * 
 * @note Requiere NVS inicializado previamente (nvs_config_init)
 * @note Bloqueante: espera hasta conectar o agotar reintentos
 * @note Credenciales WiFi definidas en wifi_credentials.h
 */
esp_err_t wifi_conn_init();

#endif // WIFI_CONN_H