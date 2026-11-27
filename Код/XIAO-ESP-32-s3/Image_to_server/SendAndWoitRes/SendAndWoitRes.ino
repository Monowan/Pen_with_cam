#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include "driver/ledc.h"

// --- Типи LED-скрипта: ОГОЛОШЕНІ ДО БУДЬ-ЯКИХ ФУНКЦІЙ ---
enum AnimMode { AM_ON, AM_BLINK, AM_SPARK };

struct LedStep {
  uint8_t  r, g, b;      // яскравість 0-100
  AnimMode mode;
  uint32_t ms;           // ON/BL: тривалість кроку; SP: тривалість спалаху
  uint32_t gap;          // SP: пауза між спалахами
  uint16_t reps;         // SP: кількість спалахів
};

// --- Wi-Fi ---
const char* ssid = "WIFI-CONNECTION-4";
const char* password = "ur4lwmur4lwm";

// --- Сервер ---
const char* serverHost = "192.168.141.2";
const uint16_t serverPort = 5000;
const char* serverPath = "/upload";

// --- XIAO ESP32S3 Sense Pins ---
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     10
#define SIOD_GPIO_NUM     40
#define SIOC_GPIO_NUM     39

#define Y9_GPIO_NUM       48
#define Y8_GPIO_NUM       11
#define Y7_GPIO_NUM       12
#define Y6_GPIO_NUM       14
#define Y5_GPIO_NUM       16
#define Y4_GPIO_NUM       18
#define Y3_GPIO_NUM       17
#define Y2_GPIO_NUM       15

#define VSYNC_GPIO_NUM    38
#define HREF_GPIO_NUM     47
#define PCLK_GPIO_NUM     13

// Пін кнопки для XIAO S3 (зазвичай D1 або вбудована, але для EXT0 краще обрати GPIO з підтримкою RTC)
// На XIAO S3 кнопка BOOT підключена до GPIO 0.
//#define WAKEUP_PIN GPIO_NUM_0 
#define WAKEUP_PIN GPIO_NUM_1

// ===========================================================================
//  RGB СВІТЛОДІОДИ — апаратний ШІМ ESP-IDF (як у проєкті Led_test)
// ===========================================================================
#define LED_RED   GPIO_NUM_8
#define LED_GREEN GPIO_NUM_9
#define LED_BLUE  GPIO_NUM_7

#define CH_R LEDC_CHANNEL_1
#define CH_G LEDC_CHANNEL_2
#define CH_B LEDC_CHANNEL_3

static void setup_led_pwm() {
  ledc_timer_config_t t = {};
  t.speed_mode      = LEDC_LOW_SPEED_MODE;   // ESP32-S3: тільки Low Speed
  t.duty_resolution = LEDC_TIMER_8_BIT;      // 0-255
  t.timer_num       = LEDC_TIMER_1;          // TIMER_0 зайнятий камерою
  t.freq_hz         = 5000;
  t.clk_cfg         = LEDC_AUTO_CLK;
  ledc_timer_config(&t);

  const int pins[3] = { LED_RED, LED_GREEN, LED_BLUE };
  const ledc_channel_t chans[3] = { CH_R, CH_G, CH_B };
  for (int i = 0; i < 3; i++) {
    ledc_channel_config_t c = {};   // ОБОВ'ЯЗКОВО нульова ініціалізація
    c.gpio_num   = pins[i];
    c.speed_mode = LEDC_LOW_SPEED_MODE;
    c.channel    = chans[i];
    c.intr_type  = LEDC_INTR_DISABLE;
    c.timer_sel  = LEDC_TIMER_1;
    c.duty       = 0;
    c.hpoint     = 0;
    ledc_channel_config(&c);
  }
}

void set_led_ch(ledc_channel_t ch, int percent) {
  if (percent > 100) percent = 100;
  if (percent < 0)   percent = 0;
  ledc_set_duty(LEDC_LOW_SPEED_MODE, ch, (uint32_t)percent * 255 / 100);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, ch);
}

