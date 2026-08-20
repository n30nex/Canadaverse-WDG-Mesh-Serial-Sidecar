#include "NV3001BDisplay.h"
#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

#if NV3001B_USE_FAST_GPIO
  #if !defined(CONFIG_IDF_TARGET_ESP32C6)
    #error "NV3001B_USE_FAST_GPIO is only supported on ESP32-C6"
  #endif
  #include <hal/gpio_ll.h>
  #include <soc/gpio_struct.h>
#endif

#ifndef SPI_FREQUENCY
  #define SPI_FREQUENCY 8000000
#endif

#ifndef PIN_TFT_SCL
  #error "PIN_TFT_SCL must be defined"
#endif

#ifndef PIN_TFT_SDA
  #error "PIN_TFT_SDA must be defined"
#endif

#ifndef PIN_TFT_CS
  #error "PIN_TFT_CS must be defined"
#endif

#ifndef PIN_TFT_DC
  #error "PIN_TFT_DC must be defined"
#endif

#ifndef PIN_TFT_MISO
  #define PIN_TFT_MISO -1
#endif

#ifndef PIN_TFT_RST
  #define PIN_TFT_RST -1
#endif

#ifndef PIN_TFT_EN
  #define PIN_TFT_EN -1
#endif

#ifndef PIN_TFT_BL
  #define PIN_TFT_BL -1
#endif

#ifndef PIN_TFT_EN_ACTIVE
  #define PIN_TFT_EN_ACTIVE LOW
#endif

#ifndef PIN_TFT_BL_ACTIVE
  #define PIN_TFT_BL_ACTIVE HIGH
#endif

#ifndef DISPLAY_ROTATION
  #define DISPLAY_ROTATION 0
#endif

#ifndef NV3001B_SCREEN_WIDTH
  #define NV3001B_SCREEN_WIDTH 220
#endif

#ifndef NV3001B_SCREEN_HEIGHT
  #define NV3001B_SCREEN_HEIGHT 128
#endif

#ifndef DISPLAY_SCALE_X
  #define DISPLAY_SCALE_X ((float)NV3001B_SCREEN_WIDTH / NV3001B_LOGICAL_WIDTH)
#endif

#ifndef DISPLAY_SCALE_Y
  #define DISPLAY_SCALE_Y ((float)NV3001B_SCREEN_HEIGHT / NV3001B_LOGICAL_HEIGHT)
#endif

#define NV3001B_SWRESET 0x01
#define NV3001B_SLPOUT  0x11
#define NV3001B_INVOFF  0x20
#define NV3001B_DISPON  0x29
#define NV3001B_CASET   0x2A
#define NV3001B_RASET   0x2B
#define NV3001B_RAMWR   0x2C
#define NV3001B_MADCTL  0x36
#define NV3001B_COLMOD  0x3A

#define NV3001B_MADCTL_MY  0x80
#define NV3001B_MADCTL_MX  0x40
#define NV3001B_MADCTL_MV  0x20
#define NV3001B_MADCTL_RGB 0x00

#ifndef NV3001B_TEXT_SIZE1_SCALE_X
  #define NV3001B_TEXT_SIZE1_SCALE_X 1
#endif

#ifndef NV3001B_TEXT_SIZE1_SCALE_Y
  #define NV3001B_TEXT_SIZE1_SCALE_Y 2
#endif

#ifndef NV3001B_TEXT_SIZE2_SCALE_X
  #define NV3001B_TEXT_SIZE2_SCALE_X 2
#endif

#ifndef NV3001B_TEXT_SIZE2_SCALE_Y
  #define NV3001B_TEXT_SIZE2_SCALE_Y 3
#endif

// Color scheme
#ifdef NEONPOCKET_UI
ColorVal UIColor::window_bkg = 0x0000;
ColorVal UIColor::title_bkg = 0x0000;
ColorVal UIColor::primary_txt = 0xFFFF;
ColorVal UIColor::popup_bkg = 0x0000;
ColorVal UIColor::popup_txt = 0xFFFF;
ColorVal UIColor::corp_blue = 0x001F;
#else
ColorVal UIColor::window_bkg = 0xFFFF;
ColorVal UIColor::title_bkg = 0x001F;
ColorVal UIColor::primary_txt = 0x0000;
ColorVal UIColor::popup_bkg = 0x07FF;  // CYAN
ColorVal UIColor::popup_txt = 0x0000;
ColorVal UIColor::corp_blue = 0x001A;
#endif
ColorVal UIColor::title_txt = 0xFFFF;
ColorVal UIColor::secondary_txt = (18 << 11) | (36 << 5) | 18;  // mid-gray
ColorVal UIColor::warning_txt = 0xFD20;

static int scaleX(int x) {
  return (int)(x * DISPLAY_SCALE_X);
}
static int scaleY(int y) {
  return (int)(y * DISPLAY_SCALE_Y);
}

static int scaleWidth(int x, int w) {
  if (w <= 0) return 0;
  int scaled = scaleX(x + w) - scaleX(x);
  return scaled > 0 ? scaled : 1;
}

static int scaleHeight(int y, int h) {
  if (h <= 0) return 0;
  int scaled = scaleY(y + h) - scaleY(y);
  return scaled > 0 ? scaled : 1;
}

