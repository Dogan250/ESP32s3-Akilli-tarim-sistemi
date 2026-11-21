#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "driver/gpio.h"
#include "driver/i2c.h"

// Kendi internet bilgilerinize göre doldurun
#define WIFI_SSID      "WIFI_ADINIZ"
#define WIFI_PASS      "WIFI_SIFRENIZ"
#define API_BASE_URL   "http://192.168.1.X:5000"

// ESP32-S3 pin baglantilari
#define OLED_SDA_PIN   GPIO_NUM_8
#define OLED_SCL_PIN   GPIO_NUM_9
#define OLED_ADDR      0x3C

#define ROTARY_CLK_PIN GPIO_NUM_4
#define ROTARY_DT_PIN  GPIO_NUM_5
#define ROTARY_SW_PIN  GPIO_NUM_6

#define I2C_PORT       I2C_NUM_0
#define WIFI_CONNECTED_BIT BIT0

static const char *TAG = "TARIM_TERMINAL";
static EventGroupHandle_t s_wifi_event_group;
static char http_resp_buf[512];
static int http_resp_len = 0;

// --- OLED ve 5x7 yazi yardimcilari ---
static esp_err_t oled_cmd(uint8_t cmd) {
    uint8_t buf[2] = {0x00, cmd};
    return i2c_master_write_to_device(I2C_PORT, OLED_ADDR, buf, sizeof(buf), pdMS_TO_TICKS(100));
}

static void oled_clear_page(uint8_t page) {
    if (page >= 8) return;
    uint8_t data[129];
    data[0] = 0x40;
    memset(&data[1], 0x00, 128);
    oled_cmd(0xB0 + page);
    oled_cmd(0x00);
    oled_cmd(0x10);
    i2c_master_write_to_device(I2C_PORT, OLED_ADDR, data, sizeof(data), pdMS_TO_TICKS(100));
}

static void oled_clear(void) {
    for (uint8_t page = 0; page < 8; page++) {
        oled_clear_page(page);
    }
}

static void oled_draw_bar(uint8_t page, bool selected) {
    if (page >= 8) return;
    uint8_t data[129];
    data[0] = 0x40;
    memset(&data[1], selected ? 0xFF : 0x00, 128);
    oled_cmd(0xB0 + page);
    oled_cmd(0x00);
    oled_cmd(0x10);
    i2c_master_write_to_device(I2C_PORT, OLED_ADDR, data, sizeof(data), pdMS_TO_TICKS(100));
}

static char to_upper_ascii(char c) {
    if (c >= 'a' && c <= 'z') {
        return (char)(c - ('a' - 'A'));
    }
    return c;
}

