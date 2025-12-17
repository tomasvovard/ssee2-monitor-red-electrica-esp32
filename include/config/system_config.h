/**
 * @file system_config.h
 * @brief Configuración centralizada de parámetros del sistema
 * 
 * Este header consolida la mayoría de los parámetros configurables del sistema en un único punto.
 * 
 * ## Categorías de configuración
 * 
 * 1. **Tareas FreeRTOS**: Prioridades, stacks y períodos
 * 2. **Control de cargas**: Timers de protección y recuperación
 * 3. **Medición ADC**: Frecuencias, ventanas y cálculos derivados
 * 4. **Comunicaciones**: Umbrales de change detection
 * 5. **Persistencia**: Frecuencia de guardado en NVS
 * 
 * ## Dependencias críticas
 * 
 * Varios parámetros están **matemáticamente relacionados**. Cambiar uno puede
 * requerir ajustar otros para mantener coherencia:
 * 
 * - `SAMPLE_FREQ_HZ` → afecta `PAIRS_PER_CYCLE`
 * - `NUM_CYCLES_ACCUM` → afecta latencia de medición y precisión
 * - `TASK_PRIORITY_*` → orden debe respetar criticidad temporal
 * 
 * @note Para cambios de configuración persistentes, preferir NVS sobre recompilar
 * 
 * @author Tomás Vovard
 * @date Diciembre 2025
 */

#ifndef SYSTEM_CONFIG_H
#define SYSTEM_CONFIG_H

#include "freertos/FreeRTOS.h"
#include "system_timers.h"

/* ========================================================================== */
/*                      PRIORIDADES DE TAREAS                                 */
/* ========================================================================== */

/**
 * @defgroup task_priorities Prioridades de tareas FreeRTOS
 * 
 * Rango válido: [0, configMAX_PRIORITIES-1] donde mayor número = mayor prioridad
 * 
 * ## Jerarquía y justificación
 * 
 * El orden de prioridades está diseñado para garantizar requisitos de tiempo real:
 * 
 * 1. **ADC Acquisition (6)** - Máxima prioridad
 *    - Muestreo a 20 kHz requiere latencia <50 μs
 *    - Pérdida de muestras → errores de medición irrecuperables
 * 
 * 2. **Control (5)** - Segunda prioridad
 *    - Protecciones de seguridad (sobrecorriente, tensión)
 * 
 * 3. **UART Communication (4)** - Tercera prioridad
 *    - Comandos de usuario/configuración
 *    - Buffer limitado (1 KB) → necesita vaciarse rápidamente
 * 
 * 4. **Display (3)** - Prioridad media
 *    - Actualización visual no es crítica
 * 
 * 5. **IoT Communication (2)** - Baja prioridad
 *    - Network I/O puede tomar varios segundos
 *    - Menos crítico que interacción local (UART/Display)
 * 
 * @{
 */

/** @brief Prioridad de tarea de adquisición ADC (la más alta del sistema) */
#define TASK_PRIORITY_ADC_ACQ 6

/** @brief Prioridad de tarea de control de cargas y protecciones */
#define TASK_PRIORITY_CONTROL 5

/** @brief Prioridad de tarea de comunicación UART */
#define TASK_PRIORITY_COMM_UART 4

/** @brief Prioridad de tarea de actualización de display OLED */
#define TASK_PRIORITY_COMM_IOT 2

/** @brief Prioridad de tarea de comunicación IoT (MQTT) */
#define TASK_PRIORITY_DISPLAY 3 

/** @} */ // end of task_priorities

/* ========================================================================== */
/*                      TAMAÑOS DE STACK                                      */
/* ========================================================================== */

/**
 * @defgroup task_stacks Tamaños de stack de tareas (en palabras de 4 bytes)
 * 
 * ## Cálculo de stack necesario
 * 
 * Factores considerados:
 * - Variables locales de la tarea
 * - Profundidad de llamadas a funciones
 * - Buffers temporales (ej: strings de printf)
 * - Margen de seguridad
 * 
 * @{
 */

