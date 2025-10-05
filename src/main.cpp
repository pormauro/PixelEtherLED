#include <Arduino.h>
#include <WiFi.h>
#include <ETH.h>
#include <WiFiUdp.h>
#include "ArtNetNode.h"
#include <FastLED.h>
#include <Preferences.h>
#include <WebServer.h>
#include <Update.h>
#include <algorithm>
#include <vector>

// ===================== CONFIG RED (IP FIJA - FALLBACK) =====================
//
// Nota: el fallback por DHCP debe estar en la misma subred que la mayoría de los
// controladores Art-Net residenciales.  Originalmente usábamos 192.168.1.50, pero
// varias instalaciones domésticas operan en 192.168.0.x, lo que impedía que el
// host controlador viera las respuestas de ArtPoll cuando el ESP32 tomaba la IP
// de respaldo.  Con este cambio, en caso de fallo de DHCP el equipo tomará una IP
// dentro del rango 192.168.0.x y permanecerá visible para los escáneres Art-Net.
static const IPAddress STATIC_IP   (192, 168, 0, 50);
static const IPAddress STATIC_GW   (192, 168, 0, 1);
static const IPAddress STATIC_MASK (255, 255, 255, 0);
static const IPAddress STATIC_DNS1 (1, 1, 1, 1);
static const IPAddress STATIC_DNS2 (8, 8, 8, 8);

constexpr bool     DEFAULT_USE_DHCP          = true;
constexpr bool     DEFAULT_FALLBACK_TO_STATIC = true;
constexpr bool     DEFAULT_WIFI_ENABLED       = false;
constexpr bool     DEFAULT_WIFI_AP_MODE       = true;
const char* const  DEFAULT_WIFI_STA_SSID      = "";
const char* const  DEFAULT_WIFI_STA_PASSWORD  = "";
const char* const  DEFAULT_WIFI_AP_SSID       = "PixelEtherLED";
const char* const  DEFAULT_WIFI_AP_PASSWORD   = "";
constexpr uint8_t  DEFAULT_ARTNET_INPUT       = static_cast<uint8_t>(ArtNetNode::InterfacePreference::Ethernet);
constexpr const char* DEVICE_HOSTNAME         = "esp32-artnet";
const uint32_t DEFAULT_STATIC_IP         = static_cast<uint32_t>(STATIC_IP);
const uint32_t DEFAULT_STATIC_GW         = static_cast<uint32_t>(STATIC_GW);
const uint32_t DEFAULT_STATIC_MASK       = static_cast<uint32_t>(STATIC_MASK);
const uint32_t DEFAULT_STATIC_DNS1       = static_cast<uint32_t>(STATIC_DNS1);
const uint32_t DEFAULT_STATIC_DNS2       = static_cast<uint32_t>(STATIC_DNS2);

// ===================== LEDS =====================
constexpr uint8_t LED_DATA_PIN        = 2;
constexpr uint16_t MAX_LEDS             = 1024;
constexpr uint16_t DEFAULT_NUM_LEDS     = 60;
constexpr uint16_t DEFAULT_START_UNIVERSE = 0;
constexpr uint16_t DEFAULT_PIXELS_PER_UNIVERSE = 170;      // 512/3
constexpr uint8_t  DEFAULT_BRIGHTNESS   = 255;
constexpr uint32_t DEFAULT_DHCP_TIMEOUT = 3000;             // ms

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

constexpr uint8_t DEFAULT_CHIP_TYPE   = static_cast<uint8_t>(LedChipType::WS2811);
constexpr uint8_t DEFAULT_COLOR_ORDER = static_cast<uint8_t>(LedColorOrder::BRG);

const char* const CHIP_TYPE_NAMES[] = {
  "WS2811",
  "WS2812B",
  "SK6812"
};

const char* const COLOR_ORDER_NAMES[] = {
  "RGB",
  "RBG",
  "GRB",
  "GBR",
  "BRG",
  "BGR"
};

CRGB leds[MAX_LEDS];

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
void restoreFactoryDefaults();
bool checkFactoryResetOnBoot();
void bringUpEthernet(const AppConfig& config);
void bringUpWiFi(const AppConfig& config);
void handleWifiScan();
String htmlEscape(const String& text);
String jsonEscape(const String& text);
String wifiAuthModeToText(wifi_auth_mode_t mode);

AppConfig g_config = makeDefaultConfig();

constexpr uint8_t FACTORY_RESET_PIN = 36;  // Entrada I4
constexpr bool    FACTORY_RESET_ACTIVE_LOW = true;
constexpr uint32_t FACTORY_RESET_HOLD_MS  = 10000;

AppConfig makeDefaultConfig()
{
  AppConfig cfg{};
  cfg.dhcpTimeoutMs   = DEFAULT_DHCP_TIMEOUT;
  cfg.numLeds         = DEFAULT_NUM_LEDS;
  cfg.startUniverse   = DEFAULT_START_UNIVERSE;
  cfg.pixelsPerUniverse = DEFAULT_PIXELS_PER_UNIVERSE;
  cfg.brightness      = DEFAULT_BRIGHTNESS;
  cfg.chipType        = DEFAULT_CHIP_TYPE;
  cfg.colorOrder      = DEFAULT_COLOR_ORDER;
  cfg.useDhcp         = DEFAULT_USE_DHCP;
  cfg.fallbackToStatic = DEFAULT_FALLBACK_TO_STATIC;
  cfg.staticIp        = DEFAULT_STATIC_IP;
  cfg.staticGateway   = DEFAULT_STATIC_GW;
  cfg.staticSubnet    = DEFAULT_STATIC_MASK;
  cfg.staticDns1      = DEFAULT_STATIC_DNS1;
  cfg.staticDns2      = DEFAULT_STATIC_DNS2;
  cfg.wifiEnabled     = DEFAULT_WIFI_ENABLED;
  cfg.wifiApMode      = DEFAULT_WIFI_AP_MODE;
  cfg.artnetInput     = DEFAULT_ARTNET_INPUT;
  cfg.wifiStaSsid     = DEFAULT_WIFI_STA_SSID;
  cfg.wifiStaPassword = DEFAULT_WIFI_STA_PASSWORD;
  cfg.wifiApSsid      = DEFAULT_WIFI_AP_SSID;
  cfg.wifiApPassword  = DEFAULT_WIFI_AP_PASSWORD;
  return cfg;
}

String ipToString(uint32_t ipValue)
{
  IPAddress ip(ipValue);
  return ip.toString();
}

uint32_t parseIp(const String& text, uint32_t fallback)
{
  IPAddress ip;
  if (ip.fromString(text)) {
    return static_cast<uint32_t>(ip);
  }
  return fallback;
}

String htmlEscape(const String& text)
{
  String out;
  out.reserve(text.length());
  for (size_t i = 0; i < text.length(); ++i) {
    const char c = text[i];
    switch (c) {
      case '&': out += F("&amp;"); break;
      case '<': out += F("&lt;"); break;
      case '>': out += F("&gt;"); break;
      case '"': out += F("&quot;"); break;
      case 39: out += F("&#39;"); break;
      default: out += c; break;
    }
  }
  return out;
}

String jsonEscape(const String& text)
{
  String out;
  out.reserve(text.length() + 8);
  for (size_t i = 0; i < text.length(); ++i) {
    const char c = text[i];
    switch (c) {
      case '\\': out += F("\\\\"); break;
      case '"': out += F("\\\""); break;
      case '\n': out += F("\\n"); break;
      case '\r': break;
      case '\t': out += F("\\t"); break;
      default:
        if (static_cast<uint8_t>(c) < 0x20) {
          char buf[7];
          snprintf(buf, sizeof(buf), "\\u%04X", static_cast<uint8_t>(c));
          out += buf;
        } else {
          out += c;
        }
        break;
    }
  }
  return out;
}

String wifiAuthModeToText(wifi_auth_mode_t mode)
{
  switch (mode) {
    case WIFI_AUTH_OPEN: return F("Abierta");
    case WIFI_AUTH_WEP: return F("WEP");
    case WIFI_AUTH_WPA_PSK: return F("WPA-PSK");
    case WIFI_AUTH_WPA2_PSK: return F("WPA2-PSK");
    case WIFI_AUTH_WPA_WPA2_PSK: return F("WPA/WPA2-PSK");
    case WIFI_AUTH_WPA2_ENTERPRISE: return F("WPA2-Enterprise");
    case WIFI_AUTH_WPA3_PSK: return F("WPA3-PSK");
    case WIFI_AUTH_WPA2_WPA3_PSK: return F("WPA2/WPA3-PSK");
    case WIFI_AUTH_WAPI_PSK: return F("WAPI-PSK");
    default: return F("Desconocido");
  }
}

Preferences g_prefs;
WebServer g_server(80);

bool g_firmwareUploadHandled = false;
bool g_firmwareUpdateShouldRestart = false;
String g_firmwareUpdateMessage;

static bool wifi_sta_running   = false;
static bool wifi_sta_connected = false;
static bool wifi_sta_has_ip    = false;
static bool wifi_ap_running    = false;
static IPAddress wifi_sta_ip;
static IPAddress wifi_ap_ip;
static String wifi_sta_ssid_current;

bool isFactoryResetPressed()
{
  int level = digitalRead(FACTORY_RESET_PIN);
  if (FACTORY_RESET_ACTIVE_LOW) {
    return level == LOW;
  }
  return level == HIGH;
}

bool checkFactoryResetOnBoot()
{
  if (FACTORY_RESET_ACTIVE_LOW) {
    pinMode(FACTORY_RESET_PIN, INPUT_PULLUP);
  } else {
    pinMode(FACTORY_RESET_PIN, INPUT);
  }

  if (!isFactoryResetPressed()) {
    return false;
  }

  Serial.println("[CFG] Botón de reset detectado. Mantener presionado 10 segundos para restaurar.");

  const uint32_t start = millis();
  while (millis() - start < FACTORY_RESET_HOLD_MS) {
    if (!isFactoryResetPressed()) {
      Serial.println("[CFG] Restablecimiento cancelado.");
      return false;
    }
    delay(50);
  }

  Serial.println("[CFG] Restablecimiento confirmado.");
  return true;
}

