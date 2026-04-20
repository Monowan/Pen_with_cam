#include "esp_camera.h"
#include <WiFi.h>

// --- Налаштування ---
const char* ssid = "WIFI-CONNECTION-2";
const char* password = "ur4lwmur4lwm";
const char* serverHost = "192.168.1.102";
const uint16_t serverPort = 5000;
const char* serverPath = "/upload";

// --- Піни AI Thinker ---
#define PWDN_GPIO_NUM    32
#define RESET_GPIO_NUM   -1
#define XCLK_GPIO_NUM     0
#define SIOD_GPIO_NUM    26
#define SIOC_GPIO_NUM    27
#define Y9_GPIO_NUM      35
#define Y8_GPIO_NUM      34
#define Y7_GPIO_NUM      39
#define Y6_GPIO_NUM      36
#define Y5_GPIO_NUM      21
#define Y4_GPIO_NUM      19
#define Y3_GPIO_NUM      18
#define Y2_GPIO_NUM       5
#define VSYNC_GPIO_NUM   25
#define HREF_GPIO_NUM    23
#define PCLK_GPIO_NUM    22

bool initCamera(framesize_t size, int quality) {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
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
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  
  config.xclk_freq_hz = 10000000; // 10MHz
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = size;
  config.jpeg_quality = quality; 
  config.fb_count = 1;           // Суворо 1 буфер
  config.fb_location = CAMERA_FB_IN_DRAM; // Тільки внутрішня RAM
  config.grab_mode = CAMERA_GRAB_LATEST;

  return (esp_camera_init(&config) == ESP_OK);
}

void setup() {
  // 1. ЖИВЛЕННЯ КАМЕРИ (найперша дія)
  pinMode(PWDN_GPIO_NUM, OUTPUT);
  digitalWrite(PWDN_GPIO_NUM, LOW); 
  delay(500);

  // 2. СЕРІАЛ ПОРТ (після живлення)
  Serial.begin(115200);
  Serial.println("\n--- START TEST (NO PSRAM) ---");

  // 3. ІНІЦІАЛІЗАЦІЯ КАМЕРИ (до Wi-Fi)
  // Пробуємо XGA з якістю 20 (більше стиснення = менше пам'яті)
  if (!initCamera(FRAMESIZE_XGA, 20)) {
    Serial.println("XGA fail. Trying SVGA...");
    if (!initCamera(FRAMESIZE_SVGA, 15)) {
      Serial.println("SVGA fail. Trying VGA...");
      if (!initCamera(FRAMESIZE_VGA, 12)) {
        Serial.println("VGA fail. Hardware issue.");
        esp_deep_sleep_start();
      }
    }
  }
  Serial.println("Camera OK!");

  // 4. WI-FI
  Serial.printf("Connecting to %s", ssid);
  WiFi.begin(ssid, password);
  unsigned long startWiFi = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startWiFi < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nWiFi Failed!");
    esp_deep_sleep_start();
  }
  Serial.println("\nWiFi OK!");

  // 5. ЗАХОПЛЕННЯ КАДРУ
  camera_fb_t *fb = NULL;
  for(int i = 0; i < 3; i++) {
    fb = esp_camera_fb_get();
    if(i < 2 && fb) {
      esp_camera_fb_return(fb);
      delay(200);
    }
  }

  if (!fb) {
    Serial.println("Capture failed!");
    esp_deep_sleep_start();
  }
  Serial.printf("Taken! Size: %zu bytes\n", fb->len);

  // 6. ВІДПРАВКА (БЕЗ malloc)
  WiFiClient client;
  if (client.connect(serverHost, serverPort)) {
    client.printf("POST %s HTTP/1.1\r\n", serverPath);
    client.printf("Host: %s\r\n", serverHost);
    client.println("Content-Type: image/jpeg");
    client.printf("Content-Length: %u\r\n", fb->len);
    client.println("Connection: close");
    client.println();

    size_t sent = 0;
    while (sent < fb->len) {
      size_t toSend = min((size_t)1024, fb->len - sent);
      client.write(fb->buf + sent, toSend);
      sent += toSend;
    }
    
    Serial.println("Sent! Waiting for response...");
    while (client.connected() || client.available()) {
      if (client.available()) Serial.print((char)client.read());
    }
    client.stop();
  } else {
    Serial.println("Server connection failed");
  }

  // 7. ЗАВЕРШЕННЯ
  esp_camera_fb_return(fb);
  digitalWrite(PWDN_GPIO_NUM, HIGH);
  Serial.println("\nDone. Sleeping 10s...");
  delay(10000); // Пауза перед наступним тестом (або esp_deep_sleep_start() для кнопки)
}

void loop() {}