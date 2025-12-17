/**
 * @file uart_protocol.h
 * @brief Protocolo de comunicación UART para control y monitoreo remoto
 * 
 * Implementa interfaz de línea de comandos sobre UART0 con arquitectura de 3 tareas:
 * - task_uart_rx: Recepción y parsing de comandos
 * - task_uart_handler: Procesamiento de comandos
 * - task_uart_tx: Transmisión de respuestas y alertas automáticas
 * 
 * Características:
 * - Autenticación con sesiones temporales
 * - Telemetría continua u on-demand
 * - Alertas automáticas de fallas
 * 
 * @author Tomás Vovard
 * @date Diciembre 2025
 */

#ifndef UART_PROTOCOL_H
#define UART_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include "config/system_config.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "app/measure.h"

/* ========================================================================== */
/*                      CONFIGURACIÓN UART                                    */
/* ========================================================================== */

/** @brief Puerto UART utilizado (UART0, USB-serial del ESP32) */
#define UART_NUM UART_NUM_0

/** @brief Tasa de baudios */
#define UART_BAUD_RATE 115200

/** @brief Pin GPIO de transmisión TX */
#define UART_TX_PIN GPIO_NUM_1

/** @brief Pin GPIO de recepción RX */
#define UART_RX_PIN GPIO_NUM_3

/** @brief Tamaño del buffer de hardware UART */
#define UART_BUF_SIZE 1024

/** @brief Tamaño de cola de comandos recibidos */
#define UART_RX_QUEUE_SIZE 10

/** @brief Tamaño de cola de respuestas pendientes */
#define UART_TX_QUEUE_SIZE 10

/** @brief Longitud máxima de comando en caracteres */
#define CMD_MAX_LEN 64

/** @brief Longitud máxima de parámetros de comando */
#define PARAMS_MAX_LEN 128

/** @brief Longitud máxima de respuesta */
#define RESPONSE_MAX_LEN 256

/* ========================================================================== */
/*                      TIPOS DE USUARIO Y SESIÓN                             */
/* ========================================================================== */

/**
 * @brief Niveles de acceso de usuario
 */
typedef enum {
    USER_VIEWER = 0, /**< Solo lectura (comandos GET, sin autenticación) */
    USER_ADMIN,      /**< Control completo (requiere LOGIN) */
    USER_LEVELS
} user_level_t;

/**
 * @brief Modos de visualización de telemetría
 */
typedef enum {
    DISP_CONT = 0,  /**< Envío continuo cuando hay cambio significativo */
    DISP_ONETIME   /**< Solo respuesta a comando MEAS GET */
} uart_disp_mode_t;

/**
 * @brief Estructura de sesión de usuario
 */
typedef struct{
    user_level_t level;     /**< Nivel de acceso actual */
    TickType_t login_time;  /**< Timestamp de último login (para timeout) */
    bool active;            /**< true si sesión válida */
} session_t;

/* ========================================================================== */
/*                      ESTRUCTURAS DE PROTOCOLO                              */
/* ========================================================================== */

/**
 * @brief Comando UART parseado
 * 
 * Formato de línea: "COMANDO param1 param2 ...\r\n"
 */
typedef struct {
    char cmd[CMD_MAX_LEN];          /**< Comando en mayúsculas */
    char params[PARAMS_MAX_LEN];    /**< Parámetros sin parsear */
    session_t *session;             /**< Sesión asociada al comando */
} uart_cmd_t;

/**
 * @brief Respuesta UART formateada
 * 
 * Formato: "OK mensaje\r\n" o "ERROR mensaje\r\n" o "ALERTA: ...\r\n"
 */
typedef struct {
    char data[RESPONSE_MAX_LEN];    /**< Respuesta o alerta formateada */
    bool is_alert;                  /**< true si es alerta automática */
}uart_resp_t;

/**
 * @brief Estado global del protocolo UART
 */
typedef struct {
    uart_disp_mode_t disp_mode;     /**< Modo de visualización actual */
    measure_t last_sent;            /**< Última medición enviada (para change detection) */
    uint32_t last_upd_time;         /**< Timestamp de último envío [ms] */
    session_t session;              /**< Sesión activa */
} uart_state_t; 

/* ========================================================================== */
/*                      TIPOS DE COMANDOS SOPORTADOS                          */
/* ========================================================================== */

/**
 * @brief Enumeración de comandos reconocidos
 * 
 * Comandos agrupados por nivel de acceso:
 * - Viewer: PING, USERID, MEAS, MODE, LOAD, DISPMODE, HELP
 * - Admin: LOGIN, LOGOUT, ENERGY, CFG
 */
typedef enum {
    CMD_PING = 0,       /**< Test de conectividad */
    CMD_LOGIN,          /**< Autenticación */
    CMD_LOGOUT,         /**< Cerrar sesión */
    CMD_USERID,         /**< Consultar nivel de acceso */
    CMD_MEAS,           /**< Obtener mediciones */
    CMD_MODE,           /**< Get/Set modo de control */
    CMD_LOAD,           /**< Get/Set estado de cargas */
    CMD_ENERGY,         /**< Reset de energía */
    CMD_CFG,            /**< Configuración del sistema */
    CMD_DISPMODE,       /**< Modo de visualización */
    CMD_HELP,           /**< Ayuda */
    CMD_UNK             /**< Comando no reconocido */
} cmd_type_t;

/**
 * @brief Mapeo de string de comando a tipo enumerado
 */
typedef struct{
    const char *str;    /**< Comando como string */
    cmd_type_t type;    /**< Tipo de comando */
} cmd_map_t;

/* ========================================================================== */
/*                      FUNCIONES PÚBLICAS                                    */
/* ========================================================================== */

/**
 * @brief Inicializa driver UART y estructuras del protocolo
 * 
 * Configura hardware UART y crea colas de comandos/respuestas.
 */
void uart_protocol_init();

/**
 * @brief Tarea de recepción UART
 * 
 * Lee caracteres del UART byte a byte, ensambla líneas completas y
 * parsea comandos que envía a la cola de handler.
 * 
 * @param pvParameters Parámetro estándar de FreeRTOS (no usado)
 * 
 * @note Timeout de línea: 30 segundos (descarta líneas incompletas)
 */
void task_uart_rx(void *pvParameters);

/**
 * @brief Tarea de transmisión UART
 * 
 * Envía respuestas pendientes de la cola y genera alertas automáticas:
 * - Cambios de fallas (FAIL_I, FAIL_V)
 * - Reposiciones automáticas
 * - Telemetría continua si DISP_CONT activo
 * 
 * @param pvParameters Parámetro estándar de FreeRTOS (no usado)
 * 
 * @note Periodo de actualización: TASK_PERIOD_COMM_UART_MS (~100 ms)
 */
void task_uart_tx(void *pvParameters);

/**
 * @brief Tarea de procesamiento de comandos
 * 
 * Consume comandos de la cola, ejecuta lógica de negocio y
 * genera respuestas en la cola de transmisión.
 * 
 * @param pvParameters Parámetro estándar de FreeRTOS (no usado)
 */
void task_uart_handler(void *pvParameters);

/**
 * @brief Configura modo de visualización de telemetría
 * 
 * @param mode DISP_CONT para envío automático, DISP_ONETIME para bajo demanda
 */
void uart_set_disp_mode(uart_disp_mode_t mode);

/**
 * @brief Obtiene modo de visualización actual
 * 
 * @return Modo configurado
 */
uart_disp_mode_t uart_get_disp_mode(void);

#endif // UART_PROTOCOL_H