// Прототипи: викликаються до свого визначення
void set_brightness(int r, int g, int b);
void feed_wdt();

void set_brightness(int r, int g, int b) {
  set_led_ch(CH_R, r);
  set_led_ch(CH_G, g);
  set_led_ch(CH_B, b);
}

// Віддає квант часу системі під час довгих анімацій:
// не дає спрацювати task watchdog, якщо він увімкнений у Tools.
void feed_wdt() {
  yield();
}

// ===========================================================================
// --- Сигнал "кадр зроблено": зовсім короткий блік червоного ---
const unsigned long SHOT_BLINK_MS = 40UL;    // 40 мс = майже мить

static void blink_shot() {
  set_brightness(45, 0, 0);
  delay(SHOT_BLINK_MS);
  set_brightness(0, 0, 0);
}

// ===========================================================================
// --- Таймер бездіяльності ---
const unsigned long IDLE_SLEEP_MS = 60000UL;   // 1 хв без натискань -> deep sleep
const unsigned long DEBOUNCE_MS   = 50;        // антибрязкіт кнопки

// --- Стан ---
bool          wakeup_ready   = false;  // ВАЖЛИВО: вже "розбудились" і обробили одне фото
bool          cameraStarted  = false;
bool          btnStable      = false;  // true = натиснута
bool          btnLastRaw     = false;
unsigned long btnRawChangeMs = 0;
unsigned long lastActivityMs = 0;

void configCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_LATEST;

  // Використання OPI PSRAM для FHD
  if (psramFound()) {
    config.frame_size = FRAMESIZE_FHD;  // 1920x1080
    config.jpeg_quality = 30;           // 0..63, менше = краще (30 = помірний стиск)
    config.fb_count = 4;                // 4 кадри в черзі: 3 із завданням + запас
    config.fb_location = CAMERA_FB_IN_PSRAM;
  } else {
    Serial.println("PSRAM not found! Check IDE settings.");
    return;
  }

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("Camera init failed");
    return;
  }

  // Тонке налаштування сенсора
  sensor_t * s = esp_camera_sensor_get();
  if (s) {
    s->set_brightness(s, 0);   // яскравість 0 (-2..2, 0 = за замовчуванням)
    s->set_ae_level(s, -2);    // компенсація експозиції -2 (-2..2)
    s->set_raw_gma(s, 0);      // GMA (гама) вимкнена
    s->set_whitebal(s, 1);
    s->set_hmirror(s, 1);      // дзеркально по горизонталі (ліво <-> право)
  }
  cameraStarted = true;
}

// ===========================================================================
//  ОБРОБНИК ВІДПОВІДІ СЕРВЕРА + СВІТЛОДІОДНА АНІМАЦІЯ
// ===========================================================================
// Формат рядка (регістр не важливий, пробіли довільні), розділювач кроків ';':
//
//   LED:<колір>:<режим>:<мс>[:<пауза>[:<повтори>]] ; <наступний крок> ; ...
//
//   колір  : R | G | B | W | Y | C | M        (R/G/B = піни 8/9/7, W = всі три)
//   режим  : ON   — світиться постійно протягом <мс>
//            BL   — миготіння із заданим періодом <мс> (повний період)
//            SP   — спалах <мс> світла + <пауза> темряви, повторити <повтори> разів
//   мс     : час у мілісекундах (для SP — тривалість самого спалаху)
//   пауза  : тільки для SP, мс темряви між спалахами (за замовч. = мс)
//   повтори: тільки для SP, скільки разів (за замовч. 1)
//
// Приймається або "LED:..." або коротке "R:ON:500".
// Розмір поза межами 30 мс..30 с обрізається.
//
// Приклад (алгоритм із 4 кроків):
//   LED:R:ON:300;LED:G:ON:300;LED:B:ON:300;LED:W:SP:100:200:3
//
// Будь-який інший текст (наприклад "3" або "1 4 5") просто логується, LED не чіпає.
// ===========================================================================



