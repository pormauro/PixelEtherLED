#include "ArtNetNode.h"

#include <ETH.h>
#include <WiFi.h>
#include <esp_system.h>
#include <algorithm>
#include <cstring>

namespace {
constexpr char kArtNetId[] = "Art-Net";
constexpr uint16_t kOpPoll = 0x2000;
constexpr uint16_t kOpDmx = 0x5000;
constexpr uint16_t kOpPollReply = 0x2100;

struct __attribute__((packed)) ArtPollReplyPacket {
  char id[8];
  uint16_t opCode;
  uint8_t ip[4];
  uint16_t port;
  uint8_t versInfoH;
  uint8_t versInfoL;
  uint8_t netSwitch;
  uint8_t subSwitch;
  uint16_t oem;
  uint8_t ubeaVersion;
  uint8_t status1;
  uint16_t estaMan;
  char shortName[18];
  char longName[64];
  char nodeReport[64];
  uint8_t numPortsHi;
  uint8_t numPortsLo;
  uint8_t portTypes[4];
  uint8_t goodInput[4];
  uint8_t goodOutput[4];
  uint8_t swIn[4];
  uint8_t swOut[4];
  uint8_t swVideo;
  uint8_t swMacro;
  uint8_t swRemote;
  uint8_t spare[3];
  uint8_t style;
  uint8_t mac[6];
  uint8_t bindIp[4];
  uint8_t bindIndex;
  uint8_t status2;
  uint8_t filler[26];
};

uint8_t clampPortCount(uint16_t value) {
  if (value == 0) return 1;
  if (value > 4) return 4;
  return static_cast<uint8_t>(value);
}

}  // namespace

void ArtNetNode::begin(uint16_t port)
{
  m_listenPort = port;
  m_udp.stop();
  m_udp.begin(m_listenPort);
  updateNetworkInfo();
}

void ArtNetNode::setArtDmxCallback(ArtDmxCallback callback)
{
  m_dmxCallback = callback;
}

void ArtNetNode::setUniverseInfo(uint16_t startUniverse, uint16_t universeCount)
{
  m_startUniverse = startUniverse;
  uint8_t desired = clampPortCount(universeCount);
  if (desired == 0) desired = 1;

  const uint16_t baseSub = (m_startUniverse >> 4) & 0x0F;
  const uint16_t baseNet = (m_startUniverse >> 8) & 0x7F;

  while (desired > 1) {
    uint16_t lastUniverse = m_startUniverse + desired - 1;
    uint16_t lastSub = (lastUniverse >> 4) & 0x0F;
    uint16_t lastNet = (lastUniverse >> 8) & 0x7F;
    if (lastSub != baseSub || lastNet != baseNet) {
      --desired;
    } else {
      break;
    }
  }

  m_portCount = desired;
}

void ArtNetNode::setNodeNames(const String& shortName, const String& longName)
{
  if (shortName.length()) m_shortName = shortName;
  if (longName.length()) m_longName = longName;
}

void ArtNetNode::updateNetworkInfo()
{
  refreshLocalInfo();
}

void ArtNetNode::refreshLocalInfo()
{
  IPAddress current = ETH.localIP();
  if (current == IPAddress((uint32_t)0)) {
    current = WiFi.localIP();
  }
  m_localIp = current;

  if (esp_read_mac(m_mac.data(), ESP_MAC_ETH) != ESP_OK) {
    std::fill(m_mac.begin(), m_mac.end(), 0);
  }
}

void ArtNetNode::copyStringToField(const String& source, char* destination, size_t maxLength)
{
  if (maxLength == 0) return;
  memset(destination, 0, maxLength);
  size_t copyLen = std::min(static_cast<size_t>(source.length()), maxLength - 1);
  memcpy(destination, source.c_str(), copyLen);
  destination[copyLen] = '\0';
}

