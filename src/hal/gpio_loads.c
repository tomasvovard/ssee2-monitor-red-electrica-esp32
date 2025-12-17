#include "gpio_loads.h"
#include "config/system_config.h"
#include "esp_log.h"

static const char *TAG = "GPIO_LOADS"; 

static const gpio_num_t load_gpio[NUM_LOADS] = {IO_LOAD_0, IO_LOAD_1, IO_LOAD_2, IO_LOAD_3};

static inline bool hw_to_logic(int hw_level){
#if LOAD_ACTIVE_LOW
    return (hw_level ==0);
#else
    return (hw_level == 1);
#endif
}

static inline int logic_to_hw(bool logic_level){
#if LOAD_ACTIVE_LOW
    return logic_level ? 0 : 1;
#else 
    return logic_level ? 1 : 0;
#endif
}

esp_err_t gpio_loads_init(){
    esp_err_t ret;
    gpio_config_t io_cfg = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = LOADS_OUT_MASK,
        .intr_type = GPIO_INTR_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };

    ret = gpio_config(&io_cfg);
    if(ret != ESP_OK){
        ESP_LOGE(TAG, "Error inicializando GPIO");
        return ret;
    }

    for(uint8_t i = 0; i < NUM_LOADS; i++){
        if(!gpio_load_update(i, false)){
            ESP_LOGW(TAG, "Carga %d no pudo setearse en init", i);
        }
    }
    return ESP_OK;
}

bool gpio_load_update(uint8_t id, bool level){
    if(id >= NUM_LOADS) return false;

    bool hw_level = logic_to_hw(level);
    uint8_t retries = 3;

    for(uint8_t i = 0; i < retries; i++){
        if(gpio_set_level(load_gpio[id], hw_level) != ESP_OK){
            ESP_LOGW(TAG, "Intento %d: Fallo escritura GPIO carga %d",i+1, id);
            continue;
        }
        return true;
    }   
    ESP_LOGE(TAG, "Fallo reiterado actualizando carga %d", id);
    return false;
}

void gpio_loads_get_state(bool *st){
    for(uint8_t i = 0; i < NUM_LOADS; i++){
        int ret = gpio_get_level(load_gpio[i]);
        if(ret < 0){
            ESP_LOGW(TAG, "Error recuperando el estado de carga %d", i);
            st[i] = false;
        } else {
            st[i] = hw_to_logic(ret);
        }
    }
}
