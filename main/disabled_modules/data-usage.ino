#include "globals.h"

 void datausage(){
  // Get RAM info
  size_t freeHeap = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
  size_t totalHeap = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
  int ramUsage = 100 - ((freeHeap * 100) / totalHeap);

  // Get Flash info
  uint32_t flashSize = ESP.getFlashChipSize();

  uint32_t flashUsed = ESP.getSketchSize();
  int flashUsage = (flashUsed * 100) / flashSize;

  // Temperature (approx)
  uint8_t temperature = temperatureRead();

  // Chip info
  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);

  
}

