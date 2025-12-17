/**
 * @file acquisition.h
 * @brief Tarea de adquisición continua de muestras ADC para medición eléctrica
 * 
 * Este módulo implementa la tarea de más alta prioridad del sistema, responsable
 * de la captura sincronizada y continua de muestras de tensión y corriente mediante
 * ADC con DMA.
 * 
 * ## Arquitectura de adquisición
 * 
 * ```
 * Hardware ADC → DMA Buffer → task_adc_acquisition → measure_add_sample()
 *                                                            ↓
 *                                              [Ventana completa cada 10 ciclos]
 *                                                            ↓
 *                                                 measure_get_results()
 *                                                            ↓
 *                                                  state_update_measure()
 * ```
 * 
 * ## Flujo de procesamiento
 * 
 * 1. **Espera bloqueante**: app_adc_dma_read() espera llenado de buffer DMA
 * 2. **Validación**: Verifica integridad de muestras (rango ADC, calibración)
 * 3. **Sincronización V-I**: Empareja muestras de tensión y corriente
 * 4. **Almacenamiento**: Llama measure_add_sample() por cada par válido
 * 5. **Publicación**: Al completar ventana, actualiza state con resultados
 * 
 * ## Características de tiempo real
 * 
 * - **Frecuencia de muestreo**: 20 kHz (configurable en system_config.h)
 * - **Período de muestra**: 50 μs (sincronismo crítico)
 * - **Latencia**: desde DMA ready hasta measure_add_sample()
 * 
 * ## Configuración de la tarea
 * 
 * Parámetros definidos en system_config.h:
 * - Prioridad: TASK_PRIORITY_ADC_ACQ (la más alta)
 * - Stack: TASK_STACK_ADC_ACQ
 * - Sin periodicidad fija: bloqueante en DMA, ejecuta inmediatamente al tener datos
 * 
 * @note Esta tarea NO debe ser bloqueada por otras - el timing es crítico
 * @warning Reducir su prioridad causa pérdida de sincronismo y errores de medición
 * 
 * @see measure.h para el algoritmo de cálculo de magnitudes eléctricas
 * @see adc_dma.h para la configuración del hardware ADC
 * 
 * @author Tomás Vovard
 * @date Diciembre 2025
 */

#ifndef ACQUISITION_H
#define ACQUISITION_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app/measure.h"
#include "hal/adc_dma.h"
#include "app/state.h"

/**
 * @brief Tarea de adquisición continua ADC con DMA
 * 
 * Responsabilidades:
 * 
 * ### 1. Lectura de buffer DMA
 * Bloquea en app_adc_dma_read() esperando FRAME_BYTES bytes de datos ADC
 * (típicamente 1024 bytes = 512 muestras intercaladas V-I).
 * 
 * ### 2. Desempaquetado y validación
 * - Extrae muestras individuales del formato adc_digi_output_data_t
 * - Verifica que valores estén en rango ADC (0-4095 para 12 bits)
 * - Descarta muestras corruptas o fuera de rango
 * 
 * ### 3. Calibración por hardware
 * Aplica corrección ADC mediante app_adc_get_voltage():
 * - Conversión de cuentas ADC a milivoltios
 * - Corrección de no-linealidad del ADC del ESP32
 * - Compensación de offset y ganancia
 * 
 * ### 4. Sincronización V-I
 * Empareja muestras de tensión y corriente en orden estricto:
 * - Canal V (ADC_CH_V) → guarda como v_mv
 * - Canal I (ADC_CH_I) → forma par (v_mv, i_mv) y envía a measure_add_sample()
 * 
 * Si la secuencia se rompe (ej: dos muestras de V consecutivas sin I),
 * descarta la muestra huérfana y resincronizan.
 * 
 * ### 5. Actualización del estado global
 * Cada NUM_SAMPLES_ACCUM pares (~4000 muestras = 10 ciclos @ 50Hz):
 * - measure_add_sample() retorna true
 * - Obtiene resultados con measure_get_results()
 * - Publica en estado global con state_update_measure()
 * 
 * ## Manejo de errores
 * 
 * | Error | Acción | Impacto |
 * |-------|--------|---------|
 * | ESP_ERR_TIMEOUT | Continue loop | Reinicia espera DMA |
 * | ESP_ERR_INVALID_STATE | Log warning + continue | Buffer overflow - datos perdidos |
 * | Calibración fallida | Descarta par V-I | Pierde 1 muestra de ~4000 |
 * | Valor ADC > 4095 | Descarta par V-I | Pierde 1 muestra de ~4000 |
 * 
 * @param pvParameters Parámetro estándar de FreeRTOS (no usado)
 * 
 * @warning Si esta tarea se bloquea >50ms, el buffer DMA hace overflow
 * @warning Prioridad debe ser TASK_PRIORITY_ADC_ACQ (la más alta del sistema)
 * @warning No llamar vTaskDelay() dentro de esta tarea - afecta sincronismo
 * 
 * @see system_config.h para configuración de prioridades y stack
 * @see measure_add_sample() para procesamiento de cada par (V,I)
 * @see app_adc_dma_read() para lectura bloqueante del buffer DMA
 */
void task_adc_acquisition(void *pvParameters);

#endif // ACQUISITION_H