void ArtNetNode::sendPollReply(IPAddress remoteIP, uint16_t remotePort)
{
  if (m_localIp == IPAddress((uint32_t)0)) {
    refreshLocalInfo();
  }

  if (remoteIP == IPAddress((uint32_t)0)) {
    return;
  }

  ArtPollReplyPacket reply{};
  memcpy(reply.id, kArtNetId, sizeof(reply.id));
  reply.id[7] = '\0';
  reply.opCode = kOpPollReply;
  for (uint8_t i = 0; i < 4; ++i) {
    reply.ip[i] = m_localIp[i];
    reply.bindIp[i] = m_localIp[i];
  }
  reply.port = ARTNET_PORT;
  reply.versInfoH = 1;
  reply.versInfoL = 0;
  reply.netSwitch = (m_startUniverse >> 8) & 0x7F;
  reply.subSwitch = (m_startUniverse >> 4) & 0x0F;
  reply.oem = 0xffff;
  reply.ubeaVersion = 0;
  reply.status1 = 0xD0;
  reply.estaMan = 0;
  copyStringToField(m_shortName, reply.shortName, sizeof(reply.shortName));
  copyStringToField(m_longName, reply.longName, sizeof(reply.longName));
  copyStringToField(String(F("#0001 [ok] PixelEtherLED")), reply.nodeReport, sizeof(reply.nodeReport));
  reply.numPortsHi = 0;
  reply.numPortsLo = m_portCount;

  for (uint8_t i = 0; i < 4; ++i) {
    reply.portTypes[i] = (i < m_portCount) ? 0x80 : 0x00;
    reply.goodInput[i] = 0x00;
    reply.goodOutput[i] = (i < m_portCount) ? 0x80 : 0x00;
    reply.swIn[i] = 0x00;
    reply.swOut[i] = (i < m_portCount) ? static_cast<uint8_t>((m_startUniverse + i) & 0x0F) : 0x00;
  }

  reply.swVideo = 0;
  reply.swMacro = 0;
  reply.swRemote = 0;
  memset(reply.spare, 0, sizeof(reply.spare));
  reply.style = 0x00;
  std::copy(m_mac.begin(), m_mac.end(), reply.mac);
  reply.bindIndex = 1;
  reply.status2 = 0x00;
  memset(reply.filler, 0, sizeof(reply.filler));

  m_udp.beginPacket(remoteIP, remotePort ? remotePort : ARTNET_PORT);
  m_udp.write(reinterpret_cast<uint8_t*>(&reply), sizeof(reply));
  m_udp.endPacket();
}

void ArtNetNode::read()
{
  refreshLocalInfo();

  int packetSize = m_udp.parsePacket();
  if (packetSize <= 0) {
    return;
  }
  if (packetSize > static_cast<int>(m_buffer.size())) {
    packetSize = static_cast<int>(m_buffer.size());
  }

  int len = m_udp.read(m_buffer.data(), packetSize);
  if (len < 10) {
    return;
  }

  if (memcmp(m_buffer.data(), kArtNetId, 8) != 0) {
    return;
  }

  uint16_t opCode = static_cast<uint16_t>(m_buffer[8]) | (static_cast<uint16_t>(m_buffer[9]) << 8);

  if (opCode == kOpPoll) {
    sendPollReply(m_udp.remoteIP(), m_udp.remotePort());
    return;
  }

  if (opCode != kOpDmx) {
    return;
  }

  if (len < 18) {
    return;
  }

  uint8_t sequence = m_buffer[12];
  uint16_t universe = static_cast<uint16_t>(m_buffer[14]) | (static_cast<uint16_t>(m_buffer[15]) << 8);
  uint16_t dataLength = static_cast<uint16_t>(m_buffer[16]) << 8 | static_cast<uint16_t>(m_buffer[17]);

  if (dataLength > static_cast<uint16_t>(len - 18)) {
    dataLength = static_cast<uint16_t>(len - 18);
  }

  if (m_dmxCallback) {
    m_dmxCallback(universe, dataLength, sequence, m_buffer.data() + 18, m_udp.remoteIP());
  }
}

