// ESP32-CAM POST example — send photo as image/jpeg in body
#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>

// ==== Wi-Fi ====
const char* ssid = "WIFI-CONNECTION-2";
const char* password = "ur4lwmur4lwm";

// ==== server URL ====
const char* serverUrl = "http://192.168.1.102:5000/upload"; // <- Змініть на IP вашого сервера

// --- AI-Thinker pins ---
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println("ESP32-CAM POST example starting...");

  // Connect WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
    if (millis() - start > 20000) { // тайм-аут 20s
      Serial.println("\nFailed to connect to WiFi");
      return;
    }
  }
  Serial.println("\nWiFi connected: " + WiFi.localIP().toString());

  // Camera config
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
  config.xclk_freq_hz = 10000000; // зменшена частота для стабільності
  config.pixel_format = PIXFORMAT_JPEG;

  // Підбір розміру (якщо є PSRAM — можна більший)
  if (psramFound()) {
    config.frame_size = FRAMESIZE_VGA;
    config.fb_count = 1;
    config.jpeg_quality = 12;
  } else {
    config.frame_size = FRAMESIZE_QVGA; // без PSRAM - QVGA для стабільності
    config.fb_count = 1;
    config.jpeg_quality = 12;
  }

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("Camera init failed");
    return;
  }
  delay(200); // дати стабілізацію

  // Зробити фото і надіслати
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    return;
  }

  Serial.printf("Captured: %u bytes\n", fb->len);

  // POST запит з тілом = JPEG
  HTTPClient http;
  http.begin(serverUrl);
  http.addHeader("Content-Type", "image/jpeg");

  // Використовуємо POST з вказанням довжини масиву (подавати як uint8_t*, size)
  int httpCode = http.POST((uint8_t*)fb->buf, fb->len);
  if (httpCode > 0) {
    Serial.printf("HTTP POST code: %d\n", httpCode);
    String resp = http.getString();
    Serial.println("Server response: " + resp);
  } else {
    Serial.printf("HTTP POST failed, err: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();

  esp_camera_fb_return(fb);
  esp_camera_deinit(); // деініціалізувати камеру, звільнити пам'ять
  Serial.println("Done. Reset device to capture/send again.");
}

void loop() {
  // Нічого не робимо
}