#define LED_MS_MIN  30UL
#define LED_MS_MAX  30000UL
#define LED_MAX_STEPS 12          // максимум кроків алгоритму за один удар
#define LED_DEFAULT_BRIGHT 100    // яскравість для BL/SP (у Led_test 45/100/20)

bool server_pending_anim = false;     // знайдено валідний LED-рядок -> треба відтворити

LedStep anim[LED_MAX_STEPS];
int     animLen = 0;

static int count_char(const String& s, char what) {
  int n = 0;
  for (int i = 0; i < (int)s.length(); i++) if (s[i] == what) n++;
  return n;
}

void handleServerResponse(String response) {
  Serial.println("Server response:");
  Serial.println(response);
  // Діагностика: скільки байт тіла дійшло і як у ньому закодовані переводи.
  // Монітор порту часто «з'їдає» переводи рядків, тому друкуємо позиції EOL.
  Serial.printf("[RX] %d bytes, CR=%d LF=%d\n", (int)response.length(),
                (int)count_char(response, '\r'), (int)count_char(response, '\n'));

  // Тіло відповіді може йти після заголовків — відрізаємо все до \r\n\r\n
  int hdr_end = response.indexOf("\r\n\r\n");
  String body = (hdr_end >= 0) ? response.substring(hdr_end + 4) : response;

  // Позначка "[ОДНА] " означає: відповідь є, але її не підтвердила інша модель.
  // Для роботи з тілом її знімаємо, в лог виводимо станом.
  if (body.startsWith("[ОДНА] ")) {
    Serial.println("[AI] WARNING: single model answer, not confirmed");
    body.remove(0, strlen("[ОДНА] "));
  }

  if (parse_led_script(body) > 0) {
    server_pending_anim = true;
    Serial.printf("[LED] script parsed, %d steps, will run after upload cycle\n", animLen);
  } else if (body.startsWith("!FAIL")) {
    // Відмова: сам текст відповіді — '!FAIL: причина'.
    // Індикацію світлодіодом задає LED-скрипт від сервера (він є в тому ж тілі).
    Serial.println("[AI] no confident answer (refusal)");
  } else {
    Serial.println("[LED] no LED script in response (answer only)");
  }
}

// --- Розбір ---
static bool parse_color(const String& s, uint8_t* r, uint8_t* g, uint8_t* b) {
  if (s == "R") { *r = 45;  *g = 0;   *b = 0;   }   // яскравості як у Led_test
  else if (s == "G") { *r = 0;   *g = 100; *b = 0; }
  else if (s == "B") { *r = 0;   *g = 0;   *b = 20;  }
  else if (s == "W") { *r = 45;  *g = 100; *b = 20;  }
  else if (s == "Y") { *r = 45;  *g = 100; *b = 0;   }
  else if (s == "C") { *r = 0;   *g = 100; *b = 20;  }
  else if (s == "M") { *r = 45;  *g = 0;   *b = 20;  }
  else return false;
  return true;
}

static void trim_upper(String& s) {
  s.trim();
  s.toUpperCase();
}

static bool parse_step(String tok, LedStep* out) {
  // токен виду "R:ON:300" або з префіксом "LED:"
  trim_upper(tok);
  if (tok.startsWith("LED:")) tok = tok.substring(4);

  // розбиваємо на поля по ':'
  String f[5];
  int nf = 0, from = 0;
  while (nf < 5) {
    int p = tok.indexOf(':', from);
    if (p < 0) { f[nf++] = tok.substring(from); break; }
    f[nf++] = tok.substring(from, p);
    from = p + 1;
  }
  if (nf < 3) return false;

  if (!parse_color(f[0], &out->r, &out->g, &out->b)) return false;

  const String& m = f[1];
  if (m == "ON" || m == "BL") {
    if (m == "ON") out->mode = AM_ON; else out->mode = AM_BLINK;
    out->ms   = f[2].toInt();
    out->gap  = out->ms;
    out->reps = 1;
  } else if (m == "SP") {
    out->mode = AM_SPARK;
    out->ms   = f[2].toInt();
    out->gap  = (nf > 3 && f[3].length()) ? f[3].toInt() : out->ms;
    out->reps = (nf > 4 && f[4].length()) ? f[4].toInt() : 1;
    if (out->reps == 0) out->reps = 1;
  } else {
    return false;
  }

  if (out->ms < LED_MS_MIN) out->ms = LED_MS_MIN;
  if (out->ms > LED_MS_MAX) out->ms = LED_MS_MAX;
  if (out->gap < LED_MS_MIN) out->gap = LED_MS_MIN;
  if (out->gap > LED_MS_MAX) out->gap = LED_MS_MAX;
  return true;
}