static const uint8_t *font5x7_get(char raw_c) {
    char c = to_upper_ascii(raw_c);
    static const uint8_t f_space[5] = {0x00,0x00,0x00,0x00,0x00};
    static const uint8_t f_colon[5] = {0x00,0x36,0x36,0x00,0x00};
    static const uint8_t f_dash[5]  = {0x08,0x08,0x08,0x08,0x08};
    static const uint8_t f_dot[5]   = {0x00,0x60,0x60,0x00,0x00};
    static const uint8_t f_slash[5] = {0x20,0x10,0x08,0x04,0x02};
    static const uint8_t f_0[5] = {0x3E,0x51,0x49,0x45,0x3E}, f_1[5] = {0x00,0x42,0x7F,0x40,0x00};
    static const uint8_t f_2[5] = {0x42,0x61,0x51,0x49,0x46}, f_3[5] = {0x21,0x41,0x45,0x4B,0x31};
    static const uint8_t f_4[5] = {0x18,0x14,0x12,0x7F,0x10}, f_5[5] = {0x27,0x45,0x45,0x45,0x39};
    static const uint8_t f_6[5] = {0x3C,0x4A,0x49,0x49,0x30}, f_7[5] = {0x01,0x71,0x09,0x05,0x03};
    static const uint8_t f_8[5] = {0x36,0x49,0x49,0x49,0x36}, f_9[5] = {0x06,0x49,0x49,0x29,0x1E};
    static const uint8_t f_A[5] = {0x7E,0x11,0x11,0x11,0x7E}, f_B[5] = {0x7F,0x49,0x49,0x49,0x36};
    static const uint8_t f_C[5] = {0x3E,0x41,0x41,0x41,0x22}, f_D[5] = {0x7F,0x41,0x41,0x22,0x1C};
    static const uint8_t f_E[5] = {0x7F,0x49,0x49,0x49,0x41}, f_F[5] = {0x7F,0x09,0x09,0x09,0x01};
    static const uint8_t f_G[5] = {0x3E,0x41,0x49,0x49,0x7A}, f_H[5] = {0x7F,0x08,0x08,0x08,0x7F};
    static const uint8_t f_I[5] = {0x00,0x41,0x7F,0x41,0x00}, f_J[5] = {0x20,0x40,0x41,0x3F,0x01};
    static const uint8_t f_K[5] = {0x7F,0x08,0x14,0x22,0x41}, f_L[5] = {0x7F,0x40,0x40,0x40,0x40};
    static const uint8_t f_M[5] = {0x7F,0x02,0x0C,0x02,0x7F}, f_N[5] = {0x7F,0x04,0x08,0x10,0x7F};
    static const uint8_t f_O[5] = {0x3E,0x41,0x41,0x41,0x3E}, f_P[5] = {0x7F,0x09,0x09,0x09,0x06};
    static const uint8_t f_Q[5] = {0x3E,0x41,0x51,0x21,0x5E}, f_R[5] = {0x7F,0x09,0x19,0x29,0x46};
    static const uint8_t f_S[5] = {0x46,0x49,0x49,0x49,0x31}, f_T[5] = {0x01,0x01,0x7F,0x01,0x01};
    static const uint8_t f_U[5] = {0x3F,0x40,0x40,0x40,0x3F}, f_V[5] = {0x1F,0x20,0x40,0x20,0x1F};
    static const uint8_t f_W[5] = {0x3F,0x40,0x38,0x40,0x3F}, f_X[5] = {0x63,0x14,0x08,0x14,0x63};
    static const uint8_t f_Y[5] = {0x07,0x08,0x70,0x08,0x07}, f_Z[5] = {0x61,0x51,0x49,0x45,0x43};

    switch (c) {
        case '0': return f_0; case '1': return f_1; case '2': return f_2; case '3': return f_3;
        case '4': return f_4; case '5': return f_5; case '6': return f_6; case '7': return f_7;
        case '8': return f_8; case '9': return f_9;
        case 'A': return f_A; case 'B': return f_B; case 'C': return f_C; case 'D': return f_D;
        case 'E': return f_E; case 'F': return f_F; case 'G': return f_G; case 'H': return f_H;
        case 'I': return f_I; case 'J': return f_J; case 'K': return f_K; case 'L': return f_L;
        case 'M': return f_M; case 'N': return f_N; case 'O': return f_O; case 'P': return f_P;
        case 'Q': return f_Q; case 'R': return f_R; case 'S': return f_S; case 'T': return f_T;
        case 'U': return f_U; case 'V': return f_V; case 'W': return f_W; case 'X': return f_X;
        case 'Y': return f_Y; case 'Z': return f_Z;
        case ':': return f_colon; case '-': return f_dash; case '.': return f_dot; case '/': return f_slash;
        default:  return f_space;
    }
}

static void oled_draw_text(uint8_t page, uint8_t col, const char *text, bool invert) {
    if (page >= 8 || col >= 128 || !text) return;
    uint8_t buf[130];
    int idx = 0;
    buf[idx++] = 0x40;
    for (int i = 0; text[i] != '\0' && (col + idx - 1) < 126; i++) {
        const uint8_t *g = font5x7_get(text[i]);
        for (int b = 0; b < 5 && (col + idx - 1) < 127; b++) {
            uint8_t v = g[b];
            buf[idx++] = invert ? (uint8_t)~v : v;
        }
        buf[idx++] = invert ? 0xFF : 0x00;
    }
    oled_cmd(0xB0 + page);
    oled_cmd(0x00 + (col & 0x0F));
    oled_cmd(0x10 + ((col >> 4) & 0x0F));
    i2c_master_write_to_device(I2C_PORT, OLED_ADDR, buf, idx, pdMS_TO_TICKS(100));
}

static void oled_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = OLED_SDA_PIN,
        .scl_io_num = OLED_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    i2c_param_config(I2C_PORT, &conf);
    i2c_driver_install(I2C_PORT, conf.mode, 0, 0, 0);

    uint8_t init_cmds[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00,
        0x40, 0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8,
        0xDA, 0x12, 0x81, 0xCF, 0xD9, 0xF1, 0xDB,
        0x40, 0xA4, 0xA6, 0xAF
    };
    for (size_t i = 0; i < sizeof(init_cmds); i++) {
        oled_cmd(init_cmds[i]);
    }
    oled_clear();
}

static void oled_show_footer(const char *alt_mesaj) {
    oled_clear_page(7);
    if (alt_mesaj && alt_mesaj[0] != '\0') {
        char ozet[21];
        int j = 0;
        for (int i = 0; alt_mesaj[i] != '\0' && j < 20; i++) {
            char c = alt_mesaj[i];
            if (c == '\r' || c == '\n' || c == '{' || c == '}' || c == '"' || c == ' ') {
                continue;
            }
            ozet[j++] = c;
        }
        ozet[j] = '\0';
        oled_draw_text(7, 2, ozet, false);
    }
}

