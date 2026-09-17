#include <WiFi.h>
#include <WiFiUdp.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel_instance;
  lgfx::Bus_SPI       _bus_instance;

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
      cfg.pin_dc   = 5;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }
    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs   = 10;
      cfg.pin_rst  = 4;
      cfg.panel_width  = 240;
      cfg.panel_height = 320;
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
#define UDP_CHUNK_SIZE 1430

typedef struct __attribute__((packed)) {
  uint16_t frame_id;
  uint8_t  chunk_id;
  uint8_t  total_chunks;
  uint16_t data_len;
  uint8_t  data[UDP_CHUNK_SIZE];
} UdpPacket;

uint8_t rx_packet_buffer[sizeof(UdpPacket)];
 
uint8_t *rx_buffer = nullptr;
uint8_t *draw_buffer = nullptr;

uint16_t active_frame_id = 0;
uint8_t  chunks_received = 0;
uint32_t current_frame_len = 0;
uint32_t ready_frame_len = 0;
bool     frame_ready = false;
 
const unsigned long FRAME_INTERVAL_MS = 66;
unsigned long lastDrawTime = 0;

void setup() { 
  setCpuFrequencyMhz(160);

  Serial.begin(115200);

  if (psramFound()) {
    rx_buffer   = (uint8_t *)ps_malloc(MAX_JPEG_SIZE);
    draw_buffer = (uint8_t *)ps_malloc(MAX_JPEG_SIZE);
  } else {
    rx_buffer   = (uint8_t *)malloc(MAX_JPEG_SIZE);
    draw_buffer = (uint8_t *)malloc(MAX_JPEG_SIZE);
  }

  lcd.init();
  lcd.setRotation(1); 
  lcd.fillScreen(TFT_BLACK);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);
  WiFi.setTxPower(WIFI_POWER_11dBm); 

  Serial.print("AP Started. IP: ");
  Serial.println(WiFi.softAPIP());

  udp.begin(localPort);
}

void loop() {
  int packetSize = udp.parsePacket();

  if (packetSize > 0) {
    int bytesRead = udp.read(rx_packet_buffer, sizeof(rx_packet_buffer));
    if (bytesRead >= 6) {
      UdpPacket *p = (UdpPacket *)rx_packet_buffer;
 
      if (p->frame_id != active_frame_id) {
        active_frame_id = p->frame_id;
        chunks_received = 0;
        current_frame_len = 0;
      }

      uint32_t offset = (uint32_t)p->chunk_id * UDP_CHUNK_SIZE;
      if (offset + p->data_len <= MAX_JPEG_SIZE) {
        memcpy(rx_buffer + offset, p->data, p->data_len);
        chunks_received++;
        current_frame_len += p->data_len;
 
        if (chunks_received == p->total_chunks) {
          unsigned long now = millis();
 
          if (now - lastDrawTime >= FRAME_INTERVAL_MS) { 
            uint8_t *temp = draw_buffer;
            draw_buffer = rx_buffer;
            rx_buffer = temp;

            ready_frame_len = current_frame_len;
            frame_ready = true;
            lastDrawTime = now;
          }

          chunks_received = 0;
          current_frame_len = 0;
        }
      }
    }
  } else { 
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  if (frame_ready) { 
    lcd.drawJpg(draw_buffer, ready_frame_len, 0, 0);
    frame_ready = false;
  }
}