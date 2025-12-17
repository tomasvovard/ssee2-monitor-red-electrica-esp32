#include "hal/display_manager.h"
#include <string.h>
#include "esp_log.h"
#include "app/state.h"

static const char *TAG = "DISPLAY";

static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2c_master_dev_handle_t s_oled_dev = NULL;

static change_detector_t change_detector;

static state_ths_t update_thresholds = {
    .i_ths = UPDATE_CURR_THS,
    .v_ths = UPDATE_VOLT_THS,
    .fp_ths = UPDATE_FP_THS,
    .tmin_ms = UPDATE_MIN_INTERVAL_MS,
    .e_ths = 0.01
};

static const uint8_t ssd1306_init_seq[] = {
    0xAE,       // display OFF
    0x20, 0x00, // Horizontal addressing mode
    0xB0,       // Page 0
    0xC8,       // COM output scan direction remapped
    0x00,       // low column address
    0x10,       // high column address
    0x40,       // start line address
    0x81, 0x7F, // contrast
    0xA1,       // segment remap
    0xA6,       // normal display
    0xA8, 0x3F, // multiplex 1/64
    0xA4,       // resume RAM content
    0xD3, 0x00, // display offset
    0xD5, 0x80, // display clock
    0xD9, 0xF1, // pre-charge
    0xDA, 0x12, // COM pins config
    0xDB, 0x40, // VCOM detect
    0x8D, 0x14, // charge pump ON
    0xAF        // display ON
};

