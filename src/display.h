#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>

// Optional SSD1306 status display (128x64, I2C). Mirrors EntropySource's
// probe-and-skip philosophy: begin() probes the bus; if no panel acks, returns
// false and the device runs headless. Defaults are the LilyGo T3S3 V1
// (SDA=18, SCL=17, 0x3C); override with -DOLED_SDA / -DOLED_SCL / -DOLED_ADDR
// for another board.
//
// ponytail: one concrete class, no abstract Display interface — add one when a
// second controller (SH1106, etc.) forces it. Full-buffer redraw (~1 KB) is
// cheap on ESP32-S3 RAM; the caller throttles (~4 Hz) so the ~25 ms I2C send
// stays out of the entropy hot loop.

struct DisplaySnapshot {
  const char* mode = "MIXED";      // "MIXED" / "RAW" / "SOLO:<name>"
  bool streaming = false;
  uint64_t emitted = 0;
  uint64_t failclose = 0;
  static constexpr int MAX_SRC = 4;
  const char* name[MAX_SRC] = {};
  uint64_t pass[MAX_SRC] = {};
  uint64_t fail[MAX_SRC] = {};
  int n_src = 0;
};

class OledDisplay {
 public:
#ifdef OLED_SDA
  static constexpr int SDA_PIN = OLED_SDA;
#else
  static constexpr int SDA_PIN = 18;   // T3S3 V1
#endif
#ifdef OLED_SCL
  static constexpr int SCL_PIN = OLED_SCL;
#else
  static constexpr int SCL_PIN = 17;   // T3S3 V1
#endif
#ifdef OLED_ADDR
  static constexpr uint8_t ADDR = OLED_ADDR;
#else
  static constexpr uint8_t ADDR = 0x3C;
#endif

  // Probe the bus. Returns false if no panel acks -> run headless.
  bool begin() {
    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.beginTransmission(ADDR);
    if (Wire.endTransmission() != 0) return false;
    u8g2_.setBusClock(400000);
    u8g2_.begin();
    u8g2_.setFont(u8g2_font_5x8_tr);
    u8g2_.setFontMode(0);            // solid: faster, no alpha blending
    return true;
  }

  void show(const DisplaySnapshot& s) {
    char line[28];
    u8g2_.clearBuffer();
    u8g2_.drawStr(0, 8, "guerrilla-entropy");
    snprintf(line, sizeof line, "%s | %s", s.mode, s.streaming ? "STREAMING" : "idle");
    u8g2_.drawStr(0, 20, line);
    int y = 32;
    for (int i = 0; i < s.n_src && i < DisplaySnapshot::MAX_SRC && y < 56; i++, y += 10) {
      snprintf(line, sizeof line, "%-6s %llu/%llu", s.name[i] ? s.name[i] : "?", s.pass[i], s.fail[i]);
      u8g2_.drawStr(0, y, line);
    }
    snprintf(line, sizeof line, "out %llu  fc %llu", s.emitted, s.failclose);
    u8g2_.drawStr(0, 58, line);
    u8g2_.sendBuffer();
  }

 private:
  U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2_{U8G2_R0, U8X8_PIN_NONE};
};
