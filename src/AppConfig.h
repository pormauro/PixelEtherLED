#pragma once

#include <Arduino.h>

enum class LedChipType : uint8_t {
  WS2811 = 0,
  WS2812B,
  SK6812,
  CHIP_TYPE_COUNT
};

enum class LedColorOrder : uint8_t {
  RGB = 0,
  RBG,
  GRB,
  GBR,
  BRG,
  BGR,
  COLOR_ORDER_COUNT
};

constexpr uint8_t LED_DATA_PIN        = 2;
constexpr uint16_t MAX_LEDS           = 1024;
constexpr uint16_t DEFAULT_NUM_LEDS   = 60;
constexpr uint16_t DEFAULT_START_UNIVERSE = 0;
constexpr uint16_t DEFAULT_PIXELS_PER_UNIVERSE = 170;  // 512/3
constexpr uint8_t  DEFAULT_BRIGHTNESS = 255;

struct AppConfig {
  uint32_t dhcpTimeoutMs;
  uint16_t numLeds;
  uint16_t startUniverse;
  uint16_t pixelsPerUniverse;
  uint8_t brightness;
  uint8_t chipType;
  uint8_t colorOrder;
  bool     useDhcp;
  bool     fallbackToStatic;
  uint32_t staticIp;
  uint32_t staticGateway;
  uint32_t staticSubnet;
  uint32_t staticDns1;
  uint32_t staticDns2;
  bool     wifiEnabled;
  bool     wifiApMode;
  uint8_t  artnetInput;
  String   wifiStaSsid;
  String   wifiStaPassword;
  String   wifiApSsid;
  String   wifiApPassword;
};

AppConfig makeDefaultConfig();
String ipToString(uint32_t ipValue);
uint32_t parseIp(const String& text, uint32_t fallback);
const char* getChipName(uint8_t value);
const char* getColorOrderName(uint8_t value);

extern const char* const CHIP_TYPE_NAMES[];
extern const char* const COLOR_ORDER_NAMES[];
