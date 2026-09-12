#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>
#include <math.h>

#if !defined(NRF52_PLATFORM)
#include <esp_random.h>
#endif

#include <helpers/ArduinoHelpers.h>
#include <helpers/BaseChatMesh.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/StaticPoolPacketManager.h>

#include "boards/BoardSupport.h"

#if defined(WDG_BOARD_RCC6) || defined(WDG_BOARD_RC52)
#include "helpers/ui/NV3001BDisplay.h"
#elif defined(WDG_BOARD_HELTEC_V3) || defined(WDG_BOARD_HELTEC_V4)
#include <helpers/ui/SSD1306Display.h>
#endif

// The display drivers consume this flag and write one CRC-protected frame to
// USB serial. It contains status only; credentials never reach the device.
volatile bool wdg_screenshot_requested = false;

namespace {

constexpr float kDefaultFrequencyMhz = 910.525f;
constexpr float kDefaultBandwidthKhz = 62.5f;
constexpr uint8_t kDefaultSpreadingFactor = 7;
constexpr uint8_t kDefaultCodingRate = 5;
constexpr uint16_t kPreambleSymbols = 32;
constexpr float kTcxoVoltage = 1.8f;
constexpr uint32_t kHostLeaseMs = 15000;
constexpr uint32_t kDisplayIntervalMs = 250;
constexpr size_t kPublicKeySize = 32;

#if defined(WDG_BOARD_RCC6)
NV3001BDisplay display;
#elif defined(WDG_BOARD_RC52)
NV3001BDisplay display(SPI1);
#elif defined(WDG_BOARD_HELTEC_V3) || defined(WDG_BOARD_HELTEC_V4)
SSD1306Display display;
#endif

void reportVerifiedAdvert(const ContactInfo &contact, uint8_t pathLength,
                          float rssi, float snr);

class SidecarRadioAdapter final : public mesh::Radio {
 public:
  explicit SidecarRadioAdapter(SX1262 &instance) : radio(&instance) {
    active = this;
  }

  void begin() override {
    radio->setPacketReceivedAction(onRadioEvent);
    state = State::Idle;
    eventReady = false;
    ensureReceive();
  }

  int recvRaw(uint8_t *bytes, int capacity) override {
    int length = 0;
    if (state == State::Receive && eventReady) {
      eventReady = false;
      length = min(static_cast<int>(radio->getPacketLength()), capacity);
      lastRssi = radio->getRSSI(true);
      lastSnr = radio->getSNR();
      if (length <= 0 || radio->readData(bytes, length) != RADIOLIB_ERR_NONE) {
        length = 0;
      }
      state = State::Idle;
    }
    ensureReceive();
    return length;
  }

  uint32_t getEstAirtimeFor(int length) override {
    return radio->getTimeOnAir(length) / 1000;
  }

  float packetScore(float snr, int packetLength) override {
    if (snr < -7.5f) {
      return 0.0f;
    }
    return constrain(((snr + 7.5f) / 10.0f) *
                         (1.0f - packetLength / 256.0f),
                     0.0f, 1.0f);
  }

  // This build is deliberately receive-only. MeshCore cannot transmit through
  // this adapter, including guest logins, neighbour requests, or adverts.
  bool startSendRaw(const uint8_t *, int) override { return false; }
  bool isSendComplete() override { return false; }
  void onSendFinished() override {}
  bool isInRecvMode() const override { return state == State::Receive; }
  float getLastRSSI() const override { return lastRssi; }
  float getLastSNR() const override { return lastSnr; }
  int getNoiseFloor() const override { return -120; }

  bool reconfigure(float frequency, float bandwidth, uint8_t spreadingFactor,
                   uint8_t codingRate) {
    radio->standby();
    state = State::Idle;
    eventReady = false;
    int16_t result = radio->setFrequency(frequency);
    if (result == RADIOLIB_ERR_NONE) {
      result = radio->setBandwidth(bandwidth);
    }
    if (result == RADIOLIB_ERR_NONE) {
      result = radio->setSpreadingFactor(spreadingFactor);
    }
    if (result == RADIOLIB_ERR_NONE) {
      result = radio->setCodingRate(codingRate);
    }
    ensureReceive();
    if (result != RADIOLIB_ERR_NONE) {
      Serial.printf("WDG1 ERROR RADIO %d\n", result);
      return false;
    }
    return true;
  }

