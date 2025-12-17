/*uart_handler.c*/
#include "comms/uart_handler.h"
#include "app/control.h"
#include "app/state.h"
#include "core/nvs_config.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

#define ADMIN_PASSWORD "admin123"
#define SESSION_TOUT_MS (30*60*1000) // 30 min

static const char *TAG = "UART_HANDLER";

static const cmd_map_t cmd_lookup_table[] = {
    {"PING", CMD_PING},
    {"LOGIN",  CMD_LOGIN},
    {"LOGOUT", CMD_LOGOUT},
    {"USERID", CMD_USERID},
    {"MEAS",   CMD_MEAS},
    {"MODE",   CMD_MODE},
    {"LOAD",   CMD_LOAD},
    {"ENERGY", CMD_ENERGY},
    {"CFG",    CMD_CFG},
    {"DISPMODE", CMD_DISPMODE},
    {"HELP",   CMD_HELP},
    {NULL,     CMD_UNK}
};

static cmd_type_t parse_command(const char *cmd_str){
    for(uint8_t i = 0; cmd_lookup_table[i].str != NULL; i++){
        if(strcmp(cmd_str, cmd_lookup_table[i].str) == 0) return cmd_lookup_table[i].type;
    }
    return CMD_UNK;
}

static void send_ok(uart_resp_t *resp, const char *msg){
    snprintf(resp->data, RESPONSE_MAX_LEN, "OK %s\r\n", msg);
    resp->is_alert = false;
}

static void send_error(uart_resp_t *resp, const char *msg){
    snprintf(resp->data, RESPONSE_MAX_LEN, "ERROR %s\r\n", msg);
    resp->is_alert = false;
}

static void send_unauthorized(uart_resp_t *resp){
    send_error(resp, "NO_AUTORIZADO");
}

bool uart_login(const char *pass, session_t *session){
    if(strcmp(pass, ADMIN_PASSWORD) == 0){
        session->level = USER_ADMIN;
        session->login_time = xTaskGetTickCount();
        session->active = true;
        ESP_LOGI(TAG, "LOGIN_EXITOSO");
        return true;
    } else {
        ESP_LOGW(TAG, "LOGIN_FALLIDO");
        return false;
    }
}

bool uart_session_check(session_t *session){
    if(!session->active) return false;

    TickType_t lapse = xTaskGetTickCount() - session->login_time;
    if(pdTICKS_TO_MS(lapse) > SESSION_TOUT_MS){
        session->active =  false;
        ESP_LOGW(TAG, "SESION_EXPIRADA");
        return false;
    }

    return true;
}

void uart_logout(session_t *session){
    session->active = false;
    session->level = USER_VIEWER;
    ESP_LOGI(TAG, "LOGOUT");
}

