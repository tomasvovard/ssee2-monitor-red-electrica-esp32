/**
 * @file adc_dma.h
 * @brief HAL del ADC con DMA para adquisición continua de tensión y corriente
 * 
 * Wrapper sobre ESP-IDF ADC continuous mode para muestreo sincronizado dual
 * a 20 kHz con transferencia DMA.
 * 
 * Canales utilizados:
 * - ADC1_CH4 (GPIO32): Tensión de red (divisor resistivo)
 * - ADC1_CH6 (GPIO34): Corriente (sensor ACS712-5A)
 * 
 * @author Tomás Vovard
 * @date Diciembre 2025
 */

#ifndef ADC_DMA_H
#define ADC_DMA_H

#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "system_config.h"

/* ========================================================================== */
/*                      CONFIGURACIÓN DE HARDWARE                             */
/* ========================================================================== */

/** @brief Unidad ADC utilizada (ADC1 del ESP32) */
#define ADC_UNIT ADC_UNIT_1

/** @brief Canal ADC para medición de tensión - GPIO32 */
#define ADC_CH_V ADC_CHANNEL_4

/** @brief Canal ADC para medición de corriente - GPIO34 */
#define ADC_CH_I ADC_CHANNEL_6

/** @brief Atenuación del ADC: 11 dB para rango 0-3.3V */
#define ADC_ATTEN_CFG ADC_ATTEN_DB_12

/** @brief Resolución del ADC: 12 bits (0-4095) */
#define ADC_BITWIDTH ADC_BITWIDTH_12

/** @brief Valor máximo de cuenta ADC de 12 bits */
#define ADC_MAX_COUNT 4095

/* ========================================================================== */
/*                      FUNCIONES PÚBLICAS                                    */
/* ========================================================================== */

/**
 * @brief Inicializa ADC en modo continuo con DMA
 * 
 * Configura:
 * - Patrón de conversión dual-canal (V, I)
 * - Frecuencia de muestreo: SAMPLE_FREQ_HZ
 * - Buffer circular DMA
 * 
 * @note Debe llamarse antes de app_adc_dma_start_conv()
 */
void app_adc_dma_init();

/**
 * @brief Inicia conversiones continuas ADC
 * 
 * @note Llamar una sola vez después de app_adc_dma_init()
 */
void app_adc_dma_start_conv();

/**
 * @brief Lee datos del buffer DMA (bloqueante)
 * 
 * @param[out] buf Buffer donde copiar los datos
 * @param len Tamaño del buffer en bytes (típicamente FRAME_BYTES)
 * @param[out] out_bytes Bytes realmente leídos
 * @param tout Timeout máximo de espera
 * 
 * @return ESP_OK si exitoso, ESP_ERR_TIMEOUT si timeout, ESP_ERR_INVALID_STATE si overflow
 * 
 * @note Bloquea hasta que haya len bytes disponibles o expire tout
 */
esp_err_t app_adc_dma_read(uint8_t *buf, size_t len, uint32_t *out_bytes, TickType_t tout);

/**
 * @brief Convierte cuenta ADC raw a milivoltios calibrados
 * 
 * @param raw Valor ADC (0-4095)
 * @param[out] mv Tensión en milivoltios
 * 
 * @return ESP_OK si exitoso, ESP_FAIL si no hay calibración
 * 
 * @note Requiere app_adc_init_calibration() previo
 */
esp_err_t app_adc_get_voltage(int raw, int *mv);

/**
 * @brief Inicializa calibración del ADC
 * 
 * Crea esquema de calibración line fitting para compensar
 * no-linealidad del ADC del ESP32.
 * 
 * @return true si exitoso, false en caso de error
 * 
 * @note Llamar antes de usar app_adc_get_voltage()
 */
bool app_adc_init_calibration();

#endif  // ADC_DMA_H