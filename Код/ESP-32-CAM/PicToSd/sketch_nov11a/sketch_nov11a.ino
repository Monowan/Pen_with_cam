#include "esp_camera.h"
#include <WiFi.h>

// --- Wi-Fi ---
const char* ssid = "WIFI-CONNECTION-4";
const char* password = "ur4lwmur4lwm";

// --- Сервер ---
const char* serverHost = "192.168.182.112";
const uint16_t serverPort = 5000;
const char* serverPath = "/upload";

// --- Camera AI Thinker ---
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

void configCamera() {
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
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_XGA;   // 320x240, стабільно QVGA
  config.jpeg_quality = 15;
  config.fb_count = 2;

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("Camera init failed");
    while (true);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  // --- Wi-Fi ---
  Serial.printf("Connecting to %s", ssid);
  WiFi.begin(ssid, password);
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 15000) {
    Serial.print(".");
    delay(500);
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nWiFi connection failed!");
    esp_deep_sleep_start();
  }

  Serial.printf("\nWiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());

  // --- Camera ---
  configCamera();
  Serial.println("Camera ready");

  // --- Capture ---
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    esp_deep_sleep_start();
  }

  Serial.printf("Picture taken! Size: %zu bytes\n", fb->len);

  // --- Copy to heap (звільняємо PSRAM) ---
  uint8_t *imgBuf = (uint8_t*)malloc(fb->len);
  if (!imgBuf) {
    Serial.println("Malloc failed");
    esp_camera_fb_return(fb);
    esp_deep_sleep_start();
  }
  memcpy(imgBuf, fb->buf, fb->len);
  size_t imgLen = fb->len;
  esp_camera_fb_return(fb);

  // --- Send via WiFiClient (порційно) ---
  WiFiClient client;
  client.setTimeout(10000);

  Serial.printf("Connecting to server %s:%d...\n", serverHost, serverPort);
  if (!client.connect(serverHost, serverPort)) {
    Serial.println("TCP connect failed!");
  } else {
    Serial.println("TCP connect OK");

    // --- HTTP POST вручну ---
    client.printf("POST %s HTTP/1.1\r\n", serverPath);
    client.printf("Host: %s\r\n", serverHost);
    client.println("Content-Type: image/jpeg");
    client.printf("Content-Length: %u\r\n", imgLen);
    client.println("Connection: close");
    client.println();

    // --- Відправка зображення порціями по 1024 байти ---
    const size_t chunkSize = 1024;
    size_t sent = 0;
    while (sent < imgLen) {
      size_t toSend = min(chunkSize, imgLen - sent);
      client.write(imgBuf + sent, toSend);
      sent += toSend;
      delay(1); // даємо час Wi-Fi стеку
    }

    Serial.println("Upload done, waiting for response...");

    // --- Читаємо відповідь ---
    while (client.connected()) {
      String line = client.readStringUntil('\n');
      if (line == "\r" || line == "") break;
    }
    Serial.println("Response:");
    while (client.available()) {
      Serial.write(client.read());
    }

    client.stop();
  }

  free(imgBuf);

  // --- Вимикаємо Wi-Fi та готуємось до сну ---
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(2000);  // даємо TCP закінчити
  digitalWrite(PWDN_GPIO_NUM, HIGH);  // вимкнути камеру
  Serial.println("Going to deep sleep...");
  delay(500);
  esp_deep_sleep_start();
}

void loop() {
}
