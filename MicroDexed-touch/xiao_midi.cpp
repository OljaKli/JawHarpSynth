#include "xiao_midi.h"

#if defined XIAO_BLUETOOTH_MIDI

#include <Arduino.h>
#include <string.h>
#include "version.h"

// --- MIDI handlers implemented in MicroDexed-touch.ino ---------------------
extern void handleNoteOn (byte inChannel, byte inNumber, byte inVelocity, byte device);
extern void handleNoteOff(byte inChannel, byte inNumber, byte inVelocity, byte device);
extern void handleControlChange(byte inChannel, byte inCtrl, byte inValue);
extern void handleProgramChange(byte inChannel, byte inProgram);
extern void handleAfterTouch(byte inChannel, byte inPressure);
extern void handlePitchBend(byte inChannel, int16_t inPitch);

// --- Version strings (identical to ESP_DISPLAY path) -----------------------
static const char XIAO_VERSION_STRING[] = VERSION;
static const char XIAO_MIN_ESP_STRING[] = VERSION;

// --- Parser state ----------------------------------------------------------
enum XiaoParserState : uint8_t {
  X_WAIT_SYNC,
  X_WAIT_CMD,
  X_WAIT_LEN,
  X_WAIT_PAYLOAD,
  X_WAIT_CHK
};

static XiaoParserState xState      = X_WAIT_SYNC;
static uint8_t         xCmd        = 0;
static uint8_t         xPayloadLen = 0;
static uint8_t         xPayloadIdx = 0;
static uint8_t         xChecksum   = 0;
static uint8_t         xBuffer[64];    // MIDI packets are <= 3 bytes; 64 is plenty

// --------------------------------------------------------------------------
//  Packet TX (identical to sendPacket() in esp32.cpp - same UART, same framing)
// --------------------------------------------------------------------------
static void xiaoSendPacket(uint8_t cmd, const uint8_t* payload, uint8_t len) {
  if (len > 250) return;

  uint8_t checksum = XIAO_PKT_SYNC ^ cmd ^ len;
  for (uint8_t i = 0; i < len; i++) checksum ^= payload[i];

  XIAO_MIDI_SERIAL.write(XIAO_PKT_SYNC);
  XIAO_MIDI_SERIAL.write(cmd);
  XIAO_MIDI_SERIAL.write(len);
  if (len > 0) XIAO_MIDI_SERIAL.write(payload, len);
  XIAO_MIDI_SERIAL.write(checksum);
}

static void xiaoSendVersion(void) {
  uint8_t len = (uint8_t)strlen(XIAO_VERSION_STRING);
  uint8_t payload[1 + 16];
  payload[0] = len;
  memcpy(&payload[1], XIAO_VERSION_STRING, len);
  xiaoSendPacket(XIAO_CMD_VERSION_REPORT, payload, 1 + len);
}

static void xiaoSendMinEspVersion(void) {
  uint8_t len = (uint8_t)strlen(XIAO_MIN_ESP_STRING);
  uint8_t payload[1 + 16];
  payload[0] = len;
  memcpy(&payload[1], XIAO_MIN_ESP_STRING, len);
  xiaoSendPacket(XIAO_CMD_MIN_VERSION_ESP, payload, 1 + len);
}

static void xiaoSendHandshakeReply(void) {
  xiaoSendVersion();
  delayMicroseconds(200);
  xiaoSendMinEspVersion();
}

