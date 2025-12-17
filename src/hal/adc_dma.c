#include "hal/adc_dma.h"

static adc_continuous_handle_t s_adc_handle;
static adc_cali_handle_t adc1_cali_handle = NULL;

void app_adc_dma_init(){

    esp_err_t ret;

    /*creo el handler*/
    adc_continuous_handle_cfg_t handle_cfg = { 
        .max_store_buf_size = 1024, //ring buffer de hasta 1024 bytes
        .conv_frame_size = FRAME_BYTES, //leo esta cantidad de bytes
    };
    ret = adc_continuous_new_handle(&handle_cfg, &s_adc_handle);
    ESP_ERROR_CHECK(ret);
    /* Si no llamo a adc_continuous_read con suficiente frecuencia, el buffer circular puede sobreescribirse
    y se pierden datos. Flujo temporal deja de ser continuo. 
    Si pasa podemos bajar la SAMPLE_FREQ, aumentar el storage del buffer, aumentar la prioridad de la task de adquisición
    */

    /*creo el patrón de conversión*/
    adc_digi_pattern_config_t pattern[2] = {0};

    /*canal voltaje*/
    pattern[0].atten = ADC_ATTEN_CFG;
    pattern[0].bit_width = ADC_BITWIDTH;
    pattern[0].channel = ADC_CH_V;
    pattern[0].unit = ADC_UNIT;

    /*canal corriente*/
    pattern[1].atten = ADC_ATTEN_CFG;
    pattern[1].bit_width = ADC_BITWIDTH;
    pattern[1].channel = ADC_CH_I;
    pattern[1].unit = ADC_UNIT;

    adc_continuous_config_t dig_cfg = {
        .sample_freq_hz = SAMPLE_FREQ_HZ,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE1,
        .pattern_num = 2,
        .adc_pattern = pattern,
    };

    ret = adc_continuous_config(s_adc_handle, &dig_cfg);
    ESP_ERROR_CHECK(ret);
}

void app_adc_dma_start_conv(){
    esp_err_t ret;
    ret = adc_continuous_start(s_adc_handle);
    ESP_ERROR_CHECK(ret);
}

esp_err_t app_adc_dma_read(uint8_t *buf, size_t len, uint32_t *out_bytes, TickType_t tout){
    return adc_continuous_read(s_adc_handle, buf, len, out_bytes, tout);
}

esp_err_t app_adc_get_voltage(int raw, int *mv){
    return adc_cali_raw_to_voltage(adc1_cali_handle, raw, mv);
}

bool app_adc_init_calibration(){
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT,
        .atten = ADC_ATTEN_CFG,
        .bitwidth = ADC_BITWIDTH,
    };
    return (ESP_OK == adc_cali_create_scheme_line_fitting(&cali_config, &adc1_cali_handle));
}