void restoreFactoryDefaults()
{
  Serial.println("[CFG] Restaurando valores de fábrica...");
  if (g_prefs.begin("pixelcfg", false)) {
    g_prefs.clear();
    g_prefs.end();
  }
  g_config = makeDefaultConfig();
}

// ===================== ART-NET =====================
ArtNetNode artnet;
std::vector<uint8_t> g_universeReceived;
uint16_t g_universeCount = 0;

// ===================== DEBUG DMX =====================
//#define DMX_DEBUG                      1
#define DMX_DEBUG_LED_INDEX            0     // LED que mostramos por serie
#define DMX_DEBUG_CHANNELS_TO_PRINT    12    // Primeros N canales del paquete
#define DMX_DEBUG_MIN_INTERVAL_MS      200   // Evitar spam serie
uint32_t g_dmxFrames = 0;

// ===================== ETHERNET (WT32-ETH01 / LAN8720) =====================
#define ETH_PHY_ADDR   1
#define ETH_PHY_TYPE   ETH_PHY_LAN8720
#define ETH_MDC_PIN    23
#define ETH_MDIO_PIN   18
#define ETH_POWER_PIN  16
#define ETH_CLK_MODE   ETH_CLOCK_GPIO0_IN   // clock externo 50MHz en GPIO0

static bool eth_link_up = false;
static bool eth_has_ip  = false;

void onWiFiEvent(WiFiEvent_t event)
{
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      Serial.println("[ETH] START");
      ETH.setHostname(DEVICE_HOSTNAME);
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      Serial.println("[ETH] LINK UP");
      eth_link_up = true;
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      Serial.print("[ETH] DHCP IP: ");
      Serial.println(ETH.localIP());
      eth_has_ip = true;
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      Serial.println("[ETH] LINK DOWN");
      eth_link_up = false;
      eth_has_ip  = false;
      break;
    case ARDUINO_EVENT_ETH_STOP:
      Serial.println("[ETH] STOP");
      eth_link_up = false;
      eth_has_ip  = false;
      break;
    case ARDUINO_EVENT_WIFI_STA_START:
      Serial.println("[WIFI] STA start");
      wifi_sta_running = true;
      wifi_sta_connected = false;
      wifi_sta_has_ip = false;
      wifi_sta_ip = IPAddress((uint32_t)0);
      break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.println("[WIFI] STA connected");
      wifi_sta_connected = true;
      wifi_sta_ssid_current = WiFi.SSID();
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.print("[WIFI] STA IP: ");
      Serial.println(WiFi.localIP());
      wifi_sta_has_ip = true;
      wifi_sta_ip = WiFi.localIP();
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println("[WIFI] STA disconnected");
      wifi_sta_connected = false;
      wifi_sta_has_ip = false;
      wifi_sta_ip = IPAddress((uint32_t)0);
      wifi_sta_ssid_current.clear();
      break;
    case ARDUINO_EVENT_WIFI_STA_STOP:
      Serial.println("[WIFI] STA stop");
      wifi_sta_running = false;
      wifi_sta_connected = false;
      wifi_sta_has_ip = false;
      wifi_sta_ip = IPAddress((uint32_t)0);
      wifi_sta_ssid_current.clear();
      break;
    case ARDUINO_EVENT_WIFI_AP_START:
      Serial.println("[WIFI] AP start");
      wifi_ap_running = true;
      wifi_ap_ip = WiFi.softAPIP();
      break;
    case ARDUINO_EVENT_WIFI_AP_STOP:
      Serial.println("[WIFI] AP stop");
      wifi_ap_running = false;
      wifi_ap_ip = IPAddress((uint32_t)0);
      break;
    case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
      wifi_ap_ip = WiFi.softAPIP();
      break;
    default:
      break;
  }
}

// ======== CALLBACK Art-Net (firma con IP de origen) ========
void applyConfig();
void saveConfig();
void handleConfigGet();
void handleConfigPost();
void handleRoot();
String buildConfigPage(const String& message = String());
void handleFirmwareUpdatePost();
void handleFirmwareUpload();
String buildVisualizerPage();
void handleVisualizerGet();
void handleLedStateJson();

void onDmxFrame(uint16_t universe, uint16_t length, uint8_t sequence,
                uint8_t* data, IPAddress remoteIP)
{
  g_dmxFrames++;

  const uint16_t universeStart = g_config.startUniverse;
  if (universe < universeStart || universe >= (universeStart + g_universeCount)) return;

  const uint16_t idxU = universe - universeStart;
  const uint16_t pixelOffset = idxU * g_config.pixelsPerUniverse;

  const uint16_t maxPixThisU    = min<uint16_t>(g_config.pixelsPerUniverse, g_config.numLeds - pixelOffset);
  const uint16_t pixelsInPacket = min<uint16_t>(length / 3, maxPixThisU);

  for (uint16_t i = 0; i < pixelsInPacket; i++) {
    const uint16_t ledIndex = pixelOffset + i;
    const uint8_t r = data[i * 3 + 0];
    const uint8_t g = data[i * 3 + 1];
    const uint8_t b = data[i * 3 + 2];
    leds[ledIndex].setRGB(r, g, b);
  }

  if (idxU < g_universeReceived.size()) {
    g_universeReceived[idxU] = 1;
  }

  // ======== PRINT DEBUG (rate-limited) ========
#if DMX_DEBUG
  static uint32_t lastPrintMs = 0;
  if (millis() - lastPrintMs >= DMX_DEBUG_MIN_INTERVAL_MS) {
    lastPrintMs = millis();
    String rip = remoteIP.toString();
    Serial.printf("[DMX] U=%u len=%u seq=%u src=%s frames=%lu\n",
                  universe, length, sequence, rip.c_str(), (unsigned long)g_dmxFrames);

    // Primeros N canales del paquete recibido (ojo: son del universo actual)
    uint16_t toShow = min<uint16_t>(DMX_DEBUG_CHANNELS_TO_PRINT, length);
    Serial.print("  ch[0..]:");
    for (uint16_t i = 0; i < toShow; i++) {
      Serial.printf(" %u", data[i]);
    }

    // Mostrar color del LED de referencia (por defecto, 0 o el primero del universo)
    uint16_t ledIndexToShow = DMX_DEBUG_LED_INDEX;
    // Si ese LED NO pertenece a este universo, mostramos el primero de este universo
    if (ledIndexToShow < pixelOffset || ledIndexToShow >= (pixelOffset + maxPixThisU)) {
      ledIndexToShow = pixelOffset; // primer LED de este universo
    }
    if (ledIndexToShow < g_config.numLeds) {
      CRGB c = leds[ledIndexToShow];
      Serial.printf("\n  LED[%u]=(%u,%u,%u)\n", ledIndexToShow, c.r, c.g, c.b);
    } else {
      Serial.println();
    }
  }
#endif

  // Actualizar cuando recibimos al menos un paquete de cada universo
  bool all = true;
  for (uint16_t i = 0; i < g_universeCount; i++) { if (!g_universeReceived[i]) { all = false; break; } }
  if (all) {
    FastLED.show();
    std::fill(g_universeReceived.begin(), g_universeReceived.end(), 0);
  }
}

template <typename T>
T clampValue(T value, T minValue, T maxValue)
{
  return std::max(minValue, std::min(value, maxValue));
}

uint8_t clampIndex(uint8_t value, uint8_t maxValue, uint8_t fallback)
{
  if (value >= maxValue) return fallback;
  return value;
}

void normalizeConfig(AppConfig& config)
{
  config.numLeds = clampValue<uint16_t>(config.numLeds, 1, MAX_LEDS);
  config.pixelsPerUniverse = clampValue<uint16_t>(config.pixelsPerUniverse, 1, MAX_LEDS);
  config.brightness = clampValue<uint8_t>(config.brightness, 1, 255);
  if (config.dhcpTimeoutMs < 500) {
    config.dhcpTimeoutMs = 500; // mínimo razonable
  }
  config.chipType   = clampIndex(config.chipType, static_cast<uint8_t>(LedChipType::CHIP_TYPE_COUNT), DEFAULT_CHIP_TYPE);
  config.colorOrder = clampIndex(config.colorOrder, static_cast<uint8_t>(LedColorOrder::COLOR_ORDER_COUNT), DEFAULT_COLOR_ORDER);
  config.useDhcp = config.useDhcp ? true : false;
  config.fallbackToStatic = config.fallbackToStatic ? true : false;
  config.wifiEnabled = config.wifiEnabled ? true : false;
  config.wifiApMode  = config.wifiApMode ? true : false;
  config.artnetInput = clampValue<uint8_t>(config.artnetInput, 0,
                                           static_cast<uint8_t>(ArtNetNode::InterfacePreference::Auto));

  config.wifiStaSsid.trim();
  config.wifiStaPassword.trim();
  config.wifiApSsid.trim();
  config.wifiApPassword.trim();

  if (config.wifiStaSsid.length() > 32) {
    config.wifiStaSsid = config.wifiStaSsid.substring(0, 32);
  }
  if (config.wifiStaPassword.length() > 64) {
    config.wifiStaPassword = config.wifiStaPassword.substring(0, 64);
  }
  if (config.wifiApSsid.length() == 0) {
    config.wifiApSsid = DEFAULT_WIFI_AP_SSID;
  } else if (config.wifiApSsid.length() > 32) {
    config.wifiApSsid = config.wifiApSsid.substring(0, 32);
  }
  if (config.wifiApPassword.length() > 0 && config.wifiApPassword.length() < 8) {
    config.wifiApPassword.clear();
  }
  if (config.wifiApPassword.length() > 64) {
    config.wifiApPassword = config.wifiApPassword.substring(0, 64);
  }

  if (config.staticSubnet == 0) {
    config.staticSubnet = DEFAULT_STATIC_MASK;
  }
  if (config.staticIp == 0) {
    config.staticIp = DEFAULT_STATIC_IP;
  }
  if (config.staticGateway == 0) {
    config.staticGateway = DEFAULT_STATIC_GW;
  }
  if (config.staticDns1 == 0) {
    config.staticDns1 = config.staticGateway != 0 ? config.staticGateway : DEFAULT_STATIC_DNS1;
  }
  if (config.staticDns2 == 0) {
    config.staticDns2 = DEFAULT_STATIC_DNS2;
  }
}

