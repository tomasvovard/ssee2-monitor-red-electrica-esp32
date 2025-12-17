/*conttrol.c*/
#include "app/control.h"
#include "core/nvs_config.h"
#include "app/state.h"
#include "hal/gpio_loads.h"
#include <string.h>

static const char *TAG = "Control";

static ctrl_mode_t ctrl_mode;
static bool load_state[NUM_LOADS];
static sys_load_cfg_t s_cfg;
static uint8_t priority_index[NUM_LOADS];

static SemaphoreHandle_t control_mutex;

static bool imax_fail = false;
static bool v_fail[NUM_LOADS];
static bool imax_repetitive = false;
static control_global_fsm_t control_global_state;
static control_indiv_fsm_t control_state[NUM_LOADS]; 

static sys_timer_t timer_global_rec;
static sys_timer_t timer_cont_fails_i;
static sys_timer_t timer_load_rec[NUM_LOADS];

static void control_rebuild_priority_index(){
    // hay que llamarla si o si con el mutex tomado
    for(uint8_t i = 0; i < NUM_LOADS; i++){ //llenado por id
        priority_index[i] = i;
    }
    
    for(uint8_t i = 0; i < NUM_LOADS-1; i++){ //ordenamiento por prioridad
        for(uint8_t j = i+1; j < NUM_LOADS; j++){
            uint8_t id_i = priority_index[i];
            uint8_t id_j = priority_index[j];
            uint8_t pr_i = s_cfg.load[id_i].priority;
            uint8_t pr_j = s_cfg.load[id_j].priority;

            if( (pr_j < pr_i) || (pr_j == pr_i && id_j < id_i)){ //orden ascendente, si hay empate resuelve por id
                uint8_t tmp = priority_index[i];
                priority_index[i] = priority_index[j];
                priority_index[j] = tmp;
            }
        }
    }
}

void control_init(){

    control_mutex = xSemaphoreCreateMutex();
    configASSERT(control_mutex != NULL);

    control_reset();
}

void control_reset(){

    xSemaphoreTake(control_mutex,portMAX_DELAY);

    ctrl_mode = CTRL_MODE_AUTO;
    s_cfg.imax = DEFAULT_IMAX;

    timer_global_rec.active = false;

    imax_fail = false;
    imax_repetitive = false;

    for(uint8_t i = 0; i < NUM_LOADS; i++){
        s_cfg.load[i].v_min = DEFAULT_VMIN;
        s_cfg.load[i].v_max = DEFAULT_VMAX;
        s_cfg.load[i].auto_rec = DEFAULT_AUTO_REC;
        s_cfg.load[i].priority = i;

        priority_index[i] = i;
        load_state[i] = false;
        v_fail[i] = false;

        control_indiv_fsm_init(i);
    }
    control_global_fsm_init();

    xSemaphoreGive(control_mutex);
}

void control_set_mode(ctrl_mode_t mode){
    xSemaphoreTake(control_mutex,portMAX_DELAY);
    if(ctrl_mode == CTRL_MODE_MAN && mode == CTRL_MODE_AUTO){
        control_global_fsm_init();
        for(uint8_t i = 0; i < NUM_LOADS; i++){
            control_indiv_fsm_init(i);
        }
    }
    ctrl_mode = mode;
    xSemaphoreGive(control_mutex);
}

ctrl_mode_t control_get_mode(){
    xSemaphoreTake(control_mutex, portMAX_DELAY);
    ctrl_mode_t mode = ctrl_mode;
    xSemaphoreGive(control_mutex);
    return mode;
}

bool control_set_load_state(uint8_t id, bool on){
    if(id >= NUM_LOADS) return false;
    if(!gpio_load_update(id, on)){
        ESP_LOGE(TAG, "No se pudo actualziar la carga %d", id);
        return false;
    }
    xSemaphoreTake(control_mutex, portMAX_DELAY);
    load_state[id] = on;
    bool local[NUM_LOADS];
    memcpy(local, load_state, sizeof(load_state));
    xSemaphoreGive(control_mutex);
    state_update_outputs(local);
    return true;
}

