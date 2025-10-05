#include "AppConfig.h"

const char* getChipName(uint8_t value)
{
  if (value < static_cast<uint8_t>(LedChipType::CHIP_TYPE_COUNT)) {
    return CHIP_TYPE_NAMES[value];
  }
  return "Desconocido";
}

const char* getColorOrderName(uint8_t value)
{
  if (value < static_cast<uint8_t>(LedColorOrder::COLOR_ORDER_COUNT)) {
    return COLOR_ORDER_NAMES[value];
  }
  return "Desconocido";
}
