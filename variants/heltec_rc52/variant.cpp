/* Heltec RC52 board initialization; LGPL-2.1-or-later compatible. */
#include "variant.h"
#include <Arduino.h>
#include <nrf.h>
#include <wiring_digital.h>

const uint32_t g_ADigitalPinMap[] = {
  0xff, 0xff, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
  16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
  32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47
};

void initVariant() {
  pinMode(RADIOCORE_FEM_EN, OUTPUT);
  digitalWrite(RADIOCORE_FEM_EN, HIGH);
  pinMode(RADIOCORE_VFEM_CTRL, OUTPUT);
  digitalWrite(RADIOCORE_VFEM_CTRL, HIGH);

  pinMode(PIN_TFT_EN, OUTPUT);
  digitalWrite(PIN_TFT_EN, !PIN_TFT_EN_ACTIVE);
  pinMode(PIN_TFT_BL, OUTPUT);
  digitalWrite(PIN_TFT_BL, !PIN_TFT_BL_ACTIVE);
  pinMode(PIN_TFT_CS, OUTPUT);
  digitalWrite(PIN_TFT_CS, HIGH);
}

void variant_shutdown() {
  digitalWrite(RADIOCORE_FEM_EN, LOW);
  digitalWrite(RADIOCORE_VFEM_CTRL, LOW);
  digitalWrite(PIN_TFT_BL, !PIN_TFT_BL_ACTIVE);
  digitalWrite(PIN_TFT_EN, !PIN_TFT_EN_ACTIVE);

  const uint8_t pins[] = {
    PIN_USER_BTN, PIN_TFT_BL, PIN_TFT_EN, PIN_TFT_CS, PIN_TFT_DC,
    PIN_TFT_SCL, PIN_TFT_SDA, PIN_TFT_RST, LORA_CS, SX126X_DIO1,
    SX126X_BUSY, SX126X_RESET, SX126X_RXEN, PIN_SPI_MISO, PIN_SPI_MOSI,
    PIN_SPI_SCK, PIN_SPI1_MISO, PIN_SPI1_MOSI, PIN_SPI1_SCK,
    RADIOCORE_FEM_EN, RADIOCORE_VFEM_CTRL
  };
  for (uint8_t pin : pins) nrf_gpio_cfg_default(pin);
}