 private:
  enum class State : uint8_t { Idle, Receive };
  static SidecarRadioAdapter *active;

  static void onRadioEvent() {
    if (active != nullptr) {
      active->eventReady = true;
    }
  }

  void ensureReceive() {
    if (state != State::Idle) {
      return;
    }
    wdg_board::beforeReceive();
    if (radio->startReceive() == RADIOLIB_ERR_NONE) {
      state = State::Receive;
    }
  }

  SX1262 *radio;
  volatile bool eventReady = false;
  State state = State::Idle;
  float lastRssi = 0.0f;
  float lastSnr = 0.0f;
};

SidecarRadioAdapter *SidecarRadioAdapter::active = nullptr;

class SidecarMeshClient final : public BaseChatMesh {
 public:
  SidecarMeshClient(mesh::Radio &radio, mesh::MillisecondClock &milliseconds,
                    mesh::RNG &rng, mesh::RTCClock &rtc,
                    mesh::PacketManager &packets, mesh::MeshTables &tables)
      : BaseChatMesh(radio, milliseconds, rng, rtc, packets, tables) {}

  bool beginClient() {
    uint8_t identity[PRV_KEY_SIZE + PUB_KEY_SIZE];
    mesh::LocalIdentity generated(getRNG());
    const size_t length = generated.writeTo(identity, sizeof(identity));
    self_id.readFrom(identity, length);
    BaseChatMesh::begin();
    return true;
  }

  void service() {
    getRTCClock()->tick();
    BaseChatMesh::loop();
  }

