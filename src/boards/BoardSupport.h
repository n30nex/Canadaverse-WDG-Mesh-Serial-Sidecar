#pragma once

#include <Arduino.h>
#include <RadioLib.h>

#if (defined(WDG_BOARD_RCC6) + defined(WDG_BOARD_HELTEC_V3) + \
     defined(WDG_BOARD_HELTEC_V4) + defined(WDG_BOARD_RAK4631) + \
     defined(WDG_BOARD_HELTEC_TRACKER) + defined(WDG_BOARD_RC52)) != 1
#error "Select exactly one WDG sidecar board"
#endif

namespace wdg_board {

#if defined(WDG_BOARD_RC52)
constexpr char kFirmwareVersion[] = "v0.1.0-beta.3-rc52";
#else
constexpr char kFirmwareVersion[] = "v0.1.0-beta.2";
#endif
constexpr char kManufacturer[] = "Canadaverse";

#if defined(WDG_BOARD_RCC6)
constexpr char kBoardLabel[] = "RCC6";
constexpr char kDeviceName[] = "Biscuit Mesh RCC6";
constexpr char kModelNumber[] = "Heltec RCC6 WDG Mesh";
constexpr char kSetupPrefix[] = "WDG-RCC6-";
constexpr bool kHasColorDisplay = true;
constexpr int kUserButtonPin = 9;
constexpr int kLoRaSclk = 21;
constexpr int kLoRaMiso = 20;
constexpr int kLoRaMosi = 22;
constexpr int kLoRaNss = 23;
constexpr int kLoRaDio1 = 19;
constexpr int kLoRaBusy = 10;
constexpr int kLoRaReset = 8;
constexpr int8_t kRadioTxPowerDbm = 17;
constexpr int8_t kEffectiveTxPowerDbm = 17;
#elif defined(WDG_BOARD_HELTEC_V3)
constexpr char kBoardLabel[] = "HELTEC V3";
constexpr char kDeviceName[] = "Biscuit Mesh V3";
constexpr char kModelNumber[] = "Heltec V3 WDG Mesh";
constexpr char kSetupPrefix[] = "WDG-V3-";
constexpr bool kHasColorDisplay = false;
constexpr int kUserButtonPin = 0;
constexpr int kLoRaSclk = 9;
constexpr int kLoRaMiso = 11;
constexpr int kLoRaMosi = 10;
constexpr int kLoRaNss = 8;
constexpr int kLoRaDio1 = 14;
constexpr int kLoRaBusy = 13;
constexpr int kLoRaReset = 12;
constexpr int8_t kRadioTxPowerDbm = 22;
constexpr int8_t kEffectiveTxPowerDbm = 22;
#elif defined(WDG_BOARD_HELTEC_V4)
constexpr char kBoardLabel[] = "HELTEC V4";
constexpr char kDeviceName[] = "Biscuit Mesh V4";
constexpr char kModelNumber[] = "Heltec V4 WDG Mesh";
constexpr char kSetupPrefix[] = "WDG-V4-";
constexpr bool kHasColorDisplay = false;
constexpr int kUserButtonPin = 0;
constexpr int kLoRaSclk = 9;
constexpr int kLoRaMiso = 11;
constexpr int kLoRaMosi = 10;
constexpr int kLoRaNss = 8;
constexpr int kLoRaDio1 = 14;
constexpr int kLoRaBusy = 13;
constexpr int kLoRaReset = 12;
// V4/V4.3 use an external front-end module. Ten dBm from the SX1262 is
// approximately 22 dBm at the antenna, matching Heltec's upstream profile.
constexpr int8_t kRadioTxPowerDbm = 10;
constexpr int8_t kEffectiveTxPowerDbm = 22;
#elif defined(WDG_BOARD_RAK4631)
constexpr char kBoardLabel[] = "RAK4631";
constexpr char kDeviceName[] = "WDG Mesh RAK4631";
constexpr char kModelNumber[] = "RAK4631 WDG Mesh Serial";
constexpr char kSetupPrefix[] = "WDG-RAK4631-";
constexpr bool kHasColorDisplay = false;
constexpr int kUserButtonPin = -1;
// Upstream MeshCore RAK4631/WisBlock pin map.
constexpr int kLoRaSclk = 43;
constexpr int kLoRaMiso = 45;
constexpr int kLoRaMosi = 44;
constexpr int kLoRaNss = 42;
constexpr int kLoRaDio1 = 47;
constexpr int kLoRaBusy = 46;
constexpr int kLoRaReset = 38;
constexpr int8_t kRadioTxPowerDbm = 22;
constexpr int8_t kEffectiveTxPowerDbm = 22;
#elif defined(WDG_BOARD_RC52)
constexpr char kBoardLabel[] = "RC52";
constexpr char kDeviceName[] = "WDG Mesh RC52";
constexpr char kModelNumber[] = "Heltec RC52 WDG Mesh Serial";
constexpr char kSetupPrefix[] = "WDG-RC52-";
constexpr bool kHasColorDisplay = true;
constexpr int kUserButtonPin = 42;
constexpr int kLoRaSclk = 25;
constexpr int kLoRaMiso = 14;
constexpr int kLoRaMosi = 22;
constexpr int kLoRaNss = 13;
constexpr int kLoRaDio1 = 11;
constexpr int kLoRaBusy = 24;
constexpr int kLoRaReset = 32;
constexpr int8_t kRadioTxPowerDbm = 22;
constexpr int8_t kEffectiveTxPowerDbm = 22;
#elif defined(WDG_BOARD_HELTEC_TRACKER)
constexpr char kBoardLabel[] = "HELTEC TRACKER";
constexpr char kDeviceName[] = "WDG Mesh Tracker";
constexpr char kModelNumber[] = "Heltec Wireless Tracker WDG Mesh Serial";
constexpr char kSetupPrefix[] = "WDG-TRACKER-";
constexpr bool kHasColorDisplay = false;
constexpr int kUserButtonPin = 0;
constexpr int kLoRaSclk = 9;
constexpr int kLoRaMiso = 11;
constexpr int kLoRaMosi = 10;
constexpr int kLoRaNss = 8;
constexpr int kLoRaDio1 = 14;
constexpr int kLoRaBusy = 13;
constexpr int kLoRaReset = -1;
constexpr int8_t kRadioTxPowerDbm = 22;
constexpr int8_t kEffectiveTxPowerDbm = 22;
#endif

void begin();
void beforeTransmit();
void afterTransmit();
void beforeReceive();
bool applyRadioConfiguration(SX1262 &radio);

}  // namespace wdg_board
