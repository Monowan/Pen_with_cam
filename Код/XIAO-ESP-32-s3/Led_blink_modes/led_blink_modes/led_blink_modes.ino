// XIAO ESP32-S3 — RGB LED з кількома режимами миготіння + вхід у режим завантаження.
//
// Кнопка на D0 (GPIO1) замикає пін на GND:
//   • коротке натискання → наступний режим
//   • утримання 1.6 с    → 3 повільні блимання білим = режим завантаження.
//                          Після цього просто натисни RST — прошивка зайде
//                          навіть без затискання BOOT.
//
// Яскравість — апаратний ШІМ ESP-IDF (ledc), як у вихідному проєкті Led_test.
#include "driver/ledc.h"
#include "esp32-hal.h"
#include "rom/gpio.h"
#include "driver/rtc_io.h"

// --- СВІТЛОДІОДИ (як у Led_test) ---
#define LED_RED   GPIO_NUM_8
#define LED_BLUE  GPIO_NUM_7
#define LED_GREEN GPIO_NUM_9

// --- КНОПКА ---
#define BTN_PIN         GPIO_NUM_1        // D0
#define BTN_ACTIVE_LOW  1                 // кнопка замикає на GND
#define BTN_DEBOUNCE_MS 25
#define BTN_HOLD_MS     1600              // утримання → режим завантаження
#define BTN_DEBUG       1                 // 1 = лог усіх подій кнопки у Serial

// --- ЯСКРАВІСТЬ (0-100%) ---
#define BRIGHT_RED   45
#define BRIGHT_GREEN 100
#define BRIGHT_BLUE  20

#define N_MODES 6

// --- Стан ---
int  mode = 1;                 // старт у режимі «усі три» — одразу видно, що ШІМ живий
int  modeBeforeOff = 1;
int  ledIdx = 0;
bool phase = false;
unsigned long lastStepMs = 0;

// стан кнопки
bool          btnStable  = false;
bool          btnLastRaw = false;
bool          btnLongFired = false;
unsigned long btnRawChangeMs = 0;
unsigned long btnPressMs = 0;

const ledc_channel_t CH_R = LEDC_CHANNEL_0;
const ledc_channel_t CH_G = LEDC_CHANNEL_1;
const ledc_channel_t CH_B = LEDC_CHANNEL_2;

const char* mode_name(int m) {
    switch (m) {
        case 0: return "вимкнено";
        case 1: return "усі три (біле)";
        case 2: return "червоний 1 Гц";
        case 3: return "зелений 5 Гц";
        case 4: return "синій подвійний спалах";
        case 5: return "веселка R->G->B";
        default: return "?";
    }
}

// ---------------------------------------------------------------------------
void set_led(ledc_channel_t channel, int percent) {
    if (percent > 100) percent = 100;
    if (percent < 0)   percent = 0;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, (uint32_t)percent * 255 / 100);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);
}

void rgb(int r, int g, int b) { set_led(CH_R, r); set_led(CH_G, g); set_led(CH_B, b); }
void all_off()                { rgb(0, 0, 0); }

void setup_pwm() {
    ledc_timer_config_t t = {};
    t.speed_mode      = LEDC_LOW_SPEED_MODE;
    t.duty_resolution = LEDC_TIMER_8_BIT;
    t.timer_num       = LEDC_TIMER_0;
    t.freq_hz         = 5000;
    t.clk_cfg         = LEDC_AUTO_CLK;
    ledc_timer_config(&t);

    const int pins[3] = { LED_RED, LED_GREEN, LED_BLUE };
    const ledc_channel_t chans[3] = { CH_R, CH_G, CH_B };
    for (int i = 0; i < 3; i++) {
        ledc_channel_config_t c = {};
        c.gpio_num   = pins[i];
        c.speed_mode = LEDC_LOW_SPEED_MODE;
        c.channel    = chans[i];
        c.intr_type  = LEDC_INTR_DISABLE;
        c.timer_sel  = LEDC_TIMER_0;
        c.duty       = 0;
        c.hpoint     = 0;
        ledc_channel_config(&c);
    }
}

// Отримати режим завантаження без затискання BOOT:
// тримаємо GPIO0 у нулі через rtc_gpio, скидаємо чип — ROM стартує в bootloader.
void enter_download_mode() {
    Serial.println("[BOOT] входимо в режим завантаження...");
    Serial.flush();

    // 3 повільні блимання
    for (int i = 0; i < 3; i++) {
        rgb(45, 100, 20); delay(180);
        all_off();        delay(180);
    }

    rtc_gpio_init(GPIO_NUM_0);
    rtc_gpio_set_direction(GPIO_NUM_0, RTC_GPIO_MODE_OUTPUT_ONLY);
    rtc_gpio_set_level(GPIO_NUM_0, 0);
    delay(50);

    Serial.flush();
    delay(50);
    esp_restart();   // ROM бачить GPIO0=0 і стартує в bootloader
    while (true) {}
}

