#include "esp_camera.h"
#include <WiFi.h>

// --- Wi-Fi ---
const char* ssid = "WIFI-CONNECTION-2";
const char* password = "ur4lwmur4lwm";

// --- Сервер ---
const char* serverHost = "192.168.1.103";
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
#define WAKEUP_PIN GPIO_NUM_0 

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

  // Використання OPI PSRAM для UXGA
  if (psramFound()) {
    config.frame_size = FRAMESIZE_UXGA; // 1600x1200
    config.jpeg_quality = 5;            // Максимальна якість
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
  } else {
    Serial.println("PSRAM not found! Check IDE settings.");
    return;
  }

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("Camera init failed");
    return;
  }

  // Тонке налаштування: вимикаємо RAW GMA
  sensor_t * s = esp_camera_sensor_get();
  if (s) {
    s->set_raw_gma(s, 0); // Вимикаємо гаму
    s->set_whitebal(s, 1);
    s->set_ae_level(s, 0);
  }
}

void handleServerResponse(String response) {
  Serial.println("Server response:");
  Serial.println(response);
}

void setup() {
  Serial.begin(115200);
  // S3 іноді потребує паузи для ініціалізації USB Serial
  delay(1000); 

  // --- BUTTON WAKEUP ---
  // На XIAO S3 кнопка BOOT на GPIO 0
  esp_sleep_enable_ext0_wakeup(WAKEUP_PIN, 0); 

  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT0) {
    Serial.println("Normal boot. Entering Deep Sleep (press BOOT button)...");
    delay(100);
    esp_deep_sleep_start();
  }

  Serial.println("Wakeup by button!");

  // --- Wi-Fi ---
  Serial.printf("Connecting to %s", ssid);
  WiFi.begin(ssid, password);
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 15000) {
    Serial.print(".");
    delay(300);
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nWiFi connection failed!");
    esp_deep_sleep_start();
  }
  Serial.printf("\nWiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());

  // --- Camera ---
  configCamera();
  Serial.println("Camera ready");
  delay(1000); // Час для автоекспозиції

  // --- Capture ---
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    esp_deep_sleep_start();
  }

  Serial.printf("Picture taken! Size: %zu bytes\n", fb->len);

  // --- Upload ---
  WiFiClient client;
  client.setTimeout(15000); // Збільшено для UXGA

  Serial.printf("Connecting to server %s:%d...\n", serverHost, serverPort);
  if (!client.connect(serverHost, serverPort)) {
    Serial.println("TCP connect failed!");
  } else {
    client.printf("POST %s HTTP/1.1\r\n", serverPath);
    client.printf("Host: %s\r\n", serverHost);
    client.println("Content-Type: image/jpeg");
    client.printf("Content-Length: %zu\r\n", fb->len);
    client.println("Connection: close");
    client.println();

    // Передаємо дані прямо з буфера камери без malloc (економимо пам'ять)
    size_t sent = 0;
    uint8_t *buf = fb->buf;
    size_t len = fb->len;
    
    while (sent < len) {
      size_t toSend = (len - sent > 1024) ? 1024 : (len - sent);
      client.write(buf + sent, toSend);
      sent += toSend;
    }

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

  esp_camera_fb_return(fb);

  // --- Power off sequence ---
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("Going to deep sleep...");
  delay(500);

  esp_deep_sleep_start();
}

void loop() {}