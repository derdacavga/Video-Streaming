#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiUdp.h>

#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

#define FLASH_LED_PIN 4
#define RED_LED_PIN 33

const char *ssid = "ESP32_STREAM";
const char *password = "12345678";
const IPAddress serverIP(192, 168, 4, 1);
const unsigned int serverPort = 8888;

WiFiUDP udp;

#define UDP_CHUNK_SIZE 1440

typedef struct __attribute__((packed)) {
  uint16_t frame_id;
  uint8_t chunk_id;
  uint8_t total_chunks;
  uint16_t data_len;
  uint8_t data[UDP_CHUNK_SIZE];
} UdpPacket;

UdpPacket packet;
uint16_t frame_counter = 0;

void setupLEDs() {
  pinMode(RED_LED_PIN, OUTPUT);
  digitalWrite(RED_LED_PIN, HIGH);
  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW);
}

void initCamera() {
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
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 16000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_QVGA; 
  config.jpeg_quality = 12;       
  config.fb_count = 2;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.grab_mode = CAMERA_GRAB_LATEST;

  esp_camera_init(&config);
}

void setup() {
  Serial.begin(115200);
  setupLEDs();
  initCamera();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  WiFi.setTxPower(WIFI_POWER_17dBm);
  WiFi.setSleep(false);

  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
  }

  udp.begin(serverPort);
}

void loop() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) return;

  uint8_t total_chunks = (fb->len + UDP_CHUNK_SIZE - 1) / UDP_CHUNK_SIZE;
  frame_counter++;

  for (uint8_t i = 0; i < total_chunks; i++) {
    uint32_t offset = (uint32_t)i * UDP_CHUNK_SIZE;
    uint16_t len = (fb->len - offset > UDP_CHUNK_SIZE) ? UDP_CHUNK_SIZE : (fb->len - offset);

    packet.frame_id = frame_counter;
    packet.chunk_id = i;
    packet.total_chunks = total_chunks;
    packet.data_len = len;
    memcpy(packet.data, fb->buf + offset, len);

    udp.beginPacket(serverIP, serverPort);
    udp.write((uint8_t *)&packet, sizeof(UdpPacket) - (UDP_CHUNK_SIZE - len));
    udp.endPacket();
  }

  esp_camera_fb_return(fb);
}
