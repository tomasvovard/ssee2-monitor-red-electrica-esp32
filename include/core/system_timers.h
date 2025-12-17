/**
 * @file system_timers.h
 * @brief Timers por software para timeouts no bloqueantes en FreeRTOS
 * 
 * Implementación simple de timers basados en TickType_t para medir
 * intervalos de tiempo sin usar hardware timers ni vTaskDelay().
 * 
 * Usados en FSMs de control para:
 * - Recuperación tras fallas (CONTROL_REC_I_TIME_MS, CONTROL_REC_V_TIME_MS)
 * - Ventana de detección de fallas reiterativas (CONTROL_REPET_I_RST_MS)
 * 
 * @note No son timers de alta precisión - resolución limitada por tick rate de FreeRTOS
 * @author Tomás Vovard
 * @date Diciembre 2025
 */

#ifndef SYSTEM_TIMERS_H
#define SYSTEM_TIMERS_H

#include "freertos/FreeRTOS.h"
#include <stdbool.h>

/**
 * @brief Estructura de timer por software
 * 
 * @note No usar directamente - siempre mediante las funciones timer_*()
 */
typedef struct {
    TickType_t start_tick;
    uint32_t timeout_ms;
    bool active;
}sys_timer_t;

/* ========================================================================== */
/*                      FUNCIONES PÚBLICAS                                    */
/* ========================================================================== */

/**
 * @brief Inicia un timer con timeout especificado
 * 
 * @param timer Puntero a estructura de timer
 * @param tout_ms Duración en milisegundos
 * 
 * @note Captura el tick actual con xTaskGetTickCount()
 * @note Llamar múltiples veces reinicia el timer
 */
void timer_start(sys_timer_t *timer, uint32_t tout_ms);

/**
 * @brief Verifica si el timer expiró
 * 
 * @param timer Puntero a estructura de timer
 * @return true si transcurrió el timeout, false en caso contrario
 * 
 * @note Si timer no está activo (active=false), retorna false
 * @note Maneja correctamente el overflow de TickType_t
 */
bool timer_expired(sys_timer_t *timer);

/**
 * @brief Detiene un timer activo
 * 
 * @param timer Puntero a estructura de timer
 * 
 * @note Pone active=false, haciendo que timer_expired() retorne false
 */
void timer_stop(sys_timer_t *timer);

#endif // SYSTEM_TIMERS_H