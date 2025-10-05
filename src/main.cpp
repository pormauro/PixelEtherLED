#include <Arduino.h>
#include <WiFi.h>
#include <ETH.h>
#include <WiFiUdp.h>
#include <Artnet.h>
#include <FastLED.h>

// ===================== CONFIG RED (IP FIJA - FALLBACK) =====================
static const IPAddress STATIC_IP   (192, 168, 1, 50);
static const IPAddress STATIC_GW   (192, 168, 1, 1);
static const IPAddress STATIC_MASK (255, 255, 255, 0);
static const IPAddress STATIC_DNS1 (1, 1, 1, 1);
static const IPAddress STATIC_DNS2 (8, 8, 8, 8);

// ===================== LEDS =====================
#define DATA_PIN      2
#define NUM_LEDS      60
#define LED_TYPE      WS2811
#define COLOR_ORDER   BRG
CRGB leds[NUM_LEDS];

// ===================== ART-NET =====================
const uint16_t START_UNIVERSE = 0;
const uint16_t PIXELS_PER_UNIVERSE = 170;      // 512/3
const uint16_t UNIVERSE_COUNT =
  (NUM_LEDS + PIXELS_PER_UNIVERSE - 1) / PIXELS_PER_UNIVERSE;

Artnet artnet;
bool universeReceived[16] = {0}; // Aumentá si usás >16 universos

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
void onDmxFrame(uint16_t universe, uint16_t length, uint8_t sequence,
                uint8_t* data, IPAddress remoteIP)
{
  g_dmxFrames++;

  if (universe < START_UNIVERSE || universe >= (START_UNIVERSE + UNIVERSE_COUNT)) return;

  const uint16_t idxU = universe - START_UNIVERSE;
  const uint16_t pixelOffset = idxU * PIXELS_PER_UNIVERSE;

  const uint16_t maxPixThisU    = min<uint16_t>(PIXELS_PER_UNIVERSE, NUM_LEDS - pixelOffset);
  const uint16_t pixelsInPacket = min<uint16_t>(length / 3, maxPixThisU);

  for (uint16_t i = 0; i < pixelsInPacket; i++) {
    const uint16_t ledIndex = pixelOffset + i;
    const uint8_t r = data[i * 3 + 0];
    const uint8_t g = data[i * 3 + 1];
    const uint8_t b = data[i * 3 + 2];
    leds[ledIndex].setRGB(r, g, b);
  }

  universeReceived[idxU] = true;

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
    if (ledIndexToShow < NUM_LEDS) {
      CRGB c = leds[ledIndexToShow];
      Serial.printf("\n  LED[%u]=(%u,%u,%u)\n", ledIndexToShow, c.r, c.g, c.b);
    } else {
      Serial.println();
    }
  }
#endif

  // Actualizar cuando recibimos al menos un paquete de cada universo
  bool all = true;
  for (uint16_t i = 0; i < UNIVERSE_COUNT; i++) { if (!universeReceived[i]) { all = false; break; } }
  if (all) {
    FastLED.show();
    memset(universeReceived, 0, sizeof(universeReceived));
  }
}

void bringUpEthernetWithDhcpFallback(unsigned long dhcpTimeoutMs = 3000)
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

  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.clear(true);
  FastLED.setDither(0);
  FastLED.setBrightness(255);

  WiFi.onEvent(onWiFiEvent);
  bringUpEthernetWithDhcpFallback();

  artnet.begin();                      // responde a ArtPoll → Jinx "Scan"
  artnet.setArtDmxCallback(onDmxFrame);

  Serial.println("[ARTNET] Listo");
  Serial.printf("  Universos: %u (desde %u)\n", UNIVERSE_COUNT, START_UNIVERSE);
  Serial.printf("  LEDs: %u, pix/universo: %u\n", NUM_LEDS, PIXELS_PER_UNIVERSE);
  Serial.print("  IP actual: "); Serial.println(ETH.localIP());
}

void loop()
{
  artnet.read();  // procesa DMX y ArtPoll

  // Heartbeat visual opcional
  static uint32_t lastBlink = 0;
  if (millis() - lastBlink > 5000 && eth_link_up) {
    lastBlink = millis();
    leds[NUM_LEDS - 1] = CRGB::Blue; FastLED.show(); delay(20);
    leds[NUM_LEDS - 1] = CRGB::Black; FastLED.show();
  }
}