/* Font 5x7 (ASCII 32..126) */
static const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // 32 ' '
    {0x00,0x00,0x5F,0x00,0x00}, // 33 '!'
    {0x00,0x07,0x00,0x07,0x00}, // 34 '"'
    {0x14,0x7F,0x14,0x7F,0x14}, // 35 '#'
    {0x24,0x2A,0x7F,0x2A,0x12}, // 36 '$'
    {0x23,0x13,0x08,0x64,0x62}, // 37 '%'
    {0x36,0x49,0x55,0x22,0x50}, // 38 '&'
    {0x00,0x05,0x03,0x00,0x00}, // 39 '''
    {0x00,0x1C,0x22,0x41,0x00}, // 40 '('
    {0x00,0x41,0x22,0x1C,0x00}, // 41 ')'
    {0x14,0x08,0x3E,0x08,0x14}, // 42 '*'
    {0x08,0x08,0x3E,0x08,0x08}, // 43 '+'
    {0x00,0x50,0x30,0x00,0x00}, // 44 ','
    {0x08,0x08,0x08,0x08,0x08}, // 45 '-'
    {0x00,0x60,0x60,0x00,0x00}, // 46 '.'
    {0x20,0x10,0x08,0x04,0x02}, // 47 '/'
    {0x3E,0x51,0x49,0x45,0x3E}, // 48 '0'
    {0x00,0x42,0x7F,0x40,0x00}, // 49 '1'
    {0x42,0x61,0x51,0x49,0x46}, // 50 '2'
    {0x21,0x41,0x45,0x4B,0x31}, // 51 '3'
    {0x18,0x14,0x12,0x7F,0x10}, // 52 '4'
    {0x27,0x45,0x45,0x45,0x39}, // 53 '5'
    {0x3C,0x4A,0x49,0x49,0x30}, // 54 '6'
    {0x01,0x71,0x09,0x05,0x03}, // 55 '7'
    {0x36,0x49,0x49,0x49,0x36}, // 56 '8'
    {0x06,0x49,0x49,0x29,0x1E}, // 57 '9'
    {0x00,0x36,0x36,0x00,0x00}, // 58 ':'
    {0x00,0x56,0x36,0x00,0x00}, // 59 ';'
    {0x08,0x14,0x22,0x41,0x00}, // 60 '<'
    {0x14,0x14,0x14,0x14,0x14}, // 61 '='
    {0x00,0x41,0x22,0x14,0x08}, // 62 '>'
    {0x02,0x01,0x51,0x09,0x06}, // 63 '?'
    {0x32,0x49,0x79,0x41,0x3E}, // 64 '@'
    {0x7E,0x11,0x11,0x11,0x7E}, // 65 'A'
    {0x7F,0x49,0x49,0x49,0x36}, // 66 'B'
    {0x3E,0x41,0x41,0x41,0x22}, // 67 'C'
    {0x7F,0x41,0x41,0x22,0x1C}, // 68 'D'
    {0x7F,0x49,0x49,0x49,0x41}, // 69 'E'
    {0x7F,0x09,0x09,0x09,0x01}, // 70 'F'
    {0x3E,0x41,0x49,0x49,0x7A}, // 71 'G'
    {0x7F,0x08,0x08,0x08,0x7F}, // 72 'H'
    {0x00,0x41,0x7F,0x41,0x00}, // 73 'I'
    {0x20,0x40,0x41,0x3F,0x01}, // 74 'J'
    {0x7F,0x08,0x14,0x22,0x41}, // 75 'K'
    {0x7F,0x40,0x40,0x40,0x40}, // 76 'L'
    {0x7F,0x02,0x0C,0x02,0x7F}, // 77 'M'
    {0x7F,0x04,0x08,0x10,0x7F}, // 78 'N'
    {0x3E,0x41,0x41,0x41,0x3E}, // 79 'O'
    {0x7F,0x09,0x09,0x09,0x06}, // 80 'P'
    {0x3E,0x41,0x51,0x21,0x5E}, // 81 'Q'
    {0x7F,0x09,0x19,0x29,0x46}, // 82 'R'
    {0x46,0x49,0x49,0x49,0x31}, // 83 'S'
    {0x01,0x01,0x7F,0x01,0x01}, // 84 'T'
    {0x3F,0x40,0x40,0x40,0x3F}, // 85 'U'
    {0x1F,0x20,0x40,0x20,0x1F}, // 86 'V'
    {0x3F,0x40,0x38,0x40,0x3F}, // 87 'W'
    {0x63,0x14,0x08,0x14,0x63}, // 88 'X'
    {0x07,0x08,0x70,0x08,0x07}, // 89 'Y'
    {0x61,0x51,0x49,0x45,0x43}, // 90 'Z'
    {0x00,0x7F,0x41,0x41,0x00}, // 91 '['
    {0x02,0x04,0x08,0x10,0x20}, // 92 '\'
    {0x00,0x41,0x41,0x7F,0x00}, // 93 ']'
    {0x04,0x02,0x01,0x02,0x04}, // 94 '^'
    {0x40,0x40,0x40,0x40,0x40}, // 95 '_'
    {0x00,0x01,0x02,0x04,0x00}, // 96 '`'
    {0x20,0x54,0x54,0x54,0x78}, // 97 'a'
    {0x7F,0x48,0x44,0x44,0x38}, // 98 'b'
    {0x38,0x44,0x44,0x44,0x20}, // 99 'c'
    {0x38,0x44,0x44,0x48,0x7F}, // 100 'd'
    {0x38,0x54,0x54,0x54,0x18}, // 101 'e'
    {0x08,0x7E,0x09,0x01,0x02}, // 102 'f'
    {0x0C,0x52,0x52,0x52,0x3E}, // 103 'g'
    {0x7F,0x08,0x04,0x04,0x78}, // 104 'h'
    {0x00,0x44,0x7D,0x40,0x00}, // 105 'i'
    {0x20,0x40,0x44,0x3D,0x00}, // 106 'j'
    {0x7F,0x10,0x28,0x44,0x00}, // 107 'k'
    {0x00,0x41,0x7F,0x40,0x00}, // 108 'l'
    {0x7C,0x04,0x18,0x04,0x78}, // 109 'm'
    {0x7C,0x08,0x04,0x04,0x78}, // 110 'n'
    {0x38,0x44,0x44,0x44,0x38}, // 111 'o'
    {0x7C,0x14,0x14,0x14,0x08}, // 112 'p'
    {0x08,0x14,0x14,0x18,0x7C}, // 113 'q'
    {0x7C,0x08,0x04,0x04,0x08}, // 114 'r'
    {0x48,0x54,0x54,0x54,0x20}, // 115 's'
    {0x04,0x3F,0x44,0x40,0x20}, // 116 't'
    {0x3C,0x40,0x40,0x20,0x7C}, // 117 'u'
    {0x1C,0x20,0x40,0x20,0x1C}, // 118 'v'
    {0x3C,0x40,0x30,0x40,0x3C}, // 119 'w'
    {0x44,0x28,0x10,0x28,0x44}, // 120 'x'
    {0x0C,0x50,0x50,0x50,0x3C}, // 121 'y'
    {0x44,0x64,0x54,0x4C,0x44}, // 122 'z'
    {0x00,0x08,0x36,0x41,0x00}, // 123 '{'
    {0x00,0x00,0x7F,0x00,0x00}, // 124 '|'
    {0x00,0x41,0x36,0x08,0x00}, // 125 '}'
    {0x08,0x04,0x08,0x10,0x08}, // 126 '~'
};