static uint8_t nv3001bMADCTL(uint8_t rotation) {
  uint8_t madctl;
  switch (rotation & 3) {
    case 0:
      madctl = NV3001B_MADCTL_MY | NV3001B_MADCTL_MV | NV3001B_MADCTL_RGB;
      break;
    case 1:
      madctl = NV3001B_MADCTL_MY | NV3001B_MADCTL_MX | NV3001B_MADCTL_RGB;
      break;
    case 2:
      madctl = NV3001B_MADCTL_RGB;
      break;
    default:
      madctl = NV3001B_MADCTL_MX | NV3001B_MADCTL_MV | NV3001B_MADCTL_RGB;
      break;
  }
  return madctl;
}

static const uint8_t font5x7[] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5f, 0x00, 0x00, 0x00, 0x07, 0x00, 0x07, 0x00, 0x14,
  0x7f, 0x14, 0x7f, 0x14, 0x24, 0x2a, 0x7f, 0x2a, 0x12, 0x23, 0x13, 0x08, 0x64, 0x62, 0x36, 0x49,
  0x55, 0x22, 0x50, 0x00, 0x05, 0x03, 0x00, 0x00, 0x00, 0x1c, 0x22, 0x41, 0x00, 0x00, 0x41, 0x22,
  0x1c, 0x00, 0x14, 0x08, 0x3e, 0x08, 0x14, 0x08, 0x08, 0x3e, 0x08, 0x08, 0x00, 0x50, 0x30, 0x00,
  0x00, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x60, 0x60, 0x00, 0x00, 0x20, 0x10, 0x08, 0x04, 0x02,
  0x3e, 0x51, 0x49, 0x45, 0x3e, 0x00, 0x42, 0x7f, 0x40, 0x00, 0x42, 0x61, 0x51, 0x49, 0x46, 0x21,
  0x41, 0x45, 0x4b, 0x31, 0x18, 0x14, 0x12, 0x7f, 0x10, 0x27, 0x45, 0x45, 0x45, 0x39, 0x3c, 0x4a,
  0x49, 0x49, 0x30, 0x01, 0x71, 0x09, 0x05, 0x03, 0x36, 0x49, 0x49, 0x49, 0x36, 0x06, 0x49, 0x49,
  0x29, 0x1e, 0x00, 0x36, 0x36, 0x00, 0x00, 0x00, 0x56, 0x36, 0x00, 0x00, 0x08, 0x14, 0x22, 0x41,
  0x00, 0x14, 0x14, 0x14, 0x14, 0x14, 0x00, 0x41, 0x22, 0x14, 0x08, 0x02, 0x01, 0x51, 0x09, 0x06,
  0x32, 0x49, 0x79, 0x41, 0x3e, 0x7e, 0x11, 0x11, 0x11, 0x7e, 0x7f, 0x49, 0x49, 0x49, 0x36, 0x3e,
  0x41, 0x41, 0x41, 0x22, 0x7f, 0x41, 0x41, 0x22, 0x1c, 0x7f, 0x49, 0x49, 0x49, 0x41, 0x7f, 0x09,
  0x09, 0x09, 0x01, 0x3e, 0x41, 0x49, 0x49, 0x7a, 0x7f, 0x08, 0x08, 0x08, 0x7f, 0x00, 0x41, 0x7f,
  0x41, 0x00, 0x20, 0x40, 0x41, 0x3f, 0x01, 0x7f, 0x08, 0x14, 0x22, 0x41, 0x7f, 0x40, 0x40, 0x40,
  0x40, 0x7f, 0x02, 0x0c, 0x02, 0x7f, 0x7f, 0x04, 0x08, 0x10, 0x7f, 0x3e, 0x41, 0x41, 0x41, 0x3e,
  0x7f, 0x09, 0x09, 0x09, 0x06, 0x3e, 0x41, 0x51, 0x21, 0x5e, 0x7f, 0x09, 0x19, 0x29, 0x46, 0x46,
  0x49, 0x49, 0x49, 0x31, 0x01, 0x01, 0x7f, 0x01, 0x01, 0x3f, 0x40, 0x40, 0x40, 0x3f, 0x1f, 0x20,
  0x40, 0x20, 0x1f, 0x3f, 0x40, 0x38, 0x40, 0x3f, 0x63, 0x14, 0x08, 0x14, 0x63, 0x07, 0x08, 0x70,
  0x08, 0x07, 0x61, 0x51, 0x49, 0x45, 0x43, 0x00, 0x7f, 0x41, 0x41, 0x00, 0x02, 0x04, 0x08, 0x10,
  0x20, 0x00, 0x41, 0x41, 0x7f, 0x00, 0x04, 0x02, 0x01, 0x02, 0x04, 0x40, 0x40, 0x40, 0x40, 0x40,
  0x00, 0x01, 0x02, 0x04, 0x00, 0x20, 0x54, 0x54, 0x54, 0x78, 0x7f, 0x48, 0x44, 0x44, 0x38, 0x38,
  0x44, 0x44, 0x44, 0x20, 0x38, 0x44, 0x44, 0x48, 0x7f, 0x38, 0x54, 0x54, 0x54, 0x18, 0x08, 0x7e,
  0x09, 0x01, 0x02, 0x0c, 0x52, 0x52, 0x52, 0x3e, 0x7f, 0x08, 0x04, 0x04, 0x78, 0x00, 0x44, 0x7d,
  0x40, 0x00, 0x20, 0x40, 0x44, 0x3d, 0x00, 0x7f, 0x10, 0x28, 0x44, 0x00, 0x00, 0x41, 0x7f, 0x40,
  0x00, 0x7c, 0x04, 0x18, 0x04, 0x78, 0x7c, 0x08, 0x04, 0x04, 0x78, 0x38, 0x44, 0x44, 0x44, 0x38,
  0x7c, 0x14, 0x14, 0x14, 0x08, 0x08, 0x14, 0x14, 0x18, 0x7c, 0x7c, 0x08, 0x04, 0x04, 0x08, 0x48,
  0x54, 0x54, 0x54, 0x20, 0x04, 0x3f, 0x44, 0x40, 0x20, 0x3c, 0x40, 0x40, 0x20, 0x7c, 0x1c, 0x20,
  0x40, 0x20, 0x1c, 0x3c, 0x40, 0x30, 0x40, 0x3c, 0x44, 0x28, 0x10, 0x28, 0x44, 0x0c, 0x50, 0x50,
  0x50, 0x3c, 0x44, 0x64, 0x54, 0x4c, 0x44, 0x00, 0x08, 0x36, 0x41, 0x00, 0x00, 0x00, 0x7f, 0x00,
  0x00, 0x00, 0x41, 0x36, 0x08, 0x00, 0x08, 0x08, 0x2a, 0x1c, 0x08, 0x00, 0x06, 0x09, 0x09, 0x06
};