template <template<uint8_t DATA_PIN, fl::EOrder RGB_ORDER> class CHIPSET>
void addFastLedControllerForOrder(LedColorOrder order)
{
  switch (order) {
    case LedColorOrder::RGB: FastLED.addLeds<CHIPSET, LED_DATA_PIN, RGB>(leds, MAX_LEDS); break;
    case LedColorOrder::RBG: FastLED.addLeds<CHIPSET, LED_DATA_PIN, RBG>(leds, MAX_LEDS); break;
    case LedColorOrder::GRB: FastLED.addLeds<CHIPSET, LED_DATA_PIN, GRB>(leds, MAX_LEDS); break;
    case LedColorOrder::GBR: FastLED.addLeds<CHIPSET, LED_DATA_PIN, GBR>(leds, MAX_LEDS); break;
    case LedColorOrder::BRG: FastLED.addLeds<CHIPSET, LED_DATA_PIN, BRG>(leds, MAX_LEDS); break;
    case LedColorOrder::BGR: FastLED.addLeds<CHIPSET, LED_DATA_PIN, BGR>(leds, MAX_LEDS); break;
    default: FastLED.addLeds<CHIPSET, LED_DATA_PIN, BRG>(leds, MAX_LEDS); break;
  }
}

void initializeFastLEDController()
{
  static bool initialized = false;
  if (initialized) return;

  LedChipType chip = static_cast<LedChipType>(clampIndex(g_config.chipType, static_cast<uint8_t>(LedChipType::CHIP_TYPE_COUNT), DEFAULT_CHIP_TYPE));
  LedColorOrder order = static_cast<LedColorOrder>(clampIndex(g_config.colorOrder, static_cast<uint8_t>(LedColorOrder::COLOR_ORDER_COUNT), DEFAULT_COLOR_ORDER));

  switch (chip) {
    case LedChipType::WS2811:
      addFastLedControllerForOrder<WS2811>(order);
      break;
    case LedChipType::WS2812B:
      addFastLedControllerForOrder<WS2812B>(order);
      break;
    case LedChipType::SK6812:
      addFastLedControllerForOrder<SK6812>(order);
      break;
    default:
      addFastLedControllerForOrder<WS2811>(order);
      break;
  }

  initialized = true;
}

const char* getChipName(uint8_t value)
{
  uint8_t idx = clampIndex(value, static_cast<uint8_t>(LedChipType::CHIP_TYPE_COUNT), DEFAULT_CHIP_TYPE);
  return CHIP_TYPE_NAMES[idx];
}

const char* getColorOrderName(uint8_t value)
{
  uint8_t idx = clampIndex(value, static_cast<uint8_t>(LedColorOrder::COLOR_ORDER_COUNT), DEFAULT_COLOR_ORDER);
  return COLOR_ORDER_NAMES[idx];
}

void loadConfig()
{
  g_config = makeDefaultConfig();

  if (g_prefs.begin("pixelcfg", true)) {
    g_config.dhcpTimeoutMs   = g_prefs.getUInt("dhcpTimeout", g_config.dhcpTimeoutMs);
    g_config.numLeds         = g_prefs.getUShort("numLeds", g_config.numLeds);
    g_config.startUniverse   = g_prefs.getUShort("startUni", g_config.startUniverse);
    g_config.pixelsPerUniverse = g_prefs.getUShort("pixPerUni", g_config.pixelsPerUniverse);
    g_config.brightness      = g_prefs.getUChar("brightness", g_config.brightness);
    g_config.chipType        = g_prefs.getUChar("chipType", g_config.chipType);
    g_config.colorOrder      = g_prefs.getUChar("colorOrder", g_config.colorOrder);
    g_config.useDhcp         = g_prefs.getBool("useDhcp", g_config.useDhcp);
    g_config.fallbackToStatic = g_prefs.getBool("dhcpFallback", g_config.fallbackToStatic);
    g_config.staticIp        = g_prefs.getUInt("staticIp", g_config.staticIp);
    g_config.staticGateway   = g_prefs.getUInt("staticGw", g_config.staticGateway);
    g_config.staticSubnet    = g_prefs.getUInt("staticMask", g_config.staticSubnet);
    g_config.staticDns1      = g_prefs.getUInt("staticDns1", g_config.staticDns1);
    g_config.staticDns2      = g_prefs.getUInt("staticDns2", g_config.staticDns2);
    g_config.wifiEnabled     = g_prefs.getBool("wifiEnabled", g_config.wifiEnabled);
    g_config.wifiApMode      = g_prefs.getBool("wifiApMode", g_config.wifiApMode);
    g_config.artnetInput     = g_prefs.getUChar("artnetInput", g_config.artnetInput);
    g_config.wifiStaSsid     = g_prefs.getString("wifiStaSsid", g_config.wifiStaSsid);
    g_config.wifiStaPassword = g_prefs.getString("wifiStaPass", g_config.wifiStaPassword);
    g_config.wifiApSsid      = g_prefs.getString("wifiApSsid", g_config.wifiApSsid);
    g_config.wifiApPassword  = g_prefs.getString("wifiApPass", g_config.wifiApPassword);
    g_prefs.end();
  }

  normalizeConfig(g_config);
}

void saveConfig()
{
  if (g_prefs.begin("pixelcfg", false)) {
    g_prefs.putUInt("dhcpTimeout", g_config.dhcpTimeoutMs);
    g_prefs.putUShort("numLeds", g_config.numLeds);
    g_prefs.putUShort("startUni", g_config.startUniverse);
    g_prefs.putUShort("pixPerUni", g_config.pixelsPerUniverse);
    g_prefs.putUChar("brightness", g_config.brightness);
    g_prefs.putUChar("chipType", g_config.chipType);
    g_prefs.putUChar("colorOrder", g_config.colorOrder);
    g_prefs.putBool("useDhcp", g_config.useDhcp);
    g_prefs.putBool("dhcpFallback", g_config.fallbackToStatic);
    g_prefs.putUInt("staticIp", g_config.staticIp);
    g_prefs.putUInt("staticGw", g_config.staticGateway);
    g_prefs.putUInt("staticMask", g_config.staticSubnet);
    g_prefs.putUInt("staticDns1", g_config.staticDns1);
    g_prefs.putUInt("staticDns2", g_config.staticDns2);
    g_prefs.putBool("wifiEnabled", g_config.wifiEnabled);
    g_prefs.putBool("wifiApMode", g_config.wifiApMode);
    g_prefs.putUChar("artnetInput", g_config.artnetInput);
    g_prefs.putString("wifiStaSsid", g_config.wifiStaSsid);
    g_prefs.putString("wifiStaPass", g_config.wifiStaPassword);
    g_prefs.putString("wifiApSsid", g_config.wifiApSsid);
    g_prefs.putString("wifiApPass", g_config.wifiApPassword);
    g_prefs.end();
  }
}

void applyConfig()
{
  normalizeConfig(g_config);
  g_universeCount = (g_config.numLeds + g_config.pixelsPerUniverse - 1) / g_config.pixelsPerUniverse;
  g_universeCount = std::max<uint16_t>(1, g_universeCount);
  g_universeReceived.assign(g_universeCount, 0);

  artnet.setUniverseInfo(g_config.startUniverse, g_universeCount);

  ArtNetNode::InterfacePreference pref = ArtNetNode::InterfacePreference::Ethernet;
  if (g_config.artnetInput == static_cast<uint8_t>(ArtNetNode::InterfacePreference::WiFi)) {
    pref = ArtNetNode::InterfacePreference::WiFi;
  } else if (g_config.artnetInput == static_cast<uint8_t>(ArtNetNode::InterfacePreference::Auto)) {
    pref = ArtNetNode::InterfacePreference::Auto;
  }
  artnet.setInterfacePreference(pref);

  FastLED.setBrightness(g_config.brightness);
  FastLED.clear(true);
}

