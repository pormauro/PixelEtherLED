#pragma once

#include <Arduino.h>
#include <IPAddress.h>

struct AppConfig;

struct WebUiRuntime {
  bool ethLinkUp;
  bool ethHasIp;
  IPAddress ethLocalIp;
  bool wifiStaConnected;
  bool wifiStaHasIp;
  IPAddress wifiStaIp;
  IPAddress wifiApIp;
  bool wifiApRunning;
  String wifiStaSsidCurrent;
  uint8_t wifiClientCount;
  IPAddress wifiLocalIp;
  IPAddress wifiSoftApIp;
  IPAddress artnetIp;
  uint16_t universeCount;
  unsigned long dmxFrames;
};

String renderConfigPage(const AppConfig& config,
                        const WebUiRuntime& runtime,
                        const String& message);

String renderVisualizerPage(const AppConfig& config,
                            const WebUiRuntime& runtime);