void next_mode() {
    if (mode == 0) {
        mode = modeBeforeOff;
    } else {
        modeBeforeOff = mode;
        mode = (mode == N_MODES - 1) ? 0 : (mode + 1);
        if (mode == 0) modeBeforeOff = N_MODES - 1;
    }
    ledIdx = 0;
    phase = false;
    lastStepMs = 0;
    all_off();
    Serial.printf("[MODE] -> %s\n", mode_name(mode));
}

void blink_startup() {
    for (int i = 0; i < 2; i++) {
        rgb(45, 100, 20); delay(120);
        all_off();        delay(120);
    }
}

// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(400);
    setup_pwm();
    pinMode(BTN_PIN, INPUT_PULLUP);
    all_off();

    unsigned long t0 = millis();
    btnLastRaw = (digitalRead(BTN_PIN) == LOW);
    Serial.println();
    Serial.println("=== LED blink modes: кнопка D0 (GPIO1) ===");
    Serial.printf("boot: D0 raw=%s, UBLOOT(0)=%d\n", btnLastRaw ? "LOW (натиснута)" : "HIGH", digitalRead(GPIO_NUM_0));
    blink_startup();
    Serial.printf("[MODE] старт -> %s (setup %lu ms)\n", mode_name(mode), millis() - t0);
}

void loop() {
    unsigned long now = millis();

    // ---- антибрязкіт ----
    bool rawPressed = BTN_ACTIVE_LOW ? (digitalRead(BTN_PIN) == LOW) : (digitalRead(BTN_PIN) == HIGH);
    if (rawPressed != btnLastRaw) { btnLastRaw = rawPressed; btnRawChangeMs = now; }

    if ((now - btnRawChangeMs) >= BTN_DEBOUNCE_MS && rawPressed != btnStable) {
        btnStable = rawPressed;
        if (btnStable) {
            btnLongFired = false;
            btnPressMs = now;
            if (BTN_DEBUG) Serial.printf("[BTN] down  t=%lu\n", now);
        } else {
            unsigned long held = now - btnPressMs;
            if (!btnLongFired) {
                if (BTN_DEBUG) Serial.printf("[BTN] up  held=%lu ms -> next mode\n", held);
                next_mode();
            } else if (BTN_DEBUG) {
                Serial.printf("[BTN] up  held=%lu ms (long fired)\n", held);
            }
        }
    }

    // ---- утримання ----
    if (btnStable && !btnLongFired && (now - btnPressMs) >= BTN_HOLD_MS) {
        btnLongFired = true;
        enter_download_mode();
    }

    // ---- індикація утримання: зелений 0..100% за прогресом ----
    if (btnStable && !btnLongFired) {
        int p = (int)((now - btnPressMs) * 100 / BTN_HOLD_MS);
        if (p > 100) p = 100;
        set_led(CH_G, p);
        set_led(CH_R, (p > 60) ? 20 : 0);
        set_led(CH_B, 0);
        return;
    }

    // ---- ефекти ----
    switch (mode) {
        case 0: all_off(); break;

        case 1:
            rgb(BRIGHT_RED, BRIGHT_GREEN, BRIGHT_BLUE);
            break;

        case 2:  // 1 Гц
            if (now - lastStepMs >= 500) {
                lastStepMs = now; phase = !phase;
                if (phase) rgb(BRIGHT_RED, 0, 0); else all_off();
            }
            break;

        case 3:  // 5 Гц
            if (now - lastStepMs >= 100) {
                lastStepMs = now; phase = !phase;
                if (phase) rgb(0, BRIGHT_GREEN, 0); else all_off();
            }
            break;

        case 4:  // подвійний спалах: 0.1+0.1+0.1 сплеск, 0.7 пауза = 1.0 с
            if (now - lastStepMs >= 100) {
                lastStepMs = now;
                static int step = 0;
                if (step == 0 || step == 2) rgb(0, 0, BRIGHT_BLUE); else all_off();
                step = (step + 1) % 10;
            }
            break;

        case 5:  // веселка
            if (now - lastStepMs >= 400) {
                lastStepMs = now;
                if (ledIdx == 0)      rgb(BRIGHT_RED, 0, 0);
                else if (ledIdx == 1) rgb(0, BRIGHT_GREEN, 0);
                else                  rgb(0, 0, BRIGHT_BLUE);
                ledIdx = (ledIdx + 1) % 3;
            }
            break;
    }
}