/** @brief Stack para ADC acquisition: 16 KB */
#define TASK_STACK_ADC_ACQ 4096

/** @brief Stack para control: 12 KB */
#define TASK_STACK_CONTROL 3072 

/** @brief Stack para comunicación UART: 16 KB */
#define TASK_STACK_COMM_UART 4096

/** @brief Stack para comunicación IoT: 12 KB */
#define TASK_STACK_COMM_IOT 3072

/** @brief Stack para display: 12 KB */
#define TASK_STACK_DISPLAY 3072 

/** @} */ // end of task_stacks

/* ========================================================================== */
/*                      PERÍODOS DE TAREAS                                    */
/* ========================================================================== */

/**
 * @defgroup task_periods Períodos de ejecución de tareas periódicas
 * 
 * Define con qué frecuencia se ejecuta cada tarea (via vTaskDelay).
 * 
 * @note task_adc_acquisition NO tiene período - bloqueante en DMA
 * 
 * @{
 */

/** @brief Período de tarea de control [ms] - 10ms */
#define TASK_PERIOD_CONTROL_MS 10 

/** @brief Período de tarea de comunicación UART [ms] - 100ms */
#define TASK_PERIOD_COMM_UART_MS 100

/** @brief Período de tarea de comunicación IoT [ms] - 1000ms */
#define TASK_PERIOD_COMM_IOT_MS 1000

/** @brief Período de tarea de display [ms] - 500ms → 2 Hz de refresco del OLED */
#define TASK_PERIOD_DISPLAY_MS  500 

/** @} */ // end of task_periods

/* ========================================================================== */
/*                      CONFIGURACIÓN ADICIONAL DE TAREAS                     */
/* ========================================================================== */

/**
 * @defgroup task_misc Otros parámetros de tareas
 * @{
 */

/** @brief Timeout de lectura UART para task_uart_rx [ms]
 *  
 *  Tiempo máximo que uart_read_bytes() bloquea esperando un carácter.
 *  
 *  100 ms permite:
 *  - Detectar línea incompleta en ~30s (timeout × 300)
 *  
 */
#define TASK_UART_RX_TIMEOUT 100

/** @} */ // end of task_misc

/* ========================================================================== */
/*                      TIMERS DE CONTROL Y PROTECCIÓN                        */
/* ========================================================================== */

/**
 * @defgroup control_timers Temporizadores del sistema de control de cargas
 * @{
 */

/** @brief Tiempo de espera antes de recuperar cargas tras falla de corriente [ms]
 *  
 *  5000 ms (5 segundos) permite:
 *  - Estabilización de corriente tras apagar cargas inductivas
 *  - Disipación de transitorios en la red
 *  - Evitar reconexión inmediata si falla persiste
 *  
 *  @see control_global_fsm() para uso en estado CONTROL_GLOBAL_REC
 */
#define CONTROL_REC_I_TIME_MS 5000

/** @brief Tiempo de espera antes de recuperar una carga tras falla de tensión [ms]
 *  
 *  3000 ms (3 segundos) para:
 *  - Verificar que tensión se estabilizó en rango válido
 *  - Evitar cycling rápido de relés (reduce vida útil)
 *  
 *  @note Más corto que CONTROL_REC_I_TIME_MS porque falla de V se considera menos crítica
 *  @see control_indiv_fsm() para uso en estado CONTROL_INDIV_OFF
 */
#define CONTROL_REC_V_TIME_MS 3000

/** @brief Ventana temporal para detección de fallas reiterativas [ms]
 *  
 *  10000 ms (10 segundos) define la ventana de "memoria" del sistema:
 *  
 *  - Si hay 2 fallas en <10s → Bloqueo permanente (CONTROL_GLOBAL_MAN_REC)
 *  - Si hay 2 fallas separadas por >10s → Se considera fallas independientes
 *  
 *  @see control_global_fsm() estado CONTROL_GLOBAL_OK para implementación
 */
#define CONTROL_REPET_I_RST_MS 10000

/** @} */ // end of control_timers