bool control_get_load_state(uint8_t id, bool *on){
    if(id >= NUM_LOADS || on == NULL) return false;
    xSemaphoreTake(control_mutex, portMAX_DELAY);
    *on = load_state[id];
    xSemaphoreGive(control_mutex);
    return true;
}

bool control_get_cfg(sys_load_cfg_t *out){
    if(!out) return false;
    xSemaphoreTake(control_mutex, portMAX_DELAY);
    *out = s_cfg;
    xSemaphoreGive(control_mutex);
    return true;
}

bool control_set_load_vmin(uint8_t id, int16_t v_min){
    if(id >= NUM_LOADS) return false;
    xSemaphoreTake(control_mutex, portMAX_DELAY);
    s_cfg.load[id].v_min = v_min;
    xSemaphoreGive(control_mutex);
    return true;
}

bool control_set_load_vmax(uint8_t id, int16_t v_max){
    if(id >= NUM_LOADS) return false;
    xSemaphoreTake(control_mutex, portMAX_DELAY);
    s_cfg.load[id].v_max = v_max;
    xSemaphoreGive(control_mutex);
    return true;
}

bool control_set_load_auto_rec(uint8_t id, bool en){
    if(id >= NUM_LOADS) return false;
    xSemaphoreTake(control_mutex, portMAX_DELAY);
    s_cfg.load[id].auto_rec = en;
    xSemaphoreGive(control_mutex);
    return true;
}

bool control_set_load_priority(uint8_t id, uint8_t pr){
    if(id >= NUM_LOADS) return false;
    xSemaphoreTake(control_mutex, portMAX_DELAY);
    s_cfg.load[id].priority = pr;
    control_rebuild_priority_index();
    xSemaphoreGive(control_mutex);
    return true;
}

bool control_set_imax(float imax){
    xSemaphoreTake(control_mutex, portMAX_DELAY);
    s_cfg.imax = imax;
    xSemaphoreGive(control_mutex);
    return true;
}

int16_t control_get_v_min(uint8_t id){
    if(id >= NUM_LOADS) return -1;
    xSemaphoreTake(control_mutex, portMAX_DELAY);
    int16_t ret = s_cfg.load[id].v_min;
    xSemaphoreGive(control_mutex);
    return ret;
}

int16_t control_get_v_max(uint8_t id){
    if(id >= NUM_LOADS) return -1;
    xSemaphoreTake(control_mutex, portMAX_DELAY);
    int16_t ret = s_cfg.load[id].v_max;
    xSemaphoreGive(control_mutex);
    return ret;
}

bool control_save_to_nvs(){
    xSemaphoreTake(control_mutex, portMAX_DELAY);
    bool result = nvs_save_config(&s_cfg);
    xSemaphoreGive(control_mutex);
    return result;
}

bool control_load_from_nvs(){
    sys_load_cfg_t cfg;
    if(!nvs_load_config(&cfg)){
        return false;
    }
    xSemaphoreTake(control_mutex, portMAX_DELAY);
    s_cfg.imax = cfg.imax;
    for(uint8_t i = 0; i < NUM_LOADS; i++){
        s_cfg.load[i].v_min = cfg.load[i].v_min;
        s_cfg.load[i].v_max = cfg.load[i].v_max;
        s_cfg.load[i].auto_rec = cfg.load[i].auto_rec;
        s_cfg.load[i].priority = cfg.load[i].priority;
    }
    control_rebuild_priority_index();
    xSemaphoreGive(control_mutex);
    return true;
}

void control_global_fsm_init(){
    control_global_state = CONTROL_GLOBAL_OK;
    imax_repetitive = false;
}

