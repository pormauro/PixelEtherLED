#include <Arduino.h>
#include <WiFi.h>
#include <ETH.h>
#include <WiFiUdp.h>
#include <Artnet.h>
#include <FastLED.h>
#include <Preferences.h>
#include <WebServer.h>
#include <algorithm>
#include <vector>

// ===================== CONFIG RED (IP FIJA - FALLBACK) =====================
static const IPAddress STATIC_IP   (192, 168, 1, 50);
static const IPAddress STATIC_GW   (192, 168, 1, 1);
static const IPAddress STATIC_MASK (255, 255, 255, 0);
static const IPAddress STATIC_DNS1 (1, 1, 1, 1);
static const IPAddress STATIC_DNS2 (8, 8, 8, 8);

// ===================== LEDS =====================
#define DATA_PIN      2
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
};

AppConfig g_config = {
  DEFAULT_DHCP_TIMEOUT,
  DEFAULT_NUM_LEDS,
  DEFAULT_START_UNIVERSE,
  DEFAULT_PIXELS_PER_UNIVERSE,
  DEFAULT_BRIGHTNESS,
  DEFAULT_CHIP_TYPE,
  DEFAULT_COLOR_ORDER
};

Preferences g_prefs;
WebServer g_server(80);

// ===================== ART-NET =====================
Artnet artnet;
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
      ETH.setHostname("esp32-artnet");
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
}