/* ========================================================================== */
/*                      CONFIGURACIÓN DE CARGAS                               */
/* ========================================================================== */

/**
 * @defgroup load_config Parámetros de las cargas controladas
 * @{
 */

/** @brief Número de cargas controlables por el sistema
 *  
 *  @warning Cambiar requiere:
 *           - Modificar hardware (pines GPIO, relés)
 *           - Ajustar estructuras de datos (load_cfg_t[], output[], etc.)
 *          
 */
#define NUM_LOADS 4

/** @brief Lógica de activación de cargas
 *  
 *  - 1: Activo en bajo (GPIO=0 → relé ON)
 *  - 0: Activo en alto (GPIO=1 → relé ON)
 *  
 *  Depende del diseño del driver de relés
 *  
 *  @note Configuración actual: active-low (optoacopladores)
 */
#define LOAD_ACTIVE_LOW 1

/** @} */ // end of load_config

/* ========================================================================== */
/*                      PARÁMETROS DE MEDICIÓN ADC                            */
/* ========================================================================== */

/**
 * @defgroup measurement_config Configuración del sistema de medición
 * 
 * ## Diseño del sistema de muestreo
 * 
 * El sistema usa ventanas de NUM_CYCLES_ACCUM ciclos completos para calcular
 * valores RMS estables.
 * 
 * @{
 */

/** @brief Frecuencia de muestreo ADC [Hz] */
#define SAMPLE_FREQ_HZ 20000

/** @brief Tamaño del frame DMA en bytes */
#define FRAME_BYTES 1024

/** @brief Frecuencia fundamental de la red eléctrica [Hz] */
#define FUND_FREQ_HZ 50

/** @brief Muestras (pares V-I) por ciclo de red   - 400 muestras por ciclo*/
#define PAIRS_PER_CYCLE (SAMPLE_FREQ_HZ / FUND_FREQ_HZ) 

/** @brief Número de ciclos de red acumulados por ventana de medición */
#define NUM_CYCLES_ACCUM 10

/** @brief Total de pares (V,I) acumulados por ventana - 4000*/
#define NUM_SAMPLES_ACCUM (PAIRS_PER_CYCLE * NUM_CYCLES_ACCUM)

/** @brief Tiempo de una ventana de medición [s] - 200ms */
#define TIME_SAMPLE_S (1.0f/SAMPLE_FREQ_HZ)*NUM_SAMPLES_ACCUM

/** @brief Tiempo de una ventana de medición [h] */
#define TIME_SAMPLE_H (TIME_SAMPLE_S / 3600.0f)

/** @} */ // end of measurement_config

/* ========================================================================== */
/*                      UMBRALES DE COMUNICACIÓN                              */
/* ========================================================================== */

/**
 * @defgroup comm_thresholds Umbrales para change detection en comunicaciones
 * 
 * Estos valores definen cuándo se considera que una variable cambió lo
 * suficiente como para justificar su actualización
 * 
 * @{
 */

/** @brief Umbral de cambio de tensión para actualización - 2V */
#define UPDATE_VOLT_THS 2.0f

/** @brief  Umbral de cambio de corriente para actualización - 0.2A */
#define UPDATE_CURR_THS 0.

/** @brief Umbral de cambio de factor de potencia para actualización*/
#define UPDATE_FP_THS 0.02f

/** @brief Intervalo mínimo entre envíos [ms] - máximo de dos actualizaciones por segundo */
#define UPDATE_MIN_INTERVAL_MS 500

/** @} */ // end of comm_thresholds

/* ========================================================================== */
/*                      UMBRALES DE PERSISTENCIA                              */
/* ========================================================================== */

/**
 * @defgroup storage_thresholds Umbrales para guardado automático en NVS
 * @{
 */

/** @brief Incremento de energía que dispara guardado automático [kWh]
 *  
 *  @see state_update_measure() para implementación de guardado automático
 *  
 */
#define SAVE_ENERGY_THS_KWH 1

/** @} */ // end of storage_thresholds

#endif // SYSTEM_CONFIG_H