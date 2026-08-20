#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>

static const uint8_t LED_BUILTIN = 35;
#define BUILTIN_LED LED_BUILTIN

static const uint8_t TX = 43;
static const uint8_t RX = 44;
static const uint8_t SDA = 3;
static const uint8_t SCL = 4;
static const uint8_t SS = 8;
static const uint8_t MOSI = 10;
static const uint8_t MISO = 11;
static const uint8_t SCK = 9;

static const uint8_t Vext = 36;
static const uint8_t LED = 35;
static const uint8_t RST_OLED = 21;
static const uint8_t SCL_OLED = 18;
static const uint8_t SDA_OLED = 17;
static const uint8_t RST_LoRa = 12;
static const uint8_t BUSY_LoRa = 13;
static const uint8_t DIO0 = 14;

#endif
