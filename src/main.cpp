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

#include "AppConfig.h"
#include "WebUi.h"

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

constexpr uint32_t DEFAULT_DHCP_TIMEOUT = 3000;             // ms

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

void restoreFactoryDefaults();
bool checkFactoryResetOnBoot();
void bringUpEthernet(const AppConfig& config);
void bringUpWiFi(const AppConfig& config);
void handleWifiScan();
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
String buildConfigPage(const String& message)
{
  WebUiRuntime runtime{};
  runtime.ethLinkUp = eth_link_up;
  runtime.ethHasIp = eth_has_ip;
  runtime.ethLocalIp = ETH.localIP();
  runtime.wifiStaConnected = wifi_sta_connected;
  runtime.wifiStaHasIp = wifi_sta_has_ip;
  runtime.wifiStaIp = wifi_sta_ip;
  runtime.wifiApIp = wifi_ap_ip;
  runtime.wifiApRunning = wifi_ap_running;
  runtime.wifiStaSsidCurrent = wifi_sta_ssid_current;
  runtime.wifiClientCount = (g_config.wifiEnabled && g_config.wifiApMode) ? static_cast<uint8_t>(WiFi.softAPgetStationNum()) : 0;
  runtime.wifiLocalIp = WiFi.localIP();
  runtime.wifiSoftApIp = WiFi.softAPIP();
  runtime.artnetIp = artnet.localIp();
  runtime.universeCount = g_universeCount;
  runtime.dmxFrames = g_dmxFrames;

  return renderConfigPage(g_config, runtime, message);
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
  if (g_server.hasArg("wifiStaSsid")) {
    newConfig.wifiStaSsid = g_server.arg("wifiStaSsid");
  }
  if (g_server.hasArg("wifiStaPassword")) {
    newConfig.wifiStaPassword = g_server.arg("wifiStaPassword");
  }
  if (g_server.hasArg("wifiApSsid")) {
    newConfig.wifiApSsid = g_server.arg("wifiApSsid");
  }
  if (g_server.hasArg("wifiApPassword")) {
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
