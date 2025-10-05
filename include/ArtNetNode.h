#pragma once

#include <Arduino.h>
#include <WiFiUdp.h>
#include <array>

class ArtNetNode {
public:
  enum class InterfacePreference : uint8_t {
    Ethernet = 0,
    WiFi     = 1,
    Auto     = 2,
  };

  using ArtDmxCallback = void (*)(uint16_t universe, uint16_t length, uint8_t sequence,
                                  uint8_t* data, IPAddress remoteIP);

  void begin(uint16_t port = 6454);
  void read();

  void setArtDmxCallback(ArtDmxCallback callback);
  void setUniverseInfo(uint16_t startUniverse, uint16_t universeCount);
  void setNodeNames(const String& shortName, const String& longName);
  void updateNetworkInfo();
  void setInterfacePreference(InterfacePreference preference);
  IPAddress localIp() const { return m_localIp; }

private:
  static constexpr uint16_t ARTNET_PORT = 6454;
  static constexpr size_t ARTNET_MAX_BUFFER = 600;

  void sendPollReply(IPAddress remoteIP, uint16_t remotePort);
  void refreshLocalInfo();
  void copyStringToField(const String& source, char* destination, size_t maxLength);

  WiFiUDP m_udp;
  ArtDmxCallback m_dmxCallback = nullptr;
  IPAddress m_localIp;
  IPAddress m_boundIp;
  uint16_t m_listenPort = ARTNET_PORT;
  uint16_t m_startUniverse = 0;
  uint8_t m_portCount = 1;
  String m_shortName = F("PixelEtherLED");
  String m_longName = F("PixelEtherLED Controller");
  std::array<uint8_t, ARTNET_MAX_BUFFER> m_buffer{};
  std::array<uint8_t, 6> m_mac{};
  InterfacePreference m_interfacePreference = InterfacePreference::Ethernet;
  bool m_udpBound = false;
};