static int textPixelScaleX(uint8_t size) {
  return size <= 1 ? NV3001B_TEXT_SIZE1_SCALE_X : NV3001B_TEXT_SIZE2_SCALE_X;
}

static int textPixelScaleY(uint8_t size) {
  return size <= 1 ? NV3001B_TEXT_SIZE1_SCALE_Y : NV3001B_TEXT_SIZE2_SCALE_Y;
}

static void setupOptionalOutput(int pin, int level) {
  if (pin < 0) return;

  pinMode(pin, OUTPUT);
  digitalWrite(pin, level);
}

static void writeOptionalPin(int pin, int level) {
  if (pin < 0) return;

  digitalWrite(pin, level);
}

void NV3001BDisplay::beginTransport() {
#if NV3001B_USE_SOFTWARE_SPI
  pinMode(PIN_TFT_SCL, OUTPUT);
  pinMode(PIN_TFT_SDA, OUTPUT);
  digitalWrite(PIN_TFT_SCL, LOW);
  digitalWrite(PIN_TFT_SDA, LOW);
#else
  if (external_spi) {
  #if defined(NRF52_PLATFORM)
    spi->setPins(PIN_TFT_MISO, PIN_TFT_SCL, PIN_TFT_SDA);
  #endif
    spi->begin();
  } else {
  #if defined(ESP32)
    spi->begin(PIN_TFT_SCL, PIN_TFT_MISO, PIN_TFT_SDA, PIN_TFT_CS);
  #else
    spi->begin();
  #endif
  }
#endif
}

void NV3001BDisplay::beginTransfer() {
#if NV3001B_USE_SOFTWARE_SPI
  digitalWrite(PIN_TFT_SCL, LOW);
#else
  spi->beginTransaction(SPISettings(SPI_FREQUENCY, MSBFIRST, SPI_MODE0));
#endif
}

void NV3001BDisplay::transferByte(uint8_t value) {
#if NV3001B_USE_SOFTWARE_SPI
  for (uint8_t mask = 0x80; mask != 0; mask >>= 1) {
  #if NV3001B_USE_FAST_GPIO
    gpio_ll_set_level(&GPIO, PIN_TFT_SDA, (value & mask) ? HIGH : LOW);
    gpio_ll_set_level(&GPIO, PIN_TFT_SCL, HIGH);
    gpio_ll_set_level(&GPIO, PIN_TFT_SCL, LOW);
  #else
    digitalWrite(PIN_TFT_SDA, (value & mask) ? HIGH : LOW);
    digitalWrite(PIN_TFT_SCL, HIGH);
    digitalWrite(PIN_TFT_SCL, LOW);
  #endif
  }
#else
  spi->transfer(value);
#endif
}

void NV3001BDisplay::endTransfer() {
#if NV3001B_USE_SOFTWARE_SPI
  digitalWrite(PIN_TFT_SCL, LOW);
#else
  spi->endTransaction();
#endif
}

void NV3001BDisplay::writeCommand(uint8_t cmd) {
  beginTransfer();
  digitalWrite(PIN_TFT_DC, LOW);
  digitalWrite(PIN_TFT_CS, LOW);
  transferByte(cmd);
  digitalWrite(PIN_TFT_CS, HIGH);
  endTransfer();
}

void NV3001BDisplay::writeBytes(const uint8_t* data, size_t len) {
  if (!data || len == 0) return;

  beginTransfer();
  digitalWrite(PIN_TFT_DC, HIGH);
  digitalWrite(PIN_TFT_CS, LOW);
  for (size_t i = 0; i < len; i++) {
    transferByte(data[i]);
  }
  digitalWrite(PIN_TFT_CS, HIGH);
  endTransfer();
}

