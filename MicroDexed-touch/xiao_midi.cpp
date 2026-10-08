
#if !defined(ESP_DISPLAY) && !defined (ONBOARD_BUTTON_INTERFACE)

#include "xiao_midi.h"
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

// --- Public state (small, no arrays of names) ------------------------------
bool    bt_enabled              = true;
bool    bt_connected            = false;
bool    bt_scanning             = false;
uint8_t bt_status_generation    = 0;
uint8_t bt_list_generation      = 0;
uint8_t bt_device_count         = 0;
char    bt_connected_name[24]   = { 0 };

// --- Version strings (identical to ESP_DISPLAY path) -----------------------
static const char XIAO_VERSION_STRING[]  = VERSION;
static const char XIAO_MIN_ESP_STRING[]  = VERSION;


// --- Parser state ----------------------------------------------------------
enum XiaoParserState : uint8_t {
  X_WAIT_SYNC, X_WAIT_CMD, X_WAIT_LEN, X_WAIT_PAYLOAD, X_WAIT_CHK
};
static XiaoParserState xState      = X_WAIT_SYNC;
static uint8_t         xCmd        = 0;
static uint8_t         xPayloadLen = 0;
static uint8_t         xPayloadIdx = 0;
static uint8_t         xChecksum   = 0;
static uint8_t         xBuffer[64];

// --- Streaming list callback ----------------------------------------------
static xiao_bt_list_cb xListCb = nullptr;

// ==========================================================================
//  Packet TX / helpers
// ==========================================================================
FLASHMEM void xiaoSendPacket(uint8_t cmd, const uint8_t* payload, uint8_t len) {
  if (len > 250) return;
  uint8_t checksum = XIAO_PKT_SYNC ^ cmd ^ len;
  for (uint8_t i = 0; i < len; i++) checksum ^= payload[i];

  XIAO_MIDI_SERIAL.write(XIAO_PKT_SYNC);
  XIAO_MIDI_SERIAL.write(cmd);
  XIAO_MIDI_SERIAL.write(len);
  if (len > 0) XIAO_MIDI_SERIAL.write(payload, len);
  XIAO_MIDI_SERIAL.write(checksum);
}

FLASHMEM void xiaoSendVersion(void) {
  uint8_t len = (uint8_t)strlen(XIAO_VERSION_STRING);
  uint8_t payload[1 + 16];
  payload[0] = len;
  memcpy(&payload[1], XIAO_VERSION_STRING, len);
  xiaoSendPacket(XIAO_CMD_VERSION_REPORT, payload, 1 + len);
}

FLASHMEM void xiaoSendMinEspVersion(void) {
  uint8_t len = (uint8_t)strlen(XIAO_MIN_ESP_STRING);
  uint8_t payload[1 + 16];
  payload[0] = len;
  memcpy(&payload[1], XIAO_MIN_ESP_STRING, len);
  xiaoSendPacket(XIAO_CMD_MIN_VERSION_ESP, payload, 1 + len);
}

FLASHMEM void xiaoSendHandshakeReply(void) {
  xiaoSendVersion();
  delayMicroseconds(200);
  xiaoSendMinEspVersion();
}

// ==========================================================================
//  Public API
// ==========================================================================
FLASHMEM void xiao_midi_set_enabled(bool en) {
  if (bt_enabled != en) {
    bt_enabled = en;
    if (!en) {
      // Reflect the state immediately on the local side so the UI does not
      // display a stale "connected" state while the XIAO tears down.
      bt_connected = false;
      bt_scanning  = false;
      bt_connected_name[0] = '\0';
    }
    bt_status_generation++;   // triggers UI refresh
  }
  uint8_t p = en ? 1 : 0;
  xiaoSendPacket(XIAO_CMD_BT_ENABLE, &p, 1);
}

FLASHMEM void xiao_midi_request_status(void) {
  xiaoSendPacket(XIAO_CMD_BT_STATUS_REQ, nullptr, 0);
}

FLASHMEM void xiao_midi_start_scan(void) {
  xiaoSendPacket(XIAO_CMD_BT_SCAN_START, nullptr, 0);
}

FLASHMEM void xiao_midi_stop_scan(void) {
  xiaoSendPacket(XIAO_CMD_BT_SCAN_STOP, nullptr, 0);
}

FLASHMEM void xiao_midi_connect_index(uint8_t idx) {
  xiaoSendPacket(XIAO_CMD_BT_CONNECT, &idx, 1);
}

FLASHMEM void xiao_midi_set_list_callback(xiao_bt_list_cb cb) {
  xListCb = cb;
}

FLASHMEM void xiao_midi_request_list(uint8_t from, uint8_t count) {
  uint8_t payload[2] = { from, count };
  xiaoSendPacket(XIAO_CMD_BT_LIST_REQ, payload, 2);
}

