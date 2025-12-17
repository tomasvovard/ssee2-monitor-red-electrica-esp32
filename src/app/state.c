#include "state.h"
#include <string.h>

static state_t state;
static SemaphoreHandle_t state_mutex;
static double last_saved_E = 0.0;

void state_init(){
    state_mutex = xSemaphoreCreateMutex();
    configASSERT(state_mutex != NULL);
    memset(&state, 0, sizeof(state));
    state_set_energy();
}

void state_update_measure(const measure_t *m){
    bool should_save = false;

    xSemaphoreTake(state_mutex, portMAX_DELAY);
    state.measure.Vrms = m->Vrms;
    state.measure.VDC = m->VDC;
    state.measure.Vpk = m->Vpk;
    state.measure.Irms = m->Irms;
    state.measure.IDC = m->IDC;
    state.measure.Ipk = m->Ipk;
    state.measure.P = m->P;
    state.measure.S = m->S;
    state.measure.fp = m->fp;
    state.measure.E += m->E;

    double delta = state.measure.E - last_saved_E;
    if(delta >= SAVE_ENERGY_THS_KWH){
        should_save = true;
        last_saved_E = state.measure.E;
    }
    xSemaphoreGive(state_mutex);

    if(should_save){
        nvs_save_energy(last_saved_E);
        ESP_LOGI("STATE", "Energía guardada automáticamente: %.3f kWh", last_saved_E);
    }
}

void state_update_outputs(const bool *out){ 
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    memcpy(state.output, out, NUM_LOADS * sizeof(bool));
    xSemaphoreGive(state_mutex);
} 

void state_update_fails(const fail_t *fails){
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    state.fails = *fails;
    xSemaphoreGive(state_mutex);
} 

void state_get(state_t *out){
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    *out = state;
    xSemaphoreGive(state_mutex);
}

void state_reset_energy(){
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    state.measure.E = 0.0;
    nvs_save_energy(0.0);
    last_saved_E = 0.0;
    ESP_LOGI("STATE", "Energía reseteada");
    xSemaphoreGive(state_mutex);
}

void state_set_energy(){
    double energy = nvs_load_energy();
    
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    state.measure.E = energy;
    last_saved_E = energy;
    xSemaphoreGive(state_mutex);
}

void state_change_detector_init(change_detector_t *detector){
    memset(&detector->last_sent, 0, sizeof(state_t));
    detector->last_update_time = 0;
}

bool state_change_detector_update(change_detector_t *detector, const state_t *s, state_ths_t *ths){
    if(detector->last_update_time == 0){
        return true;
    }
    
    float di = fabs(s->measure.Irms - detector->last_sent.measure.Irms);
    float dv = fabs(s->measure.Vrms - detector->last_sent.measure.Vrms);
    float dp = fabs(fabs(s->measure.fp) - fabs(detector->last_sent.measure.fp));
    float de = fabs(s->measure.E - detector->last_sent.measure.E);
    bool is_val_change = (di > ths->i_ths) || (dv > ths->v_ths) || (dp > ths->fp_ths) || (de > ths->e_ths);

    bool is_load_change = false;
    bool is_fail_change = (s->fails.FAIL_I != detector->last_sent.fails.FAIL_I);
    for(uint8_t i = 0; i < NUM_LOADS; i++){
        is_load_change |= (s->output[i] != detector->last_sent.output[i]);
        is_fail_change |= (s->fails.FAIL_V[i] != detector->last_sent.fails.FAIL_V[i]);
    }

    uint32_t current_time = pdTICKS_TO_MS(xTaskGetTickCount());
    bool is_enough_time = (current_time - detector->last_update_time) >= ths->tmin_ms;

    return (is_val_change || is_load_change || is_fail_change) && is_enough_time;
}

void state_change_detector_mark_sent(change_detector_t *detector, const state_t *sent){
    detector->last_sent = *sent;
    detector->last_update_time = pdTICKS_TO_MS(xTaskGetTickCount());
}

