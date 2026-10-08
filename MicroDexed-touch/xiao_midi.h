#ifndef _XIAO_MIDI_H_
#define _XIAO_MIDI_H_

#if !defined(ESP_DISPLAY) && !defined (ONBOARD_BUTTON_INTERFACE)

#include "config.h"

// ---- BLE bridge commands (Teensy -> XIAO) -------------------------
#define XIAO_CMD_BT_ENABLE      110
#define XIAO_CMD_BT_STATUS_REQ  111
#define XIAO_CMD_BT_SCAN_START  112
#define XIAO_CMD_BT_SCAN_STOP   113
#define XIAO_CMD_BT_CONNECT     114
#define XIAO_CMD_BT_LIST_REQ    115

// ---- BLE bridge responses (XIAO -> Teensy) ------------------------
#define XIAO_CMD_BT_STATUS      120   // [enabled, connected, scanning, nameLen, name...]
#define XIAO_CMD_BT_LIST_ITEM   121   // [listIndex, total, nameLen, name...]
#define XIAO_CMD_BT_LIST_END    122   // [total]
#define XIAO_CMD_BT_SCAN_DONE   123   // []
#define XIAO_CMD_BT_ERROR       124   // [code]

// --- Public state (small, no arrays of names) ------------------------------
extern bool    bt_enabled;
extern bool    bt_connected;
extern bool    bt_scanning;
extern uint8_t bt_status_generation;   // header text changes only
extern uint8_t bt_list_generation;     // list content changes only
extern uint8_t bt_device_count;
extern char    bt_connected_name[24];

extern uint8_t bt_status_generation;   // bump when header text changes
extern uint8_t bt_list_generation;     // bump when the list contents change

// ---- Lifecycle ----------------------------------------------------
void xiao_midi_setup(void);
void xiao_midi_loop(void);

// ---- Control API --------------------------------------------------
void xiao_midi_set_enabled(bool en);
void xiao_midi_request_status(void);
void xiao_midi_start_scan(void);
void xiao_midi_stop_scan(void);
void xiao_midi_connect_index(uint8_t idx);

// ---- Streaming list API -------------------------------------------
// Callback is invoked once per list item, in the order the XIAO sends
// them. The 'name' pointer is only valid during the call.
// 'is_midi' is true if the device advertised the BLE-MIDI service.
typedef void (*xiao_bt_list_cb)(uint8_t index, uint8_t total,
                                bool is_midi, const char* name);
void xiao_midi_set_list_callback(xiao_bt_list_cb cb);
void xiao_midi_request_list(uint8_t from, uint8_t count);

#endif // _XIAO_MIDI_H_

#endif