// ==========================================================================
//  Raw MIDI dispatcher - identical to processMidiDataFromESP() in UI.cpp
// ==========================================================================
FLASHMEM void xiaoDispatchMidi(const uint8_t* data, uint8_t len) {
  uint8_t idx = 0;
  uint8_t runningStatus = 0;

  while (idx < len) {
    uint8_t byte = data[idx];

    if ((byte & 0x80) && (byte & 0xF0) != 0xF0) {
      runningStatus = byte;
      idx++;
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
        idx += 2;
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
      break;
    }
  }
}

// ==========================================================================
//  BLE bridge response handlers
// ==========================================================================
FLASHMEM void handleBtStatus(const uint8_t* p, uint8_t len) {
  if (len < 4) return;

  bool    new_enabled   = (p[0] != 0);
  bool    new_connected = (p[1] != 0);
  bool    new_scanning  = (p[2] != 0);
  uint8_t nameLen       = p[3];
  if (nameLen > 23) nameLen = 23;

  char new_name[24] = { 0 };
  if (new_connected && nameLen > 0 && len >= (uint8_t)(4 + nameLen)) {
    memcpy(new_name, &p[4], nameLen);
    new_name[nameLen] = '\0';
  }

  bool changed = (new_enabled   != bt_enabled)
              || (new_connected != bt_connected)
              || (new_scanning  != bt_scanning)
              || (strcmp(new_name, bt_connected_name) != 0);

  bt_enabled   = new_enabled;
  bt_connected = new_connected;
  bt_scanning  = new_scanning;
  strncpy(bt_connected_name, new_name, sizeof(bt_connected_name) - 1);
  bt_connected_name[sizeof(bt_connected_name) - 1] = '\0';

  if (changed) {
    bt_status_generation++;
  }
}

FLASHMEM void handleBtListItem(const uint8_t* p, uint8_t len) {
  if (len < 4) return;

  uint8_t index   = p[0];
  uint8_t total   = p[1];
  bool    is_midi = (p[2] & 0x01) != 0;
  uint8_t nameLen = p[3];
  if (nameLen > 20) nameLen = 20;
  if (len < (uint8_t)(4 + nameLen)) return;

  bt_device_count = total;

  char name[21];
  memcpy(name, &p[4], nameLen);
  name[nameLen] = '\0';

  if (xListCb) xListCb(index, total, is_midi, name);
}

FLASHMEM void handleBtListEnd(const uint8_t* p, uint8_t len) {
  if (len < 1) return;
  bt_device_count = p[0];
  // NOTE: do NOT bump bt_list_generation here. The items were already
  // drawn by handleBtListItem as they streamed in; bumping now would
  // trigger a second window request from the BT page and cause a
  // continuous clear-and-redraw loop.
}

FLASHMEM void handleBtScanDone(void) {
  bt_list_generation++;
}

FLASHMEM void handleBtError(const uint8_t* p, uint8_t len) {
  if (len < 1) return;
#if defined DEBUG
  LOG.print(F("XIAO BT error: "));
  LOG.println(p[0]);
#endif
  bt_status_generation++;
}



// ==========================================================================
//  Setup / Loop
// ==========================================================================
FLASHMEM void xiao_midi_setup(void) {
  XIAO_MIDI_SERIAL.begin(XIAO_MIDI_BAUD);
  XIAO_MIDI_SERIAL.setTimeout(1);
}

FLASHMEM void xiao_midi_loop(void) {
  while (XIAO_MIDI_SERIAL.available() > 0) {
    uint8_t b = (uint8_t)XIAO_MIDI_SERIAL.read();

    // 0x98 is the handshake request, always sent as a bare byte.
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
        xCmd        = b;
        xChecksum  ^= b;
        xState      = X_WAIT_LEN;
        break;

      case X_WAIT_LEN:
        xPayloadLen = b;
        xChecksum  ^= b;
        if (xPayloadLen == 0) {
          xState = X_WAIT_CHK;
        }
        else if (xPayloadLen > sizeof(xBuffer)) {
          xState = X_WAIT_SYNC;
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
          switch (xCmd) {
            case XIAO_CMD_MIDI_DATA:
              if (xPayloadLen >= 2 && xPayloadLen <= 3)
                xiaoDispatchMidi(xBuffer, xPayloadLen);
              break;
            case XIAO_CMD_BT_STATUS:    handleBtStatus   (xBuffer, xPayloadLen); break;
            case XIAO_CMD_BT_LIST_ITEM: handleBtListItem (xBuffer, xPayloadLen); break;
            case XIAO_CMD_BT_LIST_END:  handleBtListEnd  (xBuffer, xPayloadLen); break;
            case XIAO_CMD_BT_SCAN_DONE: handleBtScanDone ();                     break;
            case XIAO_CMD_BT_ERROR:     handleBtError    (xBuffer, xPayloadLen); break;
            default: break;
          }
        }
        xState = X_WAIT_SYNC;
        break;
    }
  }
}

#endif