void control_indiv_fsm_init(uint8_t id){
    if(load_state[id]) control_state[id] = CONTROL_INDIV_ON;
    else control_state[id] = CONTROL_INDIV_OFF;
    timer_load_rec[id].active = false;
    v_fail[id] = false;
}

bool control_global_fsm(float I){

    bool ret = false;
    static uint8_t cont_fails_i = 0;

    float imax_cut = s_cfg.imax;
    float imax_reset = imax_cut * (1.0f - IMAX_HYST_PRC/100.0f);

    static bool imax_ths = false;
    if(!imax_ths && I > imax_cut){
        imax_ths = true;
    } else if(imax_ths && I < imax_reset){
        imax_ths = false;
    }

    switch (control_global_state)
    {
    case CONTROL_GLOBAL_OK:
        ret = true;
        if(cont_fails_i != 0 && !timer_cont_fails_i.active){
            timer_start(&timer_cont_fails_i, CONTROL_REPET_I_RST_MS);
        }
        if(timer_expired(&timer_cont_fails_i)){
            timer_stop(&timer_cont_fails_i);
            cont_fails_i = 0;
        } 

        if(imax_ths){
            control_global_state = CONTROL_GLOBAL_FAIL_I;
            imax_fail = true;
            ret = false;
            cont_fails_i++;
            timer_stop(&timer_cont_fails_i);
        }
        break;

    case CONTROL_GLOBAL_FAIL_I:
        ret = false;
        if(!imax_ths) {
            imax_fail = false;
            if(cont_fails_i < MAX_FAIL_I){
                control_global_state = CONTROL_GLOBAL_REC;
                timer_start(&timer_global_rec, CONTROL_REC_I_TIME_MS);
                ret = false;
            } else {
                control_global_state = CONTROL_GLOBAL_MAN_REC;
                ret = false;
                imax_repetitive = true;
            }
        }
        break;
        
    case CONTROL_GLOBAL_REC:
        ret = false;
        imax_fail = false;
        if(imax_ths){
            timer_stop(&timer_global_rec);
            control_global_state = CONTROL_GLOBAL_FAIL_I;
            ret = false;
            cont_fails_i++;
            imax_fail = true;
        } else if(timer_expired(&timer_global_rec)){
            timer_stop(&timer_global_rec);
            control_global_state = CONTROL_GLOBAL_OK;
            ret = true;
        }
        break;
    
    case CONTROL_GLOBAL_MAN_REC:
        ret = false;
        imax_repetitive = true;
        cont_fails_i = 0;
        break;
    }

    return ret;
}

bool control_indiv_fsm(uint8_t id, int16_t vrms){ 

    bool ret = false;
    int16_t vmin = s_cfg.load[id].v_min;
    int16_t vmax = s_cfg.load[id].v_max;
    bool auto_rec = s_cfg.load[id].auto_rec;

    int16_t vmin_hyst = vmin >= 0 ? vmin * (1.0f - VRANGE_HYST_PRC/100.0f) : -1;
    int16_t vmax_hyst = vmax >= 0 ? vmax * (1.0f + VRANGE_HYST_PRC/100.0f) : -1;

    bool v_out_range;
    if(v_fail[id]){
        v_out_range = ((vrms < vmin_hyst) && (vmin >= 0)) || ((vrms > vmax_hyst) && (vmax >= 0));
    }
    else{
        v_out_range = ((vrms < vmin) && (vmin >= 0)) || ((vrms > vmax) && (vmax >= 0));
    }


    switch (control_state[id])
    {
    case CONTROL_INDIV_ON:
        ret = true;
        v_fail[id] = false;
        if(v_out_range){
            control_state[id] = CONTROL_INDIV_FAIL_V;
            ret = false;
            v_fail[id] = true;
        }
        break;

    case CONTROL_INDIV_OFF:
        v_fail[id] = false;
        ret = false;
        if(v_out_range){
            timer_stop(&timer_load_rec[id]);
            control_state[id] = CONTROL_INDIV_FAIL_V;
            v_fail[id] = true;
        } else if(auto_rec) {
            if(!timer_load_rec[id].active){
                timer_start(&timer_load_rec[id], CONTROL_REC_V_TIME_MS);
            } else if(timer_expired(&timer_load_rec[id])){
                timer_stop(&timer_load_rec[id]);
                control_state[id] = CONTROL_INDIV_ON;
                ret = true;
            }   
        }
        break;
    
    case CONTROL_INDIV_FAIL_V:
        ret = false;
        v_fail[id] = true;
        if(!v_out_range){
            control_state[id] = CONTROL_INDIV_OFF;
            if(auto_rec){
                timer_start(&timer_load_rec[id], CONTROL_REC_V_TIME_MS);
            }
            v_fail[id] = false;       
        }
        break;
    }

    return ret;
}

