// Код для XIAO ESP32S3 без Arduino-бібліотек для ШІМ (використання ESP-IDF драйвера)
#include "driver/ledc.h"

#define GND_PIN   
#define LED_RED   GPIO_NUM_8  
#define LED_BLUE  GPIO_NUM_7
#define LED_GREEN GPIO_NUM_9

// --- НАЛАШТУВАННЯ ЯСКРА45СТІ (0-100%) ---
#define BRIGHT_RED   45
#define BRIGHT_GREEN 100
#define BRIGHT_BLUE  20
// ----------------------------------------

void setup_pwm() {
    // 1. Конфігурація таймера ШІМ
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE, // ESP32S3 підтримує тільки Low Speed Mode
        .duty_resolution  = LEDC_TIMER_8_BIT,    // 0-255
        .timer_num        = LEDC_TIMER_0,
        .freq_hz          = 5000,                // 5 кГц
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);

    // 2. Налаштування каналів для кожного кольору
    ledc_channel_config_t ledc_channel[3] = {
        {
            .gpio_num       = LED_RED,
            .speed_mode     = LEDC_LOW_SPEED_MODE,
            .channel        = LEDC_CHANNEL_0,
            .intr_type      = LEDC_INTR_DISABLE,
            .timer_sel      = LEDC_TIMER_0,
            .duty           = 0,
            .hpoint         = 0
        },
        {
            .gpio_num       = LED_GREEN,
            .speed_mode     = LEDC_LOW_SPEED_MODE,
            .channel        = LEDC_CHANNEL_1,
            .intr_type      = LEDC_INTR_DISABLE,
            .timer_sel      = LEDC_TIMER_0,
            .duty           = 0,
            .hpoint         = 0
        },
        {
            .gpio_num       = LED_BLUE,
            .speed_mode     = LEDC_LOW_SPEED_MODE,
            .channel        = LEDC_CHANNEL_2,
            .intr_type      = LEDC_INTR_DISABLE,
            .timer_sel      = LEDC_TIMER_0,
            .duty           = 0,
            .hpoint         = 0
        }
    };

    for (int i = 0; i < 3; i++) {
        ledc_channel_config(&ledc_channel[i]);
    }
}

void set_led_brightness(ledc_channel_t channel, int percent) {
    // Обмежуємо 0-100 і конвертуємо у 8-біт (0-255)
    if (percent > 100) percent = 100;
    if (percent < 0) percent = 0;
    
    uint32_t duty = (percent * 255) / 100;
    
    // Прямий запис у регістри через драйвер
    ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);
}

void setup() {
    // pinMode(GND_PIN, OUTPUT);
    // digitalWrite(GND_PIN, LOW);
    Serial.begin(115200);
    setup_pwm();
    Serial.println("Hardware PWM (LEDC) initialized without Arduino high-level libs.");
}

void loop() {
    // Використовуємо константи
    set_led_brightness(LEDC_CHANNEL_0, BRIGHT_RED);
    set_led_brightness(LEDC_CHANNEL_1, BRIGHT_GREEN);
    set_led_brightness(LEDC_CHANNEL_2, BRIGHT_BLUE);
    
    delay(2000);
}