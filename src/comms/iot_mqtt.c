#include "comms/iot_mqtt.h"
#include "app/control.h"
#include "app/state.h"
#include "core/nvs_config.h"
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "IOT_MQTT";

static esp_mqtt_client_handle_t mqtt_client = NULL;
static QueueHandle_t iot_cmd_queue = NULL;

static bool last_fail_i = false;
static bool last_fail_i_nr = false;
static bool last_fail_v[NUM_LOADS] = {0};

static void iot_publish_event(const char *name, cJSON *extra){
    cJSON *root = cJSON_CreateObject();
    if(!root) return;

    cJSON_AddStringToObject(root, "event", name);

    if(extra){
        cJSON_AddItemToObject(root, "data", extra);
    }

    char *json = cJSON_PrintUnformatted(root);
    if(json){
        esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC_EVT, json, 0, 1, 0);
        cJSON_free(json);
    }
    cJSON_Delete(root);
}

static bool iot_parse_cmd_json(const char *payload, int len, iot_cmd_t *out_cmd){
    if(!payload || !out_cmd) return false;
    if(len <= 0 || len >= IOT_CMD_JSON_MAX_LEN){
        ESP_LOGW(TAG, "CMD JSON muy largo (%d), descartado", len);
        return false;
    }

    char buf[IOT_CMD_JSON_MAX_LEN];
    memcpy(buf, payload, len);
    buf[len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if(!root){
        ESP_LOGW(TAG, "cJSON_Parse fallido");
        return false;
    }

    cJSON *cmd = cJSON_GetObjectItem(root, "cmd");
    if(!cJSON_IsString(cmd)){
        cJSON_Delete(root);
        return false;
    }

    memset(out_cmd, 0, sizeof(*out_cmd));

    if(strcmp(cmd->valuestring, "MODE_SET") == 0){
        cJSON *mode = cJSON_GetObjectItem(root, "mode");
        if(!cJSON_IsString(mode)){
            cJSON_Delete(root);
            return false;
        }

        out_cmd->type = IOT_CMD_MODE_SET;
        out_cmd->mode_set.manual = (strcmp(mode->valuestring, "MANUAL") == 0);
    }
    else if(strcmp(cmd->valuestring, "LOAD_SET") == 0){
        cJSON *id = cJSON_GetObjectItem(root, "id");
        cJSON *state = cJSON_GetObjectItem(root, "state");
        if(!cJSON_IsNumber(id) || !cJSON_IsString(state)){
            cJSON_Delete(root);
            return false;
        }
        out_cmd->type = IOT_CMD_LOAD_SET;
        out_cmd->load_set.id = (uint8_t)id->valuedouble;
        out_cmd->load_set.on = (strcmp(state->valuestring,"ON") == 0);
    }
    else if(strcmp(cmd->valuestring, "ENERGY_RESET") == 0){
        out_cmd->type = IOT_CMD_ENERGY_RESET;
    }
    else if (strcmp(cmd->valuestring, "CFG_IMAX_SET") == 0) {
        cJSON *val = cJSON_GetObjectItem(root, "value");
        if (!cJSON_IsNumber(val)) {
            cJSON_Delete(root);
            return false;
        }
        out_cmd->type = IOT_CMD_CFG_IMAX_SET;
        out_cmd->cfg_imax_set.imax = (float) val->valuedouble;
    }
    else if (strcmp(cmd->valuestring, "CFG_VRANGE_SET") == 0) {
        cJSON *id  = cJSON_GetObjectItem(root, "id");
        cJSON *vmin = cJSON_GetObjectItem(root, "vmin");
        cJSON *vmax = cJSON_GetObjectItem(root, "vmax");
        if (!cJSON_IsNumber(id) || !cJSON_IsNumber(vmin) || !cJSON_IsNumber(vmax)) {
            cJSON_Delete(root);
            return false;
        }
        out_cmd->type = IOT_CMD_CFG_VRANGE_SET;
        out_cmd->cfg_vrange_set.id   = (uint8_t) id->valuedouble;
        out_cmd->cfg_vrange_set.vmin = (int16_t) vmin->valuedouble;
        out_cmd->cfg_vrange_set.vmax = (int16_t) vmax->valuedouble;
    }
    else if (strcmp(cmd->valuestring, "CFG_AUTOREC_SET") == 0) {
        cJSON *id = cJSON_GetObjectItem(root, "id");
        cJSON *en = cJSON_GetObjectItem(root, "enabled");
        if (!cJSON_IsNumber(id) || !cJSON_IsBool(en)) {
            cJSON_Delete(root);
            return false;
        }
        out_cmd->type = IOT_CMD_CFG_AUTOREC_SET;
        out_cmd->cfg_autorec_set.id = (uint8_t) id->valuedouble;
        out_cmd->cfg_autorec_set.ena = cJSON_IsTrue(en);
    }
    else if (strcmp(cmd->valuestring, "CFG_PRIORITY_SET") == 0) {
        cJSON *id  = cJSON_GetObjectItem(root, "id");
        cJSON *val = cJSON_GetObjectItem(root, "value");
        if (!cJSON_IsNumber(id) || !cJSON_IsNumber(val)) {
            cJSON_Delete(root);
            return false;
        }
        out_cmd->type = IOT_CMD_CFG_PRIORITY_SET;
        out_cmd->cfg_priority_set.id   = (uint8_t)id->valuedouble;
        out_cmd->cfg_priority_set.pr = (uint8_t)val->valuedouble;
    }
    else {
        cJSON_Delete(root);
        return false;
    }
     cJSON_Delete(root);
     return true;
}

static void iot_publish_telemetry(const state_t *st){

    cJSON *root = cJSON_CreateObject();
    if(!root) return;

    cJSON_AddNumberToObject(root, "V", st->measure.Vrms);
    cJSON_AddNumberToObject(root, "I", st->measure.Irms);
    cJSON_AddNumberToObject(root, "P",  st->measure.P);
    cJSON_AddNumberToObject(root, "S",  st->measure.S);
    cJSON_AddNumberToObject(root, "fp", st->measure.fp);
    cJSON_AddNumberToObject(root, "E",  st->measure.E);

    cJSON *arrL = cJSON_CreateArray();
    for(uint8_t i = 0; i < NUM_LOADS; i++){
        cJSON_AddItemToArray(arrL, cJSON_CreateNumber(st->output[i]? 1:0));
    }
    cJSON_AddItemToObject(root, "L", arrL);

    cJSON_AddBoolToObject(root, "FAIL_I", st->fails.FAIL_I);
    cJSON_AddBoolToObject(root, "FAIL_I_NR", st->fails.FAIL_I_NR);

    cJSON *arrV = cJSON_CreateArray();
    for(uint8_t i = 0; i < NUM_LOADS; i++){
        cJSON_AddItemToArray(arrV, cJSON_CreateBool(st->fails.FAIL_V[i]));
    }
    cJSON_AddItemToObject(root, "FAIL_V", arrV);

    cJSON_AddStringToObject(root, "MODE", control_get_mode()==CTRL_MODE_MAN? "MANUAL" : "AUTO");

    char *json_str = cJSON_PrintUnformatted(root);
    if(json_str){
        esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC_TEL, json_str, 0, 1, 0);
        cJSON_free(json_str);
    }

    cJSON_Delete(root);
}