// Парсер усього рядка -> anim[]. Повертає кількість кроків (0 = не той формат).
int parse_led_script(const String& text) {
  animLen = 0;

  // У тілі спершу йде сама відповідь, а LED-скрипт - з нового рядка.
  // Нормалізуємо будь-які переводи рядків у ';', щоб "2\r\nLED:..." теж працювало,
  // і прибираємо префікс "[ОДНА] " на початку.
  String norm = text;
  norm.replace("\r\n", ";");
  norm.replace("\r", ";");
  norm.replace("\n", ";");
  norm.trim();
  if (norm.startsWith("[ОДНА] ")) norm.remove(0, 7);

  if (norm.indexOf("LED:") < 0 && norm.indexOf("ON:") < 0 && norm.indexOf("SP:") < 0) {
    return 0;
  }

  int from = 0;
  while (animLen < LED_MAX_STEPS && from <= (int)norm.length()) {
    int p = norm.indexOf(';', from);
    String tok = (p < 0) ? norm.substring(from) : norm.substring(from, p);
    from = (p < 0) ? norm.length() + 1 : p + 1;

    trim_upper(tok);
    if (tok.length() == 0) continue;

    LedStep st;
    if (parse_step(tok, &st)) {
      anim[animLen++] = st;
    } else {
      Serial.printf("[LED] skipped token: %s\n", tok.c_str());
    }
  }
  return animLen;
}

// --- Відтворення ---
void led_play(const LedStep& st) {
  switch (st.mode) {
    case AM_ON:
      set_brightness(st.r, st.g, st.b);
      delay(st.ms);
      break;

    case AM_BLINK: {
      // <мс> = повний період: половина світла, половина темряви
      uint32_t half = st.ms / 2;
      if (half < LED_MS_MIN / 2) half = LED_MS_MIN / 2;
      set_brightness(st.r, st.g, st.b); delay(half);
      set_brightness(0, 0, 0);          delay(half);
      break;
    }

    case AM_SPARK:
      for (uint16_t k = 0; k < st.reps; k++) {
        set_brightness(st.r, st.g, st.b); delay(st.ms);
        set_brightness(0, 0, 0);          delay(st.gap);
      }
      break;
  }
  set_brightness(0, 0, 0);
}

void led_play_anim() {
  Serial.printf("[LED] playing %d step(s)\n", animLen);
  for (int i = 0; i < animLen; i++) {
    led_play(anim[i]);
    // Годуємо watchdog і тримаємо таймер простою живим, поки триває анімація
    feed_wdt();
    lastActivityMs = millis();
  }
  set_brightness(0, 0, 0);
  Serial.println("[LED] done");
}



// Wi-Fi під'єднується один раз і тримається до глибокого сну.
bool wifi_ensure() {
  if (WiFi.status() == WL_CONNECTED) return true;

  Serial.printf("Connecting to %s", ssid);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);          // без modem-sleep — стабільніше з'єднання
  WiFi.begin(ssid, password);
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 15000) {
    Serial.print(".");
    delay(300);
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nWiFi connection failed! (retry next press)");
    return false;
  }
  Serial.printf("\nWiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());
  return true;
}