void NV3001BDisplay::writeCommandData(uint8_t cmd, const uint8_t* data, size_t len) {
  beginTransfer();
  digitalWrite(PIN_TFT_CS, LOW);
  digitalWrite(PIN_TFT_DC, LOW);
  transferByte(cmd);
  if (data && len) {
    digitalWrite(PIN_TFT_DC, HIGH);
    for (size_t i = 0; i < len; i++) {
      transferByte(data[i]);
    }
  }
  digitalWrite(PIN_TFT_CS, HIGH);
  endTransfer();
}

void NV3001BDisplay::setAddrWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  uint16_t x2 = x + w - 1;
  uint16_t y2 = y + h - 1;
  uint8_t data[4];

  data[0] = x >> 8;
  data[1] = x & 0xff;
  data[2] = x2 >> 8;
  data[3] = x2 & 0xff;
  writeCommandData(NV3001B_CASET, data, sizeof(data));

  data[0] = y >> 8;
  data[1] = y & 0xff;
  data[2] = y2 >> 8;
  data[3] = y2 & 0xff;
  writeCommandData(NV3001B_RASET, data, sizeof(data));
}

void NV3001BDisplay::writeColor(uint16_t rgb, uint32_t count) {
  uint8_t hi = rgb >> 8;
  uint8_t lo = rgb & 0xff;

  beginTransfer();
  digitalWrite(PIN_TFT_CS, LOW);
  digitalWrite(PIN_TFT_DC, LOW);
  transferByte(NV3001B_RAMWR);
  digitalWrite(PIN_TFT_DC, HIGH);
  while (count--) {
    transferByte(hi);
    transferByte(lo);
  }
  digitalWrite(PIN_TFT_CS, HIGH);
  endTransfer();
}

#if NV3001B_USE_FRAMEBUFFER
#if NV3001B_USE_INDEXED_FRAMEBUFFER
bool NV3001BDisplay::framebufferPaletteIndex(uint16_t rgb, uint8_t& index) {
  for (uint16_t i = 0; i < framebuffer_palette_size; i++) {
    if (framebuffer_palette[i] == rgb) {
      index = static_cast<uint8_t>(i);
      return true;
    }
  }
  if (framebuffer_palette_size == framebuffer_palette_capacity) return false;
  index = static_cast<uint8_t>(framebuffer_palette_size);
  framebuffer_palette[framebuffer_palette_size++] = rgb;
  return true;
}
#endif

uint64_t NV3001BDisplay::hashFramebufferTile(
    uint16_t x, uint16_t y, uint16_t w, uint16_t h) const {
  uint64_t hash = 0xcbf29ce484222325ULL;
  for (uint16_t row = 0; row < h; row++) {
    size_t offset = (size_t)(y + row) * NV3001B_SCREEN_WIDTH + x;
    for (uint16_t col = 0; col < w; col++, offset++) {
#if NV3001B_USE_INDEXED_FRAMEBUFFER
      hash ^= framebuffer[offset];
      hash *= 0x100000001b3ULL;
#else
      const uint16_t pixel = framebuffer[offset];
      hash ^= pixel >> 8;
      hash *= 0x100000001b3ULL;
      hash ^= pixel & 0xff;
      hash *= 0x100000001b3ULL;
#endif
    }
  }
  return hash;
}

void NV3001BDisplay::flushFramebuffer() {
  if (!framebuffer || !is_on) return;

  const uint32_t started = micros();
  const bool force_full = !framebuffer_hashes_valid || framebuffer_flushes_since_full == 4095;
  framebuffer_last_tiles_sent = 0;

  for (uint8_t tile_row = 0; tile_row < framebuffer_tile_rows; tile_row++) {
    const uint16_t y = tile_row * framebuffer_tile_height;
    const uint16_t rows = y + framebuffer_tile_height <= NV3001B_SCREEN_HEIGHT
        ? framebuffer_tile_height : NV3001B_SCREEN_HEIGHT - y;
    uint64_t current_hashes[framebuffer_tile_columns];
    bool changed[framebuffer_tile_columns];

    for (uint8_t tile_col = 0; tile_col < framebuffer_tile_columns; tile_col++) {
      const uint16_t x = tile_col * framebuffer_tile_width;
      const uint16_t width = x + framebuffer_tile_width <= NV3001B_SCREEN_WIDTH
          ? framebuffer_tile_width : NV3001B_SCREEN_WIDTH - x;
      const uint16_t tile = tile_row * framebuffer_tile_columns + tile_col;
      current_hashes[tile_col] = hashFramebufferTile(x, y, width, rows);
      changed[tile_col] = force_full || current_hashes[tile_col] != framebuffer_tile_hashes[tile];
    }

    for (uint8_t first_col = 0; first_col < framebuffer_tile_columns;) {
      while (first_col < framebuffer_tile_columns && !changed[first_col]) first_col++;
      if (first_col == framebuffer_tile_columns) break;

      uint8_t last_col = first_col;
      while (last_col + 1 < framebuffer_tile_columns && changed[last_col + 1]) last_col++;

      const uint16_t x = first_col * framebuffer_tile_width;
      const uint16_t run_width = last_col + 1 == framebuffer_tile_columns
          ? NV3001B_SCREEN_WIDTH - x
          : (last_col - first_col + 1) * framebuffer_tile_width;
      setAddrWindow(x, y, run_width, rows);
      beginTransfer();
      digitalWrite(PIN_TFT_CS, LOW);
      digitalWrite(PIN_TFT_DC, LOW);
      transferByte(NV3001B_RAMWR);
      digitalWrite(PIN_TFT_DC, HIGH);
      for (uint16_t row = 0; row < rows; row++) {
        size_t offset = (size_t)(y + row) * NV3001B_SCREEN_WIDTH + x;
        for (uint16_t col = 0; col < run_width; col++, offset++) {
#if NV3001B_USE_INDEXED_FRAMEBUFFER
          const uint16_t pixel = framebuffer_palette[framebuffer[offset]];
#else
          const uint16_t pixel = framebuffer[offset];
#endif
          transferByte(pixel >> 8);
          transferByte(pixel & 0xff);
        }
      }
      digitalWrite(PIN_TFT_CS, HIGH);
      endTransfer();

      for (uint8_t tile_col = first_col; tile_col <= last_col; tile_col++) {
        const uint16_t tile = tile_row * framebuffer_tile_columns + tile_col;
        framebuffer_tile_hashes[tile] = current_hashes[tile_col];
        framebuffer_last_tiles_sent++;
      }
      first_col = last_col + 1;
      yield();
    }
  }

  framebuffer_hashes_valid = true;
  framebuffer_flushes_since_full = force_full ? 0 : framebuffer_flushes_since_full + 1;
  framebuffer_last_flush_micros = micros() - started;
}
#endif

