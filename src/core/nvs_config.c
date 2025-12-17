/*nvs_config.c*/
#include "core/nvs_config.h"
#include <string.h>

static const char *TAG = "NVS_CFG";
static bool nvs_is_initialized = false;

void nvs_config_init(){
    esp_err_t ret = nvs_flash_init();

    if(ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND){
        ESP_LOGW(TAG, "Reseteando NVS...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS inicializado");
    nvs_is_initialized = true;
}

bool nvs_save_config(const sys_load_cfg_t *cfg){
    nvs_handle_t handle;
    esp_err_t err;
    bool is_error = false;

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if(err != ESP_OK){
        ESP_LOGE(TAG, "Error abriendo NVS: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_blob(handle, "imax", &cfg->imax, sizeof(float));
    if(err != ESP_OK) is_error = true;

    if(!is_error){
        for(uint8_t i = 0; i < NUM_LOADS; i++){
            char buf[16];

            snprintf(buf, sizeof(buf), "vmin_%d", i);
            err = nvs_set_i16(handle, buf, cfg->load[i].v_min);
            if(err != ESP_OK){
                is_error = true;
                break;
            } 

            snprintf(buf, sizeof(buf), "vmax_%d", i);
            err = nvs_set_i16(handle, buf, cfg->load[i].v_max);
            if(err != ESP_OK){
                is_error = true;
                break;
            } 

            snprintf(buf, sizeof(buf), "autorec_%d", i);
            err = nvs_set_u8(handle, buf, cfg->load[i].auto_rec? 1 : 0);
            if(err != ESP_OK){
                is_error = true;
                break;
            } 

            snprintf(buf, sizeof(buf), "priority_%d", i);
            err = nvs_set_u8(handle, buf, cfg->load[i].priority);
            if(err != ESP_OK){
                is_error = true;
                break;
            } 
        }
    }
    if(is_error){
        ESP_LOGE(TAG, "Error guardando config: %s", esp_err_to_name(err));
        nvs_close(handle);
        return false;
    }

    err = nvs_commit(handle);
    nvs_close(handle);    
    if(err == ESP_OK){
        ESP_LOGI(TAG, "Config guardada");
        return true;
    }
    return false;
}

bool nvs_load_config(sys_load_cfg_t *cfg){
    nvs_handle_t handle;
    esp_err_t err;
    bool is_error = false;

    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if(err != ESP_OK){
        ESP_LOGW(TAG, "No hay config guardada");
        return false;
    }

    size_t req_size = sizeof(float);
    err = nvs_get_blob(handle, "imax", &cfg->imax, &req_size);
    if(err != ESP_OK) is_error = true;

    if(!is_error){
        for(uint8_t i = 0; i < NUM_LOADS; i++){
            char buf[16];
            int16_t val16;
            uint8_t val8;

            snprintf(buf, sizeof(buf), "vmin_%d", i);
            err = nvs_get_i16(handle, buf, &val16);
            if(err != ESP_OK){
                is_error = true;
                break;
            }
            cfg->load[i].v_min = val16;

            snprintf(buf, sizeof(buf), "vmax_%d", i);
            err = nvs_get_i16(handle, buf, &val16);
            if(err != ESP_OK){
                is_error = true;
                break;
            }
            cfg->load[i].v_max = val16;

            snprintf(buf, sizeof(buf), "autorec_%d", i);
            err = nvs_get_u8(handle, buf, &val8);
            if(err != ESP_OK){
                is_error = true;
                break;
            } 
            cfg->load[i].auto_rec = (val8 != 0);

            snprintf(buf, sizeof(buf), "priority_%d", i);
            err = nvs_get_u8(handle, buf, &val8);
            if(err != ESP_OK){
                is_error = true;
                break;
            }
            cfg->load[i].priority = val8;
        }
    }
    if(is_error){
        ESP_LOGE(TAG, "Error leyendo config: %s", esp_err_to_name(err));
        nvs_close(handle);
        return false;
    }

    nvs_close(handle);
    ESP_LOGI(TAG, "Config cargada");
    return true;
}

bool nvs_save_energy(double energy){
    nvs_handle_t handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if( err != ESP_OK) return false;

    err = nvs_set_blob(handle, "energy", &energy, sizeof(double));
    if(err == ESP_OK){
        err = nvs_commit(handle);
    }

    nvs_close(handle);

    if(err == ESP_OK){
        ESP_LOGI(TAG, "Energia guardada: %.3f KWh", energy);
        return true;
    }
    return false;
}

double nvs_load_energy(){
    nvs_handle_t handle;
    esp_err_t err;
    double energy = 0.0;

    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if( err != ESP_OK) return 0.0;

    size_t req_size = sizeof(double);
    err = nvs_get_blob(handle, "energy", &energy, &req_size);

    nvs_close(handle);

    if(err == ESP_OK){
        ESP_LOGI(TAG, "Energia cargada: %.3f KWh", energy);
        return energy;
    }

    return 0.0;
}

bool nvs_reset_default(){
    nvs_handle_t handle;
    esp_err_t err;
    
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return false;
    
    err = nvs_erase_all(handle);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    
    nvs_close(handle);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Config reseteada a defaults");
        return true;
    }
    return false;
}

bool nvs_is_init(){
    return nvs_is_initialized; 
}