void uart_process_command(uart_cmd_t *cmd, uart_resp_t *resp){
    char subcmd[32] = {0};
    char arg1[32] = {0};
    char arg2[32] = {0};
    char arg3[32] = {0};

    sscanf(cmd->params, "%31s %31s %31s %31s", subcmd, arg1, arg2, arg3);

    cmd_type_t cmd_type = parse_command(cmd->cmd);

    switch (cmd_type)
    {
    case CMD_PING:{
        send_ok(resp, "PONG");
        break;
    }

    case CMD_LOGIN:{
        if(uart_login(subcmd, cmd->session)){
            send_ok(resp, "ADMIN");
        } else {
            send_error(resp, "PASS_INCORRECTA");
        }
        break;
    }

    case CMD_LOGOUT:{
        uart_logout(cmd->session);
        send_ok(resp, "VIEWER");
        break;
    }
    
    case CMD_USERID: {
        const char *level = (cmd->session->active && cmd->session->level == USER_ADMIN ? "ADMIN" : "VIEWER");
        send_ok(resp, level);
        break;
    }

    case CMD_MEAS: {
        state_t st;
        state_get(&st);

        if(strcmp(subcmd, "GET") == 0){
            char buf[200];
            snprintf(buf, sizeof(buf), "V:%.2f I:%.3f P:%.3f S:%.3f FP:%.3f E:%.3f", st.measure.Vrms, st.measure.Irms, st.measure.P, st.measure.S, st.measure.fp, st.measure.E);
            send_ok(resp, buf);
        } else {
            send_error(resp, "SUBCMD_INVALIDO");
        }
        break;
    }

    case CMD_MODE: {
        if(strcmp(subcmd, "GET") == 0){
            ctrl_mode_t mode = control_get_mode();
            send_ok(resp, mode == CTRL_MODE_AUTO ? "AUTO" : "MANUAL");
        } else if(strcmp(subcmd, "SET") == 0){
            if(strcmp(arg1, "AUTO") == 0){
                control_set_mode(CTRL_MODE_AUTO);
                send_ok(resp, "AUTO");
            } else if(strcmp(arg1, "MANUAL") == 0){
                control_set_mode(CTRL_MODE_MAN);
                send_ok(resp, "MANUAL");
            } else {
                send_error(resp, "MODO_INVALIDO");
            }
        } else {
            send_error(resp, "SUBCMD_INVALIDO");
        }
        break;
    }

    case CMD_LOAD: {
        if(strcmp(subcmd, "GET") == 0){
            state_t st;
            state_get(&st);
            char buf[128];
            snprintf(buf, sizeof(buf), "0:%s 1:%s 2:%s 3:%s", st.output[0]? "ON" : "OFF", st.output[1]? "ON" : "OFF", st.output[2]? "ON" : "OFF", st.output[3]? "ON" : "OFF");
            send_ok(resp, buf);
        } else if(strcmp(subcmd, "SET") == 0){
            uint8_t id = atoi(arg1);
            if(id >= NUM_LOADS){
                send_error(resp, "ID_INVALIDO");
                break;
            }
            if(control_get_mode() != CTRL_MODE_MAN){
                send_error(resp, "NO_MODO_MANUAL");
                break;
            }
            if(strcmp(arg2, "ON") == 0){
                if(control_set_load_state(id, true)){
                    send_ok(resp, "ON");
                } else {
                    send_error(resp, "No se pudo actualizar");
                }
            } else if(strcmp(arg2, "OFF") == 0){
                if(control_set_load_state(id, false)){
                    send_ok(resp, "OFF");
                } else {
                    send_error(resp, "No se pudo actualizar");
                }
                
            } else {
                send_error(resp, "ESTADO_INVALIDO");
            }
        }
        else {
            send_error(resp, "SUBCMD_INVALIDO");
        }
        break;
    }

    case CMD_ENERGY:{
        if(!uart_session_check(cmd->session)){
            send_unauthorized(resp);
            break;
        }
        if(strcmp(subcmd, "RESET") == 0){
            state_reset_energy();
            send_ok(resp, "RESET");
        } else {
            send_error(resp, "SUBCMD_INVALIDO");
        }
        break;
    }

    case CMD_CFG:{
        if(!uart_session_check(cmd->session)){
            send_unauthorized(resp);
            break;
        }

        if(strcmp(subcmd, "IMAX") == 0 && strcmp(arg1, "SET") == 0){
            float val = atof(arg2);
            if(val <= 0.0f){
                send_error(resp, "VALOR_INVALIDO");
                break;
            }
            control_set_imax(val);
            char buf[32];
            snprintf(buf, sizeof(buf), "%.2f", val);
            send_ok(resp, buf);
        } 
        else if(strcmp(subcmd, "SAVE") == 0){
            if(control_save_to_nvs()){
                state_t st;
                state_get(&st);
                nvs_save_energy(st.measure.E);
                send_ok(resp, "CONFIG_GUARDADA");
            } else {
                send_error(resp, "FALLO_GUARDADO");
            }
        }
        else if(strcmp(subcmd, "LOAD") == 0){
            if(control_load_from_nvs()){
                send_ok(resp, "CONFIG_CARGADA");
            } else {
                send_error(resp, "FALLO_CARGA");
            }
        }
        else if(strcmp(subcmd, "DEFAULTS") == 0){
            nvs_reset_default();
            control_reset();
            send_ok(resp, "RESTAURADO");
        } 
        else if(strcmp(subcmd, "VMAX") == 0 && strcmp(arg1, "SET") == 0){
            uint8_t id = atoi(arg2);
            if(id >= NUM_LOADS){
                send_error(resp,"ID_INVALIDO");
                break;
            }
            int16_t v = atoi(arg3);
            if(v < -1 || (v != -1 && v <= control_get_v_min(id))){
                send_error(resp, "VALOR_INVALIDO");
                break;
            }
            control_set_load_vmax(id, v);
            send_ok(resp, "VMAX_SETEADO");
        } 
        else if(strcmp(subcmd, "VMIN") == 0 && strcmp(arg1, "SET") == 0){
            uint8_t id = atoi(arg2);
            if(id >= NUM_LOADS){
                send_error(resp,"ID_INVALIDO");
                break;
            }
            int16_t v = atoi(arg3);
            if(v < -1 || (v != -1 && v >= control_get_v_max(id))){
                send_error(resp, "VALOR_INVALIDO");
                break;
            }
            control_set_load_vmin(id, v);
            send_ok(resp, "VMIN_SETEADO");
        }
        else if(strcmp(subcmd, "AUTOREC") == 0 && strcmp(arg1, "SET") == 0){
            uint8_t id = atoi(arg2);
            if(id >= NUM_LOADS){
                send_error(resp,"ID_INVALIDO");
                break;
            }
            bool rec;
            if(strcmp(arg3, "ON") == 0){
                rec = true;
            } else if(strcmp(arg3, "OFF") == 0){
                rec = false;
            } else {
                send_error(resp, "ESTADO_INVALIDO");
                break;
            }
            control_set_load_auto_rec(id, rec);
            send_ok(resp, "AUTOREC_SETEADO");
        }
        else if(strcmp(subcmd, "PRIORITY") == 0 && strcmp(arg1, "SET") == 0){
            uint8_t id = atoi(arg2);
            if(id >= NUM_LOADS){
                send_error(resp,"ID_INVALIDO");
                break;
            }
            uint8_t pr = atoi(arg3);
            control_set_load_priority(id, pr);
            send_ok(resp, "PRIORIDAD_SETEADA");
        }
        else if (strcmp(subcmd, "GET") == 0){
            uint8_t id = atoi(arg1);
            if(id >= NUM_LOADS){
                send_error(resp,"ID_INVALIDO");
                break;
            }
            sys_load_cfg_t cfg;
            if(!control_get_cfg(&cfg)){
                send_error(resp, "CFG_NO_ENCONTRADA");
            }
            char buf[128];
            snprintf(buf, sizeof(buf), "IMAX:%.2f VMIN:%d VMAX:%d AUTOREC:%s PRIORITY:%d", cfg.imax, cfg.load[id].v_min, cfg.load[id].v_max, cfg.load[id].auto_rec? "ON" : "OFF", cfg.load[id].priority);
            send_ok(resp, buf);
        }
        else {
            send_error(resp, "SUBCMD_INVALIDO");
        }
        break;
    }

    case CMD_DISPMODE: {
        if(strcmp(subcmd, "CONT") == 0){
            uart_set_disp_mode(DISP_CONT);
            send_ok(resp, "MODO_CONTINUO");
        } 
        else if(strcmp(subcmd, "ONETIME") == 0){
            uart_set_disp_mode(DISP_ONETIME);
            send_ok(resp, "MODO_UNA_VEZ");
        }
        else if(strcmp(subcmd, "GET") == 0){
            uart_disp_mode_t mode = uart_get_disp_mode();
            send_ok(resp, mode == DISP_CONT? "CONTINUO" : "UNA_VEZ");
        }
        else {
            send_error(resp, "MODO_INVALIDO");
        }
        break;
    }

    case CMD_HELP: {
        send_ok(resp, "PING LOGIN LOGOUT USERID MEAS MODE LOAD ENERGY CFG HELP");
        break;
    }

    case CMD_UNK:
    default:
        send_error(resp, "CMD_DESCONOCIDO");
        break;
    }
}