 protected:
  void onDiscoveredContact(ContactInfo &contact, bool, uint8_t pathLength,
                           const uint8_t *) override;
  ContactInfo *processAck(const uint8_t *) override { return nullptr; }
  void onContactPathUpdated(const ContactInfo &) override {}
  void onMessageRecv(const ContactInfo &, mesh::Packet *, uint32_t,
                     const char *) override {}
  void onCommandDataRecv(const ContactInfo &, mesh::Packet *, uint32_t,
                         const char *) override {}
  void onSignedMessageRecv(const ContactInfo &, mesh::Packet *, uint32_t,
                           const uint8_t *, const char *) override {}
  uint32_t calcFloodTimeoutMillisFor(uint32_t airtime) const override {
    return 1000 + airtime * 16;
  }
  uint32_t calcDirectTimeoutMillisFor(uint32_t airtime,
                                      uint8_t pathLength) const override {
    return 1000 + airtime * 6 * ((pathLength & 0x3F) + 1);
  }
  void onSendTimeout() override {}
  void onChannelMessageRecv(const mesh::GroupChannel &, mesh::Packet *,
                            uint32_t, const char *) override {}
  bool allowPacketForward(const mesh::Packet *) override { return false; }
  uint8_t onContactRequest(const ContactInfo &, uint32_t, const uint8_t *,
                           uint8_t, uint8_t *) override {
    return 0;
  }
  void onContactResponse(const ContactInfo &, const uint8_t *,
                         uint8_t) override {}
  bool shouldOverwriteWhenFull() const override { return true; }
};

#if defined(NRF52_PLATFORM)
SX1262 lora = new Module(wdg_board::kLoRaNss, wdg_board::kLoRaDio1,
                         wdg_board::kLoRaReset, wdg_board::kLoRaBusy, SPI);
#else
SPIClass loraSpi(0);
SX1262 lora = new Module(wdg_board::kLoRaNss, wdg_board::kLoRaDio1,
                         wdg_board::kLoRaReset, wdg_board::kLoRaBusy, loraSpi);
#endif
SidecarRadioAdapter meshRadio(lora);
ArduinoMillis meshMilliseconds;
StdRNG meshRng;
VolatileRTCClock meshRtc;
SimpleMeshTables meshTables;
StaticPoolPacketManager meshPackets(12);
SidecarMeshClient meshClient(meshRadio, meshMilliseconds, meshRng, meshRtc,
                             meshPackets, meshTables);

bool displayReady = false;
bool loraReady = false;
bool hostLeaseActive = false;
bool hostAuthValid = false;
bool lastSendConfirmed = true;
uint32_t hostLeaseDeadline = 0;
uint32_t nextDisplayAt = 0;
uint32_t advertCount = 0;
uint32_t locatedAdvertCount = 0;
uint32_t wdgSentCount = 0;
uint32_t wdgAcceptedCount = 0;
float lastRssi = 0.0f;
float lastSnr = 0.0f;
float radioFrequency = kDefaultFrequencyMhz;
float radioBandwidth = kDefaultBandwidthKhz;
uint8_t radioSpreadingFactor = kDefaultSpreadingFactor;
uint8_t radioCodingRate = kDefaultCodingRate;
String regionSlug = "us-canada";
String serialBuffer;
volatile bool displayDirty = true;

bool hostReady() { return hostLeaseActive && hostAuthValid; }
void markDisplayDirty() { displayDirty = true; }

String hexField(const uint8_t *bytes, size_t length) {
  static constexpr char kHex[] = "0123456789abcdef";
  String value;
  value.reserve(length * 2);
  for (size_t index = 0; index < length; ++index) {
    value += kHex[bytes[index] >> 4];
    value += kHex[bytes[index] & 0x0F];
  }
  return value;
}

String jsonEscape(const char *text) {
  String escaped;
  if (text == nullptr) {
    return escaped;
  }
  for (const unsigned char *cursor =
           reinterpret_cast<const unsigned char *>(text);
       *cursor != 0; ++cursor) {
    if (*cursor == '"' || *cursor == '\\') {
      escaped += '\\';
      escaped += static_cast<char>(*cursor);
    } else if (*cursor >= 0x20) {
      escaped += static_cast<char>(*cursor);
    } else {
      escaped += ' ';
    }
  }
  return escaped;
}

const char *nodeType(uint8_t type) {
  switch (type) {
    case 1:
      return "Client";
    case 2:
      return "Repeater";
    case 3:
      return "Room";
    case 4:
      return "Sensor";
    default:
      return "Unknown";
  }
}

void renderDisplay() {
  if (!displayReady) {
    return;
  }
  displayDirty = false;

#if defined(WDG_BOARD_RCC6) || defined(WDG_BOARD_RC52)
  constexpr ColorVal kBlack = 0x0000;
  constexpr ColorVal kWhite = 0xFFFF;
  constexpr ColorVal kBlue = 0x001F;
  constexpr ColorVal kCyan = 0x07FF;
  constexpr ColorVal kGreen = 0x07E0;
  constexpr ColorVal kAmber = 0xFD20;
  constexpr ColorVal kRed = 0xF800;
  char line[48];

  display.startFrame(kBlack);
  display.setColor(kBlue);
  display.fillRect(0, 0, 220, 20);
  display.setTextSize(1);
  display.setColor(kWhite);
  display.setCursor(6, 3);
  display.print("WDG MESH USB");
  display.setColor(hostReady() ? kGreen :
                   (hostLeaseActive ? kAmber : kWhite));
  display.drawTextRightAlign(214, 3, hostReady() ? "USB READY" :
                                      (hostLeaseActive ? "KEY CHECK" :
                                                         "USB WAIT"));

  display.setTextSize(2);
  display.setColor(loraReady ? kCyan : kRed);
  display.drawTextCentered(110, 29,
                           loraReady ? "PASSIVE RX" : "RADIO ERROR");

  display.setTextSize(1);
  display.setColor(kWhite);
  snprintf(line, sizeof(line), "REGION %s", regionSlug.c_str());
  display.drawTextCentered(110, 58, line);
  snprintf(line, sizeof(line), "MESH %-6lu GPS %-6lu",
           static_cast<unsigned long>(advertCount),
           static_cast<unsigned long>(locatedAdvertCount));
  display.drawTextCentered(110, 77, line);
  display.setColor(kCyan);
  snprintf(line, sizeof(line), "RX %.3f SF%u BW%.1f", radioFrequency,
           static_cast<unsigned>(radioSpreadingFactor), radioBandwidth);
  display.drawTextCentered(110, 96, line);

  if (!hostLeaseActive) {
    display.setColor(kAmber);
    snprintf(line, sizeof(line), "RUN HOST SETUP / BRIDGE");
  } else if (!hostAuthValid) {
    display.setColor(kRed);
    snprintf(line, sizeof(line), "WDG KEY NOT VERIFIED");
  } else if (!lastSendConfirmed) {
    display.setColor(kRed);
    snprintf(line, sizeof(line), "WDG SEND UNCONFIRMED");
  } else {
    display.setColor(kGreen);
    snprintf(line, sizeof(line), "WDG +%lu  USB SERIAL ONLY",
             static_cast<unsigned long>(wdgAcceptedCount));
  }
  display.drawTextCentered(110, 113, line);
  display.endFrame();
#elif defined(WDG_BOARD_HELTEC_V3) || defined(WDG_BOARD_HELTEC_V4)
  constexpr ColorVal kBlack = 0;
  constexpr ColorVal kWhite = 1;
  char line[32];
  display.startFrame(kBlack);
  display.setColor(kWhite);
  display.setTextSize(1);
  snprintf(line, sizeof(line), "WDG MESH %s SERIAL",
#if defined(WDG_BOARD_HELTEC_V3)
           "V3"
#else
           "V4"
#endif
  );
  display.drawTextCentered(64, 0, line);
  display.drawTextCentered(64, 11,
                           hostReady() ? "USB READY" : "USB WAIT");
  snprintf(line, sizeof(line), "%s %.3f SF%u", regionSlug.c_str(),
           radioFrequency, static_cast<unsigned>(radioSpreadingFactor));
  display.drawTextCentered(64, 22, line);
  snprintf(line, sizeof(line), "MESH %lu GPS %lu",
           static_cast<unsigned long>(advertCount),
           static_cast<unsigned long>(locatedAdvertCount));
  display.drawTextCentered(64, 33, line);
  snprintf(line, sizeof(line), "WDG +%lu%s",
           static_cast<unsigned long>(wdgAcceptedCount),
           lastSendConfirmed ? "" : " !");
  display.drawTextCentered(64, 44, line);
  display.drawTextCentered(64, 55,
                           loraReady ? "PASSIVE MESHCORE RX" : "RADIO ERROR");
  display.endFrame();
#endif
}

void serviceDisplay() {
  if (!displayReady || !displayDirty ||
      static_cast<int32_t>(millis() - nextDisplayAt) < 0) {
    return;
  }
  renderDisplay();
  nextDisplayAt = millis() + kDisplayIntervalMs;
}

bool validRadioSettings(float frequency, float bandwidth,
                        unsigned spreadingFactor, unsigned codingRate) {
  return frequency >= 150.0f && frequency <= 960.0f &&
         bandwidth >= 7.0f && bandwidth <= 500.0f &&
         spreadingFactor >= 5 && spreadingFactor <= 12 &&
         codingRate >= 5 && codingRate <= 8;
}

void configureFromHost(float frequency, float bandwidth,
                       unsigned spreadingFactor, unsigned codingRate,
                       uint32_t epoch, const char *slug) {
  if (!validRadioSettings(frequency, bandwidth, spreadingFactor, codingRate)) {
    Serial.println("WDG1 ERROR SETTINGS");
    return;
  }

  const bool changed = fabsf(frequency - radioFrequency) > 0.0005f ||
                       fabsf(bandwidth - radioBandwidth) > 0.05f ||
                       spreadingFactor != radioSpreadingFactor ||
                       codingRate != radioCodingRate;
  if (changed &&
      !meshRadio.reconfigure(frequency, bandwidth, spreadingFactor,
                             codingRate)) {
    return;
  }
  radioFrequency = frequency;
  radioBandwidth = bandwidth;
  radioSpreadingFactor = static_cast<uint8_t>(spreadingFactor);
  radioCodingRate = static_cast<uint8_t>(codingRate);
  if (slug != nullptr && slug[0] != 0) {
    regionSlug = String(slug).substring(0, 20);
  }
  if (epoch >= 1700000000UL) {
    meshRtc.setCurrentTime(epoch);
  }
  hostLeaseActive = true;
  hostLeaseDeadline = millis() + kHostLeaseMs;
  markDisplayDirty();
  Serial.printf("WDG1 READY 1 %s %s\n", wdg_board::kBoardLabel,
                regionSlug.c_str());
}

void processSerialCommand(const String &line) {
  if (line == "CMD:screenshot:") {
    wdg_screenshot_requested = true;
    markDisplayDirty();
    return;
  }
  if (line == "WDG1 HELLO") {
    Serial.printf("WDG1 HELLO 1 %s %s\n", wdg_board::kBoardLabel,
                  wdg_board::kFirmwareVersion);
    return;
  }
  if (line == "WDG1 AUTH OK") {
    hostAuthValid = true;
    markDisplayDirty();
    return;
  }
  if (line == "WDG1 AUTH FAIL") {
    hostAuthValid = false;
    markDisplayDirty();
    return;
  }
  if (line.startsWith("WDG1 START ")) {
    float frequency = 0.0f;
    float bandwidth = 0.0f;
    unsigned spreadingFactor = 0;
    unsigned codingRate = 0;
    unsigned long epoch = 0;
    char slug[32]{};
    const int fields = sscanf(line.c_str(), "WDG1 START %f %f %u %u %lu %31s",
                              &frequency, &bandwidth, &spreadingFactor,
                              &codingRate, &epoch, slug);
    if (fields != 6) {
      Serial.println("WDG1 ERROR START");
      return;
    }
    configureFromHost(frequency, bandwidth, spreadingFactor, codingRate,
                      static_cast<uint32_t>(epoch), slug);
    return;
  }
  unsigned long sent = 0;
  unsigned long accepted = 0;
  if (sscanf(line.c_str(), "WDG1 ACK %lu %lu", &sent, &accepted) == 2) {
    wdgSentCount += static_cast<uint32_t>(sent);
    wdgAcceptedCount += static_cast<uint32_t>(accepted);
    lastSendConfirmed = true;
    markDisplayDirty();
    return;
  }
  if (line == "WDG1 SEND UNCONFIRMED") {
    lastSendConfirmed = false;
    markDisplayDirty();
    return;
  }
  if (line == "WDG1 STOP") {
    hostLeaseActive = false;
    hostAuthValid = false;
    markDisplayDirty();
  }
}

void serviceSerial() {
  while (Serial.available()) {
    const char value = static_cast<char>(Serial.read());
    if (value == '\n') {
      serialBuffer.trim();
      if (serialBuffer.length() > 0) {
        processSerialCommand(serialBuffer);
      }
      serialBuffer = "";
    } else if (value != '\r' && serialBuffer.length() < 255) {
      serialBuffer += value;
    }
  }
}

void serviceHostLease() {
  if (hostLeaseActive &&
      static_cast<int32_t>(millis() - hostLeaseDeadline) >= 0) {
    hostLeaseActive = false;
    hostAuthValid = false;
    markDisplayDirty();
    Serial.println("WDG1 LEASE EXPIRED");
  }
}

void SidecarMeshClient::onDiscoveredContact(ContactInfo &contact, bool,
                                             uint8_t pathLength,
                                             const uint8_t *) {
  reportVerifiedAdvert(contact, pathLength, meshRadio.getLastRSSI(),
                       meshRadio.getLastSNR());
}

void reportVerifiedAdvert(const ContactInfo &contact, uint8_t pathLength,
                          float rssi, float snr) {
  const bool hasLocation =
      contact.gps_lat >= -90000000 && contact.gps_lat <= 90000000 &&
      contact.gps_lon >= -180000000 && contact.gps_lon <= 180000000 &&
      (contact.gps_lat != 0 || contact.gps_lon != 0);
  advertCount++;
  lastRssi = rssi;
  lastSnr = snr;
  if (hasLocation) {
    locatedAdvertCount++;
  }
  markDisplayDirty();

  // BaseChatMesh invokes this callback only after the signed MeshCore advert
  // has been decoded and verified. Non-GPS adverts are counted but never sent.
  if (!hasLocation || !hostReady()) {
    return;
  }

  String record = F("WDG1 {\"node_id\":\"");
  record += hexField(contact.id.pub_key, 4);
  record += F("\",\"node_type\":\"");
  record += nodeType(contact.type);
  record += F("\",\"name\":\"");
  record += jsonEscape(contact.name);
  record += F("\",\"lat\":");
  record += String(contact.gps_lat / 1000000.0, 6);
  record += F(",\"lon\":");
  record += String(contact.gps_lon / 1000000.0, 6);
  record += F(",\"rssi\":");
  record += String(rssi, 1);
  record += F(",\"snr\":");
  record += String(snr, 1);
  record += F(",\"advert_timestamp\":");
  record += String(contact.last_advert_timestamp);
  record += F(",\"public_key\":\"");
  record += hexField(contact.id.pub_key, kPublicKeySize);
  record += F("\",\"path_hops\":");
  record += String(pathLength & 0x3F);
  record += '}';
  Serial.println(record);
}

bool setupLora() {
#if defined(NRF52_PLATFORM)
  SPI.setPins(wdg_board::kLoRaMiso, wdg_board::kLoRaSclk,
              wdg_board::kLoRaMosi);
  SPI.begin();
#else
  loraSpi.begin(wdg_board::kLoRaSclk, wdg_board::kLoRaMiso,
                wdg_board::kLoRaMosi);
#endif
  int16_t result = lora.begin(
      kDefaultFrequencyMhz, kDefaultBandwidthKhz, kDefaultSpreadingFactor,
      kDefaultCodingRate, RADIOLIB_SX126X_SYNC_WORD_PRIVATE,
      wdg_board::kRadioTxPowerDbm, kPreambleSymbols, kTcxoVoltage, false);
  if (result == RADIOLIB_ERR_SPI_CMD_FAILED ||
      result == RADIOLIB_ERR_SPI_CMD_INVALID) {
    result = lora.begin(
        kDefaultFrequencyMhz, kDefaultBandwidthKhz, kDefaultSpreadingFactor,
        kDefaultCodingRate, RADIOLIB_SX126X_SYNC_WORD_PRIVATE,
        wdg_board::kRadioTxPowerDbm, kPreambleSymbols, 0.0f, false);
  }
  if (result != RADIOLIB_ERR_NONE) {
    Serial.printf("WDG1 ERROR RADIO_INIT %d\n", result);
    return false;
  }
  if ((result = lora.setCRC(1)) != RADIOLIB_ERR_NONE ||
      (result = lora.setCurrentLimit(140)) != RADIOLIB_ERR_NONE ||
      (result = lora.setDio2AsRfSwitch(true)) != RADIOLIB_ERR_NONE ||
      (result = lora.setRxBoostedGainMode(true)) != RADIOLIB_ERR_NONE ||
      !wdg_board::applyRadioConfiguration(lora)) {
    Serial.printf("WDG1 ERROR RADIO_CONFIG %d\n", result);
    return false;
  }
#if defined(NRF52_PLATFORM)
  meshRng.begin(static_cast<long>(micros() ^ analogRead(A0)));
#else
  meshRng.begin(static_cast<long>(esp_random()));
#endif
  meshClient.beginClient();
  Serial.printf("WDG1 RADIO %.3f %.1f %u %u PASSIVE\n",
                kDefaultFrequencyMhz, kDefaultBandwidthKhz,
                kDefaultSpreadingFactor, kDefaultCodingRate);
  return true;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.printf("\nCanadaverse WDG Mesh Serial Sidecar %s on %s\n",
                wdg_board::kFirmwareVersion, wdg_board::kBoardLabel);
  Serial.println("Mode: passive MeshCore RX; USB serial only; no Wi-Fi/BLE");

  wdg_board::begin();
#if defined(WDG_BOARD_RCC6) || defined(WDG_BOARD_RC52) || defined(WDG_BOARD_HELTEC_V3) || \
    defined(WDG_BOARD_HELTEC_V4)
  displayReady = display.begin();
#else
  displayReady = false;
#endif
  loraReady = setupLora();
  markDisplayDirty();
  renderDisplay();
  Serial.printf("WDG1 HELLO 1 %s %s\n", wdg_board::kBoardLabel,
                wdg_board::kFirmwareVersion);
}

void loop() {
  serviceSerial();
  serviceHostLease();
  if (loraReady) {
    meshClient.service();
  }
  serviceDisplay();
  delay(3);
}
