#pragma once
#include <Arduino.h>
#include "../source.h"

// Floating-pin ADC noise. A floating ADC1 pin picks up ambient EM; its low
// bits are biased but genuinely unpredictable. The pool purifies the bias.
//
// ponytail: pin = GPIO1 (ADC1_CH0), assumed free on the T3S3 header. If your
// variant ties it (LoRa SPI / display I2C / PMIC), override ADC_FLOAT_PIN in
// build_flags. Verify the pin is actually floating with raw-mode ent.
class AdcFloatSource : public EntropySource {
 public:
#ifdef ADC_FLOAT_PIN
  static constexpr uint8_t ADC_PIN = ADC_FLOAT_PIN;
#else
  static constexpr uint8_t ADC_PIN = 1;  // ADC1_CH0 on ESP32-S3
#endif
  bool begin() override {
    pinMode(ADC_PIN, INPUT);
    analogReadResolution(12);
    return true;
  }
  const char* name() const override { return "adc_float"; }
  size_t gather(uint8_t* buf, size_t n) override {
    for (size_t i = 0; i < n; i++) {
      uint8_t b = 0;
      for (int j = 0; j < 4; j++) {        // 4 readings x 2 low bits = 1 byte
        b = (b << 2) | (analogRead(ADC_PIN) & 0x03);
      }
      buf[i] = b;
    }
    return n;
  }
};
