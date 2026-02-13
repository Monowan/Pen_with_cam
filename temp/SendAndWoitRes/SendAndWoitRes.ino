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
  config.frame_size = FRAMESIZE_XGA;
  config.jpeg_quality = 15;
  config.fb_count = 2;

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("Camera init failed");
    while (true);
  }
}

// --- Функція для обробки відповіді сервера ---
void handleServerResponse(String response) {
  Serial.println("Server response:");
  Serial.println(response);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  // --- BUTTON WAKEUP ---
  pinMode(0, INPUT_PULLUP);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);  // wake on BOOT button

  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT0) {
    Serial.println("Booted normally. Going to sleep, waiting for button...");
    delay(300);
    esp_deep_sleep_start();
  }

  Serial.println("Wakeup by button!");

  pinMode(PWDN_GPIO_NUM, OUTPUT);
  digitalWrite(PWDN_GPIO_NUM, LOW);  // камера увімкнена
  delay(300);

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
  delay(150);

  // --- Capture ---
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    esp_deep_sleep_start();
  }

  Serial.printf("Picture taken! Size: %zu bytes\n", fb->len);

  uint8_t *imgBuf = (uint8_t*)malloc(fb->len);
  if (!imgBuf) {
    Serial.println("Malloc failed");
    esp_camera_fb_return(fb);
    esp_deep_sleep_start();
  }
  memcpy(imgBuf, fb->buf, fb->len);
  size_t imgLen = fb->len;
  esp_camera_fb_return(fb);

  // --- Upload ---
  WiFiClient client;
  client.setTimeout(10000);

  Serial.printf("Connecting to server %s:%d...\n", serverHost, serverPort);
  if (!client.connect(serverHost, serverPort)) {
    Serial.println("TCP connect failed!");
  } else {
    client.printf("POST %s HTTP/1.1\r\n", serverPath);
    client.printf("Host: %s\r\n", serverHost);
    client.println("Content-Type: image/jpeg");
    client.printf("Content-Length: %u\r\n", imgLen);
    client.println("Connection: close");
    client.println();

    const size_t chunkSize = 1024;
    size_t sent = 0;
    while (sent < imgLen) {
      size_t toSend = min(chunkSize, imgLen - sent);
      client.write(imgBuf + sent, toSend);
      sent += toSend;
      delay(1);
    }

    Serial.println("Upload done, waiting for response...");

    // --- Зчитування текстової відповіді ---
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

  free(imgBuf);

  // --- power off ---
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  digitalWrite(PWDN_GPIO_NUM, HIGH);
  delay(300);

  Serial.println("Going to deep sleep...");
  delay(200);

  esp_deep_sleep_start();
}

void loop() {}