template <typename CHIPSET>
void addControllerForOrder(LedColorOrder order)
{
  switch (order) {
    case LedColorOrder::RGB: FastLED.addLeds<CHIPSET, DATA_PIN, RGB>(leds, MAX_LEDS); break;
    case LedColorOrder::RBG: FastLED.addLeds<CHIPSET, DATA_PIN, RBG>(leds, MAX_LEDS); break;
    case LedColorOrder::GRB: FastLED.addLeds<CHIPSET, DATA_PIN, GRB>(leds, MAX_LEDS); break;
    case LedColorOrder::GBR: FastLED.addLeds<CHIPSET, DATA_PIN, GBR>(leds, MAX_LEDS); break;
    case LedColorOrder::BRG: FastLED.addLeds<CHIPSET, DATA_PIN, BRG>(leds, MAX_LEDS); break;
    case LedColorOrder::BGR: FastLED.addLeds<CHIPSET, DATA_PIN, BGR>(leds, MAX_LEDS); break;
    default: FastLED.addLeds<CHIPSET, DATA_PIN, BRG>(leds, MAX_LEDS); break;
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
      addControllerForOrder<WS2811>(order);
      break;
    case LedChipType::WS2812B:
      addControllerForOrder<WS2812B>(order);
      break;
    case LedChipType::SK6812:
      addControllerForOrder<SK6812>(order);
      break;
    default:
      addControllerForOrder<WS2811>(order);
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
  g_config = {
    DEFAULT_DHCP_TIMEOUT,
    DEFAULT_NUM_LEDS,
    DEFAULT_START_UNIVERSE,
    DEFAULT_PIXELS_PER_UNIVERSE,
    DEFAULT_BRIGHTNESS,
    DEFAULT_CHIP_TYPE,
    DEFAULT_COLOR_ORDER
  };

  if (g_prefs.begin("pixelcfg", true)) {
    g_config.dhcpTimeoutMs   = g_prefs.getUInt("dhcpTimeout", g_config.dhcpTimeoutMs);
    g_config.numLeds         = g_prefs.getUShort("numLeds", g_config.numLeds);
    g_config.startUniverse   = g_prefs.getUShort("startUni", g_config.startUniverse);
    g_config.pixelsPerUniverse = g_prefs.getUShort("pixPerUni", g_config.pixelsPerUniverse);
    g_config.brightness      = g_prefs.getUChar("brightness", g_config.brightness);
    g_config.chipType        = g_prefs.getUChar("chipType", g_config.chipType);
    g_config.colorOrder      = g_prefs.getUChar("colorOrder", g_config.colorOrder);
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
    g_prefs.end();
  }
}

void applyConfig()
{
  normalizeConfig(g_config);
  g_universeCount = (g_config.numLeds + g_config.pixelsPerUniverse - 1) / g_config.pixelsPerUniverse;
  g_universeCount = std::max<uint16_t>(1, g_universeCount);
  g_universeReceived.assign(g_universeCount, 0);

  FastLED.setBrightness(g_config.brightness);
  FastLED.clear(true);
}

String buildConfigPage(const String& message)
{
  String html;
  html.reserve(4096);
  html += F("<!DOCTYPE html><html lang='es'><head><meta charset='utf-8'>");
  html += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<title>PixelEtherLED - Configuración</title>");
  html += F("<style>body{font-family:Segoe UI,Helvetica,Arial,sans-serif;background:#0c0f1a;color:#f0f0f0;margin:0;padding:0;}\n");
  html += F("header{background:#121a2a;padding:1.5rem;text-align:center;}\n");
  html += F("h1{margin:0;font-size:1.8rem;}\nsection{padding:1.5rem;}\nform{max-width:640px;margin:0 auto;background:#141d30;padding:1.5rem;border-radius:12px;box-shadow:0 10px 30px rgba(0,0,0,0.45);}\n");
  html += F("label{display:block;margin-bottom:0.35rem;font-weight:600;}\ninput[type=number]{width:100%;padding:0.65rem;border-radius:8px;border:1px solid #23314d;background:#0c1424;color:#f0f0f0;margin-bottom:1rem;}\n");
  html += F("button{width:100%;padding:0.85rem;background:#3478f6;color:#fff;border:none;border-radius:8px;font-size:1rem;font-weight:600;cursor:pointer;}\nbutton:hover{background:#255fcb;}\n");
  html += F(".card{max-width:640px;margin:1.5rem auto;background:#141d30;padding:1.5rem;border-radius:12px;box-shadow:0 10px 30px rgba(0,0,0,0.45);}\n");
  html += F(".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:1rem;}\nfooter{text-align:center;padding:1rem;color:#96a2c5;font-size:0.85rem;}\n");
  html += F(".message{margin-bottom:1rem;padding:0.75rem 1rem;border-radius:8px;background:#1f2b44;color:#a3ffb0;}\n</style></head><body>");
  html += F("<header><h1>PixelEtherLED</h1><p>Panel de configuración avanzada</p></header>");
  html += F("<section>");
  if (message.length()) {
    html += F("<div class='message'>");
    html += message;
    html += F("</div>");
  }
  html += F("<form method='post' action='/config'>");
  html += F("<label for='dhcpTimeout'>Tiempo de espera DHCP (ms)</label>");
  html += "<input type='number' id='dhcpTimeout' name='dhcpTimeout' min='500' max='60000' value='" + String(g_config.dhcpTimeoutMs) + "'>";
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
  html += "<div><strong>IP actual:</strong><br>" + ETH.localIP().toString() + "</div>";
  html += "<div><strong>Link Ethernet:</strong><br>" + String(eth_link_up ? "activo" : "desconectado") + "</div>";
  html += "<div><strong>Universos:</strong><br>" + String(g_universeCount) + " (desde " + String(g_config.startUniverse) + ")";
  html += "</div><div><strong>Frames DMX:</strong><br>" + String((unsigned long)g_dmxFrames) + "</div>";
  html += "<div><strong>Brillo:</strong><br>" + String(g_config.brightness) + "/255";
  html += "</div><div><strong>DHCP timeout:</strong><br>" + String(g_config.dhcpTimeoutMs) + " ms";
  html += "</div><div><strong>Chip LED:</strong><br>" + String(getChipName(g_config.chipType)) + "</div>";
  html += "<div><strong>Orden:</strong><br>" + String(getColorOrderName(g_config.colorOrder)) + "</div>";
  html += F("</div></div>");
  html += F("<div class='card'>");
  html += F("<h2>Consejos</h2><ul><li>Si ampliás la tira LED, incrementá el parámetro <em>Cantidad de LEDs activos</em>.</li><li>Reducí el brillo máximo para ahorrar consumo o evitar saturación.</li><li>Ajustá el tiempo de espera de DHCP si tu red tarda más en asignar IP.</li><li>El valor de pixeles por universo determina cuántos LEDs se controlan por paquete Art-Net.</li></ul>");
  html += F("</div>");
  html += F("</section><footer>PixelEtherLED &bull; Panel de control web</footer></body></html>");
  return html;
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

void bringUpEthernetWithDhcpFallback(unsigned long dhcpTimeoutMs = DEFAULT_DHCP_TIMEOUT)
{
  pinMode(ETH_POWER_PIN, OUTPUT);
  digitalWrite(ETH_POWER_PIN, HIGH);
  delay(10);

  if (!ETH.begin(ETH_PHY_ADDR, ETH_POWER_PIN, ETH_MDC_PIN, ETH_MDIO_PIN, ETH_PHY_TYPE, ETH_CLK_MODE)) {
    Serial.println("[ETH] begin() FALLÓ");
  }

  Serial.print("[ETH] Esperando link + DHCP");
  const uint32_t t0 = millis();
  while (millis() - t0 < dhcpTimeoutMs) {
    Serial.print(".");
    delay(250);
    if (eth_link_up && eth_has_ip) break;
  }
  Serial.println();

  if (!eth_has_ip) {
    Serial.println("[ETH] DHCP no respondió. Configurando IP fija…");
    bool ok = ETH.config(STATIC_IP, STATIC_GW, STATIC_MASK, STATIC_DNS1, STATIC_DNS2);
    if (ok) {
      Serial.print("[ETH] IP fija configurada: ");
      Serial.println(ETH.localIP());
      eth_has_ip = (ETH.localIP() != IPAddress((uint32_t)0));
    } else {
      Serial.println("[ETH] ETH.config() FALLÓ (IP fija no aplicada)");
    }
  }

  if (!eth_has_ip) {
    Serial.println("[ETH] Advertencia: sin IP (no habrá Art-Net hasta que haya red).");
  }
}

void setup()
{
  Serial.begin(115200);
  delay(200);

  loadConfig();

  initializeFastLEDController();
  FastLED.clear(true);
  FastLED.setDither(0);
  FastLED.setBrightness(g_config.brightness);

  applyConfig();

  WiFi.onEvent(onWiFiEvent);
  bringUpEthernetWithDhcpFallback(g_config.dhcpTimeoutMs);

  artnet.begin();                      // responde a ArtPoll → Jinx "Scan"
  artnet.setArtDmxCallback(onDmxFrame);

  g_server.on("/", HTTP_GET, handleRoot);
  g_server.on("/config", HTTP_GET, handleConfigGet);
  g_server.on("/config", HTTP_POST, handleConfigPost);
  g_server.begin();

  Serial.println("[ARTNET] Listo");
  Serial.printf("  Universos: %u (desde %u)\n", g_universeCount, g_config.startUniverse);
  Serial.printf("  LEDs: %u, pix/universo: %u\n", g_config.numLeds, g_config.pixelsPerUniverse);
  Serial.print("  IP actual: "); Serial.println(ETH.localIP());
}

void loop()
{
  artnet.read();
  g_server.handleClient();
}
