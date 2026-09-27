#pragma once
#include <Arduino.h>

#ifdef MINI_ENCODER
#include "i2cEncoderMiniLib.h"

// I2C addresses of the two Mini encoders
#define I2C_ENC_L_ADDR  0x24
#define I2C_ENC_R_ADDR  0x25
#define I2C_ENC_DEFAULT 0x20

extern i2cEncoderMiniLib enc1;   // left, expected at 0x24
extern i2cEncoderMiniLib enc2;   // right, expected at 0x25

// Bus-scan results (1 = device responded, 0 = not present)
extern uint8_t encoder_detected_0x20;
extern uint8_t encoder_detected_0x24;
extern uint8_t encoder_detected_0x25;

extern bool encoder_setup_active;

// Set by the touch handler when the user wants to assign an address.
extern volatile uint8_t encoder_setup_requested_addr;

// Call AFTER Wire.begin() has been called.
//
// Scans the bus. If both encoders respond at 0x24/0x25 and nothing is stuck
// at 0x20, the encoders are fully initialised and this returns false.
// Otherwise the encoders are left untouched, the UI page must be shown and
// this returns true.
bool i2c_encoders_setup(void);

// Re-scan the I2C bus for 0x20 / 0x24 / 0x25
void encoder_rescan_bus(void);



void handle_touchscreen_encoder_address_setup(void);

// Callback hooks. Weak stubs are provided in the .cpp, so you can override
// them from your sketch (same names as before).
void onEncoder1Increment(i2cEncoderMiniLib*);
void onEncoder1Decrement(i2cEncoderMiniLib*);
void onEncoder2Increment(i2cEncoderMiniLib*);
void onEncoder2Decrement(i2cEncoderMiniLib*);
void onEncoder1Push(i2cEncoderMiniLib*);

#endif // I2C_ENCODERS