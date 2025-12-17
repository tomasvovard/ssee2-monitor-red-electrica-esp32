/**
 * @file display_manager.h
 * @brief Driver del display OLED SSD1306 128x64 para visualización de estado
 * 
 * Implementación custom de driver I2C para pantalla OLED monocromática.
 * Muestra en tiempo real: mediciones, estado de cargas y fallas activas.
 * 
 * Hardware: SSD1306 128x64 píxeles conectado por I2C a pines GPIO21 (SDA) y GPIO22 (SCL)
 * 
 */

#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"  
#include "config/system_config.h"
#include "app/state.h"

/* ========================================================================== */
/*                      CONFIGURACIÓN I2C                                     */
/* ========================================================================== */

/** @brief Puerto I2C utilizado para el display */
#define I2C_PORT_DISPLAY     I2C_NUM_0

/** @brief Pin GPIO para línea SDA del I2C */
#define I2C_SDA_DISPLAY       GPIO_NUM_21

/** @brief Pin GPIO para línea SCL del I2C */
#define I2C_SCL_DISPLAY       GPIO_NUM_22

/** @brief Frecuencia del bus I2C: 400 kHz (fast mode) */
#define I2C_FREQ_DISPLAY_HZ   400000

/** @brief Timeout para operaciones I2C [ms] */
#define I2C_MASTER_TIMEOUT_MS 500

/* ========================================================================== */
/*                      CONFIGURACIÓN SSD1306                                 */
/* ========================================================================== */

/** @brief Dirección I2C del SSD1306 */
#define SSD1306_I2C_ADDR      0x3C

/** @brief Ancho del display en píxeles */
#define SSD1306_WIDTH         128

/** @brief Alto del display en píxeles */
#define SSD1306_HEIGHT        64

/** @brief Prefijo de comando para protocolo SSD1306 */
#define SSD1306_CMD  0x00

/** @brief Prefijo de datos para protocolo SSD1306 */
#define SSD1306_DATA 0x40

/** @brief Número de líneas de texto disponibles (8 píxeles por línea) */
#define SSD1306_MAX_TXT_LINES 8

/* ========================================================================== */
/*                      FUNCIONES PÚBLICAS - OLED                             */
/* ========================================================================== */

/**
 * @brief Inicializa el display OLED SSD1306
 * 
 * Configura I2C, envía secuencia de inicialización y limpia la pantalla.
 * 
 * @return ESP_OK si exitoso, error en caso contrario
 * @note Debe llamarse antes de usar otras funciones oled_*
 */
esp_err_t oled_init(void);

/**
 * @brief Limpia completamente el display (pantalla negra)
 * 
 * @return ESP_OK si exitoso, error en caso contrario
 */
esp_err_t oled_clear(void);

/**
 * @brief Dibuja una línea de texto en el display
 * 
 * @param row Fila de texto (0-7, cada una de 8 píxeles de alto)
 * @param text Cadena de texto a mostrar (máximo ~21 caracteres)
 * 
 * @return ESP_OK si exitoso, error en caso contrario
 * 
 * @note Usa fuente 5x7 píxeles con 1 píxel de separación (6 píxeles/char)
 * @note Caracteres fuera de rango ASCII 32-126 se muestran como '?'
 */
esp_err_t oled_draw_text_line(uint8_t row, const char *text);

/* ========================================================================== */
/*                      FUNCIONES PÚBLICAS - DISPLAY MANAGER                  */
/* ========================================================================== */

/**
 * @brief Inicializa el módulo de gestión de display
 * 
 * Wrapper sobre oled_init() con logging adicional.
 * 
 * @return ESP_OK si exitoso, error en caso contrario
 */
esp_err_t display_init(void);

/**
 * @brief Tarea periódica de actualización del display
 * 
 * Lee estado del sistema cada TASK_PERIOD_DISPLAY_MS (~500ms) y actualiza:
 * - Línea 0-4: Mediciones (V, I, P, S, fp, E)
 * - Línea 5: Estados de cargas (L1-L4)
 * - Línea 6-7: Indicadores de fallas
 * 
 * @param pvParameters Parámetro estándar de FreeRTOS (no usado)
 * 
 * @note Actualización a ~2 Hz (suficiente para lectura humana)
 */
void task_display(void *pvParameters);

#endif // DISPLAY_MANAGER_H