void NV3001BDisplay::initPanel() {
#define CMD0(C) do { writeCommand(C); } while (0)
#define CMD1(C, A) do { const uint8_t d[] = { A }; writeCommandData(C, d, sizeof(d)); } while (0)
#define CMD2(C, A, B) do { const uint8_t d[] = { A, B }; writeCommandData(C, d, sizeof(d)); } while (0)

  CMD0(NV3001B_SWRESET);
  delay(120);
  CMD1(0xFF, 0xA5);
  CMD1(0x41, 0x00);
  CMD1(0x50, 0x02);
  CMD1(0x52, 0x6E);
  CMD1(0x57, 0x02);
  CMD1(0x46, 0x11);
  CMD2(0x47, 0x00, 0x01);
  CMD2(0x8F, 0x22, 0x03);
  CMD1(0x9A, 0x78);
  CMD1(0x9B, 0x78);
  CMD1(0x9C, 0xA0);
  CMD1(0x9D, 0x17);
  CMD1(0x9E, 0xC1);
  CMD1(0x83, 0x5A);
  CMD1(0x84, 0xB6);
  CMD1(0xFF, 0xA5);
  CMD1(0x85, 0x5F);
  CMD1(0x6E, 0x0F);
  CMD1(0x7E, 0x0F);
  CMD1(0x60, 0x00);
  CMD1(0x70, 0x00);
  CMD1(0x6D, 0x33);
  CMD1(0x7D, 0x37);
  CMD1(0x61, 0x09);
  CMD1(0x71, 0x0A);
  CMD1(0x6C, 0x2A);
  CMD1(0x7C, 0x36);
  CMD1(0x62, 0x11);
  CMD1(0x72, 0x10);
  CMD1(0x68, 0x4E);
  CMD1(0x78, 0x4E);
  CMD1(0x66, 0x36);
  CMD1(0x76, 0x3C);
  CMD1(0x1A, 0x1C);
  CMD1(0x7B, 0x14);
  CMD1(0x63, 0x0D);
  CMD1(0x73, 0x0A);
  CMD1(0x6A, 0x16);
  CMD1(0x7A, 0x12);
  CMD1(0x64, 0x0B);
  CMD1(0x74, 0x0A);
  CMD1(0x69, 0x08);
  CMD1(0x79, 0x0A);
  CMD1(0x65, 0x06);
  CMD1(0x75, 0x07);
  CMD1(0x67, 0x23);
  CMD1(0x77, 0x44);
  CMD1(0xE0, 0x00);
  CMD1(0xE9, 0x30);
  CMD1(0xEB, 0xB7);
  CMD1(0xEC, 0x00);
  CMD1(0xED, 0x11);
  CMD1(0xF0, 0xB7);
  CMD1(0x53, 0x04);
  CMD1(0x54, 0x04);
  CMD1(0x55, 0x40);
  CMD1(0x56, 0x40);
  CMD2(0xA0, 0x60, 0x01);
  CMD1(0xA1, 0x84);
  CMD1(0xA2, 0x85);
  CMD2(0xAB, 0x00, 0x02);
  CMD2(0xAC, 0x00, 0x06);
  CMD2(0xAD, 0x00, 0x03);
  CMD2(0xAE, 0x00, 0x07);
  CMD1(0xC7, 0x01);
  CMD1(0xB9, 0x82);
  CMD1(0xBA, 0x83);
  CMD1(0xBB, 0x00);
  CMD1(0xBC, 0x81);
  CMD1(0xBD, 0x02);
  CMD1(0xBE, 0x01);
  CMD1(0xBF, 0x04);
  CMD1(0xC0, 0x03);
  CMD1(0xC8, 0x55);
  CMD1(0xC9, 0xC9);
  CMD1(0xCA, 0xC8);
  CMD1(0xCB, 0xCB);
  CMD1(0xCC, 0xCA);
  CMD1(0xCD, 0x55);
  CMD1(0xCE, 0xCE);
  CMD1(0xCF, 0xCD);
  CMD1(0xD0, 0xD0);
  CMD1(0xD1, 0xCF);
  CMD1(0xF2, 0x46);
  CMD1(0xA8, 0x04);
  CMD1(0xA9, 0xB0);
  CMD1(0xAA, 0xA3);
  CMD1(0xB6, 0x00);
  CMD1(0xB7, 0xB0);
  CMD1(0xB8, 0xA3);
  CMD1(0xC4, 0x03);
  CMD1(0xC5, 0xB0);
  CMD1(0xC6, 0xA3);
  CMD1(0x80, 0x10);
  CMD1(0xFF, 0x00);
  CMD1(0x35, 0x00);
  CMD0(NV3001B_SLPOUT);
  delay(120);
  CMD1(NV3001B_COLMOD, 0x05);
  CMD1(NV3001B_MADCTL, nv3001bMADCTL(DISPLAY_ROTATION));
  CMD0(NV3001B_INVOFF);
  CMD0(NV3001B_DISPON);
  delay(10);

#undef CMD0
#undef CMD1
#undef CMD2
}

