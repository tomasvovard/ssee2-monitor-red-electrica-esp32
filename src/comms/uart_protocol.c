#include "comms/uart_protocol.h"
#include "comms/uart_handler.h"
#include <string.h>
#include "app/measure.h"
#include "app/state.h"
#include <ctype.h>

static const char *TAG = "UART_PROTOCOL";

static uart_state_t uart_state;
static change_detector_t change_detector;

static QueueHandle_t uart_cmd_buffer;
static QueueHandle_t uart_resp_buffer;

static state_ths_t update_thresholds = {
    .i_ths = UPDATE_CURR_THS,
    .v_ths = UPDATE_VOLT_THS,
    .fp_ths = UPDATE_FP_THS,
    .tmin_ms = UPDATE_MIN_INTERVAL_MS,
    .e_ths = 0.01
}; 

static void uart_send_string(const char *str){
    if(!str) return;
    size_t len = strlen(str);
    if(len == 0) return;
    uart_write_bytes(UART_NUM, str, len);
}

void uart_protocol_init(){
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };

    ESP_ERROR_CHECK(uart_driver_install(UART_NUM, UART_BUF_SIZE * 2, UART_BUF_SIZE*2, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    
    
    uart_state.disp_mode = DISP_CONT;
    uart_state.session.active = false;
    uart_state.session.level = USER_VIEWER;

    uart_cmd_buffer = xQueueCreate(UART_RX_QUEUE_SIZE, sizeof(uart_cmd_t));
    uart_resp_buffer = xQueueCreate(UART_TX_QUEUE_SIZE, sizeof(uart_resp_t));

    configASSERT(uart_cmd_buffer != NULL);
    configASSERT(uart_resp_buffer != NULL);

    state_change_detector_init(&change_detector);

    ESP_LOGI(TAG, "UART Protocol inicializado");
}
    
void task_uart_rx(void *pvParameters){
    (void)pvParameters;

    uint8_t rx_char;
    char line_buf[CMD_MAX_LEN + PARAMS_MAX_LEN + 4];
    size_t line_len = 0;
    TickType_t last_char_time = xTaskGetTickCount();

    ESP_LOGI(TAG, "Task UART Rx Inicializada");

    while (1)
    {
        int len = uart_read_bytes(UART_NUM, &rx_char, 1, pdMS_TO_TICKS(TASK_UART_RX_TIMEOUT));

        if(len <= 0){
            /*hago un timeout de línea completa de 30s*/
            if(line_len > 0 && (xTaskGetTickCount() - last_char_time) > pdMS_TO_TICKS(TASK_UART_RX_TIMEOUT*300)){
                ESP_LOGW(TAG, "Linea incompleta descartada");
                line_len = 0;
            }
            continue;  
        }

        last_char_time = xTaskGetTickCount();

        if(rx_char == '\r' || rx_char == '\n'){
            if(line_len == 0) continue;
            line_buf[line_len] = '\0';

            uart_cmd_t cmd = {0};
            cmd.session = &uart_state.session;
            
            char *space = strchr(line_buf, ' '); //si no encuentra el caracter en la cadena devuelve un NULL pointer, sino apunta a la primera aparición en la cadena
            if(space){
                *space = '\0';
                strncpy(cmd.cmd, line_buf, CMD_MAX_LEN - 1);
                strncpy(cmd.params, space + 1, PARAMS_MAX_LEN - 1);
            } else {
                strncpy(cmd.cmd, line_buf, CMD_MAX_LEN - 1);
                cmd.params[0] = '\0';
            }

            cmd.cmd[CMD_MAX_LEN - 1] = '\0';
            cmd.params[PARAMS_MAX_LEN - 1] = '\0';

            for(uint8_t i = 0; cmd.cmd[i] != 0; i++){
                cmd.cmd[i] = (char)toupper((unsigned char)cmd.cmd[i]);
            }

            if(xQueueSend(uart_cmd_buffer, &cmd, pdMS_TO_TICKS(TASK_UART_RX_TIMEOUT*10)) != pdTRUE){
                ESP_LOGW(TAG, "Cola RX llena");
                continue;
            }

            line_len = 0;
        } else {
            if(line_len < sizeof(line_buf) - 1){
                line_buf[line_len++] = (char)rx_char;
            } else {
                ESP_LOGW(TAG, "Comando muy largo, descartado");
                line_len = 0;
            }
        }
    }
    
}

void task_uart_tx(void *pvParameters){
    (void)pvParameters;

    uart_resp_t resp;
    static bool last_fail_i = false;
    static bool last_fail_v[NUM_LOADS] = {false};
    static bool waiting_rec[NUM_LOADS] = {false};
    static char alert[64];
    static char buf[200];
    static state_t st;
    static sys_load_cfg_t cfg;

    while(1){
        /*enviar respuestas pendientes*/
        while(xQueueReceive(uart_resp_buffer, &resp, 0)==pdTRUE){
            uart_send_string(resp.data);
        }

        state_get(&st);
        control_get_cfg(&cfg);

        /*enviar alertas de falla de corriente*/
        if(st.fails.FAIL_I && !last_fail_i){
            if(st.fails.FAIL_I_NR){
                uart_send_string("ALERTA: FALLA_I_REPETITIVA. AUTOREPOSICION DESACTIVADA\r\n");
            } else {
                 uart_send_string("ALERTA: FALLA_I\r\n");
            }
            last_fail_i = true;
        } else if(!st.fails.FAIL_I && last_fail_i){
            uart_send_string("AVISO: FALLA_I_OK\r\n");
            last_fail_i = false;
            if(!st.fails.FAIL_I_NR){
                for(uint8_t i = 0; i < NUM_LOADS; i++){
                    if(cfg.load[i].auto_rec && !st.output[i]){
                        waiting_rec[i] = true;
                    }
                }
            }
        }

        /*enviar alertas de fallas de tensión*/
        for(uint8_t i = 0; i < NUM_LOADS; i++){
            if(st.fails.FAIL_V[i] && !last_fail_v[i]){
                snprintf(alert, sizeof(alert), "ALERTA: FALLA_V_CARGA_%d\r\n",i);
                uart_send_string(alert);
                last_fail_v[i] = true;
            } 
            else if(!st.fails.FAIL_V[i] && last_fail_v[i]){
                snprintf(alert, sizeof(alert), "AVISO: FALLA_V_CARGA_%d_OK\r\n",i);
                uart_send_string(alert);
                last_fail_v[i] = false;
                if(cfg.load[i].auto_rec && !st.output[i]){
                    waiting_rec[i] = true;
                }
            }
        }

        /*notificar reposición*/
        for(uint8_t i = 0; i < NUM_LOADS; i++){
            if(waiting_rec[i] && st.output[i]){
                waiting_rec[i] = false;
                snprintf(alert, sizeof(alert), "AVISO: CARGA_%d_REPUESTA\r\n",i);
                uart_send_string(alert);
            }
        }

        /*enviar mediciones en modo continuo*/
        if(uart_get_disp_mode() == DISP_CONT){

            if(state_change_detector_update(&change_detector, &st, &update_thresholds)){
                snprintf(buf, sizeof(buf), "CONT_MEAS V:%d I:%.2f P:%.3f S:%.3f FP:%.3f E:%.3f\r\n", (uint16_t)st.measure.Vrms, st.measure.Irms, st.measure.P, st.measure.S, st.measure.fp, st.measure.E);              
                uart_send_string(buf);
                state_change_detector_mark_sent(&change_detector, &st);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(TASK_PERIOD_COMM_UART_MS));
    }
}

void task_uart_handler(void *pvParameters){
    (void)pvParameters;

    uart_cmd_t cmd;
    uart_resp_t resp;

    while (1)
    {
        if(xQueueReceive(uart_cmd_buffer, &cmd, portMAX_DELAY) == pdTRUE){
            memset(&resp, 0, sizeof(resp));
            uart_process_command(&cmd, &resp);
        }
        if(!xQueueSend(uart_resp_buffer, &resp, pdMS_TO_TICKS(TASK_UART_RX_TIMEOUT*10))){
            ESP_LOGW(TAG, "Cola Tx llena, respuesta perdida");
        }
    }
}

void uart_set_disp_mode(uart_disp_mode_t mode){
    uart_state.disp_mode = mode;
}

uart_disp_mode_t uart_get_disp_mode(void){
    return uart_state.disp_mode;
}