//RSSI Wifi Signal test code for ESP32 or ESP8266 / 8285 - choose board by line 5/6
//This code generates one RSSI each 3 seconds in the serial monitor
//Copy & paste into excel for analysis 

//#include <ESP8266WiFi.h>// For ESP8266
#include <WiFi.h> //ESP32, for prgramming push "BOOT" briefly, do not use the Serial monitor
const char* ssid = "WIFI-CONNECTION-2";//WiFi accesspoint, I use a ESP8266 board,  if use WiFi router change SSID
const char* password = "ur4lwmur4lwm";//if use WiFi router change password

void setup() {
  Serial.begin(115200);
  setup_wifi();
  delay(1000);
}

void setup_wifi() {
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");//Print ...... in serial while connecting
    delay(50);
  }
  Serial.println("");
  Serial.print("WiFi connected");
  Serial.print(ssid);
  Serial.print(" IP: ");
  Serial.println(WiFi.localIP());
}
void loop() {
 Serial.println (WiFi.RSSI());
  delay(3000);//wait 3 seconds
}