void NV3001BDisplay::fillPhysicalRect(int x, int y, int w, int h) {
  if (!is_on || w <= 0 || h <= 0) return;

  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (x + w > NV3001B_SCREEN_WIDTH) w = NV3001B_SCREEN_WIDTH - x;
  if (y + h > NV3001B_SCREEN_HEIGHT) h = NV3001B_SCREEN_HEIGHT - y;
  if (w <= 0 || h <= 0) return;

#if NV3001B_USE_FRAMEBUFFER
  if (framebuffer) {
#if NV3001B_USE_INDEXED_FRAMEBUFFER
    uint8_t index;
    if (!framebufferPaletteIndex(color, index)) {
      Serial.println("NV3001B: indexed framebuffer palette exhausted");
      writeOptionalPin(PIN_TFT_BL, !PIN_TFT_BL_ACTIVE);
      writeOptionalPin(PIN_TFT_EN, !PIN_TFT_EN_ACTIVE);
      is_on = false;
      if (periph_power) periph_power->release();
      return;
    }
    for (int row = 0; row < h; row++) {
      uint8_t* dest = framebuffer + (y + row) * NV3001B_SCREEN_WIDTH + x;
      memset(dest, index, w);
    }
#else
    for (int row = 0; row < h; row++) {
      uint16_t* dest = framebuffer + (y + row) * NV3001B_SCREEN_WIDTH + x;
      for (int col = 0; col < w; col++) dest[col] = color;
    }
#endif
    return;
  }
#endif

  setAddrWindow(x, y, w, h);
  writeColor(color, (uint32_t)w * h);
}

void NV3001BDisplay::drawChar(int x, int y, char ch) {
  int scale_x = textPixelScaleX(text_size);
  int scale_y = textPixelScaleY(text_size);
  if ((uint8_t)ch == 0xDB) {
    fillPhysicalRect(x, y, 5 * scale_x, 7 * scale_y);
    return;
  }
  if ((uint8_t)ch < 32 || (uint8_t)ch > 127) ch = '?';

  uint16_t index = (uint16_t)(ch - 32) * 5;
  for (int col = 0; col < 5; col++) {
    uint8_t line = pgm_read_byte(font5x7 + index + col);
    for (int row = 0; row < 7; row++) {
      if (line & (1 << row)) {
        fillPhysicalRect(x + col * scale_x, y + row * scale_y, scale_x, scale_y);
      }
    }
  }
}