String buildConfigPage(const String& message)
{
  String html;
  html.reserve(8192);
  artnet.updateNetworkInfo();

  const bool usingDhcp = g_config.useDhcp;
  const String fallbackLabel = g_config.fallbackToStatic ? "Aplicar IP fija configurada" : "Mantener sin IP";
  const String staticIpStr   = ipToString(g_config.staticIp);
  const String staticGwStr   = ipToString(g_config.staticGateway);
  const String staticMaskStr = ipToString(g_config.staticSubnet);
  const String staticDns1Str = ipToString(g_config.staticDns1);
  const String staticDns2Str = ipToString(g_config.staticDns2);
  const bool   wifiEnabled   = g_config.wifiEnabled;
  const bool   wifiApMode    = g_config.wifiApMode;
  const uint8_t artnetInputValue = g_config.artnetInput;
  const String wifiStaSsidEsc = htmlEscape(g_config.wifiStaSsid);
  const String wifiStaPassEsc = htmlEscape(g_config.wifiStaPassword);
  const String wifiApSsidEsc  = htmlEscape(g_config.wifiApSsid);
  const String wifiApPassEsc  = htmlEscape(g_config.wifiApPassword);
  IPAddress artnetIp = artnet.localIp();
  String artnetIpStr = artnetIp != IPAddress((uint32_t)0) ? artnetIp.toString() : String("-");
  String artnetInputLabel;
  switch (artnetInputValue) {
    case static_cast<uint8_t>(ArtNetNode::InterfacePreference::WiFi):
      artnetInputLabel = F("Wi-Fi");
      break;
    case static_cast<uint8_t>(ArtNetNode::InterfacePreference::Auto):
      artnetInputLabel = F("Automático");
      break;
    case static_cast<uint8_t>(ArtNetNode::InterfacePreference::Ethernet):
    default:
      artnetInputLabel = F("Ethernet");
      break;
  }
  IPAddress wifiCurrentIp = wifi_sta_has_ip ? wifi_sta_ip : (wifi_ap_running ? wifi_ap_ip : IPAddress((uint32_t)0));
  String wifiIpStr = wifiCurrentIp != IPAddress((uint32_t)0) ? wifiCurrentIp.toString() : String("-");
  String wifiStatusText;
  if (!wifiEnabled) {
    wifiStatusText = F("Deshabilitado");
  } else if (wifiApMode) {
    wifiStatusText = wifi_ap_running ? F("AP activo") : F("Inicializando AP");
  } else {
    if (wifi_sta_connected) {
      wifiStatusText = wifi_sta_has_ip ? F("Conectado") : F("Sin IP (conectando)");
    } else {
      wifiStatusText = F("Buscando red…");
    }
  }
  String wifiModeLabel = wifiApMode ? F("Punto de acceso") : F("Cliente");
  String wifiSsidLabel = wifiApMode ? g_config.wifiApSsid : (wifi_sta_ssid_current.length() ? wifi_sta_ssid_current : g_config.wifiStaSsid);
  wifiSsidLabel.trim();
  if (wifiSsidLabel.length() == 0) {
    wifiSsidLabel = F("(no configurado)");
  }
  String wifiSsidStatus = htmlEscape(wifiSsidLabel);
  String wifiClientsStr = (wifiEnabled && wifiApMode) ? String(WiFi.softAPgetStationNum()) : String("-");
  String artnetActiveLabel = F("Sin enlace");
  if (artnetIp != IPAddress((uint32_t)0)) {
    if (artnetIp == ETH.localIP()) {
      artnetActiveLabel = F("Ethernet");
    } else {
      IPAddress staIp = WiFi.localIP();
      IPAddress apIp = WiFi.softAPIP();
      if (artnetIp == staIp || artnetIp == apIp || artnetIp == wifi_sta_ip || artnetIp == wifi_ap_ip) {
        artnetActiveLabel = F("Wi-Fi");
      } else {
        artnetActiveLabel = F("Desconocido");
      }
    }
  }

  html += F("<!DOCTYPE html><html lang='es'><head><meta charset='utf-8'>");
  html += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<title>PixelEtherLED - Configuración</title>");
  html += F("<style>body{font-family:Segoe UI,Helvetica,Arial,sans-serif;background:#0c0f1a;color:#f0f0f0;margin:0;padding:0;}\n");
  html += F("header{background:#121a2a;padding:1.5rem;text-align:center;}\n");
  html += F("h1{margin:0;font-size:1.8rem;}\nsection{padding:1.5rem;}\nform{max-width:720px;margin:0 auto;background:#141d30;padding:1.5rem;border-radius:12px;box-shadow:0 10px 30px rgba(0,0,0,0.45);}\n");
  html += F("label{display:block;margin-bottom:0.35rem;font-weight:600;}\ninput[type=number],input[type=text],input[type=password],select{width:100%;padding:0.65rem;border-radius:8px;border:1px solid #23314d;background:#0c1424;color:#f0f0f0;margin-bottom:1rem;}\n");
  html += F("button{width:100%;padding:0.85rem;background:#3478f6;color:#fff;border:none;border-radius:8px;font-size:1rem;font-weight:600;cursor:pointer;}\nbutton:hover{background:#255fcb;}\n");
  html += F("button.secondary{width:auto;display:inline-block;margin:0.25rem 0 0.75rem;background:#1f2b44;}\nbutton.secondary:hover{background:#263355;}\n");
  html += F(".card{max-width:720px;margin:1.5rem auto;background:#141d30;padding:1.5rem;border-radius:12px;box-shadow:0 10px 30px rgba(0,0,0,0.45);}\n");
  html += F(".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:1rem;}\nfooter{text-align:center;padding:1rem;color:#96a2c5;font-size:0.85rem;}\n");
  html += F(".message{margin-bottom:1rem;padding:0.75rem 1rem;border-radius:8px;background:#1f2b44;color:#a3ffb0;}\n");
  html += F(".section-title{margin-top:1.5rem;font-size:1.05rem;color:#9bb3ff;text-transform:uppercase;letter-spacing:0.05em;}\n");
  html += F(".wifi-group{margin-bottom:1rem;padding:1rem;background:#101829;border-radius:10px;border:1px solid #23314d;}\n");
  html += F(".wifi-scan{margin-top:0.5rem;font-size:0.9rem;color:#cfd8f7;}\n.wifi-scan div{padding:0.35rem 0;border-bottom:1px solid #23314d;}\n.wifi-scan div:last-child{border-bottom:none;}\n.wifi-scan strong{display:block;margin-bottom:0.1rem;}\n");
  html += F("input[type=file]{width:100%;margin-bottom:1rem;}\n</style></head><body>");
  html += F("<header><h1>PixelEtherLED</h1><p>Panel de configuración avanzada</p></header>");
  html += F("<section>");
  if (message.length()) {
    html += F("<div class='message'>");
    html += message;
    html += F("</div>");
  }
  html += F("<form method='post' action='/config'>");
  html += F("<h2 class='section-title'>Ethernet</h2>");
  html += F("<label for='dhcpTimeout'>Tiempo de espera DHCP (ms)</label>");
  html += "<input type='number' id='dhcpTimeout' name='dhcpTimeout' min='500' max='60000' value='" + String(g_config.dhcpTimeoutMs) + "'>";
  html += F("<label for='networkMode'>Modo de red</label>");
  html += F("<select id='networkMode' name='networkMode'>");
  html += String("<option value='dhcp'") + (usingDhcp ? " selected" : "") + ">DHCP (automático)</option>";
  html += String("<option value='static'") + (!usingDhcp ? " selected" : "") + ">IP fija</option>";
  html += F("</select>");
  html += F("<label for='fallbackToStatic'>Si DHCP falla</label>");
  html += F("<select id='fallbackToStatic' name='fallbackToStatic'>");
  html += String("<option value='1'") + (g_config.fallbackToStatic ? " selected" : "") + ">Aplicar IP fija configurada</option>";
  html += String("<option value='0'") + (!g_config.fallbackToStatic ? " selected" : "") + ">Mantener sin IP</option>";
  html += F("</select>");
  html += F("<label for='staticIp'>IP fija</label>");
  html += "<input type='text' id='staticIp' name='staticIp' value='" + staticIpStr + "'>";
  html += F("<label for='staticGateway'>Puerta de enlace</label>");
  html += "<input type='text' id='staticGateway' name='staticGateway' value='" + staticGwStr + "'>";
  html += F("<label for='staticMask'>Máscara de subred</label>");
  html += "<input type='text' id='staticMask' name='staticMask' value='" + staticMaskStr + "'>";
  html += F("<label for='staticDns1'>DNS primario</label>");
  html += "<input type='text' id='staticDns1' name='staticDns1' value='" + staticDns1Str + "'>";
  html += F("<label for='staticDns2'>DNS secundario</label>");
  html += "<input type='text' id='staticDns2' name='staticDns2' value='" + staticDns2Str + "'>";

  html += F("<h2 class='section-title'>Art-Net</h2>");
  html += F("<label for='artnetInput'>Interfaz preferida</label>");
  html += F("<select id='artnetInput' name='artnetInput'>");
  html += String("<option value='0'") + (artnetInputValue == 0 ? " selected" : "") + ">Ethernet</option>";
  html += String("<option value='1'") + (artnetInputValue == 1 ? " selected" : "") + ">Wi-Fi</option>";
  html += F("</select>");
  html += F("<p style='margin:-0.5rem 0 1rem;font-size:0.85rem;color:#96a2c5;'>Determiná desde qué interfaz se reciben los datos Art-Net. Si la opción seleccionada no tiene IP, se usará la otra disponible.</p>");

  html += F("<h2 class='section-title'>Wi-Fi</h2>");
  html += F("<label for='wifiEnabled'>Wi-Fi</label>");
  html += F("<select id='wifiEnabled' name='wifiEnabled'>");
  html += String("<option value='1'") + (wifiEnabled ? " selected" : "") + ">Habilitado</option>";
  html += String("<option value='0'") + (!wifiEnabled ? " selected" : "") + ">Deshabilitado</option>";
  html += F("</select>");
  html += F("<label for='wifiMode'>Modo Wi-Fi</label>");
  html += F("<select id='wifiMode' name='wifiMode'>");
  html += String("<option value='ap'") + (wifiApMode ? " selected" : "") + ">Punto de acceso</option>";
  html += String("<option value='sta'") + (!wifiApMode ? " selected" : "") + ">Cliente (unirse a red)</option>";
  html += F("</select>");
  html += F("<div id='wifiStaConfig' class='wifi-group'>");
  html += F("<label for='wifiStaSsid'>SSID</label>");
  html += "<input type='text' id='wifiStaSsid' name='wifiStaSsid' list='wifiNetworks' value='" + wifiStaSsidEsc + "'>";
  html += F("<input type='hidden' id='wifiStaSsidChanged' name='wifiStaSsidChanged' value='0'>");
  html += F("<datalist id='wifiNetworks'></datalist>");
  html += F("<label for='wifiStaPassword'>Contraseña</label>");
  html += "<input type='password' id='wifiStaPassword' name='wifiStaPassword' value='" + wifiStaPassEsc + "'>";
  html += F("<input type='hidden' id='wifiStaPasswordChanged' name='wifiStaPasswordChanged' value='0'>");
  html += F("<button type='button' class='secondary' id='wifiScanButton'>Escanear redes Wi-Fi</button>");
  html += F("<div id='wifiScanResults' class='wifi-scan'></div>");
  html += F("</div>");
  html += F("<div id='wifiApConfig' class='wifi-group'>");
  html += F("<label for='wifiApSsid'>SSID del punto de acceso</label>");
  html += "<input type='text' id='wifiApSsid' name='wifiApSsid' value='" + wifiApSsidEsc + "'>";
  html += F("<input type='hidden' id='wifiApSsidChanged' name='wifiApSsidChanged' value='0'>");
  html += F("<label for='wifiApPassword'>Contraseña (mínimo 8 caracteres, dejar vacío para abierto)</label>");
  html += "<input type='password' id='wifiApPassword' name='wifiApPassword' value='" + wifiApPassEsc + "'>";
  html += F("<input type='hidden' id='wifiApPasswordChanged' name='wifiApPasswordChanged' value='0'>");
  html += F("<p style='margin:0;font-size:0.85rem;color:#96a2c5;'>Los cambios Wi-Fi se aplican inmediatamente al guardar.</p>");
  html += F("</div>");

  html += F("<h2 class='section-title'>LEDs</h2>");
  html += F("<label for='numLeds'>Cantidad de LEDs activos</label>");
  html += "<input type='number' id='numLeds' name='numLeds' min='1' max='" + String(MAX_LEDS) + "' value='" + String(g_config.numLeds) + "'>";
  html += F("<label for='startUniverse'>Universo Art-Net inicial</label>");
  html += "<input type='number' id='startUniverse' name='startUniverse' min='0' max='32767' value='" + String(g_config.startUniverse) + "'>";
  html += F("<label for='pixelsPerUniverse'>Pixeles por universo</label>");
  html += "<input type='number' id='pixelsPerUniverse' name='pixelsPerUniverse' min='1' max='512' value='" + String(g_config.pixelsPerUniverse) + "'>";
  html += F("<label for='brightness'>Brillo máximo (0-255)</label>");
  html += "<input type='number' id='brightness' name='brightness' min='1' max='255' value='" + String(g_config.brightness) + "'>";
  html += F("<label for='chipType'>Tipo de chip LED</label>");
  html += F("<select id='chipType' name='chipType'>");
  for (uint8_t i = 0; i < static_cast<uint8_t>(LedChipType::CHIP_TYPE_COUNT); ++i) {
    html += "<option value='" + String(i) + "'";
    if (g_config.chipType == i) html += " selected";
    html += ">";
    html += CHIP_TYPE_NAMES[i];
    html += F("</option>");
  }
  html += F("</select>");
  html += F("<label for='colorOrder'>Orden de color</label>");
  html += F("<select id='colorOrder' name='colorOrder'>");
  for (uint8_t i = 0; i < static_cast<uint8_t>(LedColorOrder::COLOR_ORDER_COUNT); ++i) {
    html += "<option value='" + String(i) + "'";
    if (g_config.colorOrder == i) html += " selected";
    html += ">";
    html += COLOR_ORDER_NAMES[i];
    html += F("</option>");
  }
  html += F("</select>");
  html += F("<button type='submit'>Guardar configuración</button>");
  html += F("</form>");

  html += F("<div class='card'>");
  html += F("<h2>Estado del sistema</h2><div class='grid'>");
  html += "<div><strong>IP Ethernet:</strong><br>" + ETH.localIP().toString() + "</div>";
  html += "<div><strong>Link Ethernet:</strong><br>" + String(eth_link_up ? "activo" : "desconectado") + "</div>";
  html += "<div><strong>Modo de red:</strong><br>" + String(usingDhcp ? "DHCP" : "IP fija") + "</div>";
  html += "<div><strong>Fallback DHCP:</strong><br>" + fallbackLabel + "</div>";
  html += "<div><strong>Fuente Art-Net:</strong><br>" + artnetInputLabel + "</div>";
  html += "<div><strong>Interfaz activa Art-Net:</strong><br>" + artnetActiveLabel + "</div>";
  html += "<div><strong>IP Art-Net:</strong><br>" + artnetIpStr + "</div>";
  html += "<div><strong>IP fija configurada:</strong><br>" + staticIpStr + "</div>";
  html += "<div><strong>Gateway:</strong><br>" + staticGwStr + "</div>";
  html += "<div><strong>Máscara:</strong><br>" + staticMaskStr + "</div>";
  html += "<div><strong>DNS:</strong><br>" + staticDns1Str + " / " + staticDns2Str + "</div>";
  html += "<div><strong>Wi-Fi:</strong><br>" + wifiStatusText + "</div>";
  html += "<div><strong>Modo Wi-Fi:</strong><br>" + wifiModeLabel + "</div>";
  html += "<div><strong>SSID:</strong><br>" + wifiSsidStatus + "</div>";
  html += "<div><strong>IP Wi-Fi:</strong><br>" + wifiIpStr + "</div>";
  html += "<div><strong>Clientes Wi-Fi:</strong><br>" + wifiClientsStr + "</div>";
  html += "<div><strong>Universos:</strong><br>" + String(g_universeCount) + " (desde " + String(g_config.startUniverse) + ")";
  html += "</div><div><strong>Frames DMX:</strong><br>" + String((unsigned long)g_dmxFrames) + "</div>";
  html += "<div><strong>Brillo:</strong><br>" + String(g_config.brightness) + "/255";
  html += "</div><div><strong>DHCP timeout:</strong><br>" + String(g_config.dhcpTimeoutMs) + " ms";
  html += "</div><div><strong>Chip LED:</strong><br>" + String(getChipName(g_config.chipType)) + "</div>";
  html += "<div><strong>Orden:</strong><br>" + String(getColorOrderName(g_config.colorOrder)) + "</div>";
  html += F("</div></div>");

  html += F("<div class='card'>");
  html += F("<h2>Consejos</h2><ul><li>Si ampliás la tira LED, incrementá el parámetro <em>Cantidad de LEDs activos</em>.</li><li>Reducí el brillo máximo para ahorrar consumo o evitar saturación.</li><li>Ajustá el tiempo de espera de DHCP si tu red tarda más en asignar IP.</li><li>El valor de pixeles por universo determina cuántos LEDs se controlan por paquete Art-Net.</li><li>Mantené presionado el botón de reinicio durante 10 segundos al encender para restaurar la configuración de fábrica.</li></ul>");
  html += F("</div>");

  html += F("<div class='card'>");
  html += F("<h2>Actualizar firmware</h2>");
  html += F("<form method='post' action='/update' enctype='multipart/form-data'>");
  html += F("<label for='firmware'>Seleccioná el archivo de firmware (.bin)</label>");
  html += F("<input type='file' id='firmware' name='firmware' accept='.bin,application/octet-stream'>");
  html += F("<button type='submit'>Subir y aplicar firmware</button>");
  html += F("</form>");
  html += F("<p style='margin-top:0.75rem;font-size:0.9rem;color:#96a2c5;'>El dispositivo se reiniciará automáticamente luego de una actualización exitosa.</p>");
  html += F("</div>");

  html += F("</section><footer>PixelEtherLED &bull; Panel de control web</footer>");
  html += F("<script>const wifiEnabledEl=document.getElementById(\"wifiEnabled\");const wifiModeEl=document.getElementById(\"wifiMode\");const wifiStaEl=document.getElementById(\"wifiStaConfig\");const wifiApEl=document.getElementById(\"wifiApConfig\");const scanBtn=document.getElementById(\"wifiScanButton\");const wifiScanResults=document.getElementById(\"wifiScanResults\");const wifiNetworkList=document.getElementById(\"wifiNetworks\");const wifiStaSsidChanged=document.getElementById(\"wifiStaSsidChanged\");const wifiStaPassChanged=document.getElementById(\"wifiStaPasswordChanged\");const wifiApSsidChanged=document.getElementById(\"wifiApSsidChanged\");const wifiApPassChanged=document.getElementById(\"wifiApPasswordChanged\");function updateWifiVisibility(){const enabled=wifiEnabledEl.value===\"1\";const mode=wifiModeEl.value;wifiStaEl.style.display=(enabled&&mode===\"sta\")?\"block\":\"none\";wifiApEl.style.display=(enabled&&mode===\"ap\")?\"block\":\"none\";}updateWifiVisibility();wifiEnabledEl.addEventListener(\"change\",updateWifiVisibility);wifiModeEl.addEventListener(\"change\",updateWifiVisibility);function markChanged(inputEl, hiddenEl){if(!inputEl||!hiddenEl)return;const setChanged=function(){hiddenEl.value=\"1\";};inputEl.addEventListener(\"input\",setChanged);inputEl.addEventListener(\"change\",setChanged);}markChanged(document.getElementById(\"wifiStaSsid\"),wifiStaSsidChanged);markChanged(document.getElementById(\"wifiStaPassword\"),wifiStaPassChanged);markChanged(document.getElementById(\"wifiApSsid\"),wifiApSsidChanged);markChanged(document.getElementById(\"wifiApPassword\"),wifiApPassChanged);function scanWifi(){if(!wifiScanResults)return;wifiScanResults.textContent=\"Buscando redes...\";if(wifiNetworkList){while(wifiNetworkList.firstChild){wifiNetworkList.removeChild(wifiNetworkList.firstChild);}}fetch(\"/wifi_scan\").then(function(res){if(!res.ok){throw new Error(\"http\");}return res.json();}).then(function(data){if(!data||!Array.isArray(data.networks)||data.networks.length===0){wifiScanResults.textContent=\"No se encontraron redes.\";return;}wifiScanResults.textContent=\"\";data.networks.forEach(function(net){var container=document.createElement(\"div\");var title=document.createElement(\"strong\");title.textContent=(net.ssid&&net.ssid.length)?net.ssid:\"(sin SSID)\";container.appendChild(title);container.appendChild(document.createElement(\"br\"));var details=document.createElement(\"span\");details.textContent=\"Señal: \"+net.rssi+\" dBm · \"+net.secure+\" · Canal \"+net.channel;container.appendChild(details);wifiScanResults.appendChild(container);if(wifiNetworkList){var opt=document.createElement(\"option\");opt.value=net.ssid||\"\";wifiNetworkList.appendChild(opt);}});}).catch(function(){wifiScanResults.textContent=\"No se pudo completar el escaneo.\";});}if(scanBtn){scanBtn.addEventListener(\"click\",scanWifi);}</script></body></html>");
  return html;
}

