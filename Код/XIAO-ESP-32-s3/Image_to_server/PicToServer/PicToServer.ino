#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>

// ==== Налаштування Wi-Fi ====
const char* ssid = "WIFI-CONNECTION-2";
const char* password = "ur4lwmur4lwm";

// ==== Налаштування сервера ====
const char* serverUrl = "http://192.168.1.103:5000/upload";

// ==== Розпіновка XIAO ESP32S3 Sense ====
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

void setup() {
  // На S3 важливо чекати Serial, якщо хочеш бачити лог з перших секунд
  Serial.begin(115200);
  while(!Serial); 
  
  Serial.println("\n--- XIAO S3 Camera High-Res Mode ---");

  // 1. Підключення до Wi-Fi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected: " + WiFi.localIP().toString());

  // 2. Конфігурація камери
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
  
  config.xclk_freq_hz = 20000000; // 20MHz для S3 — стандарт
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_LATEST; 

  // Використання OPI PSRAM для максимальної якості
  if (psramFound()) {
    config.frame_size = FRAMESIZE_UXGA; // 1600x1200
    config.jpeg_quality = 5;            // Найкраща якість (менше = краще)
    config.fb_count = 2;                // Подвійна буферизація
    config.fb_location = CAMERA_FB_IN_PSRAM;
  } else {
    Serial.println("FATAL: PSRAM not found! Check IDE settings.");
    return;
  }

  // Ініціалізація модуля
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x", err);
    return;
  }

  // 3. Тонке налаштування сенсора (Якість + Вимкнення Raw GMA)
  sensor_t * s = esp_camera_sensor_get();
  if (s) {
    s->set_brightness(s, 0);     
    s->set_contrast(s, 0);       
    s->set_saturation(s, 0);     
    s->set_whitebal(s, 1);       
    s->set_awb_gain(s, 1);       
    s->set_exposure_ctrl(s, 1);  
    s->set_aec2(s, 0);           
    s->set_gain_ctrl(s, 1);      
    s->set_bpc(s, 0);            
    s->set_wpc(s, 1);            
    s->set_lenc(s, 1);           
    
    // Пряма команда на вимкнення Raw GMA
    s->set_raw_gma(s, 0); 
    
    Serial.println("Sensor configured: UXGA, Quality 5, Raw GMA: OFF");
  }

  // Даємо камері час на стабілізацію зображення
  delay(1500); 

  // 4. Захоплення кадру
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Failed to capture image");
    return;
  }
  Serial.printf("Image captured! Size: %u bytes\n", fb->len);

  // 5. Відправка на сервер
  HTTPClient http;
  http.begin(serverUrl);
  http.addHeader("Content-Type", "image/jpeg");

  // Тайм-аут збільшено, бо файл при якості "5" буде великим
  http.setTimeout(10000); 

  int httpCode = http.POST(fb->buf, fb->len);
  
  if (httpCode > 0) {
    Serial.printf("Server Response: %d\n", httpCode);
    if (httpCode == 200) {
      Serial.println("Upload successful!");
      Serial.println(http.getString());
    }
  } else {
    Serial.printf("Upload failed. Error: %s\n", http.errorToString(httpCode).c_str());
  }
  
  http.end();
  
  // Звільнення ресурсів
  esp_camera_fb_return(fb);
  
  Serial.println("\nExecution finished. Going to deep sleep / idle.");
}

void loop() {
  // Для тесту робимо один раз. Якщо треба циклічно — перенеси логіку сюди.
  delay(10000);
}