bool NV3001BDisplay::begin() {
  if (is_on) return true;

  if (periph_power) periph_power->claim();

  setupOptionalOutput(PIN_TFT_EN, PIN_TFT_EN_ACTIVE);
  setupOptionalOutput(PIN_TFT_BL, !PIN_TFT_BL_ACTIVE);
#ifdef RC52_STARTUP_DIAGNOSTICS
  Serial.print("RC52_DIAG tft-power en=");
  Serial.print(digitalRead(PIN_TFT_EN));
  Serial.print(" bl=");
  Serial.println(digitalRead(PIN_TFT_BL));
#endif
  pinMode(PIN_TFT_CS, OUTPUT);
  pinMode(PIN_TFT_DC, OUTPUT);
  digitalWrite(PIN_TFT_CS, HIGH);
  digitalWrite(PIN_TFT_DC, HIGH);
  delay(20);

  beginTransport();
#ifdef RC52_STARTUP_DIAGNOSTICS
  Serial.println("RC52_DIAG tft-spi=ready");
#endif
  if (PIN_TFT_RST >= 0) {
    pinMode(PIN_TFT_RST, OUTPUT);
    digitalWrite(PIN_TFT_RST, HIGH);
    delay(100);
    digitalWrite(PIN_TFT_RST, LOW);
    delay(120);
    digitalWrite(PIN_TFT_RST, HIGH);
    delay(120);
  }

  initPanel();
#ifdef RC52_STARTUP_DIAGNOSTICS
  Serial.println("RC52_DIAG tft-panel-init=sent");
#endif
  is_on = true;
  color = 0x0000;
#if NV3001B_USE_FRAMEBUFFER
  setAddrWindow(0, 0, NV3001B_SCREEN_WIDTH, NV3001B_SCREEN_HEIGHT);
  writeColor(color, (uint32_t)NV3001B_SCREEN_WIDTH * NV3001B_SCREEN_HEIGHT);
  if (!framebuffer_allocation_attempted) {
    framebuffer_allocation_attempted = true;
    const size_t pixels = (size_t)NV3001B_SCREEN_WIDTH * NV3001B_SCREEN_HEIGHT;
#if NV3001B_USE_INDEXED_FRAMEBUFFER
    framebuffer = static_cast<uint8_t*>(calloc(pixels, sizeof(uint8_t)));
#else
    framebuffer = static_cast<uint16_t*>(calloc(pixels, sizeof(uint16_t)));
#endif
    Serial.print("NV3001B: framebuffer ");
    Serial.print(framebuffer ? "allocated (" : "allocation failed (");
    Serial.print((unsigned)(pixels * sizeof(*framebuffer)));
    Serial.println(" bytes)");
  } else if (framebuffer) {
    memset(framebuffer, 0,
        (size_t)NV3001B_SCREEN_WIDTH * NV3001B_SCREEN_HEIGHT * sizeof(*framebuffer));
  }
  if (!framebuffer) {
    writeOptionalPin(PIN_TFT_BL, !PIN_TFT_BL_ACTIVE);
    writeOptionalPin(PIN_TFT_EN, !PIN_TFT_EN_ACTIVE);
    is_on = false;
    if (periph_power) periph_power->release();
    return false;
  }
#if NV3001B_USE_INDEXED_FRAMEBUFFER
  framebuffer_palette[0] = 0x0000;
  framebuffer_palette_size = 1;
#endif
  framebuffer_hashes_valid = false;
  framebuffer_flushes_since_full = 0;
#else
  fillPhysicalRect(0, 0, NV3001B_SCREEN_WIDTH, NV3001B_SCREEN_HEIGHT);
#endif
  color = 0xffff;
  text_size = 1;
  cursor_x = 0;
  cursor_y = 0;
  writeOptionalPin(PIN_TFT_BL, PIN_TFT_BL_ACTIVE);
  return true;
}

void NV3001BDisplay::turnOn() {
  begin();
}

void NV3001BDisplay::turnOff() {
  if (!is_on) return;

#if NV3001B_USE_FRAMEBUFFER
  framebuffer_hashes_valid = false;
#endif
  writeOptionalPin(PIN_TFT_BL, !PIN_TFT_BL_ACTIVE);
  writeOptionalPin(PIN_TFT_EN, !PIN_TFT_EN_ACTIVE);
  is_on = false;
  if (periph_power) periph_power->release();
}

void NV3001BDisplay::clear() {
  uint16_t saved = color;
  color = UIColor::window_bkg;
  fillPhysicalRect(0, 0, NV3001B_SCREEN_WIDTH, NV3001B_SCREEN_HEIGHT);
#if NV3001B_USE_FRAMEBUFFER
  framebuffer_hashes_valid = false;
  flushFramebuffer();
#endif
  color = saved;
}

void NV3001BDisplay::startFrame(ColorVal bkg) {
  color = bkg;
  fillPhysicalRect(0, 0, NV3001B_SCREEN_WIDTH, NV3001B_SCREEN_HEIGHT);
  color = UIColor::primary_txt;
  text_size = 1;
  cursor_x = 0;
  cursor_y = 0;
}

void NV3001BDisplay::setTextSize(int sz) {
  text_size = sz < 1 ? 1 : sz;
}

void NV3001BDisplay::setColor(ColorVal c) {
  color = c;
}

void NV3001BDisplay::setCursor(int x, int y) {
  cursor_x = scaleX(x);
  cursor_y = scaleY(y);
}

void NV3001BDisplay::print(const char* str) {
  if (!str || !is_on) return;

  int scale_x = textPixelScaleX(text_size);
  int scale_y = textPixelScaleY(text_size);
  while (*str) {
    if (*str == '\n') {
      cursor_x = 0;
      cursor_y += 8 * scale_y;
    } else if (*str == '\r') {
      cursor_x = 0;
    } else {
      drawChar(cursor_x, cursor_y, *str);
      cursor_x += 6 * scale_x;
    }
    str++;
  }
}

void NV3001BDisplay::printWordWrap(const char* str, int max_width) {
  if (!str || !is_on || max_width <= 0) return;

  const int line_start = cursor_x;
  int line_end = line_start + scaleWidth(0, max_width);
  if (line_end > NV3001B_SCREEN_WIDTH) line_end = NV3001B_SCREEN_WIDTH;
  const int char_width = 6 * textPixelScaleX(text_size);
  if (line_end - line_start < char_width) return;
  const int glyph_height = 7 * textPixelScaleY(text_size);
  const int line_height = 8 * textPixelScaleY(text_size);
  int bottom = scaleY(NV3001B_LOGICAL_HEIGHT);
  if (bottom > NV3001B_SCREEN_HEIGHT) bottom = NV3001B_SCREEN_HEIGHT;

  auto nextLine = [&]() {
    cursor_x = line_start;
    cursor_y += line_height;
    return cursor_y + glyph_height <= bottom;
  };

  while (*str && cursor_y + glyph_height <= bottom) {
    if (*str == '\n') {
      str++;
      if (!nextLine()) return;
      continue;
    }
    if (*str == '\r') {
      str++;
      continue;
    }
    if (*str == ' ' || *str == '\t') {
      if (cursor_x != line_start) {
        if (cursor_x + char_width > line_end) {
          if (!nextLine()) return;
        } else {
          cursor_x += char_width;
        }
      }
      str++;
      continue;
    }

    const char* word_end = str;
    while (*word_end && *word_end != ' ' && *word_end != '\t' &&
           *word_end != '\n' && *word_end != '\r') word_end++;
    const int word_width = (word_end - str) * char_width;
    if (cursor_x != line_start && cursor_x + word_width > line_end && !nextLine()) return;
    while (str < word_end) {
      if (cursor_x != line_start && cursor_x + char_width > line_end && !nextLine()) return;
      drawChar(cursor_x, cursor_y, *str++);
      cursor_x += char_width;
    }
  }
}

