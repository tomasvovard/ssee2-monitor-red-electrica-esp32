/**
 * @file state.h
 * @brief Gestión centralizada del estado global del sistema con protección concurrente
 * 
 * Este módulo guarda el estado del sistema, consolidando tres categorías de información:
 * 
 * 1. **Mediciones eléctricas**: Tensión, corriente, potencia, factor de potencia, energía
 * 2. **Estados de salida**: Estado ON/OFF de cada carga
 * 3. **Fallas activas**: Indicadores de protecciones disparadas
 * 
 * ## Características principales
 * 
 * ### Thread-Safety
 * Todos los accesos están protegidos por un mutex interno (state_mutex), permitiendo
 * que múltiples tareas (control, comunicación, display) accedan concurrentemente sin
 * race conditions.
 * 
 * ### Persistencia automática de energía
 * La energía acumulada se guarda automáticamente en NVS flash cada 1 kWh
 * (SAVE_ENERGY_THS_KWH) para sobrevivir a pérdidas de alimentación.
 * 
 * ### Change detection
 * Sistema opcional para detectar cambios significativos en el estado.
 * 
 * @author Tomás Vovard
 * @date Diciembre 2025
 */

#ifndef STATE_H
#define STATE_H

#include "config/system_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "app/measure.h"
#include "core/nvs_config.h"
#include "esp_log.h"

/* ========================================================================== */
/*                      ESTRUCTURAS DE DATOS                                  */
/* ========================================================================== */

/**
 * @brief Estado de las protecciones y fallas del sistema
 * 
 * Agrupa todos los indicadores de falla en una única estructura para
 * facilitar la sincronización con módulos de comunicación y display.
 */
typedef struct{
    bool FAIL_V[NUM_LOADS];
    bool FAIL_I;
    bool FAIL_I_NR;
}fail_t;

/**
 * @brief Estado completo del sistema en un instante dado
 * 
 * Todas las lecturas mediante state_get() obtienen una copia consistente
 * de esta estructura protegida por mutex.
 *
 */
typedef struct {
    measure_t measure;
    bool output[NUM_LOADS]; 
    fail_t fails;
} state_t;

/**
 * @brief Detector de cambios para optimización de comunicaciones
 * 
 * Mantiene el último estado enviado y timestamp para determinar si
 * hay cambios suficientemente significativos que justifiquen una transmisión.
 * 
 */
typedef struct {
    state_t last_sent;
    uint32_t last_update_time;
} change_detector_t;

/**
 * @brief Umbrales de cambio para detección de variaciones significativas
 * 
 * Define qué magnitud de cambio se considera "significativa" para cada
 * variable medida.
 * 
 * @see state_change_detector_update() para uso de estos umbrales
 */
typedef struct {
    float v_ths;
    float i_ths;
    float fp_ths;
    float e_ths;
    uint32_t tmin_ms;
} state_ths_t;

/* ========================================================================== */
/*                      FUNCIONES PÚBLICAS                                    */
/* ========================================================================== */

/**
 * @defgroup state_init Inicialización
 * @{
 */

/**
 * @brief Inicializa el módulo de gestión de estado
 * 
 * Operaciones realizadas:
 * - Crea el mutex de protección concurrente (state_mutex)
 * - Inicializa todas las estructuras a cero
 * - Carga energía acumulada desde NVS flash (si existe)
 * - Inicializa el sistema de guardado automático
 * 
 * @note Debe llamarse ANTES de cualquier otra función del módulo
 * @note Debe llamarse DESPUÉS de nvs_config_init()
 * 
 * @see nvs_load_energy() para restauración de energía persistida
 */
void state_init();

/** @} */ // end of state_init

/**
 * @defgroup state_update Actualización del estado
 * @{
 */

/**
 * @brief Actualiza las mediciones eléctricas en el estado global
 * 
 * Copia las nuevas mediciones (V, I, P, S, fp) y acumula la energía (E).
 * Si la energía acumulada supera el umbral SAVE_ENERGY_THS_KWH (típicamente 1 kWh),
 * guarda automáticamente en NVS flash.
 * 
 * @param m Puntero a estructura con las nuevas mediciones
 * 
 * @note Thread-safe - protegido por mutex interno
 * @note La energía se ACUMULA (no se sobrescribe): E_total += m->E
 * @note El guardado automático previene pérdida de datos por cortes de energía
 * 
 * @warning Esta función se llama muy frecuentemente desde task_adc_acquisition()
 */
void state_update_measure(const measure_t *m);

