#include <WiFi.h>
#include <WiFiUdp.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel_instance;
  lgfx::Bus_SPI _bus_instance;

public:
  LGFX() {
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.pin_sclk = 12;
      cfg.pin_mosi = 11;
      cfg.pin_miso = 13;
      cfg.pin_dc = 5;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }
    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs = 10;
      cfg.pin_rst = 4;
      cfg.panel_width = 240;
      cfg.panel_height = 320;

      cfg.offset_x = 0;
      cfg.offset_y = 0; 

      cfg.invert = false;
      cfg.rgb_order = false;

      _panel_instance.config(cfg);
    }
    setPanel(&_panel_instance);
  }
};

LGFX lcd;

const char *ssid = "ESP32_STREAM";
const char *password = "12345678";
const unsigned int localPort = 8888;

WiFiUDP udp;

#define MAX_JPEG_SIZE 35000
#define UDP_CHUNK_SIZE 1440

typedef struct __attribute__((packed)) {
  uint16_t frame_id;
  uint8_t chunk_id;
  uint8_t total_chunks;
  uint16_t data_len;
  uint8_t data[UDP_CHUNK_SIZE];
} UdpPacket;

uint8_t rx_packet_buffer[sizeof(UdpPacket)];

uint8_t *rx_buf = nullptr;
uint8_t *draw_buf = nullptr;

volatile uint16_t active_frame_id = 0;
volatile uint8_t chunks_received = 0;
volatile uint32_t rx_len = 0;
volatile uint32_t ready_draw_len = 0;
volatile bool new_frame_ready = false;

void udpReceiveTask(void *pvParameters) {
  while (true) {
    int packetSize = udp.parsePacket();
    if (packetSize > 0) {
      int bytesRead = udp.read(rx_packet_buffer, sizeof(rx_packet_buffer));
      if (bytesRead >= 6) {
        UdpPacket *p = (UdpPacket *)rx_packet_buffer;

        if (p->frame_id != active_frame_id) {
          active_frame_id = p->frame_id;
          chunks_received = 0;
          rx_len = 0;
        }

        uint32_t offset = (uint32_t)p->chunk_id * UDP_CHUNK_SIZE;
        if (offset + p->data_len <= MAX_JPEG_SIZE) {
          memcpy(rx_buf + offset, p->data, p->data_len);
          chunks_received++;
          rx_len += p->data_len;

          if (chunks_received == p->total_chunks) {
            if (!new_frame_ready) {
              uint8_t *temp = draw_buf;
              draw_buf = rx_buf;
              rx_buf = temp;

              ready_draw_len = rx_len;
              new_frame_ready = true;
            }
            chunks_received = 0;
            rx_len = 0;
          }
        }
      }
    } else {
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
}

void setup() {
  setCpuFrequencyMhz(240);
  Serial.begin(115200);

  rx_buf = (uint8_t *)heap_caps_malloc(MAX_JPEG_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  draw_buf = (uint8_t *)heap_caps_malloc(MAX_JPEG_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

  lcd.init();
  lcd.setRotation(1);
  lcd.setColorDepth(16);
  lcd.fillScreen(TFT_BLACK);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);
  WiFi.setTxPower(WIFI_POWER_17dBm);
  WiFi.setSleep(false);

  udp.begin(localPort);

  xTaskCreatePinnedToCore(udpReceiveTask, "UDP_RX", 8192, NULL, 1, NULL, 0);
}

void loop() {
  if (new_frame_ready) {
    if (ready_draw_len > 2 && draw_buf[0] == 0xFF && draw_buf[1] == 0xD8) {
      lcd.startWrite();
      lcd.drawJpg(draw_buf, ready_draw_len, 0, 0);
      lcd.endWrite();
    }
    new_frame_ready = false;
  } else {
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}