void task_control(void *pvParameters){

    (void)pvParameters;

    while(1){
        if(ctrl_mode == CTRL_MODE_AUTO){

            state_t st;
            state_get(&st);

            int16_t V = (int16_t) st.measure.Vrms;
            float I = (float) st.measure.Irms;

            xSemaphoreTake(control_mutex, portMAX_DELAY);

            bool ret_global = control_global_fsm(I); 

            uint8_t local_priority[NUM_LOADS]; //creo una copia local del índice para no tomar por demás el mutex
            memcpy(local_priority, priority_index, sizeof(priority_index));

            xSemaphoreGive(control_mutex);

            fail_t fails = {0};
            bool local_loads[NUM_LOADS] = {0};

            fails.FAIL_I = imax_repetitive? (I > s_cfg.imax) : imax_fail;
            fails.FAIL_I_NR = imax_repetitive;

            for (uint8_t k = 0; k < NUM_LOADS; k++){
                uint8_t l = local_priority[k];

                xSemaphoreTake(control_mutex, portMAX_DELAY);
                bool ret_indiv = control_indiv_fsm(l, V);
                xSemaphoreGive(control_mutex);
                
                if(!gpio_load_update(l, ret_global && ret_indiv)){
                    ESP_LOGE(TAG, "No se pudo actualizar la carga %d", l);
                    xSemaphoreTake(control_mutex, portMAX_DELAY);
                    local_loads[l] = load_state[l];
                    fails.FAIL_V[l] = v_fail[l];
                    xSemaphoreGive(control_mutex);
                    continue;
                }

                xSemaphoreTake(control_mutex, portMAX_DELAY);
                load_state[l] = ret_global && ret_indiv;
                local_loads[l] = load_state[l];
                fails.FAIL_V[l] = v_fail[l];
                xSemaphoreGive(control_mutex);
            }
            
            state_update_fails(&fails);
            state_update_outputs(local_loads);
        }

        // static int16_t integ_count = 500;
        // if(integ_count <= 0){
        //     integ_count = 500;
        //     control_check_outputs_integrity();
        // } else {
        //     integ_count--;
        // }

        vTaskDelay(pdMS_TO_TICKS(TASK_PERIOD_CONTROL_MS));
    }
}

void control_check_outputs_integrity(){
    bool hw_state[NUM_LOADS];
    gpio_loads_get_state(hw_state);

    bool local_loads[NUM_LOADS];
    xSemaphoreTake(control_mutex, portMAX_DELAY);
    for(uint8_t i = 0; i < NUM_LOADS; i++){
        local_loads[i] = load_state[i];
    }
    xSemaphoreGive(control_mutex);

    for(uint8_t i = 0; i < NUM_LOADS; i++){
            if(hw_state[i] != local_loads[i]){
                ESP_LOGW(TAG, "Desincronización en carga %d", i);
            }
            if(!gpio_load_update(i, local_loads[i])){
                ESP_LOGE(TAG, "No se pudo resincronizar carga %d", i);
            }
    }
}
