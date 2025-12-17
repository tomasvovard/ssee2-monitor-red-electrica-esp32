/**
 * @file iot_mqtt.h
 * @brief Cliente MQTT para comunicación IoT con Node-RED/broker externo
 * 
 * Implementa telemetría periódica, eventos de fallas y recepción de comandos remotos.
 * 
 * Arquitectura:
 * - Publica: Telemetría (1 Hz) con mediciones y estado, eventos de cambio de fallas
 * - Suscribe: Comandos de control (modo, cargas, configuración)
 * 
 * Formato: JSON via cJSON library
 * 
 * @author Tomás Vovard
 * @date Diciembre 2025
 */

#ifndef IOT_MQTT_H
#define IOT_MQTT_H

#include <stdbool.h>
#include <stdint.h>
#include "config/system_config.h"
#include "app/state.h"
#include "mqtt_client.h"

/* ========================================================================== */
/*                      CONFIGURACIÓN MQTT                                    */
/* ========================================================================== */

/** @brief URI del broker MQTT
 *
 * @todo: modificar según red local */
#define MQTT_BROKER_URI "mqtt://192.168.0.119"

/** @brief Identificador único del dispositivo */
#define MQTT_DEVICE_ID "esp32_01"

/** @brief Topic para publicación de telemetría periódica (V, I, P, S, fp, E, estados) */
#define MQTT_TOPIC_TEL "sm/"MQTT_DEVICE_ID"/telemetry"

/** @brief Topic para publicación de eventos (cambios de fallas) */
#define MQTT_TOPIC_EVT "sm/"MQTT_DEVICE_ID"/event"

/** @brief Topic para suscripción de comandos remotos */
#define MQTT_TOPIC_CMD "sm/"MQTT_DEVICE_ID"/cmd"

/** @brief Tamaño máximo de payload JSON de comando */
#define IOT_CMD_JSON_MAX_LEN 256

/* ========================================================================== */
/*                      TIPOS DE COMANDOS REMOTOS                             */
/* ========================================================================== */

/**
 * @brief Tipos de comandos MQTT soportados
 * 
 * Todos los comandos se reciben en formato JSON en MQTT_TOPIC_CMD.
 */
typedef enum {
    IOT_CMD_NONE = 0,
    IOT_CMD_MODE_SET,
    IOT_CMD_LOAD_SET,
    IOT_CMD_ENERGY_RESET,
    IOT_CMD_CFG_IMAX_SET,
    IOT_CMD_CFG_VRANGE_SET,
    IOT_CMD_CFG_AUTOREC_SET,
    IOT_CMD_CFG_PRIORITY_SET
} iot_cmd_type;

/**
 * @brief Estructura de comando IoT parseado desde JSON
 * 
 * Usa unión para almacenar parámetros específicos de cada tipo de comando,
 * minimizando uso de memoria.
 * 
 */
typedef struct {
    iot_cmd_type type;
    union {
        struct {
            bool manual;
        } mode_set;

        struct {
            uint8_t id;
            bool on;
        } load_set;

        struct {
            float imax;
        } cfg_imax_set;

        struct {
            uint8_t id;
            int16_t vmin;
            int16_t vmax;
        } cfg_vrange_set;

        struct {
            uint8_t id;
            bool ena;
        } cfg_autorec_set;

        struct {
            uint8_t id;
            uint8_t pr;
        } cfg_priority_set;
        
    };
}iot_cmd_t;

/* ========================================================================== */
/*                      FUNCIONES PÚBLICAS                                    */
/* ========================================================================== */

/**
 * @brief Inicializa cliente MQTT y conecta al broker
 * 
 * Registra manejadores de eventos y se suscribe a MQTT_TOPIC_CMD.
 * 
 * @note Requiere WiFi conectado previamente
 */
void iot_mqtt_init();

/**
 * @brief Tarea de transmisión MQTT (publicación de telemetría y eventos)
 * 
 * Publica cada TASK_PERIOD_COMM_IOT_MS (~1 Hz):
 * - Telemetría completa en MQTT_TOPIC_TEL
 * - Eventos de cambio de fallas en MQTT_TOPIC_EVT
 * 
 * @param pvParameters Parámetro estándar de FreeRTOS (no usado)
 */
void task_iot_tx(void *pvParameters);

/**
 * @brief Tarea de recepción MQTT (procesamiento de comandos)
 * 
 * Parsea comandos JSON desde cola interna y ejecuta acciones sobre
 * el módulo de control.
 * 
 * @param pvParameters Parámetro estándar de FreeRTOS (no usado)
 * 
 * @note Los comandos inválidos se descartan con warning en log
 */
void task_iot_rx(void *pvParameters);

#endif