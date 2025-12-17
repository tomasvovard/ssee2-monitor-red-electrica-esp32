#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "hal/adc_dma.h"
#include "app/measure.h"
#include "app/acquisition.h"
#include "app/control.h"
#include "config/system_config.h"
#include "comms/uart_protocol.h"
#include "hal/display_manager.h"
#include "comms/iot_mqtt.h"
#include "comms/wifi_conn.h"
#include "hal/gpio_loads.h"

static void main_init(){

    nvs_config_init();

    state_init();
    ESP_ERROR_CHECK(gpio_loads_init());
    control_init();

    if(app_adc_init_calibration()){
        ESP_LOGW("ADC", "Calibración no disponible");
    }
    app_adc_dma_init();

    uart_protocol_init();
    if(wifi_conn_init() == ESP_OK){
        iot_mqtt_init();
    } else {
        ESP_LOGW("MAIN", "No se pudo inicializar wifi. Operando sin IoT.");
    }

    if (display_init() != ESP_OK) {
        ESP_LOGE("MAIN", "Error inicializando display");
    }

    app_adc_dma_start_conv();
}

void app_main(){

    #ifndef debug
    esp_log_level_set("*", ESP_LOG_ERROR); //Bajamos nivel de logs globales para que no contaminen la salida
    esp_log_level_set("task_wdt", ESP_LOG_NONE); //Apago los logs del watchdog
    /*bajo los logs de mqtt*/
    esp_log_level_set("esp-tls", ESP_LOG_NONE);       
    esp_log_level_set("transport_base", ESP_LOG_NONE);
    esp_log_level_set("mqtt_client", ESP_LOG_NONE);    
    #endif

    main_init();

    xTaskCreate(
        task_adc_acquisition,
        "adc_acq",
        TASK_STACK_ADC_ACQ,
        NULL,
        TASK_PRIORITY_ADC_ACQ,
        NULL
    );

    xTaskCreate(
        task_control,
        "control_cargas",
        TASK_STACK_CONTROL,
        NULL,
        TASK_PRIORITY_CONTROL,
        NULL
    );

    xTaskCreate(
        task_uart_rx,
        "uart_rx",
        TASK_STACK_COMM_UART,
        NULL,
        TASK_PRIORITY_COMM_UART,
        NULL
    );

    xTaskCreate(
        task_uart_handler,
        "uart_handler",
        TASK_STACK_COMM_UART,
        NULL,
        TASK_PRIORITY_COMM_UART,
        NULL
    );

    xTaskCreate(
        task_uart_tx,
        "uart_tx",
        TASK_STACK_COMM_UART,
        NULL,
        TASK_PRIORITY_COMM_UART,
        NULL
    );

     xTaskCreate(
        task_display,
        "task_display",
        TASK_STACK_DISPLAY,
        NULL,
        TASK_PRIORITY_DISPLAY,
        NULL
    );

    xTaskCreate(
        task_iot_tx,
        "task_iot_tx",
        TASK_STACK_COMM_IOT,
        NULL,
        TASK_PRIORITY_COMM_IOT,
        NULL
    );

    xTaskCreate(
        task_iot_rx,
        "task_iot_rx",
        TASK_STACK_COMM_IOT,
        NULL,
        TASK_PRIORITY_COMM_IOT,
        NULL
    );

}