void NV3001BDisplay::fillRect(int x, int y, int w, int h) {
  fillPhysicalRect(scaleX(x), scaleY(y), scaleWidth(x, w), scaleHeight(y, h));
}

void NV3001BDisplay::drawRect(int x, int y, int w, int h) {
  int x1 = scaleX(x);
  int y1 = scaleY(y);
  int sw = scaleWidth(x, w);
  int sh = scaleHeight(y, h);

  fillPhysicalRect(x1, y1, sw, 1);
  fillPhysicalRect(x1, y1 + sh - 1, sw, 1);
  fillPhysicalRect(x1, y1, 1, sh);
  fillPhysicalRect(x1 + sw - 1, y1, 1, sh);
}

void NV3001BDisplay::drawXbm(int x, int y, const uint8_t* bits, int w, int h) {
  if (!bits || !is_on) return;

  int byte_width = (w + 7) / 8;
  for (int j = 0; j < h; j++) {
    for (int i = 0; i < w; i++) {
      uint8_t byte = pgm_read_byte(bits + j * byte_width + i / 8);
      if (byte & (0x80 >> (i & 7))) {
        fillPhysicalRect(scaleX(x + i), scaleY(y + j), scaleWidth(x + i, 1), scaleHeight(y + j, 1));
      }
    }
  }
}

uint16_t NV3001BDisplay::getTextWidth(const char* str) {
  if (!str) return 0;

  uint16_t len = 0;
  while (str[len] && str[len] != '\n' && str[len] != '\r') len++;
  return (uint16_t)((len * 6 * textPixelScaleX(text_size)) / DISPLAY_SCALE_X);
}

extern volatile bool wdg_screenshot_requested;

#if NV3001B_USE_FRAMEBUFFER
static uint32_t screenshotCrc32Update(uint32_t crc, uint8_t value) {
  crc ^= value;
  for (uint8_t bit = 0; bit < 8; bit++) {
    crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1UL)));
  }
  return crc;
}

static void writeScreenshotHeader(uint32_t sequence, uint16_t width,
                                  uint16_t height, uint16_t length,
                                  uint32_t crc) {
  const char magic[] = {'N', 'P', 'O', 'F'};
  Serial.write(reinterpret_cast<const uint8_t*>(magic), sizeof(magic));
  Serial.write(reinterpret_cast<const uint8_t*>(&sequence), sizeof(sequence));
  Serial.write(reinterpret_cast<const uint8_t*>(&width), sizeof(width));
  Serial.write(reinterpret_cast<const uint8_t*>(&height), sizeof(height));
  Serial.write(reinterpret_cast<const uint8_t*>(&length), sizeof(length));
  Serial.write(reinterpret_cast<const uint8_t*>(&crc), sizeof(crc));
}
#endif

void NV3001BDisplay::endFrame() {
#if NV3001B_USE_FRAMEBUFFER
  flushFramebuffer();
  if (!wdg_screenshot_requested || framebuffer == nullptr) return;
  wdg_screenshot_requested = false;

  constexpr uint16_t width = NV3001B_SCREEN_WIDTH;
  constexpr uint16_t height = NV3001B_SCREEN_HEIGHT;
  constexpr uint16_t length = width * height * 2;
  static uint32_t sequence = 0;
  uint32_t crc = 0xFFFFFFFF;

#if NV3001B_USE_INDEXED_FRAMEBUFFER
  for (uint32_t i = 0; i < width * height; i++) {
    const uint16_t pixel = framebuffer_palette[framebuffer[i]];
    crc = screenshotCrc32Update(crc, static_cast<uint8_t>(pixel));
    crc = screenshotCrc32Update(crc, static_cast<uint8_t>(pixel >> 8));
  }
#else
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(framebuffer);
  for (uint16_t i = 0; i < length; i++) {
    crc = screenshotCrc32Update(crc, bytes[i]);
  }
#endif
  crc = ~crc;
  writeScreenshotHeader(sequence++, width, height, length, crc);

#if NV3001B_USE_INDEXED_FRAMEBUFFER
  uint8_t bytes[2];
  for (uint32_t i = 0; i < width * height; i++) {
    const uint16_t pixel = framebuffer_palette[framebuffer[i]];
    bytes[0] = static_cast<uint8_t>(pixel);
    bytes[1] = static_cast<uint8_t>(pixel >> 8);
    Serial.write(bytes, sizeof(bytes));
  }
#else
  Serial.write(reinterpret_cast<const uint8_t*>(framebuffer), length);
#endif
#endif
}