static void iot_publish_event_fail_changes(const state_t *st){

    /*Falla I*/
    if(st->fails.FAIL_I != last_fail_i){
        cJSON *root = cJSON_CreateObject();
        if(!root) return;
        if(st->fails.FAIL_I){
            cJSON_AddStringToObject(root, "event", "FAIL_I");
            cJSON_AddBoolToObject(root, "rep", st->fails.FAIL_I_NR);
        } else {
            cJSON_AddStringToObject(root, "event", "FAIL_I_OK");
        }
        char *json_str = cJSON_PrintUnformatted(root);
        if(json_str){
            esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC_EVT, json_str, 0, 1, 0);
            cJSON_free(json_str);
        }
        cJSON_Delete(root);

        last_fail_i = st->fails.FAIL_I;
        last_fail_i_nr = st->fails.FAIL_I_NR;
    }

    /*Fallas V*/
    for(uint8_t i = 0; i < NUM_LOADS; i++){
        if(st->fails.FAIL_V[i] != last_fail_v[i]){
            cJSON *root = cJSON_CreateObject();
            if(!root) return;

            if(st->fails.FAIL_V[i]){
                cJSON_AddStringToObject(root, "event", "FAIL_V");
            } else {
                cJSON_AddStringToObject(root, "event", "FAIL_V_OK");
            }
            cJSON_AddNumberToObject(root, "load", i);

            char *json_str = cJSON_PrintUnformatted(root);
            if(json_str){
                esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC_EVT, json_str, 0, 1, 0);
                cJSON_free(json_str);
            }
            cJSON_Delete(root);
            last_fail_v[i] = st->fails.FAIL_V[i];
        }
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data){
    esp_mqtt_event_handle_t event = event_data;

    switch (event->event_id)
    {
    case MQTT_EVENT_CONNECTED:{
        ESP_LOGI(TAG, "MQTT contectado");
        esp_mqtt_client_subscribe(mqtt_client, MQTT_TOPIC_CMD, 1);
        break;
    }

    case MQTT_EVENT_DATA: {
        if(event->topic_len == strlen(MQTT_TOPIC_CMD) && strncmp(event->topic, MQTT_TOPIC_CMD, event->topic_len) == 0){
            iot_cmd_t cmd = {0};

            if(iot_parse_cmd_json(event->data, event->data_len, &cmd)){
                if(xQueueSend(iot_cmd_queue, &cmd, 0) != pdTRUE){
                    ESP_LOGW(TAG, "Cola iot_cmd llena, comando descartado");
                }
            } else {
                ESP_LOGW(TAG, "Payload MQTT cmd invalido");
            }
        }
        break;
    }

    default:
        break;
    }
}