String buildVisualizerPage()
{
  const uint16_t ledCount = std::min<uint16_t>(g_config.numLeds, MAX_LEDS);
  const uint16_t fallbackWidth = ledCount > 0 ? std::min<uint16_t>(ledCount, static_cast<uint16_t>(16)) : static_cast<uint16_t>(1);
  const uint16_t safeWidth = fallbackWidth == 0 ? static_cast<uint16_t>(1) : fallbackWidth;
  const uint16_t safeHeight = std::max<uint16_t>(static_cast<uint16_t>(1), static_cast<uint16_t>((ledCount + safeWidth - 1) / safeWidth));

  String html;
  html.reserve(12288);

  html += F("<!DOCTYPE html><html lang='es'><head><meta charset='utf-8'>");
  html += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<title>PixelEtherLED - Visualizador</title>");
  html += F("<style>body{font-family:Segoe UI,Helvetica,Arial,sans-serif;background:#0c0f1a;color:#f0f0f0;margin:0;padding:0;}\n");
  html += F("header{background:#121a2a;padding:1.5rem;text-align:center;}\n");
  html += F("h1{margin:0;font-size:1.8rem;}\nsection{padding:1.5rem;}\n");
  html += F(".card{max-width:960px;margin:0 auto;background:#141d30;padding:1.5rem;border-radius:12px;box-shadow:0 10px 30px rgba(0,0,0,0.45);}\n");
  html += F(".controls{display:flex;flex-wrap:wrap;gap:1rem;margin-bottom:1.5rem;}\n");
  html += F(".controls label{display:block;font-size:0.85rem;color:#9bb3ff;margin-bottom:0.35rem;}\n");
  html += F(".controls .field{flex:1 1 140px;}\n");
  html += F("input[type=number],select{width:100%;padding:0.6rem;border-radius:8px;border:1px solid #23314d;background:#0c1424;color:#f0f0f0;}\n");
  html += F(".field.checkbox{display:flex;align-items:flex-end;}\n");
  html += F(".field.checkbox label{margin-bottom:0;font-size:0.9rem;color:#f0f0f0;display:flex;align-items:center;gap:0.5rem;}\n");
  html += F(".grid{display:grid;gap:4px;grid-auto-rows:minmax(32px,1fr);}\n");
  html += F(".led-cell{position:relative;display:flex;align-items:center;justify-content:center;border-radius:6px;background:#1f2b44;color:#fff;font-size:0.9rem;font-weight:600;transition:transform 0.1s ease;}\n");
  html += F(".led-cell:hover{transform:scale(1.05);box-shadow:0 0 0 2px rgba(255,255,255,0.15);}\n");
  html += F(".led-index{z-index:1;}\n");
  html += F(".led-overlay{position:absolute;bottom:4px;right:6px;font-size:0.7rem;font-weight:500;background:rgba(0,0,0,0.45);padding:1px 4px;border-radius:4px;}\n");
  html += F(".info-panel{margin-top:1rem;padding:0.75rem 1rem;background:#101829;border-radius:8px;border:1px solid #23314d;color:#cfd8f7;font-size:0.95rem;}\n");
  html += F(".info-panel strong{color:#fff;}\n");
  html += F("a.link{color:#9bb3ff;text-decoration:none;}a.link:hover{text-decoration:underline;}\n");
  html += F("</style></head><body>");
  html += F("<header><h1>PixelEtherLED</h1><p>Visualizador en tiempo real</p></header>");
  html += F("<section>");
  html += F("<div class='card'>");
  html += F("<p style='margin-top:0;margin-bottom:1.5rem;font-size:0.95rem;color:#cfd8f7;'>Visualizá cómo llega cada píxel Art-Net al controlador y verificá el orden correcto sin salir del navegador.</p>");
  html += F("<div class='controls'>");
  html += F("<div class='field'><label for='matrixWidth'>Ancho</label>");
  html += "<input type='number' id='matrixWidth' min='1' value='" + String(safeWidth) + "'>";
  html += F("</div>");
  html += F("<div class='field'><label for='matrixHeight'>Alto</label>");
  html += "<input type='number' id='matrixHeight' min='1' value='" + String(safeHeight) + "'>";
  html += F("</div>");
  html += F("<div class='field'><label for='scanMode'>Recorrido</label>");
  html += F("<select id='scanMode'><option value='row'>Por filas</option><option value='column'>Por columnas</option></select>");
  html += F("</div>");
  html += F("<div class='field'><label for='startCorner'>Esquina inicial</label>");
  html += F("<select id='startCorner'><option value='tl'>Superior izquierda</option><option value='tr'>Superior derecha</option><option value='bl'>Inferior izquierda</option><option value='br'>Inferior derecha</option></select>");
  html += F("</div>");
  html += F("<div class='field checkbox'><label for='serpentine'><input type='checkbox' id='serpentine'> Serpenteado</label></div>");
  html += F("</div>");
  html += "<div id='ledSummary' style='margin-bottom:1rem;font-size:0.9rem;color:#96a2c5;'>LEDs activos: <strong>" + String(ledCount) + "</strong></div>";
  html += F("<div id='visualizerGrid' class='grid'></div>");
  html += F("<div class='info-panel'>");
  html += F("<div><strong>LED seleccionado:</strong> <span id='infoIndex'>-</span></div>");
  html += F("<div><strong>Color:</strong> <span id='infoColor'>-</span></div>");
  html += F("</div>");
  html += F("<p style='margin-top:1.5rem;font-size:0.9rem;'><a class='link' href='/config'>&larr; Volver al panel de configuración</a></p>");
  html += F("</div>");
  html += F("</section>");
  html += F("<script>");
  html += "const totalLeds=" + String(ledCount) + ";";
  html += F("const grid=document.getElementById('visualizerGrid');const widthInput=document.getElementById('matrixWidth');const heightInput=document.getElementById('matrixHeight');const serpInput=document.getElementById('serpentine');const scanInput=document.getElementById('scanMode');const cornerInput=document.getElementById('startCorner');const infoIndex=document.getElementById('infoIndex');const infoColor=document.getElementById('infoColor');const cellMap=new Map();let latestData=[];");
  html += F("if(totalLeds===0){[widthInput,heightInput,serpInput,scanInput,cornerInput].forEach(function(el){if(el){el.disabled=true;}});} ");
  html += F("function ensureDimensions(){let width=parseInt(widthInput.value,10);if(!Number.isFinite(width)||width<1){width=1;widthInput.value='1';}let height=parseInt(heightInput.value,10);if(!Number.isFinite(height)||height<1){height=1;heightInput.value='1';}if(totalLeds>0){const minHeight=Math.ceil(totalLeds/width);if(height<minHeight){height=minHeight;heightInput.value=String(height);}}return{width,height};}");
  html += F("function layoutCells(width,height,serp,mode,corner){const cells=[];let led=0;if(totalLeds===0){return cells;}if(mode==='row'){rows:for(let y=0;y<height;y++){let xs=Array.from({length:width},(_,i)=>i);if(serp&&y%2===1){xs.reverse();}for(const x of xs){if(led>=totalLeds){break rows;}cells.push({ledIndex:led,x:x,y:y});led++;}}}else{cols:for(let x=0;x<width;x++){let ys=Array.from({length:height},(_,i)=>i);if(serp&&x%2===1){ys.reverse();}for(const y of ys){if(led>=totalLeds){break cols;}cells.push({ledIndex:led,x:x,y:y});led++;}}}return cells.map(function(cell){let px=cell.x;let py=cell.y;if(corner==='tr'||corner==='br'){px=width-1-px;}if(corner==='bl'||corner==='br'){py=height-1-py;}return{ledIndex:cell.ledIndex,x:px,y:py};});}");
  html += F("function rebuildGrid(){cellMap.clear();grid.innerHTML='';const dims=ensureDimensions();const width=dims.width;const height=dims.height;grid.style.gridTemplateColumns='repeat('+width+', minmax(32px,1fr))';if(totalLeds===0){const msg=document.createElement('p');msg.textContent='No hay LEDs configurados en este dispositivo.';msg.style.color='#cfd8f7';msg.style.fontSize='0.95rem';grid.appendChild(msg);return;}const cells=layoutCells(width,height,serpInput.checked,scanInput.value,cornerInput.value);cells.forEach(function(cell){const el=document.createElement('div');el.className='led-cell';el.style.gridColumn=String(cell.x+1);el.style.gridRow=String(cell.y+1);const idx=document.createElement('div');idx.className='led-index';idx.textContent=cell.ledIndex;el.appendChild(idx);const overlay=document.createElement('div');overlay.className='led-overlay';overlay.textContent='RGB';el.appendChild(overlay);el.dataset.index=cell.ledIndex;el.title='LED '+cell.ledIndex;el.addEventListener('mouseenter',function(){const rgb=el.dataset.rgb||'-, -, -';infoIndex.textContent=cell.ledIndex;infoColor.textContent=rgb;});cellMap.set(cell.ledIndex,el);grid.appendChild(el);});applyColors();}");
  html += F("function applyColors(){if(!Array.isArray(latestData)){return;}latestData.forEach(function(entry){const cell=cellMap.get(entry.index);if(!cell){return;}const color='rgb('+entry.r+','+entry.g+','+entry.b+')';cell.style.backgroundColor=color;const overlay=cell.querySelector('.led-overlay');if(overlay){overlay.textContent=entry.r+','+entry.g+','+entry.b;}cell.dataset.rgb=entry.r+', '+entry.g+', '+entry.b;cell.title='LED '+entry.index+'\nR: '+entry.r+' G: '+entry.g+' B: '+entry.b;const brightness=0.2126*entry.r+0.7152*entry.g+0.0722*entry.b;cell.style.color=brightness>140?'#000':'#fff';if(overlay){overlay.style.backgroundColor=brightness>140?'rgba(0,0,0,0.25)':'rgba(0,0,0,0.55)';}});}");
  html += F("function poll(){fetch('/api/led_state',{cache:'no-store'}).then(function(res){if(!res.ok){throw new Error('http');}return res.json();}).then(function(data){if(data&&Array.isArray(data.leds)){latestData=data.leds;applyColors();}}).catch(function(err){console.debug('visualizador: error',err);});}");
  html += F("widthInput.addEventListener('change',rebuildGrid);heightInput.addEventListener('change',rebuildGrid);serpInput.addEventListener('change',rebuildGrid);scanInput.addEventListener('change',rebuildGrid);cornerInput.addEventListener('change',rebuildGrid);rebuildGrid();poll();setInterval(poll,250);");
  html += F("</script></body></html>");
  return html;
}

