/*
 * Heltec RC52 pin mapping, derived from Heltec_nRF52's LGPL-2.1-or-later
 * heltec_rc52 variant (commit 4cd245aa30d72a6c3d40af227084943465e2d0f6).
 */
#pragma once

#include "WVariant.h"

#define USE_LFXO
#define VARIANT_MCK             (64000000ul)

#define PINS_COUNT              (48)
#define NUM_DIGITAL_PINS        (48)
#define NUM_ANALOG_INPUTS       (1)
#define NUM_ANALOG_OUTPUTS      (0)

#define PIN_LED1                (-1)
#define LED_BUILTIN             PIN_LED1
#define LED_BLUE                PIN_LED1
#define LED_STATE_ON            HIGH

#define PIN_USER_BTN            (42)  // P1.10, external pull-up, active low
#define PIN_BUTTON1             PIN_USER_BTN

#define WIRE_INTERFACES_COUNT   (1)
#define PIN_WIRE_SDA            (6)
#define PIN_WIRE_SCL            (29)

#define PIN_SERIAL1_RX          (7)
#define PIN_SERIAL1_TX          (8)

// NV3001B TFT on SPI1 (SPIM3).
#define SPI_INTERFACES_COUNT    (2)
#define SPI_32MHZ_INTERFACE     (1)
#define PIN_TFT_SCL             (30)
#define PIN_TFT_SDA             (34)  // P1.02
#define PIN_TFT_MISO            (12)  // not connected; SPIClass requires a pin
#define PIN_TFT_CS              (36)  // P1.04
#define PIN_TFT_DC              (28)
#define PIN_TFT_RST             (10)
#define PIN_TFT_EN              (45)  // P1.13
#define PIN_TFT_BL              (9)
#define PIN_TFT_EN_ACTIVE       LOW
#define PIN_TFT_BL_ACTIVE       HIGH

// SX1262/RA62A radio on SPI.
#define USE_SX1262
#define PIN_SPI_MISO            (14)
#define PIN_SPI_MOSI            (22)
#define PIN_SPI_SCK             (25)
#define LORA_CS                 (13)
#define SX126X_DIO1             (11)
#define SX126X_BUSY             (24)
#define SX126X_RESET            (32)  // P1.00
#define SX126X_RXEN             (39)  // P1.07
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

#define RADIOCORE_FEM_EN        (26)
#define RADIOCORE_VFEM_CTRL     (16)

static const uint8_t SS   = LORA_CS;
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK  = PIN_SPI_SCK;

#define PIN_SPI1_MISO           PIN_TFT_MISO
#define PIN_SPI1_MOSI           PIN_TFT_SDA
#define PIN_SPI1_SCK            PIN_TFT_SCL
static const uint8_t SS1   = PIN_TFT_CS;
static const uint8_t MOSI1 = PIN_SPI1_MOSI;
static const uint8_t MISO1 = PIN_SPI1_MISO;
static const uint8_t SCK1  = PIN_SPI1_SCK;

#define PIN_BAT_CTL             (4)
#define PIN_VBAT_READ           (31)
#define BATTERY_PIN             PIN_VBAT_READ
#define ADC_MULTIPLIER          (4.90F)
#define ADC_RESOLUTION          (14)

// Hard low-voltage shutdown stays disabled until this RC52 ADC is calibrated.
#define PWRMGT_VOLTAGE_BOOTLOCK (0)
#define PWRMGT_LPCOMP_AIN       (7)
#define PWRMGT_LPCOMP_REFSEL    (1)

void variant_shutdown();