// Скільки кадрів робити за одне натискання і яка пауза між ними.
// Мінімальна затримка: камера з fb_count=4 тримає кадри напоготові,
// тому наступний esp_camera_fb_get() віддає вже готовий кадр без очікування.
const int           SHOT_COUNT = 3;
const unsigned long SHOT_GAP_MS = 70UL;      // мс між кадрами (ледь помітна пауза)

// Один повний цикл: (Wi-Fi вже є) -> фото -> upload.
// Wi-Fi НЕ вимикається: з'єднання живе до глибокого сну.
// Повертає true, якщо досягли кінця (успішно чи ні — не важливо).
// Сам по собі НЕ засинає.
bool do_capture_cycle() {
  // --- Wi-Fi ---
  if (!wifi_ensure()) {
    return false;
  }

  // --- Camera (лише перший раз) ---
  if (!cameraStarted) {
    configCamera();
    Serial.println("Camera ready");
    delay(1000); // Час для автоекспозиції
  }

  // --- Capture (кілька кадрів підряд, усі йдуть на сервер) ---
  camera_fb_t *frames[SHOT_COUNT];
  int shotCount = 0;

  for (int i = 0; i < SHOT_COUNT; i++) {
    if (i > 0) delay(SHOT_GAP_MS);
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      Serial.printf("Picture %d/%d failed\n", i + 1, SHOT_COUNT);
      break;
    }
    frames[shotCount++] = fb;
    Serial.printf("Picture %d/%d taken! Size: %zu bytes\n", i + 1, SHOT_COUNT, fb->len);
    blink_shot();   // коротюсінький блік: кадр реально зроблено
  }

  if (shotCount == 0) {
    Serial.println("Camera capture failed");
    return false;
  }

  // Звільняємо всі буфери на будь-якому виході з функції
  auto release_frames = [&]() {
    for (int i = 0; i < shotCount; i++) {
      if (frames[i]) { esp_camera_fb_return(frames[i]); frames[i] = nullptr; }
    }
  };

  // --- Upload (multipart/form-data, усі кадри в одному запиті) ---
  WiFiClient client;
  client.setTimeout(15000); // Збільшено для FHD

  // Межова лінія multipart і попередній підрахунок Content-Length,
  // щоб надіслати один запит без chunked-кодування.
  String boundary = "----PTB" + String((unsigned long)millis(), HEX);
  size_t bodyLen = 0;
  for (int i = 0; i < shotCount; i++) {
    char head[160];
    int n = snprintf(head, sizeof(head),
                     "--%s\r\nContent-Disposition: form-data; name=\"image%d\"; "
                     "filename=\"shot%d.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n",
                     boundary.c_str(), i, i);
    bodyLen += (size_t)n + frames[i]->len + 2;   // + CRLF після даних
  }
  // "--" + boundary + "--\r\n" = 2 + len + 2 + 2
  size_t tailLen = boundary.length() + 6;
  bodyLen += tailLen;

  Serial.printf("Connecting to server %s:%d...\n", serverHost, serverPort);
  if (!client.connect(serverHost, serverPort)) {
    Serial.println("TCP connect failed!");
  } else {
    client.printf("POST %s HTTP/1.1\r\n", serverPath);
    client.printf("Host: %s\r\n", serverHost);
    client.printf("Content-Type: multipart/form-data; boundary=%s\r\n", boundary.c_str());
    client.printf("Content-Length: %zu\r\n", bodyLen);
    client.println("Connection: close");
    client.println();

    for (int i = 0; i < shotCount; i++) {
      char head[160];
      int n = snprintf(head, sizeof(head),
                       "--%s\r\nContent-Disposition: form-data; name=\"image%d\"; "
                       "filename=\"shot%d.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n",
                       boundary.c_str(), i, i);
      client.write((const uint8_t *)head, (size_t)n);

      // Передаємо дані прямо з буфера камери без malloc (економимо пам'ять)
      size_t sent = 0;
      uint8_t *buf = frames[i]->buf;
      size_t len = frames[i]->len;
      while (sent < len) {
        size_t toSend = (len - sent > 1024) ? 1024 : (len - sent);
        client.write(buf + sent, toSend);
        sent += toSend;
      }
      client.print("\r\n");
      Serial.printf("Uploaded frame %d/%d (%zu bytes)\n", i + 1, shotCount, len);
    }

    client.printf("--%s--\r\n", boundary.c_str());
    Serial.println("Upload done, waiting for response...");

    String response = "";
    while (client.connected() || client.available()) {
      if (client.available()) {
        char c = client.read();
        response += c;
      }
    }
    handleServerResponse(response);
    client.stop();
  }

  release_frames();

  return true;
}

// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  // S3 іноді потребує паузи для ініціалізації USB Serial
  delay(1000); 

  pinMode(WAKEUP_PIN, INPUT_PULLUP);
  btnLastRaw = (digitalRead(WAKEUP_PIN) == LOW);
  btnStable  = btnLastRaw;

  setup_led_pwm();          // RGB LED через ledc
  set_brightness(0, 0, 0);

  // --- BUTTON WAKEUP ---
  // На XIAO S3 кнопка BOOT на GPIO 0
  esp_sleep_enable_ext0_wakeup(WAKEUP_PIN, 0); 

  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT0) {
    Serial.println("Normal boot. Entering Deep Sleep (press BOOT button)...");
    Serial.flush();
    delay(100);
    esp_deep_sleep_start();
  }

  Serial.println("Wakeup by button!");
  wakeup_ready = true;

  // Wi-Fi піднімаємо одразу після пробудження і тримаємо до глибокого сну
  wifi_ensure();

  // Перше фото одразу після пробудження — як було
  do_capture_cycle();

  // Таймер бездіяльності стартує після завершення циклу
  lastActivityMs = millis();
  Serial.printf("Idle timer started: sleep after %lu s без натискань; WiFi %s\n",
                IDLE_SLEEP_MS / 1000,
                WiFi.status() == WL_CONNECTED ? "CONNECTED (kept alive)" : "offline");
}

void loop() {
  // --- Кнопка: коротке натискання = ще одне фото (без сну) ---
  bool raw = (digitalRead(WAKEUP_PIN) == LOW);
  unsigned long now = millis();
  if (raw != btnLastRaw) {
    btnLastRaw = raw;
    btnRawChangeMs = now;
  }
  if ((now - btnRawChangeMs) >= DEBOUNCE_MS && raw != btnStable) {
    btnStable = raw;
    if (!btnStable) {
      // Відпускання = натискання (як у вихідній логіці — кнопка на GND)
      Serial.println("[BTN] pressed -> capture again");
      do_capture_cycle();
      Serial.printf("[NET] WiFi still %s\n",
                    WiFi.status() == WL_CONNECTED ? "connected" : "disconnected");

      // Якщо сервер прислав алгоритм блимання — відтворюємо його
      if (server_pending_anim) {
        server_pending_anim = false;
        led_play_anim();
        Serial.println("[LED] animation finished");
      }
    }
    lastActivityMs = millis();
  }

  // Якщо зв'язок все ж впав (роутер перезавантажився) — тихо відновлюємо,
  // не чекаючи наступного натискання
  if (WiFi.status() != WL_CONNECTED && (millis() - lastActivityMs) < IDLE_SLEEP_MS) {
    static unsigned long lastRetry = 0;
    if (now - lastRetry > 5000) {
      lastRetry = now;
      Serial.println("[NET] WiFi lost -> reconnect");
      wifi_ensure();
    }
  }

  // --- Сон ТІЛЬКИ за таймером бездіяльності ---
  if (millis() - lastActivityMs >= IDLE_SLEEP_MS) {
    Serial.printf("No activity for %lu s -> going to deep sleep...\n", IDLE_SLEEP_MS / 1000);
    Serial.flush();
    delay(50);
    esp_sleep_enable_ext0_wakeup(WAKEUP_PIN, 0);
    esp_deep_sleep_start();
  }

  delay(10);
}