/**
 * @brief Actualiza el estado de las cargas en el estado global
 * 
 * @param out Array de NUM_LOADS booleanos con el estado de cada carga
 * 
 * @note Thread-safe - protegido por mutex interno
 * @note Se llama desde task_control() cada TASK_PERIOD_CONTROL_MS (típicamente 10ms)
 * @warning El array debe tener exactamente NUM_LOADS elementos
 */
void state_update_outputs(const bool *out);

/**
 * @brief Actualiza el estado de fallas y protecciones en el estado global
 * 
 * @param fails Puntero a estructura con los nuevos estados de falla
 * 
 * @note Thread-safe - protegido por mutex interno
 * @note Se llama desde task_control() cada vez que cambia alguna FSM
 */
void state_update_fails(const fail_t *fails); 

/** @} */ // end of state_update

/**
 * @defgroup state_read Lectura del estado
 * @{
 */

/**
 * @brief Obtiene una imagen instantánea del estado del sistema
 * 
 * Copia toda la estructura state_t de forma protegida por mutex.
 * Garantiza consistencia: no es posible leer mediciones nuevas con estados
 * de carga antiguos.
 * 
 * @param[out] out Puntero a estructura donde copiar el estado actual
 * 
 * @note Thread-safe - múltiples tareas pueden llamar simultáneamente
 * 
 */
void state_get(state_t *out);

/** @} */ // end of state_read

/**
 * @defgroup state_energy Gestión de energía acumulada
 * @{
 */

/**
 * @brief Resetea el contador de energía acumulada a cero
 * 
 * Operaciones realizadas:
 * - Pone E = 0.0 en memoria RAM
 * - Guarda 0.0 en NVS flash inmediatamente
 * - Resetea el umbral de guardado automático
 * 
 * @note Thread-safe - protegido por mutex interno
 * @note Se llama típicamente desde comandos UART "ENERGY RESET" o IoT
 * 
 * @see nvs_save_energy() para persistencia en flash
 */
void state_reset_energy();

/**
 * @brief Carga energía acumulada desde NVS flash
 * 
 * Llamada automáticamente por state_init() para restaurar el valor
 * persistido en caso de reinicio.
 * 
 * Si no hay valor guardado en NVS, inicializa en 0.0 sin error.
 * 
 * @note Thread-safe - protegido por mutex interno
 * @note Se llama una sola vez al inicio del sistema
 * 
 * @see nvs_load_energy() para lectura desde flash
 */
void state_set_energy();

/** @} */ // end of state_energy

/**
 * @defgroup state_change_detection Detección de cambios para comunicaciones
 * @{
 */

/**
 * @brief Inicializa un detector de cambios
 * 
 * Prepara la estructura change_detector_t para su primer uso.
 * Pone last_update_time en 0, lo que fuerza la primera actualización
 * a retornar siempre true (envía el estado inicial).
 * 
 * @param[out] detector Puntero a estructura a inicializar
 * 
 * @note No requiere mutex - es una operación local
 * @note Llamar una vez por cada instancia de change_detector_t
 * 
 */
void state_change_detector_init(change_detector_t *detector);

/**
 * @brief Verifica si el estado cambió significativamente respecto al último envío
 * 
 * Retorna true si se cumple ALGUNA de estas condiciones:
 * 1. Es la primera llamada (last_update_time == 0)
 * 2. |V_actual - V_enviado| > v_ths
 * 3. |I_actual - I_enviado| > i_ths
 * 4. |fp_actual - fp_enviado| > fp_ths
 * 5. |E_actual - E_enviado| > e_ths
 * 6. Cambió el estado de alguna carga (output[])
 * 7. Cambió alguna falla (fails.*)
 * 8. Han pasado más de tmin_ms milisegundos
 * 
 * @param detector Puntero a detector previamente inicializado
 * @param s Puntero al estado actual a evaluar
 * @param ths Puntero a estructura con umbrales de detección
 * 
 * @return true si hay cambio significativo (debe actualizar), false en caso contrario
 * 
 * @note No modifica el detector - usar state_change_detector_mark_sent() después
 * @note No requiere mutex - opera sobre copias locales del estado
 * 
 */
bool state_change_detector_update(change_detector_t *detector, const state_t *s, state_ths_t *ths);

/**
 * @brief Marca un estado como enviado en el detector de cambios
 * 
 * Actualiza el detector con el estado recién transmitido y el timestamp actual.
 * Las próximas llamadas a state_change_detector_update() compararán contra
 * este estado guardado.
 * 
 * @param detector Puntero a detector a actualizar
 * @param sent Puntero al estado que fue enviado
 * 
 * @note No requiere mutex - opera sobre datos locales
 * 
 */
void state_change_detector_mark_sent(change_detector_t *detector, const state_t *sent);

/** @} */ // end of state_change_detection

#endif // STATE_H