#include "psramloader.h"
#include <Audio.h>

FLASHMEM PsramLoader::PsramLoader(uint32_t &freeBytes) : 
  freeBytesPSRAM(freeBytes) {
}

FLASHMEM void PsramLoader::loadSampleToDrums(const String path, drum_config_t &slot) {
  if((slot.drum_data != nullptr) && (slot.len > 0)) {
    unloadSample(slot.drum_data, slot.len * 2); // len is 16bit
  }
  uint32_t bytesRead = 0;
  slot.drum_data = loadSample(path, bytesRead, slot.numChannels);
  slot.len = bytesRead / 2;
}

FLASHMEM uint8_t* PsramLoader::loadSample(const String filename, uint32_t &bytesRead, uint8_t &channels) {
  uint8_t *data = nullptr;
  Serial.printf("Reading %s\n", filename);
  File f = SD.open(filename.c_str(), O_READ);
  if (f) {
    if (f.size() < freeBytesPSRAM) {
      AudioNoInterrupts();
      
      uint8_t header[WAV_HEADER_SIZE];
      f.read(header, WAV_HEADER_SIZE);
      uint16_t numCh = (header[23] << 8) | header[22];
      //uint32_t size = (header[43] << 24) | header[42] << 16 | header[41] << 8 | header[40];

      uint32_t total_read = 0;
      data = (uint8_t*)extmem_malloc(f.size() - WAV_HEADER_SIZE);
      uint8_t *index = data;
      while (f.available()) {
        size_t bytesRead = f.read(index, DEFAULT_SD_BUFSIZE);
        if (bytesRead == 0)
          break;
        total_read += bytesRead;
        index += bytesRead;
      }
      AudioInterrupts();
      freeBytesPSRAM -= total_read;
      DBG_LOG(printf("loaded %i bytes. remaining %i\n", total_read, freeBytesPSRAM));
      bytesRead = total_read;
      channels = numCh;
    }
  }

  return data;
}

FLASHMEM void PsramLoader::unloadSample(const uint8_t *data, uint32_t length) {
  extmem_free((uint8_t*)data);
  freeBytesPSRAM += length;
  DBG_LOG(printf("freeing %i bytes...\n", length));
}

