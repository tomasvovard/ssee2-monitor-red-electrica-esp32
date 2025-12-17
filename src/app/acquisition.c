#include "app/acquisition.h"

void task_adc_acquisition(void *pvParameters){

    (void)pvParameters;

    // buffers static para evitar overflow de la task
    static uint8_t result[FRAME_BYTES];
    static measure_t measure_results;
    uint32_t ret_bytes = 0;

    int mv, v_mv;
    bool have_v = false; //flag de sincronización: true si hay V esperando a su par I

    while(1){

        esp_err_t ret = app_adc_dma_read(result, sizeof(result), &ret_bytes, portMAX_DELAY);

        if(ret == ESP_OK){

            const size_t step = sizeof(adc_digi_output_data_t);

            // Cada muestra ADC ocupa sizeof(adc_digi_output_data_t) bytes.
            // Si ret_bytes no es múltiplo exacto, hay corrupción de datos y se descarta el frame
            if(ret_bytes % step != 0) continue;

            for(size_t i = 0; i < ret_bytes; i += step){

                adc_digi_output_data_t *adc_digi_output_data = (adc_digi_output_data_t*)&result[i];

                uint32_t channel = adc_digi_output_data->type1.channel;
                uint32_t value = adc_digi_output_data->type1.data & 0x0FFF; //12bits

                // Chequeo valor dentro del rango del ADC (0 - 4095)
                if(value <= ADC_MAX_COUNT){
                    esp_err_t cal_ret = app_adc_get_voltage(value, &mv); 
                    if(cal_ret != ESP_OK){
                        // Si no convirtió bien el valor leído descarto todo el par V-I
                        have_v = false;
                        continue;
                    }
                } else {
                    // Valor fuera de rango - descarto este par V-I
                    have_v = false;
                    continue;
                }

                if (channel == ADC_CH_V) {
                    v_mv = mv;
                    have_v = true;
                } else if (channel == ADC_CH_I){
                    if(!have_v)continue; 
                    else {
                        if(measure_add_sample((int16_t)v_mv, (int16_t)mv)){
                            measure_get_results(&measure_results);
                            state_update_measure(&measure_results);
                            //measure_display_results(measure_results);                         
                        }
                        have_v = false;
                    }                 
                }
            }

        } else if (ret == ESP_ERR_TIMEOUT){
            // Timeout: No debería ocurrir con portMAX_DELAY, pero está por las dudas
            continue;
        } else if(ret == ESP_ERR_INVALID_STATE){
            // Overflow porque DMA escribió más rápido de lo que leímos = la tarea quedo bloqueada mucho tiempo
            ESP_LOGW("ADC", "Warning. Buffer Overflow");
            continue;
        }
    }
}