#ifndef PSRAMLOADER_H
#define PSRAMLOADER_H

#include "config.h"
#include "drums.h"

class PsramLoader {
public:

  PsramLoader(uint32_t &freeBytes);

  void loadSampleToDrums(const char* path, drum_config_t &slot);

  uint8_t* loadSample(const char *filename, uint32_t &bytesRead, uint8_t &channels);
  void unloadSample(uint8_t *data, uint32_t length);
  

private:
  uint32_t &freeBytesPSRAM;
  static constexpr uint32_t DEFAULT_SD_BUFSIZE = 4 * 1024;
  static constexpr uint8_t WAV_HEADER_SIZE = 44;
  
};

#endif //PSRAMLOADER_H