void handleVisualizerGet()
{
  g_server.send(200, "text/html", buildVisualizerPage());
}

void handleConfigGet()
{
  g_server.send(200, "text/html", buildConfigPage());
}

void handleConfigPost()
{
  AppConfig newConfig = g_config;

  if (g_server.hasArg("dhcpTimeout")) {
    long parsed = g_server.arg("dhcpTimeout").toInt();
    parsed = std::max(500L, parsed);
    parsed = std::min(60000L, parsed);
    newConfig.dhcpTimeoutMs = static_cast<uint32_t>(parsed);
  }
  if (g_server.hasArg("networkMode")) {
    String mode = g_server.arg("networkMode");
    mode.trim();
    mode.toLowerCase();
    newConfig.useDhcp = (mode != "static");
  }
  if (g_server.hasArg("fallbackToStatic")) {
    newConfig.fallbackToStatic = g_server.arg("fallbackToStatic") == "1";
  }
  if (g_server.hasArg("artnetInput")) {
    long parsed = g_server.arg("artnetInput").toInt();
    if (parsed < 0) parsed = DEFAULT_ARTNET_INPUT;
    newConfig.artnetInput = static_cast<uint8_t>(parsed);
  }
  if (g_server.hasArg("wifiEnabled")) {
    newConfig.wifiEnabled = g_server.arg("wifiEnabled") == "1";
  }
  if (g_server.hasArg("wifiMode")) {
    String mode = g_server.arg("wifiMode");
    mode.trim();
    mode.toLowerCase();
    newConfig.wifiApMode = (mode != "sta");
  }
  bool wifiStaSsidChanged = g_server.hasArg("wifiStaSsidChanged") && g_server.arg("wifiStaSsidChanged") == "1";
  if (g_server.hasArg("wifiStaSsid")) {
    String value = g_server.arg("wifiStaSsid");
    if (wifiStaSsidChanged || value.length()) {
      newConfig.wifiStaSsid = value;
    }
  }
  bool wifiStaPasswordChanged = g_server.hasArg("wifiStaPasswordChanged") && g_server.arg("wifiStaPasswordChanged") == "1";
  if (wifiStaPasswordChanged && g_server.hasArg("wifiStaPassword")) {
    newConfig.wifiStaPassword = g_server.arg("wifiStaPassword");
  }
  bool wifiApSsidChanged = g_server.hasArg("wifiApSsidChanged") && g_server.arg("wifiApSsidChanged") == "1";
  if (g_server.hasArg("wifiApSsid")) {
    String value = g_server.arg("wifiApSsid");
    if (wifiApSsidChanged || value.length()) {
      newConfig.wifiApSsid = value;
    }
  }
  bool wifiApPasswordChanged = g_server.hasArg("wifiApPasswordChanged") && g_server.arg("wifiApPasswordChanged") == "1";
  if (wifiApPasswordChanged && g_server.hasArg("wifiApPassword")) {
    newConfig.wifiApPassword = g_server.arg("wifiApPassword");
  }
  if (g_server.hasArg("staticIp")) {
    String value = g_server.arg("staticIp");
    value.trim();
    newConfig.staticIp = parseIp(value, newConfig.staticIp);
  }
  if (g_server.hasArg("staticGateway")) {
    String value = g_server.arg("staticGateway");
    value.trim();
    newConfig.staticGateway = parseIp(value, newConfig.staticGateway);
  }
  if (g_server.hasArg("staticMask")) {
    String value = g_server.arg("staticMask");
    value.trim();
    newConfig.staticSubnet = parseIp(value, newConfig.staticSubnet);
  }
  if (g_server.hasArg("staticDns1")) {
    String value = g_server.arg("staticDns1");
    value.trim();
    newConfig.staticDns1 = parseIp(value, newConfig.staticDns1);
  }
  if (g_server.hasArg("staticDns2")) {
    String value = g_server.arg("staticDns2");
    value.trim();
    newConfig.staticDns2 = parseIp(value, newConfig.staticDns2);
  }
  if (g_server.hasArg("numLeds")) {
    long parsed = g_server.arg("numLeds").toInt();
    parsed = std::max(1L, std::min<long>(parsed, static_cast<long>(MAX_LEDS)));
    newConfig.numLeds = static_cast<uint16_t>(parsed);
  }
  if (g_server.hasArg("startUniverse")) {
    long su = g_server.arg("startUniverse").toInt();
    if (su < 0) su = 0;
    newConfig.startUniverse = static_cast<uint16_t>(su);
  }
  if (g_server.hasArg("pixelsPerUniverse")) {
    long parsed = g_server.arg("pixelsPerUniverse").toInt();
    parsed = std::max(1L, std::min<long>(parsed, static_cast<long>(MAX_LEDS)));
    newConfig.pixelsPerUniverse = static_cast<uint16_t>(parsed);
  }
  if (g_server.hasArg("brightness")) {
    long parsed = g_server.arg("brightness").toInt();
    parsed = std::max(1L, std::min<long>(parsed, 255));
    newConfig.brightness = static_cast<uint8_t>(parsed);
  }
  if (g_server.hasArg("chipType")) {
    long parsed = g_server.arg("chipType").toInt();
    if (parsed < 0) parsed = DEFAULT_CHIP_TYPE;
    newConfig.chipType = static_cast<uint8_t>(parsed);
  }
  if (g_server.hasArg("colorOrder")) {
    long parsed = g_server.arg("colorOrder").toInt();
    if (parsed < 0) parsed = DEFAULT_COLOR_ORDER;
    newConfig.colorOrder = static_cast<uint8_t>(parsed);
  }

  normalizeConfig(newConfig);

  bool requiresRestart = (newConfig.chipType != g_config.chipType) ||
                         (newConfig.colorOrder != g_config.colorOrder);

  g_config = newConfig;
  applyConfig();
  saveConfig();
  // Wi-Fi bring-up switches the default LwIP interface to the wireless stack.
  // Re-initialise Ethernet afterwards so Art-Net binds to the wired interface.
  bringUpWiFi(g_config);
  bringUpEthernet(g_config);
  artnet.updateNetworkInfo();

  if (requiresRestart) {
    g_server.send(200, "text/html", buildConfigPage("Configuración actualizada. Reiniciando para aplicar tipo de chip/orden de color."));
    delay(500);
    ESP.restart();
  } else {
    g_server.send(200, "text/html", buildConfigPage("Configuración actualizada correctamente."));
  }
}

