/**
 * @file measure.h
 * @brief Sistema de medición de parámetros eléctricos RMS con cálculo de potencia
 * 
 * Este módulo implementa el algoritmo de cálculo de magnitudes eléctricas a partir
 * de muestras ADC sincronizadas de tensión y corriente. Utiliza ventanas de
 * NUM_SAMPLES_ACCUM muestras (típicamente 10 ciclos @ 50Hz = 4000 muestras) para
 * obtener valores RMS estables.
 * 
 * ## Algoritmo de medición
 * 
 * 1. **Adquisición**: Muestras simultáneas V-I a 20 kHz (400 muestras/ciclo @ 50Hz)
 * 2. **Acumulación**: 10 ciclos completos = 4000 pares (V,I)
 * 3. **Cálculo RMS**: √(Σ(v²)/N) para tensión y corriente
 * 4. **Potencia activa**: P = Σ(v·i)/N
 * 5. **Potencia aparente**: S = Vrms × Irms
 * 6. **Factor de potencia**: fp = P/S
 * 7. **Energía**: E = P × Δt
 * 
 * ## Calibración de hardware
 * 
 * ### Canal de corriente (ACS712-5A)
 * - Sensor Hall bidireccional: ±5A → 0-5V (centrado en 2.5V)
 * - Sensibilidad nominal: 185 mV/A
 * - Correcciones aplicadas:
 *   * Offset DC (ruido)
 *   * Ganancia del sensor: 0.83× (calibración experimental)
 *   * Ruido de fondo
 * 
 * ### Canal de tensión (divisor resistivo)
 * - Relación: 220V AC → ~890 mV AC 
 * - Ganancia calibrada: -4.05 mV/V (signo por inversión de fase)
 * - Ruido de fondo
 * 
 * @note La precisión mejora significativamente con señales de amplitud grande
 * @warning Con tensiones <50V las mediciones de fp no son confiables
 * 
 * @author Tomás Vovard
 * @date Diciembre 2025
 */

#ifndef MEASURE_H
#define MEASURE_H

#include <stdio.h>
#include <math.h>
#include "config/system_config.h"

/* ========================================================================== */
/*                      CALIBRACIÓN DEL HARDWARE                              */
/* ========================================================================== */

/**
 * @defgroup measure_calibration Constantes de calibración de sensores
 * @{
 */

/** @brief Sensibilidad del sensor ACS712-5A [V/A]
 * 
 * Especificación del fabricante: 185 mV/A para modelo de ±5A
 * 
 * Relación: Vout = 2.5V + (I × 0.185)
 * - I = 0A → Vout = 2.5V (punto medio)
 * - I = +5A → Vout = 3.425V -> el ADC satura en 3.3V así que la Imax leída sería menor
 * - I = -5A → Vout = 1.575V
 * 
 * @note Esta es la sensibilidad nominal - luego se calibra
 */
#define ACS712_5A_SENSITIVITY 0.185f //185mV/A

/** @brief Nivel de ruido del ACS712 [V]
 * 
 */
#define ACS712_GROUNDNOISE 0.15f

/** @brief Offset DC del ACS712 respecto al punto ideal 2.5V [V]
 * 
 * Se resta este valor antes de calcular la corriente.
 */
#define ACS712_OFFSET 0.05f

/** @brief Factor de corrección de ganancia del ACS712 [adimensional]
 * 
 * Corrección experimental determinada mediante calibración con carga
 * conocida y pinza amperimétrica de referencia.
 * 
 * @note Valor específico para la unidad de hardware usada - puede variar
 */
#define ACS712_GAIN_CORR 0.83

/** @brief Ganancia del atenuador diferencial de tensión [V/V]
 * 
 * Relación medida experimentalmente entre tensión de línea (220V AC RMS)
 * y tensión en el ADC del ESP32.
 * 
 * @note El signo negativo indica inversión de fase por el divisor
 * @warning Este valor depende de la relación exacta del divisor resistivo
 */
#define VOLT_DRIVER_GAIN -4.05e-3

/** @brief Ruido de fondo del canal de tensión
 *
 * Sirve para filtrar lecturas espurias cuando no hay tensión aplicada.
 */
#define VOLT_DRIVER_GROUNDNOISE 114

/** @} */ // end of measure_calibration

