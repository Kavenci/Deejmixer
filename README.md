# ESP32 LED Mixer with OTA

A DIY ESP32-based audio mixer controller with **9 sliders**, **RGB LED matrix feedback**, and **OTA firmware updates via captive portal**.  
Built to integrate with [deej](https://github.com/omriharel/deej) or your own custom setups.  

---

## ✨ Features

- **9 Analog Inputs** (5 sliders + 4 pots, or any combo).  
- **5×5 WS2812B LED Matrix** with multiple effects: rainbow, meteor, breathing, vertical/column/diagonal waves, raindrop sparkle.  
- **Mode Button**:
  - Short press → cycle LED effects.  
  - Long press (5s) → start OTA update mode (Wi-Fi AP with captive portal).  
- **OTA Updates**:
  - ESP32 creates its own Wi-Fi AP (`DeeJ-OTA` / `flashme123`).  
  - Connect, auto-open the captive portal, and upload a `.bin` firmware.  
- **EEPROM Storage**:
  - Remembers last LED effect after reboot.  
- **Non-blocking LED animations** to keep the button responsive.  
- **Configurable brightness** (set in code).  

---

## 🛠️ Materials

- **ESP32 Dev Board** (NodeMCU-32S, WROOM, etc.)  
- **9× 10k Potentiometers** (sliders or rotaries, mix as you prefer)  
- **25× WS2812B LEDs** (5×5 grid, or 5 strips of 5)  
- **1× Momentary Push Button** (12 mm recommended)  
- **Wires, headers, USB cable**  
- *(Optional)* 3D-printed case, knobs, and panel  

---

## ⚡ Wiring

- **Sliders/Pots** → GPIOs 34, 35, 32, 33, 25, 36, 14, 27, 26 (analog inputs).  
- **LED Strip Data** → GPIO 22.  
- **Button** → GPIO 4 to GND.  
- **Power** → USB 5 V / GND.  

> ⚠️ ESP32 ADC is 12-bit (0–4095). Code maps readings to 10-bit (0–1023) for compatibility.  

---

## 💻 Software

1. Clone this repo:  
   ```bash
   git clone https://github.com/yourusername/esp32-led-mixer.git
   cd esp32-led-mixer
   ```
2. Open the Arduino IDE or PlatformIO.  
3. Install required libraries:  
   - `Adafruit_NeoPixel`  
   - `WiFi` (built-in)  
   - `WebServer` (ESP32 core)  
   - `DNSServer` (ESP32 core)  
   - `EEPROM` (ESP32 core)  
4. Flash the provided `.ino` sketch.  

---

## 🚀 OTA Updates

- Long-press the button (5s).  
- LEDs flash white, ESP32 creates an AP:  
  - **SSID:** `DeeJ-OTA`  
  - **Password:** `flashme123`  
- Connect with your PC/phone → captive portal auto-opens.  
- Upload new `.bin` file and the ESP32 reboots.  
- Idle timeout: AP shuts down after 10 min inactivity.  

---

## 🎨 LED Effects

- **Mode 0** → Off  
- **Mode 1** → Rainbow cycle  
- **Mode 2** → Meteor random flash  
- **Mode 3** → Breathing random colors  
- **Mode 4** → Vertical sine wave  
- **Mode 5** → Column ripple wave  
- **Mode 6** → Diagonal traveling wave  
- **Mode 7** → Raindrop sparkle  

---

## 🖨️ 3D Printing (Optional)

You can design or remix a case with:  
- 9 slots for sliders/pots  
- Button cut-out  
- 5×5 LED diffuser grid  

Knobs can be found on [Printables](https://www.printables.com) or [MakerWorld](https://makerworld.com).  

---

## 🔧 Customization

- Change pin mapping, LED count, or brightness (`LED_BRIGHTNESS`) in code.  
- Add/remove LED effects (all are non-blocking).  
- Adjust button long-press time (`LONG_HOLD_MS`).  
- Tie one of the sliders to brightness/speed control.  

---

## 📜 License

MIT – free to use, remix, and share.  