void handleRoot()
{
  g_server.sendHeader("Location", "/config", true);
  g_server.send(302, "text/plain", "Redireccionando a /config");
}

void handleFirmwareUpload()
{
  HTTPUpload& upload = g_server.upload();

  switch (upload.status) {
    case UPLOAD_FILE_START:
      g_firmwareUploadHandled = true;
      g_firmwareUpdateShouldRestart = false;
      g_firmwareUpdateMessage = F("Iniciando actualización de firmware...");
      Serial.printf("[FW] Iniciando carga: %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Update.printError(Serial);
        g_firmwareUpdateMessage = F("No se pudo iniciar la actualización de firmware.");
      }
      break;
    case UPLOAD_FILE_WRITE:
      if (Update.isRunning()) {
        size_t written = Update.write(upload.buf, upload.currentSize);
        if (written != upload.currentSize) {
          Update.printError(Serial);
          g_firmwareUpdateMessage = F("Error al escribir el firmware recibido.");
        }
      }
      break;
    case UPLOAD_FILE_END:
      if (Update.isRunning()) {
        if (Update.end(true)) {
          g_firmwareUpdateMessage = F("Firmware actualizado correctamente. Reiniciando...");
          g_firmwareUpdateShouldRestart = true;
          Serial.printf("[FW] Actualización completada (%u bytes).\n", upload.totalSize);
        } else {
          Update.printError(Serial);
          g_firmwareUpdateMessage = F("La actualización de firmware falló al finalizar.");
        }
      }
      break;
    case UPLOAD_FILE_ABORTED:
      Update.abort();
      g_firmwareUpdateMessage = F("La carga de firmware fue cancelada.");
      Serial.println("[FW] Actualización abortada por el cliente.");
      break;
    default:
      break;
  }
}

