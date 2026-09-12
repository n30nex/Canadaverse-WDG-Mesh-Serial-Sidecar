#include "BoardSupport.h"

#if defined(WDG_BOARD_HELTEC_V3) || defined(WDG_BOARD_HELTEC_V4)
#include <Wire.h>
#endif

namespace wdg_board {

#if defined(WDG_BOARD_HELTEC_V4)
namespace {

constexpr int kFemPowerPin = 7;
constexpr int kFemSharedEnablePin = 2;
constexpr int kFemGc1109TxPin = 46;
constexpr int kFemKct8103TxPin = 5;
constexpr int kTxLedPin = 35;

enum class FemType : uint8_t { Unknown, Gc1109, Kct8103l };
FemType femType = FemType::Unknown;

void setFemReceive() {
  if (femType == FemType::Gc1109) {
    digitalWrite(kFemSharedEnablePin, HIGH);
    digitalWrite(kFemGc1109TxPin, LOW);
  } else if (femType == FemType::Kct8103l) {
    digitalWrite(kFemSharedEnablePin, HIGH);
    // HIGH bypasses the V4.3 LNA. This is the conservative upstream default.
    digitalWrite(kFemKct8103TxPin, HIGH);
  }
}

void setFemTransmit() {
  if (femType == FemType::Gc1109) {
    digitalWrite(kFemSharedEnablePin, HIGH);
    digitalWrite(kFemGc1109TxPin, HIGH);
  } else if (femType == FemType::Kct8103l) {
    digitalWrite(kFemSharedEnablePin, HIGH);
    digitalWrite(kFemKct8103TxPin, HIGH);
  }
}

void beginFem() {
  pinMode(kFemPowerPin, OUTPUT);
  digitalWrite(kFemPowerPin, HIGH);
  delay(1);

  pinMode(kFemSharedEnablePin, INPUT);
  delay(1);
  femType = digitalRead(kFemSharedEnablePin) == HIGH
                ? FemType::Kct8103l
                : FemType::Gc1109;

  pinMode(kFemSharedEnablePin, OUTPUT);
  digitalWrite(kFemSharedEnablePin, HIGH);
  if (femType == FemType::Gc1109) {
    pinMode(kFemGc1109TxPin, OUTPUT);
  } else {
    pinMode(kFemKct8103TxPin, OUTPUT);
  }
  setFemReceive();
  Serial.printf("Heltec V4 front-end: %s\n",
                femType == FemType::Kct8103l ? "KCT8103L" : "GC1109");
}

}  // namespace
#endif

void begin() {
#if !defined(WDG_BOARD_RAK4631)
  pinMode(kUserButtonPin, INPUT_PULLUP);
#endif

#if defined(WDG_BOARD_HELTEC_V3)
  // Heltec V3 Vext is active low and powers the integrated OLED.
  pinMode(36, OUTPUT);
  digitalWrite(36, LOW);
  delay(10);
  Wire.begin(17, 18);
#elif defined(WDG_BOARD_HELTEC_V4)
  // Heltec V4 Vext is active low and powers the integrated OLED.
  pinMode(36, OUTPUT);
  digitalWrite(36, LOW);
  delay(10);
  pinMode(kTxLedPin, OUTPUT);
  digitalWrite(kTxLedPin, LOW);
  Wire.begin(17, 18);
  beginFem();
#elif defined(WDG_BOARD_RC52)
  // Preserve the qualified RC52 FEM supply sequence and keep TX disabled.
  NRF_POWER->DCDCEN = 1;
  pinMode(RADIOCORE_FEM_EN, OUTPUT);
  digitalWrite(RADIOCORE_FEM_EN, HIGH);
  pinMode(RADIOCORE_VFEM_CTRL, OUTPUT);
  digitalWrite(RADIOCORE_VFEM_CTRL, HIGH);
  pinMode(PIN_BAT_CTL, OUTPUT);
  digitalWrite(PIN_BAT_CTL, LOW);
  delay(10);
#elif defined(WDG_BOARD_RAK4631)
  // The integrated SX1262 is behind a dedicated active-high power switch.
  constexpr int kSx126xPowerEnable = 37;
  pinMode(kSx126xPowerEnable, OUTPUT);
  digitalWrite(kSx126xPowerEnable, HIGH);
  delay(10);
#endif
}

void beforeTransmit() {
#if defined(WDG_BOARD_HELTEC_V4)
  digitalWrite(kTxLedPin, HIGH);
  setFemTransmit();
#endif
}

void afterTransmit() {
#if defined(WDG_BOARD_HELTEC_V4)
  digitalWrite(kTxLedPin, LOW);
  setFemReceive();
#endif
}

void beforeReceive() {
#if defined(WDG_BOARD_HELTEC_V4)
  setFemReceive();
#endif
}

bool applyRadioConfiguration(SX1262 &radio) {
#if defined(WDG_BOARD_RC52)
  radio.setRfSwitchPins(SX126X_RXEN, RADIOLIB_NC);
#elif defined(WDG_BOARD_HELTEC_V4)
  // Heltec's current V4 support sets bit 0 at 0x08B5 for improved receive.
  uint8_t value = 0;
  if (radio.readRegister(0x08B5, &value, 1) != RADIOLIB_ERR_NONE) {
    return false;
  }
  value |= 0x01;
  if (radio.writeRegister(0x08B5, &value, 1) != RADIOLIB_ERR_NONE) {
    return false;
  }
#else
  (void)radio;
#endif
  return true;
}

}  // namespace wdg_board