static void menu_render(int secili_satir, const char *durum_mesaji, const char *alt_mesaj) {
    oled_clear();
    oled_draw_text(0, 2, durum_mesaji, false);

    oled_draw_bar(1, secili_satir == 0);
    oled_draw_text(1, 4, "1-NEM EKLE", secili_satir == 0);

    oled_draw_bar(3, secili_satir == 1);
    oled_draw_text(3, 4, "2-SULA", secili_satir == 1);

    oled_draw_bar(5, secili_satir == 2);
    oled_draw_text(5, 4, "3-SON KAYIT", secili_satir == 2);

    oled_show_footer(alt_mesaj);

    ESP_LOGI(TAG, "MENU [%s] | 1:NEM EKLE %s | 2:SULA %s | 3:SON KAYIT %s",
             durum_mesaji,
             secili_satir == 0 ? "<--" : "",
             secili_satir == 1 ? "<--" : "",
             secili_satir == 2 ? "<--" : "");
}

// --- HTTP istek islemleri ---
static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0) {
        if (http_resp_len + evt->data_len < (int)sizeof(http_resp_buf) - 1) {
            memcpy(http_resp_buf + http_resp_len, evt->data, evt->data_len);
            http_resp_len += evt->data_len;
            http_resp_buf[http_resp_len] = '\0';
        }
    }
    return ESP_OK;
}

static void send_http_request(const char *path, esp_http_client_method_t method,
                              const char *json_body, char *oled_ozet_out, size_t ozet_len) {
    char url[160];
    snprintf(url, sizeof(url), "%s%s", API_BASE_URL, path);
    http_resp_len = 0;
    http_resp_buf[0] = '\0';

    esp_http_client_config_t config = {
        .url = url,
        .method = method,
        .event_handler = http_event_handler,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (json_body != NULL) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, json_body, strlen(json_body));
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP %d -> %s", status, http_resp_buf);
        if (oled_ozet_out && ozet_len > 0) {
            if (http_resp_len > 0) {
                snprintf(oled_ozet_out, ozet_len, "H%d:%s", status, http_resp_buf);
            } else {
                snprintf(oled_ozet_out, ozet_len, "HTTP:%d", status);
            }
        }
    } else {
        ESP_LOGE(TAG, "HTTP istegi basarisiz: %s", esp_err_to_name(err));
        if (oled_ozet_out && ozet_len > 0) {
            snprintf(oled_ozet_out, ozet_len, "HATA:%s", esp_err_to_name(err));
        }
    }
    esp_http_client_cleanup(client);
}

// --- Wi-Fi baglantisi ---
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Wi-Fi IP alindi: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init_sta(void) {
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
}

static void rotary_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << ROTARY_CLK_PIN) | (1ULL << ROTARY_DT_PIN) | (1ULL << ROTARY_SW_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
}

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    oled_init();
    rotary_init();
    oled_draw_text(0, 2, "WIFI BAGLANIYOR", false);
    wifi_init_sta();

    int secili_satir = 0;
    int last_clk = gpio_get_level(ROTARY_CLK_PIN);
    int last_sw = gpio_get_level(ROTARY_SW_PIN);
    char son_ozet[64] = "HAZIR";
    menu_render(secili_satir, "HAZIR", son_ozet);

    while (1) {
        int clk = gpio_get_level(ROTARY_CLK_PIN);
        if (clk != last_clk && clk == 0) {
            int dt = gpio_get_level(ROTARY_DT_PIN);
            if (dt != clk) {
                secili_satir++;
            } else {
                secili_satir--;
            }
            if (secili_satir < 0) secili_satir = 0;
            if (secili_satir > 2) secili_satir = 2;
            menu_render(secili_satir, "SECILI", son_ozet);
        }
        last_clk = clk;

        int sw = gpio_get_level(ROTARY_SW_PIN);
        if (last_sw == 1 && sw == 0) {
            vTaskDelay(pdMS_TO_TICKS(50));
            if (gpio_get_level(ROTARY_SW_PIN) == 0) {
                menu_render(secili_satir, "GONDERILIYOR", "BEKLEYIN");
                vTaskDelay(pdMS_TO_TICKS(150));

                if (secili_satir == 0) {
                    send_http_request("/okuma-ekle", HTTP_METHOD_POST,
                                      "{\"sensor_id\": 1, \"okunan_deger\": 24.5, \"durum\": \"Normal\"}",
                                      son_ozet, sizeof(son_ozet));
                } else if (secili_satir == 1) {
                    send_http_request("/sulama-ekle", HTTP_METHOD_POST,
                                      "{\"vana_id\": 1, \"su_tuketimi_litre\": 15.0, \"tetikleme_tipi\": \"Manuel\"}",
                                      son_ozet, sizeof(son_ozet));
                } else if (secili_satir == 2) {
                    send_http_request("/son-kayit", HTTP_METHOD_GET, NULL,
                                      son_ozet, sizeof(son_ozet));
                }
                menu_render(secili_satir, "TAMAM", son_ozet);
            }
        }
        last_sw = sw;

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