static esp_err_t i2c_init_display(void)
{
    if (s_i2c_bus != NULL && s_oled_dev != NULL) {
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_PORT_DISPLAY,
        .scl_io_num = I2C_SCL_DISPLAY,
        .sda_io_num = I2C_SDA_DISPLAY,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error creando bus I2C: %s", esp_err_to_name(err));
        return err;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SSD1306_I2C_ADDR,
        .scl_speed_hz = I2C_FREQ_DISPLAY_HZ,
    };

    err = i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_oled_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error agregando dispositivo I2C: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

static esp_err_t ssd1306_send_cmd(uint8_t cmd)
{
    uint8_t buf[2] = { SSD1306_CMD, cmd };
    return i2c_master_transmit(s_oled_dev, buf, sizeof(buf), I2C_MASTER_TIMEOUT_MS);
}

static esp_err_t ssd1306_send_cmd_list(const uint8_t *cmds, size_t len)
{
    for (uint8_t i = 0; i < len; i++) {
        esp_err_t err = ssd1306_send_cmd(cmds[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ssd1306_send_cmd_list failed in cmds[%d]: %s", i, esp_err_to_name(err));
            return err;
        }
    }
    return ESP_OK;
}

static esp_err_t ssd1306_send_data(const uint8_t *data, size_t len)
{
    if (len > SSD1306_WIDTH) {
        len = SSD1306_WIDTH;
    }

    uint8_t buf[SSD1306_WIDTH + 1];
    buf[0] = SSD1306_DATA;
    memcpy(&buf[1], data, len);

    return i2c_master_transmit(s_oled_dev, buf, len + 1, -1);
}

esp_err_t oled_clear(void)
{
    uint8_t buffer[SSD1306_WIDTH];
    memset(buffer, 0x00, sizeof(buffer));
    esp_err_t ret;

    for (uint8_t page = 0; page < (SSD1306_HEIGHT / 8); page++) {
        ret = ssd1306_send_cmd(0xB0 | page); // page address
        if(ret != ESP_OK) return ret;
        
        ret = ssd1306_send_cmd(0x00); // low column
        if(ret != ESP_OK) return ret;
        
        ret = ssd1306_send_cmd(0x10); // high column
        if(ret != ESP_OK) return ret;
        
        ret = ssd1306_send_data(buffer, sizeof(buffer)); 
        if(ret != ESP_OK) return ret;
    }

    return ESP_OK;
}

esp_err_t oled_init(void)
{
    esp_err_t ret;

    ret = i2c_init_display();
    if(ret != ESP_OK){
        ESP_LOGE(TAG, "i2c_init_display failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = ssd1306_send_cmd_list(ssd1306_init_seq, sizeof(ssd1306_init_seq)); //bug
    if(ret != ESP_OK){
        ESP_LOGE(TAG, "ssd1306_send_cmd_list failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = oled_clear();
    if(ret != ESP_OK){
        ESP_LOGE(TAG, "oled_clear failed: %s", esp_err_to_name(ret));
    }

    return ESP_OK;
}

/* fila = 0..7 (cada una 8 px alto). text: ASCII. Máx ~21 char (128/6) */
esp_err_t oled_draw_text_line(uint8_t row, const char *text)
{
    if (row >= (SSD1306_HEIGHT / 8)) return ESP_ERR_INVALID_ARG;

    uint8_t buffer[SSD1306_WIDTH];
    memset(buffer, 0x00, sizeof(buffer));

    uint8_t col = 0;
    for (uint8_t i = 0; text[i] != '\0' && col + 6 <= SSD1306_WIDTH; i++) {
        char c = text[i];
        if (c < 32 || c > 126) c = '?';
        const uint8_t *chr = font5x7[c - 32];
        for (uint8_t j = 0; j < 5; ++j) {
            buffer[col++] = chr[j];
        }
        buffer[col++] = 0x00;
    }

    esp_err_t ret;

    ret = ssd1306_send_cmd(0xB0 | row);
    if(ret != ESP_OK) return ret;

    ret = ssd1306_send_cmd(0x00);
    if(ret != ESP_OK) return ret;

    ret = ssd1306_send_cmd(0x10);
    if(ret != ESP_OK) return ret;

    return ssd1306_send_data(buffer, sizeof(buffer));
}

esp_err_t display_init(void)
{
    esp_err_t err = oled_init();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Display SSD1306 inicializado");
    }

    state_change_detector_init(&change_detector);

    return err;
}

void task_display(void *pvParameters)
{
    (void)pvParameters;

    state_t st;

    // Pantalla de bienvenida rápida
    oled_clear();
    oled_draw_text_line(0, " ANALIZADOR POTENCIA");
    oled_draw_text_line(2, "   Inicializando...");
    vTaskDelay(pdMS_TO_TICKS(1000)); //todo: reemplazar esto por una confirmación real de que está listo para mostrar datos

    while (1) {
        state_get(&st);

        char line[SSD1306_MAX_TXT_LINES][22] = {0};
        
        if(state_change_detector_update(&change_detector, &st, &update_thresholds)){

            snprintf(line[0], sizeof(line[0]), "V :%d V", (int16_t)st.measure.Vrms);
            snprintf(line[1], sizeof(line[1]), "I :%.2f A", st.measure.Irms);
            snprintf(line[2], sizeof(line[2]), "fp:%.2f", st.measure.fp);
            snprintf(line[3], sizeof(line[3]), "P :%.2f W S:%.2f VA", st.measure.P, st.measure.S);
            snprintf(line[4], sizeof(line[4]), "E :%.3f kWh", st.measure.E);
            snprintf(line[5], sizeof(line[5]), "L1:%c L2:%c L3:%c L4:%c",
                st.output[0] ? '1' : '0',
                st.output[1] ? '1' : '0',
                st.output[2] ? '1' : '0',
                st.output[3] ? '1' : '0');
            snprintf(line[6], sizeof(line[6]), "FALLAS: ");
            snprintf(line[7], sizeof(line[7]), "I:%c V 1:%c 2:%c 3:%c 4:%c",
                st.fails.FAIL_I ? '!' : '-',
                st.fails.FAIL_V[0] ? '!' : '-',
                st.fails.FAIL_V[1] ? '!' : '-',
                st.fails.FAIL_V[2] ? '!' : '-',
                st.fails.FAIL_V[3] ? '!' : '-');

            for(uint8_t i = 0; i < 8; i++){
                oled_draw_text_line(i, line[i]);
            }
                
            state_change_detector_mark_sent(&change_detector, &st);
        }
        vTaskDelay(pdMS_TO_TICKS(TASK_PERIOD_DISPLAY_MS));
    }
}
