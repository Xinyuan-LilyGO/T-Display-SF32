# Keyboard Key and LED Control Example

## Program Description

This example demonstrates how to control keyboard keys and LED lights using the T-Display-SF32 development board.

## Hardware Components

- **TCA8418**: Keyboard scanning chip
- **AW21009**: LED driver chip
- **XL9555**: IO expander chip

## Program Behavior

1. **Initialization Phase**
   - After program startup, the LOD_PIN (PA41) pin is initialized and set to high level
   - Initialize the XL9555 IO expander chip
   - Enable the WIFI module (XL9555_WIFI_EN_PIN)
   - Enable keyboard LEDs (XL9555_KEY_LED_EN_PIN)
   - Initialize the AW21009 LED driver chip
   - Initialize the TCA8418 keyboard scanning chip

2. **LED Breathing Effect**
   - Create a thread named "key_led_heart"
   - After the thread starts, all LEDs will show a breathing effect

3. **Key Detection**
   - The main loop continuously detects keyboard key events
   - When a key press is detected, the key code is output to the console
   - Key detection uses a message queue mechanism for real-time response to key events

## Usage Instructions

1. Flash the program to the T-Display-SF32 development board
2. Connect the keyboard and LED hardware
3. Restart the development board
4. Observe the LED breathing effect
5. Press keyboard keys and check the key codes output to the console

## Notes

- Ensure hardware connections are correct
- Ensure keyboard and LED driver chips are properly initialized
- If initialization fails, corresponding error messages will be output to the console