/* ========================================================================== */
/*                      ESTRUCTURAS DE DATOS                                  */
/* ========================================================================== */

/**
 * @brief Resultados de una medición eléctrica completa
 * 
 * Contiene todas las magnitudes calculadas a partir de NUM_SAMPLES_ACCUM
 * muestras ADC. Esta estructura se actualiza cada ~200ms (10 ciclos @ 50Hz).
 * 
 * ## Interpretación de valores:
 * 
 * ### Tensión
 * - Vrms: Valor eficaz [V] - usado para protecciones Vmin/Vmax
 * - VDC: Componente continua leída en el ADC, se usa para debuggin solamente, no es un dato a mostrar
 * - Vpk: Valor de pico [V] - Vpk ≈ Vrms × √2 para sinusoidal
 * 
 * ### Corriente
 * - Irms: Valor eficaz [A] - usado para protección Imax
 * - IDC: Componente continua leída en el ADC, se usa para debuggin solamente, no es un dato a mostrar
 * - Ipk: Valor de pico [A] - importante para dimensionar relés
 * 
 * ### Potencia
 * - P: Activa [W] - potencia real consumida/generada
 * - S: Aparente [VA] - producto Vrms × Irms
 * - fp: Factor de potencia
 * 
 * ### Energía
 * - E: Energía incremental [kWh] - suma cada ventana de medición
 *   * E = P × TIME_SAMPLE_H donde TIME_SAMPLE_H ≈ 0.000056h (200ms)
 *   * Esta E se ACUMULA en state.measure.E para obtener consumo total
 * 
 * @note Todas las unidades son del SI (V, A, W, VA, kWh)
 * @warning Los valores de pico (Vpk, Ipk) son aproximados (asumen sinusoidal)
 */
typedef struct 
{
    float Vrms;
    float VDC;
    float Vpk;
    float Irms;
    float IDC;
    float Ipk;
    float P;
    float S;
    float fp;
    float E;
} measure_t;

/* ========================================================================== */
/*                      FUNCIONES PÚBLICAS                                    */
/* ========================================================================== */

/**
 * @defgroup measure_api API de medición
 * @{
 */

/**
 * @brief Agrega un par sincronizado (tensión, corriente) al buffer de muestras
 * 
 * Acumula muestras ADC calibradas hasta completar NUM_SAMPLES_ACCUM pares.
 * Cuando se completa una ventana, dispara el cálculo de
 * todas las magnitudes eléctricas.
 * 
 * @param v_mv Muestra de tensión calibrada en milivoltios [mV]
 * @param i_mv Muestra de corriente calibrada en milivoltios [mV]
 * 
 * @return true si se completó una ventana (resultados listos), false en caso contrario
 * 
 * @note Thread-safe si se llama desde una única tarea (típicamente task_adc_acquisition)
 * @note Las muestras deben estar pre-calibradas (offset y ganancia aplicados)
 * @note Frecuencia de llamada típica: 20 kHz (cada 50 μs)
 * 
 * @warning No verificar NULL en v_mv/i_mv por razones de performance
 * @warning Si no se llama con frecuencia constante, el cálculo de energía será inexacto
 * 
 * @see measure_get_results() para obtener los cálculos realizados
 */
bool measure_add_sample(int16_t v_mv, int16_t i_mv);

/**
 * @brief Obtiene los resultados de la última ventana de medición completa
 * 
 * Copia la estructura measure_t calculada por la última invocación exitosa
 * de measure_add_sample() que retornó true.
 * 
 * @param[out] out Puntero a estructura donde copiar los resultados
 * 
 * @note No requiere thread-safety si se llama desde la misma tarea que measure_add_sample()
 * @note Resultados disponibles cada ~200ms (10 ciclos @ 50Hz)
 * 
 * @warning Los resultados no cambian hasta el próximo retorno true de measure_add_sample()
 * 
 */
void measure_get_results(measure_t *out);

/**
 * @brief Imprime resultados de medición en consola serial (debug)
 * 
 * Formatea y envía por puerto de debug un resumen legible de
 * todas las magnitudes medidas.
 * 
 * @param results Estructura con los resultados a imprimir
 * 
 * @note Uso exclusivo para debug
 * @note No verifica validez de los datos - imprime lo recibido directamente
 * 
 */
void measure_display_results(measure_t results);

/** @} */ // end of measure_api

#endif // MEASURE_H