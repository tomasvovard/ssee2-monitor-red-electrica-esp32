/**
 * @file gpio_loads.h
 * @brief HAL para control de GPIOs de las 4 cargas de salida
 * 
 * Maneja las salidas digitales que conmutan relés/SSRs de las cargas.
 * Incluye verificación de escritura (read-back) para detectar fallas de hardware.
 * 
 * @author Tomás Vovard
 * @date Diciembre 2025
 */

#ifndef GPIO_LOADS_H
#define GPIO_LOADS_H

#include "driver/gpio.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

/* ========================================================================== */
/*                      ASIGNACIÓN DE PINES GPIO                              */
/* ========================================================================== */

/** @brief GPIO de control para carga 0 */
#define IO_LOAD_0   GPIO_NUM_16

/** @brief GPIO de control para carga 1 */
#define IO_LOAD_1   GPIO_NUM_17

/** @brief GPIO de control para carga 2 */
#define IO_LOAD_2   GPIO_NUM_18

/** @brief GPIO de control para carga 3 */
#define IO_LOAD_3   GPIO_NUM_19

/** @brief Máscara de bits con todos los GPIOs de carga para inicialización */
#define LOADS_OUT_MASK ((1ULL <<IO_LOAD_0) | (1ULL <<IO_LOAD_1) | (1ULL <<IO_LOAD_2) | (1ULL <<IO_LOAD_3))

/* ========================================================================== */
/*                      FUNCIONES PÚBLICAS                                    */
/* ========================================================================== */

/**
 * @brief Inicializa GPIOs de las cargas como salidas en estado bajo
 * 
 * @return ESP_OK si exitoso, error en caso contrario
 * @note Todas las cargas inician apagadas (nivel bajo)
 */
esp_err_t gpio_loads_init();

/**
 * @brief Actualiza estado de una carga con verificación de escritura
 * 
 * @param id Identificador de carga (0-3)
 * @param level Estado deseado (true=ON, false=OFF)
 * 
 * @return true si escritura exitosa y verificada, false si falla read-back
 * 
 * @note Realiza gpio_set_level() seguido de gpio_get_level() para verificar
 * @warning Si retorna false, existe desincronización hardware-software crítica
 */
bool gpio_load_update(uint8_t id, bool level);

/**
 * @brief Lee estado actual de todas las cargas desde hardware
 * 
 * @param[out] st Array de 4 booleanos con estados leídos (true=ON, false=OFF)
 * 
 * @note Útil para sincronización inicial o verificación periódica
 */
void gpio_loads_get_state(bool *st);

#endif // GPIO_LOADS_H