// --------------------------------------------------------------------------
//  Raw MIDI dispatcher - identical to processMidiDataFromESP() in UI.cpp
//  Channels arrive as 0..15 on the wire and are converted to 1..16 here.
// --------------------------------------------------------------------------
static void xiaoDispatchMidi(const uint8_t* data, uint8_t len) {
  uint8_t idx = 0;
  uint8_t runningStatus = 0;

  while (idx < len) {
    uint8_t byte = data[idx];

    if ((byte & 0x80) && (byte & 0xF0) != 0xF0) {
      runningStatus = byte;
      idx++;
      uint8_t cmd     = runningStatus & 0xF0;
      uint8_t channel = (runningStatus & 0x0F) + 1;   // 1..16

      if (cmd == 0x90 || cmd == 0x80) {
        if (idx + 1 >= len) break;
        uint8_t note     = data[idx++];
        uint8_t velocity = data[idx++];
        bool    isNoteOn = (cmd == 0x90 && velocity > 0);
        if (isNoteOn) handleNoteOn (channel, note, velocity, 0);
        else          handleNoteOff(channel, note, velocity, 0);
      }
      else if (cmd == 0xB0) {
        if (idx + 1 >= len) break;
        uint8_t controller = data[idx++];
        uint8_t value      = data[idx++];
        handleControlChange(channel, controller, value);
      }
      else if (cmd == 0xC0) {
        if (idx >= len) break;
        handleProgramChange(channel, data[idx++]);
      }
      else if (cmd == 0xD0) {
        if (idx >= len) break;
        handleAfterTouch(channel, data[idx++]);
      }
      else if (cmd == 0xE0) {
        if (idx + 1 >= len) break;
        uint8_t lsb = data[idx++];
        uint8_t msb = data[idx++];
        handlePitchBend(channel, ((int16_t)((msb << 7) | lsb)) - 8192);
      }
      else {
        if (idx + 1 >= len) break;
        idx += 2;   // skip unknown 2-byte messages
      }
    }
    else if (runningStatus != 0) {
      uint8_t cmd     = runningStatus & 0xF0;
      uint8_t channel = (runningStatus & 0x0F) + 1;

      if (cmd == 0x90 || cmd == 0x80) {
        if (idx + 1 >= len) break;
        uint8_t note     = data[idx++];
        uint8_t velocity = data[idx++];
        bool    isNoteOn = (cmd == 0x90 && velocity > 0);
        if (isNoteOn) handleNoteOn (channel, note, velocity, 0);
        else          handleNoteOff(channel, note, velocity, 0);
      }
      else if (cmd == 0xB0) {
        if (idx + 1 >= len) break;
        uint8_t controller = data[idx++];
        uint8_t value      = data[idx++];
        handleControlChange(channel, controller, value);
      }
      else if (cmd == 0xE0) {
        if (idx + 1 >= len) break;
        uint8_t lsb = data[idx++];
        uint8_t msb = data[idx++];
        handlePitchBend(channel, ((int16_t)((msb << 7) | lsb)) - 8192);
      }
      else {
        idx++;
      }
    }
    else {
      break;   // orphan data byte - abort
    }
  }
}

// --------------------------------------------------------------------------
//  Setup / Loop
// --------------------------------------------------------------------------
void xiao_midi_setup(void) {
  XIAO_MIDI_SERIAL.begin(XIAO_MIDI_BAUD);   // Serial4, exactly like ESP_DISPLAY
  XIAO_MIDI_SERIAL.setTimeout(1);
#ifdef DEBUG
  LOG.print(F("XIAO_BLUETOOTH_MIDI: Serial4 ready @ "));
  LOG.println(XIAO_MIDI_BAUD, DEC);
#endif
}

void xiao_midi_loop(void) {
  while (XIAO_MIDI_SERIAL.available() > 0) {
    uint8_t b = (uint8_t)XIAO_MIDI_SERIAL.read();

    // The handshake byte 0x98 is always sent as a bare byte, never inside a
    // packet, so it is safe to intercept it while we are waiting for a sync.
    if (xState == X_WAIT_SYNC && b == XIAO_HANDSHAKE_REQ) {
      xiaoSendHandshakeReply();
      continue;
    }

    switch (xState) {
      case X_WAIT_SYNC:
        if (b == XIAO_PKT_SYNC) {
          xState    = X_WAIT_CMD;
          xChecksum = b;
        }
        break;

      case X_WAIT_CMD:
        xCmd       = b;
        xChecksum ^= b;
        xState     = X_WAIT_LEN;
        break;

      case X_WAIT_LEN:
        xPayloadLen = b;
        xChecksum  ^= b;
        if (xPayloadLen == 0) {
          xState = X_WAIT_CHK;
        }
        else if (xPayloadLen > sizeof(xBuffer)) {
          xState = X_WAIT_SYNC;         // corrupt - resync
        }
        else {
          xPayloadIdx = 0;
          xState      = X_WAIT_PAYLOAD;
        }
        break;

      case X_WAIT_PAYLOAD:
        xBuffer[xPayloadIdx++] = b;
        xChecksum ^= b;
        if (xPayloadIdx >= xPayloadLen) xState = X_WAIT_CHK;
        break;

      case X_WAIT_CHK:
        if (b == xChecksum) {
          // Only command we need to act on
          if (xCmd == XIAO_CMD_MIDI_DATA &&
              xPayloadLen >= 2 && xPayloadLen <= 3) {
            xiaoDispatchMidi(xBuffer, xPayloadLen);
          }
          // All other XIAO -> Teensy commands are silently ignored.
        }
        xState = X_WAIT_SYNC;
        break;
    }
  }
}

#endif // XIAO_BLUETOOTH_MIDI