void iot_mqtt_init(){
    iot_cmd_queue = xQueueCreate(8, sizeof(iot_cmd_t));
    configASSERT(iot_cmd_queue != NULL);

    esp_mqtt_client_config_t mqtt_cfg = { .broker.address.uri = MQTT_BROKER_URI, };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    configASSERT(mqtt_client != NULL);

    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);

    ESP_LOGI(TAG, "IOT MQTT inicialiazdo");
}

void task_iot_tx(void *pvParameters){
    (void)pvParameters;

    while(1){
        state_t st;
        state_get(&st);

        iot_publish_telemetry(&st);
        iot_publish_event_fail_changes(&st);

        vTaskDelay(pdMS_TO_TICKS(TASK_PERIOD_COMM_IOT_MS));
    }
}

void task_iot_rx(void *pvParameters){
    (void)pvParameters;

    iot_cmd_t cmd;

    while(1){
        if(xQueueReceive(iot_cmd_queue, &cmd, portMAX_DELAY) == pdTRUE){
            switch (cmd.type)
            {
            case IOT_CMD_MODE_SET:{
                control_set_mode(cmd.mode_set.manual? CTRL_MODE_MAN : CTRL_MODE_AUTO);
                break;
            }

            case IOT_CMD_LOAD_SET:{
                cJSON *d = cJSON_CreateObject();
                cJSON_AddNumberToObject(d, "id", cmd.load_set.id);
                if(cmd.load_set.id < NUM_LOADS && control_get_mode() == CTRL_MODE_MAN){
                    if(control_set_load_state(cmd.load_set.id, cmd.load_set.on)){
                        cJSON_AddStringToObject(d, "state", cmd.load_set.on? "ON" : "OFF");
                        iot_publish_event("LOAD_SET_OK", d);
                    }else{
                        iot_publish_event("LOAD_SET_FAIL", d);
                    }
                } else {
                    iot_publish_event("LOAD_SET_WRONG_MODE", d);
                }
                break;
            }

            case IOT_CMD_ENERGY_RESET: {
                state_reset_energy();
                iot_publish_event("ENERGY_RESET", NULL);
                break;
            }

            case IOT_CMD_CFG_IMAX_SET:{
                control_set_imax(cmd.cfg_imax_set.imax);
                break;
            }

            case IOT_CMD_CFG_VRANGE_SET:{
                if(cmd.cfg_vrange_set.id < NUM_LOADS){
                    control_set_load_vmin(cmd.cfg_vrange_set.id, cmd.cfg_vrange_set.vmin);
                    control_set_load_vmax(cmd.cfg_vrange_set.id, cmd.cfg_vrange_set.vmax);
                }
                break;
            }

            case IOT_CMD_CFG_AUTOREC_SET:{
                if(cmd.cfg_autorec_set.id < NUM_LOADS){
                    control_set_load_auto_rec(cmd.cfg_autorec_set.id, cmd.cfg_autorec_set.ena);
                }
                break;
            }

            case IOT_CMD_CFG_PRIORITY_SET:{
                if(cmd.cfg_priority_set.id < NUM_LOADS){
                    control_set_load_priority(cmd.cfg_priority_set.id, cmd.cfg_priority_set.pr);
                }
                break;
            }

            default:
                iot_publish_event("CMD_INVALID", NULL);
                break;
            }
        }
    }
}