void handleFirmwareUpdatePost()
{
  String message;
  if (!g_firmwareUploadHandled) {
    message = F("No se recibió ningún archivo de firmware.");
  } else if (g_firmwareUpdateMessage.length()) {
    message = g_firmwareUpdateMessage;
  } else {
    message = F("Proceso de actualización finalizado.");
  }

  bool shouldRestart = g_firmwareUpdateShouldRestart;

  g_server.send(200, "text/html", buildConfigPage(message));

  g_firmwareUploadHandled = false;
  g_firmwareUpdateShouldRestart = false;
  g_firmwareUpdateMessage.clear();

  if (shouldRestart) {
    delay(500);
    ESP.restart();
  }
}

void bringUpWiFi(const AppConfig& config)
{
  WiFi.scanDelete();
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  delay(50);

  wifi_sta_running = false;
  wifi_sta_connected = false;
  wifi_sta_has_ip = false;
  wifi_ap_running = false;
  wifi_sta_ip = IPAddress((uint32_t)0);
  wifi_ap_ip = IPAddress((uint32_t)0);
  wifi_sta_ssid_current.clear();

  if (!config.wifiEnabled) {
    Serial.println("[WIFI] Deshabilitado.");
    return;
  }

  if (config.wifiApMode) {
    Serial.print("[WIFI] Activando punto de acceso: ");
    Serial.println(config.wifiApSsid);
    WiFi.mode(WIFI_AP);
    bool ok = false;
    if (config.wifiApPassword.length() >= 8) {
      ok = WiFi.softAP(config.wifiApSsid.c_str(), config.wifiApPassword.c_str());
    } else {
      ok = WiFi.softAP(config.wifiApSsid.c_str());
    }
    if (ok) {
      WiFi.softAPsetHostname(DEVICE_HOSTNAME);
      wifi_ap_running = true;
      wifi_ap_ip = WiFi.softAPIP();
      Serial.print("[WIFI] AP IP: ");
      Serial.println(wifi_ap_ip);
    } else {
      Serial.println("[WIFI] softAP() falló");
    }
  } else {
    if (config.wifiStaSsid.length() == 0) {
      Serial.println("[WIFI] SSID no configurado; no se intentará conectar.");
      return;
    }

    Serial.print("[WIFI] Conectando a SSID: ");
    Serial.println(config.wifiStaSsid);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(DEVICE_HOSTNAME);
    WiFi.setAutoReconnect(true);
    WiFi.begin(config.wifiStaSsid.c_str(), config.wifiStaPassword.c_str());

    const uint32_t t0 = millis();
    while (millis() - t0 < config.dhcpTimeoutMs) {
      if (WiFi.status() == WL_CONNECTED) {
        break;
      }
      if (wifi_sta_has_ip) {
        break;
      }
      Serial.print('.');
      delay(250);
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      wifi_sta_connected = true;
      wifi_sta_has_ip = true;
      wifi_sta_ip = WiFi.localIP();
      wifi_sta_ssid_current = WiFi.SSID();
      Serial.print("[WIFI] IP obtenida: ");
      Serial.println(wifi_sta_ip);
    } else if (wifi_sta_has_ip) {
      Serial.print("[WIFI] IP actual: ");
      Serial.println(wifi_sta_ip);
    } else {
      Serial.println("[WIFI] No se obtuvo conexión/IP en el tiempo configurado.");
    }
  }
}

void bringUpEthernet(const AppConfig& config)
{
  eth_link_up = false;
  eth_has_ip  = false;
  pinMode(ETH_POWER_PIN, OUTPUT);
  digitalWrite(ETH_POWER_PIN, HIGH);
  delay(10);

  if (!ETH.begin(ETH_PHY_ADDR, ETH_POWER_PIN, ETH_MDC_PIN, ETH_MDIO_PIN, ETH_PHY_TYPE, ETH_CLK_MODE)) {
    Serial.println("[ETH] begin() FALLÓ");
  }

  IPAddress staticIp(config.staticIp);
  IPAddress staticGw(config.staticGateway);
  IPAddress staticMask(config.staticSubnet);
  IPAddress staticDns1(config.staticDns1);
  IPAddress staticDns2(config.staticDns2);

  if (!config.useDhcp) {
    bool ok = ETH.config(staticIp, staticGw, staticMask, staticDns1, staticDns2);
    if (ok) {
      eth_has_ip = (ETH.localIP() != IPAddress((uint32_t)0));
      Serial.print("[ETH] IP fija configurada: ");
      Serial.println(ETH.localIP());
    } else {
      Serial.println("[ETH] ETH.config() FALLÓ (no se pudo asignar la IP fija)");
    }
  }

  Serial.print(config.useDhcp ? "[ETH] Esperando link + DHCP" : "[ETH] Esperando link");
  const uint32_t t0 = millis();
  while (millis() - t0 < config.dhcpTimeoutMs) {
    Serial.print(".");
    delay(250);
    if (config.useDhcp) {
      if (eth_link_up && eth_has_ip) break;
    } else if (eth_link_up) {
      break;
    }
  }
  Serial.println();

  if (config.useDhcp && !eth_has_ip) {
    Serial.println("[ETH] DHCP no respondió.");
    if (config.fallbackToStatic) {
      Serial.println("[ETH] Aplicando configuración IP fija de respaldo…");
      bool ok = ETH.config(staticIp, staticGw, staticMask, staticDns1, staticDns2);
      if (ok) {
        Serial.print("[ETH] IP fija configurada: ");
        Serial.println(ETH.localIP());
        eth_has_ip = (ETH.localIP() != IPAddress((uint32_t)0));
      } else {
        Serial.println("[ETH] ETH.config() FALLÓ (IP fija no aplicada)");
      }
    }
  }

  if (!eth_has_ip) {
    Serial.println("[ETH] Advertencia: sin IP (no habrá Art-Net hasta que haya red).");
  }
}

void handleLedStateJson()
{
  const uint16_t ledCount = std::min<uint16_t>(g_config.numLeds, MAX_LEDS);
  String json;
  json.reserve(static_cast<size_t>(ledCount) * 30 + 32);
  json += F("{\"leds\":[");
  for (uint16_t i = 0; i < ledCount; ++i) {
    if (i > 0) json += ',';
    const CRGB& color = leds[i];
    json += F("{\"index\":");
    json += String(i);
    json += F(",\"r\":");
    json += String(static_cast<uint8_t>(color.r));
    json += F(",\"g\":");
    json += String(static_cast<uint8_t>(color.g));
    json += F(",\"b\":");
    json += String(static_cast<uint8_t>(color.b));
    json += F("}");
  }
  json += F("]}");

  g_server.sendHeader("Cache-Control", "no-store");
  g_server.send(200, "application/json", json);
}

void handleWifiScan()
{
  int16_t n = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/true);
  if (n < 0) {
    g_server.send(500, "application/json", F("{\"error\":\"scan_failed\"}"));
    return;
  }

  String json = F("{\"networks\":[");
  for (int16_t i = 0; i < n; ++i) {
    if (i > 0) json += ',';
    String ssid = WiFi.SSID(i);
    int32_t rssi = WiFi.RSSI(i);
    wifi_auth_mode_t mode = WiFi.encryptionType(i);
    int32_t channel = WiFi.channel(i);
    json += F("{\"ssid\":\"");
    json += jsonEscape(ssid);
    json += F("\",\"rssi\":");
    json += String(rssi);
    json += F(",\"secure\":\"");
    json += wifiAuthModeToText(mode);
    json += F("\",\"channel\":");
    json += String(channel);
    json += F("}");
  }
  json += F("]}");

  g_server.sendHeader("Cache-Control", "no-store");
  g_server.send(200, "application/json", json);
  WiFi.scanDelete();
}

void setup()
{
  Serial.begin(115200);
  delay(200);

  if (checkFactoryResetOnBoot()) {
    restoreFactoryDefaults();
  }

  loadConfig();

  initializeFastLEDController();
  FastLED.clear(true);
  FastLED.setDither(0);
  FastLED.setBrightness(g_config.brightness);

  applyConfig();

  WiFi.onEvent(onWiFiEvent);
  WiFi.persistent(false);
  // Wi-Fi bring-up switches the default LwIP interface to the wireless stack.
  // Re-initialise Ethernet afterwards so Art-Net binds to the wired interface.
  bringUpWiFi(g_config);
  bringUpEthernet(g_config);
  artnet.updateNetworkInfo();

  artnet.setNodeNames("PixelEtherLED", "PixelEtherLED Controller");
  artnet.begin();                      // responde a ArtPoll → Jinx "Scan"
  artnet.setArtDmxCallback(onDmxFrame);

  g_server.on("/", HTTP_GET, handleRoot);
  g_server.on("/config", HTTP_GET, handleConfigGet);
  g_server.on("/config", HTTP_POST, handleConfigPost);
  g_server.on("/visualizer", HTTP_GET, handleVisualizerGet);
  g_server.on("/api/led_state", HTTP_GET, handleLedStateJson);
  g_server.on("/update", HTTP_GET, handleRoot);
  g_server.on("/update", HTTP_POST, handleFirmwareUpdatePost, handleFirmwareUpload);
  g_server.on("/wifi_scan", HTTP_GET, handleWifiScan);
  g_server.begin();

  Serial.println("[ARTNET] Listo");
  Serial.printf("  Universos: %u (desde %u)\n", g_universeCount, g_config.startUniverse);
  Serial.printf("  LEDs: %u, pix/universo: %u\n", g_config.numLeds, g_config.pixelsPerUniverse);
  Serial.print("  IP actual: "); Serial.println(ETH.localIP());
  IPAddress wifiIp = WiFi.localIP();
  if (wifiIp == IPAddress((uint32_t)0)) {
    wifiIp = WiFi.softAPIP();
  }
  Serial.print("  Wi-Fi IP: "); Serial.println(wifiIp);
}

void loop()
{
  artnet.read();
  g_server.handleClient();
}
