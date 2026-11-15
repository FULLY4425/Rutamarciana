#include <Wire.h>
#include <SPI.h>
#include <core_esp8266_waveform.h>                // startWaveform(): tones with a chosen pulse width (volume)
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <LittleFS.h>
#include <ESP8266WebServer.h>                      // PC remote: screen mirror, knob + keyboard from the PC, files
#include <WiFiUdp.h>
#include <lwip/etharp.h>
#include <lwip/netif.h>
extern "C" {
#include <ping.h>
#include <user_interface.h>                         // system_update_cpu_freq(): 160 MHz turbo
}
// GFXcanvas16 has no virtual destructor, but we only ever delete exact GFXcanvas16 objects: safe
#pragma GCC diagnostic ignored "-Wdelete-non-virtual-dtor"
// On the ESP8266 every "string" sits in RAM unless it is put in flash (PSTR / PROGMEM). FS("text") and
// pgmItem(list, k) copy flash text into a small ring of RAM buffers just long enough to draw it.
#define FS(s) fstr(PSTR(s))
#define NOINLINE __attribute__((noinline))   // keeps a helper's big locals off the caller's stack frame
#include <time.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_ST7789.h>

ADC_MODE(ADC_VCC);   // A0 reads the supply voltage for the title-bar battery meter

// PSION EPOC - COLOR EDITION (same engine + knob fix as psion-alpha-test-2, new system UI)
// + WiFi app, Settings app (3 themes + ~25 options, saved to flash), Notes app, shuffling music
// + office suite: Word, Sheet, Calc (scientific + graphs), Code (BASIC + mini C++), Files; game palettes
// + Clock with stopwatch / timer / alarm (ring in any app) and a Tools app with 12 little utilities
// + 8 screensavers (+ OLED savers), Music tracker app
// + Web app: weather (Open-Meteo), news, currency, ISS, IP location; via the PC relay (extras/start_relay.bat):
//   text browser with pictures, full-color screen view (Edge on the PC), Claude chat with web search
// + Word 95 + Excel 95 look-alikes, Calc constants tab, NetLab app (14 WiFi/network tests)
// 3.2: 160 MHz turbo; screen view: zoom on the fly, sideways scrolling, streamed live video (+ half-res "fast" mode),
//   typing with the keys on the OLED + suggestions (hold = pick from the list); PC remote (WiFi app): both screens
//   in a PC browser, mouse + keyboard drive the knob, file manager; Calc: saved + example equations and graph sets;
//   Code: 11 examples per language; MICOR RUN: a 3D tunnel run (needs relay v5 for the web parts)
// 3.3: mouse pointer in the screen view (knob: turn / push+turn / press = click; hover menus, click into fields);
//   PC remote: the PC's mouse points + clicks on the Psion (web pages, menus, home screen, Word + Excel);
//   Word + Excel open and save .docx / .xlsx (Word 2007-365), Word: strikethrough, highlight, clear formatting,
//   table of contents, table columns, 2560-char documents; Excel: dates, TODAY, MEDIAN, STDEV, VAR, replace,
//   remove duplicates, negatives in red; Web > Screen share: your PC's screen (duplicate / new screen, zoom)
//   (needs relay v7 for the web parts)
// 3.4: home screen styles (Psion ledger, Seven desktop + Start menu, Minimal, Micor's den, Phone grid); new pixel-art
//   icons; Office 365 look for Word + Excel (or classic 95); ~10x faster text (whole characters at once) and picture
//   rows straight into the SPI FIFO; Word scrolls without repainting; 3D graph cached + styles (mesh, solid, heat
//   map, heat + mesh), 2D styles (lines, dots, filled, thin); Micor Run gentler + lives; sleep (hold 2.5 s) +
//   always-on OLED; PC mouse in every list + games, wheel / move speed; live view of every app (games too);
//   web: sound on the piezo (Live + sound), YouTube plays, ads blocked, links only fetched when picking (relay v8);
//   optional backlight switch (TFT_BL_PIN = D0): sleep turns the backlight off, Settings > Screen brightness dims it;
//   the optional 2nd piezo (D0) is gone: one piezo on D6 plays everything; Settings > Volume (off, low, mid, high)
// 3.5: BOOKS app: an e-book reader for .txt / .md / Word .docx on the Psion (no PC needed), and with relay v9 the
//   PC's "Psion Books" folder (PDF, Word, Excel, PowerPoint with their pictures + charts) in the screen view;
//   pictures: .bmp on the Psion (PC remote turns uploaded .jpg/.png/.gif/.webp into screen-sized .bmp), any image on the PC
// ---------- optional: real time from the internet ----------
const char* WIFI_SSID = "";                // your WiFi name ("" = no WiFi)
const char* WIFI_PASS = "";
const long  TZ_OFFSET_SEC = -6L * 3600L;   // UTC-6 (central Mexico, no daylight saving)

// ---------- pins ----------
// TFT: SCL=D5 SDA=D7 CS=D8 DC=D3 RST->3V3 BL->3V3 | OLED: SDA=D2 SCL=D1
// Knob: CLK=D4 DT=RX SW=TX +->3V3 | Buzzer: D6 | TFT BL: 3V3, or D0 for a switchable backlight (TFT_BL_PIN below)
// Old wiring (DT=D0) still works if you set ENC_DT to 16, but D0 has no interrupt and no
// internal pull-up: it then needs a 10k resistor D0->3V3 and only counts on CLK edges.
#define TFT_CS      D8
#define TFT_DC      D3
#define ENC_CLK     D4          // GPIO2
#define ENC_DT      3           // RX = GPIO3 (interrupt + pull-up). 16 = D0 (old wiring)
#define ENC_HALF_STEP false     // true if you need TWO notches for one menu step
#define BTN_PIN     1           // TX = GPIO1
#define BUZZER_PIN  D6
#define BUZZER_PASSIVE true
// TFT backlight. -1: the TFT's BL wire goes to 3V3 (always lit; sleep can only blank the picture).
// D0: BL is switched by D0 (free since the knob's DT moved to RX): sleep turns the backlight really off, and
// Settings > Screen brightness dims it (PWM). D0 can only give a few mA: if your module's BL pin feeds the LEDs directly (it draws
// more than ~10 mA from 3V3), switch it with a transistor. A PNP / P-MOSFET high-side switch turns the light on
// when D0 is LOW: then also set TFT_BL_ACTIVE_LOW to true.
#define TFT_BL_PIN  D0           // -1 if your BL wire still goes straight to 3V3
#define TFT_BL_ACTIVE_LOW false  // false: D0 HIGH = light on (your build). true: only if the light turns on when D0 is LOW
#define OLED_ADDR   0x3C
static_assert(!(ENC_DT == 16 && TFT_BL_PIN == 16), "D0 can't be both the knob's DT (old wiring) and the backlight");
#define TFT_SPI_HZ  40000000UL      // lower (e.g. 27000000) if the screen shows glitches
#define CPU_TURBO   true            // run the ESP8266 at 160 MHz (twice as fast drawing, web pages and games). false = 80
// I2C is timed by busy loops built for the compile-time clock: at 160 MHz on an 80 MHz build they run twice as fast,
// so the OLED is asked for half the speed to really get its normal 400 kHz.
#define OLED_I2C_HZ ((CPU_TURBO && F_CPU == 80000000L) ? 200000UL : 400000UL)

// Game palettes: while a game runs, every colour it draws can be re-mapped (Settings > Game colours).
// 0 original, 1 90s LCD, 2 pea green, 3 grayscale, 4 sepia, 5 pastel, 6 night
uint16_t pk565(int r, int g, int b) {
  r = constrain(r, 0, 255); g = constrain(g, 0, 255); b = constrain(b, 0, 255);
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
uint16_t gamePalColor(uint8_t pal, uint16_t c) {
  int r = (c >> 8) & 0xF8, g = (c >> 3) & 0xFC, b = (c << 3) & 0xF8;
  int l = (r * 77 + g * 150 + b * 29) >> 8;
  static const uint8_t LCD4[4][3] = {{190, 200, 178}, {150, 162, 138}, {90, 104, 88}, {24, 30, 26}};
  static const uint8_t PEA4[4][3] = {{155, 188, 15}, {139, 172, 15}, {48, 98, 48}, {15, 56, 15}};
  switch (pal) {
    case 1: { const uint8_t* s = LCD4[l >> 6]; return pk565(s[0], s[1], s[2]); }    // dark -> light screen, bright -> ink
    case 2: { const uint8_t* s = PEA4[l >> 6]; return pk565(s[0], s[1], s[2]); }
    case 3: return pk565(l, l, l);
    case 4: return pk565(l * 240 / 255 + 30, l * 200 / 255 + 20, l * 150 / 255 + 10);
    case 5: return pk565((r + 250) / 2, (g + 240) / 2, (b + 250) / 2);
    case 6: return pk565(r * 60 / 100, g * 42 / 100, b * 30 / 100);
  }
  return c;
}
// ---- PC remote (screen mirror): every drawing call below also goes to the PC viewer, when one is connected ----
// The ESP8266 has no RAM for a copy of the screen, so the viewer gets the drawing calls themselves (filled boxes,
// pixels, text characters, picture rows) and redraws them in the browser. See "PC REMOTE" further down.
bool rcLive = false;                              // a viewer is watching (and this isn't a game, which isn't mirrored)
uint8_t rcMute = 0;                               // >0: inside a text character (sent as one character, not pixels)
bool rcMutePix = false;                           // live web video: the PC viewer gets a quarter of the pixels
int16_t rcPx = -1, rcPy = -1;                     // the PC's mouse on the Psion screen (screen pixels)
bool rcPtrMove = false, rcPtrClick = false;       // it moved / clicked (handled in loop(), never inside a wait)
void rcRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c);
void rcPixel(int16_t x, int16_t y, uint16_t c);
void rcChar(int16_t x, int16_t y, uint8_t ch, uint16_t fg, uint16_t bg, uint8_t size);
void rcWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
void rcPixels(const uint16_t* p, uint32_t n);
void rcOled(const uint8_t* buf);
bool rcViewer();
const uint8_t PSION_FONT[1280] PROGMEM = {          // Adafruit GFX's 5x7 font (glcdfont.c), CP437 order
  0x00,0x00,0x00,0x00,0x00,0x3E,0x5B,0x4F,0x5B,0x3E,0x3E,0x6B,0x4F,0x6B,0x3E,0x1C,0x3E,0x7C,0x3E,0x1C,0x18,0x3C,0x7E,0x3C,0x18,0x1C,0x57,0x7D,0x57,0x1C,0x1C,0x5E,0x7F,0x5E,0x1C,0x00,0x18,0x3C,0x18,0x00,
  0xFF,0xE7,0xC3,0xE7,0xFF,0x00,0x18,0x24,0x18,0x00,0xFF,0xE7,0xDB,0xE7,0xFF,0x30,0x48,0x3A,0x06,0x0E,0x26,0x29,0x79,0x29,0x26,0x40,0x7F,0x05,0x05,0x07,0x40,0x7F,0x05,0x25,0x3F,0x5A,0x3C,0xE7,0x3C,0x5A,
  0x7F,0x3E,0x1C,0x1C,0x08,0x08,0x1C,0x1C,0x3E,0x7F,0x14,0x22,0x7F,0x22,0x14,0x5F,0x5F,0x00,0x5F,0x5F,0x06,0x09,0x7F,0x01,0x7F,0x00,0x66,0x89,0x95,0x6A,0x60,0x60,0x60,0x60,0x60,0x94,0xA2,0xFF,0xA2,0x94,
  0x08,0x04,0x7E,0x04,0x08,0x10,0x20,0x7E,0x20,0x10,0x08,0x08,0x2A,0x1C,0x08,0x08,0x1C,0x2A,0x08,0x08,0x1E,0x10,0x10,0x10,0x10,0x0C,0x1E,0x0C,0x1E,0x0C,0x30,0x38,0x3E,0x38,0x30,0x06,0x0E,0x3E,0x0E,0x06,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x5F,0x00,0x00,0x00,0x07,0x00,0x07,0x00,0x14,0x7F,0x14,0x7F,0x14,0x24,0x2A,0x7F,0x2A,0x12,0x23,0x13,0x08,0x64,0x62,0x36,0x49,0x56,0x20,0x50,0x00,0x08,0x07,0x03,0x00,
  0x00,0x1C,0x22,0x41,0x00,0x00,0x41,0x22,0x1C,0x00,0x2A,0x1C,0x7F,0x1C,0x2A,0x08,0x08,0x3E,0x08,0x08,0x00,0x80,0x70,0x30,0x00,0x08,0x08,0x08,0x08,0x08,0x00,0x00,0x60,0x60,0x00,0x20,0x10,0x08,0x04,0x02,
  0x3E,0x51,0x49,0x45,0x3E,0x00,0x42,0x7F,0x40,0x00,0x72,0x49,0x49,0x49,0x46,0x21,0x41,0x49,0x4D,0x33,0x18,0x14,0x12,0x7F,0x10,0x27,0x45,0x45,0x45,0x39,0x3C,0x4A,0x49,0x49,0x31,0x41,0x21,0x11,0x09,0x07,
  0x36,0x49,0x49,0x49,0x36,0x46,0x49,0x49,0x29,0x1E,0x00,0x00,0x14,0x00,0x00,0x00,0x40,0x34,0x00,0x00,0x00,0x08,0x14,0x22,0x41,0x14,0x14,0x14,0x14,0x14,0x00,0x41,0x22,0x14,0x08,0x02,0x01,0x59,0x09,0x06,
  0x3E,0x41,0x5D,0x59,0x4E,0x7C,0x12,0x11,0x12,0x7C,0x7F,0x49,0x49,0x49,0x36,0x3E,0x41,0x41,0x41,0x22,0x7F,0x41,0x41,0x41,0x3E,0x7F,0x49,0x49,0x49,0x41,0x7F,0x09,0x09,0x09,0x01,0x3E,0x41,0x41,0x51,0x73,
  0x7F,0x08,0x08,0x08,0x7F,0x00,0x41,0x7F,0x41,0x00,0x20,0x40,0x41,0x3F,0x01,0x7F,0x08,0x14,0x22,0x41,0x7F,0x40,0x40,0x40,0x40,0x7F,0x02,0x1C,0x02,0x7F,0x7F,0x04,0x08,0x10,0x7F,0x3E,0x41,0x41,0x41,0x3E,
  0x7F,0x09,0x09,0x09,0x06,0x3E,0x41,0x51,0x21,0x5E,0x7F,0x09,0x19,0x29,0x46,0x26,0x49,0x49,0x49,0x32,0x03,0x01,0x7F,0x01,0x03,0x3F,0x40,0x40,0x40,0x3F,0x1F,0x20,0x40,0x20,0x1F,0x3F,0x40,0x38,0x40,0x3F,
  0x63,0x14,0x08,0x14,0x63,0x03,0x04,0x78,0x04,0x03,0x61,0x59,0x49,0x4D,0x43,0x00,0x7F,0x41,0x41,0x41,0x02,0x04,0x08,0x10,0x20,0x00,0x41,0x41,0x41,0x7F,0x04,0x02,0x01,0x02,0x04,0x40,0x40,0x40,0x40,0x40,
  0x00,0x03,0x07,0x08,0x00,0x20,0x54,0x54,0x78,0x40,0x7F,0x28,0x44,0x44,0x38,0x38,0x44,0x44,0x44,0x28,0x38,0x44,0x44,0x28,0x7F,0x38,0x54,0x54,0x54,0x18,0x00,0x08,0x7E,0x09,0x02,0x18,0xA4,0xA4,0x9C,0x78,
  0x7F,0x08,0x04,0x04,0x78,0x00,0x44,0x7D,0x40,0x00,0x20,0x40,0x40,0x3D,0x00,0x7F,0x10,0x28,0x44,0x00,0x00,0x41,0x7F,0x40,0x00,0x7C,0x04,0x78,0x04,0x78,0x7C,0x08,0x04,0x04,0x78,0x38,0x44,0x44,0x44,0x38,
  0xFC,0x18,0x24,0x24,0x18,0x18,0x24,0x24,0x18,0xFC,0x7C,0x08,0x04,0x04,0x08,0x48,0x54,0x54,0x54,0x24,0x04,0x04,0x3F,0x44,0x24,0x3C,0x40,0x40,0x20,0x7C,0x1C,0x20,0x40,0x20,0x1C,0x3C,0x40,0x30,0x40,0x3C,
  0x44,0x28,0x10,0x28,0x44,0x4C,0x90,0x90,0x90,0x7C,0x44,0x64,0x54,0x4C,0x44,0x00,0x08,0x36,0x41,0x00,0x00,0x00,0x77,0x00,0x00,0x00,0x41,0x36,0x08,0x00,0x02,0x01,0x02,0x04,0x02,0x3C,0x26,0x23,0x26,0x3C,
  0x1E,0xA1,0xA1,0x61,0x12,0x3A,0x40,0x40,0x20,0x7A,0x38,0x54,0x54,0x55,0x59,0x21,0x55,0x55,0x79,0x41,0x22,0x54,0x54,0x78,0x42,0x21,0x55,0x54,0x78,0x40,0x20,0x54,0x55,0x79,0x40,0x0C,0x1E,0x52,0x72,0x12,
  0x39,0x55,0x55,0x55,0x59,0x39,0x54,0x54,0x54,0x59,0x39,0x55,0x54,0x54,0x58,0x00,0x00,0x45,0x7C,0x41,0x00,0x02,0x45,0x7D,0x42,0x00,0x01,0x45,0x7C,0x40,0x7D,0x12,0x11,0x12,0x7D,0xF0,0x28,0x25,0x28,0xF0,
  0x7C,0x54,0x55,0x45,0x00,0x20,0x54,0x54,0x7C,0x54,0x7C,0x0A,0x09,0x7F,0x49,0x32,0x49,0x49,0x49,0x32,0x3A,0x44,0x44,0x44,0x3A,0x32,0x4A,0x48,0x48,0x30,0x3A,0x41,0x41,0x21,0x7A,0x3A,0x42,0x40,0x20,0x78,
  0x00,0x9D,0xA0,0xA0,0x7D,0x3D,0x42,0x42,0x42,0x3D,0x3D,0x40,0x40,0x40,0x3D,0x3C,0x24,0xFF,0x24,0x24,0x48,0x7E,0x49,0x43,0x66,0x2B,0x2F,0xFC,0x2F,0x2B,0xFF,0x09,0x29,0xF6,0x20,0xC0,0x88,0x7E,0x09,0x03,
  0x20,0x54,0x54,0x79,0x41,0x00,0x00,0x44,0x7D,0x41,0x30,0x48,0x48,0x4A,0x32,0x38,0x40,0x40,0x22,0x7A,0x00,0x7A,0x0A,0x0A,0x72,0x7D,0x0D,0x19,0x31,0x7D,0x26,0x29,0x29,0x2F,0x28,0x26,0x29,0x29,0x29,0x26,
  0x30,0x48,0x4D,0x40,0x20,0x38,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x38,0x2F,0x10,0xC8,0xAC,0xBA,0x2F,0x10,0x28,0x34,0xFA,0x00,0x00,0x7B,0x00,0x00,0x08,0x14,0x2A,0x14,0x22,0x22,0x14,0x2A,0x14,0x08,
  0x55,0x00,0x55,0x00,0x55,0xAA,0x55,0xAA,0x55,0xAA,0xFF,0x55,0xFF,0x55,0xFF,0x00,0x00,0x00,0xFF,0x00,0x10,0x10,0x10,0xFF,0x00,0x14,0x14,0x14,0xFF,0x00,0x10,0x10,0xFF,0x00,0xFF,0x10,0x10,0xF0,0x10,0xF0,
  0x14,0x14,0x14,0xFC,0x00,0x14,0x14,0xF7,0x00,0xFF,0x00,0x00,0xFF,0x00,0xFF,0x14,0x14,0xF4,0x04,0xFC,0x14,0x14,0x17,0x10,0x1F,0x10,0x10,0x1F,0x10,0x1F,0x14,0x14,0x14,0x1F,0x00,0x10,0x10,0x10,0xF0,0x00,
  0x00,0x00,0x00,0x1F,0x10,0x10,0x10,0x10,0x1F,0x10,0x10,0x10,0x10,0xF0,0x10,0x00,0x00,0x00,0xFF,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0xFF,0x10,0x00,0x00,0x00,0xFF,0x14,0x00,0x00,0xFF,0x00,0xFF,
  0x00,0x00,0x1F,0x10,0x17,0x00,0x00,0xFC,0x04,0xF4,0x14,0x14,0x17,0x10,0x17,0x14,0x14,0xF4,0x04,0xF4,0x00,0x00,0xFF,0x00,0xF7,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0xF7,0x00,0xF7,0x14,0x14,0x14,0x17,0x14,
  0x10,0x10,0x1F,0x10,0x1F,0x14,0x14,0x14,0xF4,0x14,0x10,0x10,0xF0,0x10,0xF0,0x00,0x00,0x1F,0x10,0x1F,0x00,0x00,0x00,0x1F,0x14,0x00,0x00,0x00,0xFC,0x14,0x00,0x00,0xF0,0x10,0xF0,0x10,0x10,0xFF,0x10,0xFF,
  0x14,0x14,0x14,0xFF,0x14,0x10,0x10,0x10,0x1F,0x00,0x00,0x00,0x00,0xF0,0x10,0xFF,0xFF,0xFF,0xFF,0xFF,0xF0,0xF0,0xF0,0xF0,0xF0,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0x0F,0x0F,0x0F,0x0F,0x0F,
  0x38,0x44,0x44,0x38,0x44,0xFC,0x4A,0x4A,0x4A,0x34,0x7E,0x02,0x02,0x06,0x06,0x02,0x7E,0x02,0x7E,0x02,0x63,0x55,0x49,0x41,0x63,0x38,0x44,0x44,0x3C,0x04,0x40,0x7E,0x20,0x1E,0x20,0x06,0x02,0x7E,0x02,0x02,
  0x99,0xA5,0xE7,0xA5,0x99,0x1C,0x2A,0x49,0x2A,0x1C,0x4C,0x72,0x01,0x72,0x4C,0x30,0x4A,0x4D,0x4D,0x30,0x30,0x48,0x78,0x48,0x30,0xBC,0x62,0x5A,0x46,0x3D,0x3E,0x49,0x49,0x49,0x00,0x7E,0x01,0x01,0x01,0x7E,
  0x2A,0x2A,0x2A,0x2A,0x2A,0x44,0x44,0x5F,0x44,0x44,0x40,0x51,0x4A,0x44,0x40,0x40,0x44,0x4A,0x51,0x40,0x00,0x00,0xFF,0x01,0x03,0xE0,0x80,0xFF,0x00,0x00,0x08,0x08,0x6B,0x6B,0x08,0x36,0x12,0x36,0x24,0x36,
  0x06,0x0F,0x09,0x0F,0x06,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x10,0x10,0x00,0x30,0x40,0xFF,0x01,0x01,0x00,0x1F,0x01,0x01,0x1E,0x00,0x19,0x1D,0x17,0x12,0x00,0x3C,0x3C,0x3C,0x3C,0x00,0x00,0x00,0x00,0x00
};
const uint16_t ICON16[22][16] PROGMEM = {      // the app icons, 16 x 16, bit 15 = left (scratchpad icons.py)
  {0x7FE0, 0x4030, 0x4028, 0x403C, 0x4004, 0x5014, 0x5014, 0x5114, 0x5394, 0x56D4, 0x5C74, 0x5834, 0x4004, 0x5FF4, 0x7FFC, 0x0000},   // word
  {0x0000, 0x7FFE, 0x7FFE, 0x4421, 0x4421, 0x7E7E, 0x4421, 0x4421, 0x7FFE, 0x4421, 0x4421, 0x7FFE, 0x4421, 0x4421, 0x7FFE, 0x0000},   // sheet
  {0x1FF8, 0x3FFC, 0x300C, 0x37EC, 0x300C, 0x3FFC, 0x36DC, 0x36DC, 0x3FFC, 0x36DC, 0x36DC, 0x3FFC, 0x36DC, 0x36DC, 0x3FFC, 0x1FF8},   // calc
  {0x2AA0, 0x7FF0, 0x4010, 0x5FD0, 0x4016, 0x5FDF, 0x401D, 0x5F9A, 0x4034, 0x5F68, 0x4050, 0x5E60, 0x4040, 0x4000, 0x7FF0, 0x0000},   // notes
  {0x0000, 0xFFFF, 0x8001, 0xFFFF, 0x8001, 0xA001, 0x9001, 0x8801, 0x8401, 0x8801, 0x9001, 0xA1F9, 0x8001, 0x8001, 0xFFFF, 0x0000},   // code
  {0x0000, 0x0000, 0x7C00, 0x4600, 0x43FE, 0x4002, 0x7FFE, 0x4002, 0x4002, 0x4002, 0x4002, 0x4002, 0x4002, 0x7FFE, 0x0000, 0x0000},   // files
  {0x0000, 0x0038, 0x0060, 0x00C6, 0x00CE, 0x00FC, 0x01F8, 0x03C0, 0x0780, 0x0F00, 0x1E00, 0x3C00, 0x7800, 0x7000, 0x2000, 0x0000},   // tools
  {0x0000, 0x03FE, 0x03FE, 0x0202, 0x0202, 0x0202, 0x0202, 0x0202, 0x0202, 0x0202, 0x1E1E, 0x3E3E, 0x7E7E, 0x7C7C, 0x3838, 0x0000},   // music
  {0x07E0, 0x1998, 0x2244, 0x4422, 0x4422, 0xFFFF, 0x8421, 0x8421, 0x8421, 0x8421, 0xFFFF, 0x4422, 0x4422, 0x2244, 0x1998, 0x07E0},   // web
  {0x0000, 0xC000, 0xC000, 0xC000, 0xC000, 0xC030, 0xC078, 0xC078, 0xC030, 0xC003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0000},   // pong
  {0x0000, 0x381C, 0x7C3E, 0x6C36, 0x7C3E, 0x3FFF, 0x7FFE, 0x7FFE, 0x6FF6, 0x700E, 0x3FFC, 0x3FFC, 0x67E6, 0xC003, 0x8001, 0x0000},   // frogger
  {0x0180, 0x0180, 0x03C0, 0x03C0, 0x0660, 0x47E2, 0x4FF2, 0x5FFA, 0x7FFE, 0x7E7E, 0x7C3E, 0x781E, 0x724E, 0x6246, 0x4242, 0x0000},   // galaga
  {0x0100, 0x0380, 0x07C0, 0x0FE0, 0x0384, 0x07CE, 0x0FEE, 0x1FF0, 0x07DE, 0x0FE8, 0x1FEE, 0x3FF0, 0x0108, 0x0108, 0x0108, 0x0000},   // forest
  {0x0000, 0x0000, 0x07E0, 0x0C30, 0x1818, 0x3FFC, 0x7FFE, 0xFFFF, 0xFFFF, 0xDC3B, 0xA815, 0x500A, 0x2004, 0x0000, 0xFFFF, 0x0000},   // race
  {0x07E0, 0x1818, 0x2104, 0x4102, 0x4102, 0x8101, 0x8101, 0x81E1, 0x8001, 0x8001, 0x8001, 0x4002, 0x4002, 0x2004, 0x1818, 0x07E0},   // clock
  {0x0000, 0x0006, 0x0006, 0x0066, 0x0066, 0x0066, 0x0666, 0x0666, 0x0666, 0x6666, 0x6666, 0x6666, 0x6666, 0x0000, 0xFFFF, 0x0000},   // stats
  {0x0000, 0x0000, 0x0FF0, 0x300C, 0x4002, 0x87E1, 0x1818, 0x2004, 0x07E0, 0x0810, 0x0000, 0x0180, 0x03C0, 0x0180, 0x0000, 0x0000},   // wifi
  {0x0240, 0x0E70, 0x0FF0, 0x07E0, 0x6FF6, 0x7E7E, 0xFC3F, 0x381C, 0x381C, 0xFC3F, 0x7E7E, 0x6FF6, 0x07E0, 0x0FF0, 0x0E70, 0x0240},   // settings
  {0x0000, 0x0000, 0x0000, 0x0800, 0x0800, 0x1400, 0x1420, 0x1430, 0xE257, 0x0248, 0x0240, 0x0180, 0x0000, 0x0000, 0x0000, 0x0000},   // netlab
  {0xFFFF, 0xC003, 0xA005, 0x9FF9, 0x9009, 0x9429, 0x9669, 0x97E9, 0x95A9, 0x97E9, 0x93C9, 0x9009, 0x9FF9, 0xA005, 0xC003, 0xFFFF},   // run
  {0x0000, 0x0000, 0x7C3E, 0x8241, 0xBA5D, 0x8241, 0xBA5D, 0x8241, 0xBA5D, 0x8241, 0xBA5D, 0x8241, 0xFDBF, 0x03C0, 0x0000, 0x0000},   // books
  {0x0000, 0x7C7C, 0x7C7C, 0x7C7C, 0x7C7C, 0x7C7C, 0x0000, 0x0000, 0x0000, 0x7C7C, 0x7C7C, 0x7C7C, 0x7C7C, 0x7C7C, 0x0000, 0x0000},   // extras
};
// Pixels straight into the hardware SPI FIFO, 32 at a time (64 bytes), already byte-swapped (the FIFO sends the low
// byte first). Call between startWrite() and endWrite(), right after setAddrWindow(). ~3x quicker than writePixels.
static inline void rawBits(uint32_t bits) {
  const uint32_t mask = ~((SPIMMOSI << SPILMOSI) | (SPIMMISO << SPILMISO));
  bits--;
  SPI1U1 = (SPI1U1 & mask) | (bits << SPILMOSI) | (bits << SPILMISO);
}
static inline void rawSend(const uint32_t* w) {     // returns at once: the hardware sends while we work
  while (SPI1CMD & SPIBUSY) {}
  volatile uint32_t* fifo = &SPI1W0;
  for (int i = 0; i < 16; i++) fifo[i] = w[i];
  SPI1CMD |= SPIBUSY;
}
void rawPixels(const uint16_t* p, int n) {           // n swapped pixels
  uint32_t blk[16];
  rawBits(512);
  while (n >= 32) { memcpy(blk, p, 64); rawSend(blk); p += 32; n -= 32; }
  if (n > 0) { while (SPI1CMD & SPIBUSY) {} rawBits(n * 16); memcpy(blk, p, n * 2); rawSend(blk); }
  while (SPI1CMD & SPIBUSY) {}
}
class PsionTFT : public Adafruit_ST7789 {
 public:
  uint8_t filter = 0;
  PsionTFT(int8_t cs, int8_t dc, int8_t rst) : Adafruit_ST7789(cs, dc, rst) {}
  uint16_t map(uint16_t c) { return filter ? gamePalColor(filter, c) : c; }
  void drawPixel(int16_t x, int16_t y, uint16_t c) override { c = map(c); Adafruit_ST7789::drawPixel(x, y, c); if (rcLive) rcPixel(x, y, c); }
  void writePixel(int16_t x, int16_t y, uint16_t c) override { c = map(c); Adafruit_ST7789::writePixel(x, y, c); if (rcLive) rcPixel(x, y, c); }
  void writeFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) override { c = map(c); Adafruit_ST7789::writeFillRect(x, y, w, h, c); if (rcLive) rcRect(x, y, w, h, c); }
  void writeFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) override { c = map(c); Adafruit_ST7789::writeFastHLine(x, y, w, c); if (rcLive) rcRect(x, y, w, 1, c); }
  void writeFastVLine(int16_t x, int16_t y, int16_t h, uint16_t c) override { c = map(c); Adafruit_ST7789::writeFastVLine(x, y, h, c); if (rcLive) rcRect(x, y, 1, h, c); }
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) override { c = map(c); Adafruit_ST7789::fillRect(x, y, w, h, c); if (rcLive) rcRect(x, y, w, h, c); }
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) override { c = map(c); Adafruit_ST7789::drawFastHLine(x, y, w, c); if (rcLive) rcRect(x, y, w, 1, c); }
  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t c) override { c = map(c); Adafruit_ST7789::drawFastVLine(x, y, h, c); if (rcLive) rcRect(x, y, 1, h, c); }
  void setAddrWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h) override { Adafruit_ST7789::setAddrWindow(x, y, w, h); if (rcLive) rcWindow(x, y, w, h); }
  void writePixels(uint16_t* colors, uint32_t len, bool block = true, bool bigEndian = false) {
    Adafruit_ST7789::writePixels(colors, len, block, bigEndian);
    if (rcLive && !rcMutePix) rcPixels(colors, len);
  }
  // Text. Adafruit draws a character as 48 separate one-pixel windows; here an opaque character is built in RAM and
  // sent as one window (~10x quicker: every menu, list and document scrolls faster). The PC viewer gets one
  // "character" message instead of 48 pixels (its page has the same 5x7 font).
  size_t write(uint8_t c) override {
    if (gfxFont) {                                   // (no custom fonts in this firmware, but keep them working)
      int16_t x = getCursorX(), y = getCursorY();
      rcMute++; size_t r = Adafruit_ST7789::write(c); rcMute--;
      if (rcLive && c != '\n' && c != '\r') rcChar(x, y, c, textcolor, textbgcolor, textsize_x);
      return r;
    }
    if (c == '\n') { cursor_x = 0; cursor_y += textsize_y * 8; return 1; }
    if (c == '\r') return 1;
    if (wrap && cursor_x + textsize_x * 6 > _width) { cursor_x = 0; cursor_y += textsize_y * 8; }
    drawChar(cursor_x, cursor_y, c, textcolor, textbgcolor, textsize_x);
    cursor_x += textsize_x * 6;
    return 1;
  }
  void fastChar(int16_t x, int16_t y, uint8_t c, uint16_t fg, uint16_t bg, uint8_t s) {
    static uint16_t px[12 * 16];
    int w = 6 * s, h = 8 * s;
    uint16_t F = (fg >> 8) | (fg << 8), B = (bg >> 8) | (bg << 8);   // big-endian on the wire
    const uint8_t* g = PSION_FONT + c * 5;
    for (int i = 0; i < 6; i++) {
      uint8_t line = i < 5 ? pgm_read_byte(g + i) : 0;
      for (int j = 0; j < 8; j++) {
        uint16_t v = ((line >> j) & 1) ? F : B;
        uint16_t* d = px + j * s * w + i * s;
        d[0] = v;
        if (s == 2) { d[1] = v; d[w] = v; d[w + 1] = v; }
      }
    }
    startWrite();
    Adafruit_ST7789::setAddrWindow(x, y, w, h);
    rawPixels(px, w * h);
    endWrite();
  }
  void drawChar(int16_t x, int16_t y, unsigned char c, uint16_t color, uint16_t bg, uint8_t size) {
    if (bg != color && size >= 1 && size <= 2 && x >= 0 && y >= 0 && x + 6 * size <= _width && y + 8 * size <= _height) {
      color = map(color); bg = map(bg);
      fastChar(x, y, c, color, bg, size);
      if (rcLive) rcChar(x, y, c, color, bg, size);
      return;
    }
    if (!rcLive) { Adafruit_ST7789::drawChar(x, y, c, color, bg, size); return; }
    rcMute++; Adafruit_ST7789::drawChar(x, y, c, color, bg, size); rcMute--;
    rcChar(x, y, c, color, bg, size);
  }
  void drawChar(int16_t x, int16_t y, unsigned char c, uint16_t color, uint16_t bg, uint8_t sx, uint8_t sy) {
    if (sx != sy) { Adafruit_ST7789::drawChar(x, y, c, color, bg, sx, sy); return; }
    drawChar(x, y, c, color, bg, sx);
  }
  void blitRows(const uint16_t* buf, int stride, int w, int h) {   // canvas rows -> screen (after setAddrWindow)
    uint16_t row[64];
    for (int r = 0; r < h; r++) {
      const uint16_t* s = buf + r * stride;
      if (!filter) { writePixels((uint16_t*)s, w); continue; }
      for (int i = 0; i < w && i < 64; i++) row[i] = map(s[i]);
      writePixels(row, w);
    }
  }
};
PsionTFT tft(TFT_CS, TFT_DC, -1);
class PsionOLED : public Adafruit_SSD1306 {         // the OLED is mirrored to the PC viewer too (1 KB a refresh)
 public:
  PsionOLED() : Adafruit_SSD1306(128, 64, &Wire, -1, OLED_I2C_HZ, OLED_I2C_HZ / 4) {}
  void display() { Adafruit_SSD1306::display(); if (rcViewer()) rcOled(getBuffer()); }
};
PsionOLED oled;
bool oledOk = false;

char* ringBuf() { static char b[6][64]; static uint8_t r = 0; r = (r + 1) % 6; return b[r]; }
const char* fstr(PGM_P p) { char* o = ringBuf(); strncpy_P(o, p, 63); o[63] = 0; return o; }
const char* pgmItem(PGM_P list, int k) {          // k-th line of a newline-separated flash list
  char* o = ringBuf();
  int n = 0;
  for (char c; (c = pgm_read_byte(list)); list++) {
    if (c == '\n') { if (k-- == 0) break; continue; }
    if (k == 0 && n < 63) o[n++] = c;
  }
  o[n] = 0;
  return o;
}

// ================= PALETTES =================
#define RGB565(r,g,b) ((((r)&0xF8)<<8)|(((g)&0xFC)<<3)|((b)>>3))
// ---- user preferences: changed in the Settings app, saved to flash (LittleFS /prefs.bin) ----
struct Prefs {
  uint8_t ver, theme, accentMode, zebra, invert, flip, splash, saver, panel;
  uint8_t homeFace, appFace, secHand, h12, dateFmt;
  int8_t tz;
  uint8_t sound, music, keyShift, tempo, click, clickPitch;
  uint8_t knobRev, hold, oledOn, contrast;
  char owner[17];
  uint8_t gamePal;                                // added in v4, appended so v3 files still load
  uint8_t alarmH, alarmM, alarmOn;                // v5: Clock > Alarm
  int16_t tally;                                  // v5: Tools > Tally counter
  uint8_t piezo2, saverStyle, oledSaver;          // v6: (unused: the 2nd piezo was removed in 3.4), screensaver styles
  char saverText[21];                             // v6: bouncy-text screensaver
  char webCity[24], relay[24];                    // v7: Web app (weather city, AI relay IP:port)
  uint8_t curBase;                                // v7: Web > Currency base
  uint8_t bootSpeed, bootSound, pz2Vol;           // v8: boot screen speed + chime, (unused: was the 2nd piezo's volume)
  uint8_t rcOn, svFast, runLevel;                 // v9: PC remote on, screen view fast video, Micor Run difficulty
  uint16_t rcPin, runBest;                        // v9: PC remote PIN, Micor Run best distance
  uint8_t deskStyle, deskIcons, officeLook;       // v10: home screen style, Seven: icons on the desktop / start menu only, Office look
  uint8_t mWheel, mMove;                          // v10: PC remote mouse: wheel + movement speed (0..4)
  uint8_t sleepAfter, sleepHold, aodOn, aodStyle, aodBright, aodDate, aodSecs;   // v10: sleep + always-on OLED
  uint8_t webSound, g3Style, g2Style;             // v10: web sound on the piezo, 3D / 2D graph look
  uint8_t tftBright;                              // v11: Screen brightness 0 full .. 4 night (needs TFT_BL_PIN)
  uint8_t volume;                                 // v12: 0 high, 1 mid, 2 low (off = P.sound 0)
  uint8_t bkFont, bkTheme;                        // v13: Books: text size (0 small, 1 large), colours (paper, sepia, night)
};
const uint8_t PREFS_VER = 13;
const char FW_VERSION[] PROGMEM = "3.5";       // shown on the boot screen and in Settings
Prefs P;
bool fsOk = false;
int notesSaved = 0;
bool ringOn = false;                              // timer / alarm is ringing (Clock app)
char lastCrash[48] = "";                          // why the last restart happened, if it was a crash
void prefsDefaults() {
  memset(&P, 0, sizeof P);
  P.ver = PREFS_VER; P.zebra = 1; P.splash = 1; P.secHand = 1; P.tz = (int8_t)(TZ_OFFSET_SEC / 3600L);
  P.sound = 1; P.music = 1; P.keyShift = 1; P.tempo = 1; P.click = 1; P.clickPitch = 1;
  P.hold = 1; P.oledOn = 1; P.contrast = 2; P.alarmH = 7; P.alarmM = 30;
  strcpy_P(P.saverText, PSTR("Psion :3"));
  strcpy_P(P.relay, PSTR("192.168.1.100:8787"));
  P.bootSpeed = 1; P.bootSound = 1;
  P.runLevel = 1; P.svFast = 1;
  P.mWheel = 2; P.mMove = 2; P.sleepHold = 1; P.aodOn = 1; P.aodBright = 1; P.aodDate = 1;
}
// ---------- the TFT backlight: switchable only when its BL wire goes to a pin (TFT_BL_PIN) ----------
// Full and off are plain HIGH / LOW (no timer interrupts); the levels in between are 1 kHz PWM. The backlight's
// power follows the duty: "3 medium" uses about a third of full.
const uint8_t BL_DUTY[5] = {255, 150, 80, 35, 12};  // 5 full, 4 bright, 3 medium, 2 dim, 1 night
const uint8_t BL_PIN = (uint8_t)(TFT_BL_PIN < 0 ? 0 : TFT_BL_PIN);
void blSet(bool on) {
  if (TFT_BL_PIN < 0) return;
  int v = on ? BL_DUTY[P.tftBright % 5] : 0;
  analogWrite(BL_PIN, TFT_BL_ACTIVE_LOW ? 255 - v : v);
}
void prefsSave() {
  if (!fsOk) return;
  File f = LittleFS.open(FS("/prefs.bin"), FS("w"));
  if (f) { f.write((const uint8_t*)&P, sizeof P); f.close(); }
}
void prefsLoad() {
  prefsDefaults();
  if (!fsOk) return;
  File f = LittleFS.open(FS("/prefs.bin"), FS("r"));
  if (!f) return;
  Prefs t;
  size_t sz = f.size();
  t = P;
  if (sz >= offsetof(Prefs, gamePal) && sz <= sizeof t && (size_t)f.read((uint8_t*)&t, sz) == sz && t.ver >= 3 && t.ver <= PREFS_VER) { P = t; P.ver = PREFS_VER; }
  f.close();
  P.owner[16] = 0;
}
uint16_t holdMs() { const uint16_t v[3] = {400, 600, 900}; return v[P.hold % 3]; }
enum { DS_LEDGER, DS_SEVEN, DS_MINIMAL, DS_MICOR, DS_GRID, DS_N };     // home screen styles (Settings > Look)
const char DESK_NAMES_P[] PROGMEM = "Psion ledger\nSeven desktop\nMinimal\nMicor's den\nPhone grid";
bool deskStart = false;                              // Seven: the Start menu is open
int deskStartTop = 0;
int deskPage = -1;                                   // the page of icons on screen (-1 = draw it all again)
enum { AOD_BIG, AOD_ANALOG, AOD_MICOR, AOD_TINY, AOD_N };                // always-on OLED styles
const char AOD_NAMES_P[] PROGMEM = "big clock\nanalog clock\nMicor + clock\ntiny + drifting";

// ---- themes: every system colour below is a variable that applyTheme() fills in ----
enum { TH_PLATINUM, TH_LCD, TH_DARK };
uint16_t C_LCD, C_INK, C_GHOST, C_MID;                           // base, text, unlit segments, muted text
uint16_t E_WHITE, E_ROW2, E_HEAD, E_RULE, E_DOCK, E_DIMBAR;      // panels, zebra row, headers, hairlines, dock, dim icons
uint16_t E_BAR, E_BARTXT, E_ONACC;                               // title bar, its text, text on an accent block
const uint16_t THEMES[3][13] = {
  // base                   text                    ghost                   muted
  // panel                  row2                    head                    rule                    dock
  // dimbar                 bar                     bar text                on accent
  {RGB565(240, 242, 245), RGB565( 33,  37,  41), RGB565(226, 230, 235), RGB565(118, 126, 138),
   RGB565(255, 255, 255), RGB565(246, 247, 249), RGB565(226, 230, 236), RGB565(196, 202, 210), RGB565(218, 223, 230),
   RGB565( 80,  86,  94), RGB565( 33,  37,  41), RGB565(255, 255, 255), RGB565(255, 255, 255)},          // platinum
  {RGB565(190, 200, 178), RGB565( 24,  30,  26), RGB565(172, 182, 160), RGB565(100, 112,  98),
   RGB565(200, 209, 188), RGB565(190, 200, 178), RGB565(176, 188, 164), RGB565(130, 142, 122), RGB565(178, 190, 166),
   RGB565(120, 132, 112), RGB565( 24,  30,  26), RGB565(190, 200, 178), RGB565(190, 200, 178)},          // 90s LCD
  {RGB565( 18,  20,  24), RGB565(228, 232, 236), RGB565( 40,  44,  51), RGB565(140, 148, 160),
   RGB565( 30,  33,  39), RGB565( 37,  41,  48), RGB565( 46,  51,  59), RGB565( 66,  72,  82), RGB565( 26,  29,  34),
   RGB565( 70,  76,  86), RGB565(  8,   9,  12), RGB565(240, 242, 245), RGB565(255, 255, 255)}           // dark
};
const uint16_t E_MINT   = RGB565( 62, 196, 132);
const uint16_t E_AMBER  = RGB565(245, 166,  35);
const uint16_t E_CORAL  = RGB565(232,  76,  61);
const uint16_t K_INK    = RGB565( 24,  30,  26);                  // game sprite ink = the original C_INK
// one accent per app id: MENU, WORD, SHEET, CALC, NOTES, CODE, FILES, TOOLS, MUSIC, WEB, PONG, FROGGER, GALAGA, FOREST, RACE,
// CLOCK, STATS, WIFI, SETTINGS, NETLAB, RUN, BOOKS
const uint16_t APP_ACCENT_BASE[22] = {
  RGB565( 74, 105, 189), RGB565( 43,  87, 154), RGB565( 33, 115,  70), RGB565( 88,  96, 112),
  RGB565(214,  72, 140), RGB565(120,  80, 200), RGB565(176, 124,  52), RGB565(200,  45,  50), RGB565(236,  64, 122), RGB565( 88, 101, 242), RGB565(  0, 150, 199),
  RGB565(130, 180,  30), RGB565(205,  55,  85), RGB565( 38, 166,  91), RGB565(232, 108,  32),
  RGB565(196, 145,  10), RGB565(106,  76, 219), RGB565(  0, 137, 123), RGB565( 84, 110, 122), RGB565(  3, 155, 229),
  RGB565(126,  87, 255), RGB565(166,  90,  48)
};
void applyTheme() {
  const uint16_t* t = THEMES[P.theme % 3];
  C_LCD = t[0]; C_INK = t[1]; C_GHOST = t[2]; C_MID = t[3];
  E_WHITE = t[4]; E_ROW2 = P.zebra ? t[5] : t[4]; E_HEAD = t[6]; E_RULE = t[7]; E_DOCK = t[8];
  E_DIMBAR = t[9]; E_BAR = t[10]; E_BARTXT = t[11]; E_ONACC = t[12];
}
uint16_t appAccent(int a) {
  if (a < 0 || a > 21) a = 0;
  if (P.theme == TH_LCD) return C_INK;                            // the 90s screen only has one ink
  return P.accentMode ? APP_ACCENT_BASE[0] : APP_ACCENT_BASE[a];
}
const int TITLE_H = 18, DOCK_Y = 214, DOCK_TAB = 40, DOCK_X0 = 0;
const uint16_t K_BG      = RGB565( 24,  14,  44);
const uint16_t K_DIM     = RGB565( 80,  60, 110);
const uint16_t K_ORANGE  = RGB565(255, 140,  30);
const uint16_t K_CREAM   = RGB565(255, 246, 232);
const uint16_t K_PINK    = RGB565(255, 150, 185);
const uint16_t K_DPINK   = RGB565(205,  85, 110);
const uint16_t K_CYAN    = RGB565( 80, 220, 230);
const uint16_t K_YELLOW  = RGB565(255, 230,  80);
const uint16_t K_RED     = RGB565(230,  50,  60);
const uint16_t K_SPACE   = RGB565(  6,   4,  24);
const uint16_t K_WATER   = RGB565( 20,  60, 150);
const uint16_t K_GRASS   = RGB565( 40, 130,  60);
const uint16_t K_ROAD    = RGB565( 48,  48,  60);
const uint16_t K_BANK    = RGB565( 24,  90,  40);
const uint16_t K_FROG    = RGB565(120, 230,  60);
const uint16_t K_LOG     = RGB565(130,  80,  40);
const uint16_t K_LOGD    = RGB565( 90,  55,  25);
const uint16_t K_TURTLE  = RGB565( 50, 170,  90);
const uint16_t K_TURTLED = RGB565( 30, 110,  60);
const uint16_t K_TRAIL   = RGB565(200, 205, 210);
const uint16_t K_WINDOW  = RGB565( 20,  30,  50);

// ================= SHELL GLOBALS =================
const int W = 320, H = 240;
int ENC_DIR = 1;                             // -1 = reversed (Settings > Knob direction)
const uint8_t FRAME_INTERVAL_MS = 16;

enum { APP_MENU, APP_WORD, APP_SHEET, APP_CALC, APP_NOTES, APP_CODE, APP_FILES, APP_TOOLS, APP_MUSIC, APP_WEB,
       APP_PONG, APP_FROGGER, APP_GALAGA, APP_FOREST, APP_RACE, APP_CLOCK, APP_STATS, APP_WIFI, APP_SETTINGS, APP_NETLAB,
       APP_RUN, APP_BOOKS };
enum { SET_DIFFICULTY, SET_SPEED, SET_START, SET_MODE, SET_TRACK };
bool gameApp(int a) { return (a >= APP_PONG && a <= APP_RACE) || a == APP_RUN; }   // (the PC view of a game isn't repaired)
int  app = APP_MENU;
// The PC's mouse in any list: each list's row drawing says where its rows are (ptrListSet); a click on a row picks it,
// a click on the picked row opens it. kind says which list, so a list that closed isn't clicked by mistake.
enum { PL_NONE, PL_FILES, PL_PICKER, PL_SETTINGS, PL_NOTES, PL_WIFI, PL_WIFISCAN, PL_TOOLS, PL_WEB, PL_NETLAB, PL_CALCLOG, PL_BOOKS };
struct { int16_t x, y, w, rh, top, sel; uint8_t vis, kind; int8_t app; } ptrL = {0, 0, 0, 1, 0, 0, 0, PL_NONE, -1};
void ptrListSet(uint8_t kind, int x, int y, int w, int rh, int vis, int top, int sel) {
  ptrL.kind = kind; ptrL.app = app; ptrL.x = x; ptrL.y = y; ptrL.w = w; ptrL.rh = rh; ptrL.vis = vis; ptrL.top = top; ptrL.sel = sel;
}
int  menuSel = 0;
const char* mood = ":3";
bool oledDirty = true;
unsigned long lastOled = 0;
const char APP_NAMES_P[] PROGMEM = "WORD\nSHEET\nCALC\nNOTES\nCODE\nFILES\nTOOLS\nMUSIC\nWEB\nPONG\nFROGGER\nGALAGA\nFOREST\nRACE\nCLOCK\nSTATS\nWIFI\nSETTINGS\nNETLAB\nMICOR RUN\nBOOKS";
const char* appName(int i) { return pgmItem(APP_NAMES_P, i); }
const char APP_DESC_P[] PROGMEM = "documents + charts\ncells, formulas, charts\nscientific + graphs\njot ideas with the knob\nBASIC + mini C++\neverything in flash\n12 handy little tools\nmake tunes on the piezo\nnews, weather, AI + more\nfox vs robo-kitty\nhop across the road\nfox vs the mice\nmicor's forest quest\nzoom with furry friends\nbig clock + set time\ncpu / ram graphs\nnetworks + password\nthemes, sound + more\nWiFi + network tests\n3D space tunnel run\ne-books: txt, docx, PDF";
uint8_t gameDifficulty[16] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
uint8_t gameSpeed[16] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
bool gameSetupActive = false;
int gameSetupSel = SET_DIFFICULTY;
int gameSetupApp = APP_PONG;
uint8_t raceMode = 0, raceTrack = 0;
float gameSpeedFactor(int a) { const float v[3] = {0.75f, 1.0f, 1.3f}; return v[gameSpeed[a] % 3]; }
float gameDifficultyFactor(int a) { const float v[3] = {0.8f, 1.0f, 1.25f}; return v[gameDifficulty[a] % 3]; }

// ================= INPUT (knob by interrupt) =================
// state = CLK<<1 | DT. A KY-040 rests at state 3 (both high) and goes through 4 states per notch.
// With both pins on interrupts we use the full quadrature table (contact bounce cancels itself:
// +1 -1 +1 ...) and only count when the knob is back at rest. With DT on D0 (no interrupt) only
// CLK edges are seen, so DT is read at each CLK edge to get the direction instead.
volatile int32_t encCount = 0;
volatile int8_t encSub = 0;
volatile uint8_t encPrev = 3;
const bool ENC_DT_IRQ = ENC_DT < 16;
const int8_t ENC_TABLE[16] = {0, 1, -1, 0, -1, 0, 0, 1, 1, 0, 0, -1, 0, -1, 1, 0};
void IRAM_ATTR encISR() {
  uint8_t clk = (GPI >> ENC_CLK) & 1;
  uint8_t dt = (ENC_DT == 16) ? (GP16I & 1) : ((GPI >> ENC_DT) & 1);
  uint8_t s = (clk << 1) | dt;
  if (ENC_DT_IRQ) {
    encSub += ENC_TABLE[(encPrev << 2) | s];
    encPrev = s;
    if (s != 3 && !(ENC_HALF_STEP && s == 0)) return;
  } else {
    if (clk == (encPrev >> 1)) return;           // bounce that came back to the same level
    encPrev = s;
    encSub += (clk == dt) ? 1 : -1;
    if (!clk && !ENC_HALF_STEP) return;
  }
  int8_t need = (ENC_DT_IRQ && !ENC_HALF_STEP) ? 2 : 1;
  if (encSub >= need) encCount++;
  else if (encSub <= -need) encCount--;
  encSub = 0;
}
int takeClicks() {
  noInterrupts();
  int32_t clicks = encCount;
  encCount = 0;
  interrupts();
  return (int)clicks;
}

bool fPress = false, fShort = false, fLong = false, fSleep = false;   // fSleep: held 2.5 s
bool takePress() { bool r = fPress; fPress = false; return r; }
bool takeShort() { bool r = fShort; fShort = false; return r; }

// btnDown: the knob is pushed in right now. btnUsed: this push already did something (a hold, or push + turn in
// the web's mouse pointer mode), so letting go is not a click.
bool btnDown = false, btnUsed = false;
void updateButton() {
  static bool last = true;
  static unsigned long tDown = 0, tChange = 0;
  unsigned long t = millis();
  static bool sleepSent = false;
  bool now = digitalRead(BTN_PIN);
  if (now != last && t - tChange > 30) {
    tChange = t; last = now;
    if (!now) { tDown = t; btnUsed = false; sleepSent = false; fPress = true; }
    else if (!btnUsed) fShort = true;
  }
  btnDown = !last;
  if (!last && !btnUsed && t - tDown >= holdMs()) { btnUsed = true; fLong = true; }
  if (!last && !sleepSent && P.sleepHold && t - tDown >= 2500) { sleepSent = true; fSleep = true; }   // like a power button
}

// ================= SOUND (non-blocking) =================
uint16_t T_TICK[]         = {2400,3};   // key click (pitch: Settings > Click pitch)
const uint16_t T_START[] PROGMEM = {784,60, 988,60, 1175,140};
const uint16_t T_HIT[] PROGMEM = {1046,35};
const uint16_t T_WALL[] PROGMEM = {523,25};
const uint16_t T_YOU[] PROGMEM = {659,70, 784,70, 1046,150};
const uint16_t T_CPU[] PROGMEM = {392,110, 330,110, 262,220};
const uint16_t T_WIN[] PROGMEM = {523,110, 659,110, 784,110, 1046,260, 0,60, 1046,110, 1175,320};
const uint16_t T_LOSE[] PROGMEM = {392,220, 370,220, 349,220, 330,520};
const uint16_t T_HOP[] PROGMEM = {880,25};
const uint16_t T_SPLAT[] PROGMEM = {220,90, 160,130};
const uint16_t T_SPLASH[] PROGMEM = {330,60, 250,80, 180,120};
const uint16_t T_HOME[] PROGMEM = {784,60, 988,60, 1175,120};
const uint16_t T_SHOT[] PROGMEM = {1500,14};
const uint16_t T_BOOM[] PROGMEM = {300,40, 200,60, 130,90};
const uint16_t T_WAVE[] PROGMEM = {659,80, 784,80, 988,80, 1319,160};
#define SFX(t) playTune(t, sizeof(t) / sizeof(t[0]) / 2)

// ---- music: note names (Hz). Prefixed N_ because D0..D8 are pin names on this board ----
#define N_R   0
#define N_E4  330
#define N_F4  349
#define N_FS4 370
#define N_G4  392
#define N_GS4 415
#define N_A4  440
#define N_AS4 466
#define N_B4  494
#define N_C5  523
#define N_CS5 554
#define N_D5  587
#define N_DS5 622
#define N_E5  659
#define N_F5  698
#define N_FS5 740
#define N_G5  784
#define N_GS5 831
#define N_A5  880
#define N_B5  988
#define N_C6  1047
#define N_CS6 1109
#define N_D6  1175

// Every game has a pool of short phrases: {note, length in 16th-note ticks}. The sequencer comes
// back to phrase 0 (the hook) every 4 phrases, picks the others at random in between and changes
// key every 8 phrases, so the loop rarely sounds the same twice. Originals unless marked (PD) =
// public-domain classical piece.
// PONG: bouncy C major
const uint16_t PONG_0[] PROGMEM = {N_C5,2, N_E5,2, N_G5,2, N_E5,2, N_A5,2, N_G5,2, N_E5,4, N_D5,2, N_F5,2, N_A5,2, N_F5,2, N_G5,4, N_R,4};
const uint16_t PONG_1[] PROGMEM = {N_G4,2, N_C5,2, N_E5,2, N_G5,2, N_F5,2, N_E5,2, N_D5,2, N_C5,2, N_E5,2, N_D5,2, N_C5,2, N_A4,2, N_B4,4, N_G4,4};
const uint16_t PONG_2[] PROGMEM = {N_C6,1, N_R,1, N_G5,1, N_R,1, N_E5,1, N_R,1, N_C5,2, N_R,2, N_C5,1, N_D5,1, N_E5,1, N_F5,1, N_G5,4, N_R,2, N_G5,2, N_A5,2, N_B5,2, N_C6,6};
const uint16_t PONG_3[] PROGMEM = {N_B4,1, N_A4,1, N_GS4,1, N_A4,1, N_C5,4, N_D5,1, N_C5,1, N_B4,1, N_C5,1, N_E5,4,      // Mozart, Rondo alla Turca (PD)
                           N_F5,1, N_E5,1, N_DS5,1, N_E5,1, N_B5,1, N_A5,1, N_GS5,1, N_A5,1, N_B5,1, N_A5,1, N_GS5,1, N_A5,1, N_C6,4, N_A5,2, N_C6,2};
const uint16_t PONG_4[] PROGMEM = {N_A4,2, N_C5,2, N_E5,2, N_A5,2, N_G5,2, N_E5,2, N_C5,2, N_E5,2, N_F5,2, N_A5,2, N_G5,2, N_F5,2, N_E5,4, N_C5,4};
// FROGGER: hoppy G major
const uint16_t FROG_0[] PROGMEM = {N_G4,2, N_R,1, N_G4,1, N_B4,2, N_D5,2, N_G5,4, N_D5,2, N_B4,2, N_C5,2, N_E5,2, N_G5,2, N_E5,2, N_D5,4, N_R,4};
const uint16_t FROG_1[] PROGMEM = {N_D5,1, N_E5,1, N_D5,1, N_B4,1, N_G4,2, N_A4,2, N_B4,2, N_C5,2, N_D5,4, N_E5,2, N_C5,2, N_A4,2, N_FS4,2, N_G4,6, N_R,2};
const uint16_t FROG_2[] PROGMEM = {N_C5,4, N_D5,1, N_F5,1, N_E5,1, N_D5,1, N_G5,2, N_G5,2, N_G5,2, N_A5,1, N_E5,1,          // Offenbach, Galop / can-can (PD)
                           N_F5,2, N_D5,2, N_D5,2, N_F5,1, N_E5,1, N_D5,1, N_C5,1, N_C6,2, N_B5,2, N_A5,2, N_G5,2, N_F5,2, N_E5,2, N_D5,2, N_C5,2};
const uint16_t FROG_3[] PROGMEM = {N_G4,1, N_B4,1, N_D5,1, N_G5,1, N_A4,1, N_C5,1, N_E5,1, N_A5,1, N_B4,1, N_D5,1, N_FS5,1, N_B5,1, N_C5,1, N_E5,1, N_G5,1, N_C6,1,
                           N_D6,4, N_B5,2, N_G5,2, N_D5,4, N_G5,4};
// GALAGA: driving A minor
const uint16_t GALA_0[] PROGMEM = {N_A4,1, N_A4,1, N_E5,2, N_A4,1, N_A4,1, N_F5,2, N_A4,1, N_A4,1, N_E5,2, N_D5,2, N_C5,2,
                           N_A4,1, N_A4,1, N_E5,2, N_A4,1, N_A4,1, N_G5,2, N_F5,2, N_E5,2, N_D5,4};
const uint16_t GALA_1[] PROGMEM = {N_A4,2, N_B4,2, N_C5,2, N_D5,2, N_E5,2, N_C5,2, N_E5,4, N_DS5,2, N_B4,2, N_DS5,4, N_D5,2, N_AS4,2, N_D5,4,   // Grieg, Mountain King (PD)
                           N_A4,2, N_B4,2, N_C5,2, N_D5,2, N_E5,2, N_C5,2, N_E5,2, N_A5,2, N_G5,2, N_E5,2, N_C5,2, N_E5,2, N_G5,8};
const uint16_t GALA_2[] PROGMEM = {N_E5,1, N_D5,1, N_C5,1, N_B4,1, N_A4,2, N_E4,2, N_A4,2, N_C5,2, N_B4,2, N_G4,2, N_F4,2, N_A4,2, N_C5,2, N_F5,2, N_E5,4, N_GS4,4};
const uint16_t GALA_3[] PROGMEM = {N_A5,2, N_E5,2, N_A5,2, N_E5,2, N_G5,2, N_D5,2, N_G5,2, N_D5,2, N_F5,2, N_C5,2, N_F5,2, N_C5,2, N_E5,4, N_R,4};
// FOREST: calm D major pentatonic adventure
const uint16_t FORE_0[] PROGMEM = {N_D5,4, N_E5,2, N_FS5,2, N_A5,4, N_FS5,4, N_G5,2, N_FS5,2, N_E5,2, N_D5,2, N_E5,8};
const uint16_t FORE_1[] PROGMEM = {N_A4,2, N_D5,2, N_FS5,2, N_A5,2, N_B5,4, N_A5,4, N_FS5,2, N_E5,2, N_D5,2, N_B4,2, N_D5,8};
const uint16_t FORE_2[] PROGMEM = {N_B4,2, N_D5,2, N_E5,2, N_FS5,2, N_E5,2, N_D5,2, N_B4,4, N_A4,2, N_B4,2, N_D5,4, N_E5,4, N_R,4};
const uint16_t FORE_3[] PROGMEM = {N_FS5,1, N_A5,1, N_D6,2, N_A5,1, N_FS5,1, N_D5,2, N_E5,1, N_G5,1, N_B5,2, N_G5,1, N_E5,1, N_CS5,2, N_D5,8, N_R,8};
// RALLY: fast and silly, plus three public-domain classics
const uint16_t RACE_0[] PROGMEM = {N_E5,2, N_E5,1, N_R,1, N_E5,2, N_G5,2, N_A5,2, N_G5,2, N_E5,2, N_D5,2, N_C5,2, N_D5,2, N_E5,2, N_G5,2, N_A5,4, N_B5,2, N_C6,2};
const uint16_t RACE_1[] PROGMEM = {N_D5,3, N_A4,3, N_B4,3, N_FS4,3, N_G4,3, N_D5,3, N_G4,3, N_A4,3};                                    // Pachelbel, Canon (PD)
const uint16_t RACE_2[] PROGMEM = {N_E5,2, N_E5,2, N_F5,2, N_G5,2, N_G5,2, N_F5,2, N_E5,2, N_D5,2, N_C5,2, N_C5,2, N_D5,2, N_E5,2, N_E5,3, N_D5,1, N_D5,4};   // Beethoven, Ode to Joy (PD)
const uint16_t RACE_3[] PROGMEM = {N_G4,1, N_G4,1, N_G4,2, N_G4,1, N_G4,1, N_G4,2, N_G4,1, N_G4,1, N_C5,2, N_D5,2, N_E5,2,              // Rossini, William Tell galop (PD)
                           N_G4,1, N_G4,1, N_G4,2, N_G4,1, N_G4,1, N_G4,2, N_G4,1, N_G4,1, N_C5,2, N_E5,2, N_D5,2};
const uint16_t RACE_4[] PROGMEM = {N_C5,1, N_D5,1, N_E5,1, N_G5,1, N_A5,1, N_G5,1, N_E5,1, N_D5,1, N_C5,1, N_D5,1, N_E5,1, N_G5,1, N_A5,1, N_C6,1, N_A5,1, N_G5,1,
                           N_C6,4, N_G5,4, N_A5,2, N_B5,2, N_C6,4};

struct Phrase { const uint16_t* n; uint8_t len; };
struct Song { const Phrase* ph; uint8_t count; uint8_t tickMs; };
#define PH(a) {a, (uint8_t)(sizeof(a) / sizeof(a[0]) / 2)}
const Phrase PONG_PH[] = {PH(PONG_0), PH(PONG_1), PH(PONG_2), PH(PONG_3), PH(PONG_4)};
const Phrase FROG_PH[] = {PH(FROG_0), PH(FROG_1), PH(FROG_2), PH(FROG_3)};
const Phrase GALA_PH[] = {PH(GALA_0), PH(GALA_1), PH(GALA_2), PH(GALA_3)};
const Phrase FORE_PH[] = {PH(FORE_0), PH(FORE_1), PH(FORE_2), PH(FORE_3)};
const Phrase RACE_PH[] = {PH(RACE_0), PH(RACE_1), PH(RACE_2), PH(RACE_3), PH(RACE_4)};
const Song SONGS[5] = {{PONG_PH, 5, 62}, {FROG_PH, 4, 66}, {GALA_PH, 4, 55}, {FORE_PH, 4, 90}, {RACE_PH, 5, 52}};
// MICOR RUN: a quick E-minor space chase (original)
const uint16_t RUN_0[] PROGMEM = {N_E5,1, N_R,1, N_E5,1, N_G5,1, N_B5,2, N_A5,1, N_G5,1, N_FS5,2, N_E5,2, N_D5,1, N_E5,1, N_FS5,2, N_B4,4};
const uint16_t RUN_1[] PROGMEM = {N_B4,1, N_E5,1, N_G5,1, N_B5,1, N_C6,2, N_B5,2, N_A5,1, N_G5,1, N_A5,2, N_FS5,4, N_R,2};
const uint16_t RUN_2[] PROGMEM = {N_G5,1, N_FS5,1, N_E5,1, N_D5,1, N_E5,2, N_B4,2, N_C5,1, N_D5,1, N_E5,2, N_G5,2, N_FS5,4};
const uint16_t RUN_3[] PROGMEM = {N_E5,2, N_B5,2, N_A5,1, N_B5,1, N_G5,2, N_FS5,1, N_G5,1, N_E5,2, N_D5,2, N_E5,4, N_R,2};
const Phrase RUN_PH[] = {PH(RUN_0), PH(RUN_1), PH(RUN_2), PH(RUN_3)};
const Song RUN_SONG = {RUN_PH, 4, 58};
const uint16_t KEY_SHIFT[5] = {1000, 1122, 1335, 891, 1189};    // 0, +2, +5, -2, +3 semitones (x1000)

const uint8_t TEMPO_PCT[3] = {130, 100, 78};   // Settings > Music tempo
const uint16_t* curTune = nullptr;
uint8_t tuneLen = 0, tuneIdx = 0;
const Song* musicSong = nullptr;
const uint16_t* musicTune = nullptr;
uint8_t musicLen = 0, musicIdx = 0, musicPhrase = 0, musicStep = 0;
uint16_t musicRatio = 1000;
int musicApp = APP_MENU;
bool sfxActive = false, musicGap = false;
unsigned long noteEnd = 0, gapAt = 0, sfxEnd = 0;

// One piezo (D6) plays everything: a sound effect takes over from the game music for a moment, and the Music app
// shares it between lead, bass and drums with a fast chiptune arpeggio.
// Volume = how long each pulse pushes the piezo: high 50% of the cycle, mid 18%, low 6%. Off = Sound off
// (alarms still ring, at high).
const uint8_t VOL_DUTY[3] = {50, 18, 6};
void soundOn(uint16_t f) {
  if (!P.sound && !ringOn) return;                 // alarms ring even with sound off
  if (!BUZZER_PASSIVE) { digitalWrite(BUZZER_PIN, HIGH); return; }
  if (!f) { soundOff(); return; }
  uint8_t duty = (ringOn || !P.sound) ? 50 : VOL_DUTY[P.volume % 3];
  uint32_t period = 1000000UL / constrain((int)f, 30, 15000);   // microseconds
  uint32_t hi = max(period * duty / 100, (uint32_t)4);
  startWaveform(BUZZER_PIN, hi, period - hi, 0);
}
void soundOff() {
  if (BUZZER_PASSIVE) stopWaveform(BUZZER_PIN);
  digitalWrite(BUZZER_PIN, LOW);
}
void soundAllOff() { soundOff(); }                  // (everything: tunes, effects, the Music app)
void stopSound() {
  curTune = nullptr; tuneLen = 0; sfxActive = false;
  musicSong = nullptr; musicTune = nullptr; musicLen = 0; musicApp = APP_MENU;
  soundAllOff();
}
void startNote() {
  uint16_t f = pgm_read_word(curTune + tuneIdx * 2), ms = pgm_read_word(curTune + tuneIdx * 2 + 1);
  if (f) soundOn(f); else soundOff();
  sfxEnd = millis() + ms;
}
void startMusicNote() {
  uint16_t f = pgm_read_word(musicTune + musicIdx * 2);
  uint16_t ms = pgm_read_word(musicTune + musicIdx * 2 + 1) * musicSong->tickMs * TEMPO_PCT[P.tempo % 3] / 100;
  if (f) soundOn((uint32_t)f * musicRatio / 1000);
  else soundOff();
  noteEnd = millis() + ms;
  gapAt = noteEnd - (ms > 120 ? 24 : ms / 5);      // tiny breath so repeated notes stay bouncy
  musicGap = false;
}
void loadPhrase(uint8_t p) {
  musicPhrase = p; musicIdx = 0;
  musicTune = musicSong->ph[p].n; musicLen = musicSong->ph[p].len;
}
void nextPhrase() {
  musicStep++;
  uint8_t n = musicSong->count, p = 0;
  if (musicStep % 4) do p = random(n); while (p == musicPhrase || p == 0);
  if (P.keyShift && musicStep % 8 == 0) musicRatio = KEY_SHIFT[random(5)];
  loadPhrase(p);
}
void startAppMusic(int a) {
  musicApp = a;
  musicSong = (P.music && a >= APP_PONG && a <= APP_RACE) ? &SONGS[a - APP_PONG] : (P.music && a == APP_RUN ? &RUN_SONG : nullptr);
  musicStep = 0; musicRatio = 1000;
  if (musicSong) { loadPhrase(0); startMusicNote(); }
  else { musicTune = nullptr; musicLen = 0; soundOff(); }
}
void playTune(const uint16_t* t, uint8_t n) {
  curTune = t; tuneLen = n; tuneIdx = 0; sfxActive = true; startNote();
}
void updateSound() {
  unsigned long now = millis();
  if (sfxActive && (long)(now - sfxEnd) >= 0) {
    if (++tuneIdx < tuneLen) startNote();
    else {
      sfxActive = false; curTune = nullptr;
      if (musicTune && musicLen) startMusicNote();
      else soundOff();
    }
  }
  if (sfxActive) return;                           // the music waits for the effect
  if (!musicTune || !musicLen) return;
  if (!musicGap && (long)(now - gapAt) >= 0) { soundOff(); musicGap = true; }
  if ((long)(now - noteEnd) >= 0) {
    if (++musicIdx >= musicLen) nextPhrase();
    startMusicNote();
  }
}

void uiClick() { if (P.click) SFX(T_TICK); }

// ================= CLOCK (software, no RTC chip) =================
uint32_t clkEpoch = 0, clkMs = 0;
int lastDig[6] = {-1,-1,-1,-1,-1,-1};
int lastSec = -1, lastDay = -1, lastTitleMin = -1;
bool ntpStarted = false, ntpDone = false;
const char* WD[7]  = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
const char MN_P[] PROGMEM = "JAN\nFEB\nMAR\nAPR\nMAY\nJUN\nJUL\nAUG\nSEP\nOCT\nNOV\nDEC";

void forceClockRedraw() {
  for (int i = 0; i < 6; i++) lastDig[i] = -1;
  lastSec = -1; lastDay = -1; lastTitleMin = -1;
}
uint32_t daysFromCivil(int y, int m, int d) {
  y -= (m <= 2);
  int era = y / 400, yoe = y - era * 400, mp = (m + 9) % 12;
  int doy = (153 * mp + 2) / 5 + d - 1;
  int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (uint32_t)(era * 146097 + doe - 719468);
}
void civilFromDays(uint32_t z, int* y, int* m, int* d) {
  int32_t zz = (int32_t)z + 719468;
  int32_t era = zz / 146097, doe = zz - era * 146097;
  int32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int32_t yy = yoe + era * 400;
  int32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  int32_t mp = (5 * doy + 2) / 153;
  *d = doy - (153 * mp + 2) / 5 + 1;
  *m = mp < 10 ? mp + 3 : mp - 9;
  *y = yy + (*m <= 2);
}
void clockInitFromCompile() {
  const char* d = __DATE__;
  const char* t = __TIME__;
  char mons[37]; strcpy_P(mons, PSTR("JanFebMarAprMayJunJulAugSepOctNovDec"));
  char mm[4] = {d[0], d[1], d[2], 0};
  const char* p = strstr(mons, mm);
  int mon = p ? (int)(p - mons) / 3 + 1 : 1;
  clkEpoch = daysFromCivil(atoi(d + 7), mon, atoi(d + 4)) * 86400UL
           + atoi(t) * 3600UL + atoi(t + 3) * 60UL + atoi(t + 6);
  clkMs = millis();
}
void tickClock() {
  uint32_t el = millis() - clkMs;
  if (el >= 1000) { uint32_t s = el / 1000; clkEpoch += s; clkMs += s * 1000; }
}
void getHMS(int* h, int* m, int* s) {
  uint32_t sod = clkEpoch % 86400UL;
  *h = sod / 3600; *m = (sod / 60) % 60; *s = sod % 60;
}
void ntpTick() {
  if (ntpDone) return;
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck < 1000) return;
  lastCheck = millis();
  if (!ntpStarted && WiFi.status() == WL_CONNECTED) { configTime(0, 0, "pool.ntp.org", "time.nist.gov"); ntpStarted = true; }
  if (ntpStarted) {
    time_t t = time(nullptr);
    if (t > 1700000000L) {
      clkEpoch = (uint32_t)((long)t + P.tz * 3600L);
      clkMs = millis(); ntpDone = true; forceClockRedraw();
    }
  }
}

// ================= SYSTEM STATS =================
const int HN = 120;
uint8_t histCpu[HN], histRam[HN];
int hHead = 0;
uint32_t busyUs = 0, loopCount = 0, winStartUs = 0, freeHeapNow = 0;
int cpuLoad = 0, ramUsed = 0, loopHz = 0;
bool statsFresh = false;

void statsWindow() {
  uint32_t nowUs = micros(), el = nowUs - winStartUs;
  if (el < 500000UL) return;
  uint32_t pct = (uint32_t)((uint64_t)busyUs * 100ULL / el);
  cpuLoad = pct > 100 ? 100 : (int)pct;
  loopHz = (int)((uint64_t)loopCount * 1000000ULL / el);
  freeHeapNow = ESP.getFreeHeap();
  ramUsed = (int)(100UL - (freeHeapNow * 100UL) / 81920UL);
  histCpu[hHead] = cpuLoad; histRam[hHead] = ramUsed;
  hHead = (hHead + 1) % HN;
  busyUs = 0; loopCount = 0; winStartUs = nowUs; statsFresh = true;
}

// ================= DRAW HELPERS =================
void putText(int x, int y, int size, uint16_t fg, uint16_t bg, const char* s) {
  tft.setTextSize(size); tft.setTextColor(fg, bg); tft.setCursor(x, y); tft.print(s);
}
void centerText(const char* s, int y, int size, uint16_t fg, uint16_t bg) {
  putText((W - (int)strlen(s) * 6 * size) / 2, y, size, fg, bg, s);
}
void msgBox(const char* msg, uint16_t col, uint16_t bg) {
  int w = strlen(msg) * 18, x = (W - w) / 2 - 10;
  tft.fillRect(x, 94, w + 20, 40, bg);
  tft.drawRect(x, 94, w + 20, 40, col);
  centerText(msg, 102, 3, col, bg);
}
void drawPaw(int x, int y, int u, uint16_t c) {
  int tr = (3 * u) / 2;
  tft.fillCircle(x, y + 2 * u, 3 * u, c);
  tft.fillCircle(x - 19 * u / 4, y + u / 4, tr, c);
  tft.fillCircle(x - 2 * u, y - 5 * u / 2, tr, c);
  tft.fillCircle(x + 2 * u, y - 5 * u / 2, tr, c);
  tft.fillCircle(x + 19 * u / 4, y + u / 4, tr, c);
}
void drawBar(int x, int y, int w, int h, int pct) {
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  int fill = (w - 2) * pct / 100;
  tft.drawRect(x, y, w, h, C_INK);
  tft.fillRect(x + 1, y + 1, fill, h - 2, C_INK);
  tft.fillRect(x + 1 + fill, y + 1, w - 2 - fill, h - 2, C_LCD);
}

const uint8_t SEG7[10] = {0x3F,0x06,0x5B,0x4F,0x66,0x6D,0x7D,0x07,0x7F,0x6F};
void drawSeg7(int x, int y, int w, int h, int t, int d, uint16_t on, uint16_t off) {
  uint8_t m = SEG7[d];
  int ym = y + (h - t) / 2, yb = y + h - t;
  int hl = w - 2 * t - 2, vu = ym - (y + t) - 2, vl = yb - (ym + t) - 2;
  tft.fillRect(x + t + 1, y,          hl, t,  (m & 0x01) ? on : off);
  tft.fillRect(x + w - t, y + t + 1,  t,  vu, (m & 0x02) ? on : off);
  tft.fillRect(x + w - t, ym + t + 1, t,  vl, (m & 0x04) ? on : off);
  tft.fillRect(x + t + 1, yb,         hl, t,  (m & 0x08) ? on : off);
  tft.fillRect(x,         ym + t + 1, t,  vl, (m & 0x10) ? on : off);
  tft.fillRect(x,         y + t + 1,  t,  vu, (m & 0x20) ? on : off);
  tft.fillRect(x + t + 1, ym,         hl, t,  (m & 0x40) ? on : off);
}
void drawBigTime(int x0, int y0, int dw, int dh, int dt, int h, int m, int s, bool force) {
  int dg[6] = {h / 10, h % 10, m / 10, m % 10, s / 10, s % 10};
  int g = dw / 7, d2 = x0 + dw + g, cx = d2 + dw + g, cw = dt + 2 * g;
  int d3 = cx + cw, d4 = d3 + dw + g, sx = d4 + dw + 2 * g;
  int sdw = dw / 2, sdh = dh / 2, sdt = (dt / 2 < 2) ? 2 : dt / 2, sg = g / 2 + 1;
  int xs[6] = {x0, d2, d3, d4, sx, sx + sdw + sg};
  for (int i = 0; i < 6; i++) {
    if (!force && dg[i] == lastDig[i]) continue;
    lastDig[i] = dg[i];
    if (i < 4) drawSeg7(xs[i], y0, dw, dh, dt, dg[i], C_INK, C_GHOST);
    else       drawSeg7(xs[i], y0 + dh - sdh, sdw, sdh, sdt, dg[i], C_INK, C_GHOST);
  }
  if (force || s != lastSec) {
    lastSec = s;
    uint16_t c = (s % 2 == 0) ? C_INK : C_GHOST;
    tft.fillRect(cx + g, y0 + dh / 3 - dt / 2, dt, dt, c);
    tft.fillRect(cx + g, y0 + 2 * dh / 3 - dt / 2, dt, dt, c);
  }
}
int dispHour(int h) { if (!P.h12) return h; h %= 12; return h ? h : 12; }
const char* ampm(int h) { return h < 12 ? "AM" : "PM"; }
void fmtDate(char* b, size_t n, int day) {
  int yr, mo, d;
  civilFromDays((uint32_t)day, &yr, &mo, &d);
  yr %= 10000; mo &= 15; d &= 31;
  const char* wd = WD[(day + 4) % 7];
  if (P.dateFmt == 1)      snprintf_P(b, n, PSTR("%s %02d/%02d/%d"), wd, mo, d, yr);
  else if (P.dateFmt == 2) snprintf_P(b, n, PSTR("%d-%02d-%02d"), yr, mo, d);
  else                     snprintf_P(b, n, PSTR("%s %02d %s %d"), wd, d, pgmItem(MN_P, (mo + 11) % 12), yr);
}
void drawDate(int y, int size, bool center, int xLeft, bool force) {
  int day = (int)(clkEpoch / 86400UL);
  if (!force && day == lastDay) return;
  lastDay = day;
  char b[32];
  fmtDate(b, sizeof b, day);
  if (center) { tft.fillRect(0, y, W, 8 * size, C_LCD); centerText(b, y, size, C_INK, C_LCD); }
  else putText(xLeft, y, size, C_INK, C_LCD, b);
}
// ---------- EPOC chrome: title bar + flat status icons ----------
int vccPct() { return constrain(((int)ESP.getVcc() - 2900) * 100 / 400, 0, 100); }   // supply voltage 2.9-3.3V
void drawBattery(int x, int y) {
  int p = vccPct(), f = 16 * p / 100;
  uint16_t c = p > 50 ? E_MINT : (p > 20 ? E_AMBER : E_CORAL);
  tft.drawRect(x, y, 20, 10, E_BARTXT);
  tft.fillRect(x + 20, y + 3, 2, 4, E_BARTXT);
  tft.fillRect(x + 2, y + 2, f, 6, c);
  tft.fillRect(x + 2 + f, y + 2, 16 - f, 6, E_BAR);
}
void drawWifi(int x, int y) {
  int bars = 0;
  if (WiFi.status() == WL_CONNECTED) { long r = WiFi.RSSI(); bars = r > -55 ? 4 : (r > -65 ? 3 : (r > -75 ? 2 : 1)); }
  for (int i = 0; i < 4; i++) { int h = 3 + i * 2; tft.fillRect(x + i * 4, y + 10 - h, 3, h, i < bars ? E_BARTXT : E_DIMBAR); }
}
void drawTitleTime() {
  int h, m, s; getHMS(&h, &m, &s);
  char b[8]; snprintf_P(b, sizeof b, PSTR("%02d:%02d"), dispHour(h) & 31, m & 63);
  putText(240, 2, 2, E_BARTXT, E_BAR, b);
  putText(302, 6, 1, E_BARTXT, E_BAR, P.h12 ? ampm(h) : FS("  "));
  drawWifi(194, 4); drawBattery(212, 4);
  lastTitleMin = m;
}
void titleTimeTick() { int h, m, s; getHMS(&h, &m, &s); if (m != lastTitleMin) drawTitleTime(); }
void drawTitleAccent(uint16_t accent) { tft.fillRect(0, 0, 4, TITLE_H, accent); }
void drawTitleBar(const char* name, uint16_t accent) {
  tft.fillRect(0, 0, W, TITLE_H, E_BAR);
  drawTitleAccent(accent);
  int len = strlen(name);                           // the name must end before the spinner/WiFi slot at x = 168
  if (len <= 13) putText(10, 2, 2, E_BARTXT, E_BAR, name);
  else { char b[28]; snprintf_P(b, sizeof b, len > 26 ? PSTR("%.24s..") : PSTR("%s"), name); putText(10, 6, 1, E_BARTXT, E_BAR, b); }
  drawTitleTime();
}
void wrapText(const char* s, int x, int y, int maxChars, int maxLines, uint16_t fg, uint16_t bg) {
  char line[24];
  for (int ln = 0; ln < maxLines && *s; ln++) {
    while (*s == ' ') s++;
    int n = 0, cut = -1;
    while (s[n] && n < maxChars) { if (s[n] == ' ') cut = n; n++; }
    int len = (!s[n] || s[n] == ' ') ? n : (cut > 0 ? cut : n);
    memcpy(line, s, len); line[len] = 0;
    putText(x + (maxChars - len) * 3, y + ln * 10, 1, fg, bg, line);
    s += len;
  }
}
void drawGauge(int x, int y, int w, int pct, uint16_t c) {
  pct = constrain(pct, 0, 100);
  int f = (w - 2) * pct / 100;
  tft.drawRect(x, y, w, 7, E_RULE);
  tft.fillRect(x + 1, y + 1, f, 5, c);
  tft.fillRect(x + 1 + f, y + 1, w - 2 - f, 5, E_ROW2);
}
uint16_t loadColor(int pct) { return pct < 60 ? E_MINT : (pct < 85 ? E_AMBER : E_CORAL); }

// ---------- persistent app dock (bottom 26px): Psion-style shortcuts, everything else under Extras ----------
const uint8_t DOCK_APPS[7] = {APP_WORD, APP_SHEET, APP_CALC, APP_NOTES, APP_CODE, APP_FILES, APP_CLOCK};
const char DOCK_NAMES_P[] PROGMEM = "Word\nSheet\nCalc\nNotes\nCode\nFiles\nClock\nExtras";
int dockIndexOf(int a) { for (int i = 0; i < 7; i++) if (DOCK_APPS[i] == a) return i; return 7; }
// The app icons (ICON16, pixel art): s = 1 in lists and the dock, 2 or 3 on the desktops. Runs of ink are drawn as
// whole rectangles, so even the big ones are quick.
void drawIcon16(int i, int x, int y, int s, uint16_t c) {   // i = app id - 1, 21 = Extras
  if (i < 0 || i > 20) i = 20;
  for (int r = 0; r < 16; r++) {
    uint16_t bits = pgm_read_word(&ICON16[i][r]);
    for (int k = 0; k < 16; ) {
      if (!(bits & (0x8000 >> k))) { k++; continue; }
      int st = k;
      while (k < 16 && (bits & (0x8000 >> k))) k++;
      tft.fillRect(x + st * s, y + r * s, (k - st) * s, s, c);
    }
  }
}
void drawMiniGlyph(int i, int cx, int cy, uint16_t c, uint16_t bg) {   // i = app id - 1 (99 = Extras)
  (void)bg;
  drawIcon16(i >= 0 && i < 21 ? i : 21, cx - 8, cy - 8, 1, c);
}
void drawDockTab(int i, bool sel) {
  int x = DOCK_X0 + i * DOCK_TAB, a = i < 7 ? (int)DOCK_APPS[i] : (int)APP_MENU;
  uint16_t acc = appAccent(a), bg = sel ? acc : E_DOCK, fg = sel ? E_ONACC : C_INK;
  tft.fillRect(x, DOCK_Y - 3, DOCK_TAB, 3, sel ? acc : C_LCD);        // selected tab pulls up a lip
  tft.fillRect(x, DOCK_Y, DOCK_TAB, 1, sel ? acc : E_RULE);
  tft.fillRect(x, DOCK_Y + 1, DOCK_TAB, H - DOCK_Y - 1, bg);
  if (!sel) tft.drawFastVLine(x + DOCK_TAB - 1, DOCK_Y + 4, H - DOCK_Y - 8, E_RULE);
  drawMiniGlyph(i < 7 ? a - 1 : 99, x + DOCK_TAB / 2, DOCK_Y + 9, fg, bg);
  const char* dn = pgmItem(DOCK_NAMES_P, i);
  putText(x + (DOCK_TAB - (int)strlen(dn) * 6) / 2, DOCK_Y + 17, 1, fg, bg, dn);
}
void drawDock(int selApp) {                       // apps that aren't on the dock light up "Extras"
  tft.fillRect(0, DOCK_Y, W, H - DOCK_Y, E_DOCK);
  tft.drawFastHLine(0, DOCK_Y, W, E_RULE);
  int s = dockIndexOf(selApp);
  for (int i = 0; i < 8; i++) drawDockTab(i, i == s);
}

// ---------- knob keyboard (WiFi password, owner name, notes) ----------
// 40 character keys on 3 pages + a row of action keys. Turn = move, press = type.
// Spanish letters are the font's own CP437 codes: \xA4 n~  \xA5 N~  \xA0 a'  \x82 e'  \xA1 i'
// \xA2 o'  \xA3 u'  \x81 u:  \xA8 upside-down ?  \xAD upside-down !
const char* const KB_PAGES[3] = {
  "abcdefghijklmnopqrstuvwxyz0123456789-" "\xA4" ".@",
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ!#$%&*+=" "\xA5" "/:;~^",
  "()[]{}<>'\",|\\`" "\xA0\x82\xA1\xA2\xA3\x81\xA4\xA5\xA8\xAD" "!?@#$%&*-_+=/.:;"
};
const char* const KB_PAGE_NAME[3] = {"ABC", "#\xA4\xA8", "abc"};    // label = the page you switch TO
// WiFi wants UTF-8, the font uses CP437: convert the Spanish letters before sending a password
void cp437ToUtf8(const char* in, char* out, size_t n) {
  const uint8_t CP[10] = {0xA0, 0x82, 0xA1, 0xA2, 0xA3, 0x81, 0xA4, 0xA5, 0xA8, 0xAD};
  const uint8_t UC[10] = {0xE1, 0xE9, 0xED, 0xF3, 0xFA, 0xFC, 0xF1, 0xD1, 0xBF, 0xA1};   // Latin-1 code points
  size_t o = 0;
  for (; *in && o + 3 < n; in++) {
    uint8_t c = (uint8_t)*in;
    for (int k = 0; k < 10; k++) if (c == CP[k]) c = UC[k];
    if (c < 0x80) out[o++] = (char)c;
    else { out[o++] = (char)(0xC0 | (c >> 6)); out[o++] = (char)(0x80 | (c & 0x3F)); }
  }
  out[o] = 0;
}
struct KbLayout { int y, h; uint8_t n; const uint8_t* act; };
enum { KB_NONE, KB_TYPED, KB_PAGE, KB_SPACE, KB_DEL, KB_ENTER, KB_OK, KB_CANCEL, KB_MOVE, KB_VIEW, KB_MENU };
const uint8_t KB_ACT_FORM[5] = {KB_PAGE, KB_SPACE, KB_DEL, KB_OK, KB_CANCEL};
const uint8_t KB_ACT_NOTE[6] = {KB_PAGE, KB_SPACE, KB_DEL, KB_ENTER, KB_OK, KB_CANCEL};
const KbLayout KB_FORM = {66, 22, 5, KB_ACT_FORM};
const KbLayout KB_NOTE = {92, 18, 6, KB_ACT_NOTE};
const uint8_t KB_ACT_WORD[6] = {KB_PAGE, KB_SPACE, KB_DEL, KB_ENTER, KB_MOVE, KB_VIEW};
const uint8_t KB_ACT_CODE[6] = {KB_PAGE, KB_SPACE, KB_DEL, KB_ENTER, KB_MOVE, KB_MENU};
const KbLayout KB_WORD = {98, 18, 6, KB_ACT_WORD};
const KbLayout KB_CODE = {98, 18, 6, KB_ACT_CODE};
const KbLayout* kbL = &KB_FORM;
int kbKey = 0, kbPage = 0;
uint16_t kbAccent = 0;
char kbChar = 0;
int kbCount() { return 40 + kbL->n; }
void kbRect(int i, int* x, int* y, int* w, int* h) {
  if (i < 40) { *x = 5 + (i % 10) * 31; *y = kbL->y + (i / 10) * (kbL->h + 2); *w = 29; *h = kbL->h; }
  else { int aw = 310 / kbL->n; *x = 5 + (i - 40) * aw; *y = kbL->y + 4 * (kbL->h + 2); *w = aw - 2; *h = kbL->h + 2; }
}
const char* kbActLabel(uint8_t a) {
  switch (a) {
    case KB_PAGE:  return KB_PAGE_NAME[kbPage];
    case KB_SPACE: return "space";
    case KB_DEL:   return "del";
    case KB_ENTER: return "enter";
    case KB_OK:    return "OK";
    case KB_MOVE:  return "move";
    case KB_VIEW:  return "view";
    case KB_MENU:  return "menu";
    default:       return "cancel";
  }
}
void kbDrawKey(int i) {
  int x, y, w, h;
  kbRect(i, &x, &y, &w, &h);
  bool sel = i == kbKey;
  uint16_t bg = sel ? kbAccent : (i < 40 ? E_WHITE : E_HEAD), fg = sel ? E_ONACC : C_INK;
  tft.fillRect(x, y, w, h, bg);
  tft.drawRect(x, y, w, h, sel ? kbAccent : E_RULE);
  if (i < 40) {
    char c[2] = {KB_PAGES[kbPage][i], 0};
    putText(x + (w - 12) / 2, y + (h - 16) / 2 + 1, 2, fg, bg, c);
  } else {
    uint8_t a = kbL->act[i - 40];
    const char* l = kbActLabel(a);
    uint16_t c = (!sel && a == KB_OK) ? E_MINT : ((!sel && a == KB_CANCEL) ? E_CORAL : fg);
    putText(x + (w - (int)strlen(l) * 6) / 2, y + (h - 8) / 2, 1, c, bg, l);
  }
}
void kbDrawAll() { for (int i = 0; i < kbCount(); i++) kbDrawKey(i); }
enum { KB_LAYOUT_FORM, KB_LAYOUT_NOTE, KB_LAYOUT_WORD, KB_LAYOUT_CODE };
void kbBegin(int layout, uint16_t acc) {
  kbL = layout == KB_LAYOUT_NOTE ? &KB_NOTE : layout == KB_LAYOUT_WORD ? &KB_WORD : layout == KB_LAYOUT_CODE ? &KB_CODE : &KB_FORM;
  kbAccent = acc; kbKey = 0; kbPage = 0; }
// Keys typed on the PC (PC remote) wait here until a keyboard on the Psion takes them ('\b' = delete, '\n' = enter/OK,
// 27 = cancel). Whatever nobody takes within 3 s is dropped, so typing with no text box open does nothing.
char kbRq[48];
uint8_t kbRqHead = 0, kbRqTail = 0;
uint32_t kbRqAt = 0, kbLastTick = 0;
void kbRemotePush(char c) {
  uint8_t nx = (kbRqHead + 1) % sizeof kbRq;
  if (nx == kbRqTail) return;                     // full
  kbRq[kbRqHead] = c; kbRqHead = nx; kbRqAt = millis();
}
char kbRemotePop() {
  if (kbRqHead == kbRqTail) return 0;
  if (millis() - kbRqAt > 3000) { kbRqTail = kbRqHead; return 0; }
  char c = kbRq[kbRqTail]; kbRqTail = (kbRqTail + 1) % sizeof kbRq;
  return c;
}
bool kbHasAct(uint8_t a) { for (int i = 0; i < kbL->n; i++) if (kbL->act[i] == a) return true; return false; }
int kbTick(int clicks, bool press) {             // KB_NONE, KB_TYPED (char in kbChar) or the action key
  kbLastTick = millis();
  if (clicks) {
    int old = kbKey, n = kbCount();
    kbKey = ((kbKey + clicks) % n + n) % n;
    kbDrawKey(old); kbDrawKey(kbKey); uiClick();
  }
  if (!press) {
    char rc = kbRemotePop();
    if (!rc) return KB_NONE;
    if (rc == '\b') return KB_DEL;
    if (rc == '\n') return kbHasAct(KB_ENTER) ? KB_ENTER : KB_OK;
    if (rc == 27) return kbHasAct(KB_CANCEL) ? KB_CANCEL : KB_NONE;
    if (rc == ' ') return KB_SPACE;
    kbChar = rc;
    return KB_TYPED;
  }
  SFX(T_HOP);
  if (kbKey < 40) { kbChar = KB_PAGES[kbPage][kbKey]; return KB_TYPED; }
  uint8_t a = kbL->act[kbKey - 40];
  if (a == KB_PAGE) { kbPage = (kbPage + 1) % 3; for (int i = 0; i < 40; i++) kbDrawKey(i); kbDrawKey(kbKey); }
  return a;
}

// ================= MENU (EPOC desktop) =================
// Ledger of apps on the left (6 rows visible + scroll bar), System panel on the right, dock at the bottom.
const int MENU_N = 21, MENU_VIS = 6, LEDGER_X = 4, LEDGER_Y = 22, LEDGER_W = 202, LEDGER_ROW = 24, LEDGER_HEAD = 14;
const int SYS_X = 210, SYS_W = 106;
int menuTop = 0;

void drawIconGlyph(int i, int x, int y, uint16_t fg, uint16_t bg) {
  const uint16_t mid = C_MID;
  switch (i) {
    case 0:   // pong: fox paddle vs robo-kitty paddle, and a ball of yarn
      for (int k = 0; k < 4; k++) tft.fillRect(x + 31, y + 5 + k * 13, 2, 6, mid);
      tft.fillRoundRect(x + 7, y + 16, 8, 30, 3, fg);
      tft.fillTriangle(x + 7, y + 18, x + 7, y + 9, x + 11, y + 16, fg);
      tft.fillTriangle(x + 15, y + 18, x + 15, y + 9, x + 11, y + 16, fg);
      tft.fillRect(x + 9, y + 21, 1, 2, bg); tft.fillRect(x + 12, y + 21, 1, 2, bg);
      tft.fillRoundRect(x + 49, y + 12, 8, 30, 3, fg);
      tft.fillTriangle(x + 49, y + 14, x + 49, y + 5, x + 53, y + 12, fg);
      tft.fillTriangle(x + 57, y + 14, x + 57, y + 5, x + 53, y + 12, fg);
      tft.fillRect(x + 50, y + 18, 6, 2, bg);
      tft.fillCircle(x + 32, y + 30, 8, fg);
      tft.drawLine(x + 26, y + 27, x + 37, y + 35, bg);
      tft.drawLine(x + 27, y + 24, x + 38, y + 31, bg);
      tft.drawLine(x + 28, y + 36, x + 35, y + 23, bg);
      tft.drawLine(x + 25, y + 36, x + 20, y + 48, fg);
      tft.drawLine(x + 20, y + 48, x + 26, y + 51, fg);
      break;
    case 1:   // frogger: a frog with kitty ears going :3
      tft.fillTriangle(x + 12, y + 16, x + 13, y + 4, x + 20, y + 11, fg);
      tft.fillTriangle(x + 52, y + 16, x + 51, y + 4, x + 44, y + 11, fg);
      tft.fillCircle(x + 21, y + 20, 8, fg); tft.fillCircle(x + 43, y + 20, 8, fg);
      tft.fillRoundRect(x + 11, y + 22, 42, 26, 11, fg);
      tft.fillCircle(x + 21, y + 20, 5, bg); tft.fillCircle(x + 43, y + 20, 5, bg);
      tft.fillCircle(x + 22, y + 19, 2, fg); tft.fillCircle(x + 44, y + 19, 2, fg);
      tft.fillRect(x + 15, y + 34, 6, 2, mid); tft.fillRect(x + 43, y + 34, 6, 2, mid);
      tft.drawLine(x + 27, y + 37, x + 29, y + 40, bg); tft.drawLine(x + 29, y + 40, x + 32, y + 37, bg);
      tft.drawLine(x + 32, y + 37, x + 35, y + 40, bg); tft.drawLine(x + 35, y + 40, x + 37, y + 37, bg);
      tft.fillRoundRect(x + 8, y + 45, 13, 6, 3, fg); tft.fillRoundRect(x + 43, y + 45, 13, 6, 3, fg);
      break;
    case 2:   // galaga: fox-head starship zapping a mouse
      tft.drawPixel(x + 8, y + 8, mid); tft.drawPixel(x + 14, y + 30, mid); tft.drawPixel(x + 22, y + 6, mid);
      tft.drawPixel(x + 55, y + 34, mid); tft.drawPixel(x + 50, y + 48, mid);
      tft.fillCircle(x + 43, y + 7, 3, fg); tft.fillCircle(x + 53, y + 7, 3, fg);
      tft.fillRoundRect(x + 41, y + 8, 14, 9, 4, fg);
      tft.fillRect(x + 45, y + 11, 1, 2, bg); tft.fillRect(x + 50, y + 11, 1, 2, bg);
      tft.drawLine(x + 41, y + 13, x + 35, y + 17, fg);
      tft.fillRect(x + 31, y + 8, 2, 10, mid);
      tft.fillTriangle(x + 32, y + 24, x + 15, y + 52, x + 49, y + 52, fg);
      tft.fillTriangle(x + 18, y + 42, x + 19, y + 25, x + 27, y + 37, fg);
      tft.fillTriangle(x + 46, y + 42, x + 45, y + 25, x + 37, y + 37, fg);
      tft.fillRect(x + 25, y + 41, 3, 3, bg); tft.fillRect(x + 37, y + 41, 3, 3, bg);
      tft.fillTriangle(x + 27, y + 52, x + 37, y + 52, x + 32, y + 46, bg);
      tft.fillRect(x + 31, y + 46, 2, 2, fg);
      break;
    case 3:   // forest: Micor the wolf peeking out of the trees
      tft.fillTriangle(x + 2, y + 24, x + 7, y + 6, x + 12, y + 24, mid);
      tft.fillTriangle(x + 52, y + 24, x + 57, y + 6, x + 62, y + 24, mid);
      tft.fillTriangle(x + 14, y + 26, x + 16, y + 5, x + 29, y + 17, fg);
      tft.fillTriangle(x + 50, y + 26, x + 48, y + 5, x + 35, y + 17, fg);
      tft.fillTriangle(x + 18, y + 19, x + 18, y + 11, x + 24, y + 17, mid);
      tft.fillTriangle(x + 46, y + 19, x + 46, y + 11, x + 40, y + 17, mid);
      tft.fillRoundRect(x + 14, y + 16, 36, 30, 12, fg);
      tft.fillTriangle(x + 14, y + 32, x + 8, y + 42, x + 20, y + 44, fg);
      tft.fillTriangle(x + 50, y + 32, x + 56, y + 42, x + 44, y + 44, fg);
      tft.fillRoundRect(x + 24, y + 34, 16, 10, 5, bg);
      tft.fillRoundRect(x + 29, y + 33, 6, 4, 2, fg);
      tft.fillRoundRect(x + 21, y + 25, 5, 6, 2, bg); tft.fillRoundRect(x + 38, y + 25, 5, 6, 2, bg);
      tft.fillRect(x + 17, y + 33, 4, 2, mid); tft.fillRect(x + 43, y + 33, 4, 2, mid);
      tft.drawLine(x + 29, y + 40, x + 32, y + 42, fg); tft.drawLine(x + 32, y + 42, x + 35, y + 40, fg);
      break;
    case 4:   // race: zoomy car with kitty ears and a fluffy tail
      tft.drawFastHLine(x + 2, y + 31, 6, mid); tft.drawFastHLine(x + 1, y + 36, 7, mid); tft.drawFastHLine(x + 3, y + 41, 5, mid);
      tft.fillCircle(x + 12, y + 24, 5, fg); tft.fillCircle(x + 8, y + 18, 4, fg); tft.fillCircle(x + 7, y + 14, 3, mid);
      tft.fillTriangle(x + 21, y + 20, x + 23, y + 9, x + 29, y + 18, fg);
      tft.fillTriangle(x + 41, y + 20, x + 39, y + 9, x + 33, y + 18, fg);
      tft.fillRoundRect(x + 17, y + 16, 28, 16, 7, fg);
      tft.fillRoundRect(x + 10, y + 27, 48, 14, 6, fg);
      tft.fillRoundRect(x + 21, y + 20, 9, 8, 3, bg); tft.fillRoundRect(x + 33, y + 20, 9, 8, 3, bg);
      tft.fillRect(x + 53, y + 31, 3, 3, bg);
      tft.fillCircle(x + 21, y + 42, 7, bg); tft.fillCircle(x + 47, y + 42, 7, bg);
      tft.fillCircle(x + 21, y + 42, 5, fg); tft.fillCircle(x + 47, y + 42, 5, fg);
      tft.fillCircle(x + 21, y + 42, 2, bg); tft.fillCircle(x + 47, y + 42, 2, bg);
      break;
    case 5:   // clock: cat-face clock with whiskers and a swishy tail
      tft.drawLine(x + 4, y + 25, x + 13, y + 27, mid); tft.drawLine(x + 4, y + 31, x + 13, y + 30, mid);
      tft.drawLine(x + 60, y + 25, x + 51, y + 27, mid); tft.drawLine(x + 60, y + 31, x + 51, y + 30, mid);
      tft.fillTriangle(x + 16, y + 17, x + 17, y + 3, x + 28, y + 11, fg);
      tft.fillTriangle(x + 48, y + 17, x + 47, y + 3, x + 36, y + 11, fg);
      tft.drawLine(x + 32, y + 44, x + 39, y + 50, fg); tft.drawLine(x + 33, y + 44, x + 40, y + 50, fg);
      tft.fillCircle(x + 41, y + 50, 3, fg);
      tft.fillCircle(x + 32, y + 28, 18, fg);
      tft.fillCircle(x + 32, y + 28, 14, bg);
      tft.fillRect(x + 31, y + 15, 2, 3, fg); tft.fillRect(x + 31, y + 39, 2, 3, fg);
      tft.fillRect(x + 19, y + 27, 3, 2, fg); tft.fillRect(x + 43, y + 27, 3, 2, fg);
      tft.drawLine(x + 32, y + 28, x + 32, y + 19, fg); tft.drawLine(x + 33, y + 28, x + 33, y + 19, fg);
      tft.drawLine(x + 32, y + 28, x + 39, y + 32, fg);
      tft.fillCircle(x + 32, y + 28, 2, fg);
      break;
    case 6: { // stats: a little monitor whose bars are toe beans
      tft.drawRoundRect(x + 8, y + 5, 48, 37, 5, fg); tft.drawRoundRect(x + 9, y + 6, 46, 35, 4, fg);
      tft.fillRect(x + 29, y + 42, 6, 5, fg); tft.fillRoundRect(x + 19, y + 47, 26, 4, 2, fg);
      const int bx[4] = {15, 25, 35, 45}, bt[4] = {29, 22, 15, 24};
      for (int k = 0; k < 4; k++) {
        uint16_t c = (k == 2) ? fg : mid;
        tft.fillRect(x + bx[k], y + bt[k], 6, 37 - bt[k], c);
        tft.fillCircle(x + bx[k] + 3, y + bt[k], 3, c);
      }
      break;
    }
  }
}
// ---------- ledger (left) ----------
const char APP_KIND_P[] PROGMEM = "DOC\nDOC\nTOOL\nTOOL\nDEV\nSYS\nTOOL\nDAW\nNET\nGAME\nGAME\nGAME\nRPG\nGAME\nTOOL\nTOOL\nSYS\nSYS\nNET\n3D\nDOC";
const char* appKind(int i) { return pgmItem(APP_KIND_P, i); }
uint16_t menuAccent() { return appAccent(1 + menuSel); }
void drawLedgerFrame() {
  tft.drawRect(LEDGER_X - 1, LEDGER_Y - 1, LEDGER_W + 2, LEDGER_HEAD + MENU_VIS * LEDGER_ROW + 2, E_RULE);
  tft.fillRect(LEDGER_X, LEDGER_Y, LEDGER_W, LEDGER_HEAD, E_HEAD);
  putText(LEDGER_X + 28, LEDGER_Y + 3, 1, C_INK, E_HEAD, FS("Name"));
  putText(LEDGER_X + 148, LEDGER_Y + 3, 1, C_INK, E_HEAD, FS("Type"));
  tft.drawFastHLine(LEDGER_X, LEDGER_Y + LEDGER_HEAD - 1, LEDGER_W, E_RULE);
}
void drawLedgerRow(int s) {
  int i = menuTop + s, x = LEDGER_X, y = LEDGER_Y + LEDGER_HEAD + s * LEDGER_ROW, w = LEDGER_W - 8;
  bool sel = i == menuSel;
  uint16_t acc = appAccent(1 + i), bg = (i & 1) ? E_ROW2 : E_WHITE;
  tft.fillRect(x, y, w, LEDGER_ROW, bg);
  tft.fillRoundRect(x + 4, y + 3, 18, 18, 3, acc);
  drawMiniGlyph(i, x + 13, y + 12, E_ONACC, acc);
  putText(x + 28, y + 4, 1, C_INK, bg, appName(i));
  char d[20]; snprintf_P(d, sizeof d, PSTR("%.18s"), pgmItem(APP_DESC_P, i));
  putText(x + 28, y + 14, 1, C_MID, bg, d);
  tft.fillRect(x + 144, y + 6, 32, 12, sel ? acc : bg);
  tft.drawRect(x + 144, y + 6, 32, 12, acc);
  putText(x + 160 - (int)strlen(appKind(i)) * 3, y + 8, 1, sel ? E_ONACC : acc, sel ? acc : bg, appKind(i));
  if (sel) { tft.drawRect(x, y, w, LEDGER_ROW, acc); tft.drawRect(x + 1, y + 1, w - 2, LEDGER_ROW - 2, acc); }
}
void drawLedgerScroll() {
  int x = LEDGER_X + LEDGER_W - 8, y = LEDGER_Y + LEDGER_HEAD, h = MENU_VIS * LEDGER_ROW;
  tft.fillRect(x, y, 8, h, E_ROW2);
  tft.drawFastVLine(x, y, h, E_RULE);
  int th = h * MENU_VIS / MENU_N, ty = y + (h - th) * menuTop / (MENU_N - MENU_VIS);
  tft.fillRect(x + 2, ty + 1, 4, th - 2, menuAccent());
}

// ---------- System panel (right) ----------
void drawSysFrame() {
  tft.drawRect(SYS_X, LEDGER_Y - 1, SYS_W, 188, E_RULE);
  tft.fillRect(SYS_X + 1, LEDGER_Y, SYS_W - 2, LEDGER_HEAD, E_HEAD);
  putText(SYS_X + 6, LEDGER_Y + 3, 1, C_INK, E_HEAD, FS("System"));
  tft.drawFastHLine(SYS_X + 1, LEDGER_Y + LEDGER_HEAD - 1, SYS_W - 2, E_RULE);
  tft.fillRect(SYS_X + 1, LEDGER_Y + LEDGER_HEAD, SYS_W - 2, 186 - LEDGER_HEAD, E_WHITE);
}
void drawHomeStats(bool force) {
  char b[24];
  if (P.panel) {                                   // owner card instead of live stats
    if (!force) return;
    putText(SYS_X + 8, 84, 1, C_MID, E_WHITE, FS("this psion is"));
    putText(SYS_X + 8, 94, 1, C_MID, E_WHITE, FS("owned by:"));
    snprintf_P(b, sizeof b, PSTR("%.8s"), P.owner[0] ? P.owner : FS("you :3"));
    putText(SYS_X + 8, 106, 2, menuAccent(), E_WHITE, b);
    snprintf_P(b, sizeof b, PSTR("%d note%s saved"), notesSaved, notesSaved == 1 ? FS("") : FS("s"));
    putText(SYS_X + 8, 128, 1, C_MID, E_WHITE, b);
    return;
  }
  uint32_t u = millis() / 1000;
  snprintf_P(b, sizeof b, PSTR("CPU %3d%%  "), cpuLoad);                       putText(SYS_X + 8, 84, 1, C_INK, E_WHITE, b);
  drawGauge(SYS_X + 8, 94, SYS_W - 16, cpuLoad, loadColor(cpuLoad));
  snprintf_P(b, sizeof b, PSTR("RAM %2uK free "), (unsigned)(freeHeapNow / 1024)); putText(SYS_X + 8, 106, 1, C_INK, E_WHITE, b);
  drawGauge(SYS_X + 8, 116, SYS_W - 16, ramUsed, loadColor(ramUsed));
  snprintf_P(b, sizeof b, PSTR("up %02u:%02u:%02u"), (unsigned)(u / 3600), (unsigned)((u / 60) % 60), (unsigned)(u % 60));
  putText(SYS_X + 8, 128, 1, C_MID, E_WHITE, b);
}
void drawAnalogClock(int cx, int cy, int R, int h, int m, int s, bool face, uint16_t accent) {
  if (face) {
    tft.fillCircle(cx, cy, R, E_ROW2);
    tft.drawCircle(cx, cy, R, C_INK); tft.drawCircle(cx, cy, R - 1, C_INK);
    for (int k = 0; k < 12; k++) {
      float a = k * 0.5236f;
      int l = (k % 3 == 0) ? R / 6 : R / 15;
      tft.drawLine(cx + (int)(sinf(a) * (R - 3)), cy - (int)(cosf(a) * (R - 3)),
                   cx + (int)(sinf(a) * (R - 3 - l)), cy - (int)(cosf(a) * (R - 3 - l)), C_INK);
    }
  }
  tft.fillCircle(cx, cy, R * 7 / 10, E_ROW2);
  float ah = ((h % 12) + m / 60.0f) * 0.5236f, am = (m + s / 60.0f) * 0.10472f, as = s * 0.10472f;
  int lh = R * 2 / 5, lm = R * 63 / 100, ls = R * 2 / 3;
  tft.drawLine(cx, cy, cx + (int)(sinf(ah) * lh), cy - (int)(cosf(ah) * lh), C_INK);
  tft.drawLine(cx + 1, cy, cx + 1 + (int)(sinf(ah) * lh), cy - (int)(cosf(ah) * lh), C_INK);
  tft.drawLine(cx, cy, cx + (int)(sinf(am) * lm), cy - (int)(cosf(am) * lm), C_INK);
  if (P.secHand) tft.drawLine(cx, cy, cx + (int)(sinf(as) * ls), cy - (int)(cosf(as) * ls), accent);
  tft.fillCircle(cx, cy, R > 40 ? 4 : 2, accent);
}
void menuClock(bool force) {
  static int lastS = -1, lastM = -1, lastD = -1;
  int h, m, s; getHMS(&h, &m, &s);
  if (!force && s == lastS) return;
  char b[32];
  int day = (int)(clkEpoch / 86400UL);
  if (force || m != lastM || day != lastD) {
    if (P.homeFace == 2) snprintf_P(b, sizeof b, PSTR("%s  "), WD[(day + 4) % 7]);          // analog only: weekday up top
    else snprintf(b, sizeof b, P.h12 ? "%2d:%02d" : "%02d:%02d", dispHour(h) & 31, m & 63);
    putText(SYS_X + 8, 44, 3, C_INK, E_WHITE, b);
    putText(SYS_X + SYS_W - 18, LEDGER_Y + 3, 1, C_INK, E_HEAD, P.h12 && P.homeFace != 2 ? ampm(h) : FS("  "));
    lastM = m;
  }
  if (force || day != lastD) {
    fmtDate(b, sizeof b, day);
    tft.fillRect(SYS_X + 1, 70, SYS_W - 2, 8, E_WHITE);
    putText(SYS_X + 8, 70, 1, C_MID, E_WHITE, b);
    lastD = day;
  }
  if (P.homeFace == 1) {                                    // digital only: big seconds instead of the dial
    if (force) { tft.fillRect(SYS_X + 1, 140, SYS_W - 2, 66, E_WHITE); putText(SYS_X + 41, 192, 1, C_MID, E_WHITE, FS("sec")); }
    snprintf_P(b, sizeof b, PSTR("%02d"), s & 63);
    putText(SYS_X + 29, 150, 4, menuAccent(), E_WHITE, b);
  } else drawAnalogClock(SYS_X + SYS_W / 2, 174, 30, h, m, s, force, menuAccent());
  lastS = s;
}

// ---------- screensaver: sleepy paws bouncing around ----------
const uint8_t SAVER_MIN[4] = {0, 1, 3, 10};
bool saverOn = false, wakeSwallow = false;
uint32_t idleSince = 0;
// ---------- screensavers (TFT + OLED) ----------
// TFT styles: paws, 3D pipes, bouncy text, Micor's face, furry eyes, starfield, mystify, matrix rain, shuffle.
// The OLED either mirrors the TFT style ("match") or runs its own: eyes, paws, stars, text, clock, off.
enum { SV_PAWS, SV_PIPES, SV_TEXT, SV_MICOR, SV_EYES, SV_STARS, SV_MYSTIFY, SV_MATRIX, SV_SHUFFLE };
enum { OS_MATCH, OS_EYES, OS_PAWS, OS_STARS, OS_TEXT, OS_CLOCK, OS_OFF };
const uint16_t SV_COLS[8] = {RGB565(230, 60, 60), RGB565(60, 200, 90), RGB565(70, 120, 240), RGB565(240, 210, 60),
                             RGB565(60, 210, 220), RGB565(220, 80, 220), RGB565(255, 150, 40), RGB565(210, 214, 222)};
// Micor's face-cam colours, copied from forest.ino (that file stays untouched)
const uint16_t MF_WHITE = RGB565(246, 248, 252), MF_GRAY = RGB565(200, 206, 214), MF_BLUE = RGB565(60, 120, 230);
const uint16_t MF_TEXT = RGB565(36, 32, 48), MF_PINK = RGB565(255, 150, 185);
int svStyle = SV_PAWS;
uint32_t svAt = 0, svSwitchAt = 0, svOledAt = 0;
union SaverState {
  struct { int8_t x, y, z, d; uint8_t pipes, segs; uint16_t col; uint8_t occ[40]; } pipe;
  struct { int16_t x[40], y[40], z[40]; int16_t px[40], py[40]; } star;
  struct { int16_t p[2][4][2], v[2][4][2], tr[2][6][4][2]; uint8_t head, hue; } mys;
  struct { int8_t head[54]; } rain;
  struct { int16_t x, y, dx, dy, w, h; uint8_t col, expr; uint32_t blinkAt, exprAt; } bounce;
  struct { int16_t lx, ly, tx, ty; uint32_t blinkAt, moveAt; bool shut; } eyes;
};
SaverState SV;
struct { int16_t x, y, dx, dy, lx, ly, tx, ty; int8_t sx[24], sy[24], sz[24]; uint8_t px, py, steps; } OSV;   // OLED saver state

uint16_t svShade(uint16_t c, int pct) {            // darken / lighten a colour (0..140 %)
  int r = ((c >> 11) & 31) * 8, g = ((c >> 5) & 63) * 4, b = (c & 31) * 8;
  return pk565(r * pct / 100, g * pct / 100, b * pct / 100);
}

// ----- Micor's face (scaled from the forest game's face cam; bx, by = top-left, s = scale) -----
void mfLine(int x1, int y1, int x2, int y2, int t, uint16_t c) { for (int k = 0; k < t; k++) { tft.drawLine(x1 + k, y1, x2 + k, y2, c); tft.drawLine(x1, y1 + k, x2, y2 + k, c); } }
void micorFace(int bx, int by, int s, int e, bool blink) {
  uint16_t bg = (e == 0) ? RGB565(120, 205, 130) : (e == 1) ? RGB565(235, 210, 100) : (e == 2) ? RGB565(240, 150, 70) : RGB565(110, 110, 125);
  auto S = [&](int v) { return v * s; };
  tft.fillRect(bx, by, S(52), S(52), bg);
  tft.fillTriangle(bx + S(8), by + S(22), bx + S(12), by + S(4), bx + S(22), by + S(16), MF_WHITE);
  tft.fillTriangle(bx + S(44), by + S(22), bx + S(40), by + S(4), bx + S(30), by + S(16), MF_WHITE);
  tft.fillTriangle(bx + S(12), by + S(17), bx + S(13), by + S(9), bx + S(18), by + S(14), MF_PINK);
  tft.fillTriangle(bx + S(40), by + S(17), bx + S(39), by + S(9), bx + S(34), by + S(14), MF_PINK);
  tft.fillRoundRect(bx + S(7), by + S(14), S(38), S(30), S(12), MF_WHITE);
  tft.fillTriangle(bx + S(7), by + S(30), bx + S(3), by + S(38), bx + S(12), by + S(40), MF_WHITE);
  tft.fillTriangle(bx + S(45), by + S(30), bx + S(49), by + S(38), bx + S(40), by + S(40), MF_WHITE);
  tft.fillRoundRect(bx + S(17), by + S(31), S(18), S(12), S(5), MF_GRAY);
  tft.fillRoundRect(bx + S(23), by + S(30), S(6), S(4), S(2), 0);
  int ey = by + S(26);
  for (int i = 0; i < 2; i++) {
    int ex = bx + S(i ? 35 : 17);
    if (blink) mfLine(ex - S(4), ey, ex + S(4), ey, s, 0);
    else if (e == 0) { mfLine(ex - S(4), ey + S(3), ex, ey - S(1), s, 0); mfLine(ex, ey - S(1), ex + S(4), ey + S(3), s, 0); }
    else if (e == 3) { mfLine(ex - S(3), ey - S(3), ex + S(3), ey + S(3), s, 0); mfLine(ex - S(3), ey + S(3), ex + S(3), ey - S(3), s, 0); }
    else { tft.fillRoundRect(ex - S(3), ey - S(5), S(7), S(9), S(2), MF_BLUE); tft.fillRect(ex - S(1), ey - S(3), S(2), S(2), MF_WHITE); }
  }
  if (e == 0) { mfLine(bx + S(22), by + S(38), bx + S(26), by + S(41), s, 0); mfLine(bx + S(26), by + S(41), bx + S(30), by + S(38), s, 0); }
  else if (e == 1) tft.fillRect(bx + S(22), by + S(39), S(8), s, 0);
  else if (e == 2) { tft.fillCircle(bx + S(45), by + S(15), S(3), K_CYAN); mfLine(bx + S(22), by + S(40), bx + S(26), by + S(38), s, 0); mfLine(bx + S(26), by + S(38), bx + S(30), by + S(40), s, 0); }
  else tft.fillRect(bx + S(22), by + S(40), S(8), s, 0);
}

// ----- one frame of each TFT style -----
void svPipeBall(int x, int y, int r, uint16_t c) {
  for (int k = r + 2; k > 0; k--) tft.fillCircle(x, y, k, svShade(c, 45 + (r + 2 - k) * 60 / (r + 2)));
  tft.fillCircle(x - r / 3, y - r / 3, max(1, r / 3), svShade(c, 140));
}
void svPipeXY(int gx, int gy, int gz, int* x, int* y) { *x = 26 + gx * 30 + gz * 9; *y = 40 + gy * 28 - gz * 9; }
bool svPipeFree(int x, int y, int z) {
  if (x < 0 || x > 8 || y < 0 || y > 6 || z < 0 || z > 4) return false;
  int i = (z * 7 + y) * 9 + x;
  return !(SV.pipe.occ[i >> 3] & (1 << (i & 7)));
}
void svPipeMark(int x, int y, int z) { int i = (z * 7 + y) * 9 + x; SV.pipe.occ[i >> 3] |= 1 << (i & 7); }
void svPipeNew() {
  auto& p = SV.pipe;
  if (++p.pipes > 7) { tft.fillScreen(0); memset(p.occ, 0, sizeof p.occ); p.pipes = 1; }
  for (int t = 0; t < 30; t++) { p.x = random(9); p.y = random(7); p.z = random(5); if (svPipeFree(p.x, p.y, p.z)) break; }
  p.d = random(6); p.segs = 0; p.col = SV_COLS[random(8)];
  svPipeMark(p.x, p.y, p.z);
  int x, y; svPipeXY(p.x, p.y, p.z, &x, &y);
  svPipeBall(x, y, 7 - p.z / 2, p.col);
}
void svPipes() {
  static const int8_t D[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  auto& p = SV.pipe;
  int d = p.d;
  if (random(100) < 25 || !svPipeFree(p.x + D[d][0], p.y + D[d][1], p.z + D[d][2])) {
    int opts[6], n = 0;
    for (int k = 0; k < 6; k++) if (svPipeFree(p.x + D[k][0], p.y + D[k][1], p.z + D[k][2])) opts[n++] = k;
    if (!n || p.segs > 40) { svPipeNew(); return; }
    d = opts[random(n)];
  }
  int x1, y1, x2, y2, r = 7 - p.z / 2;
  svPipeXY(p.x, p.y, p.z, &x1, &y1);
  bool turn = d != p.d;
  p.x += D[d][0]; p.y += D[d][1]; p.z += D[d][2]; p.d = d; p.segs++;
  svPipeMark(p.x, p.y, p.z);
  svPipeXY(p.x, p.y, p.z, &x2, &y2);
  bool horiz = abs(x2 - x1) >= abs(y2 - y1);
  for (int k = -r; k <= r; k++) {                   // shaded tube: bright in the middle, dark at the edges
    uint16_t c = svShade(p.col, 115 - (k * k * 75) / (r * r));
    tft.drawLine(x1 + (horiz ? 0 : k), y1 + (horiz ? k : 0), x2 + (horiz ? 0 : k), y2 + (horiz ? k : 0), c);
  }
  if (turn) svPipeBall(x1, y1, r, p.col);           // a ball joint where it bends
}
void svBounceMove(int maxX, int maxY) {
  auto& b = SV.bounce;
  int ox = b.x, oy = b.y;
  b.x += b.dx; b.y += b.dy;
  bool hit = false;
  if (b.x < 0 || b.x > maxX) { b.dx = -b.dx; b.x = constrain(b.x, 0, maxX); hit = true; }
  if (b.y < 0 || b.y > maxY) { b.dy = -b.dy; b.y = constrain(b.y, 0, maxY); hit = true; }
  if (hit) b.col = (b.col + 1) % 8;
  if (b.x > ox) tft.fillRect(ox, oy, b.x - ox, b.h, 0); else if (b.x < ox) tft.fillRect(b.x + b.w, oy, ox - b.x, b.h, 0);
  if (b.y > oy) tft.fillRect(ox, oy, b.w, b.y - oy, 0); else if (b.y < oy) tft.fillRect(ox, b.y + b.h, b.w, oy - b.y, 0);
}
void svText() {
  auto& b = SV.bounce;
  const char* t = P.saverText[0] ? P.saverText : "Psion :3";
  int sz = strlen(t) * 24 <= 300 ? 4 : (strlen(t) * 18 <= 300 ? 3 : 2);
  b.w = strlen(t) * 6 * sz; b.h = 8 * sz;
  svBounceMove(W - b.w, H - b.h);
  putText(b.x, b.y, sz, SV_COLS[b.col], 0, t);
}
void svMicor(uint32_t now) {
  auto& b = SV.bounce;
  b.w = b.h = 52 * 3;
  svBounceMove(W - b.w, H - b.h);
  if (now > b.exprAt) { b.expr = (b.expr + 1) % 4; b.exprAt = now + 3500; }
  bool blink = (now % 4000) < 160;
  const uint8_t EX[4] = {0, 1, 0, 2};
  micorFace(b.x, b.y, 3, EX[b.expr], blink);
}
void svEyesDraw(bool full) {
  auto& e = SV.eyes;
  const uint16_t fur = RGB565(70, 50, 90);
  if (full) {
    tft.fillScreen(0);
    tft.fillRoundRect(20, 40, 280, 170, 60, fur);
    tft.fillTriangle(40, 70, 60, 4, 120, 50, fur); tft.fillTriangle(280, 70, 260, 4, 200, 50, fur);
    tft.fillTriangle(60, 56, 66, 20, 100, 48, MF_PINK); tft.fillTriangle(260, 56, 254, 20, 220, 48, MF_PINK);
    tft.fillTriangle(150, 160, 170, 160, 160, 172, MF_PINK);       // little nose
  }
  for (int i = 0; i < 2; i++) {
    int cx = i ? 220 : 100, cy = 120;
    if (e.shut) { tft.fillCircle(cx, cy, 46, fur); tft.fillRect(cx - 40, cy - 2, 80, 5, 0); continue; }
    tft.fillCircle(cx, cy, 46, MF_WHITE);
    tft.fillCircle(cx + e.lx, cy + e.ly, 20, RGB565(60, 140, 90));
    tft.fillCircle(cx + e.lx, cy + e.ly, 11, 0);
    tft.fillCircle(cx + e.lx - 7, cy + e.ly - 7, 5, MF_WHITE);
  }
}
void svEyes(uint32_t now) {
  auto& e = SV.eyes;
  bool redraw = false;
  if (now > e.moveAt) { e.tx = random(-20, 21); e.ty = random(-16, 17); e.moveAt = now + 1200 + random(2500); }
  if (e.lx != e.tx || e.ly != e.ty) { e.lx += (e.tx > e.lx) - (e.tx < e.lx); e.ly += (e.ty > e.ly) - (e.ty < e.ly); redraw = true; }
  bool shut = now > e.blinkAt && now < e.blinkAt + 140;
  if (now > e.blinkAt + 140) e.blinkAt = now + 2500 + random(4000);
  if (shut != e.shut) { e.shut = shut; redraw = true; }
  if (redraw) svEyesDraw(false);
}
void svStars() {
  auto& s = SV.star;
  for (int i = 0; i < 40; i++) {
    if (s.px[i] >= 0) tft.fillRect(s.px[i], s.py[i], 2, 2, 0);
    s.z[i] -= 12;
    if (s.z[i] < 8) { s.x[i] = random(-800, 800); s.y[i] = random(-600, 600); s.z[i] = 1000; }
    int x = 160 + s.x[i] * 120 / s.z[i], y = 120 + s.y[i] * 120 / s.z[i];
    if (x < 0 || x >= W - 1 || y < 0 || y >= H - 1) { s.z[i] = 0; s.px[i] = -1; continue; }
    int br = 255 - s.z[i] / 5;
    tft.fillRect(x, y, s.z[i] < 300 ? 2 : 1, s.z[i] < 300 ? 2 : 1, pk565(br, br, br));
    s.px[i] = x; s.py[i] = y;
  }
}
void svMystifyPoly(int k, int t, uint16_t c) {
  auto& m = SV.mys;
  for (int i = 0; i < 4; i++) { int j = (i + 1) % 4; tft.drawLine(m.tr[k][t][i][0], m.tr[k][t][i][1], m.tr[k][t][j][0], m.tr[k][t][j][1], c); }
}
void svMystify() {
  auto& m = SV.mys;
  int nh = (m.head + 1) % 6;
  for (int k = 0; k < 2; k++) {
    svMystifyPoly(k, nh, 0);                         // erase the oldest copy
    for (int i = 0; i < 4; i++) for (int a = 0; a < 2; a++) {
      m.p[k][i][a] += m.v[k][i][a];
      int lim = a ? H - 1 : W - 1;
      if (m.p[k][i][a] < 0 || m.p[k][i][a] > lim) { m.v[k][i][a] = -m.v[k][i][a]; m.p[k][i][a] = constrain(m.p[k][i][a], 0, lim); }
      m.tr[k][nh][i][a] = m.p[k][i][a];
    }
    m.hue++;
    svMystifyPoly(k, nh, SV_COLS[((m.hue >> 5) + k * 3) % 8]);
  }
  m.head = nh;
}
void svMatrix() {
  auto& r = SV.rain;
  for (int c = 0; c < 53; c++) {
    if (random(100) < 45) continue;                  // columns fall at different speeds
    int h = r.head[c], x = c * 6;
    if (h >= 0 && h < 24) tft.drawChar(x, h * 10, 33 + random(94), RGB565(60, 200, 90), 0, 1);
    h++;
    if (h < 24) tft.drawChar(x, h * 10, 33 + random(94), RGB565(200, 255, 210), 0, 1);
    int tail = h - 9;
    if (tail >= 0 && tail < 24) tft.fillRect(x, tail * 10, 6, 10, 0);
    if (tail > 24) h = -random(20);
    r.head[c] = h;
  }
}

// ----- OLED styles (the OLED is a frame buffer, so most styles redraw from scratch) -----
void oPaw(int x, int y, int u) {
  oled.fillCircle(x, y + 2 * u, 3 * u, SSD1306_WHITE);
  oled.fillCircle(x - 19 * u / 4, y + u / 4, 3 * u / 2, SSD1306_WHITE); oled.fillCircle(x - 2 * u, y - 5 * u / 2, 3 * u / 2, SSD1306_WHITE);
  oled.fillCircle(x + 2 * u, y - 5 * u / 2, 3 * u / 2, SSD1306_WHITE); oled.fillCircle(x + 19 * u / 4, y + u / 4, 3 * u / 2, SSD1306_WHITE);
}
void oledSaverFrame(int style, uint32_t now) {
  auto& o = OSV;
  bool keep = style == 100 + SV_PIPES;              // pipes accumulate on the OLED
  if (!keep) oled.clearDisplay();
  int h, m, s; getHMS(&h, &m, &s);
  char b[12];
  switch (style) {
    case OS_EYES: case 100 + SV_EYES: {
      if (now % 4000 < 150) { oled.fillRect(16, 32, 40, 3, SSD1306_WHITE); oled.fillRect(72, 32, 40, 3, SSD1306_WHITE); break; }
      if (now % 1700 < 120) { o.tx = random(-9, 10); o.ty = random(-6, 7); }
      o.lx += (o.tx > o.lx) - (o.tx < o.lx); o.ly += (o.ty > o.ly) - (o.ty < o.ly);
      for (int i = 0; i < 2; i++) {
        int cx = i ? 92 : 36;
        oled.fillTriangle(cx - 18, 14, cx - 14, 0, cx - 4, 10, SSD1306_WHITE); oled.fillTriangle(cx + 18, 14, cx + 14, 0, cx + 4, 10, SSD1306_WHITE);
        oled.fillCircle(cx, 36, 22, SSD1306_WHITE);
        oled.fillCircle(cx + o.lx, 36 + o.ly, 9, SSD1306_BLACK);
        oled.fillCircle(cx + o.lx - 3, 33 + o.ly, 2, SSD1306_WHITE);
      }
      break;
    }
    case OS_PAWS: case 100 + SV_PAWS:
      randomSeed(now / 1500);                        // same paws for 1.5 s, then new ones
      for (int k = 0; k < 3; k++) oPaw(random(14, 114), random(14, 52), 2 + random(2));
      randomSeed(ESP.random());
      break;
    case OS_STARS: case 100 + SV_STARS: case 100 + SV_MATRIX:
      for (int i = 0; i < 24; i++) {
        o.sz[i] -= 3;
        if (o.sz[i] < 4) { o.sx[i] = random(-60, 60); o.sy[i] = random(-30, 30); o.sz[i] = 100; }
        int x = 64 + o.sx[i] * 40 / o.sz[i], y = 32 + o.sy[i] * 40 / o.sz[i];
        if (style == 100 + SV_MATRIX) { oled.drawChar(x & 127, (now / 60 + i * 7) % 64, 33 + (i * 7 + now / 200) % 90, SSD1306_WHITE, SSD1306_BLACK, 1); continue; }
        if (x < 0 || x > 127 || y < 0 || y > 63) { o.sz[i] = 0; continue; }
        if (o.sz[i] < 40) oled.fillRect(x, y, 2, 2, SSD1306_WHITE); else oled.drawPixel(x, y, SSD1306_WHITE);
      }
      break;
    case OS_TEXT: case 100 + SV_TEXT: case 100 + SV_MYSTIFY: {
      const char* t = P.saverText[0] ? P.saverText : "Psion :3";
      int w = strlen(t) * 12;
      if (style == 100 + SV_MYSTIFY) { oled.drawLine(o.x, o.y, 127 - o.y * 2, o.x / 2, SSD1306_WHITE); oled.drawLine(127 - o.x, 63 - o.y, o.y * 2, 63 - o.x / 2, SSD1306_WHITE); w = 0; }
      if (!o.dx) { o.dx = 2; o.dy = 1; }
      o.x += o.dx; o.y += o.dy;
      if (o.x < 0 || o.x > max(0, 128 - w)) o.dx = -o.dx;
      if (o.y < 0 || o.y > 48) o.dy = -o.dy;
      o.x = constrain(o.x, 0, max(0, 128 - w)); o.y = constrain(o.y, 0, 48);
      if (w) { oled.setTextSize(2); oled.setCursor(o.x, o.y); oled.print(t); oled.setTextSize(1); }
      break;
    }
    case 100 + SV_MICOR: {                           // tiny Micor face
      if (!o.dx) { o.dx = 1; o.dy = 1; }
      o.x += o.dx; o.y += o.dy;
      if (o.x < 0 || o.x > 88) o.dx = -o.dx;
      if (o.y < 0 || o.y > 24) o.dy = -o.dy;
      int x = constrain(o.x, 0, 88), y = constrain(o.y, 0, 24);
      oled.fillTriangle(x + 4, y + 18, x + 6, y, x + 16, y + 10, SSD1306_WHITE); oled.fillTriangle(x + 36, y + 18, x + 34, y, x + 24, y + 10, SSD1306_WHITE);
      oled.fillRoundRect(x + 4, y + 8, 32, 30, 10, SSD1306_WHITE);
      oled.fillRoundRect(x + 14, y + 26, 12, 8, 3, SSD1306_BLACK); oled.fillRect(x + 18, y + 25, 4, 3, SSD1306_WHITE);
      bool blink = now % 4000 < 150;
      for (int i = 0; i < 2; i++) {
        int ex = x + (i ? 27 : 13), ey = y + 19;
        if (blink) oled.drawFastHLine(ex - 3, ey, 7, SSD1306_BLACK);
        else { oled.drawLine(ex - 3, ey + 2, ex, ey - 1, SSD1306_BLACK); oled.drawLine(ex, ey - 1, ex + 3, ey + 2, SSD1306_BLACK); }
      }
      break;
    }
    case 100 + SV_PIPES: {                           // a line that wanders around a grid
      if (!o.steps || o.steps > 140) { oled.clearDisplay(); o.px = 8 * random(16); o.py = 8 * random(8); o.steps = 1; }
      static const int8_t DX[4] = {8, -8, 0, 0}, DY[4] = {0, 0, 8, -8};
      int d = random(4), nx = o.px + DX[d], ny = o.py + DY[d];
      if (nx >= 0 && nx < 128 && ny >= 0 && ny < 64) {
        for (int k = -1; k <= 1; k++) oled.drawLine(o.px + (DY[d] ? k : 0), o.py + (DX[d] ? k : 0), nx + (DY[d] ? k : 0), ny + (DX[d] ? k : 0), SSD1306_WHITE);
        o.px = nx; o.py = ny; o.steps++;
      }
      break;
    }
    default: {                                       // clock (drifts a little so pixels don't burn in)
      snprintf_P(b, sizeof b, PSTR("%02d:%02d"), dispHour(h) & 31, m & 63);
      int dx = (now / 20000) % 10, dy = (now / 33000) % 8;
      oled.setTextSize(3); oled.setCursor(10 + dx, 12 + dy); oled.print(b);
      oled.setTextSize(1); oled.setCursor(40 + dx, 46 + dy); snprintf_P(b, sizeof b, PSTR(":%02d"), s & 63); oled.print(b);
    }
  }
  oled.display();
}

// ----- start / run -----
void saverPick(uint32_t now) {
  svStyle = P.saverStyle % 9;
  if (svStyle == SV_SHUFFLE) { svStyle = random(8); svSwitchAt = now + 60000; }
  memset(&SV, 0, sizeof SV);
  memset(&OSV, 0, sizeof OSV);
  tft.fillScreen(0);
  switch (svStyle) {
    case SV_PIPES: SV.pipe.pipes = 0; svPipeNew(); break;
    case SV_TEXT: case SV_MICOR: SV.bounce.x = 40; SV.bounce.y = 30; SV.bounce.dx = 2; SV.bounce.dy = 1; SV.bounce.w = SV.bounce.h = 1; break;
    case SV_EYES: svEyesDraw(true); break;
    case SV_STARS: for (int i = 0; i < 40; i++) { SV.star.z[i] = 0; SV.star.px[i] = -1; } break;
    case SV_MYSTIFY:
      for (int k = 0; k < 2; k++) for (int i = 0; i < 4; i++) {
        SV.mys.p[k][i][0] = random(W); SV.mys.p[k][i][1] = random(H);
        SV.mys.v[k][i][0] = random(2) ? random(2, 5) : -random(2, 5); SV.mys.v[k][i][1] = random(2) ? random(2, 5) : -random(2, 5);
      }
      break;
    case SV_MATRIX: for (int c = 0; c < 53; c++) SV.rain.head[c] = -random(24); break;
  }
}
void saverStart() {
  saverOn = true;
  saverPick(millis());
  if (oledOk && P.oledOn && P.oledSaver == OS_OFF) { oled.clearDisplay(); oled.display(); }
}
void saverTick(uint32_t now) {
  static const uint16_t PERIOD[8] = {2000, 45, 30, 50, 30, 30, 30, 60};
  if (P.saverStyle % 9 == SV_SHUFFLE && now > svSwitchAt) saverPick(now);
  if (now - svAt >= PERIOD[svStyle]) {
    svAt = now;
    switch (svStyle) {
      case SV_PAWS: {
        static int sx = -1, sy = 0;
        if (sx >= 0) tft.fillRect(sx - 32, sy - 20, 66, 66, 0);
        sx = random(40, 280); sy = random(30, 170);
        drawPaw(sx, sy, 4, APP_ACCENT_BASE[random(17)]);
        int h, m, s; getHMS(&h, &m, &s);
        char b[8]; snprintf_P(b, sizeof b, PSTR("%02d:%02d"), dispHour(h) & 31, m & 63);
        putText(sx - 30, sy + 26, 2, RGB565(120, 126, 138), 0, b);
        break;
      }
      case SV_PIPES: svPipes(); break;
      case SV_TEXT: svText(); break;
      case SV_MICOR: svMicor(now); break;
      case SV_EYES: svEyes(now); break;
      case SV_STARS: svStars(); break;
      case SV_MYSTIFY: svMystify(); break;
      default: svMatrix(); break;
    }
  }
  if (oledOk && P.oledOn && P.oledSaver != OS_OFF && now - svOledAt >= 100) {
    svOledAt = now;
    oledSaverFrame(P.oledSaver == OS_MATCH ? 100 + svStyle : P.oledSaver, now);
  }
}

// ---------- desktop ----------
void menuEnter() {
  tft.fillScreen(C_LCD);
  saverOn = false; idleSince = millis();
  if (menuSel >= MENU_N) menuSel = MENU_N - 1;
  if (P.deskStyle % DS_N != DS_LEDGER) { deskStart = false; deskPage = -1; deskDraw(); return; }
  if (menuSel < menuTop) menuTop = menuSel;
  else if (menuSel >= menuTop + MENU_VIS) menuTop = menuSel - MENU_VIS + 1;
  drawTitleBar(P.owner[0] ? P.owner : FS("System"), menuAccent());
  drawLedgerFrame();
  for (int s = 0; s < MENU_VIS; s++) drawLedgerRow(s);
  drawLedgerScroll();
  putText(LEDGER_X, 188, 1, C_MID, C_LCD, FS("turn: select   press: open"));
  putText(LEDGER_X, 199, 1, C_MID, C_LCD, FS("hold: back to System"));
  drawSysFrame();
  drawHomeStats(true);
  menuClock(true);
  drawDock(1 + menuSel);
}
void menuTick() {
  uint32_t now = millis();
  int c = takeClicks();
  bool pr = takePress(), sh = takeShort();
  if (c || pr || sh) {
    idleSince = now;
    if (saverOn) { wakeSwallow = pr; menuEnter(); oledDirty = true; return; }
  }
  if (saverOn) { saverTick(now); return; }
  if (P.saver && now - idleSince > SAVER_MIN[P.saver % 4] * 60000UL) { saverStart(); return; }
  if (P.deskStyle % DS_N != DS_LEDGER) {               // the other home screen styles
    if (c) deskTurn(c * ENC_DIR);
    if (sh) { if (wakeSwallow) wakeSwallow = false; else { deskOpen(); return; } }
    deskClockTick();
    static uint32_t lastD = 0;
    if (clkEpoch != lastD) { lastD = clkEpoch; oledDirty = true; }
    return;
  }
  if (c) {
    int old = menuSel, oldTop = menuTop;
    menuSel = (((menuSel + c * ENC_DIR) % MENU_N) + MENU_N) % MENU_N;
    if (menuSel < menuTop) menuTop = menuSel;
    else if (menuSel >= menuTop + MENU_VIS) menuTop = menuSel - MENU_VIS + 1;
    if (old != menuSel) {
      uiClick();
      if (menuTop != oldTop) for (int s = 0; s < MENU_VIS; s++) drawLedgerRow(s);
      else { drawLedgerRow(old - menuTop); drawLedgerRow(menuSel - menuTop); }
      drawLedgerScroll();
      int ot = dockIndexOf(1 + old), nt = dockIndexOf(1 + menuSel);
      if (ot != nt) { drawDockTab(ot, false); drawDockTab(nt, true); }
      drawTitleAccent(menuAccent());
      oledDirty = true;
    }
  }
  if (sh) {
    if (wakeSwallow) wakeSwallow = false;                  // that press only woke the screen
    else { enterApp(1 + menuSel); return; }
  }
  titleTimeTick();
  menuClock(false);
  static uint32_t lastE = 0;
  if (clkEpoch != lastE) { lastE = clkEpoch; oledDirty = true; }
  if (statsFresh) { statsFresh = false; drawHomeStats(false); }
}

// ================= HOME SCREEN STYLES (Settings > Look > Home screen style) =================
// Ledger (the Psion list above) or one of these. All of them: turn = pick an app, press = open it, the PC's mouse
// points + clicks, and the OLED shows the app's card.
//   Seven   - a glassy desktop: icons on a wallpaper (or a clean desktop), a taskbar with a Start button + clock;
//             hold opens / closes the Start menu (apps on the left, Micor's picture + the time on the right)
//   Minimal - a big clock and one big app at a time
//   Micor   - Micor's den: his face, a speech bubble with what he thinks of each app, a shelf of paw buttons
//   Grid    - a phone-style home screen: 4 x 3 icons a page, status bar, page dots
const char DESK_QUIPS_P[] PROGMEM =
  "Let's write a story! About a wolf?\nNumbers in little boxes. So tidy!\nMath! I can count to... lots.\nJot it down before I forget! ...what?\n"
  "BEEP BOOP. I am a computer wolf.\nWhere did I bury that file...\nDice, timers, a flashlight. Handy!\nAwoo~! Let's make some tunes!\n"
  "The internet! Full of cat videos. Hmph.\nBonk! Bonk! Bonk! x3\nDon't get squished, froggy!\nPew pew! Space mice incoming!\n"
  "Ohh, it's me :3\nZoom zoom! Hold on to your ears!\nIs it snack time yet?\nHow fast is my brain going?\n"
  "*sniff sniff* ...I smell WiFi!\nMake me pretty! Or dark. Or both.\nPing! ...pong? Wrong app.\nRun, Micor, run! Wheee!\nOnce upon a time, there was a wolf... me!";
uint16_t mix565(uint16_t a, uint16_t b, int t) {     // t = 0 (a) .. 255 (b)
  int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31, br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  return ((ar + (br - ar) * t / 255) << 11) | ((ag + (bg - ag) * t / 255) << 5) | (ab + (bb - ab) * t / 255);
}
void vGrad(int x, int y, int w, int h, uint16_t top, uint16_t bot, int band) {
  for (int k = 0; k < h; k += band) tft.fillRect(x, y + k, w, min(band, h - k), mix565(top, bot, k * 255 / max(1, h - 1)));
}
void shadowText(int x, int y, const char* s, uint16_t fg) {   // see-through text with a 1 px shadow
  tft.setTextSize(1);
  tft.setTextColor(0); tft.setCursor(x + 1, y + 1); tft.print(s);
  tft.setTextColor(fg); tft.setCursor(x, y); tft.print(s);
}
void deskLabel(int cx, int y, int app, uint16_t fg, bool shadow) {   // an app's name, centred, at most 10 letters
  char b[12]; snprintf_P(b, sizeof b, PSTR("%.10s"), appName(app - 1));
  for (char* p = b + 1; *p; p++) *p = tolower((uint8_t)*p);
  int x = cx - (int)strlen(b) * 3;
  if (shadow) shadowText(x, y, b, fg); else { tft.setTextSize(1); tft.setTextColor(fg); tft.setCursor(x, y); tft.print(b); }
}
void deskIcon(int x, int y, int app, int s, uint16_t bg) {   // a rounded accent tile with the icon (s = 1, 2, 3)
  int side = 16 * s + 4 * s;
  tft.fillRoundRect(x, y, side, side, 3 * s + 1, appAccent(app));
  drawIcon16(app - 1, x + 2 * s, y + 2 * s, s, P.theme == TH_LCD ? C_LCD : 0xFFFF);
  (void)bg;
}
void deskClockText(char* b, size_t n) { int h, m, s; getHMS(&h, &m, &s); snprintf_P(b, n, P.h12 ? PSTR("%d:%02d") : PSTR("%02d:%02d"), dispHour(h) & 31, m & 63); }

// ---------- Seven ----------
const uint16_t S7_TOP = RGB565(10, 36, 86), S7_BOT = RGB565(30, 140, 170), S7_BAR1 = RGB565(36, 52, 74), S7_BAR2 = RGB565(12, 20, 32);
const int S7_COLS = 5, S7_ROWS = 3, S7_CW = 64, S7_CH = 68;
void s7Wall(int x, int y0, int w, int y1) {          // the wallpaper's gradient in one rectangle
  for (int y = y0; y < y1; y += 6) tft.fillRect(x, y, w, min(6, y1 - y), mix565(S7_TOP, S7_BOT, y * 255 / DOCK_Y));
}
void s7Wallpaper() {                                 // gradient + (clean desktop) a soft aurora swoosh
  s7Wall(0, 0, W, DOCK_Y);
  if (P.deskIcons) for (int k = 0; k < 7; k++) tft.drawCircleHelper(-40, 330, 300 + k, 0x1, mix565(S7_BOT, 0xFFFF, 60 + k * 14));
}
void s7IconCell(int i, bool sel) {                   // app i (0..19) in its cell on the current page
  int k = i % (S7_COLS * S7_ROWS), cx = 8 + (k % S7_COLS) * S7_CW, cy = 8 + (k / S7_COLS) * S7_CH;
  int y0 = cy, y1 = cy + S7_CH - 4;
  s7Wall(cx, y0, S7_CW - 4, y1);
  if (sel) {
    tft.fillRoundRect(cx, y0, S7_CW - 4, y1 - y0, 5, RGB565(66, 128, 196));
    tft.drawRoundRect(cx, y0, S7_CW - 4, y1 - y0, 5, RGB565(150, 200, 245));
  }
  deskIcon(cx + (S7_CW - 4) / 2 - 20, y0 + 6, i + 1, 2, 0);
  deskLabel(cx + (S7_CW - 4) / 2, y0 + 52, i + 1, 0xFFFF, true);
}
void s7Taskbar() {
  vGrad(0, DOCK_Y, W, H - DOCK_Y, S7_BAR1, S7_BAR2, 2);
  tft.drawFastHLine(0, DOCK_Y, W, RGB565(110, 140, 170));
  uint16_t orb = deskStart ? RGB565(90, 190, 255) : RGB565(40, 120, 210);   // the Start orb (a paw, not a flag)
  tft.fillCircle(16, DOCK_Y + 13, 11, RGB565(10, 40, 90));
  tft.fillCircle(16, DOCK_Y + 13, 10, orb);
  tft.fillCircle(16, DOCK_Y + 9, 6, mix565(orb, 0xFFFF, 120));
  drawPaw(16, DOCK_Y + 11, 1, 0xFFFF);
  for (int k = 0; k < 7; k++) {                      // pinned apps
    int x = 36 + k * 30, a = DOCK_APPS[k];
    bool sel = a == 1 + menuSel;
    if (sel) { tft.fillRoundRect(x, DOCK_Y + 2, 28, 22, 3, RGB565(70, 110, 160)); tft.drawRoundRect(x, DOCK_Y + 2, 28, 22, 3, RGB565(160, 200, 240)); }
    tft.fillRoundRect(x + 5, DOCK_Y + 4, 18, 18, 3, appAccent(a));
    drawIcon16(a - 1, x + 6, DOCK_Y + 5, 1, 0xFFFF);
  }
  char b[12]; deskClockText(b, sizeof b);
  tft.setTextSize(1); tft.setTextColor(0xFFFF, S7_BAR2);
  tft.setCursor(W - 6 - (int)strlen(b) * 6, DOCK_Y + 5); tft.print(b);
  fmtDate(b, sizeof b, (int)(clkEpoch / 86400UL));
  char d[8]; snprintf_P(d, sizeof d, PSTR("%.6s"), b + (P.dateFmt == 0 ? 4 : 0));
  tft.setCursor(W - 6 - (int)strlen(d) * 6, DOCK_Y + 15); tft.print(d);
}
void s7StartRow(int k) {                             // Start menu: row k of the visible list
  int i = deskStartTop + k, y = 30 + k * 18;
  bool sel = i == menuSel;
  tft.fillRect(4, y, 134, 18, sel ? RGB565(204, 228, 247) : 0xFFFF);
  if (sel) tft.drawRect(4, y, 134, 18, RGB565(120, 170, 220));
  if (i > MENU_N) return;
  if (i == MENU_N) { putText(28, y + 5, 1, C_INK, sel ? RGB565(204, 228, 247) : 0xFFFF, FS("Sleep")); tft.fillCircle(16, y + 9, 6, RGB565(200, 70, 50)); tft.drawFastVLine(16, y + 4, 5, 0xFFFF); return; }
  tft.fillRoundRect(8, y + 1, 16, 16, 3, appAccent(i + 1));
  drawIcon16(i, 8, y + 1, 1, 0xFFFF);
  char b[16]; snprintf_P(b, sizeof b, PSTR("%.14s"), appName(i));
  for (char* p = b + 1; *p; p++) *p = tolower((uint8_t)*p);
  putText(28, y + 5, 1, C_INK, sel ? RGB565(204, 228, 247) : 0xFFFF, b);
}
void s7StartMenu() {
  tft.fillRoundRect(2, 24, 220, DOCK_Y - 22, 6, RGB565(30, 60, 110));
  tft.drawRoundRect(2, 24, 220, DOCK_Y - 22, 6, RGB565(140, 180, 230));
  tft.fillRect(4, 28, 136, DOCK_Y - 30, 0xFFFF);
  for (int k = 0; k < 10; k++) s7StartRow(k);
  micorFace(152, 32, 1, 0, false);                   // the "user picture"
  tft.drawRect(151, 31, 54, 54, 0xFFFF);
  tft.setTextColor(0xFFFF, RGB565(30, 60, 110));
  tft.setTextSize(1);
  tft.setCursor(146, 92); tft.print(P.owner[0] ? P.owner : "Micor");
  char b[16]; deskClockText(b, sizeof b);
  tft.setTextSize(2); tft.setCursor(150, 112); tft.print(b);
  tft.setTextSize(1);
  for (int k = 0; k < 4; k++) { tft.setCursor(150, 142 + k * 15); tft.print(pgmItem(PSTR("Documents\nPictures\nComputer\nHelp"), k)); }
}
void s7Draw() {
  deskPage = menuSel / (S7_COLS * S7_ROWS);
  s7Wallpaper();
  if (!P.deskIcons) {
    int first = deskPage * S7_COLS * S7_ROWS, last = min(MENU_N, first + S7_COLS * S7_ROWS);
    for (int i = first; i < last; i++) s7IconCell(i, i == menuSel);
    if (MENU_N > S7_COLS * S7_ROWS) for (int p = 0; p < 2; p++) tft.fillCircle(W - 22 + p * 10, DOCK_Y - 8, 3, p == deskPage ? 0xFFFF : RGB565(90, 130, 170));
  } else {                                           // a clean desktop: a clock gadget + a hint
    tft.fillRoundRect(206, 14, 104, 104, 12, RGB565(20, 50, 90));
    tft.drawRoundRect(206, 14, 104, 104, 12, RGB565(140, 190, 240));
    int h, m, s; getHMS(&h, &m, &s);
    drawAnalogClock(258, 66, 40, h, m, s, true, RGB565(120, 200, 255));
    shadowText(10, DOCK_Y - 14, "press: Start menu", 0xFFFF);
  }
  if (deskStart) s7StartMenu();
  s7Taskbar();
}
void s7Select(int old) {                             // the selection moved
  if (deskStart) {
    int oldTop = deskStartTop;
    if (menuSel < deskStartTop) deskStartTop = menuSel; else if (menuSel >= deskStartTop + 10) deskStartTop = menuSel - 9;
    if (oldTop != deskStartTop) for (int k = 0; k < 10; k++) s7StartRow(k);
    else { if (old >= deskStartTop && old < deskStartTop + 10) s7StartRow(old - deskStartTop); s7StartRow(menuSel - deskStartTop); }
    return;
  }
  if (P.deskIcons || menuSel >= MENU_N) return;
  if (menuSel / (S7_COLS * S7_ROWS) != deskPage) { s7Draw(); return; }
  if (old >= 0 && old < MENU_N && old / (S7_COLS * S7_ROWS) == deskPage) s7IconCell(old, false);
  s7IconCell(menuSel, true);
  s7Taskbar();
}

// ---------- Minimal ----------
void mnDrawApp() {
  tft.fillRect(0, 88, W, 128, C_LCD);
  int a = 1 + menuSel;
  deskIcon(W / 2 - 30, 92, a, 3, C_LCD);
  char b[24]; snprintf_P(b, sizeof b, PSTR("%s"), appName(menuSel));
  centerText(b, 160, 2, C_INK, C_LCD);
  snprintf_P(b, sizeof b, PSTR("%.24s"), pgmItem(APP_DESC_P, menuSel));
  centerText(b, 182, 1, C_MID, C_LCD);
  tft.fillTriangle(30, 122, 40, 112, 40, 132, C_MID);
  tft.fillTriangle(290, 122, 280, 112, 280, 132, C_MID);
  for (int i = 0; i < MENU_N; i++) tft.fillCircle(W / 2 - MENU_N * 4 + i * 8 + 4, 204, i == menuSel ? 3 : 1, i == menuSel ? appAccent(a) : C_MID);
}
void mnDrawClock() {
  char b[12]; deskClockText(b, sizeof b);
  tft.fillRect(0, 10, W, 70, C_LCD);
  centerText(b, 16, 5, C_INK, C_LCD);
  fmtDate(b, sizeof b, (int)(clkEpoch / 86400UL));
  centerText(b, 62, 1, C_MID, C_LCD);
}
void mnDraw() { tft.fillScreen(C_LCD); mnDrawClock(); mnDrawApp(); putText(4, 228, 1, C_MID, C_LCD, FS("turn: apps   press: open")); }

// ---------- Micor's den ----------
const uint16_t MC_BG1 = RGB565(70, 44, 110), MC_BG2 = RGB565(236, 140, 170);
void mcBackground() {
  vGrad(0, 0, W, H, MC_BG1, MC_BG2, 8);
  for (int k = 0; k < 9; k++) drawPaw(20 + (k * 97) % 290, 30 + (k * 61) % 180, 1, mix565(MC_BG1, MC_BG2, 128 + (k & 3) * 20));
}
void mcBubble() {
  int a = 1 + menuSel;
  tft.fillRoundRect(122, 14, 192, 78, 10, 0xFFFF);
  tft.fillTriangle(122, 60, 108, 78, 134, 70, 0xFFFF);
  char b[24]; snprintf_P(b, sizeof b, PSTR("%s"), appName(menuSel));
  putText(132, 22, 2, appAccent(a), 0xFFFF, b);
  char q[48]; snprintf_P(q, sizeof q, PSTR("%s"), pgmItem(DESK_QUIPS_P, menuSel));
  char l1[31], l2[31];                               // two lines of up to 30 letters
  int cut = strlen(q) <= 30 ? strlen(q) : 30;
  if ((int)strlen(q) > 30) { while (cut > 10 && q[cut] != ' ') cut--; }
  snprintf_P(l1, sizeof l1, PSTR("%.*s"), cut, q);
  snprintf_P(l2, sizeof l2, PSTR("%.30s"), q[cut] == ' ' ? q + cut + 1 : q + cut);
  putText(132, 50, 1, RGB565(40, 30, 60), 0xFFFF, l1);
  putText(132, 62, 1, RGB565(40, 30, 60), 0xFFFF, l2);
  putText(132, 78, 1, RGB565(150, 120, 170), 0xFFFF, FS("press to pounce!"));
}
void mcShelf() {                                     // 5 paw buttons, the chosen one in the middle and big
  vGrad(0, 168, W, 72, mix565(MC_BG1, MC_BG2, 190), MC_BG2, 8);
  for (int k = -2; k <= 2; k++) {
    int i = ((menuSel + k) % MENU_N + MENU_N) % MENU_N, cx = 160 + k * 60, cy = 202;
    if (k == 0) { tft.fillCircle(cx, cy, 25, 0xFFFF); tft.fillCircle(cx, cy, 22, appAccent(i + 1)); drawIcon16(i, cx - 16, cy - 16, 2, 0xFFFF); }
    else { tft.fillCircle(cx, cy, 15, appAccent(i + 1)); drawIcon16(i, cx - 8, cy - 8, 1, 0xFFFF); }
  }
}
void mcFace() { static const uint8_t EX[21] = {0, 1, 1, 1, 2, 1, 1, 0, 1, 2, 0, 2, 0, 2, 1, 1, 1, 0, 1, 2, 0}; micorFace(8, 24, 2, EX[menuSel % 21], false); }
void mcDraw() {
  mcBackground();
  mcFace();
  mcBubble();
  mcShelf();
  char b[12]; deskClockText(b, sizeof b);
  shadowText(132, 100, b, 0xFFFF);
  shadowText(8, 136, "turn: sniff around", 0xFFFF);
}

// ---------- Grid (phone) ----------
const int GR_COLS = 4, GR_ROWS = 3, GR_CW = 78, GR_CH = 64;
void grStatus() {
  tft.fillRect(0, 0, W, 16, 0);
  char b[12]; deskClockText(b, sizeof b);
  putText(6, 4, 1, 0xFFFF, 0, b);
  bool on = WiFi.status() == WL_CONNECTED;
  for (int k = 0; k < 3; k++) tft.fillRect(W - 30 + k * 6, 12 - k * 3 - 2, 4, k * 3 + 2, on ? 0xFFFF : RGB565(90, 90, 90));
  if (P.owner[0]) putText(W / 2 - (int)strlen(P.owner) * 3, 4, 1, RGB565(200, 200, 220), 0, P.owner);
}
void grCell(int i, bool sel) {
  int k = i % (GR_COLS * GR_ROWS), x = 4 + (k % GR_COLS) * GR_CW, y = 22 + (k / GR_COLS) * GR_CH;
  vGrad(x, y, GR_CW, GR_CH, mix565(RGB565(60, 30, 120), RGB565(20, 110, 190), y * 255 / H), mix565(RGB565(60, 30, 120), RGB565(20, 110, 190), (y + GR_CH) * 255 / H), 8);
  int ix = x + GR_CW / 2 - 20, iy = y + 4;
  if (sel) tft.drawRoundRect(ix - 3, iy - 3, 46, 46, 10, 0xFFFF);
  deskIcon(ix, iy, i + 1, 2, 0);
  deskLabel(x + GR_CW / 2, y + 50, i + 1, 0xFFFF, true);
}
void grDraw() {
  deskPage = menuSel / (GR_COLS * GR_ROWS);
  vGrad(0, 16, W, H - 16, RGB565(60, 30, 120), RGB565(20, 110, 190), 8);
  grStatus();
  int first = deskPage * GR_COLS * GR_ROWS, last = min(MENU_N, first + GR_COLS * GR_ROWS);
  for (int i = first; i < last; i++) grCell(i, i == menuSel);
  int pages = (MENU_N + GR_COLS * GR_ROWS - 1) / (GR_COLS * GR_ROWS);
  for (int p = 0; p < pages; p++) tft.fillCircle(W / 2 - (pages - 1) * 6 + p * 12, 228, 3, p == deskPage ? 0xFFFF : RGB565(120, 120, 170));
}

// ---------- the dispatch ----------
void deskDraw() {
  switch (P.deskStyle % DS_N) {
    case DS_SEVEN: s7Draw(); break;
    case DS_MINIMAL: mnDraw(); break;
    case DS_MICOR: mcDraw(); break;
    case DS_GRID: grDraw(); break;
  }
}
void deskMoved(int old) {                            // redraw just what the new selection changes
  switch (P.deskStyle % DS_N) {
    case DS_SEVEN: s7Select(old); break;
    case DS_MINIMAL: mnDrawApp(); break;
    case DS_MICOR: mcFace(); mcBubble(); mcShelf(); break;
    case DS_GRID:
      if (menuSel / (GR_COLS * GR_ROWS) != deskPage) grDraw();
      else { if (old >= 0 && old / (GR_COLS * GR_ROWS) == deskPage) grCell(old, false); grCell(menuSel, true); }
      break;
  }
}
void deskClockTick() {                               // once a minute
  static int lastM = -1;
  int h, m, s; getHMS(&h, &m, &s);
  if (m == lastM) return;
  lastM = m;
  switch (P.deskStyle % DS_N) {
    case DS_SEVEN: s7Taskbar(); if (deskStart) s7StartMenu(); break;
    case DS_MINIMAL: mnDrawClock(); break;
    case DS_MICOR: { char b[12]; deskClockText(b, sizeof b); tft.fillRect(130, 98, 60, 12, mix565(MC_BG1, MC_BG2, 110)); shadowText(132, 100, b, 0xFFFF); break; }
    case DS_GRID: grStatus(); break;
  }
}
void deskOpen() {                                    // press
  if (deskStart && menuSel == MENU_N) { deskStart = false; menuSel = MENU_N - 1; sleepEnter(); return; }
  if (P.deskStyle % DS_N == DS_SEVEN && P.deskIcons && !deskStart) { deskStart = true; deskStartTop = max(0, min(menuSel, MENU_N + 1 - 10)); s7Draw(); return; }
  deskStart = false;
  enterApp(1 + menuSel);
}
void deskHold() {                                    // hold on the home screen: Seven opens / closes the Start menu
  if (P.deskStyle % DS_N != DS_SEVEN) return;
  deskStart = !deskStart;
  if (!deskStart && menuSel >= MENU_N) menuSel = MENU_N - 1;
  deskStartTop = max(0, min(menuSel, MENU_N + 1 - 10));
  s7Draw(); SFX(T_HOP);
}
void deskTurn(int c) {
  if (P.deskStyle % DS_N == DS_SEVEN && P.deskIcons && !deskStart) { deskHold(); return; }   // clean desktop: the Start menu
  int n = deskStart ? MENU_N + 1 : MENU_N, old = menuSel;
  menuSel = ((menuSel + c) % n + n) % n;
  if (menuSel != old) { uiClick(); deskMoved(old); oledDirty = true; }
}
int deskHit(int x, int y) {                          // the PC's mouse: which app is at (x, y)? -1 none, -2 Start, -3 Micor
  switch (P.deskStyle % DS_N) {
    case DS_SEVEN:
      if (y >= DOCK_Y) {
        if (x < 30) return -2;
        int k = (x - 36) / 30;
        return (x >= 36 && k < 7) ? DOCK_APPS[k] - 1 : -1;
      }
      if (deskStart) return (x >= 4 && x < 138 && y >= 30 && y < 30 + 10 * 18) ? deskStartTop + (y - 30) / 18 : -1;
      if (P.deskIcons) return -1;
      if (x >= 8 && y >= 8 && x < 8 + S7_COLS * S7_CW && y < 8 + S7_ROWS * S7_CH) {
        int i = deskPage * S7_COLS * S7_ROWS + ((y - 8) / S7_CH) * S7_COLS + (x - 8) / S7_CW;
        return i < MENU_N ? i : -1;
      }
      return -1;
    case DS_MINIMAL:
      if (y < 88 || y > 200) return -1;
      if (x < 80) return (menuSel + MENU_N - 1) % MENU_N;
      if (x > 240) return (menuSel + 1) % MENU_N;
      return menuSel;
    case DS_MICOR:
      if (x < 112 && y > 20 && y < 132) return -3;
      if (y > 175) { int k = (int)lround((x - 160) / 60.0); return ((menuSel + constrain(k, -2, 2)) % MENU_N + MENU_N) % MENU_N; }
      return -1;
    case DS_GRID:
      if (y >= 22 && y < 22 + GR_ROWS * GR_CH && x >= 4 && x < 4 + GR_COLS * GR_CW) {
        int i = deskPage * GR_COLS * GR_ROWS + ((y - 22) / GR_CH) * GR_COLS + (x - 4) / GR_CW;
        return i < MENU_N ? i : -1;
      }
      return -1;
  }
  return -1;
}
void deskPtr(int x, int y, bool click) {             // hover = select, click = open (Seven's Start orb toggles)
  int i = deskHit(x, y);
  if (i == -2) { if (click) deskHold(); return; }
  if (i == -3) { if (click) { micorFace(8, 24, 2, random(4), random(2)); SFX(T_HOP); } return; }
  if (i < 0) return;
  int st = P.deskStyle % DS_N;
  if (st == DS_MINIMAL || st == DS_MICOR) {          // these scroll under the mouse: clicks only (sides move, middle opens)
    if (!click) return;
    if (i != menuSel) { int old = menuSel; menuSel = i; uiClick(); deskMoved(old); oledDirty = true; return; }
    uiClick(); deskOpen(); return;
  }
  if (i != menuSel) { int old = menuSel; menuSel = i; deskMoved(old); oledDirty = true; }
  if (click) { uiClick(); deskOpen(); }
}

// ================= GAME SETUP =================
// Corporate preference panel: app card on the left, a tabbed "Preferences" ledger with gauges on
// the right, a "Controls" tab under it and a flat accent Start button.
void startSelectedGame();
const char* const SET_LEVELS[3] = {"EASY", "NORMAL", "HARD"};
const char* const SET_SPEEDS[3] = {"SLOW", "NORMAL", "FAST"};
const char* const RACE_MODES[2] = {"FREEWAY", "RACE"};
const char* const RACE_TRACKS[3] = {"SUNNY MEADOW", "PINE FOREST", "MOON ROAD"};
const char SETUP_HELP_P[] PROGMEM = "turn: move your paddle\npress: pause / resume\nfirst to 7 wins! x3\nturn: sidestep\npress: hop forward\n5 frogs home = level up\nturn: move the fox ship\npress: pew pew!\nclear every mouse wave\nturn: walk around\npress: switch L/R - U/D\npress twice: open pocket\nturn: steer   press: turbo\ngrab fish = more turbo\ndodge cones, beat rivals!";
const int SET_RACE_ORDER[5] = {SET_DIFFICULTY, SET_SPEED, SET_MODE, SET_TRACK, SET_START};
const int SET_GAME_ORDER[3] = {SET_DIFFICULTY, SET_SPEED, SET_START};
const int SET_X = 106, SET_W = 206, SET_ROW = 22, SET_ROWS_Y = 38;

int gameSetupRows() { return gameSetupApp == APP_RACE ? 5 : 3; }
int setupItemAt(int pos) { return gameSetupApp == APP_RACE ? SET_RACE_ORDER[pos] : SET_GAME_ORDER[pos]; }
int setupPosOf(int item) { for (int p = 0; p < gameSetupRows(); p++) if (setupItemAt(p) == item) return p; return 0; }
const char* setupItemName(int item) {
  switch (item) {
    case SET_DIFFICULTY: return "DIFFICULTY";
    case SET_SPEED:      return "SPEED";
    case SET_MODE:       return "MODE";
    case SET_TRACK:      return "TRACK";
    default:             return "START";
  }
}
void drawTab(int x, int y, const char* label, uint16_t acc) {   // draw after the panel frame: tab opens into it
  int tw = (int)strlen(label) * 6 + 16;
  tft.fillRect(x, y, tw, 14, E_WHITE);
  tft.fillRect(x, y, tw, 2, acc);
  tft.drawFastVLine(x, y, 14, E_RULE); tft.drawFastVLine(x + tw - 1, y, 14, E_RULE);
  putText(x + 8, y + 4, 1, C_INK, E_WHITE, label);
}
void drawSetupItem(int pos) {
  int item = setupItemAt(pos);
  bool sel = item == gameSetupSel;
  uint16_t acc = appAccent(gameSetupApp);
  if (item == SET_START) {
    const int y = 184, h = 24;
    tft.fillRect(SET_X, y, SET_W, h, sel ? acc : E_WHITE);
    tft.drawRect(SET_X, y, SET_W, h, sel ? C_INK : acc);
    if (sel) tft.drawRect(SET_X + 1, y + 1, SET_W - 2, h - 2, C_INK);
    const char* s = gameSetupApp == APP_RACE ? "Start race  >" : "Start game  >";
    putText(SET_X + (SET_W - (int)strlen(s) * 12) / 2, y + 5, 2, sel ? E_ONACC : acc, sel ? acc : E_WHITE, s);
    return;
  }
  int y = SET_ROWS_Y + pos * SET_ROW, lvl = -1;
  uint16_t bg = (pos & 1) ? E_ROW2 : E_WHITE;
  const char* value;
  switch (item) {
    case SET_DIFFICULTY: lvl = gameDifficulty[gameSetupApp] % 3; value = SET_LEVELS[lvl]; break;
    case SET_SPEED:      lvl = gameSpeed[gameSetupApp] % 3;      value = SET_SPEEDS[lvl]; break;
    case SET_MODE:       value = RACE_MODES[raceMode % 2]; break;
    default:             value = RACE_TRACKS[raceTrack % 3]; break;
  }
  tft.fillRect(SET_X, y, SET_W, SET_ROW, bg);
  putText(SET_X + 6, y + 7, 1, C_INK, bg, setupItemName(item));
  if (lvl >= 0) {                                   // 3-segment status gauge
    const uint16_t risk[3] = {E_MINT, E_AMBER, E_CORAL};
    uint16_t on = item == SET_DIFFICULTY ? risk[lvl] : acc;
    for (int k = 0; k < 3; k++) {
      int gx = SET_X + 74 + k * 20;
      tft.fillRect(gx, y + 7, 18, 8, k <= lvl ? on : E_HEAD);
      tft.drawRect(gx, y + 7, 18, 8, k <= lvl ? on : E_RULE);
    }
  }
  int vx = SET_X + SET_W - 16 - (int)strlen(value) * 6;
  putText(vx, y + 7, 1, sel ? acc : C_INK, bg, value);
  putText(vx - 10, y + 7, 1, sel ? acc : bg, bg, FS("<"));
  putText(SET_X + SET_W - 10, y + 7, 1, sel ? acc : bg, bg, FS(">"));
  if (sel) { tft.drawRect(SET_X, y, SET_W, SET_ROW, acc); tft.drawRect(SET_X + 1, y + 1, SET_W - 2, SET_ROW - 2, acc); }
}
void drawGameSetup() {
  tft.filter = 0;
  int a = gameSetupApp;
  bool race = a == APP_RACE;
  uint16_t acc = appAccent(a);
  char name[12];
  snprintf_P(name, sizeof name, PSTR("%s"), race ? FS("RALLY") : appName(a - 1));   // a copy: the ring buffer is reused below
  char title[24];
  snprintf_P(title, sizeof title, PSTR("%s Setup"), name);
  tft.fillScreen(C_LCD);
  drawTitleBar(title, acc);
  // app card
  tft.fillRect(5, 22, 96, 187, E_WHITE);
  tft.drawRect(5, 22, 96, 187, E_RULE);
  tft.fillRect(5, 22, 96, 3, acc);
  tft.fillRoundRect(21, 32, 64, 56, 6, acc);
  drawIconGlyph(a - APP_PONG, 21, 32, E_ONACC, acc);
  int sz = strlen(name) * 12 <= 88 ? 2 : 1;
  putText(53 - (int)strlen(name) * 3 * sz, 96, sz, C_INK, E_WHITE, name);
  wrapText(pgmItem(APP_DESC_P, a - 1), 14, 118, 13, 4, C_MID, E_WHITE);
  tft.drawFastHLine(13, 162, 80, E_RULE);
  putText(14, 170, 1, C_MID, E_WHITE, FS("type"));
  putText(92 - (int)strlen(appKind(a - 1)) * 6, 170, 1, acc, E_WHITE, appKind(a - 1));
  putText(14, 184, 1, C_MID, E_WHITE, FS("hold"));
  putText(68, 184, 1, C_INK, E_WHITE, FS("back"));
  // preferences tab
  int rows = gameSetupRows() - 1;
  tft.drawRect(SET_X - 1, SET_ROWS_Y - 1, SET_W + 2, rows * SET_ROW + 2, E_RULE);
  drawTab(SET_X - 1, 24, FS("Preferences"), acc);
  for (int p = 0; p < gameSetupRows(); p++) drawSetupItem(p);
  // controls tab
  int cy = SET_ROWS_Y + rows * SET_ROW + 8, ch = 178 - (cy + 14);
  tft.fillRect(SET_X, cy + 14, SET_W, ch, E_WHITE);
  tft.drawRect(SET_X - 1, cy + 13, SET_W + 2, ch + 2, E_RULE);
  drawTab(SET_X - 1, cy, FS("Controls"), acc);
  int gap = race ? 9 : 16;
  for (int k = 0; k < 3; k++) putText(SET_X + 8, cy + (race ? 17 : 18) + k * gap, 1, C_INK, E_WHITE, pgmItem(SETUP_HELP_P, (a - APP_PONG) * 3 + (k)));
  drawDock(a);
}
void gameSetupTick() {
  int clicks = takeClicks();
  if (clicks) {
    int rows = gameSetupRows(), oldPos = setupPosOf(gameSetupSel);
    int pos = ((oldPos + clicks * ENC_DIR) % rows + rows) % rows;
    gameSetupSel = setupItemAt(pos);
    if (pos != oldPos) { drawSetupItem(oldPos); drawSetupItem(pos); uiClick(); oledDirty = true; }
  }
  titleTimeTick();
  takePress();
  if (!takeShort()) return;
  if (gameSetupSel == SET_START) { startSelectedGame(); return; }
  switch (gameSetupSel) {
    case SET_DIFFICULTY: gameDifficulty[gameSetupApp] = (gameDifficulty[gameSetupApp] + 1) % 3; break;
    case SET_SPEED:      gameSpeed[gameSetupApp] = (gameSpeed[gameSetupApp] + 1) % 3; break;
    case SET_MODE:       raceMode = (raceMode + 1) % 2; break;
    case SET_TRACK:      raceTrack = (raceTrack + 1) % 3; break;
  }
  drawSetupItem(setupPosOf(gameSetupSel));
  SFX(T_HOP);
  oledDirty = true;
}

// ================= PONG =================
const int PW = 8, PH = 44, PBR = 6;
const int PLY_X = 12, CPU_X = W - 12 - PW;
const float PMAX_Y = H - PH;
const int WIN_SCORE = 7;
const float BASE_SPEED = 3.0f, MAX_SPEED = 7.0f;
const int PX_PER_CLICK = 14;
enum { PS_TITLE, PS_SERVE, PS_PLAY, PS_POINT, PS_PAUSED, PS_OVER };
int pState = PS_TITLE;
unsigned long pStateT = 0, pLast = 0;
int pBlink = -1;
int pScore = 0, cScore = 0, rally = 0, bestRally = 0, pServeDir = 1;
float pY = 98, cY = 98, pbx = 160, pby = 120, pvx = 0, pvy = 0, pSpd = 3.0f, aiAim = 0;
int oPY = 0, oCY = 0, oBx = 0, oBy = 0;
bool cpuSleepy = false;
const char WIN_MSGS_P[] PROGMEM = "YIPPEEE!!\nhehe gotcha :3\nOWO nice!\nmrrp! point!";
const char LOSE_MSGS_P[] PROGMEM = "nyooo >w<\nouchie ;-;\npaws slipped!\nbonk... ;-;";

int pongLevel() { return (int)((pSpd - BASE_SPEED) / 0.5f) + 1; }
void pongCenterLine() { for (int y = 4; y < H; y += 16) tft.fillRect(W / 2 - 1, y, 3, 8, K_DIM); }
void pongField() { tft.fillScreen(K_BG); pongCenterLine(); }
void pongYarn(int x, int y) {
  tft.fillCircle(x, y, PBR, K_PINK);
  tft.drawLine(x - 4, y - 2, x + 3, y + 4, K_CREAM);
  tft.drawLine(x - 3, y - 4, x + 4, y + 2, K_CREAM);
  tft.drawLine(x - 5, y + 1, x + 1, y - 5, K_DPINK);
}
void pongErasePad(int x, int oy, int ny) {
  if (ny > oy)      tft.fillRect(x, oy, PW, ny - oy, K_BG);
  else if (ny < oy) tft.fillRect(x, ny + PH, PW, oy - ny, K_BG);
}
void pongRender(bool force) {
  int py = (int)pY, cy = (int)cY, nx = (int)pbx, ny = (int)pby;
  bool moved = force || nx != oBx || ny != oBy;
  if (moved) {
    tft.fillCircle(oBx, oBy, PBR, K_BG);
    if (abs(oBx - W / 2) < PBR + 3) pongCenterLine();
  }
  pongErasePad(PLY_X, oPY, py);
  pongErasePad(CPU_X, oCY, cy);
  tft.fillRect(PLY_X, py, PW, PH, K_ORANGE);
  tft.fillRect(CPU_X, cy, PW, PH, K_CYAN);
  if (moved) pongYarn(nx, ny);
  oBx = nx; oBy = ny; oPY = py; oCY = cy;
}
void pongTitle() {
  tft.fillScreen(K_BG);
  centerText(FS("PONG"), 30, 8, K_ORANGE, K_BG);
  centerText(FS("fox vs robo-kitty"), 110, 2, K_CYAN, K_BG);
  drawPaw(50, 170, 4, K_PINK); drawPaw(270, 170, 4, K_PINK);
  pBlink = -1;
}
void pongServe(int dir) {
  pbx = W / 2; pby = H / 2; pSpd = BASE_SPEED * gameSpeedFactor(APP_PONG);
  float a = random(-30, 31) * 0.01745f;
  pvx = dir * pSpd * cosf(a); pvy = pSpd * sinf(a);
  rally = 0; cpuSleepy = (random(6) == 0); aiAim = 0; mood = ":3";
  pongField(); pongRender(true);
  pState = PS_SERVE; pStateT = millis(); oledDirty = true;
}
void pongNew() {
  pScore = 0; cScore = 0; pY = cY = (H - PH) / 2;
  SFX(T_START);
  pongServe(random(2) ? 1 : -1);
}
void pongPoint(bool byPlayer) {
  tft.fillCircle(oBx, oBy, PBR, K_BG);
  if (byPlayer) { pScore++; mood = "^w^"; SFX(T_YOU); msgBox(pgmItem(WIN_MSGS_P, random(4)), K_YELLOW, K_BG); }
  else          { cScore++; mood = ";-;"; SFX(T_CPU); msgBox(pgmItem(LOSE_MSGS_P, random(4)), K_PINK, K_BG); }
  pServeDir = byPlayer ? 1 : -1;
  pState = PS_POINT; pStateT = millis(); oledDirty = true;
}
void pongOver() {
  bool won = pScore > cScore;
  pState = PS_OVER; pStateT = millis();
  mood = won ? "x3" : ">w<";
  if (won) SFX(T_WIN); else SFX(T_LOSE);
  uint16_t col = won ? K_YELLOW : K_PINK;
  tft.fillRect(30, 80, 260, 80, K_BG);
  tft.drawRect(30, 80, 260, 80, col);
  centerText(won ? FS("YOU WIN!! x3") : FS("CPU WINS ;-;"), 92, 3, col, K_BG);
  pBlink = -1; oledDirty = true;
}
void pongHit(bool playerSide) {
  float padY = playerSide ? pY : cY;
  float rel = (pby - (padY + PH / 2.0f)) / (PH / 2.0f + PBR);
  rel = constrain(rel, -1.0f, 1.0f);
  float a = rel * 0.95f;
  pSpd += 0.25f * gameSpeedFactor(APP_PONG);
  float maxSpeed = MAX_SPEED * gameSpeedFactor(APP_PONG);
  if (pSpd > maxSpeed) pSpd = maxSpeed;
  pvx = pSpd * cosf(a) * (playerSide ? 1 : -1);
  pvy = pSpd * sinf(a);
  pbx = playerSide ? (PLY_X + PW + PBR) : (CPU_X - PBR);
  if (playerSide) { rally++; if (rally > bestRally) bestRally = rally; aiAim = random(-PH / 3, PH / 3); oledDirty = true; }
  SFX(T_HIT);
}
void pongUpdate() {
  float target = (pvx > 0) ? (pby - PH / 2.0f + aiAim) : (H / 2.0f - PH / 2.0f);
  float maxStep = (2.2f + 0.15f * pongLevel()) * gameDifficultyFactor(APP_PONG);
  if (maxStep > 3.6f) maxStep = 3.6f;
  if (cpuSleepy) maxStep *= 0.45f;
  float diff = target - cY;
  if (fabsf(diff) > 2.0f) cY += constrain(diff, -maxStep, maxStep);
  cY = constrain(cY, 0.0f, PMAX_Y);
  pbx += pvx; pby += pvy;
  if (pby - PBR < 0)     { pby = PBR;          pvy =  fabsf(pvy); SFX(T_WALL); }
  if (pby + PBR > H - 1) { pby = H - 1 - PBR;  pvy = -fabsf(pvy); SFX(T_WALL); }
  if (pvx < 0 && pbx - PBR <= PLY_X + PW && pbx + PBR >= PLY_X && pby + PBR >= pY && pby - PBR <= pY + PH) pongHit(true);
  else if (pvx > 0 && pbx + PBR >= CPU_X && pbx - PBR <= CPU_X + PW && pby + PBR >= cY && pby - PBR <= cY + PH) pongHit(false);
  if (pbx + PBR < 0)      pongPoint(false);
  else if (pbx - PBR > W) pongPoint(true);
}
void pongEnter() {
  pScore = 0; cScore = 0; rally = 0; pSpd = BASE_SPEED; pY = cY = (H - PH) / 2;
  pongTitle(); pState = PS_TITLE;
}
void pongStatus(char* out, size_t n) {
  switch (pState) {
    case PS_TITLE:  snprintf_P(out, n, PSTR("press knob to start")); break;
    case PS_SERVE:
    case PS_POINT:  snprintf_P(out, n, PSTR("get ready!")); break;
    case PS_PLAY:
      if (cpuSleepy) snprintf_P(out, n, PSTR("cpu is sleepy zZ"));
      else           snprintf_P(out, n, PSTR("level %d  to %d wins"), pongLevel(), WIN_SCORE);
      break;
    case PS_PAUSED: snprintf_P(out, n, PSTR("paused :3")); break;
    default:        snprintf_P(out, n, PSTR("press knob again")); break;
  }
}
void pongTick(uint32_t now) {
  bool sp = takeShort();
  takePress();
  int clicks = takeClicks();
  if (pState == PS_SERVE || pState == PS_PLAY || pState == PS_POINT) {
    pY += clicks * ENC_DIR * PX_PER_CLICK;
    pY = constrain(pY, 0.0f, PMAX_Y);
  }
  switch (pState) {
    case PS_TITLE:
    case PS_OVER: {
      int b = (now / 500) % 2;
      if (b != pBlink) { pBlink = b; centerText(FS("press the knob!"), pState == PS_TITLE ? 190 : 130, 2, b ? K_CREAM : K_BG, K_BG); }
      if (sp) pongNew();
      break;
    }
    case PS_SERVE:
      if (now - pLast >= FRAME_INTERVAL_MS) { pLast = now; pongRender(false); }
      if (now - pStateT >= 900) { pState = PS_PLAY; oledDirty = true; }
      break;
    case PS_PLAY:
      if (sp) { pState = PS_PAUSED; stopSound(); msgBox(FS("PAUSED :3"), K_CYAN, K_BG); oledDirty = true; break; }
      if (now - pLast >= FRAME_INTERVAL_MS) { pLast = now; pongUpdate(); if (pState == PS_PLAY) pongRender(false); }
      break;
    case PS_POINT:
      if (now - pStateT >= 1400) { if (pScore >= WIN_SCORE || cScore >= WIN_SCORE) pongOver(); else pongServe(pServeDir); }
      break;
    case PS_PAUSED:
      if (sp) { pongField(); pongRender(true); pState = PS_PLAY; startAppMusic(APP_PONG); oledDirty = true; }
      break;
  }
}

// ================= FROGGER =================
const int F_ROW = 20, F_FW = 14;
const uint8_t F_TYPE[12] = {0, 3, 4, 3, 4, 0, 1, 2, 1, 2, 1, 0};
const uint8_t F_N[12]    = {0, 3, 3, 3, 3, 0, 3, 3, 4, 3, 4, 0};
const uint8_t F_W[12]    = {0, 70, 72, 100, 72, 0, 24, 44, 24, 44, 24, 0};
const float   F_V[12]    = {0, 1.2f, -1.6f, 2.0f, -1.3f, 0, -2.2f, 1.6f, 3.0f, -1.5f, 2.4f, 0};
const uint16_t F_COL[12] = {0, 0, 0, 0, 0, 0, RGB565(230,50,60), RGB565(240,130,40), RGB565(240,200,40),
                            RGB565(60,140,230), RGB565(190,70,200), 0};
enum { FS_TITLE, FS_PLAY, FS_PAUSE, FS_OVER };
int fState = FS_TITLE, fAfter = 0, fBlink = -1;
unsigned long fStateT = 0, fLast = 0;
int fScore = 0, fLives = 3, fLevel = 1, fHome = 0, fRow = 11, fBest = 11;
int fOfx = 153, fOfy = 233, fOrow = 11;
float fFx = 153, fMul = 1.0f, fFrameScale = 1.0f;
bool fSlot[5];
float lanX[12][4];
int lanT[12];

uint16_t fBg(int r) {
  if (r == 0) return K_BANK;
  if (r <= 4) return K_WATER;
  if (r == 5 || r == 11) return K_GRASS;
  return K_ROAD;
}
void fDrawFrog(int x, int y, uint16_t col) {
  tft.fillRoundRect(x - 1, y + 2, 16, 12, 5, K_CREAM);
  tft.fillCircle(x + 3, y + 3, 4, K_CREAM); tft.fillCircle(x + 11, y + 3, 4, K_CREAM);
  tft.fillRoundRect(x, y + 3, 14, 10, 4, col);
  tft.fillCircle(x + 3, y + 3, 3, col); tft.fillCircle(x + 11, y + 3, 3, col);
  tft.fillRect(x + 2, y + 2, 2, 2, ST77XX_BLACK); tft.fillRect(x + 10, y + 2, 2, 2, ST77XX_BLACK);
  tft.fillRect(x + 6, y + 8, 3, 2, K_PINK);
}
void fDrawObj(int r, int x) {
  int y = r * F_ROW, w = F_W[r];
  if (x >= W || x + w <= 0) return;
  switch (F_TYPE[r]) {
    case 1:
      tft.fillRect(x, y + 4, w, 12, F_COL[r]);
      tft.fillRect(x + 5, y + 6, w - 10, 6, K_WINDOW);
      break;
    case 2: {
      bool right = F_V[r] > 0;
      int cabX = right ? x + w - 14 : x;
      tft.fillRect(x, y + 3, w, 14, K_TRAIL);
      tft.fillRect(cabX, y + 5, 14, 12, F_COL[r]);
      tft.fillRect(right ? cabX + 9 : cabX + 1, y + 7, 4, 5, K_WINDOW);
      break;
    }
    case 3:
      tft.fillRect(x, y + 3, w, 14, K_LOG);
      tft.fillRect(x + 4, y + 9, w - 8, 2, K_LOGD);
      break;
    case 4:
      for (int k = 0; k < w / 18; k++) {
        tft.fillRoundRect(x + 18 * k + 1, y + 3, 16, 14, 6, K_TURTLE);
        tft.fillRect(x + 18 * k + 6, y + 8, 6, 4, K_TURTLED);
      }
      break;
  }
}
void fDrawHome() {
  tft.fillRect(0, 0, W, F_ROW, K_BANK);
  for (int i = 0; i < 5; i++) {
    int cx = 32 + i * 64;
    tft.fillRect(cx - 18, 0, 36, F_ROW, K_WATER);
    if (fSlot[i]) fDrawFrog(cx - 7, 3, K_FROG);
  }
}
void fInitLanes() {
  for (int r = 0; r < 12; r++) {
    if (!F_N[r]) continue;
    int s = (W + F_W[r] + F_N[r] - 1) / F_N[r];
    lanT[r] = s * F_N[r];
    for (int i = 0; i < F_N[r]; i++) lanX[r][i] = (float)(i * s + (r * 37) % s - F_W[r]);
  }
}
void fBoard() {
  for (int r = 1; r < 12; r++) tft.fillRect(0, r * F_ROW, W, F_ROW, fBg(r));
  fDrawHome();
  for (int r = 1; r < 11; r++) for (int i = 0; i < F_N[r]; i++) fDrawObj(r, (int)lanX[r][i]);
}
void fMoveObj(int r, int i) {
  float v = F_V[r] * fMul * fFrameScale;
  float ox = lanX[r][i], nx = ox + v;
  int w = F_W[r];
  bool wrapped = false;
  if (v > 0 && nx >= W)          { nx -= lanT[r]; wrapped = true; }
  else if (v < 0 && nx + w <= 0) { nx += lanT[r]; wrapped = true; }
  lanX[r][i] = nx;
  int y = r * F_ROW;
  uint16_t bg = fBg(r);
  int sw = (int)ceilf(fabsf(v)) + 2;
  if (wrapped)    tft.fillRect((int)ox - 2, y, w + 4, F_ROW, bg);
  else if (v > 0) tft.fillRect((int)ox - 1, y, sw, F_ROW, bg);
  else            tft.fillRect((int)(ox + w) - sw + 1, y, sw, F_ROW, bg);
  fDrawObj(r, (int)nx);
}
void fRestoreFrogGround(int x, int y, int row) {
  tft.fillRect(x - 1, y + 2, F_FW + 2, F_FW, fBg(row));
  if (row == 0) fDrawHome();
  if (row >= 1 && row <= 10) {
    for (int i = 0; i < F_N[row]; i++) {
      int ox = (int)lanX[row][i];
      if (x + F_FW > ox && x < ox + F_W[row]) fDrawObj(row, ox);
    }
  }
}
void fReset() {
  tft.fillRect(fOfx, fOfy, F_FW, F_FW, fBg(fOrow));
  if (fOrow == 0) fDrawHome();
  fRow = 11; fFx = 153; fBest = 11;
  fOfx = 153; fOfy = fRow * F_ROW + 3; fOrow = fRow;
  fDrawFrog(fOfx, fOfy, K_FROG);
}
void fKill(bool drown) {
  int y = fRow * F_ROW + 3;
  fDrawFrog((int)fFx, y, drown ? K_CYAN : K_RED);
  fOfx = (int)fFx; fOfy = y; fOrow = fRow;
  fLives--; mood = ";-;";
  if (drown) SFX(T_SPLASH); else SFX(T_SPLAT);
  fState = FS_PAUSE; fStateT = millis(); fAfter = (fLives <= 0) ? 2 : 0;
  oledDirty = true;
}
void fLand() {
  float cx = fFx + F_FW / 2.0f;
  int slot = -1;
  for (int i = 0; i < 5; i++) { float sc = 32 + i * 64; if (cx >= sc - 16 && cx <= sc + 16) slot = i; }
  if (slot >= 0 && !fSlot[slot]) {
    fSlot[slot] = true; fHome++; fScore += 50;
    mood = "^w^"; SFX(T_HOME);
    fDrawHome(); fReset(); oledDirty = true;
    if (fHome >= 5) {
      fScore += 100 * fLevel;
      msgBox(FS("LEVEL UP! x3"), K_YELLOW, K_BG);
      fState = FS_PAUSE; fStateT = millis(); fAfter = 1;
    }
  } else fKill(false);
}
void fNextLevel() {
  fLevel++; fMul = gameDifficultyFactor(APP_FROGGER) * (1.0f + 0.12f * (fLevel - 1));
  fHome = 0; for (int i = 0; i < 5; i++) fSlot[i] = false;
  mood = ":3"; fBoard(); fReset();
  fState = FS_PLAY; oledDirty = true;
}
void fNew() {
  fScore = 0; fLives = gameDifficulty[APP_FROGGER] == 0 ? 4 : (gameDifficulty[APP_FROGGER] == 2 ? 2 : 3);
  fLevel = 1; fMul = gameDifficultyFactor(APP_FROGGER); fHome = 0;
  fLast = millis(); fFrameScale = 1.0f;
  for (int i = 0; i < 5; i++) fSlot[i] = false;
  fInitLanes(); fBoard();
  fOfx = 153; fOfy = 11 * F_ROW + 3; fOrow = 11;
  fReset(); mood = ":3"; SFX(T_START);
  fState = FS_PLAY; oledDirty = true;
}
void fFinish() {
  if (fAfter == 0)      { fReset(); fState = FS_PLAY; mood = ":3"; oledDirty = true; }
  else if (fAfter == 1) fNextLevel();
  else {
    msgBox(FS("GAME OVER ;-;"), K_PINK, K_BG);
    tft.fillRect(60, 138, 200, 24, K_BG);
    fState = FS_OVER; fBlink = -1; oledDirty = true;
  }
}
void fFrame() {
  for (int r = 1; r <= 10; r++) for (int i = 0; i < F_N[r]; i++) fMoveObj(r, i);
  int row = fRow;
  bool dead = false, drown = false;
  if (row >= 1 && row <= 4) {
    float cx = fFx + F_FW / 2.0f;
    bool on = false;
    for (int i = 0; i < F_N[row]; i++) { float ox = lanX[row][i]; if (cx >= ox + 2 && cx <= ox + F_W[row] - 2) { on = true; break; } }
    if (on) { fFx += F_V[row] * fMul * fFrameScale; if (fFx < -6 || fFx > W - 8) { dead = true; drown = true; } }
    else { dead = true; drown = true; }
  } else if (row >= 6 && row <= 10) {
    for (int i = 0; i < F_N[row]; i++) { float ox = lanX[row][i]; if (fFx + 12 > ox && fFx + 2 < ox + F_W[row]) { dead = true; break; } }
  }
  int nextX = (int)fFx, nextY = row * F_ROW + 3;
  if (nextX != fOfx || nextY != fOfy || row != fOrow) fRestoreFrogGround(fOfx, fOfy, fOrow);
  if (dead) { fKill(drown); return; }
  if (row < 1 || row > 4) fFx = constrain(fFx, 0.0f, (float)(W - F_FW));
  fOfx = (int)fFx; fOfy = row * F_ROW + 3; fOrow = row;
  fDrawFrog(fOfx, fOfy, K_FROG);
}
void froggerEnter() {
  tft.fillScreen(K_BG);
  centerText(FS("FROGGER"), 36, 5, K_FROG, K_BG);
  centerText(FS("turn the knob = sidestep"), 110, 2, K_CREAM, K_BG);
  centerText(FS("press = hop forward"), 134, 2, K_CREAM, K_BG);
  centerText(FS("5 frogs home = next level"), 166, 1, K_PINK, K_BG);
  fState = FS_TITLE; fBlink = -1; fScore = 0; fLives = 3; fLevel = 1; fHome = 0;
}
void froggerTick(uint32_t now) {
  bool press = takePress();
  takeShort();
  int clicks = takeClicks();
  switch (fState) {
    case FS_TITLE:
    case FS_OVER: {
      int b = (now / 500) % 2;
      if (b != fBlink) { fBlink = b; centerText(FS("press the knob!"), fState == FS_TITLE ? 196 : 142, 2, b ? K_CREAM : K_BG, K_BG); }
      if (press) fNew();
      break;
    }
    case FS_PLAY:
      if (clicks) { fFx += clicks * 9 * ENC_DIR; fFx = constrain(fFx, 0.0f, (float)(W - F_FW)); }
      if (press && fRow > 0) {
        fRow--;
        if (fRow < fBest) { fBest = fRow; fScore += 10; oledDirty = true; }
        SFX(T_HOP);
        if (fRow == 0) fLand();
      }
      if (fState == FS_PLAY && now - fLast >= FRAME_INTERVAL_MS) {
        uint32_t elapsed = now - fLast;
        fLast = now;
        fFrameScale = constrain(elapsed / 33.0f * gameSpeedFactor(APP_FROGGER), 0.25f, 2.0f);
        fFrame();
      }
      break;
    case FS_PAUSE:
      if (now - fStateT >= (fAfter == 1 ? 1300UL : 700UL)) fFinish();
      break;
  }
}

// ================= GALAGA (sprites = no flicker) =================
const int G_COLS = 8, G_ROWS = 3, G_EW = 18, G_EH = 14, G_SPX = 28, G_SPY = 22;
const int G_BW = (G_COLS - 1) * G_SPX + G_EW;
const int G_BH = (G_ROWS - 1) * G_SPY + G_EH;
const int G_SY = 212;
enum { GS_TITLE, GS_PLAY, GS_HIT, GS_WAVE, GS_OVER };
int gState = GS_TITLE, gBlink = -1;
unsigned long gStateT = 0, gLast = 0, gInv = 0;
int gScore = 0, gLives = 3, gWave = 1, gShipX = 160, gOldShipX = 160, gAlive = 0, gDir = 1;
float gFrameN = 0, gFrameScale = 1.0f, gFx = 53, gFy = 34;
uint8_t gMouse[G_ROWS][G_COLS];
int pbX[2]; float pbY[2]; bool pbOn[2];
int ebX[3]; float ebY[3]; bool ebOn[3];
int exX[6], exY[6]; float exT[6];
bool gDive = false;
int gdR = 0, gdC = 0, gdX0 = 0, gdY0 = 0, gdSg = 1, gdX = 0, gdY = 0;
float gdT = 0;
GFXcanvas16* spr[9];       // 0-5 mice (row*2+flap), 6-7 diver, 8 fox ship

void gBuildMouse(GFXcanvas16* cv, uint16_t col, bool flap) {
  if (!cv) return;
  cv->fillScreen(K_SPACE);
  cv->fillRect(4, 3, 4, 4, col); cv->fillRect(14, 3, 4, 4, col);
  cv->fillRoundRect(3, 6, 16, 10, 4, col);
  cv->fillRect(7, 9, 2, 2, 0); cv->fillRect(13, 9, 2, 2, 0);
  cv->fillRect(10, flap ? 13 : 12, 2, 2, K_PINK);
}
void gBuildSprites() {
  const uint16_t col[3] = {RGB565(200,200,215), RGB565(210,170,110), RGB565(190,140,175)};
  for (int i = 0; i < 8; i++) spr[i] = new GFXcanvas16(22, 18);
  spr[8] = new GFXcanvas16(24, 18);
  for (int r = 0; r < 3; r++) for (int f = 0; f < 2; f++) gBuildMouse(spr[r * 2 + f], col[r], f);
  gBuildMouse(spr[6], K_YELLOW, false); gBuildMouse(spr[7], K_YELLOW, true);
  GFXcanvas16* s = spr[8];
  if (!s) return;
  s->fillScreen(K_SPACE);
  s->fillTriangle(2, 16, 22, 16, 12, 6, K_ORANGE);
  s->fillTriangle(2, 12, 4, 2, 9, 10, K_ORANGE);
  s->fillTriangle(22, 12, 20, 2, 15, 10, K_ORANGE);
  s->fillTriangle(8, 16, 16, 16, 12, 11, K_CREAM);
  s->fillRect(7, 10, 2, 2, 0); s->fillRect(15, 10, 2, 2, 0);
}
void gFreeSprites() { for (int i = 0; i < 9; i++) { delete spr[i]; spr[i] = nullptr; } }
void gBlit(int x, int y, GFXcanvas16* cv) {
  if (!cv || !cv->getBuffer()) return;
  int w = cv->width(), h = cv->height();
  if (x < 0 || y < 0 || x + w > W || y + h > H) return;
  tft.startWrite();
  tft.setAddrWindow(x, y, w, h);
  tft.blitRows(cv->getBuffer(), w, w, h);
  tft.endWrite();
}
void gBoom(int x, int y) { for (int i = 0; i < 6; i++) if (exT[i] <= 0) { exX[i] = x; exY[i] = y; exT[i] = 8; return; } }
void gExplosions() {
  for (int i = 0; i < 6; i++) {
    if (exT[i] <= 0) continue;
    exT[i] -= gFrameScale;
    if (exT[i] <= 0) { exT[i] = 0; tft.fillRect(exX[i] - 12, exY[i] - 12, 24, 24, K_SPACE); }
    else tft.fillCircle(exX[i], exY[i], 2 + (int)exT[i], ((int)exT[i] & 1) ? K_YELLOW : K_ORANGE);
  }
}
void gClearShots() {
  for (int i = 0; i < 2; i++) pbOn[i] = false;
  for (int i = 0; i < 3; i++) ebOn[i] = false;
  for (int i = 0; i < 6; i++) exT[i] = 0;
}
void gNewWave() {
  tft.fillScreen(K_SPACE);
  gLast = millis(); gFrameScale = 1.0f;
  for (int r = 0; r < G_ROWS; r++) for (int c = 0; c < G_COLS; c++) gMouse[r][c] = 1;
  gAlive = G_ROWS * G_COLS; gFx = 53; gFy = 34; gDir = 1; gDive = false;
  gClearShots(); gOldShipX = gShipX; gInv = millis() + 1500;
  gState = GS_PLAY; oledDirty = true;
}
void gNew() { gScore = 0; gLives = 3; gWave = 1; gShipX = 160; mood = ":3"; SFX(T_START); gNewWave(); }
void gShipHit(uint32_t now) {
  gLives--; mood = ";-;"; SFX(T_BOOM);
  gBoom(gShipX, G_SY + 7); gBoom(gShipX - 9, G_SY + 5); gBoom(gShipX + 9, G_SY + 9);
  tft.fillRect(gShipX - 12, G_SY - 2, 24, 18, K_SPACE);
  for (int i = 0; i < 3; i++) if (ebOn[i]) { tft.fillRect(ebX[i] - 2, ebY[i], 5, 9, K_SPACE); ebOn[i] = false; }
  gState = GS_HIT; gStateT = now; oledDirty = true;
}
void galaFrame(uint32_t now) {
  gFrameN += gFrameScale;
  for (int i = 0; i < 2; i++) if (pbOn[i]) tft.fillRect(pbX[i] - 1, pbY[i], 3, 9, K_SPACE);
  for (int i = 0; i < 3; i++) if (ebOn[i]) tft.fillRect(ebX[i] - 2, ebY[i], 5, 9, K_SPACE);
  if (gDive) tft.fillRect(gdX - 2, gdY - 2, G_EW + 4, G_EH + 4, K_SPACE);
  if (gShipX != gOldShipX) tft.fillRect(gOldShipX - 12, G_SY - 2, 24, 18, K_SPACE);
  gOldShipX = gShipX;

  for (int i = 0; i < 2; i++) if (pbOn[i]) { pbY[i] -= 7.0f * gFrameScale; if (pbY[i] < -9) pbOn[i] = false; }
  float ebSpd = (3.0f + gWave / 2) * gameDifficultyFactor(APP_GALAGA);
  for (int i = 0; i < 3; i++) if (ebOn[i]) { ebY[i] += ebSpd * gFrameScale; if (ebY[i] > H) ebOn[i] = false; }

  int oldX = (int)gFx, oldY = (int)gFy;
  float sp = 0.9f + 0.15f * gWave;
  if (sp > 1.9f) sp = 1.9f;
  gFx += gDir * sp * gFrameScale * gameDifficultyFactor(APP_GALAGA);
  if (gFx < 4) { gFx = 4; gDir = 1; gFy += 8; }
  else if (gFx > W - 4 - G_BW) { gFx = W - 4 - G_BW; gDir = -1; gFy += 8; }
  int nx = (int)gFx, ny = (int)gFy;
  if (ny != oldY) tft.fillRect(oldX - 3, oldY - 3, G_BW + 6, G_BH + 6, K_SPACE);

  if (!gDive && gAlive > 0 && (int)random(1000) < (4 + gWave * 2) * gFrameScale) {
    for (int t = 0; t < 30; t++) {
      int r = random(G_ROWS), c = random(G_COLS);
      if (gMouse[r][c]) {
        gdR = r; gdC = c; gMouse[r][c] = 0; gAlive--;
        gdX0 = nx + c * G_SPX; gdY0 = ny + r * G_SPY;
        tft.fillRect(gdX0 - 2, gdY0 - 2, G_EW + 4, G_EH + 4, K_SPACE);
        gdT = 0; gdSg = (gdX0 + G_EW / 2 < W / 2) ? 1 : -1;
        gdX = gdX0; gdY = gdY0; gDive = true;
        break;
      }
    }
  }
  if (gDive) {
    gdT += gFrameScale;
    gdY = gdY0 + (int)(gdT * (3.0f + 0.2f * gWave) * gameDifficultyFactor(APP_GALAGA));
    gdX = gdX0 + (int)(sinf(gdT * 0.1f) * 55.0f * gdSg);
    gdX = constrain(gdX, 2, W - G_EW - 2);
    if (gdY > 224) { gDive = false; gMouse[gdR][gdC] = 1; gAlive++; }
  }

  if (gAlive > 0 && (int)random(1000) < (12 + gWave * 3) * gFrameScale) {
    int slot = -1;
    for (int i = 0; i < 3; i++) if (!ebOn[i]) { slot = i; break; }
    if (slot >= 0) {
      for (int t = 0; t < 30; t++) {
        int r = random(G_ROWS), c = random(G_COLS);
        if (gMouse[r][c]) { ebX[slot] = nx + c * G_SPX + G_EW / 2; ebY[slot] = ny + r * G_SPY + G_EH; ebOn[slot] = true; break; }
      }
    }
  }

  for (int b = 0; b < 2; b++) {
    if (!pbOn[b]) continue;
    bool hit = false;
    for (int r = 0; r < G_ROWS && !hit; r++) {
      for (int c = 0; c < G_COLS; c++) {
        if (!gMouse[r][c]) continue;
        int ex0 = nx + c * G_SPX, ey0 = ny + r * G_SPY;
        if (pbX[b] + 1 >= ex0 && pbX[b] - 1 <= ex0 + G_EW && pbY[b] + 9 >= ey0 && pbY[b] <= ey0 + G_EH) {
          gMouse[r][c] = 0; gAlive--;
          gScore += 20 + (G_ROWS - 1 - r) * 10;
          tft.fillRect(ex0 - 2, ey0 - 2, G_EW + 4, G_EH + 4, K_SPACE);
          gBoom(ex0 + G_EW / 2, ey0 + G_EH / 2);
          hit = true; break;
        }
      }
    }
    if (!hit && gDive && pbX[b] + 1 >= gdX && pbX[b] - 1 <= gdX + G_EW && pbY[b] + 9 >= gdY && pbY[b] <= gdY + G_EH) {
      gDive = false; gScore += 60; gBoom(gdX + G_EW / 2, gdY + G_EH / 2); hit = true;
    }
    if (hit) { pbOn[b] = false; SFX(T_BOOM); mood = "^w^"; oledDirty = true; }
  }

  bool hurt = false;
  if (now >= gInv) {
    for (int i = 0; i < 3; i++)
      if (ebOn[i] && ebX[i] + 2 >= gShipX - 9 && ebX[i] - 2 <= gShipX + 9 && ebY[i] + 9 >= G_SY + 2 && ebY[i] <= G_SY + 14) hurt = true;
    if (gDive && gdX + G_EW >= gShipX - 9 && gdX <= gShipX + 9 && gdY + G_EH >= G_SY + 2 && gdY <= G_SY + 14) hurt = true;
  }
  if (ny + G_BH >= G_SY - 6) { gLives = 1; hurt = true; }
  if (hurt) gShipHit(now);

  gExplosions();
  int flap = ((int)(gFrameN / 12)) % 2;
  for (int r = 0; r < G_ROWS; r++)
    for (int c = 0; c < G_COLS; c++)
      if (gMouse[r][c]) gBlit(nx + c * G_SPX - 2, ny + r * G_SPY - 2, spr[r * 2 + flap]);
  if (gDive) gBlit(gdX - 2, gdY - 2, spr[6 + flap]);
  for (int i = 0; i < 2; i++) if (pbOn[i]) tft.fillRect(pbX[i] - 1, pbY[i], 3, 9, K_CYAN);
  for (int i = 0; i < 3; i++) if (ebOn[i]) { tft.fillRect(ebX[i] - 1, ebY[i], 3, 8, K_PINK); tft.fillRect(ebX[i] - 2, ebY[i] + 2, 5, 3, K_PINK); }
  if (gState == GS_PLAY) {
    bool blink = (now < gInv) && ((now / 100) % 2);
    if (blink) tft.fillRect(gShipX - 12, G_SY - 2, 24, 18, K_SPACE);
    else gBlit(gShipX - 12, G_SY - 2, spr[8]);
  }

  if (gState == GS_PLAY && gAlive <= 0 && !gDive) {
    gState = GS_WAVE; gStateT = now; mood = "x3"; SFX(T_WAVE);
    char b[24]; snprintf_P(b, sizeof b, PSTR("WAVE %d CLEAR!"), gWave);
    msgBox(b, K_YELLOW, K_SPACE); oledDirty = true;
  }
}
void galaEnter() {
  tft.fillScreen(K_SPACE);
  centerText(FS("GALAGA"), 30, 6, K_ORANGE, K_SPACE);
  centerText(FS("fox vs the mice!"), 96, 2, K_CYAN, K_SPACE);
  centerText(FS("turn: move   press: fire"), 130, 2, K_CREAM, K_SPACE);
  gBlit(118, 162, spr[0]); gBlit(149, 162, spr[3]); gBlit(180, 162, spr[4]);
  gBlit(148, 210, spr[8]);
  gClearShots();
  gState = GS_TITLE; gBlink = -1; gScore = 0; gLives = 3; gWave = 1; gDive = false;
}
void galaTick(uint32_t now) {
  bool press = takePress();
  takeShort();
  int clicks = takeClicks();
  switch (gState) {
    case GS_TITLE:
    case GS_OVER: {
      int b = (now / 500) % 2;
      if (b != gBlink) { gBlink = b; centerText(FS("press the knob!"), gState == GS_TITLE ? 190 : 142, 2, b ? K_CREAM : K_SPACE, K_SPACE); }
      if (press) gNew();
      break;
    }
    case GS_PLAY:
      if (clicks) { gShipX += clicks * 10 * ENC_DIR; gShipX = constrain(gShipX, 14, W - 14); }
      if (press) { for (int i = 0; i < 2; i++) if (!pbOn[i]) { pbOn[i] = true; pbX[i] = gShipX; pbY[i] = G_SY - 6; SFX(T_SHOT); break; } }
      if (now - gLast >= FRAME_INTERVAL_MS) {
        uint32_t elapsed = now - gLast;
        gLast = now;
        gFrameScale = constrain(elapsed / 33.0f * gameSpeedFactor(APP_GALAGA), 0.25f, 2.0f);
        galaFrame(now);
      }
      break;
    case GS_HIT:
      if (now - gLast >= FRAME_INTERVAL_MS) {
        uint32_t elapsed = now - gLast;
        gLast = now;
        gFrameScale = constrain(elapsed / 33.0f * gameSpeedFactor(APP_GALAGA), 0.25f, 2.0f);
        gExplosions();
      }
      if (now - gStateT >= 1100) {
        for (int i = 0; i < 6; i++) if (exT[i] > 0) { exT[i] = 0; tft.fillRect(exX[i] - 12, exY[i] - 12, 24, 24, K_SPACE); }
        if (gLives <= 0) {
          msgBox(FS("GAME OVER ;-;"), K_PINK, K_SPACE);
          tft.fillRect(60, 138, 200, 24, K_SPACE);
          gState = GS_OVER; gBlink = -1;
        } else { gShipX = 160; gOldShipX = 160; gInv = now + 2000; gState = GS_PLAY; mood = ":3"; }
        oledDirty = true;
      }
      break;
    case GS_WAVE:
      if (now - gStateT >= 1500) { gWave++; mood = ":3"; gNewWave(); }
      break;
  }
}

// ================= FUZZY RALLY =================
// Flicker-free road: each frame only small boxes are repainted (kerbs, lane dashes, and the old+new
// spot of every car). A box is composed in a 48x64 off-screen canvas (road, dashes, every car that
// touches it) and pushed to the TFT in one go, so nothing is ever erased on the screen itself.
enum { RACE_SETUP, RACE_PLAY, RACE_OVER };
enum { RK_CAR, RK_CONE, RK_FISH };
struct RaceThing {
  float y;
  float speed;
  uint8_t lane;
  uint8_t kind;
  uint16_t color;
  bool active;
};
const int RACE_N = 6;
const int RACE_LANES[3] = {84, 160, 236};
const int RACE_DIV[2] = {121, 197};
const int ROAD_L = 38, ROAD_R = 282, ROAD_T = 21;     // area the canvas repaints (kerbs included)
const int RACE_PY = 184;                               // player car centre y
const int RCV_W = 48, RCV_H = 64;
const int MAP_X = 308, MAP_T = 34, MAP_B = 226;        // race-mode progress map on the right grass
const float RACE_TOP = 5.0f, RACE_LEN = 12000.0f;
const int8_t RACE_BOX[3][4] = {{-16, -23, 32, 45}, {-9, -11, 18, 22}, {-11, -8, 23, 16}};   // x, y, w, h to repaint
const int8_t RACE_HIT[3][4] = {{-11, -19, 22, 38}, {-7, -9, 14, 18}, {-10, -7, 20, 14}};   // x, y, w, h to collide
const uint16_t RACE_CAR_COLORS[4] = {K_PINK, K_CYAN, K_YELLOW, RGB565(170, 120, 255)};
const uint16_t RACE_GRASS[3] = {RGB565(45, 122, 64), RGB565(31, 94, 55), RGB565(42, 69, 112)};
const uint16_t RACE_ROAD[3] = {RGB565(62, 66, 75), RGB565(45, 51, 53), RGB565(33, 38, 65)};
const uint16_t RACE_EDGE[3] = {RGB565(196, 159, 83), RGB565(110, 82, 50), RGB565(78, 99, 150)};
GFXcanvas16* raceCv = nullptr;
int raceState = RACE_SETUP;
float raceX = 160, raceTargetX = 160, raceSpeed = 0, raceDistance = 0, raceScroll = 0;
float raceRival[2], raceRivalSpeed[2];
uint32_t raceLast = 0, raceHudAt = 0, raceBoostUntil = 0, raceInvulnerableUntil = 0, raceStartMs = 0;
int raceLives = 3, raceLivesMax = 3, racePowerups = 0, raceBest = 0, racePlace = 0;
int raceHudLives = -1, raceHudTurbo = -1, raceMapY[3] = {-1, -1, -1};
bool raceShowPlayer = true, raceFlames = false;
uint8_t raceFlick = 0;
RaceThing raceThings[RACE_N];

int raceRivalY(int i) { return RACE_PY - (int)(raceRival[i] - raceDistance); }
int raceFinishY() { return RACE_PY - (int)(RACE_LEN - raceDistance); }
int racePosition() { return 1 + (raceRival[0] > raceDistance) + (raceRival[1] > raceDistance); }
bool raceHits(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
  return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

// ---------- sprites (drawn into the canvas, x/y already relative to it) ----------
void raceCar(Adafruit_GFX& g, int x, int y, uint16_t color, bool player) {
  if (player) {                                       // fluffy fox tail (+ turbo flames)
    if (raceFlames) {
      int f = raceFlick % 4;
      g.fillTriangle(x - 10, y + 17, x - 4, y + 17, x - 7, y + 26 + f, K_YELLOW);
      g.fillTriangle(x + 4, y + 17, x + 10, y + 17, x + 7, y + 29 - f, K_YELLOW);
      g.fillTriangle(x - 9, y + 17, x - 5, y + 17, x - 7, y + 22 + f, K_RED);
      g.fillTriangle(x + 5, y + 17, x + 9, y + 17, x + 7, y + 25 - f, K_RED);
    }
    g.fillCircle(x, y + 22, 5, color);
    g.fillCircle(x, y + 26, 3, K_CREAM);
  }
  g.fillRect(x - 15, y - 12, 3, 8, K_INK); g.fillRect(x + 12, y - 12, 3, 8, K_INK);
  g.fillRect(x - 15, y + 8, 3, 8, K_INK);  g.fillRect(x + 12, y + 8, 3, 8, K_INK);
  g.fillRoundRect(x - 13, y - 18, 26, 36, 7, color);
  g.fillTriangle(x - 12, y - 12, x - 8, y - 22, x - 3, y - 15, color);      // kitty ears
  g.fillTriangle(x + 12, y - 12, x + 8, y - 22, x + 3, y - 15, color);
  g.fillTriangle(x - 9, y - 15, x - 8, y - 19, x - 6, y - 15, K_PINK);
  g.fillTriangle(x + 9, y - 15, x + 8, y - 19, x + 6, y - 15, K_PINK);
  g.fillRoundRect(x - 8, y - 9, 16, 10, 4, K_WINDOW);
  g.fillRect(x - 5, y - 6, 3, 3, K_CREAM); g.fillRect(x + 2, y - 6, 3, 3, K_CREAM);
  g.drawLine(x - 4, y + 6, x - 2, y + 8, K_INK); g.drawLine(x - 2, y + 8, x, y + 6, K_INK);   // :3 mouth
  g.drawLine(x, y + 6, x + 2, y + 8, K_INK);     g.drawLine(x + 2, y + 8, x + 4, y + 6, K_INK);
  if (!player) g.fillRect(x - 6, y + 13, 12, 3, K_CREAM);
}
void raceCone(Adafruit_GFX& g, int x, int y) {
  g.fillTriangle(x, y - 10, x - 8, y + 9, x + 8, y + 9, K_ORANGE);
  g.fillRect(x - 5, y - 1, 10, 3, K_CREAM);
  g.fillRect(x - 8, y + 7, 17, 3, K_ORANGE);
}
void raceFish(Adafruit_GFX& g, int x, int y) {          // turbo snack
  g.fillRoundRect(x - 9, y - 5, 14, 10, 5, K_CYAN);
  g.fillTriangle(x + 3, y, x + 10, y - 6, x + 10, y + 6, K_CYAN);
  g.fillRect(x - 6, y - 2, 2, 2, K_INK);
  g.drawFastVLine(x - 1, y - 3, 6, K_CREAM);
}

// ---------- compositor ----------
void raceCompose(int x, int y, int w, int h) {
  GFXcanvas16& g = *raceCv;
  uint8_t tr = raceTrack % 3;
  int phase = (int)raceScroll;
  g.fillRect(0, 0, w, h, RACE_ROAD[tr]);
  const int kerbX[2] = {ROAD_L, 273};
  for (int s = 0; s < 2; s++) {
    int kx = kerbX[s] - x;
    if (kx + 9 <= 0 || kx >= w) continue;
    g.fillRect(kx, 0, 9, h, RACE_EDGE[tr]);
    for (int sy = phase % 24 - 24; sy < H; sy += 24)
      if (sy + 12 > y && sy < y + h) g.fillRect(kx, sy - y, 9, 12, K_CREAM);
  }
  for (int d = 0; d < 2; d++) {
    int dx = RACE_DIV[d] + 1 - x;
    if (dx + 3 <= 0 || dx >= w) continue;
    for (int sy = phase % 42 - 42; sy < H; sy += 42)
      if (sy + 19 > y && sy < y + h) g.fillRect(dx, sy - y, 3, 19, K_CREAM);
  }
  if (raceMode) {                                        // checkered finish line
    int fy = raceFinishY() - 4;
    if (fy + 8 > y && fy < y + h)
      for (int r = 0; r < 2; r++)
        for (int cx = 47; cx < 273; cx += 8)
          if (cx + 8 > x && cx < x + w) g.fillRect(cx - x, fy + r * 4 - y, 8, 4, ((cx / 8 + r) & 1) ? K_INK : K_CREAM);
  }
  for (int i = 0; i < RACE_N; i++) {
    const RaceThing& v = raceThings[i];
    if (!v.active) continue;
    int tx = RACE_LANES[v.lane], ty = (int)v.y;
    const int8_t* b = RACE_BOX[v.kind];
    if (!raceHits(x, y, w, h, tx + b[0], ty + b[1], b[2], b[3])) continue;
    if (v.kind == RK_CAR) raceCar(g, tx - x, ty - y, v.color, false);
    else if (v.kind == RK_CONE) raceCone(g, tx - x, ty - y);
    else raceFish(g, tx - x, ty - y);
  }
  if (raceMode)
    for (int i = 0; i < 2; i++) {
      int tx = RACE_LANES[i * 2], ty = raceRivalY(i);
      if (raceHits(x, y, w, h, tx - 16, ty - 23, 32, 45)) raceCar(g, tx - x, ty - y, RACE_CAR_COLORS[i], false);
    }
  int px = (int)raceX;
  if (raceShowPlayer && raceHits(x, y, w, h, px - 16, RACE_PY - 23, 32, 54)) raceCar(g, px - x, RACE_PY - y, K_ORANGE, true);
  uint16_t* buf = g.getBuffer();
  tft.startWrite();
  tft.setAddrWindow(x, y, w, h);
  tft.blitRows(buf, RCV_W, w, h);
  tft.endWrite();
}
void raceRefresh(int x, int y, int w, int h) {
  if (x < ROAD_L) { w -= ROAD_L - x; x = ROAD_L; }
  if (x + w > ROAD_R) w = ROAD_R - x;
  if (y < ROAD_T) { h -= ROAD_T - y; y = ROAD_T; }
  if (y + h > H) h = H - y;
  if (w <= 0 || h <= 0 || !raceCv || !raceCv->getBuffer()) return;   // no RAM for the canvas: draw nothing, don't crash
  for (int ty = y; ty < y + h; ty += RCV_H)
    for (int tx = x; tx < x + w; tx += RCV_W)
      raceCompose(tx, ty, min(RCV_W, x + w - tx), min(RCV_H, y + h - ty));
}
void raceRefreshPair(int ax, int ay, int bx, int by, int w, int h) {     // old + new spot of one sprite
  if (abs(bx - ax) < w && abs(by - ay) < h) raceRefresh(min(ax, bx), min(ay, by), w + abs(bx - ax), h + abs(by - ay));
  else { raceRefresh(ax, ay, w, h); raceRefresh(bx, by, w, h); }
}

// ---------- static scenery, HUD, progress map ----------
void raceDrawBackground() {
  uint8_t tr = raceTrack % 3;
  tft.fillScreen(RACE_GRASS[tr]);
  for (int k = 0; k < 12; k++) {
    int side = k & 1, row = k >> 1;
    int cx = side ? 291 : ((row & 1) ? 12 : 25), cy = 38 + row * 36;
    if (tr == 0) {
      tft.fillRect(cx - 1, cy + 4, 3, 8, RGB565(110, 72, 40));
      tft.fillCircle(cx, cy, 7, RGB565(30, 96, 52));
      tft.fillRect(cx + (side ? -8 : 7), cy + 12, 2, 2, K_PINK);
    } else if (tr == 1) {
      tft.fillRect(cx - 1, cy + 8, 3, 5, RGB565(110, 72, 40));
      tft.fillTriangle(cx, cy - 9, cx - 7, cy + 9, cx + 7, cy + 9, RGB565(24, 70, 44));
    } else {
      tft.fillCircle(cx, cy, 6, RGB565(60, 70, 110));
      tft.fillCircle(cx + 1, cy + 1, 4, RGB565(48, 56, 92));
      tft.drawPixel(cx + (side ? -8 : 9), cy - 12, K_CREAM);
    }
  }
  tft.fillRect(0, 0, W, 20, K_BG);
  tft.drawFastHLine(0, 20, W, K_PINK);
}
void raceHeart(int x, int y, uint16_t c) {
  tft.fillCircle(x + 2, y + 2, 2, c); tft.fillCircle(x + 6, y + 2, 2, c);
  tft.fillTriangle(x, y + 3, x + 8, y + 3, x + 4, y + 8, c);
}
void raceBolt(int x, int y, uint16_t c) {
  tft.fillTriangle(x + 5, y, x, y + 5, x + 4, y + 5, c);
  tft.fillTriangle(x + 2, y + 4, x + 6, y + 4, x + 1, y + 10, c);
}
void raceDrawHud(bool force) {
  char t[28];
  if (raceMode) snprintf_P(t, sizeof t, PSTR("RACE  P%d/3  %3d%%   "), racePosition(), (int)(min(raceDistance, RACE_LEN) * 100 / RACE_LEN));
  else          snprintf_P(t, sizeof t, PSTR("FREEWAY %5dm   "), (int)(raceDistance / 10));
  putText(6, 6, 1, K_CREAM, K_BG, t);
  if (!force && raceLives == raceHudLives && racePowerups == raceHudTurbo) return;
  raceHudLives = raceLives; raceHudTurbo = racePowerups;
  tft.fillRect(176, 1, 144, 18, K_BG);
  for (int i = 0; i < raceLivesMax; i++) raceHeart(180 + i * 12, 6, i < raceLives ? K_PINK : K_DIM);
  for (int i = 0; i < 5; i++) raceBolt(250 + i * 13, 5, i < racePowerups ? K_YELLOW : K_DIM);
}
void raceMap(bool force) {
  if (!raceMode) return;
  float prog[3] = {raceDistance, raceRival[0], raceRival[1]};
  const uint16_t col[3] = {K_ORANGE, RACE_CAR_COLORS[0], RACE_CAR_COLORS[1]};
  int ny[3];
  bool moved = force;
  for (int i = 0; i < 3; i++) {
    float p = constrain(prog[i] / RACE_LEN, 0.0f, 1.0f);
    ny[i] = MAP_B - (int)(p * (MAP_B - MAP_T));
    if (ny[i] != raceMapY[i]) moved = true;
  }
  if (!moved) return;
  if (force)
    for (int r = 0; r < 2; r++) for (int c = 0; c < 3; c++)
      tft.fillRect(MAP_X - 3 + c * 2, MAP_T - 10 + r * 2, 2, 2, ((r + c) & 1) ? K_INK : K_CREAM);
  for (int i = 0; i < 3; i++) if (raceMapY[i] >= 0) tft.fillRect(MAP_X - 3, raceMapY[i] - 3, 7, 7, RACE_GRASS[raceTrack % 3]);
  tft.drawFastVLine(MAP_X, MAP_T, MAP_B - MAP_T + 1, K_CREAM);
  for (int i = 2; i >= 0; i--) { tft.fillCircle(MAP_X, ny[i], 3, col[i]); raceMapY[i] = ny[i]; }
}

// ---------- traffic ----------
bool raceLaneBusy(int lane, float y, int skip, bool solidOnly, float gap) {
  for (int j = 0; j < RACE_N; j++) {
    const RaceThing& o = raceThings[j];
    if (j == skip || !o.active || o.lane != lane || (solidOnly && o.kind == RK_FISH)) continue;
    if (fabsf(o.y - y) < gap) return true;
  }
  return false;
}
void raceSpawn(int i) {
  RaceThing& v = raceThings[i];
  float y = -40 - random(0, 120);
  int r = random(100);
  uint8_t kind = raceMode ? (r < 70 ? RK_CONE : RK_FISH) : (r < 58 ? RK_CAR : (r < 86 ? RK_CONE : RK_FISH));
  if (kind == RK_FISH && racePowerups >= 5) kind = RK_CONE;
  for (int t = 0; t < 4; t++) {
    int lane = random(3);
    if (raceLaneBusy(lane, y, i, false, 95)) continue;
    if (kind != RK_FISH) {                               // always leave one lane open
      int blocked = 0;
      for (int l = 0; l < 3; l++) if (l != lane && raceLaneBusy(l, y, i, true, 120)) blocked++;
      if (blocked >= 2) continue;
    }
    v.lane = lane; v.y = y; v.kind = kind; v.active = true;
    v.speed = (kind == RK_CAR) ? 1.6f + random(0, 9) / 10.0f : 0;
    v.color = RACE_CAR_COLORS[random(4)];
    return;
  }
}
void raceCrash(uint32_t now) {
  raceLives--;
  raceSpeed *= 0.3f;
  raceInvulnerableUntil = now + 1500;
  mood = ";-;"; SFX(T_BOOM); oledDirty = true;
  if (raceLives <= 0) { raceLives = 0; racePlace = 0; raceState = RACE_OVER; }
}
void raceShowEnd(uint32_t now) {
  bool won;
  char a[24], b[32];
  if (!raceMode) {
    int m = (int)(raceDistance / 10);
    won = m > raceBest;
    if (won) raceBest = m;
    snprintf_P(a, sizeof a, PSTR("%s"), won ? FS("NEW RECORD! x3") : FS("BONK! ;-;"));
    snprintf_P(b, sizeof b, PSTR("%dm   best %dm"), m, raceBest);
  } else if (racePlace == 0) {
    won = false;
    snprintf_P(a, sizeof a, PSTR("OUT OF HP ;-;"));
    snprintf_P(b, sizeof b, PSTR("the rivals zoomed past..."));
  } else {
    unsigned long ms = now - raceStartMs;
    won = racePlace == 1;
    snprintf_P(a, sizeof a, PSTR("%s"), pgmItem(PSTR("1ST PLACE! x3\n2ND PLACE :3\n3RD PLACE ;-;"), racePlace - 1));
    snprintf_P(b, sizeof b, PSTR("time %lu.%lus"), ms / 1000, (ms / 100) % 10);
  }
  stopSound();
  if (won) SFX(T_WIN); else SFX(T_LOSE);
  mood = won ? "x3" : ";-;";
  uint16_t col = won ? K_YELLOW : K_PINK;
  tft.fillRoundRect(45, 76, 230, 92, 10, K_BG);
  tft.drawRoundRect(45, 76, 230, 92, 10, col);
  tft.drawRoundRect(47, 78, 226, 88, 8, K_DIM);
  drawPaw(66, 96, 2, col); drawPaw(254, 96, 2, col);
  centerText(a, 90, 2, col, K_BG);
  centerText(b, 118, 1, K_CREAM, K_BG);
  centerText(FS("press: setup   hold: menu"), 146, 1, K_CYAN, K_BG);
  raceDrawHud(true);
  oledDirty = true;
}

// ---------- game flow ----------
void raceStart() {
  if (!raceCv) raceCv = new GFXcanvas16(RCV_W, RCV_H);
  raceState = RACE_PLAY;
  raceX = raceTargetX = 160; raceSpeed = 1.5f; raceDistance = 0; raceScroll = 0;
  raceLivesMax = raceLives = gameDifficulty[APP_RACE] == 0 ? 4 : (gameDifficulty[APP_RACE] == 2 ? 2 : 3);
  racePowerups = 2; raceBoostUntil = 0; raceInvulnerableUntil = 0;
  raceHudLives = raceHudTurbo = -1;
  for (int i = 0; i < 3; i++) raceMapY[i] = -1;
  raceShowPlayer = true; raceFlames = false;
  raceLast = raceStartMs = millis(); raceHudAt = 0;
  for (int i = 0; i < RACE_N; i++) raceThings[i].active = false;
  float d = gameDifficultyFactor(APP_RACE);
  raceRival[0] = 60; raceRival[1] = 130;
  raceRivalSpeed[0] = RACE_TOP * 0.80f * d;
  raceRivalSpeed[1] = RACE_TOP * 0.88f * d;
  raceDrawBackground();
  raceRefresh(ROAD_L, ROAD_T, ROAD_R - ROAD_L, H - ROAD_T);
  raceDrawHud(true);
  raceMap(true);
  mood = ":3"; SFX(T_START);
  oledDirty = true;
}
void raceFrame(uint32_t now, float scale) {
  raceFlick++;
  bool boost = now < raceBoostUntil;
  float top = RACE_TOP * (boost ? 1.45f : 1.0f);
  if (raceSpeed < top) raceSpeed = min(top, raceSpeed + 0.06f * scale);   // auto gas
  else raceSpeed = max(top, raceSpeed - 0.08f * scale);
  float dy = raceSpeed * scale;

  int oldPX = (int)raceX, oldFin = raceFinishY();
  int oldRY[2] = {raceRivalY(0), raceRivalY(1)};
  int oldY[RACE_N];
  bool oldOn[RACE_N];
  uint8_t oldLane[RACE_N], oldKind[RACE_N];
  for (int i = 0; i < RACE_N; i++) {
    oldY[i] = (int)raceThings[i].y; oldOn[i] = raceThings[i].active;
    oldLane[i] = raceThings[i].lane; oldKind[i] = raceThings[i].kind;
  }

  // move everything
  raceX += (raceTargetX - raceX) * min(1.0f, 0.3f * scale);
  raceDistance += dy;
  raceScroll = fmodf(raceScroll + dy, 168.0f);             // 168 = both dash periods (24, 42) line up
  int active = 0;
  for (int i = 0; i < RACE_N; i++) {
    RaceThing& v = raceThings[i];
    if (!v.active) continue;
    v.y += (v.kind == RK_CAR) ? max(0.8f * scale, dy - v.speed * scale) : dy;
    if (v.y > H + 30) v.active = false; else active++;
  }
  int lvl = gameDifficulty[APP_RACE];
  int target = raceMode ? 2 + lvl / 2 : min(RACE_N, 2 + lvl + min(2, (int)(raceDistance / 6000)));
  if (active < target && random(100) < 8)
    for (int i = 0; i < RACE_N; i++) if (!raceThings[i].active) { raceSpawn(i); break; }
  if (raceMode) for (int i = 0; i < 2; i++) raceRival[i] += raceRivalSpeed[i] * scale;

  // collisions
  int px = (int)raceX;
  for (int i = 0; i < RACE_N && raceState == RACE_PLAY; i++) {
    RaceThing& v = raceThings[i];
    if (!v.active) continue;
    const int8_t* hb = RACE_HIT[v.kind];
    if (!raceHits(px - 11, RACE_PY - 20, 22, 40, RACE_LANES[v.lane] + hb[0], (int)v.y + hb[1], hb[2], hb[3])) continue;
    if (v.kind == RK_FISH) { v.active = false; racePowerups = min(5, racePowerups + 1); mood = "^w^"; SFX(T_HOME); }
    else if (now >= raceInvulnerableUntil) { v.active = false; raceCrash(now); }
  }
  if (raceMode)
    for (int i = 0; i < 2 && raceState == RACE_PLAY; i++) {
      int ry = raceRivalY(i);
      if (now >= raceInvulnerableUntil && abs(ry - RACE_PY) < 38 && abs(px - RACE_LANES[i * 2]) < 24) {
        raceCrash(now);
        raceRival[i] += (ry < RACE_PY) ? 45 : -45;          // bonk them out of the way
      }
    }
  if (raceMode && raceState == RACE_PLAY && raceDistance >= RACE_LEN) { racePlace = racePosition(); raceState = RACE_OVER; }

  // repaint only what changed
  raceShowPlayer = !(now < raceInvulnerableUntil && (now / 90) % 2);
  raceFlames = boost;
  raceRefresh(ROAD_L, ROAD_T, 9, H - ROAD_T);
  raceRefresh(273, ROAD_T, 9, H - ROAD_T);
  for (int d = 0; d < 2; d++) raceRefresh(RACE_DIV[d], ROAD_T, 5, H - ROAD_T);
  for (int i = 0; i < RACE_N; i++) {
    const RaceThing& v = raceThings[i];
    const int8_t* ob = RACE_BOX[oldKind[i]];
    const int8_t* nb = RACE_BOX[v.kind];
    if (oldOn[i] && v.active && oldLane[i] == v.lane && oldKind[i] == v.kind)
      raceRefreshPair(RACE_LANES[v.lane] + nb[0], oldY[i] + nb[1], RACE_LANES[v.lane] + nb[0], (int)v.y + nb[1], nb[2], nb[3]);
    else {
      if (oldOn[i]) raceRefresh(RACE_LANES[oldLane[i]] + ob[0], oldY[i] + ob[1], ob[2], ob[3]);
      if (v.active) raceRefresh(RACE_LANES[v.lane] + nb[0], (int)v.y + nb[1], nb[2], nb[3]);
    }
  }
  if (raceMode) {
    for (int i = 0; i < 2; i++) raceRefreshPair(RACE_LANES[i * 2] - 16, oldRY[i] - 23, RACE_LANES[i * 2] - 16, raceRivalY(i) - 23, 32, 45);
    raceRefreshPair(47, oldFin - 4, 47, raceFinishY() - 4, 226, 8);
  }
  raceRefreshPair(oldPX - 16, RACE_PY - 23, (int)raceX - 16, RACE_PY - 23, 32, 54);

  if (now - raceHudAt >= 100) { raceHudAt = now; raceDrawHud(false); raceMap(false); oledDirty = true; }
  if (raceState == RACE_OVER) raceShowEnd(now);
}
void raceTick(uint32_t now) {
  int clicks = takeClicks();
  takePress();
  bool tap = takeShort();
  if (raceState != RACE_PLAY) {
    if (tap) { gameSetupActive = true; gameSetupApp = APP_RACE; gameSetupSel = SET_START; drawGameSetup(); oledDirty = true; }
    return;
  }
  if (clicks) raceTargetX = constrain(raceTargetX + clicks * 19.0f * ENC_DIR, 64.0f, 256.0f);
  if (tap) {
    if (racePowerups > 0) { racePowerups--; raceBoostUntil = now + 1500; mood = "^w^"; SFX(T_WAVE); }
    else { mood = ">w<"; SFX(T_WALL); }
  }
  if (now - raceLast < FRAME_INTERVAL_MS) return;
  float scale = constrain((now - raceLast) / (float)FRAME_INTERVAL_MS * gameSpeedFactor(APP_RACE), 0.25f, 3.0f);
  raceLast = now;
  raceFrame(now, scale);
}

// ================= CLOCK APP: clock, stopwatch, timer, alarm =================
// Turn = move the highlight (tabs on top, buttons at the bottom), press = use it.
// Number buttons (Min / Sec / Hour) switch to edit: turn changes the number, press again to finish.
// The stopwatch, timer and alarm keep running in the background while you use other apps.
enum { CT_CLOCK, CT_SW, CT_TIMER, CT_ALARM };
const char* const CT_NAMES[4] = {"Clock", "Stopwatch", "Timer", "Alarm"};
const uint8_t CT_NACT[4] = {2, 3, 4, 4};
int cTab = CT_CLOCK, cFocus = 0, cEdit = -1, cMode = 0, cSetH = 0, cSetM = 0, cBl = -2;
bool cForce = true;
bool swRun = false;
uint32_t swStart = 0, swAcc = 0, swLap[4];
int swLaps = 0;
uint16_t tmSet = 300;                                // timer length in seconds
bool tmRun = false;
uint32_t tmEnd = 0, tmLeftMs = 300000;
uint32_t ringStarted = 0, ringNext = 0;
const char* ringWhat = "";
const uint16_t T_ALARM[] PROGMEM = {1760,90, 0,60, 1760,90, 0,60, 1760,90};

uint16_t clkAccent() { return appAccent(APP_CLOCK); }
uint32_t swElapsed() { return swAcc + (swRun ? millis() - swStart : 0); }
uint32_t tmLeft() { if (!tmRun) return tmLeftMs; int32_t l = (int32_t)(tmEnd - millis()); return l > 0 ? l : 0; }
void fmtSw(uint32_t ms, char* b, size_t n) {
  uint32_t s = ms / 1000;
  if (s >= 3600) snprintf_P(b, n, PSTR("%u:%02u:%02u"), (unsigned)(s / 3600) % 100, (unsigned)(s / 60) % 60, (unsigned)s % 60);
  else snprintf_P(b, n, PSTR("%02u:%02u.%02u"), (unsigned)(s / 60) % 60, (unsigned)s % 60, (unsigned)(ms / 10) % 100);
}

// ---------- background service: runs every loop, whatever app is open ----------
void ringBegin(const char* what) { ringOn = true; ringWhat = what; ringStarted = millis(); ringNext = 0; oledDirty = true; cForce = true; }
void ringStop() { ringOn = false; soundAllOff(); oledDirty = true; cForce = true; }
void clockService(uint32_t now) {
  if (tmRun && (int32_t)(tmEnd - now) <= 0) { tmRun = false; tmLeftMs = 0; ringBegin("TIMER DONE"); }
  static uint32_t firedMin = 0;
  if (P.alarmOn) {
    int h, m, s; getHMS(&h, &m, &s);
    uint32_t em = clkEpoch / 60;
    if (h == P.alarmH && m == P.alarmM && em != firedMin) { firedMin = em; ringBegin("ALARM"); }
  }
  if (!ringOn) return;
  if (now - ringStarted > 30000) { ringStop(); return; }
  if ((int32_t)(now - ringNext) >= 0) { SFX(T_ALARM); ringNext = now + 900; }
}

// ---------- drawing ----------
void ctDrawTabs() {
  uint16_t acc = clkAccent();
  for (int i = 0; i < 4; i++) {
    int x = i * 80;
    bool on = i == cTab, foc = i == cFocus && cEdit < 0 && !cMode;
    tft.fillRect(x, 20, 80, 17, on ? acc : E_HEAD);
    putText(x + (80 - (int)strlen(CT_NAMES[i]) * 6) / 2, 25, 1, on ? E_ONACC : C_INK, on ? acc : E_HEAD, CT_NAMES[i]);
    if (foc) { tft.drawRect(x, 20, 80, 17, on ? E_ONACC : acc); tft.drawRect(x + 1, 21, 78, 15, on ? E_ONACC : acc); }
    else tft.drawFastVLine(x + 79, 22, 13, E_RULE);
  }
  tft.drawFastHLine(0, 37, W, acc);
}
void ctActLabel(int i, char* b, size_t n) {
  switch (cTab) {
    case CT_CLOCK: snprintf_P(b, n, PSTR("%s"), i == 0 ? FS("Set time") : (P.appFace ? FS("Face: analog") : FS("Face: digital"))); break;
    case CT_SW:    snprintf_P(b, n, PSTR("%s"), i == 0 ? (swRun ? "Stop" : (swElapsed() ? "Resume" : "Start")) : (i == 1 ? "Lap" : "Reset")); break;
    case CT_TIMER:
      if (i == 0) snprintf_P(b, n, PSTR("Min %02u"), (unsigned)(tmSet / 60) % 100);
      else if (i == 1) snprintf_P(b, n, PSTR("Sec %02u"), (unsigned)(tmSet % 60));
      else snprintf_P(b, n, PSTR("%s"), i == 2 ? (tmRun ? FS("Pause") : FS("Start")) : FS("Reset"));
      break;
    default:
      if (i == 0) snprintf_P(b, n, PSTR("Hour %02d"), P.alarmH % 24);
      else if (i == 1) snprintf_P(b, n, PSTR("Min %02d"), P.alarmM % 60);
      else snprintf_P(b, n, PSTR("%s"), i == 2 ? (P.alarmOn ? FS("Alarm: ON") : FS("Alarm: OFF")) : FS("Test"));
  }
}
void ctDrawButton(int i) {
  int n = CT_NACT[cTab], w = W / n, x = i * w;
  bool foc = cFocus == 4 + i && !cMode, edit = cEdit == 4 + i;
  uint16_t acc = clkAccent(), bg = edit ? acc : E_WHITE;
  char b[16];
  ctActLabel(i, b, sizeof b);
  tft.fillRect(x + 2, 186, w - 4, 22, bg);
  tft.drawRect(x + 2, 186, w - 4, 22, foc ? acc : E_RULE);
  if (foc) tft.drawRect(x + 3, 187, w - 6, 20, acc);
  putText(x + (w - (int)strlen(b) * 6) / 2, 193, 1, edit ? E_ONACC : (foc ? acc : C_INK), bg, b);
}
void ctDrawButtons() { tft.fillRect(0, 184, W, 26, C_LCD); for (int i = 0; i < CT_NACT[cTab]; i++) ctDrawButton(i); }
void clockAnalogFace(int h, int m, int s, bool force, bool dirty) {
  static int lastS = -1;
  if (!force && !dirty && s == lastS) return;
  lastS = s;
  uint16_t acc = clkAccent();
  drawAnalogClock(92, 112, 66, h, m, s, force, acc);
  char b[24];
  int day = (int)(clkEpoch / 86400UL), yr, mo, d;
  civilFromDays((uint32_t)day, &yr, &mo, &d);
  putText(184, 50, 3, acc, C_LCD, WD[(day + 4) % 7]);
  snprintf_P(b, sizeof b, PSTR("%02d %s"), d & 31, pgmItem(MN_P, (mo + 11) % 12)); putText(184, 82, 2, C_INK, C_LCD, b);
  snprintf_P(b, sizeof b, PSTR("%d"), yr % 10000);                       putText(184, 102, 2, C_MID, C_LCD, b);
  snprintf_P(b, sizeof b, PSTR("%02d:%02d"), dispHour(h) & 31, m & 63);  putText(184, 134, 3, C_INK, C_LCD, b);
  putText(278, 134, 1, C_MID, C_LCD, P.h12 ? ampm(h) : FS("  "));
}
void ctDrawClockTab(bool f, bool dirty, uint32_t now) {
  int h, m, s;
  if (cMode == 0) getHMS(&h, &m, &s); else { h = cSetH; m = cSetM; s = 0; }
  uint16_t acc = clkAccent();
  int bl = (cMode == 0) ? -1 : (int)((now / 400) % 2);
  if (P.appFace) {
    clockAnalogFace(h, m, s, f, dirty);
    if (bl != cBl || f) {
      cBl = bl;
      tft.fillRect(184, 162, 34, 4, (cMode == 1 && bl == 1) ? acc : C_LCD);
      tft.fillRect(220, 162, 34, 4, (cMode == 2 && bl == 1) ? acc : C_LCD);
    }
  } else {
    drawBigTime(17, 46, 46, 84, 10, dispHour(h), m, s, f);
    drawDate(152, 3, true, 0, f);
    if (f || dirty) putText(262, 140, 2, C_MID, C_LCD, P.h12 ? ampm(h) : FS("  "));
    if (bl != cBl || f) {
      cBl = bl;
      tft.fillRect(17, 136, 98, 5, (cMode == 1 && bl == 1) ? acc : C_LCD);
      tft.fillRect(143, 136, 98, 5, (cMode == 2 && bl == 1) ? acc : C_LCD);
    }
  }
}
void ctDrawSw(bool f) {
  static uint32_t last = 0;
  uint32_t now = millis();
  if (!f && (!swRun || now - last < 40)) return;
  last = now;
  char b[24];
  fmtSw(swElapsed(), b, sizeof b);
  putText((W - (int)strlen(b) * 24) / 2, 60, 4, swRun ? clkAccent() : C_INK, C_LCD, b);
  if (!f) return;
  tft.fillRect(0, 100, W, 80, C_LCD);
  if (!swLaps) { centerText(FS("press Lap while it runs to save splits"), 120, 1, C_MID, C_LCD); return; }
  for (int i = 0; i < swLaps; i++) {
    char t[12], d[12], line[56];
    fmtSw(swLap[i], t, sizeof t);
    fmtSw(swLap[i] - (i ? swLap[i - 1] : 0), d, sizeof d);
    snprintf_P(line, sizeof line, PSTR("Lap %d   %s   (+%s)"), i + 1, t, d);
    putText(40, 108 + i * 16, 1, i == swLaps - 1 ? clkAccent() : C_INK, C_LCD, line);
  }
}
void ctDrawTimer(bool f) {
  static uint32_t lastS = 0xFFFFFFFF;
  uint32_t left = tmLeft(), s = (left + 999) / 1000;
  if (!f && s == lastS) return;
  lastS = s;
  char b[16];
  snprintf_P(b, sizeof b, PSTR("%02u:%02u"), (unsigned)(s / 60) % 100, (unsigned)s % 60);
  bool done = !tmRun && !left;
  putText((W - 5 * 30) / 2, 54, 5, done ? E_CORAL : (tmRun ? clkAccent() : C_INK), C_LCD, b);
  int pct = tmSet ? (int)(100 - left / 10 / tmSet) : 0;
  drawGauge(30, 108, 260, pct, done ? E_CORAL : clkAccent());
  tft.fillRect(0, 124, W, 10, C_LCD);
  centerText(done ? FS("done! set Min/Sec, then Start") : (tmRun ? FS("running... (keeps going in other apps)") : FS("paused / ready")), 126, 1, C_MID, C_LCD);
}
void ctDrawAlarm(bool f) {
  if (!f) return;
  char b[48];
  snprintf_P(b, sizeof b, PSTR("%02d:%02d"), P.alarmH % 24, P.alarmM % 60);
  putText((W - 5 * 30) / 2, 54, 5, P.alarmOn ? clkAccent() : C_MID, C_LCD, b);
  tft.fillRect(0, 104, W, 40, C_LCD);
  if (P.alarmOn) {
    int h, m, s; getHMS(&h, &m, &s);
    int mins = ((P.alarmH * 60 + P.alarmM) - (h * 60 + m) + 1440) % 1440;
    if (!mins) mins = 1440;
    snprintf_P(b, sizeof b, PSTR("rings every day - next in %dh %02dm"), mins / 60, mins % 60);
    centerText(b, 110, 1, C_INK, C_LCD);
  } else centerText(FS("alarm is off"), 110, 1, C_MID, C_LCD);
  centerText(FS("(24-hour time; rings even if Sound is off)"), 126, 1, C_MID, C_LCD);
}
void ctDrawContent(bool f, bool dirty, uint32_t now) {
  if (f) tft.fillRect(0, 38, W, 146, C_LCD);
  if (ringOn) {
    bool on = (now / 300) % 2;
    tft.fillRect(20, 150, 280, 26, on ? E_CORAL : E_WHITE);
    putText(28, 157, 1, on ? E_ONACC : E_CORAL, on ? E_CORAL : E_WHITE, ringWhat);
    putText(170, 157, 1, on ? E_ONACC : E_CORAL, on ? E_CORAL : E_WHITE, FS("press to stop"));
  }
  switch (cTab) {
    case CT_CLOCK: ctDrawClockTab(f, dirty, now); break;
    case CT_SW:    ctDrawSw(f); break;
    case CT_TIMER: ctDrawTimer(f); break;
    default:       ctDrawAlarm(f); break;
  }
}
void clockAppEnter() {
  tft.fillScreen(C_LCD); drawTitleBar(FS("Clock"), clkAccent());
  cMode = 0; cEdit = -1; cBl = -2; cForce = true;
  if (cFocus >= 4 + CT_NACT[cTab]) cFocus = cTab;
  ctDrawTabs(); ctDrawButtons();
  drawDock(APP_CLOCK);
}
void ctActivate(int a, uint32_t now) {
  switch (cTab) {
    case CT_CLOCK:
      if (a == 0) { int h, m, s; getHMS(&h, &m, &s); cSetH = h; cSetM = m; cMode = 1; cBl = -2; }
      else { P.appFace ^= 1; prefsSave(); }
      cForce = true;
      break;
    case CT_SW:
      if (a == 0) { if (swRun) { swAcc += now - swStart; swRun = false; } else { swStart = now; swRun = true; } }
      else if (a == 1) {
        if (swRun) { if (swLaps == 4) { memmove(swLap, swLap + 1, 3 * sizeof swLap[0]); swLaps = 3; } swLap[swLaps++] = swElapsed(); }
      } else { swRun = false; swAcc = 0; swLaps = 0; }
      cForce = true;
      break;
    case CT_TIMER:
      if (a <= 1) cEdit = 4 + a;
      else if (a == 2) {
        if (tmRun) { tmLeftMs = tmLeft(); tmRun = false; }
        else if (tmSet) { if (!tmLeftMs) tmLeftMs = tmSet * 1000UL; tmEnd = now + tmLeftMs; tmRun = true; }
      } else { tmRun = false; tmLeftMs = tmSet * 1000UL; }
      cForce = true;
      break;
    default:
      if (a <= 1) cEdit = 4 + a;
      else if (a == 2) { P.alarmOn ^= 1; prefsSave(); }
      else ringBegin("ALARM TEST");
      cForce = true;
  }
}
void ctAdjust(int c) {
  int a = cEdit - 4;
  if (cTab == CT_TIMER) {
    int mn = tmSet / 60, sc = tmSet % 60;
    if (a == 0) mn = constrain(mn + c, 0, 99); else sc = ((sc + c) % 60 + 60) % 60;
    tmSet = mn * 60 + sc;
    if (!tmRun) tmLeftMs = tmSet * 1000UL;
  } else {
    if (a == 0) P.alarmH = ((P.alarmH + c) % 24 + 24) % 24; else P.alarmM = ((P.alarmM + c) % 60 + 60) % 60;
  }
}
void clockAppTick(uint32_t now) {
  int c = takeClicks() * ENC_DIR;
  takePress();
  bool sp = takeShort();
  titleTimeTick();
  bool dirty = false;
  if (cMode) {                                       // setting the clock: hours, then minutes
    if (c) { if (cMode == 1) cSetH = ((cSetH + c) % 24 + 24) % 24; else cSetM = ((cSetM + c) % 60 + 60) % 60; uiClick(); dirty = true; oledDirty = true; }
    if (sp) {
      if (cMode == 1) { cMode = 2; cBl = -2; }
      else {
        uint32_t day = clkEpoch - clkEpoch % 86400UL;
        clkEpoch = day + cSetH * 3600UL + cSetM * 60UL; clkMs = millis();
        cMode = 0; cBl = -2; forceClockRedraw(); cForce = true;
        ctDrawTabs(); ctDrawButtons();
      }
      oledDirty = true;
    }
  } else if (cEdit >= 0) {                           // editing a number button
    if (c) { ctAdjust(c); ctDrawButton(cEdit - 4); uiClick(); cForce = true; oledDirty = true; }
    if (sp) { int b = cEdit - 4; cEdit = -1; ctDrawButton(b); if (cTab == CT_ALARM) prefsSave(); SFX(T_HOP); cForce = true; }
  } else {
    int n = 4 + CT_NACT[cTab];
    if (c) {
      int old = cFocus;
      cFocus = ((cFocus + c) % n + n) % n;
      if (old < 4 || cFocus < 4) ctDrawTabs();
      if (old >= 4) ctDrawButton(old - 4);
      if (cFocus >= 4) ctDrawButton(cFocus - 4);
      uiClick(); oledDirty = true;
    }
    if (sp) {
      SFX(T_HOP);
      if (cFocus < 4) { cTab = cFocus; cMode = 0; cForce = true; cBl = -2; forceClockRedraw(); ctDrawTabs(); ctDrawButtons(); }
      else { ctActivate(cFocus - 4, now); ctDrawButtons(); if (cMode) ctDrawTabs(); }
      oledDirty = true;
    }
  }
  bool f = cForce;
  cForce = false;
  if (f) { forceClockRedraw(); cBl = -2; }
  ctDrawContent(f, dirty, now);
  static bool wasRun = false;                        // labels change when the timer/stopwatch stops by itself
  bool run = swRun || tmRun;
  if (run != wasRun) { wasRun = run; ctDrawButtons(); }
  static uint32_t lastE = 0;
  if (clkEpoch != lastE) { lastE = clkEpoch; oledDirty = true; }
}
void oledClock() {
  char l[4][24], b[16];
  switch (cTab) {
    case CT_SW:
      fmtSw(swElapsed(), b, sizeof b);
      snprintf_P(l[0], 24, PSTR("%s"), b); snprintf_P(l[1], 24, PSTR("%s"), swRun ? FS("running") : FS("stopped"));
      snprintf_P(l[2], 24, PSTR("%d lap%s"), swLaps, swLaps == 1 ? FS("") : FS("s")); snprintf_P(l[3], 24, PSTR("turn: pick button"));
      oledCard(FS("STOPWATCH"), l[0], l[1], l[2], l[3]);
      return;
    case CT_TIMER: {
      uint32_t s = (tmLeft() + 999) / 1000;
      snprintf_P(l[0], 24, PSTR("left %02u:%02u"), (unsigned)(s / 60) % 100, (unsigned)s % 60);
      snprintf_P(l[1], 24, PSTR("set  %02u:%02u"), (unsigned)(tmSet / 60) % 100, (unsigned)tmSet % 60);
      snprintf_P(l[2], 24, PSTR("%s"), tmRun ? "running" : "paused"); snprintf_P(l[3], 24, PSTR("%s"), cEdit >= 0 ? FS("turn: change value") : FS(""));
      oledCard(FS("TIMER"), l[0], l[1], l[2], l[3]);
      return;
    }
    case CT_ALARM:
      snprintf_P(l[0], 24, PSTR("%02d:%02d"), P.alarmH % 24, P.alarmM % 60); snprintf_P(l[1], 24, PSTR("%s"), P.alarmOn ? FS("alarm ON") : FS("alarm OFF"));
      snprintf_P(l[2], 24, PSTR("rings every day")); snprintf_P(l[3], 24, PSTR("%s"), cEdit >= 0 ? FS("turn: change value") : FS(""));
      oledCard(FS("ALARM"), l[0], l[1], l[2], l[3]);
      return;
  }
  int h, m, s;
  if (cMode == 0) getHMS(&h, &m, &s); else { h = cSetH; m = cSetM; }
  snprintf_P(l[0], 24, PSTR("%02d:%02d"), dispHour(h) & 31, m & 63);
  snprintf_P(l[1], 24, PSTR("%s"), cMode == 0 ? FS("clock is running") : (cMode == 1 ? FS("setting HOUR") : FS("setting MINUTE")));
  snprintf_P(l[2], 24, PSTR("%s"), swRun ? FS("stopwatch running") : (tmRun ? FS("timer running") : FS("")));
  snprintf_P(l[3], 24, PSTR("%s"), cMode ? FS("press = next / save") : FS("turn: tabs + buttons"));
  oledCard(FS("CLOCK"), l[0], l[1], l[2], l[3]);
}

// ================= STATS APP =================
void drawGraph(int x, int y, int h, const uint8_t* hist, uint16_t col) {
  tft.drawRect(x - 1, y - 1, HN * 2 + 2, h + 2, E_RULE);
  for (int i = 0; i < HN; i++) {
    int bh = hist[(hHead + i) % HN] * h / 100;
    tft.fillRect(x + i * 2, y, 2, h - bh, (i & 8) ? E_WHITE : E_ROW2);
    tft.fillRect(x + i * 2, y + h - bh, 2, bh, col);
  }
}
void statsDraw() {
  char b[40];
  int sum = 0;
  for (int i = 0; i < HN; i++) sum += histCpu[i];
  uint16_t acc = appAccent(APP_STATS);
  putText(12, 26, 1, C_INK, C_LCD, FS("CPU load (last 60 s)"));
  drawGraph(12, 38, 62, histCpu, acc);
  snprintf_P(b, sizeof b, PSTR("%3d%%"), cpuLoad);        putText(264, 44, 2, loadColor(cpuLoad), C_LCD, b);
  snprintf_P(b, sizeof b, PSTR("avg %d%%   "), sum / HN); putText(264, 68, 1, C_MID, C_LCD, b);
  drawGauge(264, 82, 48, cpuLoad, loadColor(cpuLoad));
  putText(12, 108, 1, C_INK, C_LCD, FS("RAM used (last 60 s)"));
  drawGraph(12, 120, 62, histRam, RGB565(150, 120, 240));
  snprintf_P(b, sizeof b, PSTR("%3d%%"), ramUsed);        putText(264, 126, 2, loadColor(ramUsed), C_LCD, b);
  snprintf_P(b, sizeof b, PSTR("%uK free "), (unsigned)(freeHeapNow / 1024)); putText(264, 150, 1, C_MID, C_LCD, b);
  drawGauge(264, 164, 48, ramUsed, loadColor(ramUsed));
  uint32_t u = millis() / 1000;
  snprintf_P(b, sizeof b, PSTR("%uMHz  up %02u:%02u:%02u  %dl/s   "), (unsigned)ESP.getCpuFreqMHz(),
           (unsigned)(u / 3600), (unsigned)((u / 60) % 60), (unsigned)(u % 60), loopHz);
  putText(12, 194, 1, C_INK, C_LCD, b);
}
void statsEnter() {
  tft.fillScreen(C_LCD); drawTitleBar(FS("Stats"), appAccent(APP_STATS));
  drawDock(APP_STATS);
  statsDraw(); statsFresh = false;
}
void statsTick() {
  takeClicks(); takePress(); takeShort();
  titleTimeTick();
  if (statsFresh) { statsFresh = false; statsDraw(); }
  static uint32_t lastE = 0;
  if (clkEpoch != lastE) { lastE = clkEpoch; oledDirty = true; }
}

// ================= OLED =================
void oledHead(const char* name) {
  oled.setTextSize(1);
  oled.setCursor(0, 0);   oled.print(name);
  oled.setCursor(104, 0); oled.print(mood);
}
void oledBig(const char* s) {
  oled.setTextSize(3);
  oled.setCursor((128 - (int)strlen(s) * 18) / 2, 14);
  oled.print(s);
  oled.setTextSize(1);
}
void oledLine(int y, const char* s) { oled.setCursor(0, y); oled.print(s); }
void oledCard(const char* title, const char* a, const char* b, const char* c, const char* d) {
  oled.setTextSize(2); oled.setCursor(0, 0); oled.print(title);
  oled.setTextSize(1);
  oled.drawFastHLine(0, 17, 128, SSD1306_WHITE);
  oledLine(21, a); oledLine(32, b); oledLine(43, c); oledLine(54, d);
}

// ================= SHARED WORK AREA =================
// Only one app runs at a time, so the big per-app buffers share one block of RAM. An app that finds
// wkOwner isn't itself reloads its data (from flash) before using it.
const int SQ_STEPS = 16, SQ_PATS = 8, SQ_TR = 3, SQ_ORDER = 32, SQ_FOCUS = 5 + SQ_TR * SQ_STEPS;
struct SeqSong { uint8_t ver, bpm, swing, len, gate; int8_t transpose; uint8_t order[SQ_ORDER]; uint8_t cell[SQ_PATS][SQ_TR][SQ_STEPS]; };
struct CalcCfg {
  uint8_t ver, deg, fmt, digits, pretty, frac;
  double mem[7], x, y;                               // A..F, M, and the x / y variables
  char fn[4][60], z[60];                             // graph functions y1..y4 and the 3D surface z(x,y)
};
union WorkArea {
  struct { char buf[2561]; char con[22][51]; } ed;           // Word + Code (+ program console)
  struct { char raw[118][24]; float val[118]; uint16_t pos[118]; uint8_t fmt[118]; uint8_t colw[26]; uint8_t n; } xl;   // Excel: sparse A1:Z60
  struct { char name[40][48]; uint32_t size[40]; } fm;        // Files
  struct { char title[12][27]; char buf[401]; } nt;          // Notes: list titles + the note being edited
  struct { char ssid[15][33]; } wf;                          // WiFi: the scan list
  SeqSong sq;                                                // Music: the song
  struct { char log[2049]; CalcCfg cc; } calc;               // Calc: history page + settings
  struct { char text[2600]; char ltxt[12][26]; char url[400]; char next[400]; uint8_t nlinks; } web;
  struct { char ssid[30][21]; int8_t rssi[30]; uint8_t ch[30], enc[30], n; int8_t hist[150]; int16_t ms[24];
           uint8_t host[48], mac[48][6]; } nl;                                          // NetLab   // Web
  struct { uint8_t tiles[64][18]; uint16_t recip[702]; uint16_t pal[6][4][8]; } run;    // Micor Run: tunnel + tables
  struct { char name[36][48]; char io[1700]; } bk;          // Books: the list (on the Psion or on the PC) + relay replies
};
WorkArea WK;
char openPath[64] = "";                                       // Files app -> "open this in Word/Sheet/Code"
int wkOwner = 0;                                              // which app's data is in WK right now
#define SQ WK.sq                                              // (names the apps already use)
#define CC WK.calc.cc
#define npTitle WK.nt.title
#define npBuf WK.nt.buf
#define spSsid WK.wf.ssid

// ================= WIFI (networks + device info) =================
// Pages: main list -> network scan -> on-screen keyboard (password) -> connecting -> result.
// The ESP8266 keeps the password in its own flash (WiFi.persistent), so it reconnects after a reboot.
enum { SP_MAIN, SP_SCAN, SP_KEYS, SP_JOIN, SP_INFO };
const int SP_NET_MAX = 15, SP_MAIN_N = 7, SP_SCAN_VIS = 8, SP_ROW = 22;
const char SP_ROWS_P[] PROGMEM = "WiFi radio\nNetworks\nSync clock\nForget network\nPC remote + files\nBluetooth\nAbout device";
int spPage = SP_MAIN, spSel = 0, spTop = 0, spNets = 0, spJoinResult = 0, spSig = -1;
int8_t spRssi[SP_NET_MAX];
bool spLock[SP_NET_MAX], spScanning = false, spInfoBt = false;
char spJoinSsid[33], spPass[65];
uint8_t spPassLen = 0;
uint32_t spJoinAt = 0, spAnimAt = 0;
uint8_t spDots = 0;

uint16_t spAccent() { return appAccent(APP_WIFI); }
bool spWifiOn() { return WiFi.getMode() != WIFI_OFF; }
bool spOnline() { return WiFi.status() == WL_CONNECTED; }
void spHeader(const char* a, const char* b) {
  tft.drawRect(3, 21, 314, 1, E_RULE);
  tft.fillRect(4, 22, 312, 14, E_HEAD);
  putText(10, 25, 1, C_INK, E_HEAD, a);
  putText(316 - 8 - (int)strlen(b) * 6, 25, 1, C_INK, E_HEAD, b);
}
void spStatus() {
  char b[54];
  if (!spWifiOn())   snprintf_P(b, sizeof b, PSTR("WiFi is off"));
  else if (spOnline()) snprintf_P(b, sizeof b, PSTR("online  %s  %d dBm"), WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
  else               snprintf_P(b, sizeof b, PSTR("WiFi on, not connected"));
  tft.fillRect(4, 196, 312, 12, C_LCD);
  tft.fillCircle(10, 201, 3, !spWifiOn() ? C_MID : (spOnline() ? E_MINT : E_AMBER));
  putText(18, 198, 1, C_INK, C_LCD, b);
}

// ---------- main page ----------
void spRowValue(int i, char* out, size_t n) {
  switch (i) {
    case 0: snprintf_P(out, n, PSTR("%s"), spWifiOn() ? FS("ON") : FS("OFF")); break;
    case 1: if (spOnline()) snprintf_P(out, n, PSTR("%.18s"), WiFi.SSID().c_str()); else snprintf_P(out, n, PSTR("scan + join")); break;
    case 2: snprintf_P(out, n, PSTR("%s"), ntpDone ? FS("synced") : (spOnline() ? FS("syncing...") : FS("needs WiFi"))); break;
    case 3: { String s = WiFi.SSID(); snprintf_P(out, n, PSTR("%.18s"), s.length() ? s.c_str() : FS("none saved")); break; }
    case 4: if (P.rcOn) snprintf_P(out, n, PSTR("ON  PIN %u"), P.rcPin); else snprintf_P(out, n, PSTR("OFF")); break;
    case 5: snprintf_P(out, n, PSTR("no radio")); break;
    default: snprintf_P(out, n, PSTR("ESP8266 %uMHz"), (unsigned)ESP.getCpuFreqMHz()); break;
  }
}
void spDrawMainRow(int i) {
  int y = 36 + i * SP_ROW;
  bool sel = i == spSel;
  ptrListSet(PL_WIFI, 4, 36, 304, SP_ROW, SP_MAIN_N, 0, spSel);
  uint16_t bg = (i & 1) ? E_ROW2 : E_WHITE, acc = spAccent();
  char v[24];
  spRowValue(i, v, sizeof v);
  tft.fillRect(4, y, 312, SP_ROW, bg);
  putText(12, y + 7, 1, C_INK, bg, pgmItem(SP_ROWS_P, i));
  uint16_t vc = (i == 0) ? (spWifiOn() ? E_MINT : C_MID) : (i == 4 ? (P.rcOn ? E_MINT : C_MID) : (i == 5 ? E_CORAL : (sel ? acc : C_MID)));
  putText(296 - (int)strlen(v) * 6, y + 7, 1, vc, bg, v);
  putText(304, y + 7, 1, sel ? acc : E_RULE, bg, FS(">"));
  if (sel) { tft.drawRect(4, y, 312, SP_ROW, acc); tft.drawRect(5, y + 1, 310, SP_ROW - 2, acc); }
}
void spDrawMain() {
  tft.fillRect(0, TITLE_H, W, DOCK_Y - 3 - TITLE_H, C_LCD);
  spHeader(FS("Setting"), FS("Value"));
  for (int i = 0; i < SP_MAIN_N; i++) spDrawMainRow(i);
  tft.drawRect(3, 21, 314, 14 + SP_MAIN_N * SP_ROW + 1, E_RULE);
  spStatus();
}
void spShowRemote() {                               // PC remote: how to connect (shown when it's turned on)
  spPage = SP_INFO; spInfoBt = false;
  tft.fillRect(0, TITLE_H, W, DOCK_Y - 3 - TITLE_H, C_LCD);
  tft.fillRect(6, 24, 308, 182, E_WHITE);
  tft.drawRect(6, 24, 308, 182, E_RULE);
  tft.fillRect(6, 24, 308, 3, P.rcOn ? E_MINT : C_MID);
  putText(16, 34, 2, C_INK, E_WHITE, FS("PC remote + files"));
  if (!P.rcOn) {
    putText(16, 62, 1, C_MID, E_WHITE, FS("turned off"));
    putText(16, 190, 1, C_MID, E_WHITE, FS("press: back"));
    return;
  }
  char b[48];
  putText(16, 60, 1, C_INK, E_WHITE, FS("On your PC (same WiFi), open:"));
  if (spOnline()) snprintf_P(b, sizeof b, PSTR("http://%s/"), WiFi.localIP().toString().c_str());
  else snprintf_P(b, sizeof b, PSTR("(connect to WiFi first)"));
  putText(16, 74, 2, spAccent(), E_WHITE, b);
  putText(16, 100, 1, C_INK, E_WHITE, FS("and type this PIN:"));
  snprintf_P(b, sizeof b, PSTR("%u"), P.rcPin);
  putText(16, 114, 3, C_INK, E_WHITE, b);
  for (int i = 0; i < 4; i++)
    putText(16, 146 + i * 10, 1, C_MID, E_WHITE, pgmItem(PSTR("You see both screens and drive the knob with the\nmouse + keyboard; files can be up/downloaded.\nGames aren't mirrored. Turn it off when you're\ndone (it uses a little battery + RAM)."), i));
  putText(16, 190, 1, C_MID, E_WHITE, FS("press: back"));
  oledDirty = true;
}

// ---------- network list ----------
void spSignal(int x, int y, int rssi, uint16_t on) {
  int bars = rssi > -55 ? 4 : (rssi > -65 ? 3 : (rssi > -75 ? 2 : 1));
  for (int i = 0; i < 4; i++) { int h = 2 + i * 2; tft.fillRect(x + i * 4, y + 8 - h, 3, h, i < bars ? on : E_RULE); }
}
void spLockIcon(int x, int y, uint16_t c) {
  tft.drawRoundRect(x + 1, y, 6, 6, 2, c);
  tft.fillRect(x, y + 3, 8, 6, c);
}
void spDrawScanRow(int s) {
  int i = spTop + s, y = 36 + s * 20;
  ptrListSet(PL_WIFISCAN, 4, 36, 304, 20, SP_SCAN_VIS, spTop, spSel);
  uint16_t bg = (s & 1) ? E_ROW2 : E_WHITE, acc = spAccent();
  tft.fillRect(4, y, 304, 20, bg);
  if (i < 2 + spNets) {
    bool sel = i == spSel;
    if (i == 0)      putText(12, y + 6, 1, sel ? acc : C_INK, bg, FS("< Back"));
    else if (i == 1) putText(12, y + 6, 1, sel ? acc : C_INK, bg, spScanning ? FS("Scanning...") : FS("Rescan"));
    else {
      int n = i - 2;
      char b[26]; snprintf_P(b, sizeof b, PSTR("%.24s"), spSsid[n]);
      bool cur = spOnline() && WiFi.SSID() == spSsid[n];
      putText(12, y + 6, 1, cur ? E_MINT : C_INK, bg, b);
      spSignal(250, y + 5, spRssi[n], sel ? acc : C_INK);
      if (spLock[n]) spLockIcon(284, y + 5, C_MID);
    }
    if (sel) { tft.drawRect(4, y, 304, 20, acc); tft.drawRect(5, y + 1, 302, 18, acc); }
  }
}
void spDrawScanBar() {
  int items = 2 + spNets, h = SP_SCAN_VIS * 20;
  tft.fillRect(308, 36, 8, h, E_ROW2);
  tft.drawFastVLine(308, 36, h, E_RULE);
  int th = items <= SP_SCAN_VIS ? h : h * SP_SCAN_VIS / items;
  int ty = items <= SP_SCAN_VIS ? 36 : 36 + (h - th) * spTop / (items - SP_SCAN_VIS);
  tft.fillRect(310, ty + 1, 4, th - 2, spAccent());
}
void spDrawScan() {
  tft.fillRect(0, TITLE_H, W, DOCK_Y - 3 - TITLE_H, C_LCD);
  spHeader(FS("Network"), FS("Signal  Lock"));
  for (int s = 0; s < SP_SCAN_VIS; s++) spDrawScanRow(s);
  spDrawScanBar();
  tft.drawRect(3, 21, 314, 14 + SP_SCAN_VIS * 20 + 1, E_RULE);
  char b[40];
  if (spScanning) snprintf_P(b, sizeof b, PSTR("looking for networks..."));
  else snprintf_P(b, sizeof b, PSTR("%d network%s found"), spNets, spNets == 1 ? FS("") : FS("s"));
  tft.fillRect(4, 199, 312, 10, C_LCD);
  putText(8, 200, 1, C_MID, C_LCD, b);
}
void spStartScan() {
  if (!spWifiOn()) WiFi.mode(WIFI_STA);
  WiFi.scanDelete();
  WiFi.scanNetworks(true);
  spScanning = true; spNets = 0; spSel = 1; spTop = 0;
}
void spCollectScan(int n) {
  spNets = 0;
  for (int i = 0; i < n && spNets < SP_NET_MAX; i++) {
    String s = WiFi.SSID(i);
    if (!s.length()) continue;                           // hidden network
    bool dup = false;
    for (int k = 0; k < spNets; k++) if (s == spSsid[k]) dup = true;
    if (dup) continue;
    snprintf_P(spSsid[spNets], 33, PSTR("%s"), s.c_str());
    spRssi[spNets] = (int8_t)WiFi.RSSI(i);
    spLock[spNets] = WiFi.encryptionType(i) != ENC_TYPE_NONE;
    spNets++;
  }
  for (int a = 1; a < spNets; a++)                       // strongest first
    for (int b = a; b > 0 && spRssi[b] > spRssi[b - 1]; b--) {
      char t[33]; memcpy(t, spSsid[b], 33); memcpy(spSsid[b], spSsid[b - 1], 33); memcpy(spSsid[b - 1], t, 33);
      int8_t r = spRssi[b]; spRssi[b] = spRssi[b - 1]; spRssi[b - 1] = r;
      bool l = spLock[b]; spLock[b] = spLock[b - 1]; spLock[b - 1] = l;
    }
  WiFi.scanDelete();
  spSel = spNets ? 2 : 1;
}

// ---------- password entry (shared knob keyboard) ----------
void spDrawField() {
  tft.fillRect(6, 38, 308, 22, E_WHITE);
  tft.drawRect(5, 37, 310, 24, spAccent());
  int from = spPassLen > 24 ? spPassLen - 24 : 0;
  putText(10, 41, 2, C_INK, E_WHITE, spPass + from);
  tft.fillRect(10 + (spPassLen - from) * 12, 41, 2, 16, spAccent());   // cursor
}
void spDrawKeys() {
  tft.fillRect(0, TITLE_H, W, DOCK_Y - 3 - TITLE_H, C_LCD);
  char b[48]; snprintf_P(b, sizeof b, PSTR("Password for %.30s"), spJoinSsid);
  putText(6, 25, 1, C_INK, C_LCD, b);
  spDrawField();
  kbDrawAll();
  putText(6, 196, 1, C_MID, C_LCD, FS("turn: pick key   press: type   hold: menu"));
}

// ---------- joining / info pages ----------
void spJoin() {
  WiFi.persistent(true);
  WiFi.mode(WIFI_STA);
  char pass[130];
  cp437ToUtf8(spPass, pass, sizeof pass);
  WiFi.begin(spJoinSsid, pass);
  spPage = SP_JOIN; spJoinResult = 0; spJoinAt = spAnimAt = millis(); spDots = 0;
  tft.fillRect(0, TITLE_H, W, DOCK_Y - 3 - TITLE_H, C_LCD);
  tft.fillRect(30, 60, 260, 96, E_WHITE);
  tft.drawRect(30, 60, 260, 96, E_RULE);
  tft.fillRect(30, 60, 260, 3, spAccent());
  centerText(FS("Connecting to"), 74, 1, C_MID, E_WHITE);
  char b[24]; snprintf_P(b, sizeof b, PSTR("%.20s"), spJoinSsid);
  centerText(b, 90, 2, C_INK, E_WHITE);
  oledDirty = true;
}
void spJoinDone(int result, const char* why) {
  spJoinResult = result;
  tft.fillRect(31, 112, 258, 43, E_WHITE);
  if (result > 0) {
    centerText(FS("Connected!"), 114, 2, E_MINT, E_WHITE);
    centerText(WiFi.localIP().toString().c_str(), 134, 1, C_INK, E_WHITE);
    ntpDone = false;                                    // grab the real time now that we're online
    SFX(T_HOME);
  } else {
    centerText(FS("Could not connect"), 114, 2, E_CORAL, E_WHITE);
    centerText(why, 134, 1, C_INK, E_WHITE);
    SFX(T_WALL);
  }
  centerText(result > 0 ? FS("press: done") : FS("press: edit password"), 144, 1, C_MID, E_WHITE);
  drawWifi(208, 4);
  oledDirty = true;
}
void spShowInfo(bool bluetooth) {
  spPage = SP_INFO; spInfoBt = bluetooth;
  tft.fillRect(0, TITLE_H, W, DOCK_Y - 3 - TITLE_H, C_LCD);
  tft.fillRect(6, 24, 308, 182, E_WHITE);
  tft.drawRect(6, 24, 308, 182, E_RULE);
  tft.fillRect(6, 24, 308, 3, bluetooth ? E_CORAL : spAccent());
  putText(16, 34, 2, C_INK, E_WHITE, bluetooth ? FS("Bluetooth") : FS("About device"));
  char b[52];
  int y = 60;
  if (bluetooth) {
    for (int i = 0; i < 9; i++, y += 13) putText(16, y, 1, i == 0 ? E_CORAL : C_INK, E_WHITE, pgmItem(PSTR("This board (ESP8266) has no Bluetooth radio,\nso it can't scan for or pair Bluetooth\nkeyboards, speakers or phones.\n\nTo add Bluetooth:\n- swap the NodeMCU for an ESP32 (BLE keyboards\n  work with the NimBLE library), or\n- add an ESP32 as a Bluetooth helper board.\n"), i));
  } else {
    uint8_t mac[6]; WiFi.macAddress(mac);
    snprintf_P(b, sizeof b, PSTR("chip     ESP8266  id %06X"), (unsigned)ESP.getChipId());        putText(16, y, 1, C_INK, E_WHITE, b); y += 13;
    snprintf_P(b, sizeof b, PSTR("cpu      %u MHz"), (unsigned)ESP.getCpuFreqMHz());               putText(16, y, 1, C_INK, E_WHITE, b); y += 13;
    snprintf_P(b, sizeof b, PSTR("flash    %u KB"), (unsigned)(ESP.getFlashChipRealSize() / 1024)); putText(16, y, 1, C_INK, E_WHITE, b); y += 13;
    snprintf_P(b, sizeof b, PSTR("free ram %u KB (block %u KB, stack %u B)"), (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getMaxFreeBlockSize() / 1024), (unsigned)ESP.getFreeContStack());
    putText(16, y, 1, C_INK, E_WHITE, b); y += 13;
    snprintf_P(b, sizeof b, PSTR("mac      %02X:%02X:%02X:%02X:%02X:%02X"), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    putText(16, y, 1, C_INK, E_WHITE, b); y += 13;
    snprintf_P(b, sizeof b, PSTR("ip       %s"), spOnline() ? WiFi.localIP().toString().c_str() : FS("-")); putText(16, y, 1, C_INK, E_WHITE, b); y += 13;
    snprintf_P(b, sizeof b, PSTR("core     %s"), ESP.getCoreVersion().c_str());                     putText(16, y, 1, C_INK, E_WHITE, b); y += 13;
    { char ver[12]; strncpy_P(ver, FW_VERSION, sizeof ver - 1); ver[sizeof ver - 1] = 0;
      snprintf_P(b, sizeof b, PSTR("firmware %s (built %s)"), ver, __DATE__); }                     putText(16, y, 1, C_INK, E_WHITE, b); y += 13;
    snprintf_P(b, sizeof b, PSTR("restart  %.41s"), lastCrash[0] ? lastCrash : ESP.getResetReason().c_str());
    putText(16, y, 1, lastCrash[0] ? E_CORAL : C_INK, E_WHITE, b);
  }
  putText(16, 190, 1, C_MID, E_WHITE, FS("press: back"));
  oledDirty = true;
}
void spGoMain() { spPage = SP_MAIN; spSig = -1; spDrawMain(); oledDirty = true; }
void spGoScan() { spPage = SP_SCAN; spStartScan(); spDrawScan(); oledDirty = true; }

// ---------- app ----------
void wifiEnter() {
  if (wkOwner != APP_WIFI) spNets = 0;               // the scan list lived in the shared area: scan again
  wkOwner = APP_WIFI;
  tft.fillScreen(C_LCD);
  drawTitleBar(FS("WiFi"), spAccent());
  drawDock(APP_WIFI);
  spSel = 0;
  spGoMain();
}
void wifiTick(uint32_t now) {
  int c = takeClicks() * ENC_DIR;
  takePress();
  bool sp = takeShort();
  titleTimeTick();
  switch (spPage) {
    case SP_MAIN: {
      if (c) {
        int old = spSel;
        spSel = ((spSel + c) % SP_MAIN_N + SP_MAIN_N) % SP_MAIN_N;
        if (old != spSel) { spDrawMainRow(old); spDrawMainRow(spSel); uiClick(); oledDirty = true; }
      }
      if (sp) {
        switch (spSel) {
          case 0: if (spWifiOn()) WiFi.mode(WIFI_OFF); else { WiFi.mode(WIFI_STA); WiFi.begin(); } break;
          case 1: spGoScan(); return;
          case 2: ntpDone = false; break;
          case 3: WiFi.disconnect(); break;                // also erases the saved password
          case 4: rcSetOn(!P.rcOn); SFX(T_HOP); if (P.rcOn) { spShowRemote(); return; } break;
          case 5: spShowInfo(true); return;
          default: spShowInfo(false); return;
        }
        SFX(T_HOP); spSig = -1;
      }
      int sig = (int)WiFi.getMode() * 100 + (int)WiFi.status() * 2 + (ntpDone ? 1 : 0);
      if (sig != spSig) {                                 // something changed: refresh values
        spSig = sig;
        for (int i = 0; i < SP_MAIN_N; i++) spDrawMainRow(i);
        spStatus(); drawWifi(208, 4); oledDirty = true;
      }
      break;
    }
    case SP_SCAN: {
      if (spScanning) {
        int n = WiFi.scanComplete();
        if (n >= 0 || n == WIFI_SCAN_FAILED) { spScanning = false; if (n > 0) spCollectScan(n); else spNets = 0; spDrawScan(); oledDirty = true; }
      }
      int items = 2 + spNets;
      if (c) {
        int old = spSel, oldTop = spTop;
        spSel = ((spSel + c) % items + items) % items;
        if (spSel < spTop) spTop = spSel;
        else if (spSel >= spTop + SP_SCAN_VIS) spTop = spSel - SP_SCAN_VIS + 1;
        if (spTop != oldTop) { for (int s = 0; s < SP_SCAN_VIS; s++) spDrawScanRow(s); spDrawScanBar(); }
        else if (old != spSel) { spDrawScanRow(old - spTop); spDrawScanRow(spSel - spTop); }
        if (old != spSel) { uiClick(); oledDirty = true; }
      }
      if (sp) {
        if (spSel == 0) { spSel = 1; spGoMain(); }
        else if (spSel == 1) { if (!spScanning) { spStartScan(); spDrawScan(); } }
        else {
          int n = spSel - 2;
          snprintf_P(spJoinSsid, sizeof spJoinSsid, PSTR("%s"), spSsid[n]);
          spPassLen = 0; spPass[0] = 0;
          if (!spLock[n]) spJoin();
          else { spPage = SP_KEYS; kbBegin(KB_LAYOUT_FORM, spAccent()); spDrawKeys(); oledDirty = true; }
        }
      }
      break;
    }
    case SP_KEYS: {
      int r = kbTick(c, sp);
      if ((r == KB_TYPED || r == KB_SPACE) && spPassLen < 63) { spPass[spPassLen++] = r == KB_SPACE ? ' ' : kbChar; spPass[spPassLen] = 0; }
      else if (r == KB_DEL && spPassLen) spPass[--spPassLen] = 0;
      else if (r == KB_OK) { spJoin(); return; }
      else if (r == KB_CANCEL) { spGoScan(); return; }
      if (r != KB_NONE && r != KB_PAGE) { spDrawField(); oledDirty = true; }
      break;
    }
    case SP_JOIN: {
      if (spJoinResult == 0) {
        wl_status_t st = WiFi.status();
        uint32_t t = now - spJoinAt;
        if (st == WL_CONNECTED) spJoinDone(1, FS(""));
        else if (st == WL_WRONG_PASSWORD) spJoinDone(-1, FS("wrong password"));
        else if (st == WL_CONNECT_FAILED) spJoinDone(-1, FS("connection refused"));
        else if (st == WL_NO_SSID_AVAIL && t > 8000) spJoinDone(-1, FS("network not found"));
        else if (t > 20000) spJoinDone(-1, FS("timed out"));
        else if (now - spAnimAt >= 300) {
          spAnimAt = now; spDots = (spDots + 1) % 8;
          for (int i = 0; i < 7; i++) tft.fillCircle(124 + i * 12, 124, 3, i == spDots ? spAccent() : E_HEAD);
        }
      } else if (sp) {
        if (spJoinResult > 0) { spSel = 1; spGoMain(); }
        else { spPage = SP_KEYS; kbBegin(KB_LAYOUT_FORM, spAccent()); kbKey = 43; spDrawKeys(); }
      }
      break;
    }
    default:
      if (sp) spGoMain();
      break;
  }
}
void oledWifi() {
  char l[4][24];
  snprintf_P(l[0], 24, PSTR("%s"), !spWifiOn() ? FS("radio  OFF") : (spOnline() ? FS("status ONLINE") : FS("status OFFLINE")));
  snprintf_P(l[1], 24, PSTR("%.21s"), spOnline() ? WiFi.SSID().c_str() : FS("-"));
  snprintf_P(l[2], 24, PSTR("%.21s"), spOnline() ? WiFi.localIP().toString().c_str() : FS("no ip"));
  const char* title = "SETTINGS";
  switch (spPage) {
    case SP_SCAN: title = "NETWORKS"; snprintf_P(l[3], 24, PSTR("%d found"), spNets); break;
    case SP_KEYS: title = "PASSWORD"; snprintf_P(l[2], 24, PSTR("%.21s"), spJoinSsid); snprintf_P(l[3], 24, PSTR("%d chars typed"), spPassLen); break;
    case SP_JOIN: title = "JOINING"; snprintf_P(l[3], 24, PSTR("%s"), spJoinResult > 0 ? FS("connected :3") : (spJoinResult < 0 ? FS("failed ;-;") : FS("please wait..."))); break;
    case SP_INFO: title = spInfoBt ? "BLUETOOTH" : "ABOUT"; snprintf_P(l[3], 24, PSTR("%s"), spInfoBt ? FS("no radio on ESP8266") : FS("press: back")); break;
    default: snprintf_P(l[3], 24, PSTR("> %s"), pgmItem(SP_ROWS_P, spSel)); break;
  }
  oledCard(title, l[0], l[1], l[2], l[3]);
}


// ================= SETTINGS (themes + lots of knobs to twiddle) =================
// One long grouped list. Press = change the value (saved to flash right away). "Time zone" and
// "Owner name" open an editor: knob adjusts the zone, the knob keyboard types the name.
enum { PR_H_DISPLAY, PR_THEME, PR_BRIGHT, PR_ACCENT, PR_ZEBRA, PR_INVERT, PR_FLIP, PR_SPLASH, PR_BOOTSPEED, PR_BOOTSOUND, PR_BOOTPREVIEW, PR_SAVER, PR_SAVERSTYLE, PR_OLEDSAVER, PR_SAVERTEXT, PR_PREVIEW, PR_PANEL,
       PR_H_LOOK, PR_DESK, PR_DESKICONS, PR_OFFICE,
       PR_H_GAMES, PR_GAMEPAL,
       PR_H_CLOCK, PR_HOMEFACE, PR_APPFACE, PR_SECHAND, PR_H12, PR_DATE, PR_TZ,
       PR_H_SOUND, PR_SOUND, PR_MUSIC, PR_KEYSHIFT, PR_TEMPO, PR_CLICK, PR_PITCH,
       PR_H_INPUT, PR_KNOB, PR_HOLD, PR_MWHEEL, PR_MMOVE,
       PR_H_OLED, PR_OLED, PR_CONTRAST,
       PR_H_SLEEP, PR_SLEEPAFTER, PR_SLEEPHOLD, PR_SLEEPNOW, PR_AOD, PR_AODSTYLE, PR_AODBRIGHT, PR_AODDATE, PR_AODSECS, PR_AODPREVIEW,
       PR_H_SYSTEM, PR_OWNER, PR_RESET, PR_N };
const char PR_LABEL_P[] PROGMEM = "DISPLAY\nTheme\nScreen brightness\nAccent colours\nZebra rows\nInvert colours\nFlip screen\nBoot screen\nBoot speed\nBoot sound\nPreview boot\nScreensaver\nSaver style\nOLED saver\nSaver text\nPreview saver\nHome panel\nLOOK\nHome screen style\nSeven: apps on\nOffice look\nGAMES\nGame colours\nCLOCK\nHome clock\nClock app face\nSecond hand\nTime format\nDate format\nTime zone\nSOUND\nVolume\nGame music\nMusic key changes\nMusic tempo\nKnob click\nClick pitch\nINPUT\nKnob direction\nHold to go back\nPC mouse wheel\nPC mouse moves\nOLED\nOLED screen\nOLED contrast\nSLEEP + ALWAYS-ON\nSleep after\nHold 2.5 s = sleep\nSleep now\nAlways-on OLED\nAlways-on style\nAlways-on brightness\nAlways-on date\nAlways-on seconds\nPreview always-on\nSYSTEM\nOwner name\nReset everything";
const int PR_VIS = 9, PR_ROW = 20;
const uint16_t CLICK_HZ[3] = {1600, 2400, 3600};
const uint8_t OLED_CONTRAST[3] = {8, 110, 255};
int prSel = PR_THEME, prTop = 0;
bool prEditTz = false, prResetArm = false, prTyping = false;
int prTypeWhat = 0;                               // 0 owner name, 1 screensaver text
char prName[21];
uint8_t prNameLen = 0;

bool prHeader(int r) {
  return r == PR_H_DISPLAY || r == PR_H_CLOCK || r == PR_H_SOUND || r == PR_H_INPUT || r == PR_H_OLED || r == PR_H_SYSTEM || r == PR_H_GAMES ||
         r == PR_H_LOOK || r == PR_H_SLEEP;
}
uint16_t prAccent() { return appAccent(APP_SETTINGS); }
void applySettings() {                         // push every preference into the hardware / engine
  applyTheme();
  tft.invertDisplay(!P.invert);                  // this IPS panel shows true colours with inversion ON (tft.init's default)
  tft.setRotation(P.flip ? 3 : 1);
  ENC_DIR = P.knobRev ? -1 : 1;
  blSet(true);
  T_TICK[0] = CLICK_HZ[P.clickPitch % 3];
  if (!P.sound) soundAllOff();
  if (oledOk) {
    oled.ssd1306_command(P.oledOn ? SSD1306_DISPLAYON : SSD1306_DISPLAYOFF);
    oled.ssd1306_command(SSD1306_SETCONTRAST);
    oled.ssd1306_command(OLED_CONTRAST[P.contrast % 3]);
  }
}
void prValue(int r, char* b, size_t n, uint16_t* col) {
  static const char* const ONOFF[2] = {"OFF", "ON"};
  *col = C_MID;
  switch (r) {
    case PR_THEME:    snprintf_P(b, n, PSTR("%s"), pgmItem(PSTR("Platinum\n90s LCD\nDark"), P.theme % 3)); break;
    case PR_BRIGHT:
      if (TFT_BL_PIN < 0) { snprintf_P(b, n, PSTR("always on (3V3)")); *col = C_MID; break; }
      snprintf_P(b, n, PSTR("%s"), pgmItem(PSTR("5 full\n4 bright\n3 medium\n2 dim\n1 night"), P.tftBright % 5)); break;
    case PR_ACCENT:   snprintf_P(b, n, PSTR("%s"), P.theme == TH_LCD ? FS("ink only") : (P.accentMode ? FS("single blue") : FS("one per app"))); break;
    case PR_SAVERSTYLE: snprintf_P(b, n, PSTR("%s"), pgmItem(PSTR("paws\n3D pipes\nbouncy text\nMicor face\nfurry eyes\nstarfield\nmystify\nmatrix rain\nshuffle"), P.saverStyle % 9)); break;
    case PR_OLEDSAVER:  snprintf_P(b, n, PSTR("%s"), pgmItem(PSTR("match screen\nfurry eyes\npaws\nstars\nbouncy text\nclock\noff"), P.oledSaver % 7)); break;
    case PR_SAVERTEXT:  snprintf_P(b, n, PSTR("%.18s"), P.saverText); break;
    case PR_PREVIEW: case PR_BOOTPREVIEW: snprintf_P(b, n, PSTR("press to watch")); break;
    case PR_SPLASH:     snprintf_P(b, n, PSTR("%s"), pgmItem(PSTR("OFF\nclassic\ninfo\nterminal"), P.splash % 4)); *col = P.splash ? E_MINT : C_MID; break;
    case PR_BOOTSPEED:  snprintf_P(b, n, PSTR("%s"), pgmItem(PSTR("fast\nnormal\nslow\nwait for a press"), P.bootSpeed % 4)); break;
    case PR_SAVER:    snprintf_P(b, n, PSTR("%s"), pgmItem(PSTR("OFF\n1 min\n3 min\n10 min"), P.saver % 4)); break;
    case PR_PANEL:    snprintf_P(b, n, PSTR("%s"), P.panel ? FS("owner card") : FS("system stats")); break;
    case PR_HOMEFACE: snprintf_P(b, n, PSTR("%s"), pgmItem(PSTR("analog+digital\ndigital\nanalog"), P.homeFace % 3)); break;
    case PR_APPFACE:  snprintf_P(b, n, PSTR("%s"), P.appFace ? FS("analog") : FS("7-segment")); break;
    case PR_H12:      snprintf_P(b, n, PSTR("%s"), P.h12 ? FS("12 hour") : FS("24 hour")); break;
    case PR_DATE:     snprintf_P(b, n, PSTR("%s"), pgmItem(PSTR("MON 02 NOV 2026\nMON 11/02/2026\n2026-11-02"), P.dateFmt % 3)); break;
    case PR_TZ:       snprintf_P(b, n, PSTR("UTC%+d"), (int)P.tz); break;
    case PR_SOUND:    snprintf_P(b, n, PSTR("%s"), !P.sound ? FS("off") : pgmItem(PSTR("high\nmid\nlow"), P.volume % 3));
                      *col = P.sound ? E_MINT : C_MID; break;
    case PR_TEMPO:    snprintf_P(b, n, PSTR("%s"), pgmItem(PSTR("chill\nnormal\nzoomy"), P.tempo % 3)); break;
    case PR_PITCH:    snprintf_P(b, n, PSTR("%s"), pgmItem(PSTR("low\nmid\nhigh"), P.clickPitch % 3)); break;
    case PR_KNOB:     snprintf_P(b, n, PSTR("%s"), P.knobRev ? FS("reversed") : FS("normal")); break;
    case PR_HOLD:     snprintf_P(b, n, PSTR("%s"), pgmItem(PSTR("0.4 s\n0.6 s\n0.9 s"), P.hold % 3)); break;
    case PR_GAMEPAL:  snprintf_P(b, n, PSTR("%s"), pgmItem(PSTR("original\n90s LCD\npea green\ngrayscale\nsepia\npastel\nnight"), P.gamePal % 7)); break;
    case PR_CONTRAST: snprintf_P(b, n, PSTR("%s"), pgmItem(PSTR("low\nmid\nhigh"), P.contrast % 3)); break;
    case PR_DESK:       snprintf_P(b, n, PSTR("%s"), pgmItem(DESK_NAMES_P, P.deskStyle % DS_N)); *col = E_MINT; break;
    case PR_DESKICONS:  snprintf_P(b, n, PSTR("%s"), P.deskIcons ? FS("start menu only") : FS("desktop + start")); break;
    case PR_OFFICE:     snprintf_P(b, n, PSTR("%s"), P.officeLook ? FS("classic 95") : FS("modern 365")); break;
    case PR_MWHEEL: case PR_MMOVE: snprintf_P(b, n, PSTR("%s"), pgmItem(PSTR("very slow\nslow\nnormal\nfast\nvery fast"), (r == PR_MWHEEL ? P.mWheel : P.mMove) % 5)); break;
    case PR_SLEEPAFTER: snprintf_P(b, n, PSTR("%s"), pgmItem(PSTR("never\n1 min\n2 min\n5 min\n10 min\n30 min"), P.sleepAfter % 6)); break;
    case PR_SLEEPNOW: case PR_AODPREVIEW: snprintf_P(b, n, PSTR("press")); break;
    case PR_AODSTYLE:   snprintf_P(b, n, PSTR("%s"), pgmItem(AOD_NAMES_P, P.aodStyle % AOD_N)); break;
    case PR_AODBRIGHT:  snprintf_P(b, n, PSTR("%s"), pgmItem(PSTR("1 faint\n2 dim\n3 medium\n4 bright\n5 full"), P.aodBright % 5)); break;
    case PR_OWNER:    snprintf_P(b, n, PSTR("%s"), P.owner[0] ? P.owner : FS("(nobody)")); break;
    case PR_RESET:    snprintf_P(b, n, PSTR("%s"), prResetArm ? FS("press again!") : FS("press twice")); *col = prResetArm ? E_CORAL : C_MID; break;
    default: {
      uint8_t v = 0;
      switch (r) {
        case PR_ZEBRA: v = P.zebra; break;     case PR_INVERT: v = P.invert; break;   case PR_FLIP: v = P.flip; break;
        case PR_BOOTSOUND: v = P.bootSound; break; case PR_SECHAND: v = P.secHand; break; 
        case PR_MUSIC: v = P.music; break;     case PR_KEYSHIFT: v = P.keyShift; break; case PR_CLICK: v = P.click; break;
        case PR_OLED: v = P.oledOn; break;
        case PR_SLEEPHOLD: v = P.sleepHold; break; case PR_AOD: v = P.aodOn; break;
        case PR_AODDATE: v = P.aodDate; break;     case PR_AODSECS: v = P.aodSecs; break;
      }
      snprintf_P(b, n, PSTR("%s"), ONOFF[v ? 1 : 0]);
      *col = v ? E_MINT : C_MID;
    }
  }
}
void prDrawRow(int s) {
  int r = prTop + s, y = 22 + s * PR_ROW;
  ptrListSet(PL_SETTINGS, 4, 22, 304, PR_ROW, PR_VIS, prTop, prSel);
  if (r >= PR_N) { tft.fillRect(4, y, 304, PR_ROW, E_WHITE); return; }
  uint16_t acc = prAccent();
  if (prHeader(r)) {
    tft.fillRect(4, y, 304, PR_ROW, E_HEAD);
    tft.fillRect(4, y + 4, 3, PR_ROW - 8, acc);
    putText(14, y + 7, 1, C_INK, E_HEAD, pgmItem(PR_LABEL_P, r));
    tft.drawFastHLine(4, y + PR_ROW - 1, 304, E_RULE);
    return;
  }
  bool sel = r == prSel;
  uint16_t bg = (r & 1) ? E_ROW2 : E_WHITE, col;
  char v[24];
  prValue(r, v, sizeof v, &col);
  tft.fillRect(4, y, 304, PR_ROW, bg);
  putText(18, y + 6, 1, C_INK, bg, pgmItem(PR_LABEL_P, r));
  int vx = 296 - (int)strlen(v) * 6;
  if (sel && r == PR_TZ && prEditTz) {
    tft.fillRect(vx - 4, y + 3, (int)strlen(v) * 6 + 8, PR_ROW - 6, acc);
    putText(vx, y + 6, 1, E_ONACC, acc, v);
  } else putText(vx, y + 6, 1, sel && col == C_MID ? acc : col, bg, v);
  if (sel) { tft.drawRect(4, y, 304, PR_ROW, acc); tft.drawRect(5, y + 1, 302, PR_ROW - 2, acc); }
}
void prDrawScroll() {
  int h = PR_VIS * PR_ROW;
  tft.fillRect(308, 22, 8, h, E_ROW2);
  tft.drawFastVLine(308, 22, h, E_RULE);
  int th = h * PR_VIS / PR_N, ty = 22 + (h - th) * prTop / (PR_N - PR_VIS);
  tft.fillRect(310, ty + 1, 4, th - 2, prAccent());
}
void prDrawAll() {
  tft.fillScreen(C_LCD);
  drawTitleBar(FS("Settings"), prAccent());
  tft.drawRect(3, 21, 314, PR_VIS * PR_ROW + 2, E_RULE);
  for (int s = 0; s < PR_VIS; s++) prDrawRow(s);
  prDrawScroll();
  drawDock(APP_SETTINGS);
}
void prMove(int clicks) {
  int dir = clicks > 0 ? 1 : -1, n = abs(clicks), old = prSel, oldTop = prTop;
  while (n--) {
    do prSel = (prSel + dir + PR_N) % PR_N; while (prHeader(prSel));
  }
  if (prSel - 1 < prTop) prTop = max(0, prSel - 1);                 // keep the group header in view
  else if (prSel >= prTop + PR_VIS) prTop = prSel - PR_VIS + 1;
  prResetArm = false;
  if (prTop != oldTop) { for (int s = 0; s < PR_VIS; s++) prDrawRow(s); prDrawScroll(); }
  else if (old != prSel) { prDrawRow(old - prTop); prDrawRow(prSel - prTop); }
}
void prRedrawSel() { if (prSel >= prTop && prSel < prTop + PR_VIS) prDrawRow(prSel - prTop); }
void prDrawNameEditor() {
  tft.fillRect(0, TITLE_H, W, DOCK_Y - 3 - TITLE_H, C_LCD);
  putText(6, 25, 1, C_INK, C_LCD, prTypeWhat ? FS("Screensaver text (bounces around)") : FS("Owner name (shown on the home screen)"));
  tft.fillRect(6, 38, 308, 22, E_WHITE);
  tft.drawRect(5, 37, 310, 24, prAccent());
  putText(10, 41, 2, C_INK, E_WHITE, prName);
  tft.fillRect(10 + prNameLen * 12, 41, 2, 16, prAccent());
  putText(6, 196, 1, C_MID, C_LCD, prTypeWhat ? FS("max 20 letters   OK: save   cancel: back") : FS("max 15 letters   OK: save   cancel: back"));
}
void prChange(uint32_t now) {
  bool full = false;
  switch (prSel) {
    case PR_THEME:    P.theme = (P.theme + 1) % 3; full = true; break;
    case PR_BRIGHT:   if (TFT_BL_PIN >= 0) { P.tftBright = (P.tftBright + 1) % 5; blSet(true); } break;
    case PR_ACCENT:   P.accentMode ^= 1; full = true; break;
    case PR_ZEBRA:    P.zebra ^= 1; full = true; break;
    case PR_INVERT:   P.invert ^= 1; break;
    case PR_FLIP:     P.flip ^= 1; full = true; break;
    case PR_SPLASH:   P.splash = (P.splash + 1) % 4; break;
    case PR_BOOTSPEED: P.bootSpeed = (P.bootSpeed + 1) % 4; break;
    case PR_BOOTSOUND: P.bootSound ^= 1; break;
    case PR_BOOTPREVIEW: prefsSave(); { uint8_t s0 = P.splash; if (!P.splash) P.splash = 1; drawSplash(); P.splash = s0; }
      for (uint32_t t = millis(); digitalRead(BTN_PIN) == LOW && millis() - t < 3000; ) delay(5);
      fPress = fShort = fLong = false; prDrawAll(); return;
    case PR_SAVER:    P.saver = (P.saver + 1) % 4; break;
    case PR_SAVERSTYLE: P.saverStyle = (P.saverStyle + 1) % 9; break;
    case PR_OLEDSAVER:  P.oledSaver = (P.oledSaver + 1) % 7; break;
    case PR_PREVIEW:    enterApp(APP_MENU); saverStart(); return;
    case PR_PANEL:    P.panel ^= 1; break;
    case PR_HOMEFACE: P.homeFace = (P.homeFace + 1) % 3; break;
    case PR_APPFACE:  P.appFace ^= 1; break;
    case PR_SECHAND:  P.secHand ^= 1; break;
    case PR_H12:      P.h12 ^= 1; drawTitleTime(); break;
    case PR_DATE:     P.dateFmt = (P.dateFmt + 1) % 3; break;
    case PR_TZ:       prEditTz = !prEditTz; break;
    case PR_SOUND: {                                 // low -> mid -> high -> off -> low
      int st = !P.sound ? 3 : 2 - (P.volume % 3);       // 0 low, 1 mid, 2 high, 3 off
      st = (st + 1) % 4;
      if (st == 3) { soundAllOff(); P.sound = 0; }
      else { P.sound = 1; P.volume = 2 - st; soundOn(880); delay(120); soundOff(); }
      break;
    }
    case PR_MUSIC:    P.music ^= 1; break;
    case PR_KEYSHIFT: P.keyShift ^= 1; break;
    case PR_TEMPO:    P.tempo = (P.tempo + 1) % 3; break;
    case PR_CLICK:    P.click ^= 1; break;
    case PR_PITCH:    P.clickPitch = (P.clickPitch + 1) % 3; break;
    case PR_KNOB:     P.knobRev ^= 1; break;
    case PR_HOLD:     P.hold = (P.hold + 1) % 3; break;
    case PR_OLED:     P.oledOn ^= 1; break;
    case PR_GAMEPAL:  P.gamePal = (P.gamePal + 1) % 7; break;
    case PR_CONTRAST: P.contrast = (P.contrast + 1) % 3; break;
    case PR_DESK:       P.deskStyle = (P.deskStyle + 1) % DS_N; break;
    case PR_DESKICONS:  P.deskIcons ^= 1; break;
    case PR_OFFICE:     P.officeLook ^= 1; break;
    case PR_MWHEEL:     P.mWheel = (P.mWheel + 1) % 5; break;
    case PR_MMOVE:      P.mMove = (P.mMove + 1) % 5; break;
    case PR_SLEEPAFTER: P.sleepAfter = (P.sleepAfter + 1) % 6; break;
    case PR_SLEEPHOLD:  P.sleepHold ^= 1; break;
    case PR_SLEEPNOW:   prefsSave(); sleepEnter(); return;
    case PR_AOD:        P.aodOn ^= 1; break;
    case PR_AODSTYLE:   P.aodStyle = (P.aodStyle + 1) % AOD_N; break;
    case PR_AODBRIGHT:  P.aodBright = (P.aodBright + 1) % 5; break;
    case PR_AODDATE:    P.aodDate ^= 1; break;
    case PR_AODSECS:    P.aodSecs ^= 1; break;
    case PR_AODPREVIEW: prefsSave(); aodPreview(); fPress = fShort = fLong = false; takeClicks(); prDrawAll(); oledDirty = true; return;
    case PR_OWNER: case PR_SAVERTEXT:
      prTyping = true; prTypeWhat = prSel == PR_SAVERTEXT;
      snprintf_P(prName, sizeof prName, PSTR("%s"), prTypeWhat ? P.saverText : P.owner);
      prNameLen = strlen(prName);
      kbBegin(KB_LAYOUT_FORM, prAccent());
      prDrawNameEditor(); kbDrawAll();
      oledDirty = true;
      return;
    case PR_RESET:
      if (!prResetArm) { prResetArm = true; prRedrawSel(); return; }
      prResetArm = false; prefsDefaults(); full = true;
      break;
  }
  applySettings();
  prefsSave();
  SFX(T_TICK);                                       // let you hear the new click pitch
  if (full) prDrawAll(); else prRedrawSel();
  oledDirty = true;
  (void)now;
}
void prefsEnter() {
  prEditTz = false; prResetArm = false; prTyping = false;
  if (prHeader(prSel)) prSel = PR_THEME;
  prDrawAll();
}
void prefsTick(uint32_t now) {
  int c = takeClicks() * ENC_DIR;
  takePress();
  bool sp = takeShort();
  titleTimeTick();
  if (prTyping) {
    int r = kbTick(c, sp);
    if ((r == KB_TYPED || r == KB_SPACE) && prNameLen < (prTypeWhat ? 20 : 15)) { prName[prNameLen++] = r == KB_SPACE ? ' ' : kbChar; prName[prNameLen] = 0; }
    else if (r == KB_DEL && prNameLen) prName[--prNameLen] = 0;
    else if (r == KB_OK || r == KB_CANCEL) {
      if (r == KB_OK) {
        if (prTypeWhat) snprintf_P(P.saverText, sizeof P.saverText, PSTR("%s"), prName);
        else snprintf_P(P.owner, sizeof P.owner, PSTR("%s"), prName);
        prefsSave();
      }
      prTyping = false; prDrawAll(); oledDirty = true;
      return;
    }
    if (r != KB_NONE && r != KB_PAGE) {
      tft.fillRect(6, 38, 308, 22, E_WHITE);
      putText(10, 41, 2, C_INK, E_WHITE, prName);
      tft.fillRect(10 + prNameLen * 12, 41, 2, 16, prAccent());
      oledDirty = true;
    }
    return;
  }
  if (c) {
    if (prEditTz) {                                  // knob adjusts the zone; the clock shifts with it
      int8_t old = P.tz;
      P.tz = constrain(P.tz + c, -12, 14);
      clkEpoch += (int32_t)(P.tz - old) * 3600L;
      forceClockRedraw(); drawTitleTime();
      prRedrawSel(); uiClick(); prefsSave();
    } else { prMove(c); uiClick(); }
    oledDirty = true;
  }
  if (sp) prChange(now);
}
void oledPrefs() {
  char l[4][24], v[24];
  uint16_t col;
  int h = prSel;
  while (h > 0 && !prHeader(h)) h--;
  if (prTyping) {
    snprintf_P(l[0], 24, PSTR("%s"), prTypeWhat ? FS("saver text:") : FS("owner name:")); snprintf_P(l[1], 24, PSTR("%.21s"), prName);
    snprintf_P(l[2], 24, PSTR("%d / %d letters"), prNameLen, prTypeWhat ? 20 : 15); snprintf_P(l[3], 24, PSTR("OK = save"));
    oledCard(prTypeWhat ? FS("SAVER TEXT") : FS("OWNER"), l[0], l[1], l[2], l[3]);
    return;
  }
  prValue(prSel, v, sizeof v, &col);
  snprintf_P(l[0], 24, PSTR("> %.19s"), pgmItem(PR_LABEL_P, prSel));
  snprintf_P(l[1], 24, PSTR("  %.19s"), v);
  snprintf_P(l[2], 24, PSTR("group: %.14s"), pgmItem(PR_LABEL_P, h));
  snprintf_P(l[3], 24, PSTR("%s"), prEditTz ? FS("turn: zone  press: ok") : FS("press: change"));
  oledCard(FS("SETTINGS"), l[0], l[1], l[2], l[3]);
}

// ================= NOTES =================
// Up to 12 notes of 400 characters, each saved as /n<id>.txt in flash. Typing uses the knob keyboard.
// Leaving with a long press saves the note you were writing; "cancel" throws the changes away.
const int NOTE_MAX = 12, NOTE_LEN = 400, NOTE_VIS = 7, NOTE_COLS = 50, NOTE_ROWS = 6;
enum { NP_LIST, NP_EDIT };
int npPage = NP_LIST, npSel = 0, npTop = 0, npCount = 0, npEditId = -1;
int8_t npIds[NOTE_MAX];
uint16_t npLen[NOTE_MAX];
uint16_t npBufLen = 0;

uint16_t npAccent() { return appAccent(APP_NOTES); }
void notePath(char* b, size_t n, int id) { snprintf_P(b, n, PSTR("/n%d.txt"), id & 15); }
void notesScan() {                                  // titles only when Notes owns the shared area (else just count)
  bool titles = wkOwner == APP_NOTES;
  npCount = 0;
  for (int id = 0; id < NOTE_MAX && fsOk; id++) {
    char p[12]; notePath(p, sizeof p, id);
    if (!LittleFS.exists(p)) continue;
    File f = LittleFS.open(p, FS("r"));
    if (!f) continue;
    npLen[npCount] = f.size();
    if (titles) {
      int n = f.readBytes(npTitle[npCount], 26);
      npTitle[npCount][n] = 0;
      for (char* c = npTitle[npCount]; *c; c++) if (*c == '\n') *c = ' ';
    }
    f.close();
    npIds[npCount++] = id;
  }
  notesSaved = npCount;
}
void npDrawRow(int s) {
  int i = npTop + s, y = 36 + s * 22;
  ptrListSet(PL_NOTES, 4, 36, 304, 22, NOTE_VIS, npTop, npSel);
  uint16_t bg = (s & 1) ? E_ROW2 : E_WHITE, acc = npAccent();
  tft.fillRect(4, y, 304, 22, bg);
  if (i > npCount) return;
  bool sel = i == npSel;
  if (i == 0) putText(12, y + 7, 1, sel ? acc : C_INK, bg, npCount < NOTE_MAX ? FS("+ New note") : FS("(notes full - delete one)"));
  else {
    char b[12];
    putText(12, y + 7, 1, C_INK, bg, npTitle[i - 1][0] ? npTitle[i - 1] : FS("(empty)"));
    snprintf_P(b, sizeof b, PSTR("%u ch"), (unsigned)npLen[i - 1]);
    putText(300 - (int)strlen(b) * 6, y + 7, 1, sel ? acc : C_MID, bg, b);
  }
  if (sel) { tft.drawRect(4, y, 304, 22, acc); tft.drawRect(5, y + 1, 302, 20, acc); }
}
void npDrawScroll() {
  int items = npCount + 1, h = NOTE_VIS * 22;
  tft.fillRect(308, 36, 8, h, E_ROW2);
  tft.drawFastVLine(308, 36, h, E_RULE);
  int th = items <= NOTE_VIS ? h : h * NOTE_VIS / items;
  int ty = items <= NOTE_VIS ? 36 : 36 + (h - th) * npTop / (items - NOTE_VIS);
  tft.fillRect(310, ty + 1, 4, th - 2, npAccent());
}
void npOpenList() {
  npPage = NP_LIST;
  notesScan();
  if (npSel > npCount) npSel = npCount;
  if (npSel < npTop) npTop = npSel;
  tft.fillRect(0, TITLE_H, W, DOCK_Y - 3 - TITLE_H, C_LCD);
  tft.drawRect(3, 21, 314, 14 + NOTE_VIS * 22 + 1, E_RULE);
  tft.fillRect(4, 22, 312, 14, E_HEAD);
  putText(10, 25, 1, C_INK, E_HEAD, FS("Note"));
  putText(278, 25, 1, C_INK, E_HEAD, FS("Size"));
  for (int s = 0; s < NOTE_VIS; s++) npDrawRow(s);
  npDrawScroll();
  char b[48];
  snprintf_P(b, sizeof b, PSTR("%d of %d notes   press: open   hold: back"), npCount, NOTE_MAX);
  putText(6, 198, 1, C_MID, C_LCD, b);
  oledDirty = true;
}
void npDrawText() {
  tft.fillRect(4, 22, 312, 66, E_WHITE);
  tft.drawRect(3, 21, 314, 68, npAccent());
  int ring[NOTE_ROWS], nl = 1, col = 0;
  ring[0] = 0;
  for (int k = 0; k < npBufLen; k++) {
    if (npBuf[k] == '\n') { ring[nl % NOTE_ROWS] = k + 1; nl++; col = 0; continue; }
    if (col == NOTE_COLS) { ring[nl % NOTE_ROWS] = k; nl++; col = 0; }
    col++;
  }
  int first = nl > NOTE_ROWS ? nl - NOTE_ROWS : 0;
  for (int r = first; r < nl; r++) {
    int s = ring[r % NOTE_ROWS], e = (r + 1 < nl) ? ring[(r + 1) % NOTE_ROWS] : npBufLen;
    char line[NOTE_COLS + 1];
    int n = 0;
    for (int k = s; k < e && n < NOTE_COLS; k++) if (npBuf[k] != '\n') line[n++] = npBuf[k];
    line[n] = 0;
    putText(8, 25 + (r - first) * 10, 1, C_INK, E_WHITE, line);
  }
  tft.fillRect(8 + col * 6, 24 + (nl - 1 - first) * 10, 2, 9, npAccent());     // cursor
}
void npDrawStatus() {
  char b[48];
  snprintf_P(b, sizeof b, PSTR("%3u/%d chars   OK: save   cancel: discard"), (unsigned)npBufLen, NOTE_LEN);
  putText(6, 198, 1, C_MID, C_LCD, b);
}
void npOpenEditor(int item) {
  npPage = NP_EDIT;
  npBufLen = 0; npBuf[0] = 0; npEditId = -1;
  if (item > 0) {
    npEditId = npIds[item - 1];
    char p[12]; notePath(p, sizeof p, npEditId);
    File f = LittleFS.open(p, FS("r"));
    if (f) { npBufLen = f.readBytes(npBuf, NOTE_LEN); f.close(); }
    npBuf[npBufLen] = 0;
  }
  tft.fillRect(0, TITLE_H, W, DOCK_Y - 3 - TITLE_H, C_LCD);
  npDrawText();
  kbBegin(KB_LAYOUT_NOTE, npAccent());
  kbDrawAll();
  npDrawStatus();
  oledDirty = true;
}
void npSave() {
  if (!fsOk) return;
  if (npEditId < 0) {                                   // new note: first free slot
    if (!npBufLen) return;
    for (int id = 0; id < NOTE_MAX; id++) { char p[12]; notePath(p, sizeof p, id); if (!LittleFS.exists(p)) { npEditId = id; break; } }
    if (npEditId < 0) return;
  }
  char p[12]; notePath(p, sizeof p, npEditId);
  if (!npBufLen) { LittleFS.remove(p); return; }       // emptied note = deleted
  File f = LittleFS.open(p, FS("w"));
  if (f) { f.write((const uint8_t*)npBuf, npBufLen); f.close(); }
}
void notesLeave() { if (npPage == NP_EDIT) { npSave(); npPage = NP_LIST; } }   // called when the app is left with a long press
void notesEnter() {
  wkOwner = APP_NOTES;
  tft.fillScreen(C_LCD);
  drawTitleBar(FS("Notes"), npAccent());
  drawDock(APP_NOTES);
  npSel = 0; npTop = 0;
  npOpenList();
}
void notesTick(uint32_t now) {
  int c = takeClicks() * ENC_DIR;
  takePress();
  bool sp = takeShort();
  titleTimeTick();
  if (npPage == NP_LIST) {
    int items = npCount + 1;
    if (c) {
      int old = npSel, oldTop = npTop;
      npSel = ((npSel + c) % items + items) % items;
      if (npSel < npTop) npTop = npSel;
      else if (npSel >= npTop + NOTE_VIS) npTop = npSel - NOTE_VIS + 1;
      if (npTop != oldTop) { for (int s = 0; s < NOTE_VIS; s++) npDrawRow(s); npDrawScroll(); }
      else if (old != npSel) { npDrawRow(old - npTop); npDrawRow(npSel - npTop); }
      if (old != npSel) { uiClick(); oledDirty = true; }
    }
    if (sp && !(npSel == 0 && npCount >= NOTE_MAX)) npOpenEditor(npSel);
    return;
  }
  int r = kbTick(c, sp);
  if ((r == KB_TYPED || r == KB_SPACE || r == KB_ENTER) && npBufLen < NOTE_LEN) {
    npBuf[npBufLen++] = r == KB_TYPED ? kbChar : (r == KB_SPACE ? ' ' : '\n');
    npBuf[npBufLen] = 0;
  } else if (r == KB_DEL && npBufLen) npBuf[--npBufLen] = 0;
  else if (r == KB_OK) { npSave(); SFX(T_HOME); npOpenList(); return; }
  else if (r == KB_CANCEL) { npOpenList(); return; }
  if (r != KB_NONE && r != KB_PAGE) { npDrawText(); npDrawStatus(); oledDirty = true; }
  (void)now;
}
void oledNotes() {
  char l[4][24];
  if (npPage == NP_EDIT) {
    snprintf_P(l[0], 24, PSTR("%s"), npEditId < 0 ? FS("new note") : FS("editing note"));
    snprintf_P(l[1], 24, PSTR("%u / %d chars"), (unsigned)npBufLen, NOTE_LEN);
    snprintf_P(l[2], 24, PSTR("keys: %s"), kbPage == 0 ? FS("abc 123") : (kbPage == 1 ? FS("ABC !#$") : FS("symbols")));
    snprintf_P(l[3], 24, PSTR("OK=save  hold=exit"));
  } else {
    snprintf_P(l[0], 24, PSTR("%d note%s saved"), npCount, npCount == 1 ? FS("") : FS("s"));
    if (npSel == 0) snprintf_P(l[1], 24, PSTR("> new note"));
    else snprintf_P(l[1], 24, PSTR("> %.19s"), npTitle[npSel - 1]);
    snprintf_P(l[2], 24, PSTR("%s"), fsOk ? FS("flash: ok") : FS("flash: NOT mounted"));
    snprintf_P(l[3], 24, PSTR("press: open"));
  }
  oledCard(FS("NOTES"), l[0], l[1], l[2], l[3]);
}

// ================= BOOT SPLASH =================
// Settings > Boot screen: off / classic / info / terminal; Boot speed: fast / normal / slow / wait for a press.
// A knob press skips the rest of it.
bool bootSkip = false;
void bootPause(uint32_t ms) {                       // waits, but a press skips the rest of the boot screen
  uint32_t t = millis();
  while (!bootSkip && millis() - t < ms) { if (digitalRead(BTN_PIN) == LOW) bootSkip = true; delay(5); yield(); }
}
uint16_t bootStepMs() { return P.bootSpeed == 0 ? 40 : P.bootSpeed == 2 ? 450 : 180; }
uint32_t bootHoldMs() { return P.bootSpeed == 0 ? 250 : P.bootSpeed == 1 ? 1500 : P.bootSpeed == 2 ? 3500 : 60000; }
void bootChime() {
  if (!P.bootSound || !P.sound || bootSkip) return;
  static const uint16_t N[4] = {784, 988, 1175, 1568};
  for (int i = 0; i < 4; i++) { soundOn(N[i]); delay(i == 3 ? 170 : 70); }
  soundOff();
}
bool bootFact(int i, char* label, size_t ln, char* val, size_t vn, uint16_t* col) {   // the system check lines
  *col = C_INK;
  char ver[12]; strncpy_P(ver, FW_VERSION, sizeof ver - 1); ver[sizeof ver - 1] = 0;
  switch (i) {
    case 0: snprintf_P(label, ln, PSTR("Version")); snprintf_P(val, vn, PSTR("%s  (built %s)"), ver, __DATE__); break;
    case 1: snprintf_P(label, ln, PSTR("Processor")); snprintf_P(val, vn, PSTR("ESP8266 at %d MHz"), ESP.getCpuFreqMHz()); break;
    case 2: snprintf_P(label, ln, PSTR("Flash")); snprintf_P(val, vn, PSTR("%u MB"), (unsigned)(ESP.getFlashChipRealSize() >> 20)); break;
    case 3: snprintf_P(label, ln, PSTR("Free RAM")); snprintf_P(val, vn, PSTR("%u KB (largest block %u KB)"), (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getMaxFreeBlockSize() / 1024)); break;
    case 4: {
      snprintf_P(label, ln, PSTR("Files"));
      FSInfo fi;
      if (fsOk && LittleFS.info(fi)) snprintf_P(val, vn, PSTR("%u of %u KB used"), (unsigned)(fi.usedBytes / 1024), (unsigned)(fi.totalBytes / 1024));
      else { snprintf_P(val, vn, PSTR("NOT MOUNTED")); *col = E_CORAL; }
      break;
    }
    case 5: snprintf_P(label, ln, PSTR("OLED")); snprintf_P(val, vn, oledOk ? PSTR("found") : PSTR("missing")); if (!oledOk) *col = E_AMBER; break;
    case 6: snprintf_P(label, ln, PSTR("Settings")); snprintf_P(val, vn, PSTR("loaded, %s theme"), pgmItem(PSTR("Platinum\n90s LCD\nDark"), P.theme % 3)); break;
    case 7: snprintf_P(label, ln, PSTR("Last restart")); snprintf_P(val, vn, PSTR("%.30s"), lastCrash[0] ? lastCrash : FS("normal")); if (lastCrash[0]) *col = E_CORAL; break;
    case 8: snprintf_P(label, ln, PSTR("WiFi")); snprintf_P(val, vn, PSTR("%.24s"), WiFi.SSID().length() ? WiFi.SSID().c_str() : FS("not set up yet")); break;
    case 9: snprintf_P(label, ln, PSTR("Owner")); snprintf_P(val, vn, PSTR("%.24s"), P.owner[0] ? P.owner : FS("(nobody)")); break;
    default: return false;
  }
  return true;
}
void drawSplash() {
  bootSkip = digitalRead(BTN_PIN) == LOW;           // hold the knob down while powering on: skip it
  if (!P.splash || bootSkip) return;
  uint8_t style = P.splash % 4;                     // 1 classic, 2 info, 3 terminal
  char ver[12]; strncpy_P(ver, FW_VERSION, sizeof ver - 1); ver[sizeof ver - 1] = 0;
  char label[16], val[44], line[64];
  uint16_t col;
  if (style == 3) {                                 // a 90s PC power-on self test
    const uint16_t BG = 0x0000, FG = RGB565(90, 255, 120), DIM = RGB565(40, 140, 70);
    tft.fillScreen(BG);
    snprintf_P(line, sizeof line, PSTR("PSION EPOC COLOR BIOS v%s"), ver);
    putText(6, 6, 1, FG, BG, line);
    putText(6, 18, 1, DIM, BG, FS("(c) Micor & Claude - press the knob to skip"));
    for (int i = 0; bootFact(i, label, sizeof label, val, sizeof val, &col) && !bootSkip; i++) {
      snprintf_P(line, sizeof line, PSTR("%-12s %s"), label, val);
      int y = 40 + i * 14;
      for (int k = 0; line[k] && !bootSkip; k++) {    // typed out
        char ch[2] = {line[k], 0};
        putText(6 + k * 6, y, 1, col == C_INK ? FG : col, BG, ch);
        if (P.bootSpeed) bootPause(P.bootSpeed == 2 ? 12 : 3);
      }
      bootPause(bootStepMs());
    }
    putText(6, 190, 1, FG, BG, FS("All systems purring. Starting EPOC..."));
    bootChime();
    bootPause(bootHoldMs());
    return;
  }
  tft.fillScreen(C_LCD);
  const char* L = "PSION";
  uint16_t acc = appAccent(APP_MENU);
  int ly = style == 2 ? 30 : 64, sz = style == 2 ? 2 : 3, bw = style == 2 ? 26 : 34, bh = style == 2 ? 28 : 40, gap = style == 2 ? 30 : 38;
  if (style == 1) drawPaw(160, 36, 3, acc);
  for (int i = 0; i < 5; i++) {
    int x = 160 - (5 * gap - (gap - bw)) / 2 + i * gap;
    tft.drawRect(x, ly, bw, bh, C_INK); tft.drawRect(x + 1, ly + 1, bw - 2, bh - 2, C_INK);
    char c[2] = {L[i], 0};
    putText(x + (bw - 6 * sz) / 2, ly + (bh - 8 * sz) / 2, sz, C_INK, C_LCD, c);
  }
  snprintf_P(line, sizeof line, PSTR("EPOC COLOUR EDITION  v%s"), ver);
  centerText(line, ly + bh + 10, 1, C_MID, C_LCD);
  if (style == 1) {
    if (P.owner[0]) {
      centerText(FS("this psion belongs to"), 140, 1, C_MID, C_LCD);
      centerText(P.owner, 154, 2, acc, C_LCD);
    }
    int ms = P.bootSpeed == 0 ? 300 : P.bootSpeed == 2 ? 3200 : 1200;
    for (int p = 0; p <= 100 && !bootSkip; p += 4) { drawGauge(80, 196, 160, p, acc); bootPause(ms / 25); }
  } else {                                          // info: the system checks, one line at a time
    tft.fillRect(8, 86, 304, 128, E_WHITE);
    tft.drawRect(8, 86, 304, 128, E_RULE);
    for (int i = 0; bootFact(i, label, sizeof label, val, sizeof val, &col) && !bootSkip; i++) {
      int y = 90 + i * 12;
      putText(14, y, 1, C_MID, E_WHITE, label);
      putText(98, y, 1, col, E_WHITE, val);
      drawGauge(80, 222, 160, (i + 1) * 10, acc);
      bootPause(bootStepMs());
    }
  }
  if (P.bootSpeed == 3 && !bootSkip) centerText(FS("press the knob to start"), style == 1 ? 210 : 230, 1, acc, C_LCD);
  bootChime();
  bootPause(bootHoldMs());
}

// ================= OFFICE SHARED: popup menu, text prompt, file picker, charts =================

// file names typed with the knob: letters/digits/-/_ only, n~ becomes n, max 12 chars
void cleanName(const char* in, char* out) {
  int o = 0;
  for (; *in && o < 12; in++) {
    uint8_t c = (uint8_t)*in;
    if (c == 0xA4 || c == 0xA5) c = 'n';
    if (isalnum(c) || c == '-' || c == '_') out[o++] = (char)c;
    else if (c == ' ' && o) out[o++] = '_';
  }
  out[o] = 0;
  if (!o) strcpy_P(out, PSTR("untitled"));
}
// "/docs/letter.doc" -> "letter"
void baseName(const char* path, char* out, size_t n) {
  const char* s = strrchr(path, '/'); s = s ? s + 1 : path;
  const char* d = strrchr(s, '.');
  size_t len = d ? (size_t)(d - s) : strlen(s);
  if (len >= n) len = n - 1;
  memcpy(out, s, len); out[len] = 0;
}
void fmtNum(double v, char* b, size_t n, int sig) {
  if (isnan(v)) { snprintf_P(b, n, PSTR("NaN")); return; }
  if (isinf(v)) { snprintf(b, n, v > 0 ? "inf" : "-inf"); return; }
  if (fabs(v) < 1e12 && v == floor(v)) snprintf_P(b, n, PSTR("%.0f"), v);
  else snprintf_P(b, n, PSTR("%.*g"), sig, v);
}

// ---------- popup menu ----------
const int PM_MAX = 12, PM_W = 190;
const char* pmItem[PM_MAX];
char pmTitle[40], pmBuf[300];
int pmN = 0, pmSel = 0, pmX = 0, pmY = 0;
bool pmOpen = false;
uint16_t pmAcc = 0;
void pmDrawItem(int i) {
  int y = pmY + 16 + i * 16;
  bool sel = i == pmSel;
  tft.fillRect(pmX + 1, y, PM_W - 2, 16, sel ? pmAcc : E_WHITE);
  putText(pmX + 10, y + 4, 1, sel ? E_ONACC : C_INK, sel ? pmAcc : E_WHITE, pmItem[i]);
}
void pmDraw(const char* title, uint16_t acc) {     // pmItem[] / pmN are filled: draw the box
  pmSel = 0; pmAcc = acc; pmOpen = true;
  snprintf_P(pmTitle, sizeof pmTitle, PSTR("%s"), title);
  int h = 16 + pmN * 16 + 3;
  pmX = (W - PM_W) / 2; pmY = max(22, (H - h) / 2);
  tft.fillRect(pmX + 4, pmY + 4, PM_W, h, E_RULE);                 // flat drop shadow
  tft.fillRect(pmX, pmY, PM_W, h, E_WHITE);
  tft.drawRect(pmX, pmY, PM_W, h, C_INK);
  tft.fillRect(pmX + 1, pmY + 1, PM_W - 2, 14, E_BAR);
  putText(pmX + 6, pmY + 4, 1, E_BARTXT, E_BAR, pmTitle);
  for (int i = 0; i < pmN; i++) pmDrawItem(i);
}
void pmShow(const char* title, const char* const* items, int n, uint16_t acc) {
  pmN = min(n, PM_MAX);
  int used = 0;                                       // copy: callers may pass short-lived text
  for (int i = 0; i < pmN; i++) {
    int room = (int)sizeof pmBuf - 1 - used;         // was able to go negative (= a 4 GB copy) on long link lists
    if (room <= 0) { pmN = i; break; }
    int l = min((int)strlen(items[i]), room);
    memmove(pmBuf + used, items[i], l); pmBuf[used + l] = 0;
    pmItem[i] = pmBuf + used; used += l + 1;
  }
  pmDraw(title, acc);
}
void pmShowP(const char* title, PGM_P list, uint16_t acc) {   // list = newline-separated flash text
  char t[40];
  snprintf_P(t, sizeof t, PSTR("%s"), title);       // the title may live in pmBuf's neighbour, the FS() ring
  strncpy_P(pmBuf, list, sizeof pmBuf - 1); pmBuf[sizeof pmBuf - 1] = 0;
  pmN = 0;
  for (char* p = pmBuf; *p && pmN < PM_MAX; ) { pmItem[pmN++] = p; char* e = strchr(p, '\n'); if (!e) break; *e = 0; p = e + 1; }
  pmDraw(t, acc);
}
int pmTick(int c, bool sp) {                      // -1 while open, else the chosen item
  if (pmN <= 0) { pmOpen = false; return sp ? 0 : -1; }   // (never divide by an empty menu)
  if (c) {
    int old = pmSel;
    pmSel = ((pmSel + c) % pmN + pmN) % pmN;
    if (old != pmSel) { pmDrawItem(old); pmDrawItem(pmSel); uiClick(); }
  }
  if (!sp) return -1;
  pmOpen = false; SFX(T_HOP);
  return pmSel;
}

// ---------- one-line text prompt (file names, sheet cells) ----------
char askBuf[161], askPrompt[48];
uint8_t askLen = 0, askMax = 32;
bool askOpen = false;
uint16_t askAcc = 0;
void askDrawField() {
  tft.fillRect(6, 38, 308, 22, E_WHITE);
  tft.drawRect(5, 37, 310, 24, askAcc);
  int from = askLen > 24 ? askLen - 24 : 0;
  putText(10, 41, 2, C_INK, E_WHITE, askBuf + from);
  tft.fillRect(10 + (askLen - from) * 12, 41, 2, 16, askAcc);
}
void askBegin(const char* prompt, const char* init, uint8_t maxLen, uint16_t acc) {
  askOpen = true; askAcc = acc; askMax = min((int)maxLen, 160);
  snprintf_P(askPrompt, sizeof askPrompt, PSTR("%s"), prompt);
  snprintf_P(askBuf, sizeof askBuf, PSTR("%s"), init);
  askLen = strlen(askBuf);
  if (askLen > askMax) { askLen = askMax; askBuf[askLen] = 0; }
  tft.fillRect(0, TITLE_H, W, H - TITLE_H, C_LCD);
  putText(6, 25, 1, C_INK, C_LCD, askPrompt);
  askDrawField();
  kbBegin(KB_LAYOUT_FORM, acc);
  kbDrawAll();
  putText(6, 196, 1, C_MID, C_LCD, FS("turn: pick key   press: type   OK: done"));
}
int askTick(int c, bool sp) {                     // 0 typing, 1 OK, -1 cancel
  int r = kbTick(c, sp);
  if ((r == KB_TYPED || r == KB_SPACE) && askLen < askMax) { askBuf[askLen++] = r == KB_SPACE ? ' ' : kbChar; askBuf[askLen] = 0; }
  else if (r == KB_DEL && askLen) askBuf[--askLen] = 0;
  else if (r == KB_OK) { askOpen = false; return 1; }
  else if (r == KB_CANCEL) { askOpen = false; return -1; }
  if (r != KB_NONE && r != KB_PAGE) { askDrawField(); oledDirty = true; }
  return 0;
}

// ---------- file picker: lists one folder ----------
const int FP_MAX = 16, FP_VIS = 9;
enum { FP_BUSY = -3, FP_NEW = -2, FP_CANCEL = -1 };
char fpName[FP_MAX][32], fpDir[12], fpExt[6], fpPath[64], fpNewLabel[24], fpAlt[7];
int fpN = 0, fpSel = 0, fpTop = 0;
bool fpOpen = false;
uint16_t fpAcc = 0, fpAltBits = 0;                  // fpAltBits: the entries that have the second extension
PGM_P fpAltExt = nullptr;                           // set just before fpBegin: also list these (".docx" next to ".doc")
int fpItems() { return 1 + (fpNewLabel[0] ? 1 : 0) + fpN; }
void fpDrawRow(int s) {
  int i = fpTop + s, y = 38 + s * 19;
  ptrListSet(PL_PICKER, 4, 38, 312, 19, FP_VIS, fpTop, fpSel);
  uint16_t bg = (s & 1) ? E_ROW2 : E_WHITE;
  tft.fillRect(4, y, 312, 19, bg);
  if (i >= fpItems()) return;
  bool sel = i == fpSel, hasNew = fpNewLabel[0];
  int k = i - 1 - (hasNew ? 1 : 0);
  char alt[36];
  const char* label = i == 0 ? "< Cancel" : (hasNew && i == 1 ? fpNewLabel : fpName[k]);
  if (k >= 0 && i > (hasNew ? 1 : 0) && (fpAltBits >> k & 1)) { snprintf_P(alt, sizeof alt, PSTR("%s  (%s)"), fpName[k], fpAlt + 1); label = alt; }
  putText(12, y + 6, 1, (i <= (hasNew ? 1 : 0)) ? (sel ? fpAcc : C_MID) : C_INK, bg, label);
  if (sel) { tft.drawRect(4, y, 312, 19, fpAcc); tft.drawRect(5, y + 1, 310, 17, fpAcc); }
}
void fpDraw(const char* title) {
  tft.fillRect(0, TITLE_H, W, H - TITLE_H, C_LCD);
  tft.fillRect(4, 22, 312, 14, E_HEAD);
  putText(10, 25, 1, C_INK, E_HEAD, title);
  tft.drawRect(3, 21, 314, 16 + FP_VIS * 19 + 1, E_RULE);
  for (int s = 0; s < FP_VIS; s++) fpDrawRow(s);
  char b[40]; snprintf_P(b, sizeof b, PSTR("%d file%s in %s"), fpN, fpN == 1 ? FS("") : FS("s"), fpDir);
  putText(6, 220, 1, C_MID, C_LCD, b);
}
void fpBegin(const char* title, const char* dir, const char* ext, const char* newLabel, uint16_t acc) {
  snprintf_P(fpDir, sizeof fpDir, PSTR("%s"), dir); snprintf_P(fpExt, sizeof fpExt, PSTR("%s"), ext);
  snprintf_P(fpNewLabel, sizeof fpNewLabel, PSTR("%s"), newLabel ? newLabel : FS(""));
  if (fpAltExt) strncpy_P(fpAlt, fpAltExt, sizeof fpAlt - 1); else fpAlt[0] = 0;
  fpAlt[sizeof fpAlt - 1] = 0;
  fpAltExt = nullptr; fpAltBits = 0;
  fpN = 0; fpSel = fpNewLabel[0] ? 1 : (fpN ? 1 : 0); fpTop = 0; fpAcc = acc; fpOpen = true;
  if (fsOk) {
    Dir d = LittleFS.openDir(dir);
    while (d.next() && fpN < FP_MAX) {
      String n = d.fileName();
      String ln = n; ln.toLowerCase();
      bool alt = fpAlt[0] && ln.endsWith(fpAlt);
      if (!alt && !ln.endsWith(ext)) continue;
      snprintf_P(fpName[fpN], 32, PSTR("%.*s"), (int)(n.length() - strlen(alt ? fpAlt : ext)), n.c_str());
      if (alt) fpAltBits |= 1 << fpN;
      fpN++;
    }
  }
  if (!fpNewLabel[0] && fpN) fpSel = 1;
  fpDraw(title);
}
int fpTick(int c, bool sp) {                       // FP_BUSY, FP_CANCEL, FP_NEW or file index (path in fpPath)
  int items = fpItems();
  if (c) {
    int old = fpSel, oldTop = fpTop;
    fpSel = ((fpSel + c) % items + items) % items;
    if (fpSel < fpTop) fpTop = fpSel;
    else if (fpSel >= fpTop + FP_VIS) fpTop = fpSel - FP_VIS + 1;
    if (fpTop != oldTop) for (int s = 0; s < FP_VIS; s++) fpDrawRow(s);
    else if (old != fpSel) { fpDrawRow(old - fpTop); fpDrawRow(fpSel - fpTop); }
    uiClick();
  }
  if (!sp) return FP_BUSY;
  fpOpen = false; SFX(T_HOP);
  if (fpSel == 0) return FP_CANCEL;
  if (fpNewLabel[0] && fpSel == 1) return FP_NEW;
  int i = fpSel - 1 - (fpNewLabel[0] ? 1 : 0);
  snprintf_P(fpPath, sizeof fpPath, PSTR("%s/%s%s"), fpDir, fpName[i], (fpAltBits >> i & 1) ? fpAlt : fpExt);
  return i;
}

// ---------- bar / line chart (Sheet chart view and charts inside Word) ----------
// (Arduino can't auto-prototype function-pointer parameters, so the label source is a global)
const char* (*chartLabel)(int) = nullptr;
void drawChart(int x, int y, int w, int h, const float* v, int n, bool line, uint16_t acc, const char* title) {
  const char* (*label)(int) = chartLabel;
  tft.fillRect(x, y, w, h, E_WHITE);
  tft.drawRect(x, y, w, h, E_RULE);
  putText(x + 6, y + 4, 1, C_INK, E_WHITE, title);
  if (n <= 0) { putText(x + 6, y + h / 2, 1, C_MID, E_WHITE, FS("no numbers to chart")); return; }
  float lo = 0, hi = 0;
  for (int i = 0; i < n; i++) { lo = min(lo, v[i]); hi = max(hi, v[i]); }
  if (hi == lo) hi = lo + 1;
  int px = x + 8, pw = w - 16, py = y + 16, ph = h - 16 - (label ? 12 : 4);
  int zero = py + (int)(ph * hi / (hi - lo));
  tft.drawFastHLine(px, zero, pw, C_MID);
  int slot = pw / n, bw = max(2, slot * 2 / 3), lx = -1, ly = -1;
  char b[12];
  for (int i = 0; i < n; i++) {
    int cx = px + i * slot + slot / 2, vy = py + (int)(ph * (hi - v[i]) / (hi - lo));
    if (line) {
      if (lx >= 0) { tft.drawLine(lx, ly, cx, vy, acc); tft.drawLine(lx, ly + 1, cx, vy + 1, acc); }
      tft.fillCircle(cx, vy, 2, acc);
      lx = cx; ly = vy;
    } else tft.fillRect(cx - bw / 2, min(vy, zero), bw, max(1, abs(zero - vy)), acc);
    if (label && slot >= 14) {
      snprintf_P(b, sizeof b, PSTR("%.*s"), max(1, slot / 6), label(i));
      putText(cx - (int)strlen(b) * 3, y + h - 10, 1, C_MID, E_WHITE, b);
    }
  }
  fmtNum(hi, b, sizeof b, 4); putText(x + w - 6 - (int)strlen(b) * 6, y + 4, 1, C_MID, E_WHITE, b);
}

// ================= EXPRESSION ENGINE (Calc, Sheet, BASIC, C++) =================
// Recursive descent over the text: or > and > not > compare > + - > * / % mod > unary > ^ > primary.
// Works with both BASIC words (AND OR NOT MOD = <>) and C symbols (&& || ! % == !=).
// Variables and sheet cells come from the exVar / exCell callbacks set by the app.
bool (*exVar)(const char* n, int len, double* v) = nullptr;      // name -> value (false = unknown)
bool (*exCell)(int col, int row, double* v) = nullptr;          // sheet ranges (false = empty cell)
bool exDeg = false;
bool exImplicit = false;                         // Calc only: 2x, 3sin(x), 2(1+x) mean multiplication
double* exBound = nullptr;                       // Calc's x: the variable d/dx, integrals and sums sweep
const char* exP = "";
const char* exErr = nullptr;
int exDepth = 0;

bool exAl(char c) { return isalpha((uint8_t)c) || c == '_'; }
bool exAlnum(char c) { return isalnum((uint8_t)c) || c == '_' || c == '$'; }
void exSkip() { while (*exP == ' ' || *exP == '\t' || *exP == '\r') exP++; }
bool exWord(const char* w) {                     // keyword, any case, whole word only
  exSkip();
  int n = strlen(w);
  for (int i = 0; i < n; i++) if (toupper((uint8_t)exP[i]) != w[i]) return false;
  if (exAlnum(exP[n])) return false;
  exP += n;
  return true;
}
bool exOp(const char* o) { exSkip(); size_t n = strlen(o); if (strncmp(exP, o, n)) return false; exP += n; return true; }
double exFail(const char* m) { if (!exErr) exErr = m; return 0; }
double exOr();
double exArg() { return exOr(); }
bool exName(const char* s, int len, double* v) {   // a constant or variable by name (false = unknown)
  if (len == 2 && toupper((uint8_t)s[0]) == 'P' && toupper((uint8_t)s[1]) == 'I') { *v = M_PI; return true; }
  if (exVar && exVar(s, len, v)) return true;
  if (len == 1 && (s[0] == 'e' || s[0] == 'E')) { *v = M_E; return true; }   // (in Calc, E is memory E)
  if (len == 4 && !strncmp_P(s, PSTR("true"), 4)) { *v = 1; return true; }
  if (len == 5 && !strncmp_P(s, PSTR("false"), 5)) { *v = 0; return true; }
  return false;
}
double __attribute__((noinline)) exDecimal(const char* s, int n) {   // strtod of just these n characters
  char t[24];
  n = min(n, 23);
  memcpy(t, s, n);
  t[n] = 0;
  return strtod(t, nullptr);
}
bool exCellRef(const char*& p, int* c, int* r) {   // "B12" -> col 1, row 11
  const char* q = p;
  while (*q == ' ') q++;
  if (!isalpha((uint8_t)*q)) return false;
  *c = toupper((uint8_t)*q) - 'A'; q++;
  if (!isdigit((uint8_t)*q)) return false;
  int n = 0;
  while (isdigit((uint8_t)*q)) n = n * 10 + (*q++ - '0');
  if (exAlnum(*q)) return false;
  *r = n - 1; p = q;
  return true;
}
void exSkipArg() {                               // jump over one argument without evaluating it
  int d = 0;
  for (; *exP; exP++) {
    if (*exP == '(') d++;
    else if (*exP == ')') { if (!d) break; d--; }
    else if (*exP == ',' && !d) break;
  }
}
double exCall(const char* name, int len) {
  char f[10];
  int n = min(len, 9);
  for (int i = 0; i < n; i++) f[i] = toupper((uint8_t)name[i]);
  f[n] = 0;
  if (!strcmp_P(f, PSTR("DERIV")) || !strcmp_P(f, PSTR("INTEG")) || !strcmp_P(f, PSTR("SIGMA"))) {
    // deriv(f, a) = f'(a); integ(f, a, b) = Simpson's rule; sigma(f, a, b) = f(a) + ... + f(b), all over x
    if (!exBound) return exFail("d/dx, integrals, sums: Calc only");
    const char* body = exP;
    exSkipArg();
    double a[2] = {*exBound, 0};
    int na = 0;
    while (na < 2 && exOp(",")) a[na++] = exArg();
    if (!exOp(")")) return exFail("missing )");
    if (f[0] != 'D' && na < 2) return exFail("needs (f, from, to)");
    const char* after = exP;
    double saved = *exBound, r = 0;
    auto at = [&](double xv) { *exBound = xv; exP = body; return exArg(); };
    if (f[0] == 'D') { double h = 1e-5 * max(1.0, fabs(a[0])); r = (at(a[0] + h) - at(a[0] - h)) / (2 * h); }
    else if (f[0] == 'I') {
      const int N = 64;
      double step = (a[1] - a[0]) / N, sum = at(a[0]) + at(a[1]);
      for (int i = 1; i < N && !exErr; i++) sum += at(a[0] + i * step) * ((i & 1) ? 4 : 2);
      r = sum * step / 3;
    } else {
      long lo = lround(a[0]), hi = lround(a[1]);
      if (hi - lo > 5000) { *exBound = saved; return exFail("sum too long"); }
      for (long k = lo; k <= hi && !exErr; k++) { r += at(k); if (!((k - lo) & 63)) yield(); }
    }
    *exBound = saved; exP = after;
    return r;
  }
  bool med = !strcmp_P(f, PSTR("MEDIAN")), sd = !strcmp_P(f, PSTR("STDEV")) || !strcmp_P(f, PSTR("VAR"));
  bool agg = !strcmp_P(f, PSTR("SUM")) || !strcmp_P(f, PSTR("AVG")) || !strcmp_P(f, PSTR("AVERAGE")) || !strcmp_P(f, PSTR("COUNT")) || !strcmp_P(f, PSTR("PRODUCT")) ||
             ((!strcmp_P(f, PSTR("MIN")) || !strcmp_P(f, PSTR("MAX"))) && exCell) || med || sd;
  if (agg) {                                       // accepts ranges like A1:B5 and plain numbers
    static float exMed[48];                        // MEDIAN's values (static: off the 4 KB stack)
    static bool exMedBusy = false;
    if (med && exMedBusy) return exFail("MEDIAN inside MEDIAN");
    if (med) exMedBusy = true;
    double sum = 0, prod = 1, lo = INFINITY, hi = -INFINITY, sq = 0, v;
    int cnt = 0;
    auto add = [&](double x) { sum += x; prod *= x; sq += x * x; lo = min(lo, x); hi = max(hi, x); if (med && cnt < 48) exMed[cnt] = x; cnt++; };
    do {
      const char* save = exP;
      int c1, r1, c2, r2;
      if (exCell && exCellRef(exP, &c1, &r1) && exOp(":") && exCellRef(exP, &c2, &r2)) {
        for (int r = min(r1, r2); r <= max(r1, r2); r++)
          for (int c = min(c1, c2); c <= max(c1, c2); c++)
            if (exCell(c, r, &v)) add(v);
      } else {
        exP = save;
        exSkip();
        if (*exP == ')') break;
        add(exArg());
      }
    } while (!exErr && exOp(","));
    if (med) exMedBusy = false;
    if (!exOp(")")) return exFail("missing )");
    if (!strcmp_P(f, PSTR("SUM"))) return sum;
    if (!strcmp_P(f, PSTR("PRODUCT"))) return cnt ? prod : 0;
    if (!strcmp_P(f, PSTR("COUNT"))) return cnt;
    if (!cnt) return 0;
    if (!strcmp_P(f, PSTR("MIN"))) return lo;
    if (!strcmp_P(f, PSTR("MAX"))) return hi;
    if (med) {
      int n = min(cnt, 48);
      for (int i = 1; i < n; i++) { float t = exMed[i]; int j = i; while (j && exMed[j - 1] > t) { exMed[j] = exMed[j - 1]; j--; } exMed[j] = t; }
      return n & 1 ? exMed[n / 2] : (exMed[n / 2 - 1] + exMed[n / 2]) / 2.0;
    }
    if (sd) {                                      // sample variance / standard deviation, like Excel's VAR / STDEV
      double var = cnt > 1 ? max(0.0, (sq - sum * sum / cnt) / (cnt - 1)) : 0;
      return f[0] == 'V' ? var : sqrt(var);
    }
    return sum / cnt;
  }
  if (!strcmp_P(f, PSTR("TODAY")) || !strcmp_P(f, PSTR("NOW"))) {   // Excel's day number (dates: Format > Date)
    exSkip();
    if (!exOp(")")) return exFail("missing )");
    double d = clkEpoch / 86400UL + 25569.0;
    return f[0] == 'N' ? d + (clkEpoch % 86400UL) / 86400.0 : d;
  }
  double a[3] = {0, 0, 0};
  int na = 0;
  exSkip();
  if (*exP != ')') do { if (na < 3) a[na] = exArg(); na++; } while (!exErr && exOp(","));
  if (!exOp(")")) return exFail("missing )");
  double k = exDeg ? 0.01745329252 : 1.0;
  double x = a[0];
  if (!strcmp_P(f, PSTR("SIN"))) return sin(x * k);
  if (!strcmp_P(f, PSTR("COS"))) return cos(x * k);
  if (!strcmp_P(f, PSTR("TAN"))) return tan(x * k);
  if (!strcmp_P(f, PSTR("ASIN"))) return asin(x) / k;
  if (!strcmp_P(f, PSTR("ACOS"))) return acos(x) / k;
  if (!strcmp_P(f, PSTR("ATAN"))) return na > 1 ? atan2(a[0], a[1]) / k : atan(x) / k;
  if (!strcmp_P(f, PSTR("SQRT")) || !strcmp_P(f, PSTR("SQR"))) return sqrt(x);
  if (!strcmp_P(f, PSTR("ABS"))) return fabs(x);
  if (!strcmp_P(f, PSTR("LN"))) return log(x);
  if (!strcmp_P(f, PSTR("LOG"))) return log10(x);
  if (!strcmp_P(f, PSTR("EXP"))) return exp(x);
  if (!strcmp_P(f, PSTR("INT")) || !strcmp_P(f, PSTR("FLOOR"))) return floor(x);
  if (!strcmp_P(f, PSTR("CEIL"))) return ceil(x);
  if (!strcmp_P(f, PSTR("ROUND"))) return na > 1 ? round(x * pow(10, a[1])) / pow(10, a[1]) : round(x);
  if (!strcmp_P(f, PSTR("IF"))) return a[0] ? a[1] : a[2];
  if (!strcmp_P(f, PSTR("SGN"))) return (x > 0) - (x < 0);
  if (!strcmp_P(f, PSTR("POW"))) return pow(a[0], a[1]);
  if (!strcmp_P(f, PSTR("MIN"))) return min(a[0], a[1]);
  if (!strcmp_P(f, PSTR("MAX"))) return max(a[0], a[1]);
  if (!strcmp_P(f, PSTR("SINH"))) return sinh(x);
  if (!strcmp_P(f, PSTR("COSH"))) return cosh(x);
  if (!strcmp_P(f, PSTR("TANH"))) return tanh(x);
  if (!strcmp_P(f, PSTR("ASINH"))) return asinh(x);
  if (!strcmp_P(f, PSTR("ACOSH"))) return acosh(x);
  if (!strcmp_P(f, PSTR("ATANH"))) return atanh(x);
  if (!strcmp_P(f, PSTR("CBRT"))) return cbrt(x);
  if (!strcmp_P(f, PSTR("ROOT"))) return na > 1 ? (a[0] ? (a[1] < 0 && fmod(a[0], 2) == 1 ? -pow(-a[1], 1 / a[0]) : pow(a[1], 1 / a[0])) : NAN) : sqrt(x);
  if (!strcmp_P(f, PSTR("LOGN"))) return log(a[1]) / log(a[0]);      // logn(base, x)
  if (!strcmp_P(f, PSTR("MOD"))) { double m = fmod(a[0], a[1]); return (m && (m < 0) != (a[1] < 0)) ? m + a[1] : m; }
  if (!strcmp_P(f, PSTR("NCR")) || !strcmp_P(f, PSTR("NPR"))) {
    if (a[1] < 0 || a[1] > a[0]) return 0;
    double v = lgamma(a[0] + 1) - lgamma(a[0] - a[1] + 1) - (f[1] == 'C' ? lgamma(a[1] + 1) : 0);
    return round(exp(v));
  }
  if (!strcmp_P(f, PSTR("GCD")) || !strcmp_P(f, PSTR("LCM"))) {
    long p = labs(lround(a[0])), q = labs(lround(a[1])), g0 = p, g1 = q;
    while (g1) { long t = g0 % g1; g0 = g1; g1 = t; }
    return f[0] == 'G' ? g0 : (g0 ? (double)p / g0 * q : 0);
  }
  if (!strcmp_P(f, PSTR("RND")) || !strcmp_P(f, PSTR("RANDOM"))) return na && x >= 1 ? (double)random((long)x) : random(1000000) / 1000000.0;
  return exFail("unknown function");
}
double exPrimary() {
  exSkip();
  if (++exDepth > 6) { exDepth--; return exFail("too deeply nested"); }
  double v = 0;
  char c = *exP;
  if (isdigit((uint8_t)c) || c == '.') {
    char* end;
    v = strtod(exP, &end);
    if (end == exP) v = exFail("bad number");
    else if (exImplicit) {                           // Calc: e is Euler's e and E is memory E, never "x 10^"
      const char* q = exP;                           // (its EE key writes *10^( ), so 2e2 = 2 x e x 2, not 200
      while (isdigit((uint8_t)*q) || *q == '.') q++;
      if (q < end) { v = exDecimal(exP, q - exP); end = (char*)q; }
    }
    exP = end;
  } else if (c == '(') {
    exP++;
    v = exOr();
    if (!exOp(")")) v = exFail("missing )");
  } else if (exAl(c)) {
    const char* s = exP;
    while (exAlnum(*exP)) exP++;
    int len = exP - s;
    exSkip();
    if (*exP == '(') { exP++; v = exCall(s, len); }
    else if (!exName(s, len, &v)) {
      int k = len;                                   // Calc: e2, pi3, x2, c02 = e x 2, pi x 3, x x 2, c0 x 2
      bool found = false;
      while (exImplicit && !found && k > 1 && isdigit((uint8_t)s[k - 1])) found = exName(s, --k, &v);
      if (found) exP = s + k;                        // the digits are read next, as a number times this
      else v = exFail("unknown name");
    }
  } else v = exFail(c ? "syntax error" : "unexpected end");
  exDepth--;
  return v;
}
double exUnary();
double exPow() {
  double b = exPrimary();
  while (exP[0] == '!' && exP[1] != '=') { exP++; b = (b < 0 || b > 170) ? NAN : round(tgamma(b + 1)); }   // x!
  if (exOp("^")) return pow(b, exUnary());
  return b;
}
double exUnary() {
  exSkip();
  if (*exP == '-') { exP++; return -exUnary(); }
  if (*exP == '+') { exP++; return exUnary(); }
  return exPow();
}
double exMul() {
  double a = exUnary();
  while (!exErr) {
    exSkip();
    if (*exP == '*') { exP++; a *= exUnary(); }
    else if (*exP == '/') { exP++; a /= exUnary(); }
    else if (*exP == '%' || exWord("MOD")) { if (*exP == '%') exP++; double b = exUnary(); a = b ? fmod(a, b) : NAN; }
    else if (exImplicit && (exAl(*exP) || *exP == '(' || isdigit((uint8_t)*exP))) a *= exUnary();   // 2x, 3sin(x), 2(1+x), e2
    else break;
  }
  return a;
}
double exAdd() {
  double a = exMul();
  while (!exErr) {
    exSkip();
    if (*exP == '+') { exP++; a += exMul(); }
    else if (*exP == '-') { exP++; a -= exMul(); }
    else break;
  }
  return a;
}
double exCmp() {
  double a = exAdd();
  while (!exErr) {
    exSkip();
    const char* p = exP;
    if (p[0] == '=' && p[1] == '=') { exP += 2; a = a == exAdd(); }
    else if ((p[0] == '!' && p[1] == '=') || (p[0] == '<' && p[1] == '>')) { exP += 2; a = a != exAdd(); }
    else if (p[0] == '<' && p[1] == '=') { exP += 2; a = a <= exAdd(); }
    else if (p[0] == '>' && p[1] == '=') { exP += 2; a = a >= exAdd(); }
    else if (p[0] == '<' && p[1] != '<') { exP++; a = a < exAdd(); }
    else if (p[0] == '>' && p[1] != '>') { exP++; a = a > exAdd(); }
    else if (p[0] == '=' && p[1] != '=') { exP++; a = a == exAdd(); }
    else break;
  }
  return a;
}
double exNot() {
  exSkip();
  if (exP[0] == '!' && exP[1] != '=') { exP++; return !exNot(); }
  if (exWord("NOT")) return !exNot();
  return exCmp();
}
double exAnd() {
  double a = exNot();
  while (!exErr && (exOp("&&") || exWord("AND"))) { double b = exNot(); a = (a != 0) && (b != 0); }
  return a;
}
double exOr() {
  double a = exAnd();
  while (!exErr && (exOp("||") || exWord("OR"))) { double b = exAnd(); a = (a != 0) || (b != 0); }
  return a;
}
double exEval(const char* s) {                    // whole string must be one expression
  exP = s; exErr = nullptr; exDepth = 0;
  double v = exOr();
  exSkip();
  if (!exErr && *exP) exFail("syntax error");
  return v;
}

// ================= CALC (ClassWiz-style scientific + 2D/3D grapher + history) =================
// Pages: the calculator (key tabs 123 / f(x) / sym / const / mem / mode), the graph list (y1..y4 and z),
// the 2D plot, the 3D surface and the history log. The knob walks the tab strip and the keys;
// hold goes back one page. Settings, memories and graph functions live in /calc.cfg,
// every result is appended to /calc.log.
enum { CP_CALC, CP_LIST, CP_2D, CP_3D, CP_LOG };
enum { CF_NORM, CF_FIX, CF_SCI, CF_ENG };
char cExpr[128] = "";
uint8_t cLen = 0, cCur = 0;
int cPage = CP_CALC, ckTab = 0, ckFocus = 9, cEditFn = -1, cLogSel = 0, cLogTop = 0, cLogN = 0;
uint8_t cPop = 0;                                    // popup list open: 1 my equations, 2 equation examples, 3 graph examples
bool cAsk = false, cPick = false;                    // naming a graph set / picking a saved one
bool cFresh = false, cShowFrac = false, cErrShown = false;
double cAns = 0;
char cShow[40] = "0", cNote[48] = "";
const uint16_t CG_COL[4] = {RGB565(0, 150, 255), RGB565(232, 76, 61), RGB565(40, 180, 100), RGB565(230, 150, 20)};

// ---------- key tabs (labels + what each key inserts; '@' = an action) ----------
const char CK_LAB_P[] PROGMEM =
  "7\n8\n9\n\xF6\n(\n)\n\x11\n\x10\n4\n5\n6\n*\n^\n\xFB\nx\xFD\nDEL\n1\n2\n3\n-\na/b\nx\nAns\nAC\n0\n.\nEE\n+\n(-)\nSOLVE\nS\x1D" "D\n=\n"
  "sin\ncos\ntan\nasin\nacos\natan\nln\nlog\nsinh\ncosh\ntanh\ne^x\n10^x\nAbs\n3\xFB\nn\xFB\nfloor\nceil\nround\nmod\nnCr\nnPr\nx!\ngcd\nd/dx\n\xF4\xF5" "dx\n\xE4\nlcm\n\x11\n\x10\nDEL\n=\n"
  "\xE3\ne\nx\ny\nA\nB\nC\nD\nE\nF\nM\n,\n=\n<\n>\n\xF8\nc0\ng0\nG0\nh0\nNA\nkB\nqe\nme\nlogb\nmin\nmax\nRan#\n\x11\n\x10\nDEL\n=\n"
  "\xE3\ne\n\xED\ngamma\n\xFB" "2\n\xFB" "3\nln2\n\xE7\n"
  "c\ng\nG\nh\nhbar\nNA\nkB\nR\n"
  "qe\nme\nmp\n\xEE" "0\n\xE6" "0\n\xE5\natm\nAU\n"
  "\xF8\n%\n1/x\nx^3\n\x11\n\x10\nDEL\n=\n"
  "\x1A" "A\n\x1A" "B\n\x1A" "C\n\x1A" "D\n\x1A" "E\n\x1A" "F\n\x1A" "M\nM+\nA\nB\nC\nD\nE\nF\nM\nAns\nx=Ans\nprev\nnext\nClrMem\n(\n)\nAC\nSOLVE\n\x11\n\x10\nDEL\nS\x1D" "D\nAns\nx\ny\n=\n"
  "D/R\nFormat\ndig-\ndig+\nPretty\nS\x1D" "D\nClrLog\nReset\nSaveEq\nMyEqs\nEq ex\nGraphs\nGr ex\nSaveGr\nOpenGr\nClrEqs";
const char CK_INS_P[] PROGMEM =
  "7\n8\n9\n/\n(\n)\n@L\n@R\n4\n5\n6\n*\n^\nsqrt(\n^2\n@D\n1\n2\n3\n-\n@/\nx\nans\n@C\n0\n.\n*10^(\n+\n-\n@S\n@F\n@=\n"
  "sin(\ncos(\ntan(\nasin(\nacos(\natan(\nln(\nlog(\nsinh(\ncosh(\ntanh(\nexp(\n10^(\nabs(\ncbrt(\nroot(\nfloor(\nceil(\nround(\nmod(\nncr(\nnpr(\n!\ngcd(\nderiv(\ninteg(\nsigma(\nlcm(\n@L\n@R\n@D\n@=\n"
  "pi\ne\nx\ny\nA\nB\nC\nD\nE\nF\nM\n,\n=\n<\n>\n*pi/180\nc0\ng0\nG0\nh0\nNA\nkB\nqe\nme\nlogn(\nmin(\nmax(\nrnd()\n@L\n@R\n@D\n@=\n"
  "pi\ne\nphi\ngam\nsqrt(2)\nsqrt(3)\nln(2)\n2*pi\n"
  "c0\ng0\nG0\nh0\nhb0\nNA\nkB\nR0\n"
  "qe\nme\nmp0\nep0\nmu0\nsb0\natm\nau0\n"
  "*pi/180\n/100\n^(-1)\n^3\n@L\n@R\n@D\n@=\n"
  "@sA\n@sB\n@sC\n@sD\n@sE\n@sF\n@sM\n@+\nA\nB\nC\nD\nE\nF\nM\nans\n@x\n@p\n@n\n@M\n(\n)\n@C\n@S\n@L\n@R\n@D\n@F\nans\nx\ny\n@=\n"
  "@a\n@f\n@-\n@^\n@P\n@F\n@K\n@Z\n@W\n@O\n@E\n@G\n@X\n@V\n@B\n@Q";
const char CK_TABS_P[] PROGMEM = "123\nf(x)\nsym\nconst\nmem\nmode\n2D\n3D\nlog";
const int CK_NT = 9, CK_KEYTABS = 6, CK_MODE = 5, CK_TW = 35;   // tab strip: 6 key tabs + 3 pages
int ckCount() { return ckTab == CK_MODE ? 16 : 32; }
const char* ckLabel(int k) { return pgmItem(CK_LAB_P, ckTab * 32 + k); }
const char* ckInsert(int k) { return pgmItem(CK_INS_P, ckTab * 32 + k); }

// ---------- settings + variables ----------
uint16_t calcAccent() { return appAccent(APP_CALC); }
void calcSaveCfg() {
  if (!fsOk) return;
  File f = LittleFS.open(FS("/calc.cfg"), FS("w"));
  if (f) { f.write((const uint8_t*)&CC, sizeof CC); f.close(); }
}
void calcLoadCfg() {
  memset(&CC, 0, sizeof CC);
  CC.ver = 1; CC.digits = 4; CC.pretty = 1;
  strcpy_P(CC.fn[0], PSTR("sin(x)")); strcpy_P(CC.fn[1], PSTR("x^2/4-2")); strcpy_P(CC.z, PSTR("sin(x)*cos(y)"));
  if (!fsOk) return;
  File f = LittleFS.open(FS("/calc.cfg"), FS("r"));
  if (f && f.size() == sizeof CC) { CalcCfg t; if (f.read((uint8_t*)&t, sizeof t) == (int)sizeof t && t.ver == 1) CC = t; }
  if (f) f.close();
}
// Calc's named constants (the const tab inserts these names). CODATA 2022; c, h, NA, kB, qe are exact by definition,
// so hbar = h/2pi, R = NA kB and sigma = 2pi^5 kB^4/(15 h^3 c^2) are exact too (stored to full double precision).
const char CALC_CONST_N[] PROGMEM = "c0\ng0\nG0\nh0\nNA\nkB\nqe\nme\nphi\ngam\nhb0\nR0\nmp0\nep0\nmu0\nsb0\natm\nau0";
const double CALC_CONST_V[18] PROGMEM = {
  299792458.0, 9.80665, 6.67430e-11, 6.62607015e-34, 6.02214076e23, 1.380649e-23, 1.602176634e-19, 9.1093837139e-31,
  1.6180339887498949, 0.57721566490153286, 1.0545718176461565e-34, 8.31446261815324, 1.67262192595e-27, 8.8541878188e-12,
  1.25663706127e-6, 5.6703744191844314e-8, 101325.0, 1.495978707e11};
bool calcVar(const char* n, int len, double* v) {
  if (len == 1) {
    char c = n[0];
    if (c == 'x' || c == 'X') { *v = CC.x; return true; }
    if (c == 'y' || c == 'Y') { *v = CC.y; return true; }
    if (c >= 'A' && c <= 'F') { *v = CC.mem[c - 'A']; return true; }
    if (c == 'M') { *v = CC.mem[6]; return true; }
    return false;
  }
  if (len == 3 && !strncasecmp_P(n, PSTR("ans"), 3)) { *v = cAns; return true; }
  PGM_P p = CALC_CONST_N;
  for (int i = 0; i < 18; i++) {                   // walk the flash list: "c0\ng0\n..."
    PGM_P q = p;
    while (pgm_read_byte(q) && pgm_read_byte(q) != '\n') q++;
    if (q - p == len && !strncmp_P(n, p, len)) { memcpy_P(v, &CALC_CONST_V[i], sizeof(double)); return true; }
    if (!pgm_read_byte(q)) break;
    p = q + 1;
  }
  return false;
}
void calcEvalSetup() { exVar = calcVar; exCell = nullptr; exDeg = CC.deg; exImplicit = true; exBound = &CC.x; }
void calcEvalDone() { exImplicit = false; exBound = nullptr; }
double calcAt(const char* e, double xv, double yv) {   // evaluate an expression at (x, y), keeping the user's x/y
  double sx = CC.x, sy = CC.y;
  CC.x = xv; CC.y = yv;
  double v = exEval(e);
  CC.x = sx; CC.y = sy;
  return exErr ? NAN : v;
}
bool calcFraction(double v, long* p, long* q) {      // continued fraction, denominators up to 9999
  if (fabs(v) > 1e9 || v == floor(v)) return false;
  double x = fabs(v);
  long h0 = 1, h1 = 0, k0 = 0, k1 = 1;
  for (int i = 0; i < 20; i++) {
    double a = floor(x);
    long h2 = (long)a * h0 + h1, k2 = (long)a * k0 + k1;
    if (k2 > 9999) break;
    h1 = h0; h0 = h2; k1 = k0; k0 = k2;
    if (fabs(fabs(v) - (double)h0 / k0) < 1e-10 * max(1.0, fabs(v))) { *p = v < 0 ? -h0 : h0; *q = k0; return true; }
    if (x - a < 1e-12) break;
    x = 1 / (x - a);
  }
  return false;
}
void calcFmt(double v, char* b, size_t n) {
  long p, q;
  if (isnan(v)) { snprintf_P(b, n, PSTR("Math ERROR")); return; }
  if (cShowFrac && calcFraction(v, &p, &q)) { snprintf_P(b, n, PSTR("%ld/%ld"), p, q); return; }
  int d = CC.digits % 10;
  switch (CC.fmt) {
    case CF_FIX: snprintf_P(b, n, PSTR("%.*f"), d, v); break;
    case CF_SCI: snprintf_P(b, n, PSTR("%.*e"), d, v); break;
    case CF_ENG: {
      if (!v) { snprintf_P(b, n, PSTR("0")); break; }
      int e = (int)floor(log10(fabs(v)) / 3) * 3;
      snprintf_P(b, n, PSTR("%.*fE%d"), d, v / pow(10, e), e);
      break;
    }
    default: fmtNum(v, b, n, 10);
  }
}

// ---------- history log (/calc.log, "expression<TAB>result" per line) ----------
void calcLog(const char* e, const char* r) {
  if (!fsOk) return;
  File f = LittleFS.open(FS("/calc.log"), FS("a"));
  if (!f) return;
  f.print(e); f.write('\t'); f.print(r); f.write('\n');
  size_t sz = f.size();
  f.close();
  if (sz > 6000) {                                   // keep the newest ~3 KB
    File in = LittleFS.open(FS("/calc.log"), FS("r")), out = LittleFS.open(FS("/calc.tmp"), FS("w"));
    if (in && out) {
      in.seek(sz - 3000);
      in.readStringUntil('\n');
      uint8_t buf[128];
      int r;
      while ((r = in.read(buf, sizeof buf)) > 0) out.write(buf, r);
    }
    if (in) in.close();
    if (out) out.close();
    LittleFS.remove(FS("/calc.log"));
    LittleFS.rename(FS("/calc.tmp"), FS("/calc.log"));
  }
}

// ---------- the editor (cursor inside the expression) ----------
void calcInsert(const char* s) {
  if (cFresh) {                                      // after a result: an operator continues from Ans
    cFresh = false;
    if (strchr("+-*/^!", s[0]) && s[0]) { strcpy_P(cExpr, PSTR("ans")); cLen = cCur = 3; }
    else if (s[0] != '@') { cExpr[0] = 0; cLen = cCur = 0; }
  }
  int n = strlen(s);
  if (cLen + n >= (int)sizeof cExpr - 1) return;
  memmove(cExpr + cCur + n, cExpr + cCur, cLen - cCur + 1);
  memcpy(cExpr + cCur, s, n);
  cLen += n; cCur += n;
}
void calcBackspace() {
  if (!cCur) return;
  int k = cCur - 1;
  if (cExpr[k] == '(') while (k > 0 && isalpha((uint8_t)cExpr[k - 1])) k--;     // a whole "sin(" at once
  else if (k >= 2 && !strncmp_P(cExpr + k - 2, PSTR("ans"), 3)) k -= 2;
  memmove(cExpr + k, cExpr + cCur, cLen - cCur + 1);
  cLen -= cCur - k; cCur = k;
}

// ---------- textbook ("natural") display ----------
// A small recursive layout: each piece is measured first (width, ascent, descent above/below the
// baseline) and then drawn, so a/b becomes a stacked fraction, x^2 a raised power, sqrt( a real root.
const char* tsP = "";
void tsExpr(int x, int y, int s, bool draw, int* w, int* as, int* ds);
void tsChar(int x, int y, int s, char c, uint16_t col) { tft.drawChar(x, y - 8 * s, c, col, col, s); }
void tsWord(int x, int y, int s, bool draw, const char* t, int n, int* w, int* as, int* ds) {
  if (draw) for (int i = 0; i < n; i++) tsChar(x + i * 6 * s, y, s, t[i], C_INK);
  *w = n * 6 * s; *as = 8 * s; *ds = 0;
}
void tsParens(int x, int y, int s, bool draw, int iw, int ias, int ids, char kind) {   // ( ) or | | around a box
  if (!draw) return;
  int top = y - ias - 1, bot = y + ids + 1, h = bot - top;
  if (kind == '|') { tft.drawFastVLine(x + 2, top, h, C_INK); tft.drawFastVLine(x + iw + 4 * s + 1, top, h, C_INK); return; }
  if (h <= 8 * s + 2) { tsChar(x, y, s, '(', C_INK); tsChar(x + 3 * s + iw, y, s, ')', C_INK); return; }
  int xl = x + 3, xr = x + iw + 4 * s;                 // tall brackets for fractions inside
  tft.drawLine(xl, top + 3, xl + 2, top, C_INK); tft.drawFastVLine(xl, top + 3, h - 6, C_INK); tft.drawLine(xl, bot - 3, xl + 2, bot, C_INK);
  tft.drawLine(xr, top + 3, xr - 2, top, C_INK); tft.drawFastVLine(xr, top + 3, h - 6, C_INK); tft.drawLine(xr, bot - 3, xr - 2, bot, C_INK);
}
void tsGroup(int x, int y, int s, bool draw, int* w, int* as, int* ds, char kind, bool showParens) {
  const char* p0 = tsP;                              // tsP sits just after the opening bracket
  int iw, ia, id;
  tsExpr(0, 0, s, false, &iw, &ia, &id);
  int pad = showParens ? 3 * s : 0;
  if (draw) {
    tsP = p0;
    tsExpr(x + pad, y, s, true, &iw, &ia, &id);
    if (showParens) tsParens(x, y, s, true, iw, ia, id, kind);
  }
  if (*tsP == ')') tsP++;
  *w = iw + 2 * pad; *as = ia + 1; *ds = id + 1;
}
int tsDepth = 0;                                     // bracket nesting: each level costs ~600 bytes of the 4 KB stack
void tsPrimBody(int x, int y, int s, bool draw, int* w, int* as, int* ds);
void tsPrim(int x, int y, int s, bool draw, int* w, int* as, int* ds) {
  if (tsDepth >= 4) {                                // too deep to draw safely: the rest of this bracket as plain text
    const char* b = tsP;
    int lvl = 0;
    while (*tsP && !(lvl == 0 && (*tsP == ')' || *tsP == ','))) { if (*tsP == '(') lvl++; else if (*tsP == ')') lvl--; tsP++; }
    tsWord(x, y, s, draw, b, tsP - b, w, as, ds);
    return;
  }
  tsDepth++;
  tsPrimBody(x, y, s, draw, w, as, ds);
  tsDepth--;
}
void tsPrimBody(int x, int y, int s, bool draw, int* w, int* as, int* ds) {
  while (*tsP == ' ') tsP++;
  char c = *tsP;
  if (isdigit((uint8_t)c) || c == '.') {
    const char* b = tsP;
    while (isdigit((uint8_t)*tsP) || *tsP == '.') tsP++;
    tsWord(x, y, s, draw, b, tsP - b, w, as, ds);
  } else if (c == '(') { tsP++; tsGroup(x, y, s, draw, w, as, ds, '(', true); }
  else if (c == '-') {
    tsP++;
    int w2, a2, d2;
    tsWord(x, y, s, draw, "-", 1, w, as, ds);
    tsPrim(x + 6 * s, y, s, draw, &w2, &a2, &d2);
    *w += w2; *as = max(*as, a2); *ds = max(*ds, d2);
  } else if (isalpha((uint8_t)c)) {
    const char* b = tsP;
    while (isalnum((uint8_t)*tsP) || *tsP == '_') tsP++;
    int n = tsP - b;
    if (*tsP != '(') {                               // a name: pi becomes the real symbol
      if (n == 2 && !strncmp_P(b, PSTR("pi"), 2)) tsWord(x, y, s, draw, "\xE3", 1, w, as, ds);
      else tsWord(x, y, s, draw, b, n, w, as, ds);
      return;
    }
    tsP++;
    int gw, ga, gd;
    if ((n == 4 && !strncmp_P(b, PSTR("sqrt"), 4)) || (n == 4 && !strncmp_P(b, PSTR("cbrt"), 4))) {   // radical sign
      int off = 5 + 2 * s;
      const char* p0 = tsP;
      tsGroup(0, 0, s, false, &gw, &ga, &gd, '(', false);
      if (draw) {
        tsP = p0;
        tsGroup(x + off + 2, y, s, true, &gw, &ga, &gd, '(', false);
        int top = y - ga - 2;
        tft.drawLine(x, y - 4 * s, x + 2, y - 4 * s, C_INK); tft.drawLine(x + 2, y - 4 * s, x + off / 2, y + gd, C_INK);
        tft.drawLine(x + off / 2, y + gd, x + off, top, C_INK); tft.drawFastHLine(x + off, top, gw + 4, C_INK);
        if (b[0] == 'c') tft.drawChar(x - 1, top - 2, '3', C_INK, C_INK, 1);
      }
      *w = gw + off + 4; *as = ga + 3; *ds = gd;
      return;
    }
    if (n == 3 && !strncmp_P(b, PSTR("abs"), 3)) { tsGroup(x + 2, y, s, draw, &gw, &ga, &gd, '|', false); if (draw) tsParens(x - 1, y, s, true, gw, ga, gd, '|'); *w = gw + 4 * s + 4; *as = ga; *ds = gd; return; }
    const char* name = b;
    int nl = n;
    if (n == 5 && !strncmp_P(b, PSTR("deriv"), 5)) { name = "d/dx"; nl = 4; }
    else if (n == 5 && !strncmp_P(b, PSTR("sigma"), 5)) { name = "\xE4"; nl = 1; }
    else if (n == 5 && !strncmp_P(b, PSTR("integ"), 5)) { name = "\xF4"; nl = 1; }
    int nw, na, nd;
    tsWord(x, y, s, draw, name, nl, &nw, &na, &nd);
    if (draw && name[0] == '\xF4') tsChar(x, y + 8 * s - 2, s, '\xF5', C_INK);   // bottom half of the integral sign
    tsGroup(x + nw, y, s, draw, &gw, &ga, &gd, '(', true);
    *w = nw + gw; *as = max(na, ga); *ds = max(nd + (name[0] == '\xF4' ? 8 * s - 2 : 0), gd);
  } else {                                           // anything else: show it as it is
    tsP++;
    char t = c == '*' ? (char)0xFA : c;
    tsWord(x, y, s, draw, &t, 1, w, as, ds);
  }
}
void tsPower(int x, int y, int s, bool draw, int* w, int* as, int* ds) {
  tsPrim(x, y, s, draw, w, as, ds);
  while (*tsP == '!') { int a, b, c; tsWord(x + *w, y, s, draw, "!", 1, &a, &b, &c); *w += a; tsP++; }
  while (*tsP == '^') {                              // raised, smaller exponent (its brackets are implied)
    tsP++;
    int es = 1, ew, ea, ed;
    bool grp = *tsP == '(';
    int ey = y - 8 * s + 6;
    if (grp) { tsP++; tsGroup(x + *w + 1, ey, es, draw, &ew, &ea, &ed, '(', false); }
    else tsPrim(x + *w + 1, ey, es, draw, &ew, &ea, &ed);
    *w += ew + 2;
    *as = max(*as, (y - ey) + ea);
  }
}
void tsTerm(int x, int y, int s, bool draw, int* w, int* as, int* ds) {
  *w = 0; *as = 8 * s; *ds = 0;
  for (;;) {
    while (*tsP == ' ') tsP++;
    if (!*tsP || *tsP == ')' || *tsP == ',' || strchr("+-=<>", *tsP)) { if (*w || *tsP != '-') break; }
    const char* p0 = tsP;
    int fw, fa, fd;
    tsPower(0, 0, s, false, &fw, &fa, &fd);
    if (*tsP == '/') {                               // stacked fraction: this factor over the next one(s)
      const char* d0 = tsP + 1;
      tsP = d0;
      int dw = 0, da = 0, dd = 0;
      for (;;) { int a, b, c; tsPower(0, 0, s, false, &a, &b, &c); dw += a; da = max(da, b); dd = max(dd, c); if (*tsP != '/') break; tsP++; dw += 6 * s; }
      int fwid = max(fw, dw) + 4, bar = y - 4 * s;
      if (draw) {
        tsP = p0; tsPower(x + *w + (fwid - fw) / 2, bar - 2 - fd, s, true, &fw, &fa, &fd);
        tsP = d0;
        int dx = x + *w + (fwid - dw) / 2;
        for (;;) { int a, b, c; tsPower(dx, bar + 2 + da, s, true, &a, &b, &c); dx += a; if (*tsP != '/') break; tsP++; tsChar(dx, bar + 2 + da, s, (char)0xFA, C_INK); dx += 6 * s; }
        tft.drawFastHLine(x + *w, bar, fwid, C_INK);
      }
      *w += fwid + 2; *as = max(*as, 4 * s + 2 + fd + fa); *ds = max(*ds, -4 * s + 2 + da + dd);
    } else {
      if (draw) { tsP = p0; tsPower(x + *w, y, s, true, &fw, &fa, &fd); }
      *w += fw; *as = max(*as, fa); *ds = max(*ds, fd);
    }
    if (*tsP == '*') { tsP++; int a, b, c; tsWord(x + *w, y, s, draw, "\xFA", 1, &a, &b, &c); *w += a; }
  }
}
void tsExpr(int x, int y, int s, bool draw, int* w, int* as, int* ds) {
  *w = 0; *as = 8 * s; *ds = 0;
  while (*tsP && *tsP != ')') {
    int tw, ta, td;
    const char* p0 = tsP;
    tsTerm(x + *w, y, s, draw, &tw, &ta, &td);
    *w += tw; *as = max(*as, ta); *ds = max(*ds, td);
    char c = *tsP;
    if (!c || c == ')') break;
    if (strchr("+-=<>,", c)) {
      char op[2] = {c, 0};
      tsP++;
      if ((c == '<' || c == '>') && *tsP == '=') { op[0] = c == '<' ? (char)0xF3 : (char)0xF2; tsP++; }
      int pad = c == ',' ? 0 : 2 * s;
      if (draw) tsChar(x + *w + pad, y, s, op[0], C_INK);
      *w += 6 * s + pad * 2;
    } else if (tsP == p0) tsP++;                     // never get stuck on something unexpected
  }
}
void calcDrawPretty(int top, int h) {                // the input as a textbook would print it
  tft.fillRect(4, top, 312, h, E_WHITE);
  if (!CC.pretty || !cLen) return;
  int s = 2, w, as, ds;
  tsP = cExpr; tsExpr(0, 0, s, false, &w, &as, &ds);
  if (w > 300 || as + ds > h - 2) { s = 1; tsP = cExpr; tsExpr(0, 0, s, false, &w, &as, &ds); }
  int x = w > 300 ? 310 - w : 10, y = top + (h - as - ds) / 2 + as;
  tsP = cExpr; tsExpr(x, y, s, true, &w, &as, &ds);
}

// ---------- calculator screen ----------
void calcDrawEdit() {                                // the raw input line with its cursor
  tft.fillRect(4, 22, 312, 11, E_WHITE);
  int from = cCur > 48 ? cCur - 48 : 0;
  char line[52];
  snprintf_P(line, sizeof line, PSTR("%.50s"), cExpr + from);
  putText(8, 24, 1, cLen ? C_INK : C_MID, E_WHITE, cLen ? line : FS("type with the keys below..."));
  if (!cFresh) tft.fillRect(8 + (cCur - from) * 6, 23, 2, 10, calcAccent());
  char st[24];                                       // e.g. "D FIX4 a/b" in the corner
  const char* fm[4] = {"", "FIX", "SCI", "ENG"};
  if (CC.fmt) snprintf_P(st, sizeof st, PSTR("%s %s%d%s"), CC.deg ? FS("D") : FS("R"), fm[CC.fmt % 4], CC.digits % 10, cShowFrac ? FS(" a/b") : FS(""));
  else snprintf_P(st, sizeof st, PSTR("%s%s"), CC.deg ? FS("D") : FS("R"), cShowFrac ? FS(" a/b") : FS(""));
  putText(312 - (int)strlen(st) * 6, 24, 1, calcAccent(), E_WHITE, st);
}
void calcDrawResult() {
  tft.fillRect(4, 80, 312, 16, E_WHITE);
  putText(8, 84, 1, C_MID, E_WHITE, cNote);
  int sz = (int)strlen(cShow) * 12 <= 300 - (int)strlen(cNote) * 6 ? 2 : 1;
  putText(312 - (int)strlen(cShow) * 6 * sz, sz == 2 ? 80 : 84, sz, cErrShown ? E_CORAL : C_INK, E_WHITE, cShow);
}
void calcDrawDisplay() {
  tft.fillRect(4, 20, 312, 77, E_WHITE);
  tft.drawRect(3, 19, 314, 79, E_RULE);
  calcDrawEdit();
  calcDrawPretty(35, 44);
  calcDrawResult();
}
void calcDrawTab(int i) {
  int x = i * CK_TW;
  bool sel = i == ckTab && i < CK_KEYTABS, foc = ckFocus == i;
  uint16_t acc = calcAccent(), bg = sel ? acc : (i >= CK_KEYTABS ? E_ROW2 : E_HEAD);
  const char* l = pgmItem(CK_TABS_P, i);
  tft.fillRect(x, 99, CK_TW, 13, bg);
  if (i == CK_NT - 1) tft.fillRect(x + CK_TW, 99, W - x - CK_TW, 13, E_ROW2);
  putText(x + CK_TW / 2 - (int)strlen(l) * 3, 102, 1, sel ? E_ONACC : (i >= CK_KEYTABS ? acc : C_INK), bg, l);
  if (foc) tft.drawRect(x, 99, CK_TW, 13, sel ? E_ONACC : acc);
  else tft.drawFastVLine(x + CK_TW - 1, 100, 11, E_RULE);
}
void calcDrawKey(int k) {
  int x = (k % 8) * 40, y = 114 + (k / 8) * 31;
  if (k >= ckCount()) { tft.fillRect(x, y, 40, 31, C_LCD); return; }
  const char* l = ckLabel(k);
  const char* ins = ckInsert(k);
  bool foc = ckFocus == CK_NT + k, act = ins[0] == '@', num = isdigit((uint8_t)ins[0]) || ins[0] == '.';
  uint16_t acc = calcAccent(), bg = foc ? acc : (!strcmp_P(ins, PSTR("@=")) ? E_HEAD : (num ? E_WHITE : (act ? E_HEAD : E_ROW2)));
  uint16_t fg = foc ? E_ONACC : (!strcmp_P(ins, PSTR("@=")) || !strcmp_P(ins, PSTR("@S")) ? acc : C_INK);
  tft.fillRect(x + 1, y, 38, 29, bg);
  tft.drawRect(x + 1, y, 38, 29, foc ? acc : E_RULE);
  int sz = strlen(l) <= 2 && ckTab == 0 ? 2 : 1;
  putText(x + 20 - (int)strlen(l) * 3 * sz, y + (sz == 2 ? 7 : 11), sz, fg, bg, l);
}
void calcDrawModeInfo() {                           // the "mode" tab has two rows of keys and this note under them
  tft.fillRect(0, 176, W, 64, C_LCD);
  char b[56];
  snprintf_P(b, sizeof b, PSTR("%s, %s %d digits, textbook %s, a/b %s"), CC.deg ? FS("degrees") : FS("radians"),
             pgmItem(PSTR("normal\nfixed\nsci\neng"), CC.fmt % 4), CC.digits % 10, CC.pretty ? FS("on") : FS("off"), cShowFrac ? FS("on") : FS("off"));
  putText(8, 180, 1, C_INK, C_LCD, b);
  putText(8, 194, 1, C_MID, C_LCD, FS("SaveEq keeps the line, MyEqs brings it back;"));
  putText(8, 205, 1, C_MID, C_LCD, FS("Eq ex / Gr ex: ready-made equations + graphs."));
  putText(8, 216, 1, C_MID, C_LCD, FS("2x, 3sin(x) multiply; SOLVE finds x in x^2=2;"));
  putText(8, 227, 1, C_MID, C_LCD, FS("d/dx(f,a)  integ(f,a,b)  sigma(f,a,b) use x"));
}
void calcDrawKeys() {
  for (int k = 0; k < 32; k++) if (k < ckCount()) calcDrawKey(k);
  if (ckTab == CK_MODE) calcDrawModeInfo();
}
void calcDrawAll() {
  tft.fillScreen(C_LCD);
  char t[24];
  if (cEditFn >= 0) snprintf_P(t, sizeof t, cEditFn < 4 ? PSTR("Calc: edit y%d") : PSTR("Calc: edit z(x,y)"), cEditFn + 1);
  else snprintf_P(t, sizeof t, PSTR("Calc"));
  drawTitleBar(t, calcAccent());
  calcDrawDisplay();
  for (int i = 0; i < CK_NT; i++) calcDrawTab(i);
  calcDrawKeys();
}

// ---------- evaluate / solve ----------
const char* calcEqSplit(char* buf, size_t n) {      // "L=R" -> "(L)-(R)"; nullptr if there's no equation sign
  for (int i = 0; i < cLen; i++) {
    char c = cExpr[i], p = i ? cExpr[i - 1] : 0, q = cExpr[i + 1];
    if (c == '=' && p != '<' && p != '>' && p != '!' && p != '=' && q != '=') {
      snprintf_P(buf, n, PSTR("(%.*s)-(%s)"), i, cExpr, cExpr + i + 1);
      return buf;
    }
  }
  return nullptr;
}
bool calcSolveExpr(const char* f, double* root) {    // Newton from the current x, then a sign-change scan
  calcEvalSetup();
  double x = CC.x;
  for (int i = 0; i < 60; i++) {
    double fx = calcAt(f, x, CC.y);
    if (isnan(fx)) break;
    if (fabs(fx) < 1e-12) { *root = x; return true; }
    double h = 1e-6 * max(1.0, fabs(x)), d = (calcAt(f, x + h, CC.y) - calcAt(f, x - h, CC.y)) / (2 * h);
    if (!d || isnan(d)) break;
    double nx = x - fx / d;
    if (fabs(nx - x) < 1e-13 * max(1.0, fabs(x))) { *root = nx; return fabs(calcAt(f, nx, CC.y)) < 1e-6; }
    x = nx;
    if (!(i & 15)) yield();
  }
  double best = NAN;                                 // scan outwards from x for a sign change
  for (double span = 10; span <= 1e5 && isnan(best); span *= 10) {
    double lo = CC.x - span, step = span / 500, plo = calcAt(f, lo, CC.y);
    for (int i = 1; i <= 1000; i++) {
      double hi = lo + step, phi = calcAt(f, hi, CC.y);
      if (!isnan(plo) && !isnan(phi) && (plo <= 0) != (phi <= 0)) {
        double a = lo, b = hi, fa = plo;
        for (int k = 0; k < 80; k++) { double m = (a + b) / 2, fm = calcAt(f, m, CC.y); if ((fa <= 0) == (fm <= 0)) { a = m; fa = fm; } else b = m; }
        double r = (a + b) / 2;
        if (fabs(calcAt(f, r, CC.y)) < 1e-6 && (isnan(best) || fabs(r - CC.x) < fabs(best - CC.x))) best = r;
      }
      lo = hi; plo = phi;
      if (!(i & 63)) yield();
    }
  }
  if (isnan(best)) return false;
  *root = best;
  return true;
}
void calcEquals(bool solve) {
  if (!cLen) return;
  char eq[280];
  const char* f = calcEqSplit(eq, sizeof eq);
  cErrShown = false; cNote[0] = 0;
  calcEvalSetup();
  if (f || solve) {
    double r;
    if (!calcSolveExpr(f ? f : cExpr, &r)) { cErrShown = true; snprintf_P(cShow, sizeof cShow, PSTR("no solution found")); calcEvalDone(); SFX(T_WALL); return; }
    CC.x = r; cAns = r;
    char v[24]; calcFmt(r, v, sizeof v);
    snprintf_P(cShow, sizeof cShow, PSTR("x=%s"), v);
    double res = calcAt(f ? f : cExpr, r, CC.y);
    char rv[16]; fmtNum(res, rv, sizeof rv, 3);
    snprintf_P(cNote, sizeof cNote, PSTR("L-R=%s"), rv);
  } else {
    double v = exEval(cExpr);
    if (exErr) { cErrShown = true; snprintf_P(cShow, sizeof cShow, PSTR("%s"), exErr); calcEvalDone(); SFX(T_WALL); return; }
    cAns = v;
    calcFmt(v, cShow, sizeof cShow);
  }
  calcEvalDone();
  calcLog(cExpr, cShow);
  calcSaveCfg();
  cFresh = true;
}
void calcAction(const char* a, uint32_t now) {
  (void)now;
  switch (a[1]) {
    case 'L': if (cCur) cCur--; cFresh = false; break;
    case 'R': if (cCur < cLen) cCur++; cFresh = false; break;
    case 'D': cFresh = false; calcBackspace(); break;
    case 'C': cLen = cCur = 0; cExpr[0] = 0; cFresh = false; strcpy_P(cShow, PSTR("0")); cNote[0] = 0; cErrShown = false; break;
    case '/': calcInsert("()/()"); cCur -= 4; break;
    case '=':
      if (cEditFn >= 0) {                            // editing a graph function: = saves it
        snprintf_P(cEditFn < 4 ? CC.fn[cEditFn] : CC.z, 60, PSTR("%s"), cExpr);
        calcSaveCfg();
        int was = cEditFn;
        cEditFn = -1;
        calcOpenList(was);
        return;
      }
      calcEquals(false);
      break;
    case 'S': calcEquals(true); break;
    case 'F': cShowFrac = !cShowFrac; if (!cErrShown && cShow[0] && strcmp_P(cShow, PSTR("0"))) calcFmt(cAns, cShow, sizeof cShow); break;
    case 's': { int i = a[2] == 'M' ? 6 : a[2] - 'A'; CC.mem[i] = cAns; calcSaveCfg(); snprintf_P(cNote, sizeof cNote, PSTR("Ans -> %c"), a[2]); break; }
    case '+': CC.mem[6] += cAns; calcSaveCfg(); snprintf_P(cNote, sizeof cNote, PSTR("M += Ans")); break;
    case 'x': CC.x = cAns; calcSaveCfg(); snprintf_P(cNote, sizeof cNote, PSTR("x = Ans")); break;
    case 'M': memset(CC.mem, 0, sizeof CC.mem); calcSaveCfg(); snprintf_P(cNote, sizeof cNote, PSTR("memories cleared")); break;
    case 'p': case 'n': calcRecall(a[1] == 'p' ? 1 : -1); break;
    case 'a': CC.deg ^= 1; calcSaveCfg(); break;
    case 'f': CC.fmt = (CC.fmt + 1) % 4; calcSaveCfg(); if (!cErrShown) calcFmt(cAns, cShow, sizeof cShow); break;
    case '-': case '^': CC.digits = constrain(CC.digits + (a[1] == '^' ? 1 : -1), 0, 9); calcSaveCfg(); if (!cErrShown) calcFmt(cAns, cShow, sizeof cShow); break;
    case 'P': CC.pretty ^= 1; calcSaveCfg(); break;
    case 'K': if (fsOk) LittleFS.remove(FS("/calc.log")); snprintf_P(cNote, sizeof cNote, PSTR("history cleared")); break;
    case 'W': calcEqSave(); break;
    case 'O': calcEqList(); break;
    case 'E': calcExMenu(2); break;
    case 'G': calcOpenList(0); break;
    case 'X': calcExMenu(3); break;
    case 'V': cAsk = true; askBegin(FS("Save these graphs as (name):"), FS(""), 12, calcAccent()); break;
    case 'B': cPick = true; fpBegin(FS("Open saved graphs"), FS("/calc"), FS(".gph"), nullptr, calcAccent()); break;
    case 'Q': if (fsOk) LittleFS.remove(FS("/calc.eq")); snprintf_P(cNote, sizeof cNote, PSTR("saved equations cleared")); break;
    case 'Z': {                                      // settings + memories back to default, graphs kept
      char f0[4][60], z0[60];
      memcpy(f0, CC.fn, sizeof f0); memcpy(z0, CC.z, sizeof z0);
      memset(&CC, 0, sizeof CC);
      CC.ver = 1; CC.digits = 4; CC.pretty = 1;
      memcpy(CC.fn, f0, sizeof f0); memcpy(CC.z, z0, sizeof z0);
      cShowFrac = false;
      calcSaveCfg();
      snprintf_P(cNote, sizeof cNote, PSTR("settings reset"));
      break;
    }
  }
}
int cRecallPos = 0;
void calcRecall(int dir) {                           // step through earlier expressions from the log
  if (!fsOk) return;
  File f = LittleFS.open(FS("/calc.log"), FS("r"));
  if (!f) return;
  int total = 0;
  while (f.available()) { if (f.read() == '\n') total++; }
  cRecallPos = constrain(cRecallPos + dir, 1, max(1, total));
  f.seek(0);
  int want = total - cRecallPos, line = 0;
  while (f.available() && line < want) if (f.read() == '\n') line++;
  String s = f.readStringUntil('\t');
  f.close();
  if (!total) return;
  snprintf_P(cExpr, sizeof cExpr, PSTR("%s"), s.c_str());
  cLen = cCur = strlen(cExpr); cFresh = false;
  snprintf_P(cNote, sizeof cNote, PSTR("history %d/%d"), cRecallPos, total);
}

// ---------- graph list (y1..y4, z) ----------
int cListSel = 0;
const int CL_N = 11, CL_H = 17;
const char CL_ROWS_P[] PROGMEM = "y1 =\ny2 =\ny3 =\ny4 =\nz(x,y) =\nPlot 2D\nPlot 3D\nExamples...\nSave these graphs...\nOpen saved graphs...\nBack to calculator";
void calcListRow(int i) {
  int y = 22 + i * CL_H;
  bool sel = i == cListSel;
  uint16_t acc = calcAccent(), bg = (i & 1) ? E_ROW2 : E_WHITE;
  tft.fillRect(4, y, 312, CL_H, bg);
  if (i < 4) tft.fillRect(8, y + 4, 10, 9, CG_COL[i]);
  if (i == 4) { tft.fillRect(8, y + 4, 5, 9, CG_COL[0]); tft.fillRect(13, y + 4, 5, 9, CG_COL[1]); }
  putText(24, y + 5, 1, i >= 5 ? acc : C_INK, bg, pgmItem(CL_ROWS_P, i));
  if (i < 5) {
    const char* e = i < 4 ? CC.fn[i] : CC.z;
    char b[44]; snprintf_P(b, sizeof b, PSTR("%.36s"), e[0] ? e : FS("(empty - press to type)"));
    putText(84, y + 5, 1, e[0] ? C_INK : C_MID, bg, b);
  }
  if (sel) { tft.drawRect(4, y, 312, CL_H, acc); tft.drawRect(5, y + 1, 310, CL_H - 2, acc); }
}
void calcOpenList(int sel) {
  cPage = CP_LIST; cListSel = constrain(sel, 0, CL_N - 1);
  tft.fillScreen(C_LCD);
  drawTitleBar(FS("Calc: graphs"), calcAccent());
  for (int i = 0; i < CL_N; i++) calcListRow(i);
  putText(6, 222, 1, C_MID, C_LCD, FS("turn: pick   press: edit / plot   hold: back"));
}

// ---------- saved equations (/calc.eq), ready-made examples, saved graph sets (/calc/NAME.gph) ----------
extern double gX0, gY0, gR, g3R;                     // the graph views (defined with the plotters below)
uint16_t cEqOff[12];                                 // where each listed equation starts in WK.calc.log
// "menu label|what goes in the calculator"
const char CEQ_EX_P[] PROGMEM =
  "Quadratic x^2-5x+6=0|x^2-5x+6=0\nPythagoras: 3, 4, ?|sqrt(3^2+4^2)\nCircle area, r = 5|pi*5^2\n"
  "1000 at 5% for 10 years|1000*(1+0.05)^10\nOhm: 20 mA through 220R|0.02*220\nFall time from 20 m (s)|sqrt(2*20/g0)\n"
  "37 C in Fahrenheit|37*9/5+32\nSolve 2^x = 10|2^x=10\nGolden ratio|(1+sqrt(5))/2\nSum of 1 to 100|sigma(x,1,100)\n"
  "Area under x^2, 0 to 3|integ(x^2,0,3)\nSunlight to Earth (min)|au0/c0/60";
// "label|y1|y2|y3|y4|z(x,y)|half-width|centre y|2 or 3 (which plot opens)"
const char CG_EX_P[] PROGMEM =
  "Waves: sin, cos + sum|sin(x)|cos(x)|sin(x)+cos(x)||sin(x)*cos(y)|8|0|2\n"
  "Parabolas|x^2|x^2-4|(x-2)^2|-x^2/2+3|x^2-y^2|6|1|2\n"
  "Lines + slopes|2x+1|-x/2+3|0.5x-2|x|x+y|10|0|2\n"
  "Growth + decay|exp(x)|exp(-x)|2^x|ln(x)|exp(-(x^2+y^2)/4)|5|2|2\n"
  "Circle + ellipse|sqrt(9-x^2)|-sqrt(9-x^2)|sqrt(4-x^2/4)|-sqrt(4-x^2/4)|sqrt(9-x^2-y^2)|5|0|2\n"
  "Damped spring|exp(-x/6)*cos(2x)|exp(-x/6)|-exp(-x/6)||exp(-y/6)*cos(2x)|12|0|2\n"
  "Cubic + its slope|x^3-3x^2-x+3|3x^2-6x-1|||x^3-3x*y^2|4|0|2\n"
  "Heart|sqrt(1-(abs(x)-1)^2)|acos(1-abs(x))-pi|||sin(x)*sin(y)|2.5|-0.8|2\n"
  "Bell curve|exp(-x^2/2)/sqrt(2pi)|exp(-x^2)|||exp(-(x^2+y^2)/2)|4|0.3|2\n"
  "3D: ripple|sin(x)||||sin(sqrt(x^2+y^2))|8|0|3\n"
  "3D: saddle|x^2||||x^2-y^2|3|0|3\n"
  "3D: egg crate|sin(x)||||sin(x)+sin(y)|6|0|3";
bool pgmLine(PGM_P list, int k, char* out, size_t n) {   // k-th line of a flash list, any length up to n - 1
  size_t o = 0;
  for (char c; (c = pgm_read_byte(list)); list++) {
    if (c == '\n') { if (k-- == 0) break; continue; }
    if (k == 0 && o < n - 1) out[o++] = c;
  }
  out[o] = 0;
  return k <= 0 && o > 0;
}
void calcExMenu(int kind) {                          // 2 = equation examples, 3 = graph examples
  char lb[12][30];
  const char* it[12];
  int n = 0;
  for (; n < 12; n++) {
    char line[160];
    if (!pgmLine(kind == 2 ? CEQ_EX_P : CG_EX_P, n, line, sizeof line)) break;
    char* bar = strchr(line, '|'); if (bar) *bar = 0;
    snprintf_P(lb[n], 30, PSTR("%.29s"), line); it[n] = lb[n];
  }
  cPop = kind;
  pmShow(kind == 2 ? FS("Example equations") : FS("Example graphs"), it, n, calcAccent());
}
void calcEqSave() {
  if (!cLen || !fsOk) { snprintf_P(cNote, sizeof cNote, PSTR("type an equation first")); return; }
  File f = LittleFS.open(FS("/calc.eq"), FS("a"));
  if (!f) return;
  f.print(cExpr); f.write('\n');
  size_t sz = f.size();
  f.close();
  if (sz > 3000) {                                   // keep the newest ~2 KB
    File in = LittleFS.open(FS("/calc.eq"), FS("r")), out = LittleFS.open(FS("/calc.eqt"), FS("w"));
    if (in && out) { in.seek(sz - 2000); in.readStringUntil('\n'); uint8_t b[128]; int r; while ((r = in.read(b, sizeof b)) > 0) out.write(b, r); }
    if (in) in.close();
    if (out) out.close();
    LittleFS.remove(FS("/calc.eq")); LittleFS.rename(FS("/calc.eqt"), FS("/calc.eq"));
  }
  snprintf_P(cNote, sizeof cNote, PSTR("saved - MyEqs brings it back"));
  SFX(T_HOME);
}
void calcEqList() {                                  // the newest 12 saved equations, newest first
  char* buf = WK.calc.log;                           // (the history page's buffer; it's reloaded when that page opens)
  buf[0] = 0;
  File f = fsOk ? LittleFS.open(FS("/calc.eq"), FS("r")) : File();
  if (f) {
    size_t sz = f.size();
    if (sz > 2000) { f.seek(sz - 2000); f.readStringUntil('\n'); }
    int r = f.read((uint8_t*)buf, 2047);
    buf[max(0, r)] = 0;
    f.close();
  }
  int starts[64], ns = 0;
  for (int i = 0; buf[i] && ns < 64; ) { starts[ns++] = i; while (buf[i] && buf[i] != '\n') i++; if (buf[i]) buf[i++] = 0; }
  if (!ns) { snprintf_P(cNote, sizeof cNote, PSTR("nothing saved yet: SaveEq first")); return; }
  char lb[12][30];
  const char* it[12];
  int n = 0;
  for (int k = ns - 1; k >= 0 && n < 12; k--) {
    cEqOff[n] = starts[k];
    snprintf_P(lb[n], 30, PSTR("%.29s"), buf + starts[k]); it[n] = lb[n]; n++;
  }
  cPop = 1;
  pmShow(FS("My equations"), it, n, calcAccent());
}
void calcSetExpr(const char* e, const char* note) {
  snprintf_P(cExpr, sizeof cExpr, PSTR("%s"), e);
  cLen = cCur = strlen(cExpr); cFresh = false; cErrShown = false;
  strcpy_P(cShow, PSTR("")); snprintf_P(cNote, sizeof cNote, PSTR("%s"), note);
}
void calcGraphExample(int k) {
  char line[200];
  if (!pgmLine(CG_EX_P, k, line, sizeof line)) return;
  char* fld[9];
  int nf = 0;
  fld[nf++] = line;
  for (char* p = line; *p && nf < 9; p++) if (*p == '|') { *p = 0; fld[nf++] = p + 1; }
  if (nf < 9) return;
  for (int i = 0; i < 4; i++) snprintf_P(CC.fn[i], 60, PSTR("%s"), fld[1 + i]);
  snprintf_P(CC.z, 60, PSTR("%s"), fld[5]);
  gR = atof(fld[6]); gX0 = 0; gY0 = atof(fld[7]); g3R = gR;
  CC.deg = 0;                                        // the examples are in radians
  calcSaveCfg();
  if (fld[8][0] == '3') g3Open(); else gOpen();
}
struct GraphFile { char tag[4]; char fn[4][60]; char z[60]; double x0, y0, r, r3; };
void calcGraphSave(const char* name) {
  char nm[16], p[40];
  cleanName(name, nm);
  if (!nm[0] || !fsOk) return;
  snprintf_P(p, sizeof p, PSTR("/calc/%s.gph"), nm);
  if (!LittleFS.exists(FS("/calc"))) LittleFS.mkdir(FS("/calc"));
  GraphFile g;
  memset(&g, 0, sizeof g);
  memcpy_P(g.tag, PSTR("GPH1"), 4);
  memcpy(g.fn, CC.fn, sizeof g.fn); memcpy(g.z, CC.z, sizeof g.z);
  g.x0 = gX0; g.y0 = gY0; g.r = gR; g.r3 = g3R;
  File f = LittleFS.open(p, FS("w"));
  if (f) { f.write((const uint8_t*)&g, sizeof g); f.close(); SFX(T_HOME); }
}
bool calcGraphLoad(const char* path) {
  File f = LittleFS.open(path, FS("r"));
  if (!f) return false;
  GraphFile g;
  bool ok = f.read((uint8_t*)&g, sizeof g) == (int)sizeof g && !memcmp_P(g.tag, PSTR("GPH1"), 4);
  f.close();
  if (!ok) return false;
  memcpy(CC.fn, g.fn, sizeof g.fn); memcpy(CC.z, g.z, sizeof g.z);
  for (int i = 0; i < 4; i++) CC.fn[i][59] = 0;
  CC.z[59] = 0;
  gX0 = g.x0; gY0 = g.y0; gR = constrain(g.r, 1e-4, 1e6); g3R = constrain(g.r3, 0.1, 1000.0);
  calcSaveCfg();
  return true;
}
void calcPopDone(int r) {                            // a popup list item was picked
  uint8_t kind = cPop;
  cPop = 0;
  if (kind == 1) {
    if (r >= 0 && r < 12) calcSetExpr(WK.calc.log + cEqOff[r], FS("= works it out, SOLVE finds x"));
    cPage = CP_CALC; calcDrawAll();
  } else if (kind == 2) {
    char line[160];
    if (pgmLine(CEQ_EX_P, r, line, sizeof line)) { char* bar = strchr(line, '|'); calcSetExpr(bar ? bar + 1 : line, strchr(bar ? bar + 1 : line, '=') ? FS("press = to solve for x") : FS("press = to work it out")); }
    cPage = CP_CALC; calcDrawAll();
  } else calcGraphExample(r);
}

// ---------- 2D grapher ----------
double gX0 = 0, gY0 = 0, gR = 10;                   // view centre and half-width
int gMode = 0, gFn = 0, gTx = 160;                  // 0 zoom 1 pan x 2 pan y 3 style 4 trace 5 roots
uint32_t gFine = 0;                                  // a quick preview is on screen: draw it properly when the knob rests
const int G_TOP = 18, G_BOT = 222;
double gSx() { return W / (2 * gR); }
double gYr() { return gR * (G_BOT - G_TOP) / W; }
int gPx(double x) { return W / 2 + (int)lround((x - gX0) * gSx()); }
int gPy(double y) { return (G_TOP + G_BOT) / 2 - (int)lround((y - gY0) * gSx()); }
double gXof(int px) { return gX0 + (px - W / 2) / gSx(); }
void gStatus(const char* s) {
  tft.fillRect(0, G_BOT, W, H - G_BOT, C_LCD);
  putText(4, G_BOT + 5, 1, C_INK, C_LCD, s);
}
void gModeHint() {
  char b[60];
  if (gMode == 3) { snprintf_P(b, sizeof b, PSTR("STYLE: %s (turn)  press: next"), pgmItem(PSTR("lines\ndots\nfilled area\nthin lines"), P.g2Style % 4)); gStatus(b); }
  else if (gMode < 3) { snprintf_P(b, sizeof b, PSTR("%s   press: next mode"), pgmItem(PSTR("ZOOM: turn = in/out\nPAN X: turn = left/right\nPAN Y: turn = up/down"), gMode)); gStatus(b); }
}
void gDrawQ(int q);
void gDraw() { gDrawQ(1); }
void gDrawQ(int q) {                                 // q > 1: every q-th column only (a quick preview while turning)
  calcEvalSetup();
  tft.fillRect(0, G_TOP, W, G_BOT - G_TOP, E_WHITE);
  double step = pow(10, floor(log10(gR)));
  if (gR / step > 5) step *= 2;
  for (double g = floor((gX0 - gR) / step) * step; g <= gX0 + gR; g += step) tft.drawFastVLine(gPx(g), G_TOP, G_BOT - G_TOP, E_HEAD);
  for (double g = floor((gY0 - gYr()) / step) * step; g <= gY0 + gYr(); g += step) { int y = gPy(g); if (y > G_TOP && y < G_BOT) tft.drawFastHLine(0, y, W, E_HEAD); }
  int ax = gPx(0), ay = gPy(0);
  if (ax >= 0 && ax < W) tft.drawFastVLine(ax, G_TOP, G_BOT - G_TOP, C_MID);
  if (ay > G_TOP && ay < G_BOT) tft.drawFastHLine(0, ay, W, C_MID);
  char b[16];
  fmtNum(step, b, sizeof b, 3);
  putText(W - 6 - (int)strlen(b) * 6 - 18, G_TOP + 3, 1, C_MID, E_WHITE, FS("grid"));
  putText(W - 4 - (int)strlen(b) * 6, G_TOP + 3, 1, C_MID, E_WHITE, b);
  int st = P.g2Style % 4, axY = constrain(ay, G_TOP, G_BOT - 1);
  if (st == 1 && q == 1) q = 3;                // dots: every third column
  for (int f = 0; f < 4; f++) {
    if (!CC.fn[f][0]) continue;
    int ly = -9999;
    uint16_t col = CG_COL[f], tint = mix565(col, E_WHITE, 175);
    for (int px = 0; px < W; px += q) {
      double y = calcAt(CC.fn[f], gXof(px), CC.y);
      int py = gPy(y);
      bool ok = !isnan(y) && !isinf(y) && py > G_TOP - 300 && py < G_BOT + 300;
      int y1 = constrain(py, G_TOP, G_BOT - 1);
      if (ok && st == 2) {                           // filled area: from the x axis to the curve
        int a = min(axY, y1), b = max(axY, y1);
        tft.drawFastVLine(px, a, b - a + 1, tint);
        if (q > 1) tft.fillRect(px, a, q, b - a + 1, tint);
      }
      if (ok && st == 1) tft.fillRect(px - 1, y1 - 1, 3, 3, col);
      else if (ok && ly != -9999 && abs(py - ly) < (G_BOT - G_TOP)) {
        int y0 = constrain(ly, G_TOP, G_BOT - 1);
        tft.drawLine(px - q, y0, px, y1, col);
        if (st != 3) tft.drawLine(px - q, y0 + 1, px, y1 + 1, col);
      }
      ly = ok ? py : -9999;
      if (!(px & 31)) yield();
    }
    char l[24]; snprintf_P(l, sizeof l, PSTR("y%d=%.16s"), f + 1, CC.fn[f]);
    putText(4, G_TOP + 3 + f * 10, 1, CG_COL[f], E_WHITE, l);
  }
  calcEvalDone();
}
void gTrace() {
  calcEvalSetup();
  double x = gXof(gTx), y = calcAt(CC.fn[gFn], x, CC.y);
  double h = 1e-5 * max(1.0, fabs(x)), d = (calcAt(CC.fn[gFn], x + h, CC.y) - calcAt(CC.fn[gFn], x - h, CC.y)) / (2 * h);
  calcEvalDone();
  int py = gPy(y);
  tft.drawFastVLine(gTx, G_TOP, G_BOT - G_TOP, E_RULE);
  if (!isnan(y) && py > G_TOP && py < G_BOT) { tft.drawCircle(gTx, py, 5, CG_COL[gFn]); tft.fillCircle(gTx, py, 2, C_INK); }
  char a[14], bb[14], c[14], s[60];
  fmtNum(x, a, sizeof a, 5); fmtNum(y, bb, sizeof bb, 6); fmtNum(d, c, sizeof c, 4);
  snprintf_P(s, sizeof s, PSTR("y%d x=%s y=%s dy/dx=%s"), gFn + 1, a, bb, c);
  gStatus(s);
}
void gRoots() {                                      // zero crossings of every function in view
  calcEvalSetup();
  char s[60] = "roots:";
  int found = 0;
  for (int f = 0; f < 4; f++) {
    if (!CC.fn[f][0]) continue;
    double px0 = calcAt(CC.fn[f], gXof(0), CC.y);
    for (int px = 1; px < W; px++) {
      double v = calcAt(CC.fn[f], gXof(px), CC.y);
      if (!isnan(v) && !isnan(px0) && (v <= 0) != (px0 <= 0) && fabs(v - px0) < gYr()) {
        double a = gXof(px - 1), b = gXof(px), fa = px0;
        for (int k = 0; k < 50; k++) { double m = (a + b) / 2, fm = calcAt(CC.fn[f], m, CC.y); if ((fa <= 0) == (fm <= 0)) { a = m; fa = fm; } else b = m; }
        double r = (a + b) / 2;
        tft.fillCircle(gPx(r), gPy(0), 4, CG_COL[f]); tft.drawCircle(gPx(r), gPy(0), 6, C_INK);
        if (found < 3) { char t[14]; fmtNum(r, t, sizeof t, 5); int n = strlen(s); snprintf_P(s + n, sizeof s - n, PSTR(" %s"), t); }
        found++;
      }
      px0 = v;
      if (!(px & 31)) yield();
    }
  }
  calcEvalDone();
  if (!found) strcpy_P(s, PSTR("no roots in view (zoom out?)"));
  else if (found > 3) { int n = strlen(s); snprintf_P(s + n, sizeof s - n, PSTR(" +%d"), found - 3); }
  gStatus(s);
}
void gOpen() {
  cPage = CP_2D; gMode = 0;
  tft.fillScreen(C_LCD);
  drawTitleBar(FS("Calc: 2D graph"), calcAccent());
  gDraw(); gModeHint();
}

// ---------- 3D surface z = f(x, y) ----------
// The surface is worked out once on a 21 x 21 grid and kept: turning, tilting and zooming only re-project it (fast).
// Looks (STYLE mode): mesh, solid (shaded by a light), heat map (colour = height), heat map + mesh. While the knob
// turns a quick mesh shows; the chosen look is drawn when it rests.
int g3Yaw = 35, g3Pitch = 30, g3Mode = 0;           // 0 rotate 1 tilt 2 zoom 3 domain 4 style
double g3Zoom = 1.0, g3R = 5;
const int G3N = 21;
uint16_t* g3Z = nullptr;                             // heights: 0..65534 = zmin..zmax, 65535 = no value
int16_t* g3P = nullptr;                              // the projected points (x, y) of the grid
uint32_t g3Key = 0, g3At = 0;
float g3Lo = 0, g3Hi = 1;
const char* g3Err = nullptr;
void g3Hint() {
  char b[60];
  if (g3Mode == 4) snprintf_P(b, sizeof b, PSTR("STYLE: %s (turn)   press: next"), pgmItem(PSTR("mesh\nsolid\nheat map\nheat map + mesh"), P.g3Style % 4));
  else snprintf_P(b, sizeof b, PSTR("%s: turn   press: next   x,y in +-%g"), pgmItem(PSTR("ROTATE\nTILT\nZOOM\nDOMAIN"), g3Mode), g3R);
  tft.fillRect(0, 226, W, 14, C_LCD);
  putText(4, 229, 1, C_INK, C_LCD, b);
}
uint32_t g3Hash() {
  uint32_t h = 2166136261u;
  for (const char* q = CC.z; *q; q++) h = (h ^ (uint8_t)*q) * 16777619u;
  float r = g3R; uint32_t ri; memcpy(&ri, &r, 4);
  return (h ^ ri) * 16777619u + CC.deg;
}
bool g3Compute() {                                   // the heights (only when the function or the domain changed)
  if (!g3Z) { g3Z = (uint16_t*)malloc(G3N * G3N * 2); g3P = (int16_t*)malloc(G3N * G3N * 4); g3Key = 0; }
  if (!g3Z || !g3P) { free(g3Z); free(g3P); g3Z = nullptr; g3P = nullptr; return false; }
  uint32_t k = g3Hash();
  if (k == g3Key) return true;
  float* raw = (float*)g3P;                          // (the points' memory holds the raw values meanwhile)
  calcEvalSetup();
  g3Lo = INFINITY; g3Hi = -INFINITY;
  for (int i = 0; i < G3N; i++) {
    for (int j = 0; j < G3N; j++) {
      double z = calcAt(CC.z, -g3R + 2 * g3R * i / (G3N - 1), -g3R + 2 * g3R * j / (G3N - 1));
      bool ok = !isnan(z) && !isinf(z);
      raw[i * G3N + j] = ok ? (float)z : NAN;
      if (ok) { g3Lo = min(g3Lo, (float)z); g3Hi = max(g3Hi, (float)z); }
    }
    yield();
  }
  g3Err = exErr;
  calcEvalDone();
  if (g3Err || g3Lo > g3Hi) { g3Key = 0; return true; }
  if (g3Hi - g3Lo < 1e-9f) { g3Hi += 1; g3Lo -= 1; }
  for (int k2 = 0; k2 < G3N * G3N; k2++) g3Z[k2] = isnan(raw[k2]) ? 65535 : (uint16_t)((raw[k2] - g3Lo) / (g3Hi - g3Lo) * 65534);
  g3Key = k;
  return true;
}
uint16_t g3Heat(int t) {                             // 0..255: blue, cyan, green, yellow, red
  if (t < 64) return pk565(0, t * 4, 255);
  if (t < 128) return pk565(0, 255, 255 - (t - 64) * 4);
  if (t < 192) return pk565((t - 128) * 4, 255, 0);
  return pk565(255, 255 - (t - 192) * 4, 0);
}
uint16_t g3Shade(uint16_t c, float k) { return pk565((int)(((c >> 11) & 31) * 8 * k), (int)(((c >> 5) & 63) * 4 * k), (int)((c & 31) * 8 * k)); }
void g3Draw(bool fine) {
  tft.fillRect(0, 18, W, 208, E_WHITE);
  if (!g3Compute()) { putText(10, 40, 1, E_CORAL, E_WHITE, FS("not enough free memory for 3D")); return; }
  if (g3Err || !g3Key) { putText(10, 40, 1, E_CORAL, E_WHITE, g3Err ? g3Err : FS("nothing to draw")); return; }
  float ca = cos(g3Yaw * 0.01745f), sa = sin(g3Yaw * 0.01745f), cp = cos(g3Pitch * 0.01745f), sp = sin(g3Pitch * 0.01745f);
  float S = 70 * g3Zoom;
  int cx = 160, cy = 126;
  auto proj = [&](float u, float v, float w, int* px, int* py) {
    float ur = u * ca - v * sa, vr = u * sa + v * ca;
    *px = cx + (int)(ur * S);
    *py = cy - (int)((w * cp + vr * sp) * S);
  };
  auto wOf = [&](int k) { return g3Z[k] == 65535 ? -0.8f : g3Z[k] / 65534.0f * 1.6f - 0.8f; };
  for (int i = 0; i < G3N; i++) for (int j = 0; j < G3N; j++) {
    int k = i * G3N + j, px, py;
    proj(-1 + 2.0f * i / (G3N - 1), -1 + 2.0f * j / (G3N - 1), wOf(k), &px, &py);
    g3P[2 * k] = px; g3P[2 * k + 1] = py;
  }
  int ax, ay, bx, by;                                // axes
  proj(-1.2f, 0, -0.8f, &ax, &ay); proj(1.2f, 0, -0.8f, &bx, &by); tft.drawLine(ax, ay, bx, by, CG_COL[1]); putText(bx + 2, by - 4, 1, CG_COL[1], E_WHITE, FS("x"));
  proj(0, -1.2f, -0.8f, &ax, &ay); proj(0, 1.2f, -0.8f, &bx, &by); tft.drawLine(ax, ay, bx, by, CG_COL[2]); putText(bx + 2, by - 4, 1, CG_COL[2], E_WHITE, FS("y"));
  proj(0, 0, -0.8f, &ax, &ay); proj(0, 0, 1.0f, &bx, &by); tft.drawLine(ax, ay, bx, by, CG_COL[0]); putText(bx + 2, by - 8, 1, CG_COL[0], E_WHITE, FS("z"));
  int st = fine ? P.g3Style % 4 : 0;
  auto X = [&](int i, int j) { return (int)g3P[2 * (i * G3N + j)]; };
  auto Y = [&](int i, int j) { return (int)g3P[2 * (i * G3N + j) + 1]; };
  if (st == 0) {                                     // mesh, coloured by height
    for (int i = 0; i < G3N; i++) for (int j = 0; j < G3N; j++) {
      int t = constrain((int)((wOf(i * G3N + j) + 0.8f) / 1.6f * 255), 0, 255);
      uint16_t col = pk565(t, 80 + t / 3, 255 - t);
      if (j) tft.drawLine(X(i, j - 1), Y(i, j - 1), X(i, j), Y(i, j), col);
      if (i) tft.drawLine(X(i - 1, j), Y(i - 1, j), X(i, j), Y(i, j), col);
    }
  } else {                                           // filled quads, far ones first (painter's order)
    const float L[3] = {-0.42f, -0.52f, 0.74f};      // the light (unit vector)
    float d = 2.0f / (G3N - 1);
    for (int a = 0; a < G3N - 1; a++) {
      int i = sa > 0 ? G3N - 2 - a : a;
      for (int b2 = 0; b2 < G3N - 1; b2++) {
        int j = ca > 0 ? G3N - 2 - b2 : b2;
        int k00 = i * G3N + j, k10 = k00 + G3N, k01 = k00 + 1, k11 = k10 + 1;
        if (g3Z[k00] == 65535 || g3Z[k10] == 65535 || g3Z[k01] == 65535 || g3Z[k11] == 65535) continue;
        float w00 = wOf(k00), w10 = wOf(k10), w01 = wOf(k01), w11 = wOf(k11);
        float du = (w10 + w11 - w00 - w01) / (2 * d), dv = (w01 + w11 - w00 - w10) / (2 * d);
        float nl = sqrtf(du * du + dv * dv + 1);
        float lit = max(0.0f, (-du * L[0] - dv * L[1] + L[2]) / nl);
        int t = constrain((int)(((w00 + w10 + w01 + w11) / 4 + 0.8f) / 1.6f * 255), 0, 255);
        uint16_t col = st == 1 ? g3Shade(CG_COL[0], 0.3f + 0.7f * lit) : g3Shade(g3Heat(t), 0.55f + 0.45f * lit);
        tft.fillTriangle(X(i, j), Y(i, j), X(i + 1, j), Y(i + 1, j), X(i + 1, j + 1), Y(i + 1, j + 1), col);
        tft.fillTriangle(X(i, j), Y(i, j), X(i + 1, j + 1), Y(i + 1, j + 1), X(i, j + 1), Y(i, j + 1), col);
        if (st == 3) {
          uint16_t e = g3Shade(col, 0.55f);
          tft.drawLine(X(i, j), Y(i, j), X(i + 1, j), Y(i + 1, j), e);
          tft.drawLine(X(i, j), Y(i, j), X(i, j + 1), Y(i, j + 1), e);
        }
      }
      yield();
    }
  }
  char bb[60], lo[12], hi[12];
  fmtNum(g3Lo, lo, sizeof lo, 4); fmtNum(g3Hi, hi, sizeof hi, 4);
  snprintf_P(bb, sizeof bb, PSTR("z=%.24s"), CC.z); putText(4, 21, 1, C_INK, E_WHITE, bb);
  snprintf_P(bb, sizeof bb, PSTR("z: %s .. %s"), lo, hi); putText(4, 32, 1, C_MID, E_WHITE, bb);
}
void g3Open() {
  cPage = CP_3D;
  tft.fillScreen(C_LCD);
  drawTitleBar(FS("Calc: 3D surface"), calcAccent());
  if (!CC.z[0]) { cEditFn = 4; cPage = CP_CALC; strcpy_P(cExpr, PSTR("")); cLen = cCur = 0; calcDrawAll(); return; }
  g3Draw(true); g3Hint();
}

// ---------- history log page ----------
void calcLogLoad() {                                 // newest ~2 KB of /calc.log into the shared buffer
  wkOwner = APP_CALC;
  WK.calc.log[0] = 0; cLogN = 0;
  if (!fsOk) return;
  File f = LittleFS.open(FS("/calc.log"), FS("r"));
  if (!f) return;
  size_t sz = f.size();
  if (sz > 2000) { f.seek(sz - 2000); f.readStringUntil('\n'); }
  int n = f.read((uint8_t*)WK.calc.log, 2047);
  f.close();
  WK.calc.log[max(0, n)] = 0;
  for (char* p = WK.calc.log; *p; p++) if (*p == '\n') cLogN++;
}
const char* calcLogLine(int i, char* e, size_t en, char* r, size_t rn) {   // i = 0 oldest
  const char* p = WK.calc.log;
  for (int k = 0; k < i && p; k++) { p = strchr(p, '\n'); if (p) p++; }
  if (!p || !*p) return nullptr;
  const char* tab = strchr(p, '\t');
  const char* nl = strchr(p, '\n');
  if (!tab || !nl || tab > nl) return nullptr;
  snprintf_P(e, en, PSTR("%.*s"), (int)min((long)(en - 1), (long)(tab - p)), p);
  snprintf_P(r, rn, PSTR("%.*s"), (int)min((long)(rn - 1), (long)(nl - tab - 1)), tab + 1);
  return p;
}
void calcLogRow(int s) {
  int i = cLogTop + s, y = 22 + s * 22;
  ptrListSet(PL_CALCLOG, 4, 22, 312, 22, 8, cLogTop, cLogSel);
  uint16_t bg = (s & 1) ? E_ROW2 : E_WHITE, acc = calcAccent();
  tft.fillRect(4, y, 312, 22, bg);
  char e[60], r[30];
  if (i >= cLogN || !calcLogLine(i, e, sizeof e, r, sizeof r)) return;
  char b[40]; snprintf_P(b, sizeof b, PSTR("%.34s"), e);
  putText(10, y + 7, 1, C_INK, bg, b);
  putText(310 - (int)strlen(r) * 6, y + 7, 1, acc, bg, r);
  if (i == cLogSel) { tft.drawRect(4, y, 312, 22, acc); tft.drawRect(5, y + 1, 310, 20, acc); }
}
void calcOpenLog() {
  cPage = CP_LOG;
  calcLogLoad();
  cLogSel = max(0, cLogN - 1); cLogTop = max(0, cLogN - 8);
  tft.fillScreen(C_LCD);
  drawTitleBar(FS("Calc: history"), calcAccent());
  for (int s = 0; s < 8; s++) calcLogRow(s);
  putText(6, 204, 1, C_MID, C_LCD, cLogN ? FS("turn: scroll   press: reuse   hold: back") : FS("no history yet - results land here"));
}

// ---------- app ----------
void calcBack() {                                    // hold: one page back
  if (cPop || cAsk || cPick) {                       // a popup, the name box or the file list: just close it
    cPop = 0; cAsk = false; cPick = false; pmOpen = false; fpOpen = false;
    if (cPage == CP_LIST) calcOpenList(cListSel); else { cPage = CP_CALC; calcDrawAll(); }
    return;
  }
  if (cPage == CP_CALC && cEditFn >= 0) { int was = cEditFn; cEditFn = -1; calcOpenList(was); return; }
  if (cPage == CP_2D || cPage == CP_3D) { calcOpenList(cPage == CP_2D ? 5 : 6); return; }
  cPage = CP_CALC; calcDrawAll();
}
bool calcWantsBack() { return cPage != CP_CALC || cEditFn >= 0 || cPop || cAsk || cPick; }
void calcEnter() {
  if (wkOwner != APP_CALC) calcLoadCfg();            // settings + memories live in the shared area
  wkOwner = APP_CALC;
  cPage = CP_CALC; cEditFn = -1;
  calcDrawAll();
}
void calcTick(uint32_t now) {
  int c = takeClicks() * ENC_DIR;
  takePress();
  bool sp = takeShort();
  if (cPop) { int r = pmTick(c, sp); if (r >= 0) calcPopDone(r); return; }
  if (cAsk) {
    int r = askTick(c, sp);
    if (!r) return;
    cAsk = false;
    if (r > 0 && askBuf[0]) { calcGraphSave(askBuf); snprintf_P(cNote, sizeof cNote, PSTR("graphs saved: OpenGr")); }
    calcOpenList(cPage == CP_LIST ? 8 : 0);
    return;
  }
  if (cPick) {
    int r = fpTick(c, sp);
    if (r == FP_BUSY) return;
    cPick = false;
    if (r >= 0 && calcGraphLoad(fpPath)) { gOpen(); return; }
    calcOpenList(9);
    return;
  }
  if (cPage == CP_CALC)                              // typed on the PC (remote control): straight into the line
    for (char rc; (rc = kbRemotePop()); ) {
      if (rc == '\b') calcBackspace();
      else if (rc == '\n') { calcEquals(false); }
      else if (rc == 27) { cLen = cCur = 0; cExpr[0] = 0; }
      else { char s[2] = {rc, 0}; calcInsert(s); }
      calcDrawDisplay(); oledDirty = true;
    }
  switch (cPage) {
    case CP_LIST:
      if (c) { int old = cListSel; cListSel = ((cListSel + c) % CL_N + CL_N) % CL_N; calcListRow(old); calcListRow(cListSel); uiClick(); }
      if (sp) {
        if (cListSel < 5) {                          // edit a function with the full keypad
          cEditFn = cListSel;
          snprintf_P(cExpr, sizeof cExpr, PSTR("%s"), cListSel < 4 ? CC.fn[cListSel] : CC.z);
          cLen = cCur = strlen(cExpr); cFresh = false; cPage = CP_CALC;
          strcpy_P(cShow, PSTR("")); snprintf_P(cNote, sizeof cNote, PSTR("press = to save"));
          calcDrawAll();
        } else if (cListSel == 5) gOpen();
        else if (cListSel == 6) g3Open();
        else if (cListSel == 7) calcExMenu(3);
        else if (cListSel == 8) { cAsk = true; askBegin(FS("Save these graphs as (name):"), FS(""), 12, calcAccent()); }
        else if (cListSel == 9) { cPick = true; fpBegin(FS("Open saved graphs"), FS("/calc"), FS(".gph"), nullptr, calcAccent()); }
        else { cPage = CP_CALC; calcDrawAll(); }
      }
      return;
    case CP_2D:
      if (c) {                                       // zoom / pan: a quick preview now, the full curve when the knob rests
        if (gMode == 0) { gR = constrain(gR * pow(1.25, -c), 1e-4, 1e6); gDrawQ(4); gFine = millis(); gModeHint(); }
        else if (gMode == 1) { gX0 += c * gR / 8; gDrawQ(4); gFine = millis(); gModeHint(); }
        else if (gMode == 2) { gY0 += c * gR / 8; gDrawQ(4); gFine = millis(); gModeHint(); }
        else if (gMode == 3) { P.g2Style = (P.g2Style + (c > 0 ? 1 : 3)) % 4; prefsSave(); gDraw(); gModeHint(); }
        else if (gMode == 4) { int old = gTx; gTx = constrain(gTx + c * 3, 0, W - 1); tft.drawFastVLine(old, G_TOP, G_BOT - G_TOP, E_WHITE); gDraw(); gTrace(); }
      } else if (gFine && millis() - gFine > 250) { gFine = 0; gDraw(); gModeHint(); }
      if (sp) {
        gFine = 0;
        if (gMode == 4) {                            // trace: press goes to the next function, then roots
          int f = gFn + 1;
          while (f < 4 && !CC.fn[f][0]) f++;
          if (f < 4) { gFn = f; gDraw(); gTrace(); return; }
          gMode = 5; gDraw(); gRoots();
          return;
        }
        gMode = (gMode + 1) % 6;
        if (gMode == 4) { gFn = 0; while (gFn < 3 && !CC.fn[gFn][0]) gFn++; gTx = 160; gDraw(); gTrace(); }
        else if (gMode == 5) { gDraw(); gRoots(); }
        else { gDraw(); gModeHint(); }
      }
      return;
    case CP_3D:
      if (c) {
        if (g3Mode == 0) g3Yaw = (g3Yaw + c * 10 + 360) % 360;
        else if (g3Mode == 1) g3Pitch = constrain(g3Pitch + c * 5, -10, 90);
        else if (g3Mode == 2) g3Zoom = constrain(g3Zoom * pow(1.15, c), 0.3, 4.0);
        else if (g3Mode == 3) g3R = constrain(g3R * pow(1.25, c), 0.1, 1000.0);
        else { P.g3Style = (P.g3Style + (c > 0 ? 1 : 3)) % 4; prefsSave(); }
        g3Draw(g3Mode == 4); g3Hint();
        g3At = g3Mode == 4 ? 0 : millis();           // the chosen look follows when the knob rests
      } else if (g3At && millis() - g3At > 300) { g3At = 0; if (P.g3Style % 4) { g3Draw(true); g3Hint(); } }
      if (sp) { g3Mode = (g3Mode + 1) % 5; g3Hint(); }
      return;
    case CP_LOG:
      if (c && cLogN) {
        int old = cLogSel, oldTop = cLogTop;
        cLogSel = constrain(cLogSel + c, 0, cLogN - 1);
        if (cLogSel < cLogTop) cLogTop = cLogSel; else if (cLogSel >= cLogTop + 8) cLogTop = cLogSel - 7;
        if (oldTop != cLogTop) for (int s = 0; s < 8; s++) calcLogRow(s);
        else { calcLogRow(old - cLogTop); calcLogRow(cLogSel - cLogTop); }
        uiClick();
      }
      if (sp && cLogN) {
        char e[128], r[30];
        if (calcLogLine(cLogSel, e, sizeof e, r, sizeof r)) {
          snprintf_P(cExpr, sizeof cExpr, PSTR("%s"), e);
          cLen = cCur = strlen(cExpr); cFresh = false;
          snprintf_P(cShow, sizeof cShow, PSTR("%s"), r); cNote[0] = 0;
        }
        cPage = CP_CALC; calcDrawAll();
      }
      return;
  }
  // the calculator: tabs 0..8 then keys 9..40
  if (c) {
    int old = ckFocus, n = CK_NT + ckCount();
    ckFocus = ((ckFocus + c) % n + n) % n;
    if (old < CK_NT) calcDrawTab(old); else calcDrawKey(old - CK_NT);
    if (ckFocus < CK_NT) calcDrawTab(ckFocus); else calcDrawKey(ckFocus - CK_NT);
    uiClick(); oledDirty = true;
  }
  if (!sp) return;
  SFX(T_HOP);
  if (ckFocus < CK_NT) {
    if (ckFocus < CK_KEYTABS) { int old = ckTab; ckTab = ckFocus; calcDrawTab(old); calcDrawTab(ckTab); tft.fillRect(0, 113, W, 127, C_LCD); calcDrawKeys(); }
    else if (ckFocus == CK_KEYTABS) calcOpenList(5);
    else if (ckFocus == CK_KEYTABS + 1) g3Open();
    else calcOpenLog();
    oledDirty = true;
    return;
  }
  const char* ins = ckInsert(ckFocus - CK_NT);
  char a[8];
  snprintf_P(a, sizeof a, PSTR("%s"), ins);
  cRecallPos = a[1] == 'p' || a[1] == 'n' ? cRecallPos : 0;
  if (a[0] == '@') calcAction(a, now);
  else calcInsert(a);
  if (cPage != CP_CALC || cPop || cAsk || cPick) return;   // a page, popup or name box took over the screen
  if (ckTab == CK_MODE) { calcDrawKeys(); }
  calcDrawDisplay();
  oledDirty = true;
}
void oledCalc() {
  char l[4][24];
  snprintf_P(l[0], 24, PSTR("%.21s"), cLen ? cExpr : FS("(empty)"));
  snprintf_P(l[1], 24, PSTR("= %.19s"), cShow);
  snprintf_P(l[2], 24, PSTR("%s  %s"), pgmItem(PSTR("calculator\ngraph list\n2D graph\n3D surface\nhistory"), cPage), CC.deg ? FS("DEG") : FS("RAD"));
  if (cPage == CP_CALC && ckFocus >= CK_NT) snprintf_P(l[3], 24, PSTR("key: %.16s"), ckLabel(ckFocus - CK_NT));
  else snprintf_P(l[3], 24, PSTR("Ans %.16s"), cShow);
  oledCard(FS("CALC"), l[0], l[1], l[2], l[3]);
}

// ================= WIN95 LOOK (Word + Excel) =================
// Classic grey 3D chrome: navy title bar, a menu bar with drop-downs, a toolbar and a status bar.
// One knob drives it: press opens the bar, turn walks the menu titles and then the toolbar buttons,
// press opens a menu (or clicks the button), turn + press picks an item, hold closes. Menus are
// newline lists in flash; a "-" line is a separator.
// Two looks (Settings > Look > Office look): modern 365 (flat, the app's colour on the title + status bars, light
// ribbon) or classic 95 (grey bevels, navy). w95Look() sets the palette; the drawing below asks w95New.
uint16_t W95_FACE = RGB565(192, 192, 192), W95_SH = RGB565(128, 128, 128), W95_DK = 0x0000, W95_NAVY = RGB565(0, 0, 128),
         W95_DESK = RGB565(128, 128, 128);
const uint16_t W95_HI = 0xFFFF, W95_PAPER = 0xFFFF, W95_INK = 0x0000,
               W95_YEL = RGB565(255, 255, 0), W95_RED = RGB565(192, 0, 0), W95_GRN = RGB565(0, 128, 0);
bool w95New = true;
uint16_t w95Acc = RGB565(43, 87, 154);               // the app's colour (Word blue, Excel green)
uint16_t w95Tint(uint16_t c, int t) {                // mix with white: t 0 (c) .. 255 (white)
  int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
  return ((r + (31 - r) * t / 255) << 11) | ((g + (63 - g) * t / 255) << 5) | (b + (31 - b) * t / 255);
}
void w95Look(uint16_t accent) {
  w95New = !P.officeLook; w95Acc = accent;
  if (w95New) { W95_FACE = RGB565(243, 243, 243); W95_SH = RGB565(200, 200, 200); W95_DK = RGB565(176, 176, 176); W95_DESK = RGB565(226, 226, 226); W95_NAVY = accent; }
  else { W95_FACE = RGB565(192, 192, 192); W95_SH = RGB565(128, 128, 128); W95_DK = 0x0000; W95_DESK = RGB565(128, 128, 128); W95_NAVY = RGB565(0, 0, 128); }
}
enum { IC_TYPE, IC_NEW, IC_OPEN, IC_SAVE, IC_PREVIEW, IC_B, IC_I, IC_U, IC_LEFT, IC_CENTER, IC_RIGHT, IC_BULLET, IC_NUMBER,
       IC_TABLE, IC_CHART, IC_SUM, IC_FX, IC_SORTA, IC_SORTZ, IC_CUR, IC_PCT, IC_COMMA, IC_UNDO, IC_FIND, IC_COPY, IC_PASTE };
int w95Mode = 0;                                     // 0 = off, 1 = bar has focus, 2 = a menu is open
int w95Focus = 0, w95Item = 0, w95NM = 0, w95NT = 0;
int w95MenuX[9];
int16_t w95ToolX[16];
uint8_t w95ToolIc[16];                              // toolbar: icon of each button (gaps removed)
PGM_P w95Titles = nullptr;
const char* const* w95Menus = nullptr;               // one flash list per menu
const uint8_t* w95Tools = nullptr;
void (*w95Redraw)() = nullptr;                       // the app's full repaint (after a menu closes)
bool (*w95On)(int cmd) = nullptr;                    // check marks / pressed buttons (cmd = menu*32+item, 1000+tool)

void w95Bevel(int x, int y, int w, int h, bool raised) {
  if (w95New) {                                      // flat: a hairline (buttons, headers) or a frame (fields)
    if (raised) { tft.drawFastHLine(x, y + h - 1, w, W95_SH); tft.drawFastVLine(x + w - 1, y, h, W95_SH); }
    else tft.drawRect(x, y, w, h, W95_SH);
    return;
  }
  uint16_t a = raised ? W95_HI : W95_SH, b = raised ? W95_DK : W95_HI;
  tft.drawFastHLine(x, y, w, a); tft.drawFastVLine(x, y, h, a);
  tft.drawFastHLine(x, y + h - 1, w, b); tft.drawFastVLine(x + w - 1, y, h, b);
  if (raised && w > 2 && h > 2) { tft.drawFastHLine(x + 1, y + h - 2, w - 2, W95_SH); tft.drawFastVLine(x + w - 2, y + 1, h - 2, W95_SH); }
}
void w95Field(int x, int y, int w, int h) {         // sunken white box (edit fields, the sheet)
  if (w95New) { tft.fillRect(x, y, w, h, W95_PAPER); tft.drawRect(x, y, w, h, W95_SH); return; }
  tft.fillRect(x + 2, y + 2, w - 4, h - 4, W95_PAPER);
  tft.drawFastHLine(x, y, w, W95_SH); tft.drawFastVLine(x, y, h, W95_SH);
  tft.drawFastHLine(x + 1, y + 1, w - 2, W95_DK); tft.drawFastVLine(x + 1, y + 1, h - 2, W95_DK);
  tft.drawFastHLine(x, y + h - 1, w, W95_HI); tft.drawFastVLine(x + w - 1, y, h, W95_HI);
}
void w95Text(int x, int y, const char* s, uint16_t fg, uint16_t bg) { putText(x, y, 1, fg, bg, s); }
void w95Title(const char* t, uint16_t iconCol, char letter) {
  if (w95New) {                                      // Office 365: the app's colour, a white app tile, flat window buttons
    tft.fillRect(0, 0, W, 14, w95Acc);
    tft.fillRoundRect(2, 2, 10, 10, 2, W95_HI);
    char l[2] = {letter, 0};
    putText(4, 3, 1, w95Acc, W95_HI, l);
    putText(16, 4, 1, W95_HI, w95Acc, t);
    tft.drawFastHLine(282, 7, 6, W95_HI);
    tft.drawRect(297, 3, 7, 7, W95_HI);
    tft.drawLine(310, 3, 316, 9, W95_HI); tft.drawLine(316, 3, 310, 9, W95_HI);
    (void)iconCol;
    return;
  }
  tft.fillRect(0, 0, W, 14, W95_NAVY);
  tft.fillRect(2, 2, 10, 10, iconCol);
  char l[2] = {letter, 0};
  putText(5, 3, 1, W95_HI, iconCol, l);
  putText(16, 4, 1, W95_HI, W95_NAVY, t);
  for (int i = 0; i < 3; i++) {                      // _ [] X
    int x = 278 + i * 13 + (i == 2 ? 2 : 0);
    tft.fillRect(x, 2, 12, 10, W95_FACE);
    w95Bevel(x, 2, 12, 10, true);
    if (i == 0) tft.fillRect(x + 3, 8, 5, 2, W95_INK);
    else if (i == 1) { tft.drawRect(x + 3, 4, 6, 5, W95_INK); tft.drawFastHLine(x + 3, 5, 6, W95_INK); }
    else { tft.drawLine(x + 3, 4, x + 7, 8, W95_INK); tft.drawLine(x + 4, 4, x + 8, 8, W95_INK); tft.drawLine(x + 7, 4, x + 3, 8, W95_INK); tft.drawLine(x + 8, 4, x + 4, 8, W95_INK); }
  }
}
void w95MenuBar() {
  tft.fillRect(0, 14, W, 12, w95New ? w95Acc : W95_FACE);
  int x = 3;
  w95NM = 0;
  for (int i = 0; i < 9; i++) {
    const char* t = pgmItem(w95Titles, i);
    if (!t[0]) break;
    int w = strlen(t) * 6 + 8;
    w95MenuX[i] = x;
    bool on = w95Mode && w95Focus == i;
    if (w95New) {                                    // ribbon tabs: the open one is a white tab, the focused one underlined
      bool open = on && w95Mode == 2;
      if (open) tft.fillRect(x, 15, w, 11, W95_FACE);
      putText(x + 4, 16, 1, open ? w95Acc : W95_HI, open ? W95_FACE : w95Acc, t);
      if (on && !open) tft.fillRect(x + 3, 24, w - 6, 2, W95_HI);
      x += w; w95NM++;
      continue;
    }
    if (on && w95Mode == 2) { tft.fillRect(x, 14, w, 12, W95_FACE); w95Bevel(x, 14, w, 12, false); }
    else if (on) tft.fillRect(x, 15, w, 10, W95_NAVY);
    uint16_t fg = on && w95Mode == 1 ? W95_HI : W95_INK, bg = on && w95Mode == 1 ? W95_NAVY : W95_FACE;
    putText(x + 4, 16, 1, fg, bg, t);
    tft.drawFastHLine(x + 4, 24, 5, fg);             // underlined access letter
    x += w;
    w95NM++;
  }
}
// ---------- toolbar icons (14 x 14, centred on cx, cy) ----------
void w95Icon(int ic, int cx, int cy) {
  int x = cx - 6, y = cy - 6;
  switch (ic) {
    case IC_TYPE: tft.drawLine(x + 2, y + 11, x + 10, y + 3, W95_INK); tft.drawLine(x + 3, y + 11, x + 11, y + 3, W95_INK);
                  tft.drawLine(x + 2, y + 12, x + 1, y + 12, W95_INK); tft.fillRect(x + 9, y + 1, 3, 3, W95_RED); break;
    case IC_NEW: tft.fillRect(x + 2, y, 8, 12, W95_PAPER); tft.drawRect(x + 2, y, 8, 12, W95_INK); tft.drawLine(x + 7, y, x + 9, y + 2, W95_INK); break;
    case IC_OPEN: tft.fillRect(x, y + 3, 11, 8, W95_YEL); tft.drawRect(x, y + 3, 11, 8, W95_INK); tft.drawFastHLine(x + 1, y + 2, 4, W95_INK); break;
    case IC_SAVE: tft.fillRect(x + 1, y + 1, 11, 11, W95_NAVY); tft.fillRect(x + 3, y + 1, 7, 4, W95_PAPER); tft.fillRect(x + 4, y + 8, 5, 4, W95_FACE); break;
    case IC_PREVIEW: tft.fillRect(x + 1, y, 8, 11, W95_PAPER); tft.drawRect(x + 1, y, 8, 11, W95_INK); tft.drawCircle(x + 8, y + 7, 3, W95_INK); tft.drawLine(x + 10, y + 9, x + 12, y + 11, W95_INK); break;
    case IC_B: putText(x + 3, y + 3, 1, W95_INK, W95_FACE, FS("B")); tft.setTextColor(W95_INK); tft.setCursor(x + 4, y + 3); tft.print('B'); break;
    case IC_I: tft.drawLine(x + 7, y + 2, x + 4, y + 10, W95_INK); tft.drawLine(x + 8, y + 2, x + 5, y + 10, W95_INK); tft.drawFastHLine(x + 5, y + 2, 5, W95_INK); tft.drawFastHLine(x + 2, y + 10, 5, W95_INK); break;
    case IC_U: tft.drawFastVLine(x + 3, y + 1, 7, W95_INK); tft.drawFastVLine(x + 9, y + 1, 7, W95_INK); tft.drawFastHLine(x + 4, y + 8, 5, W95_INK); tft.drawFastHLine(x + 2, y + 11, 9, W95_INK); break;
    case IC_LEFT: case IC_CENTER: case IC_RIGHT:
      for (int k = 0; k < 5; k++) { int w = (k & 1) ? 7 : 11, ox = ic == IC_LEFT ? 1 : ic == IC_RIGHT ? 12 - w : (13 - w) / 2; tft.drawFastHLine(x + ox, y + 2 + k * 2, w, W95_INK); }
      break;
    case IC_BULLET: case IC_NUMBER:
      for (int k = 0; k < 3; k++) {
        if (ic == IC_BULLET) tft.fillRect(x + 1, y + 2 + k * 4, 2, 2, W95_INK); else tft.drawFastVLine(x + 2, y + 1 + k * 4, 3, W95_INK);
        tft.drawFastHLine(x + 5, y + 3 + k * 4, 7, W95_INK);
      }
      break;
    case IC_TABLE: tft.drawRect(x, y + 1, 13, 11, W95_INK); tft.drawFastHLine(x, y + 5, 13, W95_INK); tft.drawFastHLine(x, y + 8, 13, W95_INK);
                   tft.drawFastVLine(x + 4, y + 1, 11, W95_INK); tft.drawFastVLine(x + 8, y + 1, 11, W95_INK); tft.fillRect(x + 1, y + 2, 11, 3, W95_NAVY); break;
    case IC_CHART: tft.fillRect(x + 1, y + 6, 3, 6, W95_RED); tft.fillRect(x + 5, y + 2, 3, 10, W95_NAVY); tft.fillRect(x + 9, y + 4, 3, 8, W95_GRN); tft.drawFastHLine(x, y + 12, 13, W95_INK); break;
    case IC_SUM: tft.drawFastHLine(x + 2, y + 1, 9, W95_INK); tft.drawFastHLine(x + 2, y + 11, 9, W95_INK); tft.drawLine(x + 2, y + 1, x + 6, y + 6, W95_INK); tft.drawLine(x + 6, y + 6, x + 2, y + 11, W95_INK);
                 tft.drawLine(x + 3, y + 1, x + 7, y + 6, W95_INK); break;
    case IC_FX: putText(x + 1, y + 3, 1, W95_INK, W95_FACE, FS("fx")); break;
    case IC_SORTA: case IC_SORTZ:
      putText(x, y, 1, W95_INK, W95_FACE, ic == IC_SORTA ? FS("A") : FS("Z")); putText(x, y + 6, 1, W95_INK, W95_FACE, ic == IC_SORTA ? FS("Z") : FS("A"));
      tft.drawFastVLine(x + 10, y + 1, 11, W95_NAVY); tft.drawLine(x + 8, y + 9, x + 10, y + 11, W95_NAVY); tft.drawLine(x + 12, y + 9, x + 10, y + 11, W95_NAVY); break;
    case IC_CUR: putText(x + 3, y + 3, 1, W95_GRN, W95_FACE, FS("$")); break;
    case IC_PCT: putText(x + 3, y + 3, 1, W95_INK, W95_FACE, FS("%")); break;
    case IC_COMMA: putText(x + 1, y + 3, 1, W95_INK, W95_FACE, FS(",0")); break;
    case IC_UNDO: tft.drawCircleHelper(x + 7, y + 7, 4, 0x3, W95_NAVY); tft.drawCircleHelper(x + 7, y + 7, 4, 0x6, W95_NAVY);
                  tft.fillTriangle(x + 1, y + 5, x + 6, y + 5, x + 3, y + 1, W95_NAVY); break;
    case IC_FIND: tft.drawCircle(x + 5, y + 5, 4, W95_INK); tft.drawLine(x + 8, y + 8, x + 12, y + 12, W95_INK); tft.drawLine(x + 9, y + 8, x + 12, y + 11, W95_INK); break;
    case IC_COPY: tft.fillRect(x, y, 7, 9, W95_PAPER); tft.drawRect(x, y, 7, 9, W95_INK); tft.fillRect(x + 5, y + 4, 7, 9, W95_PAPER); tft.drawRect(x + 5, y + 4, 7, 9, W95_INK); break;
    case IC_PASTE: tft.fillRect(x + 1, y + 1, 9, 11, RGB565(160, 110, 50)); tft.fillRect(x + 3, y, 5, 3, W95_FACE); tft.drawRect(x + 3, y, 5, 3, W95_INK);
                   tft.fillRect(x + 6, y + 5, 7, 8, W95_PAPER); tft.drawRect(x + 6, y + 5, 7, 8, W95_INK); break;
  }
}
void w95DrawTool(int t, bool focus) {
  int x = w95ToolX[t], y = 27;
  bool on = w95On && w95On(1000 + t);
  if (w95New) {                                      // flat buttons: pressed = a light tint, focus = an outline
    tft.fillRect(x, y, 16, 16, W95_FACE);
    if (on) tft.fillRoundRect(x, y, 16, 16, 3, w95Tint(w95Acc, 190));
    if (focus) tft.drawRoundRect(x, y, 16, 16, 3, w95Acc);
    w95Icon(w95ToolIc[t], x + 8, y + 8);
    return;
  }
  tft.fillRect(x, y, 16, 16, W95_FACE);
  if (on) { w95Bevel(x, y, 16, 16, false); for (int yy = y + 2; yy < y + 14; yy += 2) for (int xx = x + 2 + (yy & 2) / 2; xx < x + 14; xx += 2) tft.drawPixel(xx, yy, W95_HI); }
  else w95Bevel(x, y, 16, 16, true);
  w95Icon(w95ToolIc[t], x + 8 + (on ? 1 : 0), y + 8 + (on ? 1 : 0));
  if (focus) for (int k = 0; k < 16; k += 2) {       // the Win95 dotted focus box
    tft.drawPixel(x + 1 + k, y + 1, W95_INK); tft.drawPixel(x + 1 + k, y + 14, W95_INK);
    tft.drawPixel(x + 1, y + 1 + k, W95_INK); tft.drawPixel(x + 14, y + 1 + k, W95_INK);
  }
}
void w95Toolbar() {                                   // a 255 entry in the list = a gap between groups
  tft.fillRect(0, 26, W, 18, W95_FACE);
  tft.drawFastHLine(0, w95New ? 43 : 26, W, w95New ? W95_SH : W95_HI);
  int x = 4, t = 0;
  for (int i = 0; w95Tools && t < 16; i++) {
    uint8_t ic = w95Tools[i];
    if (ic == 254) break;
    if (ic == 255) { tft.drawFastVLine(x + 1, 29, 12, W95_SH); if (!w95New) tft.drawFastVLine(x + 2, 28, 14, W95_HI); x += 6; continue; }
    w95ToolX[t] = x; w95ToolIc[t] = ic;
    x += 17;
    t++;
  }
  w95NT = t;
  for (int k = 0; k < w95NT; k++) w95DrawTool(k, w95Mode == 1 && w95Focus == w95NM + k);
}
void w95StatusBar(const char* a, const char* b) {
  if (w95New) {                                      // a coloured status bar, like Word / Excel 365
    tft.fillRect(0, 226, W, 14, w95Acc);
    putText(5, 230, 1, W95_HI, w95Acc, a);
    putText(203, 230, 1, W95_HI, w95Acc, b);
    return;
  }
  tft.fillRect(0, 226, W, 14, W95_FACE);
  tft.drawFastHLine(0, 226, W, W95_HI);
  tft.fillRect(2, 228, 196, 11, W95_FACE); w95Bevel(2, 228, 196, 11, false);
  putText(5, 230, 1, W95_INK, W95_FACE, a);
  tft.fillRect(200, 228, 118, 11, W95_FACE); w95Bevel(200, 228, 118, 11, false);
  putText(203, 230, 1, W95_INK, W95_FACE, b);
}
void w95ScrollV(int x, int y, int h, long pos, long total, long page) {   // Win95 vertical scroll bar
  if (w95New) {                                      // a thin modern bar
    tft.fillRect(x, y, 12, h, W95_FACE);
    long span = max(1L, total);
    int kh = max(10L, (long)(h - 4) * min(page, span) / span);
    int ky = y + 2 + (total > page ? (long)(h - 4 - kh) * pos / max(1L, total - page) : 0);
    tft.fillRoundRect(x + 3, ky, 6, kh, 3, W95_DK);
    return;
  }
  tft.fillRect(x, y, 12, 12, W95_FACE); w95Bevel(x, y, 12, 12, true);
  tft.fillTriangle(x + 3, y + 7, x + 8, y + 7, x + 5, y + 4, W95_INK);
  tft.fillRect(x, y + h - 12, 12, 12, W95_FACE); w95Bevel(x, y + h - 12, 12, 12, true);
  tft.fillTriangle(x + 3, y + h - 8, x + 8, y + h - 8, x + 5, y + h - 5, W95_INK);
  int ty = y + 12, th = h - 24;
  for (int yy = ty; yy < ty + th; yy++) for (int xx = x; xx < x + 12; xx++) tft.drawPixel(xx, yy, ((xx + yy) & 1) ? W95_HI : W95_FACE);
  long span = max(1L, total);
  int kh = max(8L, (long)th * min(page, span) / span);
  int ky = ty + (total > page ? (long)(th - kh) * pos / max(1L, total - page) : 0);
  tft.fillRect(x, ky, 12, kh, W95_FACE); w95Bevel(x, ky, 12, kh, true);
}
// ---------- menus ----------
int w95ItemCount() { int n = 0; while (pgmItem(w95Menus[w95Focus], n)[0]) n++; return n; }
bool w95Sep(int i) { const char* t = pgmItem(w95Menus[w95Focus], i); return t[0] == '-' && !t[1]; }
int w95DropX() { return w95MenuX[w95Focus]; }
int w95DropW() { int n = w95ItemCount(), m = 0; for (int i = 0; i < n; i++) m = max(m, (int)strlen(pgmItem(w95Menus[w95Focus], i))); return m * 6 + 22; }
int w95ItemY(int i) { int y = 28; for (int k = 0; k < i; k++) y += w95Sep(k) ? 6 : 11; return y; }
void w95DrawItem(int i, bool sel) {
  int x = w95DropX(), w = w95DropW();
  if (x + w > W - 2) x = W - 2 - w;
  int y = w95ItemY(i);
  uint16_t menuBg = w95New ? W95_PAPER : W95_FACE;
  if (w95Sep(i)) { tft.fillRect(x + 2, y, w - 4, 6, menuBg); tft.drawFastHLine(x + 3, y + 2, w - 6, W95_SH); if (!w95New) tft.drawFastHLine(x + 3, y + 3, w - 6, W95_HI); return; }
  uint16_t bg = sel ? (w95New ? w95Tint(w95Acc, 200) : W95_NAVY) : menuBg, fg = sel && !w95New ? W95_HI : W95_INK;
  tft.fillRect(x + 2, y, w - 4, 11, bg);
  putText(x + 14, y + 2, 1, fg, bg, pgmItem(w95Menus[w95Focus], i));
  if (w95On && w95On(w95Focus * 32 + i)) putText(x + 4, y + 2, 1, fg, bg, FS("\xFB"));   // check mark
}
void w95DrawDrop() {
  int n = w95ItemCount(), x = w95DropX(), w = w95DropW(), h = w95ItemY(n) - 26 + 2;
  if (x + w > W - 2) x = W - 2 - w;
  if (w95New) { tft.fillRect(x + 2, 28, w, h, W95_SH); tft.fillRect(x, 26, w, h, W95_PAPER); tft.drawRect(x, 26, w, h, W95_SH); }
  else { tft.fillRect(x, 26, w, h, W95_FACE); w95Bevel(x, 26, w, h, true); }
  for (int i = 0; i < n; i++) w95DrawItem(i, i == w95Item);
}
void w95Open() {                                      // press in the document: the bar gets focus
  w95Mode = 1;
  if (w95Focus >= w95NM + w95NT) w95Focus = w95NM;
  w95MenuBar(); w95Toolbar();
}
void w95Close() { w95Mode = 0; if (w95Redraw) w95Redraw(); }
void w95Back() {                                      // hold: close the open menu, then leave the bar
  if (w95Mode == 2) { w95Mode = 1; if (w95Redraw) w95Redraw(); w95MenuBar(); w95Toolbar(); }
  else w95Close();
}
int w95Tick(int c, bool sp) {                         // -1 = nothing yet, else a command (menu*32+item, 1000+tool)
  if (w95Mode == 1) {
    if (c) {
      int n = w95NM + w95NT, old = w95Focus;
      w95Focus = ((w95Focus + c) % n + n) % n;
      if (old < w95NM || w95Focus < w95NM) w95MenuBar();
      if (old >= w95NM) w95DrawTool(old - w95NM, false);
      if (w95Focus >= w95NM) w95DrawTool(w95Focus - w95NM, true);
      uiClick();
    }
    if (!sp) return -1;
    SFX(T_HOP);
    if (w95Focus < w95NM) { w95Mode = 2; w95Item = 0; while (w95Sep(w95Item)) w95Item++; w95MenuBar(); w95DrawDrop(); return -1; }
    w95Mode = 0;
    int t = w95Focus - w95NM;
    w95MenuBar(); w95DrawTool(t, false);
    return 1000 + t;
  }
  if (w95Mode == 2) {
    if (c) {
      int n = w95ItemCount(), old = w95Item, d = c > 0 ? 1 : -1;
      for (int k = 0; k < abs(c); k++) do { w95Item = (w95Item + d + n) % n; } while (w95Sep(w95Item));
      if (old != w95Item) { w95DrawItem(old, false); w95DrawItem(w95Item, true); uiClick(); }
    }
    if (!sp) return -1;
    SFX(T_HOP);
    int cmd = w95Focus * 32 + w95Item;
    w95Mode = 0;
    return cmd;
  }
  return -1;
}
// ---------- message box (OK) ----------
bool w95Msg = false;
void w95MsgBox(const char* title, const char* l1, const char* l2, const char* l3) {
  int x = 30, y = 70, w = 260, h = 92;
  if (w95New) {                                      // a flat card with the app's colour
    tft.fillRect(x + 3, y + 3, w, h, W95_SH);
    tft.fillRect(x, y, w, h, W95_PAPER); tft.drawRect(x, y, w, h, W95_SH);
    tft.fillRect(x, y, w, 14, w95Acc);
    putText(x + 6, y + 4, 1, W95_HI, w95Acc, title);
    tft.fillCircle(x + 20, y + 34, 10, w95Acc); putText(x + 18, y + 31, 1, W95_HI, w95Acc, FS("i"));
    putText(x + 40, y + 22, 1, W95_INK, W95_PAPER, l1);
    putText(x + 40, y + 34, 1, W95_INK, W95_PAPER, l2);
    putText(x + 40, y + 46, 1, W95_INK, W95_PAPER, l3);
    int bx = x + w / 2 - 30;
    tft.fillRoundRect(bx, y + 66, 60, 18, 3, w95Acc);
    putText(bx + 24, y + 71, 1, W95_HI, w95Acc, FS("OK"));
    w95Msg = true;
    return;
  }
  tft.fillRect(x + 3, y + 3, w, h, W95_DK);
  tft.fillRect(x, y, w, h, W95_FACE); w95Bevel(x, y, w, h, true);
  tft.fillRect(x + 2, y + 2, w - 4, 12, W95_NAVY);
  putText(x + 6, y + 4, 1, W95_HI, W95_NAVY, title);
  tft.fillCircle(x + 20, y + 34, 10, W95_NAVY); putText(x + 18, y + 31, 1, W95_HI, W95_NAVY, FS("i"));
  putText(x + 40, y + 22, 1, W95_INK, W95_FACE, l1);
  putText(x + 40, y + 34, 1, W95_INK, W95_FACE, l2);
  putText(x + 40, y + 46, 1, W95_INK, W95_FACE, l3);
  int bx = x + w / 2 - 30;
  tft.fillRect(bx, y + 66, 60, 18, W95_FACE); w95Bevel(bx, y + 66, 60, 18, true); tft.drawRect(bx - 1, y + 65, 62, 20, W95_INK);
  putText(bx + 24, y + 71, 1, W95_INK, W95_FACE, FS("OK"));
  w95Msg = true;
}
bool w95MsgTick(int c, bool sp) { (void)c; if (!sp) return false; w95Msg = false; SFX(T_HOP); if (w95Redraw) w95Redraw(); return true; }
// ---------- pie chart (Word + Excel) ----------
const uint16_t PIE_COL[8] = {RGB565(0, 0, 128), RGB565(192, 0, 0), RGB565(0, 128, 0), RGB565(255, 192, 0),
                             RGB565(128, 0, 128), RGB565(0, 160, 192), RGB565(255, 128, 0), RGB565(96, 96, 96)};
void drawPie(int cx, int cy, int r, const float* v, int n) {
  float tot = 0;
  for (int i = 0; i < n; i++) if (v[i] > 0) tot += v[i];
  if (tot <= 0) { tft.drawCircle(cx, cy, r, W95_INK); return; }
  float a = -M_PI / 2;
  for (int i = 0; i < n; i++) {
    if (v[i] <= 0) continue;
    float span = v[i] / tot * 2 * M_PI, end = a + span;
    for (float t = a; t < end; t += 0.08f) {
      float t2 = min(end, t + 0.08f);
      tft.fillTriangle(cx, cy, cx + r * cos(t), cy + r * sin(t), cx + r * cos(t2), cy + r * sin(t2), PIE_COL[i % 8]);
    }
    a = end;
  }
  tft.drawCircle(cx, cy, r, W95_INK);
}

// ================= OFFICE FILES: .docx + .xlsx (ZIP + deflate + XML in a few KB of RAM) =================
// Word 2007-365 (.docx) and Excel 2007-365 (.xlsx) files are ZIP archives of XML parts. Reading one: find the part
// in the ZIP's directory, inflate it (the decoder below) and walk its XML while it streams by - nothing is
// unpacked whole. Deflate may copy text from up to 32 KB back and the ESP8266 can't spare 32 KB, so the newest
// 2-16 KB of output stay in RAM and older output goes to scratch files in flash (/~z0, /~z1...) that the decoder
// reads back when it needs them. Writing one: a "stored" ZIP (no compression - Word and Excel open those fine);
// each part is generated twice, once to measure its size and CRC for the ZIP header, then for real.

// ---------- the font's characters (CP437) <-> Unicode ----------
const uint16_t CP_HI[128] PROGMEM = {
  0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7, 0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
  0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9, 0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
  0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA, 0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
  0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
  0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F, 0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
  0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B, 0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
  0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4, 0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
  0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248, 0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0};
const uint16_t CP_LO[32] PROGMEM = {
  0, 0x263A, 0x263B, 0x2665, 0x2666, 0x2663, 0x2660, 0x2022, 0x25D8, 0x25CB, 0x25D9, 0x2642, 0x2640, 0x266A, 0x266B, 0x263C,
  0x25BA, 0x25C4, 0x2195, 0x203C, 0x00B6, 0x00A7, 0x25AC, 0x21A8, 0x2191, 0x2193, 0x2192, 0x2190, 0x221F, 0x2194, 0x25B2, 0x25BC};
uint16_t cpUni(uint8_t c) {
  if (c >= 0x80) return pgm_read_word(&CP_HI[c - 0x80]);
  if (c && c < 32 && c != '\t' && c != '\n' && c != '\r') return pgm_read_word(&CP_LO[c]);
  return c;
}
uint8_t utf8ToCp437(uint16_t u);
uint8_t uniCp(uint16_t u) {                         // 0 = the font has no such character
  if (u < 0x80) return u;
  if (u == 0xA0) return ' ';
  for (int i = 0; i < 128; i++) if (pgm_read_word(&CP_HI[i]) == u) return 0x80 + i;
  for (int i = 1; i < 32; i++) if (i != '\t' && i != '\n' && i != '\r' && pgm_read_word(&CP_LO[i]) == u) return i;
  if (u >= 0xC0 && u <= 0xFF)                        // accented letters the font lacks (A-acute...): the plain letter
    return pgm_read_byte(PSTR("AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPsaaaaaaaceeeeiiiidnooooo/ouuuuypy") + (u - 0xC0));
  return utf8ToCp437(u);
}

// ---------- the working state (only allocated while a file is read or written) ----------
struct ZState {
  File in, seg, out;                                 // the ZIP being read; a chunk of old output; the ZIP being written
  uint32_t inLeft = 0, bits = 0, pos = 0, ws = 0, cacheAt = 0;
  uint16_t ip = 0, inN = 0;
  uint8_t nbits = 0, cacheN = 0, nfiles = 0;
  int segNo = -1;
  bool spill = false, err = false, stop = false;
  uint8_t* win = nullptr;                            // the newest ws bytes of output (a ring)
  uint8_t ib[256], cache[64], lens[320];
  uint16_t lcnt[16], lsym[288], dcnt[16], dsym[30];
  uint8_t xst = 0, tn = 0, en = 0, u8n = 0; char q = 0; bool u8big = false; uint16_t u8c = 0;   // XML reader
  char tag[160], ent[10];
  uint32_t crc = 0, sz = 0, off = 0; bool real = false; uint8_t wb[128], wn = 0, ne = 0;          // ZIP writer
  uint32_t eCrc[8], eSize[8], eOff[8]; PGM_P eName[8];
  union {                                            // what the Word / Excel readers + writers keep on the side
    struct { char sid[20][14]; uint8_t skind[20], ns, kind, inStyle; char cur[14]; int16_t abs, num; bool lvl0;
             uint32_t absNum[2], numNum[2]; uint16_t nblocks; } d;
    struct { uint16_t pend[118][2]; float cache[118]; uint8_t xf[64], font[32], nfCls[24], ord[118];
             uint16_t nfId[24]; uint8_t nnf, nxf, nfont, sect, t, sfmt, vn, fn, svn, skip;
             bool inV, inF, inIs, inT, want; int16_t row, col, si, fSi; uint16_t npend, maxIdx, dropped;
             char v[32], f[64], sv[24], rid[12], sname[32], target[48];
             char shf[6][24]; int16_t shPos[6]; int8_t shSi[6]; uint8_t nsh; } x;
  } u;
};
ZState* Z = nullptr;
bool zBegin() { Z = new (std::nothrow) ZState(); return Z != nullptr; }
void zEnd() {
  if (!Z) return;
  if (Z->in) Z->in.close();
  if (Z->seg) Z->seg.close();
  if (Z->out) Z->out.close();
  free(Z->win);
  delete Z; Z = nullptr;
}
uint16_t zU16(const uint8_t* p) { return p[0] | p[1] << 8; }
uint32_t zU32(const uint8_t* p) { return p[0] | p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
// where a part lives in the ZIP (data offset, sizes, method 0 = stored / 8 = deflate). false = not there
bool zipFind(File& f, const char* name, uint32_t* off, uint32_t* csize, uint32_t* usize, uint16_t* method) {
  uint32_t size = f.size();
  if (size < 22) return false;
  uint8_t b[46];
  int32_t at = (int32_t)size - 22, lo = max(0, (int32_t)size - 22 - 2048);   // the directory's end record
  for (; at >= lo; at--) { f.seek(at); if (f.read(b, 4) == 4 && zU32(b) == 0x06054b50) break; }
  if (at < lo || !f.seek(at) || f.read(b, 22) != 22) return false;
  uint16_t n = zU16(b + 10);
  f.seek(zU32(b + 16));
  size_t want = strlen(name);
  for (uint16_t k = 0; k < n; k++) {
    if (f.read(b, 46) != 46 || zU32(b) != 0x02014b50) return false;
    uint16_t nl = zU16(b + 28), skip = zU16(b + 30) + zU16(b + 32), meth = zU16(b + 10);
    uint32_t cs = zU32(b + 20), us = zU32(b + 24), lo2 = zU32(b + 42);
    char nm[64];
    uint16_t rd = min(nl, (uint16_t)63);
    if (f.read((uint8_t*)nm, rd) != rd) return false;
    nm[rd] = 0;
    f.seek(nl - rd + skip, SeekCur);
    if (nl == want && !strcasecmp(nm, name)) {
      f.seek(lo2);
      if (f.read(b, 30) != 30 || zU32(b) != 0x04034b50) return false;
      *off = lo2 + 30 + zU16(b + 26) + zU16(b + 28); *csize = cs; *usize = us; *method = meth;
      return true;
    }
  }
  return false;
}

// ---------- inflate (RFC 1951), the canonical-Huffman way of zlib's "puff" ----------
const uint16_t Z_LBASE[29] PROGMEM = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t Z_LEXT[29] PROGMEM = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t Z_DBASE[30] PROGMEM = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073,
                                      4097, 6145, 8193, 12289, 16385, 24577};
const uint8_t Z_DEXT[30] PROGMEM = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
const uint8_t Z_ORDER[19] PROGMEM = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
void (*zSink)(uint8_t) = nullptr;                   // where the output goes (the XML reader)
int zByte() {
  if (Z->ip >= Z->inN) {
    if (!Z->inLeft) { Z->err = true; return -1; }
    int n = Z->in.read(Z->ib, min((uint32_t)sizeof Z->ib, Z->inLeft));
    if (n <= 0) { Z->err = true; return -1; }
    Z->inN = n; Z->ip = 0; Z->inLeft -= n;
  }
  return Z->ib[Z->ip++];
}
int zBits(int n) {
  while (Z->nbits < n) { int b = zByte(); if (b < 0) return 0; Z->bits |= (uint32_t)b << Z->nbits; Z->nbits += 8; }
  int v = Z->bits & ((1UL << n) - 1);
  Z->bits >>= n; Z->nbits -= n;
  return v;
}
void zBuild(uint16_t* cnt, uint16_t* sym, const uint8_t* len, int n) {
  uint16_t offs[16];
  memset(cnt, 0, 16 * sizeof(uint16_t));
  for (int i = 0; i < n; i++) cnt[len[i] & 15]++;
  cnt[0] = 0;
  offs[1] = 0;
  for (int i = 1; i < 15; i++) offs[i + 1] = offs[i] + cnt[i];
  for (int i = 0; i < n; i++) if (len[i]) sym[offs[len[i] & 15]++] = i;
}
int zDecode(const uint16_t* cnt, const uint16_t* sym) {
  int code = 0, first = 0, index = 0;
  for (int len = 1; len < 16; len++) {
    code |= zBits(1);
    int count = cnt[len];
    if (code - count < first) return sym[index + (code - first)];
    index += count; first += count; first <<= 1; code <<= 1;
  }
  Z->err = true;
  return -1;
}
char zSegName[8];
const char* zSegFile(int k) { snprintf_P(zSegName, sizeof zSegName, PSTR("/~z%d"), k % Z->nfiles); return zSegName; }
void zFlushSeg(int k) {                              // the ring is full: keep a copy in flash for long copies
  if (Z->segNo >= 0 && Z->segNo % Z->nfiles == k % Z->nfiles) { Z->seg.close(); Z->segNo = -1; }
  File f = LittleFS.open(zSegFile(k), "w");
  if (!f || f.write(Z->win, Z->ws) != Z->ws) Z->err = true;
  if (f) f.close();
}
void zPut(uint8_t b) {
  if (Z->pos && !(Z->pos % Z->ws)) { if (!Z->spill) { Z->err = true; return; } zFlushSeg(Z->pos / Z->ws - 1); }
  Z->win[Z->pos % Z->ws] = b;
  Z->pos++;
  zSink(b);
}
uint8_t zFar(uint32_t sp) {                          // an older byte: read back from its chunk in flash
  int k = sp / Z->ws;
  uint32_t o = sp % Z->ws;
  if (k != Z->segNo || o < Z->cacheAt || o >= Z->cacheAt + Z->cacheN) {
    if (k != Z->segNo) {
      if (Z->segNo >= 0) Z->seg.close();
      Z->seg = LittleFS.open(zSegFile(k), "r");
      Z->segNo = Z->seg ? k : -1;
      if (!Z->seg) { Z->err = true; return 0; }
    }
    Z->seg.seek(o);
    Z->cacheAt = o;
    Z->cacheN = Z->seg.read(Z->cache, min((uint32_t)sizeof Z->cache, Z->ws - o));
    if (!Z->cacheN) { Z->err = true; return 0; }
  }
  return Z->cache[o - Z->cacheAt];
}
bool zInflate() {
  int last;
  do {
    last = zBits(1);
    int type = zBits(2);
    if (Z->err) return false;
    if (type == 0) {                                 // stored block
      Z->bits = 0; Z->nbits = 0;
      int a = zByte(), b = zByte(), c = zByte(), d = zByte();
      if (Z->err) return false;
      unsigned n = a | b << 8;
      if ((n ^ 0xFFFF) != (unsigned)(c | d << 8)) { Z->err = true; return false; }
      while (n--) { int v = zByte(); if (v < 0) return false; zPut(v); if (Z->err) return false; if (Z->stop) return true; }
    } else if (type == 1 || type == 2) {
      uint8_t* L = Z->lens;
      if (type == 1) {                               // the fixed codes
        for (int i = 0; i < 288; i++) L[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
        zBuild(Z->lcnt, Z->lsym, L, 288);
        for (int i = 0; i < 30; i++) L[i] = 5;
        zBuild(Z->dcnt, Z->dsym, L, 30);
      } else {                                       // codes sent with the block
        int nl = zBits(5) + 257, nd = zBits(5) + 1, nc = zBits(4) + 4;
        if (nl > 286 || nd > 30 || Z->err) { Z->err = true; return false; }
        uint8_t cl[19];
        memset(cl, 0, sizeof cl);
        for (int i = 0; i < nc; i++) cl[pgm_read_byte(&Z_ORDER[i])] = zBits(3);
        uint16_t ccnt[16], csym[19];
        zBuild(ccnt, csym, cl, 19);
        int i = 0;
        while (i < nl + nd) {
          int s = zDecode(ccnt, csym);
          if (s < 0 || Z->err) return false;
          if (s < 16) { L[i++] = s; continue; }
          int rep, val = 0;
          if (s == 16) { if (!i) { Z->err = true; return false; } val = L[i - 1]; rep = 3 + zBits(2); }
          else if (s == 17) rep = 3 + zBits(3);
          else rep = 11 + zBits(7);
          if (i + rep > nl + nd) { Z->err = true; return false; }
          while (rep--) L[i++] = val;
        }
        if (!L[256]) { Z->err = true; return false; }
        zBuild(Z->lcnt, Z->lsym, L, nl);
        zBuild(Z->dcnt, Z->dsym, L + nl, nd);
      }
      for (;;) {
        int s = zDecode(Z->lcnt, Z->lsym);
        if (s < 0 || Z->err) return false;
        if (s < 256) { zPut(s); if (Z->err) return false; if (Z->stop) return true; continue; }
        if (s == 256) break;
        s -= 257;
        if (s >= 29) { Z->err = true; return false; }
        int n = pgm_read_word(&Z_LBASE[s]) + zBits(pgm_read_byte(&Z_LEXT[s]));
        int ds = zDecode(Z->dcnt, Z->dsym);
        if (ds < 0 || ds >= 30) { Z->err = true; return false; }
        uint32_t dist = pgm_read_word(&Z_DBASE[ds]) + zBits(pgm_read_byte(&Z_DEXT[ds]));
        if (dist > Z->pos || Z->err) { Z->err = true; return false; }
        while (n--) {
          uint32_t sp = Z->pos - dist;
          zPut(dist <= Z->ws ? Z->win[sp % Z->ws] : zFar(sp));
          if (Z->err) return false;
          if (Z->stop) return true;
        }
      }
    } else { Z->err = true; return false; }
    yield();
  } while (!last);
  return true;
}

// ---------- XML as it streams by: start tags (local name + raw attributes), end tags, text (Unicode) ----------
void (*xStart)(const char* name, const char* attrs, bool empty) = nullptr;
void (*xEnd)(const char* name) = nullptr;
void (*xText)(uint16_t u) = nullptr;
void xTag() {
  char* t = Z->tag;
  t[Z->tn] = 0;
  if (t[0] == '?' || t[0] == '!') return;            // <?xml ...?>, <!-- comments -->
  bool end = t[0] == '/';
  if (end) t++;
  size_t n = strlen(t);
  bool empty = n && t[n - 1] == '/';
  if (empty) t[--n] = 0;
  char* a = t;
  while (*a && *a != ' ' && *a != '\t' && *a != '\r' && *a != '\n') a++;
  const char* attrs = a;
  if (*a) { *a = 0; attrs = a + 1; }
  const char* name = strchr(t, ':') ? strchr(t, ':') + 1 : t;   // w:p -> p
  if (end) { if (xEnd) xEnd(name); return; }
  if (xStart) xStart(name, attrs, empty);
  if (empty && xEnd) xEnd(name);
}
void xChar(uint16_t u) { if (xText) xText(u); }
void xUtf8(uint8_t b) {
  if (Z->u8n) {
    Z->u8c = (Z->u8c << 6) | (b & 0x3F);
    if (--Z->u8n == 0 && !Z->u8big) xChar(Z->u8c);   // (4-byte ones - emoji - are dropped)
    return;
  }
  if (b < 0x80) xChar(b);
  else if ((b & 0xE0) == 0xC0) { Z->u8c = b & 0x1F; Z->u8n = 1; Z->u8big = false; }
  else if ((b & 0xF0) == 0xE0) { Z->u8c = b & 0x0F; Z->u8n = 2; Z->u8big = false; }
  else if ((b & 0xF8) == 0xF0) { Z->u8c = 0; Z->u8n = 3; Z->u8big = true; }
}
void xByte(uint8_t b) {                              // the inflater's sink
  switch (Z->xst) {
    case 0:                                          // text
      if (b == '<') { Z->xst = 1; Z->tn = 0; Z->q = 0; }
      else if (b == '&') { Z->xst = 2; Z->en = 0; }
      else xUtf8(b);
      break;
    case 1:                                          // inside < ... >
      if (Z->q) { if (b == Z->q) Z->q = 0; }
      else if (b == '"' || b == '\'') Z->q = b;
      else if (b == '>') { xTag(); Z->xst = 0; break; }
      if (Z->tn < sizeof Z->tag - 1) Z->tag[Z->tn++] = b;
      break;
    default: {                                       // &amp; &lt; &#233; &#xE9;
      if (b != ';' && Z->en < sizeof Z->ent - 1) { Z->ent[Z->en++] = b; break; }
      Z->ent[Z->en] = 0; Z->xst = 0;
      const char* e = Z->ent;
      uint16_t u = 0;
      if (e[0] == '#') u = (e[1] == 'x' || e[1] == 'X') ? strtol(e + 2, nullptr, 16) : atoi(e + 1);
      else if (!strcmp_P(e, PSTR("amp"))) u = '&';
      else if (!strcmp_P(e, PSTR("lt"))) u = '<';
      else if (!strcmp_P(e, PSTR("gt"))) u = '>';
      else if (!strcmp_P(e, PSTR("quot"))) u = '"';
      else if (!strcmp_P(e, PSTR("apos"))) u = '\'';
      if (u) xChar(u);
      break;
    }
  }
}
// an attribute's value: "val" matches w:val (any prefix); a name with ':' must match exactly. false = not there
bool xAttr(const char* a, const char* want, char* out, int n) {
  bool full = strchr(want, ':') != nullptr;
  size_t wl = strlen(want);
  while (*a) {
    while (*a == ' ' || *a == '\t' || *a == '\r' || *a == '\n') a++;
    const char* nm = a;
    while (*a && *a != '=' && *a != ' ' && *a != '\t' && *a != '\r' && *a != '\n') a++;
    const char* ne = a;
    while (*a == ' ' || *a == '\t' || *a == '\r' || *a == '\n') a++;
    if (*a != '=') { if (*a && a == ne) a++; continue; }
    a++;
    while (*a == ' ') a++;
    char q = *a;
    if (q != '"' && q != '\'') return false;
    const char* v = ++a;
    while (*a && *a != q) a++;
    int vl = a - v;
    if (*a) a++;
    const char* ln = nm;
    if (!full) for (const char* c = nm; c < ne; c++) if (*c == ':') ln = c + 1;
    if ((size_t)(ne - ln) == wl && !strncmp(ln, want, wl)) { int k = min(vl, n - 1); memcpy(out, v, k); out[k] = 0; return true; }
  }
  return false;
}
long xAttrInt(const char* a, const char* want, long def) { char v[16]; return xAttr(a, want, v, sizeof v) ? atol(v) : def; }
bool xOn(const char* a) {                            // <w:b/> = on, <w:b w:val="0"/> = off
  char v[8];
  if (!xAttr(a, "val", v, sizeof v)) return true;
  return strcmp_P(v, PSTR("0")) && strcmp_P(v, PSTR("false")) && strcmp_P(v, PSTR("none")) && strcmp_P(v, PSTR("off"));
}
// stream one part of a ZIP through the XML reader: 0 ok, 1 no such file/part, 2 not enough memory, 3 broken
int zPart(const char* zipPath, const char* part) {
  char name[64];
  snprintf_P(name, sizeof name, PSTR("%s"), part);
  Z->in = LittleFS.open(zipPath, "r");
  if (!Z->in) return 1;
  uint32_t off, cs, us; uint16_t m;
  if (!zipFind(Z->in, name, &off, &cs, &us, &m)) { Z->in.close(); return 1; }
  Z->in.seek(off);
  Z->inLeft = cs; Z->ip = Z->inN = 0; Z->bits = 0; Z->nbits = 0; Z->pos = 0;
  Z->err = Z->stop = false; Z->segNo = -1; Z->cacheN = 0; Z->spill = false;
  Z->xst = 0; Z->u8n = 0;
  zSink = xByte;
  int r = 0;
  if (m == 0) {                                      // stored: the XML as is
    for (uint32_t i = 0; i < cs && !Z->stop && !Z->err; i++) { int b = zByte(); if (b < 0) break; xByte(b); if (!(i & 4095)) yield(); }
  } else if (m == 8) {
    free(Z->win); Z->win = nullptr;                  // the window: the whole part if it fits, else 16/8/4/2 KB
    uint32_t w = us <= 16384 ? max(us, (uint32_t)256) : 16384;
    for (;;) {
      if (ESP.getMaxFreeBlockSize() > w + 3072) Z->win = (uint8_t*)malloc(w);
      if (Z->win || w <= 2048) break;
      w /= 2;
    }
    if (!Z->win) r = 2;
    else {
      Z->ws = w; Z->spill = us > w; Z->nfiles = 32768 / w + 1;
      zInflate();
      if (Z->seg) Z->seg.close();
      Z->segNo = -1;
      if (Z->spill) for (int k = 0; k < Z->nfiles; k++) { snprintf_P(name, sizeof name, PSTR("/~z%d"), k); LittleFS.remove(name); }
      free(Z->win); Z->win = nullptr;
    }
  } else r = 3;                                      // (another compression method: never seen in Office files)
  Z->in.close();
  if (!r && Z->err) r = 3;
  return r;
}
int zPartP(const char* zipPath, PGM_P part) { char n[64]; strncpy_P(n, part, sizeof n - 1); n[sizeof n - 1] = 0; return zPart(zipPath, n); }

// ---------- ZIP writer ----------
const uint32_t CRC_NIB[16] PROGMEM = {0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
                                      0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C, 0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C};
uint16_t zDosTime = 0, zDosDate = 0x21;              // the files' date in the ZIP (1980-01-01 if the clock isn't set)
void zwFlush() { if (Z->wn && Z->out.write(Z->wb, Z->wn) != Z->wn) Z->err = true; Z->wn = 0; }
void zwRaw(uint8_t b) { Z->wb[Z->wn++] = b; if (Z->wn == sizeof Z->wb) zwFlush(); }
void zw16(uint16_t v) { zwRaw(v); zwRaw(v >> 8); }
void zw32(uint32_t v) { zw16(v); zw16(v >> 16); }
void zwByte(uint8_t b) {                             // part content: counted, CRC'd and (second pass) written
  uint32_t c = Z->crc ^ b;
  c = (c >> 4) ^ pgm_read_dword(&CRC_NIB[c & 15]);
  c = (c >> 4) ^ pgm_read_dword(&CRC_NIB[c & 15]);
  Z->crc = c; Z->sz++;
  if (Z->real) zwRaw(b);
}
void zwStr(const char* s) { while (*s) zwByte(*s++); }
void zwP(PGM_P s) { for (char c; (c = pgm_read_byte(s)); s++) zwByte(c); }
void zwNum(long v) { char b[12]; ltoa(v, b, 10); zwStr(b); }
void zwUni(uint16_t u) {
  if (u < 0x80) zwByte(u);
  else if (u < 0x800) { zwByte(0xC0 | u >> 6); zwByte(0x80 | (u & 0x3F)); }
  else { zwByte(0xE0 | u >> 12); zwByte(0x80 | ((u >> 6) & 0x3F)); zwByte(0x80 | (u & 0x3F)); }
}
void zwText(const char* s, int n, bool line) {      // the font's text -> XML-escaped UTF-8 (line: the \xC4 line = _)
  for (int i = 0; i < n && s[i]; i++) {
    uint8_t c = s[i];
    if (c == '&') zwP(PSTR("&amp;"));
    else if (c == '<') zwP(PSTR("&lt;"));
    else if (c == '>') zwP(PSTR("&gt;"));
    else if (c == '"') zwP(PSTR("&quot;"));
    else if (line && c == 0xC4) zwByte('_');
    else if (c < 32 && c != '\t') { if (cpUni(c) != c) zwUni(cpUni(c)); }
    else zwUni(cpUni(c));
  }
}
void zwStart(File f) {
  Z->out = f; Z->off = 0; Z->ne = 0; Z->wn = 0; Z->err = false;
  if (clkEpoch > 315532800UL) {                      // (1980 or later: the clock was set)
    int y, m, d; civilFromDays(clkEpoch / 86400UL, &y, &m, &d);
    zDosDate = ((y - 1980) << 9) | (m << 5) | d;
    zDosTime = ((clkEpoch / 3600 % 24) << 11) | ((clkEpoch / 60 % 60) << 5) | (clkEpoch % 60 / 2);
  }
}
void zwPart(PGM_P name, void (*gen)()) {             // one file of the ZIP: measured, then written
  if (Z->ne >= 8 || Z->err) { Z->err = true; return; }
  Z->real = false; Z->crc = 0xFFFFFFFF; Z->sz = 0;
  gen();
  uint32_t crc = ~Z->crc, size = Z->sz;
  int k = Z->ne++;
  uint16_t nl = strlen_P(name);
  Z->eCrc[k] = crc; Z->eSize[k] = size; Z->eOff[k] = Z->off; Z->eName[k] = name;
  zw32(0x04034b50); zw16(20); zw16(0); zw16(0); zw16(zDosTime); zw16(zDosDate);
  zw32(crc); zw32(size); zw32(size); zw16(nl); zw16(0);
  for (uint16_t i = 0; i < nl; i++) zwRaw(pgm_read_byte(name + i));
  Z->real = true; Z->crc = 0xFFFFFFFF; Z->sz = 0;
  gen();
  Z->real = false;
  if (~Z->crc != crc || Z->sz != size) Z->err = true;   // (a generator must say the same thing twice)
  Z->off += 30 + nl + size;
  yield();
}
bool zwFinish() {                                    // the directory + its end record
  uint32_t at = Z->off, size = 0;
  for (int k = 0; k < Z->ne; k++) {
    uint16_t nl = strlen_P(Z->eName[k]);
    zw32(0x02014b50); zw16(20); zw16(20); zw16(0); zw16(0); zw16(zDosTime); zw16(zDosDate);
    zw32(Z->eCrc[k]); zw32(Z->eSize[k]); zw32(Z->eSize[k]); zw16(nl); zw16(0); zw16(0); zw16(0); zw16(0); zw32(0);
    zw32(Z->eOff[k]);
    for (uint16_t i = 0; i < nl; i++) zwRaw(pgm_read_byte(Z->eName[k] + i));
    size += 46 + nl;
  }
  zw32(0x06054b50); zw16(0); zw16(0); zw16(Z->ne); zw16(Z->ne); zw32(size); zw32(at); zw16(0);
  zwFlush();
  Z->out.close();
  return !Z->err;
}
bool zwSaveAs(const char* path) {                    // the new file replaces the old one only once it's complete
  bool ok = zwFinish();
  if (!ok) { LittleFS.remove(FS("/~save.zip")); return false; }
  LittleFS.remove(path);
  return LittleFS.rename(FS("/~save.zip"), path);
}

// ================= SHEET (an Excel 95 look-alike: A..Z x 60 rows, formulas, formats, charts) =================
// Cells are stored sparsely (up to XL_MAX filled cells), so the grid can be big while RAM stays small.
// A cell holds a number, text, or a formula starting with "=" (=A1*2, =SUM(A1:A5), =IF(B2>10,1,0)...).
// Saved as /sheets/<name>.csv (opens in Excel / LibreOffice), <name>.fmt (formats, widths) and
// <name>.val (numbers, one "col row value" per line, for charts in Word) - or as a real Excel workbook,
// /sheets/<name>.xlsx (File > Save as Excel .xlsx, or opening one): then Save keeps it an .xlsx.
const int XL_COLS = 26, XL_ROWS = 60, XL_MAX = 118, XL_RH = 12, XL_GX = 24, XL_GY = 70, XL_VR = 12, XL_GW = 284;
enum { XLM_GRID, XLM_ASK, XLM_PICK, XLM_CHART, XLM_FN, XLM_CTYPE };
enum { XQ_EDIT, XQ_SAVEAS, XQ_GOTO, XQ_FIND, XQ_SAVEX, XQ_REPL1, XQ_REPL2 };
enum { XF_GEN, XF_INT, XF_DEC2, XF_THOU, XF_CUR, XF_PCT, XF_SCI, XF_DATE };
const int XB_BOLD = 8, XB_ITAL = 16;                 // fmt bits; bits 5-6 = align (0 auto, 1 left, 2 centre, 3 right)
char shName[24] = "", xlFind[24] = "";
int xlCur = 0, xlTop = 0, xlLeft = 0, xlMode = XLM_GRID, xlAsk = XQ_EDIT, xlAnchor = -1, xlChart = 0, xlChartCol = 0;
bool shDown = false, shDirty = false, xlFormulas = false, xlGrid = true, xlSel = false;
bool shXlsx = false, xlNegRed = false;               // this workbook is an .xlsx; negative numbers in red
char xlClip[24] = ""; int xlClipFrom = -1;
const char XL_TITLES_P[] PROGMEM = "File\nEdit\nView\nInsert\nFormat\nTools\nData\nHelp";
const char XL_M0_P[] PROGMEM = "New\nOpen...\nSave\nSave As...\nSave as Excel .xlsx...\n-\nProperties\n-\nClose";
const char XL_M1_P[] PROGMEM = "Edit cell\nClear\n-\nCopy\nPaste\n-\nFill Down\nFill Right\n-\nSelect range\nGo To...\nFind...\nReplace...";
const char XL_M2_P[] PROGMEM = "Move down (not across)\nShow formulas\nGridlines";
const char XL_M3_P[] PROGMEM = "Row above\nColumn left\nDelete row\nDelete column\n-\nFunction...\nChart...";
const char XL_M4_P[] PROGMEM = "General\nNumber 0\nNumber 0.00\nThousands 1,000\nCurrency $\nPercent %\nScientific\n-\nBold\nItalic\n-\nAlign left\nCenter\nAlign right\n-\nColumn wider\nColumn narrower\n-\nDate 2024-12-31\nNegatives in red";
const char XL_M5_P[] PROGMEM = "AutoSum\nRecalculate now\nClear whole sheet";
const char XL_M6_P[] PROGMEM = "Sort ascending\nSort descending\nFill series 1,2,3...\nStatistics...\nRemove duplicates";
const char XL_M7_P[] PROGMEM = "Functions\nAbout Excel";
const char* const XL_MENUS[8] = {XL_M0_P, XL_M1_P, XL_M2_P, XL_M3_P, XL_M4_P, XL_M5_P, XL_M6_P, XL_M7_P};
const uint8_t XL_TOOLS[] = {IC_TYPE, 255, IC_NEW, IC_OPEN, IC_SAVE, 255, IC_COPY, IC_PASTE, 255, IC_SUM, IC_FX, IC_SORTA, IC_SORTZ, IC_CHART, 255,
                            IC_CUR, IC_PCT, IC_COMMA, 255, IC_B, 254};
const char XL_FNS_P[] PROGMEM = "SUM\nAVERAGE\nMIN\nMAX\nCOUNT\nMEDIAN\nSTDEV\nIF\nROUND\nABS\nSQRT\nTODAY (date)";

uint16_t shAccent() { return RGB565(0, 128, 0); }
// ---------- cells ----------
int xlFindCell(int pos) { for (int i = 0; i < WK.xl.n; i++) if (WK.xl.pos[i] == pos) return i; return -1; }
const char* xlRaw(int pos) { int i = xlFindCell(pos); return i < 0 ? "" : WK.xl.raw[i]; }
uint8_t xlFmt(int pos) { int i = xlFindCell(pos); return i < 0 ? 0 : WK.xl.fmt[i]; }
bool xlSet(int pos, const char* raw) {               // "" deletes the cell
  int i = xlFindCell(pos);
  if (!raw[0]) {
    if (i >= 0) { int last = --WK.xl.n; WK.xl.pos[i] = WK.xl.pos[last]; memcpy(WK.xl.raw[i], WK.xl.raw[last], 24); WK.xl.val[i] = WK.xl.val[last]; WK.xl.fmt[i] = WK.xl.fmt[last]; }
    return true;
  }
  if (i < 0) {
    if (WK.xl.n >= XL_MAX) return false;
    i = WK.xl.n++; WK.xl.pos[i] = pos; WK.xl.fmt[i] = 0;
  }
  snprintf_P(WK.xl.raw[i], 24, PSTR("%.23s"), raw);
  return true;
}
void xlSetFmt(int pos, uint8_t f) { int i = xlFindCell(pos); if (i >= 0) WK.xl.fmt[i] = f; }
bool shCellNum(int c, int r, double* v) {
  int i = xlFindCell(r * XL_COLS + c);
  if (i < 0 || isnan(WK.xl.val[i])) return false;
  *v = WK.xl.val[i];
  return true;
}
bool shVar(const char* n, int len, double* v) {      // "B3" style cell names
  const char* p = n;
  int c, r;
  if (!exCellRef(p, &c, &r) || p != n + len || c >= XL_COLS || r < 0 || r >= XL_ROWS) return false;
  if (!shCellNum(c, r, v)) *v = 0;                   // empty / text cells count as 0
  return true;
}
bool shIsNumber(const char* s, float* out) {
  char* end;
  double v = strtod(s, &end);
  if (end == s) return false;
  while (*end == ' ') end++;
  if (*end == '%' && !end[1]) { *out = v / 100; return true; }
  if (*end) return false;
  *out = v;
  return true;
}
void shRecalc() {
  // plain numbers first, then formulas in a few passes so chains like A1 -> B1 -> C1 settle
  // (passes instead of recursion keep the ESP8266's 4 KB stack safe)
  for (int i = 0; i < WK.xl.n; i++) {
    float f;
    const char* s = WK.xl.raw[i];
    WK.xl.val[i] = (s[0] && s[0] != '=' && shIsNumber(s, &f)) ? f : (s[0] == '=' ? 0 : NAN);
  }
  exVar = shVar; exCell = shCellNum;
  for (int pass = 0; pass < 8; pass++) {
    bool changed = false;
    for (int i = 0; i < WK.xl.n; i++) {
      if (WK.xl.raw[i][0] != '=') continue;
      double v = exEval(WK.xl.raw[i] + 1);
      float nv = exErr ? NAN : (float)v;
      if (!(nv == WK.xl.val[i] || (isnan(nv) && isnan(WK.xl.val[i])))) { WK.xl.val[i] = nv; changed = true; }
    }
    if (!changed) break;
    yield();
  }
  exCell = nullptr;
}
void xlThousands(double v, int dec, char* b, size_t n) {   // 1234567.5 -> 1,234,567.50
  char t[24];
  snprintf_P(t, sizeof t, PSTR("%.*f"), dec, fabs(v));
  char* dot = strchr(t, '.');
  int ilen = dot ? dot - t : strlen(t), o = 0;
  if (v < 0 && o < (int)n - 1) b[o++] = '-';
  for (int i = 0; i < ilen && o < (int)n - 1; i++) { b[o++] = t[i]; if ((ilen - i - 1) % 3 == 0 && i < ilen - 1 && o < (int)n - 1) b[o++] = ','; }
  for (const char* p = dot; p && *p && o < (int)n - 1; p++) b[o++] = *p;
  b[o] = 0;
}
void xlFmtVal(double v, uint8_t f, char* b, size_t n) {
  switch (f & 7) {
    case XF_INT: snprintf_P(b, n, PSTR("%.0f"), v); break;
    case XF_DEC2: snprintf_P(b, n, PSTR("%.2f"), v); break;
    case XF_THOU: xlThousands(v, v == floor(v) ? 0 : 2, b, n); break;
    case XF_CUR: { char t[24]; xlThousands(v, 2, t, sizeof t); snprintf_P(b, n, v < 0 ? PSTR("-$%s") : PSTR("$%s"), t + (v < 0)); break; }
    case XF_PCT: snprintf_P(b, n, v * 100 == floor(v * 100) ? PSTR("%.0f%%") : PSTR("%.1f%%"), v * 100); break;
    case XF_SCI: snprintf_P(b, n, PSTR("%.2E"), v); break;
    case XF_DATE:                                    // Excel's day numbers: 1 = 1900-01-01, 25569 = 1970-01-01
      if (v >= 25569 && v < 2958466) { int y, m, d; civilFromDays((uint32_t)(floor(v) - 25569), &y, &m, &d); snprintf_P(b, n, PSTR("%04d-%02d-%02d"), y, m, d); break; }
      fmtNum(v, b, n, 7); break;
    default: fmtNum(v, b, n, 7); if (strlen(b) > 10) snprintf_P(b, n, PSTR("%.3g"), v);
  }
}
void shCellText(int pos, char* b, size_t n, bool* right) {   // what the cell shows
  int i = xlFindCell(pos);
  *right = false;
  b[0] = 0;
  if (i < 0) return;
  const char* s = WK.xl.raw[i];
  float v = WK.xl.val[i];
  if (xlFormulas) { snprintf_P(b, n, PSTR("%s"), s); return; }
  if (s[0] == '=' && isnan(v)) { snprintf_P(b, n, PSTR("#ERR")); *right = true; return; }
  if (isnan(v)) { snprintf_P(b, n, PSTR("%s"), s); return; }
  *right = true;
  xlFmtVal(v, WK.xl.fmt[i], b, n);
}
// ---------- drawing ----------
int xlColX(int c) {                                  // screen x of column c (-1 if left of the view)
  if (c < xlLeft) return -1;
  int x = XL_GX;
  for (int k = xlLeft; k < c; k++) x += WK.xl.colw[k];
  return x;
}
bool xlInSel(int pos) {
  if (!xlSel || xlAnchor < 0) return false;
  int c = pos % XL_COLS, r = pos / XL_COLS, ac = xlAnchor % XL_COLS, ar = xlAnchor / XL_COLS, cc = xlCur % XL_COLS, cr = xlCur / XL_COLS;
  return c >= min(ac, cc) && c <= max(ac, cc) && r >= min(ar, cr) && r <= max(ar, cr);
}
void shDrawCell(int pos) {
  int c = pos % XL_COLS, r = pos / XL_COLS;
  if (r < xlTop || r >= xlTop + XL_VR) return;
  int x = xlColX(c);
  if (x < 0) return;
  int w = WK.xl.colw[c];
  if (x >= XL_GX + XL_GW) return;
  w = min(w, XL_GX + XL_GW - x);
  int y = XL_GY + (r - xlTop) * XL_RH;
  bool cur = pos == xlCur, sel = xlInSel(pos) && !cur, right;
  uint8_t f = xlFmt(pos);
  uint16_t bg = sel ? W95_NAVY : W95_PAPER, fg = sel ? W95_HI : W95_INK;
  char b[40];
  shCellText(pos, b, sizeof b, &right);
  tft.fillRect(x, y, w, XL_RH, bg);
  if (xlGrid) { tft.drawFastVLine(x + w - 1, y, XL_RH, W95_FACE); tft.drawFastHLine(x, y + XL_RH - 1, w, W95_FACE); }
  int maxc = max(0, (w - 4) / 6);
  if ((int)strlen(b) > maxc) { if (right && maxc > 0) { memset(b, '#', maxc); b[maxc] = 0; } else b[maxc] = 0; }
  int al = (f >> 5) & 3;
  int tx = al == 1 ? x + 2 : al == 2 ? x + (w - (int)strlen(b) * 6) / 2 : (al == 3 || (al == 0 && right)) ? x + w - 3 - (int)strlen(b) * 6 : x + 2;
  if (!strcmp(b, FS("#ERR"))) fg = sel ? W95_HI : W95_RED;
  if (xlNegRed && !sel && right) { int ci = xlFindCell(pos); if (ci >= 0 && WK.xl.val[ci] < 0) fg = W95_RED; }
  if (f & XB_ITAL) {
    for (int k = 0; b[k]; k++) wdItalic(tx + k * 6, y + 2, b[k], 1, fg, f & XB_BOLD);
  } else {
    putText(tx, y + 2, 1, fg, bg, b);
    if (f & XB_BOLD) { tft.setTextColor(fg); tft.setCursor(tx + 1, y + 2); tft.print(b); }
  }
  if (cur) {                                         // Excel's thick frame + the fill handle
    uint16_t fr = w95New ? W95_NAVY : W95_INK;
    tft.drawRect(x, y, w, XL_RH, fr); tft.drawRect(x + 1, y + 1, w - 2, XL_RH - 2, fr);
    tft.fillRect(x + w - 3, y + XL_RH - 3, 4, 4, fr);
  }
}
void xlDrawHeaders() {
  tft.fillRect(0, 58, W, 12, W95_FACE);
  tft.fillRect(0, 58, XL_GX, 12, W95_FACE); w95Bevel(0, 58, XL_GX, 12, true);
  int cc = xlCur % XL_COLS;
  for (int c = xlLeft, x = XL_GX; c < XL_COLS && x < XL_GX + XL_GW; x += WK.xl.colw[c], c++) {
    int w = min((int)WK.xl.colw[c], XL_GX + XL_GW - x);
    tft.fillRect(x, 58, w, 12, W95_FACE); w95Bevel(x, 58, w, 12, true);
    char h[2] = {(char)('A' + c), 0};
    putText(x + w / 2 - 3, 60, 1, W95_INK, W95_FACE, h);
    if (c == cc) { tft.setTextColor(W95_INK); tft.setCursor(x + w / 2 - 2, 60); tft.print(h); }
  }
}
void shDrawGrid() {
  xlDrawHeaders();
  int cr = xlCur / XL_COLS;
  for (int s = 0; s < XL_VR; s++) {
    int r = xlTop + s, y = XL_GY + s * XL_RH;
    tft.fillRect(0, y, XL_GX, XL_RH, W95_FACE); w95Bevel(0, y, XL_GX, XL_RH, true);
    char b[4]; snprintf_P(b, sizeof b, PSTR("%d"), (r + 1) % 100);
    putText(XL_GX - 3 - (int)strlen(b) * 6, y + 2, 1, W95_INK, W95_FACE, b);
    if (r == cr) { tft.setTextColor(W95_INK); tft.setCursor(XL_GX - 2 - (int)strlen(b) * 6, y + 2); tft.print(b); }
    int lastX = XL_GX;
    for (int c = xlLeft; c < XL_COLS && xlColX(c) < XL_GX + XL_GW; c++) { shDrawCell(r * XL_COLS + c); lastX = xlColX(c) + WK.xl.colw[c]; }
    if (lastX < XL_GX + XL_GW) tft.fillRect(lastX, y, XL_GX + XL_GW - lastX, XL_RH, W95_PAPER);
  }
  w95ScrollV(308, XL_GY, XL_VR * XL_RH, xlTop, XL_ROWS, XL_VR);
}
void shDrawFormulaBar() {
  tft.fillRect(0, 44, W, 14, W95_FACE);
  char nb[12];
  int c = xlCur % XL_COLS, r = xlCur / XL_COLS;
  if (xlSel && xlAnchor >= 0) snprintf_P(nb, sizeof nb, PSTR("%c%d:%c%d"), 'A' + min(c, xlAnchor % XL_COLS), min(r, xlAnchor / XL_COLS) + 1, 'A' + max(c, xlAnchor % XL_COLS), max(r, xlAnchor / XL_COLS) + 1);
  else snprintf_P(nb, sizeof nb, PSTR("%c%d"), 'A' + c, r + 1);
  w95Field(1, 44, 56, 14);
  putText(5, 47, 1, W95_INK, W95_PAPER, nb);
  tft.fillRect(60, 46, 14, 11, W95_FACE); w95Bevel(60, 45, 14, 12, true); putText(64, 47, 1, W95_INK, W95_FACE, FS("="));
  w95Field(76, 44, 243, 14);
  char b[40]; snprintf_P(b, sizeof b, PSTR("%.38s"), xlRaw(xlCur));
  putText(80, 47, 1, W95_INK, W95_PAPER, b);
}
void xlSheetTabs() {
  tft.fillRect(0, 214, W, 12, W95_FACE);
  for (int k = 0; k < 4; k++) { int x = 2 + k * 11; tft.fillRect(x, 215, 10, 10, W95_FACE); w95Bevel(x, 215, 10, 10, true); tft.fillTriangle(x + (k < 2 ? 6 : 3), 217, x + (k < 2 ? 6 : 3), 223, x + (k < 2 ? 3 : 6), 220, W95_INK); }
  tft.fillRect(48, 214, 46, 11, W95_PAPER); tft.drawRect(48, 213, 46, 12, W95_INK);
  putText(53, 216, 1, W95_INK, W95_PAPER, FS("Sheet1"));
  tft.drawFastHLine(94, 214, 226, W95_INK);
  for (int k = 0; k < 2; k++) { int x = 100 + k * 50; putText(x, 216, 1, W95_SH, W95_FACE, k ? FS("Sheet3") : FS("Sheet2")); }
}
void shStatus() {
  char a[40], b[28];
  // AutoCalculate, like Excel's status bar: the sum of the selection (or of this column)
  double sum = 0; int cnt = 0;
  int c0 = xlCur % XL_COLS, c1 = c0, r0 = 0, r1 = XL_ROWS - 1;
  if (xlSel && xlAnchor >= 0) { c0 = min(c0, xlAnchor % XL_COLS); c1 = max(xlCur % XL_COLS, xlAnchor % XL_COLS); r0 = min(xlCur / XL_COLS, xlAnchor / XL_COLS); r1 = max(xlCur / XL_COLS, xlAnchor / XL_COLS); }
  for (int i = 0; i < WK.xl.n; i++) {
    int c = WK.xl.pos[i] % XL_COLS, r = WK.xl.pos[i] / XL_COLS;
    if (c >= c0 && c <= c1 && r >= r0 && r <= r1 && !isnan(WK.xl.val[i])) { sum += WK.xl.val[i]; cnt++; }
  }
  char s[20]; fmtNum(sum, s, sizeof s, 8);
  snprintf_P(a, sizeof a, PSTR("%s  %s"), xlSel ? FS("Select") : FS("Ready"), shDown ? FS("(moving down)") : FS("(moving across)"));
  snprintf_P(b, sizeof b, PSTR("Sum=%s  n=%d"), s, cnt);
  w95StatusBar(a, b);
}
void xlChrome() {
  char t[40];
  snprintf_P(t, sizeof t, PSTR("Excel - %s.%s%s"), shName[0] ? shName : FS("Book1"), shXlsx ? FS("XLSX") : FS("XLS"), shDirty ? FS(" *") : FS(""));
  for (char* p = t + 8; *p; p++) *p = toupper((uint8_t)*p);
  w95Title(t, W95_GRN, 'X');
  w95MenuBar();
  w95Toolbar();
}
void shDrawAll() {
  xlChrome();
  shDrawFormulaBar();
  shDrawGrid();
  xlSheetTabs();
  shStatus();
}
void xlRedrawCb() { shDrawAll(); }
bool xlOnCb(int cmd) {
  uint8_t f = xlFmt(xlCur);
  switch (cmd) {
    case 64 + 0: return shDown;
    case 64 + 1: return xlFormulas;
    case 64 + 2: return xlGrid;
    case 32 + 9: return xlSel;
    case 128 + 0: return (f & 7) == XF_GEN;
    case 128 + 1: return (f & 7) == XF_INT;
    case 128 + 2: return (f & 7) == XF_DEC2;
    case 128 + 3: case 1000 + 13: return (f & 7) == XF_THOU;
    case 128 + 4: case 1000 + 11: return (f & 7) == XF_CUR;
    case 128 + 5: case 1000 + 12: return (f & 7) == XF_PCT;
    case 128 + 6: return (f & 7) == XF_SCI;
    case 128 + 8: case 1000 + 14: return f & XB_BOLD;
    case 128 + 9: return f & XB_ITAL;
    case 128 + 11: return ((f >> 5) & 3) == 1;
    case 128 + 12: return ((f >> 5) & 3) == 2;
    case 128 + 13: return ((f >> 5) & 3) == 3;
    case 128 + 18: return (f & 7) == XF_DATE;
    case 128 + 19: return xlNegRed;
  }
  return false;
}
void xlUse() { w95Titles = XL_TITLES_P; w95Menus = XL_MENUS; w95Tools = XL_TOOLS; w95Redraw = xlRedrawCb; w95On = xlOnCb; w95Look(RGB565(33, 115, 70)); }
void xlScrollTo() {                                  // keep the active cell on screen
  int c = xlCur % XL_COLS, r = xlCur / XL_COLS;
  if (r < xlTop) xlTop = r; else if (r >= xlTop + XL_VR) xlTop = r - XL_VR + 1;
  if (c < xlLeft) xlLeft = c;
  while (xlColX(c) + WK.xl.colw[c] > XL_GX + XL_GW && xlLeft < c) xlLeft++;
}
void shMove(int c) {
  int old = xlCur, oldTop = xlTop, oldLeft = xlLeft;
  int d = c > 0 ? 1 : -1, steps = abs(c) > 2 ? abs(c) * 2 : abs(c);   // a fast spin jumps further
  for (int k = 0; k < steps; k++) {
    int col = xlCur % XL_COLS, row = xlCur / XL_COLS;
    if (shDown) row = constrain(row + d, 0, XL_ROWS - 1);
    else { col += d; if (col >= XL_COLS) { col = 0; row = min(row + 1, XL_ROWS - 1); } if (col < 0) { col = XL_COLS - 1; row = max(row - 1, 0); } }
    xlCur = row * XL_COLS + col;
  }
  xlScrollTo();
  if (xlTop != oldTop || xlLeft != oldLeft || xlSel) shDrawGrid();
  else { shDrawCell(old); shDrawCell(xlCur); xlDrawHeaders(); }
  if (!xlSel && xlTop == oldTop && xlLeft == oldLeft) {      // row numbers: un-bold the old row, bold the new
    int y0 = XL_GY + (old / XL_COLS - xlTop) * XL_RH, y1 = XL_GY + (xlCur / XL_COLS - xlTop) * XL_RH;
    for (int y : {y0, y1}) {
      int r = xlTop + (y - XL_GY) / XL_RH;
      tft.fillRect(0, y, XL_GX, XL_RH, W95_FACE); w95Bevel(0, y, XL_GX, XL_RH, true);
      char b[4]; snprintf_P(b, sizeof b, PSTR("%d"), (r + 1) % 100);
      putText(XL_GX - 3 - (int)strlen(b) * 6, y + 2, 1, W95_INK, W95_FACE, b);
      if (r == xlCur / XL_COLS) { tft.setTextColor(W95_INK); tft.setCursor(XL_GX - 2 - (int)strlen(b) * 6, y + 2); tft.print(b); }
    }
  }
  shDrawFormulaBar();
  shStatus();
}
// ---------- references: copy a formula somewhere else and its cell names follow (A1 -> A2) ----------
void xlShift(const char* src, char* out, size_t n, int dc, int dr, int atCol, int atRow, int insC, int insR) {
  // dc/dr: plain offset (copy, fill). insC/insR: insert (+1) / delete (-1) a column/row at atCol/atRow.
  size_t o = 0;
  for (int i = 0; src[i] && o < n - 1; ) {
    char ch = src[i];
    bool start = isalpha((uint8_t)ch) && (i == 0 || !isalnum((uint8_t)src[i - 1])) && isdigit((uint8_t)src[i + 1]);
    if (start) {
      int j = i + 1, row = 0;
      while (isdigit((uint8_t)src[j])) row = row * 10 + (src[j++] - '0');
      if (!isalnum((uint8_t)src[j]) && src[j] != '(') {
        int c = toupper((uint8_t)ch) - 'A', r = row - 1;
        c += dc; r += dr;
        if (insC && c >= atCol) c += insC;
        if (insR && r >= atRow) r += insR;
        c = constrain(c, 0, XL_COLS - 1); r = constrain(r, 0, XL_ROWS - 1);
        o += snprintf_P(out + o, n - o, PSTR("%c%d"), 'A' + c, r + 1);
        i = j;
        continue;
      }
    }
    out[o++] = ch; i++;
  }
  out[min(o, n - 1)] = 0;
}
void xlCopyCell(int from, int to) {                   // raw + format, formulas shifted
  const char* s = xlRaw(from);
  char b[24];
  if (s[0] == '=') xlShift(s, b, sizeof b, to % XL_COLS - from % XL_COLS, to / XL_COLS - from / XL_COLS, 0, 0, 0, 0);
  else snprintf_P(b, sizeof b, PSTR("%s"), s);
  uint8_t f = xlFmt(from);
  if (xlSet(to, b)) xlSetFmt(to, f);
}
void xlInsertDelete(bool col, int at, int dir) {      // dir +1 insert before 'at', -1 delete 'at'
  for (int i = 0; i < WK.xl.n; ) {
    int c = WK.xl.pos[i] % XL_COLS, r = WK.xl.pos[i] / XL_COLS, k = col ? c : r;
    if (dir < 0 && k == at) { xlSet(WK.xl.pos[i], FS("")); continue; }
    if (k >= at) { if (col) c += dir; else r += dir; }
    if (c >= XL_COLS || r >= XL_ROWS) { xlSet(WK.xl.pos[i], FS("")); continue; }
    WK.xl.pos[i] = r * XL_COLS + c;
    i++;
  }
  for (int i = 0; i < WK.xl.n; i++) if (WK.xl.raw[i][0] == '=') {
    char b[24];
    xlShift(WK.xl.raw[i], b, sizeof b, 0, 0, col ? at : 0, col ? 0 : at, col ? dir : 0, col ? 0 : dir);
    memcpy(WK.xl.raw[i], b, 24);
  }
  if (col) { if (dir > 0) { memmove(WK.xl.colw + at + 1, WK.xl.colw + at, XL_COLS - at - 1); WK.xl.colw[at] = 48; } else { memmove(WK.xl.colw + at, WK.xl.colw + at + 1, XL_COLS - at - 1); WK.xl.colw[XL_COLS - 1] = 48; } }
  shDirty = true;
}
// ---------- files ----------
void shClear() {
  memset(&WK.xl, 0, sizeof WK.xl);
  for (int c = 0; c < XL_COLS; c++) WK.xl.colw[c] = 48;
  xlCur = 0; xlTop = 0; xlLeft = 0; shDirty = false; xlSel = false; xlAnchor = -1;
}
void shSaveVal() {                                   // the numbers alone, for charts in Word
  char p[40];
  snprintf_P(p, sizeof p, PSTR("/sheets/%s.val"), shName);
  File f = LittleFS.open(p, FS("w"));
  if (!f) return;
  for (int r = 0; r < XL_ROWS; r++) for (int c = 0; c < XL_COLS; c++) {
    double v;
    if (shCellNum(c, r, &v)) { char b[48]; snprintf_P(b, sizeof b, PSTR("%d %d %.6g\n"), c, r, v); f.print(b); }
  }
  f.close();
}
bool xlsxExport(const char* path);
bool shSave() {
  if (!fsOk) return false;
  LittleFS.mkdir(FS("/sheets"));
  if (shXlsx) {                                      // a real Excel workbook (+ the numbers for Word's charts)
    char p[40]; snprintf_P(p, sizeof p, PSTR("/sheets/%s.xlsx"), shName);
    bool ok = xlsxExport(p);
    shSaveVal();
    if (ok) shDirty = false;
    return ok;
  }
  int maxR = -1, maxC = -1;
  for (int i = 0; i < WK.xl.n; i++) { maxR = max(maxR, WK.xl.pos[i] / XL_COLS); maxC = max(maxC, WK.xl.pos[i] % XL_COLS); }
  char p[40];
  snprintf_P(p, sizeof p, PSTR("/sheets/%s.csv"), shName);
  File f = LittleFS.open(p, FS("w"));
  if (!f) return false;
  for (int r = 0; r <= maxR; r++) {
    for (int c = 0; c <= maxC; c++) {
      const char* s = xlRaw(r * XL_COLS + c);
      if (c) f.write(',');
      if (strchr(s, ',') || strchr(s, '"')) { f.write('"'); for (; *s; s++) { if (*s == '"') f.write('"'); f.write(*s); } f.write('"'); }
      else f.print(s);
    }
    f.write('\n');
  }
  f.close();
  snprintf_P(p, sizeof p, PSTR("/sheets/%s.fmt"), shName);
  f = LittleFS.open(p, FS("w"));
  if (f) {
    f.print(FS("w"));
    for (int c = 0; c < XL_COLS; c++) { f.write(' '); f.print(WK.xl.colw[c]); }
    f.write('\n');
    for (int i = 0; i < WK.xl.n; i++) if (WK.xl.fmt[i]) { f.print(WK.xl.pos[i]); f.write(' '); f.print(WK.xl.fmt[i]); f.write('\n'); }
    f.close();
  }
  shSaveVal();
  shDirty = false;
  return true;
}
void shLoad(const char* path) {
  shClear();
  baseName(path, shName, sizeof shName);
  File f = LittleFS.open(path, FS("r"));
  if (!f) return;
  int r = 0, c = 0, n = 0;
  bool q = false;
  char cell[24];
  auto put = [&]() { cell[n] = 0; if (n && r < XL_ROWS && c < XL_COLS) xlSet(r * XL_COLS + c, cell); n = 0; };
  while (f.available() && r < XL_ROWS) {
    char ch = f.read();
    if (q) {
      if (ch == '"') { if (f.peek() == '"') { f.read(); if (n < 23) cell[n++] = '"'; } else q = false; }
      else if (n < 23) cell[n++] = ch;
    } else if (ch == '"') q = true;
    else if (ch == ',') { put(); c++; }
    else if (ch == '\n') { put(); r++; c = 0; }
    else if (ch != '\r' && n < 23) cell[n++] = ch;
  }
  put();
  f.close();
  char p[40]; snprintf_P(p, sizeof p, PSTR("/sheets/%s.fmt"), shName);
  f = LittleFS.open(p, FS("r"));
  if (f) {
    f.setTimeout(0);                                 // parseInt must not wait at the end of the file
    String l = f.readStringUntil('\n');
    const char* s = l.c_str();
    if (s[0] == 'w') { s++; for (int k = 0; k < XL_COLS && *s; k++) { int w = strtol(s, (char**)&s, 10); WK.xl.colw[k] = constrain(w, 24, 120); } }
    while (f.available()) { int pos = f.parseInt(), fm = f.parseInt(); xlSetFmt(pos, fm); }
    f.close();
  }
  shRecalc();
}
// ---------- Excel .xlsx in ----------
// What fits the Psion comes in: the first sheet's A1:Z60 (118 filled cells at most), number formats, dates,
// bold / italic, alignment and column widths. A formula the Psion can't work out (another sheet, a function it
// doesn't have, too long for a cell) keeps Excel's last result as a plain value.
uint16_t xlImpDropped = 0;
char xlImpSheet[24] = "";
uint8_t xlNumClass(int id) {                         // Excel's built-in number formats -> the Psion's
  for (int i = 0; i < Z->u.x.nnf; i++) if (Z->u.x.nfId[i] == id) return Z->u.x.nfCls[i];
  if (id == 1) return XF_INT;
  if (id == 2) return XF_DEC2;
  if (id == 3 || id == 4 || (id >= 37 && id <= 40)) return XF_THOU;
  if ((id >= 5 && id <= 8) || id == 42 || id == 44) return XF_CUR;
  if (id == 9 || id == 10) return XF_PCT;
  if (id == 11 || id == 48) return XF_SCI;
  if ((id >= 14 && id <= 17) || id == 22 || (id >= 27 && id <= 36) || (id >= 50 && id <= 58)) return XF_DATE;
  return XF_GEN;
}
uint8_t xlFmtClass(const char* c) {                  // a custom format code ("$#,##0.00", "dd/mm/yyyy") -> the nearest
  bool q = false, date = false, digit = false, cur = false;
  for (const char* p = c; *p; p++) {
    char ch = *p;
    if (ch == '"') { q = !q; continue; }
    if (q) { if (ch == '$') cur = true; continue; }
    if (ch == '[') { if (p[1] == '$' && p[2] != '-') cur = true; while (*p && *p != ']') p++; if (!*p) break; continue; }
    if (ch == '\\' || ch == '_' || ch == '*') { if (!*++p) break; continue; }
    if (ch == '%') return XF_PCT;
    if ((ch == 'E' || ch == 'e') && (p[1] == '+' || p[1] == '-')) return XF_SCI;
    if (ch == '$') cur = true;
    if (ch == 'd' || ch == 'D' || ch == 'y' || ch == 'Y') date = true;
    if (ch == '0' || ch == '#') digit = true;
  }
  if (cur || strstr_P(c, PSTR("\xE2\x82\xAC")) || strstr_P(c, PSTR("\xC2\xA3"))) return XF_CUR;   // $, euro, pound
  if (date && !digit) return XF_DATE;
  if (strstr_P(c, PSTR("#,##"))) return XF_THOU;
  if (strstr_P(c, PSTR("0.00"))) return XF_DEC2;
  if (!strcmp_P(c, PSTR("0"))) return XF_INT;
  return XF_GEN;
}
bool xlRef(const char* s, int* c, int* r) {          // "AB12" -> column 27, row 11
  int col = 0, row = 0;
  if (!isalpha((uint8_t)*s)) return false;
  while (isalpha((uint8_t)*s)) col = col * 26 + (toupper((uint8_t)*s++) - 'A' + 1);
  while (isdigit((uint8_t)*s)) row = row * 10 + (*s++ - '0');
  if (!row || col > 16384) return false;
  *c = col - 1; *r = row - 1;
  return true;
}
void xwbStart(const char* n, const char* a, bool) {  // workbook.xml: the first sheet
  if (!Z->u.x.rid[0] && !strcmp_P(n, PSTR("sheet"))) { xAttr(a, "id", Z->u.x.rid, sizeof Z->u.x.rid); xAttr(a, "name", Z->u.x.sname, sizeof Z->u.x.sname); }
}
void xrelStart(const char* n, const char* a, bool) { // its file
  char id[12];
  if (!strcmp_P(n, PSTR("Relationship")) && xAttr(a, "Id", id, sizeof id) && !strcmp(id, Z->u.x.rid)) xAttr(a, "Target", Z->u.x.target, sizeof Z->u.x.target);
}
void xstStart(const char* n, const char* a, bool) {  // styles.xml: number formats, fonts, cell formats
  auto& X = Z->u.x;
  char v[48];
  if (!strcmp_P(n, PSTR("numFmt"))) {
    if (X.nnf < 24) { X.nfId[X.nnf] = xAttrInt(a, "numFmtId", 0); X.nfCls[X.nnf] = xAttr(a, "formatCode", v, sizeof v) ? xlFmtClass(v) : (uint8_t)XF_GEN; X.nnf++; }
  } else if (!strcmp_P(n, PSTR("fonts"))) X.sect = 1;
  else if (!strcmp_P(n, PSTR("cellXfs"))) X.sect = 2;
  else if (X.sect == 1 && !strcmp_P(n, PSTR("font"))) { if (X.nfont < 32) X.font[X.nfont] = 0; X.nfont++; }
  else if (X.sect == 1 && X.nfont && X.nfont <= 32 && (!strcmp_P(n, PSTR("b")) || !strcmp_P(n, PSTR("i"))) && xOn(a))
    X.font[X.nfont - 1] |= n[0] == 'b' ? XB_BOLD : XB_ITAL;
  else if (X.sect == 2 && !strcmp_P(n, PSTR("xf"))) {
    int fo = xAttrInt(a, "fontId", 0);
    uint8_t f = xlNumClass(xAttrInt(a, "numFmtId", 0)) | ((fo >= 0 && fo < 32 && fo < X.nfont) ? X.font[fo] : 0);
    if (X.nxf < 64) X.xf[X.nxf] = f;
    X.nxf++;
  } else if (X.sect == 2 && !strcmp_P(n, PSTR("alignment")) && X.nxf && X.nxf <= 64 && xAttr(a, "horizontal", v, sizeof v)) {
    int al = !strcmp_P(v, PSTR("left")) ? 1 : (!strcmp_P(v, PSTR("center")) || !strcmp_P(v, PSTR("centerContinuous"))) ? 2 : !strcmp_P(v, PSTR("right")) ? 3 : 0;
    X.xf[X.nxf - 1] = (X.xf[X.nxf - 1] & ~0x60) | al << 5;
  }
}
void xstEnd(const char* n) { if (!strcmp_P(n, PSTR("fonts")) || !strcmp_P(n, PSTR("cellXfs"))) Z->u.x.sect = 0; }
void xlImpCell() {                                   // one <c> of the sheet is complete
  auto& X = Z->u.x;
  X.v[X.vn] = 0; X.f[X.fn] = 0;
  if (X.row < 0 || X.col < 0) return;
  if (X.row >= XL_ROWS || X.col >= XL_COLS) { if (X.vn || X.fn) X.dropped++; return; }
  if (X.fSi >= 0) {                                  // a shared formula: the first cell has the text, the others copy it
    if (X.fn) {
      if (X.nsh < 6 && X.fn <= 23) { snprintf_P(X.shf[X.nsh], 24, PSTR("%s"), X.f); X.shPos[X.nsh] = X.row * XL_COLS + X.col; X.shSi[X.nsh] = X.fSi; X.nsh++; }
    } else for (int k = 0; k < X.nsh; k++) if (X.shSi[k] == X.fSi) {
      int p = X.shPos[k];
      xlShift(X.shf[k], X.f, sizeof X.f, X.col - p % XL_COLS, X.row - p / XL_COLS, 0, 0, 0, 0);
      X.fn = strlen(X.f);
      break;
    }
  }
  char raw[24] = "";
  float cached = NAN;
  if (X.t == 1) { raw[0] = 1; raw[1] = 0; }          // a shared string: filled in from sharedStrings.xml next
  else if (X.t == 2 || X.t == 3 || X.t == 5) snprintf_P(raw, sizeof raw, PSTR("%.23s"), X.v);   // text, a text result, #N/A
  else if (X.t == 4) strcpy_P(raw, X.v[0] == '1' ? PSTR("TRUE") : PSTR("FALSE"));
  else {
    if (!X.vn && !X.fn) return;                      // a cell that only has a format
    if (X.vn) cached = atof(X.v);
    if (X.fn) {                                      // keep the formula if it fits: $A$1 -> A1, no _xlfn. prefixes
      char g[64]; int o = 0;
      for (const char* p = X.f; *p && o < 62; p++) {
        if (*p == '$') continue;
        if (!strncmp_P(p, PSTR("_xlfn."), 6)) { p += 5; continue; }
        g[o++] = *p;
      }
      g[o] = 0;
      if (o <= 22) snprintf_P(raw, sizeof raw, PSTR("=%s"), g);
    }
    if (!raw[0] && X.vn) { if (X.vn <= 23) strcpy(raw, X.v); else fmtNum(cached, raw, sizeof raw, 10); }
  }
  if (!raw[0]) return;
  int pos = X.row * XL_COLS + X.col;
  if (xlFindCell(pos) < 0 && WK.xl.n >= XL_MAX) { X.dropped++; Z->stop = true; return; }   // the workbook is full
  xlSet(pos, raw);
  xlSetFmt(pos, X.sfmt);
  int i = xlFindCell(pos);
  if (i >= 0) X.cache[i] = cached;
  if (X.t == 1 && X.npend < XL_MAX) {
    uint16_t idx = atoi(X.v);
    X.pend[X.npend][0] = idx; X.pend[X.npend][1] = pos; X.npend++;
    if (idx > X.maxIdx) X.maxIdx = idx;
  }
}
void xshStart(const char* n, const char* a, bool) {  // the sheet: <row r> <c r t s> <v> <f> <is><t> <col min max width>
  auto& X = Z->u.x;
  char v[16];
  if (!strcmp_P(n, PSTR("c"))) {
    X.col++;
    int c, r;
    if (xAttr(a, "r", v, sizeof v) && xlRef(v, &c, &r)) { X.col = c; X.row = r; }
    X.t = 0;
    if (xAttr(a, "t", v, sizeof v))
      X.t = !strcmp_P(v, PSTR("s")) ? 1 : !strcmp_P(v, PSTR("str")) ? 2 : !strcmp_P(v, PSTR("inlineStr")) ? 3 : !strcmp_P(v, PSTR("b")) ? 4 : !strcmp_P(v, PSTR("e")) ? 5 : !strcmp_P(v, PSTR("d")) ? 2 : 0;
    long s = xAttrInt(a, "s", 0);
    X.sfmt = (s >= 0 && s < 64 && s < X.nxf) ? X.xf[s] : 0;
    X.vn = X.fn = 0; X.fSi = -1; X.inV = X.inF = X.inIs = X.inT = false;
  } else if (!strcmp_P(n, PSTR("row"))) { long r = xAttrInt(a, "r", 0); X.row = r > 0 ? r - 1 : X.row + 1; X.col = -1; }
  else if (!strcmp_P(n, PSTR("v"))) X.inV = true;
  else if (!strcmp_P(n, PSTR("f"))) { X.inF = true; X.fSi = (xAttr(a, "t", v, sizeof v) && !strcmp_P(v, PSTR("shared"))) ? xAttrInt(a, "si", -1) : -1; }
  else if (!strcmp_P(n, PSTR("is"))) X.inIs = true;
  else if (!strcmp_P(n, PSTR("t"))) X.inT = X.inIs;
  else if (!strcmp_P(n, PSTR("col"))) {
    int mn = xAttrInt(a, "min", 1), mx = xAttrInt(a, "max", mn);
    char w[16];
    int px = (xAttr(a, "hidden", w, sizeof w) && (w[0] == '1' || w[0] == 't')) ? 24 : xAttr(a, "width", w, sizeof w) ? constrain((int)(atof(w) * 6 + 2), 24, 120) : 48;
    for (int c = max(1, mn); c <= min(mx, XL_COLS); c++) WK.xl.colw[c - 1] = px;
  }
}
void xshText(uint16_t u) {
  auto& X = Z->u.x;
  uint8_t c = uniCp(u);
  if (c == '\n' || c == '\r' || c == '\t') c = ' ';
  if (!c) return;
  if (X.inV || (X.inIs && X.inT)) { if (X.vn < sizeof X.v - 1) X.v[X.vn++] = c; }
  else if (X.inF) { if (X.fn < sizeof X.f - 1) X.f[X.fn++] = c; }
}
void xshEnd(const char* n) {
  auto& X = Z->u.x;
  if (!strcmp_P(n, PSTR("v"))) X.inV = false;
  else if (!strcmp_P(n, PSTR("f"))) X.inF = false;
  else if (!strcmp_P(n, PSTR("is"))) X.inIs = false;
  else if (!strcmp_P(n, PSTR("t"))) X.inT = false;
  else if (!strcmp_P(n, PSTR("c"))) xlImpCell();
}
void xssStart(const char* n, const char* a, bool) {  // sharedStrings.xml: <si> per string, its <t> parts
  (void)a;
  auto& X = Z->u.x;
  if (!strcmp_P(n, PSTR("si"))) {
    X.si++; X.svn = 0; X.want = false;
    for (int k = 0; k < X.npend && !X.want; k++) X.want = X.pend[k][0] == X.si;
  } else if (!strcmp_P(n, PSTR("t"))) X.inT = !X.skip;
  else if (!strcmp_P(n, PSTR("rPh"))) X.skip = 1;    // (phonetic hints: not part of the text)
}
void xssText(uint16_t u) {
  auto& X = Z->u.x;
  if (!X.inT || !X.want) return;
  uint8_t c = uniCp(u);
  if (c == '\n' || c == '\r' || c == '\t') c = ' ';
  if (c && X.svn < 23) X.sv[X.svn++] = c;
}
void xssEnd(const char* n) {
  auto& X = Z->u.x;
  if (!strcmp_P(n, PSTR("t"))) X.inT = false;
  else if (!strcmp_P(n, PSTR("rPh"))) X.skip = 0;
  else if (!strcmp_P(n, PSTR("si"))) {
    if (X.want) {
      X.sv[X.svn] = 0;
      for (int k = 0; k < X.npend; k++) if (X.pend[k][0] == X.si) { int i = xlFindCell(X.pend[k][1]); if (i >= 0) snprintf_P(WK.xl.raw[i], 24, PSTR("%s"), X.sv); }
    }
    if (X.si >= (int)X.maxIdx) Z->stop = true;       // every string the sheet needs has come
  }
}
int xlsxImport(const char* path) {                   // 0 ok, 1 not an Excel workbook, 2 not enough memory
  if (!zBegin()) return 2;
  auto& X = Z->u.x;
  shClear();
  X.row = X.col = -1; X.si = -1;
  for (int i = 0; i < XL_MAX; i++) X.cache[i] = NAN;
  xText = nullptr; xEnd = nullptr;
  xStart = xwbStart; zPartP(path, PSTR("xl/workbook.xml"));   // which file is the first sheet
  if (X.rid[0]) { xStart = xrelStart; zPartP(path, PSTR("xl/_rels/workbook.xml.rels")); }
  char part[64];
  const char* t = X.target;
  if (t[0] == '/') t++;
  if (!strncmp_P(t, PSTR("xl/"), 3)) t += 3;
  if (t[0]) snprintf_P(part, sizeof part, PSTR("xl/%s"), t); else strcpy_P(part, PSTR("xl/worksheets/sheet1.xml"));
  snprintf_P(xlImpSheet, sizeof xlImpSheet, PSTR("%s"), X.sname[0] ? X.sname : FS("Sheet1"));
  xStart = xstStart; xEnd = xstEnd; zPartP(path, PSTR("xl/styles.xml"));
  xStart = xshStart; xEnd = xshEnd; xText = xshText;
  int r = zPart(path, part);
  if (r == 1 && strcmp_P(part, PSTR("xl/worksheets/sheet1.xml"))) r = zPartP(path, PSTR("xl/worksheets/sheet1.xml"));
  if (r == 1 || r == 2) { zEnd(); shClear(); return r; }
  if (X.npend) { X.si = -1; X.skip = 0; xStart = xssStart; xEnd = xssEnd; xText = xssText; zPartP(path, PSTR("xl/sharedStrings.xml")); }
  for (int i = WK.xl.n - 1; i >= 0; i--)             // shared strings that never came, empty texts: gone
    if (!WK.xl.raw[i][0] || WK.xl.raw[i][0] == 1) { X.cache[i] = X.cache[WK.xl.n - 1]; xlSet(WK.xl.pos[i], FS("")); }
  shRecalc();
  bool again = false;
  for (int i = 0; i < WK.xl.n; i++)                  // formulas the Psion can't do: Excel's last result
    if (WK.xl.raw[i][0] == '=' && isnan(WK.xl.val[i]) && !isnan(X.cache[i])) { fmtNum(X.cache[i], WK.xl.raw[i], 24, 10); again = true; }
  if (again) shRecalc();
  xlImpDropped = X.dropped;
  zEnd();
  xStart = nullptr; xEnd = nullptr; xText = nullptr;
  return 0;
}
// ---------- Excel .xlsx out ----------
uint8_t xxFmtOf(int i) {                             // a cell's format ("50%" typed into a General cell = a percent)
  uint8_t f = WK.xl.fmt[i];
  const char* r = WK.xl.raw[i];
  size_t l = strlen(r);
  if ((f & 7) == XF_GEN && l && r[l - 1] == '%' && r[0] != '=') f |= XF_PCT;
  return f;
}
const uint16_t XX_NUMFMT[8] PROGMEM = {0, 1, 2, 3, 164, 10, 11, 165};   // the Psion's formats -> Excel number formats
void xxTypes() {
  zwP(PSTR("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
           "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"xml\" ContentType=\"application/xml\"/>"
           "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
           "<Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>"
           "<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/></Types>"));
}
void xxRels() {
  zwP(PSTR("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
           "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/></Relationships>"));
}
void xxBook() {
  zwP(PSTR("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
           "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><sheets><sheet name=\"Sheet1\" sheetId=\"1\" r:id=\"rId1\"/></sheets>"
           "<calcPr calcId=\"191029\" fullCalcOnLoad=\"1\"/></workbook>"));
}
void xxBookRels() {
  zwP(PSTR("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
           "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/>"
           "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/></Relationships>"));
}
void xxStyles() {
  auto& X = Z->u.x;
  zwP(PSTR("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
           "<numFmts count=\"2\"><numFmt numFmtId=\"164\" formatCode=\"&quot;$&quot;#,##0.00\"/><numFmt numFmtId=\"165\" formatCode=\"yyyy\\-mm\\-dd\"/></numFmts>"
           "<fonts count=\"4\"><font><sz val=\"11\"/><name val=\"Calibri\"/></font><font><b/><sz val=\"11\"/><name val=\"Calibri\"/></font>"
           "<font><i/><sz val=\"11\"/><name val=\"Calibri\"/></font><font><b/><i/><sz val=\"11\"/><name val=\"Calibri\"/></font></fonts>"
           "<fills count=\"2\"><fill><patternFill patternType=\"none\"/></fill><fill><patternFill patternType=\"gray125\"/></fill></fills>"
           "<borders count=\"1\"><border><left/><right/><top/><bottom/><diagonal/></border></borders>"
           "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs><cellXfs count=\""));
  zwNum(X.nxf + 1);
  zwP(PSTR("\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>"));
  for (int k = 0; k < X.nxf; k++) {
    uint8_t f = X.xf[k];
    int al = (f >> 5) & 3;
    zwP(PSTR("<xf numFmtId=\"")); zwNum(pgm_read_word(&XX_NUMFMT[f & 7]));
    zwP(PSTR("\" fontId=\"")); zwNum(((f & XB_BOLD) ? 1 : 0) + ((f & XB_ITAL) ? 2 : 0));
    zwP(PSTR("\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyNumberFormat=\"1\" applyFont=\"1\""));
    if (al) { zwP(PSTR(" applyAlignment=\"1\"><alignment horizontal=\"")); zwP(al == 1 ? PSTR("left") : al == 2 ? PSTR("center") : PSTR("right")); zwP(PSTR("\"/></xf>")); }
    else zwP(PSTR("/>"));
  }
  zwP(PSTR("</cellXfs><cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles></styleSheet>"));
}
void xxSheet() {
  auto& X = Z->u.x;
  zwP(PSTR("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
           "<sheetFormatPr defaultRowHeight=\"15\"/>"));
  bool cols = false;
  for (int c = 0; c < XL_COLS; c++) {
    bool date = false;                               // (Excel's default width shows a date as #####)
    for (int i = 0; i < WK.xl.n && !date; i++) date = WK.xl.pos[i] % XL_COLS == c && (WK.xl.fmt[i] & 7) == XF_DATE;
    if (WK.xl.colw[c] == 48 && !date) continue;
    if (!cols) { zwP(PSTR("<cols>")); cols = true; }
    char b[12]; dtostrf(WK.xl.colw[c] == 48 ? 11.0 : (WK.xl.colw[c] - 2) / 6.0, 1, 2, b);
    zwP(PSTR("<col min=\"")); zwNum(c + 1); zwP(PSTR("\" max=\"")); zwNum(c + 1); zwP(PSTR("\" width=\"")); zwStr(b); zwP(PSTR("\" customWidth=\"1\"/>"));
  }
  if (cols) zwP(PSTR("</cols>"));
  zwP(PSTR("<sheetData>"));
  int row = -1;
  for (int k = 0; k < WK.xl.n; k++) {
    int i = X.ord[k], pos = WK.xl.pos[i], r = pos / XL_COLS, c = pos % XL_COLS;
    if (r != row) { if (row >= 0) zwP(PSTR("</row>")); zwP(PSTR("<row r=\"")); zwNum(r + 1); zwP(PSTR("\">")); row = r; }
    char b[24];
    snprintf_P(b, sizeof b, PSTR("<c r=\"%c%d\""), 'A' + c, r + 1);
    zwStr(b);
    uint8_t f = xxFmtOf(i);
    for (int q = 0; f && q < X.nxf; q++) if (X.xf[q] == f) { zwP(PSTR(" s=\"")); zwNum(q + 1); zwByte('"'); break; }
    const char* raw = WK.xl.raw[i];
    float v = WK.xl.val[i];
    if (raw[0] == '=') {                             // a formula (+ its result; Excel works it out again anyway)
      char g[24];
      for (int q = 0; q < 23; q++) { g[q] = toupper((uint8_t)raw[q + 1]); if (!g[q]) break; }
      g[23] = 0;
      zwP(PSTR("><f>")); zwText(g, 23, false); zwP(PSTR("</f>"));
      if (!isnan(v) && !isinf(v)) { snprintf_P(b, sizeof b, PSTR("%.9g"), v); zwP(PSTR("<v>")); zwStr(b); zwP(PSTR("</v>")); }
      zwP(PSTR("</c>"));
    } else if (!isnan(v)) {                          // a number: as typed when it's a plain one ("1.5"), else the value
      char* end;
      strtod(raw, &end);
      if (*end || raw[0] == ' ' || raw[0] == '+') snprintf_P(b, sizeof b, PSTR("%.9g"), v); else snprintf_P(b, sizeof b, PSTR("%s"), raw);
      zwP(PSTR("><v>")); zwStr(b); zwP(PSTR("</v></c>"));
    } else {
      zwP(PSTR(" t=\"inlineStr\"><is><t xml:space=\"preserve\">")); zwText(raw, 23, false); zwP(PSTR("</t></is></c>"));
    }
  }
  if (row >= 0) zwP(PSTR("</row>"));
  zwP(PSTR("</sheetData></worksheet>"));
}
bool xlsxExport(const char* path) {
  shRecalc();
  if (!zBegin()) return false;
  auto& X = Z->u.x;
  for (int i = 0; i < WK.xl.n; i++) X.ord[i] = i;    // the cells in row order...
  for (int i = 1; i < WK.xl.n; i++) { uint8_t t = X.ord[i]; int j = i; while (j && WK.xl.pos[X.ord[j - 1]] > WK.xl.pos[t]) { X.ord[j] = X.ord[j - 1]; j--; } X.ord[j] = t; }
  X.nxf = 0;                                         // ...and one Excel style per format in use
  for (int i = 0; i < WK.xl.n; i++) {
    uint8_t f = xxFmtOf(i);
    bool have = !f;
    for (int q = 0; q < X.nxf && !have; q++) have = X.xf[q] == f;
    if (!have && X.nxf < 64) X.xf[X.nxf++] = f;
  }
  File f = LittleFS.open(FS("/~save.zip"), "w");
  if (!f) { zEnd(); return false; }
  zwStart(f);
  zwPart(PSTR("[Content_Types].xml"), xxTypes);
  zwPart(PSTR("_rels/.rels"), xxRels);
  zwPart(PSTR("xl/workbook.xml"), xxBook);
  zwPart(PSTR("xl/_rels/workbook.xml.rels"), xxBookRels);
  zwPart(PSTR("xl/styles.xml"), xxStyles);
  zwPart(PSTR("xl/worksheets/sheet1.xml"), xxSheet);
  bool ok = zwSaveAs(path);
  zEnd();
  return ok;
}
// Word charts read the .val file: "col row value" lines, numbers only
int sheetColumn(const char* name, int col, float* out, int maxn) {
  char p[40];
  snprintf_P(p, sizeof p, PSTR("/sheets/%s.val"), name);
  File f = LittleFS.open(p, FS("r"));
  if (!f) return -1;
  int n = 0;
  while (f.available() && n < maxn) {
    String l = f.readStringUntil('\n');
    int c, r; float v;
    if (sscanf(l.c_str(), FS("%d %d %f"), &c, &r, &v) == 3 && c == col) out[n++] = v;
  }
  f.close();
  return n;
}
// ---------- tools ----------
int xlRegion(int col, int* r0, int* r1) {            // the block of filled rows around the cursor
  int r = xlCur / XL_COLS;
  auto rowUsed = [&](int rr) { for (int i = 0; i < WK.xl.n; i++) if (WK.xl.pos[i] / XL_COLS == rr) return true; return false; };
  *r0 = r; *r1 = r;
  while (*r0 > 0 && rowUsed(*r0 - 1)) (*r0)--;
  while (*r1 < XL_ROWS - 1 && rowUsed(*r1 + 1)) (*r1)++;
  (void)col;
  return *r1 - *r0 + 1;
}
void xlSort(bool asc) {                             // rows by the active column: compare first, move once, recalc once
  int col = xlCur % XL_COLS, r0, r1;
  if (xlSel && xlAnchor >= 0) { r0 = min(xlCur / XL_COLS, xlAnchor / XL_COLS); r1 = max(xlCur / XL_COLS, xlAnchor / XL_COLS); }
  else xlRegion(col, &r0, &r1);
  double v;
  if (r1 > r0 && !shCellNum(col, r0, &v) && xlRaw(r0 * XL_COLS + col)[0] && shCellNum(col, r0 + 1, &v)) r0++;   // keep a header row on top
  int n = r1 - r0 + 1;
  if (n < 2) return;
  uint8_t order[XL_ROWS];                            // order[k] = the old row that ends up at r0 + k
  for (int k = 0; k < n; k++) order[k] = r0 + k;
  for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) {
    int a = order[i], b = order[j];
    double va, vb;
    bool na = shCellNum(col, a, &va), nb = shCellNum(col, b, &vb);
    const char* ta = xlRaw(a * XL_COLS + col);
    const char* tb = xlRaw(b * XL_COLS + col);
    int cmp = na && nb ? (va < vb ? -1 : va > vb) : na ? -1 : nb ? 1 : strcasecmp(ta, tb);
    if (!ta[0] && tb[0]) cmp = 1;                    // empty cells sink to the bottom
    if (!tb[0] && ta[0]) cmp = -1;
    if (asc ? cmp > 0 : cmp < 0) { order[i] = b; order[j] = a; }
  }
  uint8_t dest[XL_ROWS];                             // old row -> new row
  for (int k = 0; k < n; k++) dest[order[k]] = r0 + k;
  for (int i = 0; i < WK.xl.n; i++) {
    int r = WK.xl.pos[i] / XL_COLS, c = WK.xl.pos[i] % XL_COLS;
    if (r >= r0 && r <= r1) WK.xl.pos[i] = dest[r] * XL_COLS + c;
  }
  shRecalc();
  shDirty = true;
}
void xlAutoSum() {
  int c = xlCur % XL_COLS, r = xlCur / XL_COLS, a = r - 1;
  double v;
  char b[24];
  while (a >= 0 && shCellNum(c, a, &v)) a--;
  if (a < r - 1) snprintf_P(b, sizeof b, PSTR("=SUM(%c%d:%c%d)"), 'A' + c, a + 2, 'A' + c, r);
  else {
    int l = c - 1;
    while (l >= 0 && shCellNum(l, r, &v)) l--;
    if (l == c - 1) { w95MsgBox(FS("AutoSum"), FS("No numbers above or to the left"), FS("of this cell to add up."), FS("")); return; }
    snprintf_P(b, sizeof b, PSTR("=SUM(%c%d:%c%d)"), 'A' + l + 1, r + 1, 'A' + c - 1, r + 1);
  }
  xlSet(xlCur, b); shRecalc(); shDirty = true;
}
void xlStats() {
  int c0 = xlCur % XL_COLS, c1 = c0, r0 = 0, r1 = XL_ROWS - 1;
  if (xlSel && xlAnchor >= 0) { c0 = min(c0, xlAnchor % XL_COLS); c1 = max(xlCur % XL_COLS, xlAnchor % XL_COLS); r0 = min(xlCur / XL_COLS, xlAnchor / XL_COLS); r1 = max(xlCur / XL_COLS, xlAnchor / XL_COLS); }
  float v[XL_MAX]; int n = 0;
  for (int i = 0; i < WK.xl.n; i++) { int c = WK.xl.pos[i] % XL_COLS, r = WK.xl.pos[i] / XL_COLS; if (c >= c0 && c <= c1 && r >= r0 && r <= r1 && !isnan(WK.xl.val[i])) v[n++] = WK.xl.val[i]; }
  if (!n) { w95MsgBox(FS("Statistics"), FS("No numbers in this column"), FS("(or in the selected range)."), FS("")); return; }
  for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) if (v[j] < v[i]) { float t = v[i]; v[i] = v[j]; v[j] = t; }
  double sum = 0, sq = 0;
  for (int i = 0; i < n; i++) { sum += v[i]; sq += (double)v[i] * v[i]; }
  double mean = sum / n, sd = n > 1 ? sqrt(max(0.0, (sq - n * mean * mean) / (n - 1))) : 0;
  double med = n & 1 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
  char a[44], b[44], c[44], s1[14], s2[14], s3[14];
  fmtNum(sum, s1, sizeof s1, 7); fmtNum(mean, s2, sizeof s2, 6); snprintf_P(a, sizeof a, PSTR("count %d   sum %s   mean %s"), n, s1, s2);
  fmtNum(v[0], s1, sizeof s1, 6); fmtNum(v[n - 1], s2, sizeof s2, 6); fmtNum(med, s3, sizeof s3, 6); snprintf_P(b, sizeof b, PSTR("min %s  max %s  median %s"), s1, s2, s3);
  fmtNum(sd, s1, sizeof s1, 6); snprintf_P(c, sizeof c, PSTR("standard deviation %s"), s1);
  w95MsgBox(FS("Statistics"), a, b, c);
}
void xlDedupe() {                                    // Data > Remove duplicates: rows repeating this column's value
  int col = xlCur % XL_COLS, r0, r1, gone = 0;
  if (xlSel && xlAnchor >= 0) { r0 = min(xlCur / XL_COLS, xlAnchor / XL_COLS); r1 = max(xlCur / XL_COLS, xlAnchor / XL_COLS); }
  else xlRegion(col, &r0, &r1);
  for (int r = r1; r > r0; r--) {                    // from the bottom up, so the rows above keep their numbers
    const char* a = xlRaw(r * XL_COLS + col);
    if (!a[0]) continue;
    for (int q = r0; q < r; q++) if (!strcasecmp(a, xlRaw(q * XL_COLS + col))) { xlInsertDelete(false, r, -1); gone++; break; }
  }
  xlSel = false;
  if (gone) shRecalc();
  char m[40]; snprintf_P(m, sizeof m, PSTR("%d duplicate row%s removed"), gone, gone == 1 ? FS("") : FS("s"));
  shDrawAll();
  w95MsgBox(FS("Remove duplicates"), m, FS("(rows with the same value"), FS("in this column as one above)"));
}
int xlReplaceAll(const char* with) {                 // Edit > Replace: in every cell (formulas too)
  int n = strlen(xlFind), count = 0;
  if (!n) return 0;
  for (int i = 0; i < WK.xl.n; i++) {
    char b[48]; int o = 0;
    const char* s = WK.xl.raw[i];
    bool hit = false;
    for (int k = 0; s[k] && o < 47; ) {
      if (!strncasecmp(s + k, xlFind, n)) { for (const char* w = with; *w && o < 47; w++) b[o++] = *w; k += n; hit = true; count++; }
      else b[o++] = s[k++];
    }
    b[o] = 0;
    if (hit) snprintf_P(WK.xl.raw[i], 24, PSTR("%.23s"), b);
  }
  if (count) { shRecalc(); shDirty = true; }
  return count;
}
// ---------- chart window ----------
const char* shChartLabel(int i) {                    // row label: text in column A if there is one
  static char b[8];
  int col = xlChartCol, k = 0;
  for (int r = 0; r < XL_ROWS; r++) {
    double v;
    if (!shCellNum(col, r, &v)) continue;
    if (k++ == i) {
      const char* a = xlRaw(r * XL_COLS);
      if (col && a[0] && !shCellNum(0, r, &v)) snprintf_P(b, sizeof b, PSTR("%.7s"), a);
      else snprintf_P(b, sizeof b, PSTR("%d"), (r + 1) % 100);
      return b;
    }
  }
  return FS("");
}
void shDrawChart() {
  float v[XL_ROWS < 40 ? XL_ROWS : 40];
  int n = 0, col = xlChartCol;
  for (int r = 0; r < XL_ROWS && n < 40; r++) { double d; if (shCellNum(col, r, &d)) v[n++] = d; }
  char t[44];
  const char* kind = xlChart == 0 ? FS("Column") : xlChart == 1 ? FS("Line") : FS("Pie");
  snprintf_P(t, sizeof t, PSTR("Chart1 - %s chart of column %c"), kind, 'A' + col);
  tft.fillRect(0, 14, W, 226, W95_DESK);
  w95Title(FS("Excel - Chart Wizard"), W95_GRN, 'X');
  tft.fillRect(8, 20, 304, 202, W95_FACE); w95Bevel(8, 20, 304, 202, true);
  tft.fillRect(10, 22, 300, 12, W95_NAVY); putText(14, 24, 1, W95_HI, W95_NAVY, t);
  if (xlChart == 2) {
    tft.fillRect(12, 36, 296, 168, W95_PAPER);
    drawPie(90, 120, 70, v, n);
    for (int i = 0; i < min(n, 12); i++) {
      char b[24], num[12]; fmtNum(v[i], num, sizeof num, 6);
      xlChartCol = col;
      snprintf_P(b, sizeof b, PSTR("%.7s %s"), shChartLabel(i), num);
      tft.fillRect(176, 42 + i * 13, 8, 8, PIE_COL[i % 8]);
      putText(188, 42 + i * 13, 1, W95_INK, W95_PAPER, b);
    }
  } else {
    chartLabel = shChartLabel;
    drawChart(12, 36, 296, 168, v, n, xlChart == 1, W95_NAVY, shName[0] ? shName : FS("Book1"));
    chartLabel = nullptr;
  }
  putText(14, 208, 1, W95_INK, W95_FACE, FS("turn: column / line / pie   press: done"));
}
// ---------- commands ----------
void xlEditCell() {
  char t[48]; snprintf_P(t, sizeof t, PSTR("Cell %c%d: number, text or =formula"), 'A' + xlCur % XL_COLS, xlCur / XL_COLS + 1);
  xlMode = XLM_ASK; xlAsk = XQ_EDIT;
  askBegin(t, xlRaw(xlCur), 23, W95_GRN);
}
void xlFormat(int setFmt, int bits, int align) {      // on the cell or the whole selection
  int c0 = xlCur % XL_COLS, c1 = c0, r0 = xlCur / XL_COLS, r1 = r0;
  if (xlSel && xlAnchor >= 0) { c0 = min(c0, xlAnchor % XL_COLS); c1 = max(xlCur % XL_COLS, xlAnchor % XL_COLS); r0 = min(xlCur / XL_COLS, xlAnchor / XL_COLS); r1 = max(xlCur / XL_COLS, xlAnchor / XL_COLS); }
  bool turnOn = bits && !(xlFmt(xlCur) & bits);
  for (int i = 0; i < WK.xl.n; i++) {
    int c = WK.xl.pos[i] % XL_COLS, r = WK.xl.pos[i] / XL_COLS;
    if (c < c0 || c > c1 || r < r0 || r > r1) continue;
    uint8_t f = WK.xl.fmt[i];
    if (setFmt >= 0) f = (f & ~7) | setFmt;
    if (bits) f = turnOn ? (f | bits) : (f & ~bits);
    if (align >= 0) f = (f & ~0x60) | (align << 5);
    WK.xl.fmt[i] = f;
  }
  shDirty = true;
}
void xlCommand(int cmd) {
  int c = xlCur % XL_COLS, r = xlCur / XL_COLS;
  switch (cmd) {
    case 0: case 1000 + 1: sheetLeave(); shClear(); shName[0] = 0; shXlsx = false; break;
    case 1: case 1000 + 2: xlMode = XLM_PICK; fpAltExt = PSTR(".xlsx"); fpBegin(FS("Open workbook"), FS("/sheets"), FS(".csv"), nullptr, W95_GRN); return;
    case 2: case 1000 + 3:
      if (!shName[0]) { xlMode = XLM_ASK; xlAsk = XQ_SAVEAS; askBegin(FS("Save workbook as (name):"), FS(""), 12, W95_GRN); return; }
      if (!shSave()) { shDrawAll(); w95MsgBox(FS("Save"), FS("Couldn't save the workbook"), FS("(flash full, or low on memory?)"), FS("")); return; }
      SFX(T_HOME); break;
    case 3: xlMode = XLM_ASK; xlAsk = XQ_SAVEAS; askBegin(FS("Save workbook as (name):"), shName, 12, W95_GRN); return;
    case 4: xlMode = XLM_ASK; xlAsk = XQ_SAVEX; askBegin(FS("Save as Excel .xlsx (name):"), shName, 12, W95_GRN); return;
    case 6: {
      char a[40], b[40];
      snprintf_P(a, sizeof a, PSTR("%s.%s - %d of %d cells used"), shName[0] ? shName : FS("Book1"), shXlsx ? FS("xlsx") : FS("xls"), WK.xl.n, XL_MAX);
      snprintf_P(b, sizeof b, PSTR("grid A1:Z%d   %s"), XL_ROWS, shDirty ? FS("not saved yet") : FS("saved"));
      shDrawAll(); w95MsgBox(FS("Properties"), a, b, shXlsx ? FS("an Excel 2007-365 workbook (.xlsx)") : FS("saved as .csv: opens in Excel on a PC")); return;
    }
    case 8: enterApp(APP_MENU); return;
    case 32 + 0: case 1000 + 0: xlEditCell(); return;
    case 32 + 1:
      if (xlSel && xlAnchor >= 0) { for (int i = WK.xl.n - 1; i >= 0; i--) if (xlInSel(WK.xl.pos[i])) xlSet(WK.xl.pos[i], FS("")); xlSel = false; }
      else xlSet(xlCur, FS(""));
      shRecalc(); shDirty = true; break;
    case 32 + 3: case 1000 + 4: snprintf_P(xlClip, sizeof xlClip, PSTR("%s"), xlRaw(xlCur)); xlClipFrom = xlCur; break;
    case 32 + 4: case 1000 + 5:
      if (xlClipFrom >= 0) {
        char b[24];
        if (xlClip[0] == '=') xlShift(xlClip, b, sizeof b, c - xlClipFrom % XL_COLS, r - xlClipFrom / XL_COLS, 0, 0, 0, 0);
        else snprintf_P(b, sizeof b, PSTR("%s"), xlClip);
        xlSet(xlCur, b); shRecalc(); shDirty = true;
      }
      break;
    case 32 + 6: case 32 + 7: {                       // fill down / right: from the cell above (left), or across the selection
      bool down = cmd == 32 + 6;
      if (xlSel && xlAnchor >= 0) {
        int c0 = min(c, xlAnchor % XL_COLS), c1 = max(c, xlAnchor % XL_COLS), r0 = min(r, xlAnchor / XL_COLS), r1 = max(r, xlAnchor / XL_COLS);
        for (int rr = r0; rr <= r1; rr++) for (int cc = c0; cc <= c1; cc++) {
          if (down && rr > r0) xlCopyCell(r0 * XL_COLS + cc, rr * XL_COLS + cc);
          if (!down && cc > c0) xlCopyCell(rr * XL_COLS + c0, rr * XL_COLS + cc);
        }
        xlSel = false;
      } else if (down && r > 0) xlCopyCell(xlCur - XL_COLS, xlCur);
      else if (!down && c > 0) xlCopyCell(xlCur - 1, xlCur);
      shRecalc(); shDirty = true; break;
    }
    case 32 + 9: xlSel = !xlSel; xlAnchor = xlCur; break;
    case 32 + 10: xlMode = XLM_ASK; xlAsk = XQ_GOTO; askBegin(FS("Go to cell (e.g. C12):"), FS(""), 4, W95_GRN); return;
    case 32 + 11: xlMode = XLM_ASK; xlAsk = XQ_FIND; askBegin(FS("Find what:"), xlFind, 23, W95_GRN); return;
    case 32 + 12: xlMode = XLM_ASK; xlAsk = XQ_REPL1; askBegin(FS("Replace: find what?"), xlFind, 23, W95_GRN); return;
    case 64 + 0: shDown = !shDown; break;
    case 64 + 1: xlFormulas = !xlFormulas; break;
    case 64 + 2: xlGrid = !xlGrid; break;
    case 96 + 0: xlInsertDelete(false, r, 1); shRecalc(); break;
    case 96 + 1: xlInsertDelete(true, c, 1); shRecalc(); break;
    case 96 + 2: xlInsertDelete(false, r, -1); shRecalc(); break;
    case 96 + 3: xlInsertDelete(true, c, -1); shRecalc(); break;
    case 96 + 5: case 1000 + 7: shDrawAll(); xlMode = XLM_FN; pmShowP(FS("Paste Function"), XL_FNS_P, W95_GRN); return;
    case 96 + 6: case 1000 + 10: xlChartCol = c; xlMode = XLM_CHART; shDrawChart(); return;
    case 128 + 0: case 128 + 1: case 128 + 2: case 128 + 3: case 128 + 4: case 128 + 5: case 128 + 6: xlFormat(cmd - 128, 0, -1); break;
    case 1000 + 11: xlFormat(XF_CUR, 0, -1); break;
    case 1000 + 12: xlFormat(XF_PCT, 0, -1); break;
    case 1000 + 13: xlFormat(XF_THOU, 0, -1); break;
    case 128 + 8: case 1000 + 14: xlFormat(-1, XB_BOLD, -1); break;
    case 128 + 9: xlFormat(-1, XB_ITAL, -1); break;
    case 128 + 11: case 128 + 12: case 128 + 13: xlFormat(-1, 0, cmd - 128 - 10); break;
    case 128 + 15: WK.xl.colw[c] = min(120, WK.xl.colw[c] + 12); shDirty = true; xlScrollTo(); break;
    case 128 + 16: WK.xl.colw[c] = max(24, WK.xl.colw[c] - 12); shDirty = true; break;
    case 128 + 18: xlFormat(XF_DATE, 0, -1); break;
    case 128 + 19: xlNegRed = !xlNegRed; break;
    case 160 + 0: case 1000 + 6: shDrawAll(); xlAutoSum(); if (w95Msg) return; break;
    case 160 + 1: shRecalc(); break;
    case 160 + 2: shClear(); shDirty = true; break;
    case 192 + 0: case 1000 + 8: xlSort(true); break;
    case 192 + 1: case 1000 + 9: xlSort(false); break;
    case 192 + 2: {                                   // 1, 2, 3... down from the cell (or the start value in it)
      double v = 1; shCellNum(c, r, &v);
      int r1 = xlSel && xlAnchor >= 0 ? max(r, xlAnchor / XL_COLS) : min(XL_ROWS - 1, r + 9);
      int r0 = xlSel && xlAnchor >= 0 ? min(r, xlAnchor / XL_COLS) : r;
      for (int rr = r0; rr <= r1; rr++) { char b[16]; fmtNum(v + (rr - r0), b, sizeof b, 8); xlSet(rr * XL_COLS + c, b); }
      xlSel = false; shRecalc(); shDirty = true; break;
    }
    case 192 + 3: shDrawAll(); xlStats(); return;
    case 192 + 4: shDrawAll(); xlDedupe(); return;
    case 224 + 0: shDrawAll(); w95MsgBox(FS("Functions"), FS("SUM AVERAGE MIN MAX COUNT MEDIAN"), FS("STDEV VAR PRODUCT IF ROUND TODAY"), FS("ABS SQRT +-*/^ <> ranges A1:B5")); return;
    case 224 + 1: shDrawAll(); w95MsgBox(FS("About Excel"), FS("Excel for the Psion :3  version 365"), FS("opens + saves .xlsx, charts, dates"), FS("Made with paws by Micor & Claude")); return;
  }
  shDrawAll();
}
bool sheetWantsBack() { return w95Mode || w95Msg || xlMode != XLM_GRID || xlSel; }
void sheetBack() {
  if (w95Msg) { w95Msg = false; shDrawAll(); return; }
  if (w95Mode) { w95Back(); return; }
  if (xlMode == XLM_ASK) askOpen = false;
  if (xlMode == XLM_PICK) fpOpen = false;
  if (xlMode == XLM_FN || xlMode == XLM_CTYPE) pmOpen = false;
  if (xlMode == XLM_GRID && xlSel) xlSel = false;
  xlMode = XLM_GRID; shDrawAll();
}
bool isXlsx(const char* p) {                         // .xlsx .xlsm .xltx .xltm, any case
  size_t l = strlen(p);
  return l > 5 && (!strcasecmp_P(p + l - 5, PSTR(".xlsx")) || !strcasecmp_P(p + l - 5, PSTR(".xlsm")) ||
                   !strcasecmp_P(p + l - 5, PSTR(".xltx")) || !strcasecmp_P(p + l - 5, PSTR(".xltm")));
}
void shOpenFile(const char* path) {                  // a .csv or an Excel .xlsx (picker, Files app)
  xlMode = XLM_GRID;
  if (!isXlsx(path)) { shXlsx = false; shLoad(path); shDrawAll(); return; }
  char nm[16]; baseName(path, nm, sizeof nm);
  int r = xlsxImport(path);
  if (r) { shClear(); shName[0] = 0; shXlsx = false; } else { shXlsx = true; snprintf_P(shName, sizeof shName, PSTR("%s"), nm); }
  shDrawAll();
  if (r == 2) w95MsgBox(FS("Open"), FS("Not enough free memory to open"), FS("that workbook right now."), FS(""));
  else if (r == 1) w95MsgBox(FS("Open"), FS("That isn't an Excel .xlsx file"), FS("(or it's damaged)."), FS(""));
  else if (xlImpDropped) {
    char a[40]; snprintf_P(a, sizeof a, PSTR("%u cells were left out:"), xlImpDropped);
    w95MsgBox(FS("Opened (partly)"), a, FS("only A1:Z60 and 118 filled cells"), FS("fit on the Psion."));
  }
}
void sheetEnter() {
  xlUse();
  if (openPath[0]) {                                   // from the Files app
    char p[64]; snprintf_P(p, sizeof p, PSTR("%s"), openPath); openPath[0] = 0;
    wkOwner = APP_SHEET; w95Mode = 0; w95Msg = false; w95Focus = 8;
    shOpenFile(p);
    return;
  }
  else if (wkOwner != APP_SHEET) {                     // WK was used by another app: reload our file
    char p[40]; snprintf_P(p, sizeof p, PSTR("/sheets/%s.%s"), shName, shXlsx ? FS("xlsx") : FS("csv"));
    if (shName[0] && fsOk && LittleFS.exists(p)) { if (shXlsx) { if (xlsxImport(p)) shClear(); } else shLoad(p); }
    else {
      shXlsx = false;
      shClear(); shName[0] = 0;
      for (int k = 0; k < 15; k++) xlSet((k / 3) * XL_COLS + k % 3, pgmItem(PSTR("Fruit\nPaws\nPrice\napples\n3\n1.5\npears\n5\n2.25\nplums\n2\n0.8\nTotal\n=SUM(B2:B4)\n=SUM(C2:C4)"), k));
      xlSetFmt(0, XB_BOLD); xlSetFmt(1, XB_BOLD); xlSetFmt(2, XB_BOLD); xlSetFmt(4 * XL_COLS, XB_BOLD);
      for (int rr = 1; rr <= 4; rr++) xlSetFmt(rr * XL_COLS + 2, XF_CUR | (rr == 4 ? XB_BOLD : 0));
      shDirty = false;
    }
  }
  wkOwner = APP_SHEET;
  shRecalc();
  xlMode = XLM_GRID; w95Mode = 0; w95Msg = false; w95Focus = 8;   // first press lands on the pencil (Edit cell)
  shDrawAll();
}
void sheetLeave() {                                  // long press out: keep the work
  if (shDirty) { if (!shName[0]) strcpy(shName, FS("untitled")); shSave(); }
}
void sheetTick(uint32_t now) {
  static uint32_t lastMin = 0;
  int c = takeClicks() * ENC_DIR;
  takePress();
  bool sp = takeShort();
  xlUse();
  if (w95Msg) { w95MsgTick(c, sp); return; }
  switch (xlMode) {
    case XLM_GRID:
      if (w95Mode) { int cmd = w95Tick(c, sp); if (cmd >= 0) xlCommand(cmd); break; }
      if (c) { shMove(c); uiClick(); oledDirty = true; }
      if (sp) w95Open();
      if (now - lastMin > 30000) { lastMin = now; shStatus(); }
      break;
    case XLM_ASK: {
      int r = askTick(c, sp);
      if (!r) break;
      xlMode = XLM_GRID;
      if (r > 0) switch (xlAsk) {
        case XQ_EDIT:
          if (!xlSet(xlCur, askBuf)) { shDrawAll(); w95MsgBox(FS("Excel"), FS("This workbook is full"), FS("(118 filled cells max)."), FS("Clear some cells first.")); return; }
          if (!strncasecmp_P(askBuf, PSTR("=TODAY("), 7) && (xlFmt(xlCur) & 7) == XF_GEN) xlSetFmt(xlCur, (xlFmt(xlCur) & ~7) | XF_DATE);
          shRecalc(); shDirty = true;
          if (xlCur / XL_COLS < XL_ROWS - 1) { xlCur += XL_COLS; xlScrollTo(); }   // Enter moves down, like Excel
          break;
        case XQ_SAVEAS: case XQ_SAVEX: {
          cleanName(askBuf, shName);
          if (xlAsk == XQ_SAVEX) shXlsx = true;
          bool ok = shSave();
          shDrawAll();
          if (!ok) { w95MsgBox(FS("Save"), FS("Couldn't save the workbook"), FS("(flash full, or low on memory?)"), FS("")); return; }
          SFX(T_HOME);
          if (xlAsk == XQ_SAVEX) { char m[40]; snprintf_P(m, sizeof m, PSTR("/sheets/%s.xlsx"), shName); w95MsgBox(FS("Saved as Excel"), m, FS("Opens in Excel 2007-365 on a PC"), FS("(get it from the PC remote page).")); return; }
          break;
        }
        case XQ_REPL1: snprintf_P(xlFind, sizeof xlFind, PSTR("%s"), askBuf); xlMode = XLM_ASK; xlAsk = XQ_REPL2; askBegin(FS("Replace with:"), FS(""), 23, W95_GRN); return;
        case XQ_REPL2: {
          int n = xlReplaceAll(askBuf);
          char m[32]; snprintf_P(m, sizeof m, PSTR("%d replacement%s made."), n, n == 1 ? FS("") : FS("s"));
          shDrawAll(); w95MsgBox(FS("Replace"), m, FS(""), FS("")); return;
        }
        case XQ_GOTO: { const char* p = askBuf; int cc, rr; if (exCellRef(p, &cc, &rr) && cc < XL_COLS && rr >= 0 && rr < XL_ROWS) { xlCur = rr * XL_COLS + cc; xlScrollTo(); } break; }
        case XQ_FIND: {
          snprintf_P(xlFind, sizeof xlFind, PSTR("%s"), askBuf);
          int best = -1;
          for (int i = 0; i < WK.xl.n; i++) {
            const char* s = WK.xl.raw[i]; bool hit = false;
            for (int k = 0; s[k] && !hit; k++) hit = !strncasecmp(s + k, xlFind, strlen(xlFind));
            if (hit && (best < 0 || (WK.xl.pos[i] > xlCur && (WK.xl.pos[best] <= xlCur || WK.xl.pos[i] < WK.xl.pos[best])) || (WK.xl.pos[best] <= xlCur && WK.xl.pos[i] < WK.xl.pos[best]))) best = i;
          }
          if (best >= 0) { xlCur = WK.xl.pos[best]; xlScrollTo(); }
          else { shDrawAll(); w95MsgBox(FS("Find"), FS("Excel couldn't find:"), xlFind, FS("")); return; }
          break;
        }
      }
      shDrawAll();
      break;
    }
    case XLM_PICK: {
      int r = fpTick(c, sp);
      if (r == FP_BUSY) break;
      if (r >= 0) { if (shDirty) sheetLeave(); shOpenFile(fpPath); return; }
      xlMode = XLM_GRID; shDrawAll();
      break;
    }
    case XLM_FN: {
      int r = pmTick(c, sp);
      if (r < 0) break;
      xlMode = XLM_GRID;
      if (r < 12) {                                   // =FN( with a guess at the range above
        const char* fn = pgmItem(XL_FNS_P, r);
        char b[24];
        int cc = xlCur % XL_COLS, rr = xlCur / XL_COLS, a = rr - 1;
        double v;
        while (a >= 0 && shCellNum(cc, a, &v)) a--;
        if (r < 7 && a < rr - 1) snprintf_P(b, sizeof b, PSTR("=%s(%c%d:%c%d)"), fn, 'A' + cc, a + 2, 'A' + cc, rr);
        else if (r == 7) snprintf_P(b, sizeof b, PSTR("=IF(A1>0,1,0)"));
        else if (r == 8) snprintf_P(b, sizeof b, PSTR("=ROUND(A1,2)"));
        else if (r == 11) snprintf_P(b, sizeof b, PSTR("=TODAY()"));
        else snprintf_P(b, sizeof b, PSTR("=%s("), fn);
        xlMode = XLM_ASK; xlAsk = XQ_EDIT;
        char t[40]; snprintf_P(t, sizeof t, PSTR("Cell %c%d: edit the formula"), 'A' + cc, rr + 1);
        askBegin(t, b, 23, W95_GRN);
        return;
      }
      shDrawAll();
      break;
    }
    case XLM_CHART:
      if (c) { xlChart = ((xlChart + c) % 3 + 3) % 3; shDrawChart(); uiClick(); }
      if (sp) { xlMode = XLM_GRID; shDrawAll(); }
      break;
  }
  if (c || sp) oledDirty = true;
}
void oledSheet() {
  char l[4][24], v[24];
  bool right;
  shCellText(xlCur, v, sizeof v, &right);
  snprintf_P(l[0], 24, PSTR("cell %c%d"), 'A' + xlCur % XL_COLS, xlCur / XL_COLS + 1);
  snprintf_P(l[1], 24, PSTR("%.21s"), xlRaw(xlCur)[0] ? xlRaw(xlCur) : FS("(empty)"));
  snprintf_P(l[2], 24, PSTR("= %.19s"), v);
  snprintf_P(l[3], 24, PSTR("%.12s%s %d cells"), shName[0] ? shName : FS("Book1"), shDirty ? FS("*") : FS(""), WK.xl.n);
  oledCard(FS("EXCEL 95"), l[0], l[1], l[2], l[3]);
}

// ================= TEXT EDITOR (Word + Code) =================
// Insert/delete at a cursor; the "move" key turns the knob into cursor movement until you press.
const uint16_t ED_CAP = 2560;                       // (WK.ed.buf is ED_CAP + 1; the web pages' work area is as big anyway)
const int ED_COLS = 50, ED_ROWS = 7;
uint16_t edLen = 0, edCur = 0;
int edTop = 0;
bool edMove = false, edDirty = false;
uint16_t edAcc = 0;
const char* edWho = "";

void edSet(const char* text) {
  edLen = 0;
  if (text) { edLen = min((int)strlen(text), (int)ED_CAP); memcpy(WK.ed.buf, text, edLen); }
  WK.ed.buf[edLen] = 0; edCur = edLen; edTop = 0; edDirty = false; edMove = false;
}
void edSetP(PGM_P text) {                           // edSet() for flash text
  strncpy_P(WK.ed.buf, text, ED_CAP); WK.ed.buf[ED_CAP] = 0;
  edLen = strlen(WK.ed.buf); edCur = edLen; edTop = 0; edDirty = false; edMove = false;
}
bool edLoadFile(const char* path) {
  edSet(nullptr);
  File f = LittleFS.open(path, FS("r"));
  if (!f) return false;
  edLen = f.readBytes(WK.ed.buf, ED_CAP);
  f.close();
  WK.ed.buf[edLen] = 0; edCur = 0;
  return true;
}
bool edSaveFile(const char* dir, const char* path) {
  if (!fsOk) return false;
  LittleFS.mkdir(dir);
  File f = LittleFS.open(path, FS("w"));
  if (!f) return false;
  f.write((const uint8_t*)WK.ed.buf, edLen);
  f.close();
  edDirty = false;
  return true;
}
void edDraw() {
  const char* b = WK.ed.buf;
  int line = 0, col = 0, cl = 0, cc = 0;
  for (int k = 0; k <= edLen; k++) {                 // pass 1: where is the cursor?
    if (k == edCur) { cl = line; cc = col; }
    if (k == edLen) break;
    if (b[k] == '\n') { line++; col = 0; continue; }
    if (col == ED_COLS) { line++; col = 0; }
    col++;
  }
  if (cl < edTop) edTop = cl;
  if (cl >= edTop + ED_ROWS) edTop = cl - ED_ROWS + 1;
  tft.fillRect(4, 22, 312, ED_ROWS * 10 + 4, E_WHITE);
  tft.drawRect(3, 21, 314, ED_ROWS * 10 + 6, edMove ? edAcc : E_RULE);
  char out[ED_COLS + 1];
  int n = 0;
  line = 0; col = 0;
  for (int k = 0; k <= edLen && line < edTop + ED_ROWS; k++) {   // pass 2: draw the visible lines
    bool brk = k == edLen || b[k] == '\n' || col == ED_COLS;
    if (brk) {
      if (line >= edTop) { out[n] = 0; putText(8, 24 + (line - edTop) * 10, 1, C_INK, E_WHITE, out); }
      n = 0; line++; col = 0;
      if (k == edLen) break;
      if (b[k] == '\n') continue;
    }
    out[n++] = b[k]; col++;
  }
  tft.fillRect(8 + cc * 6, 23 + (cl - edTop) * 10, 2, 9, edAcc);
}
void edStatus() {
  char s[56];
  tft.fillRect(0, 200, W, 11, C_LCD);
  if (edMove) snprintf_P(s, sizeof s, PSTR("MOVE CURSOR: turn = move   press = back to typing"));
  else snprintf_P(s, sizeof s, PSTR("%s  %u/%u chars%s"), edWho, (unsigned)edLen, (unsigned)ED_CAP, edDirty ? FS("  *") : FS(""));
  putText(6, 201, 1, edMove ? edAcc : C_MID, C_LCD, s);
}
void edBegin(int layout, uint16_t acc, const char* who) {
  edAcc = acc; edWho = who; edMove = false;
  tft.fillRect(0, TITLE_H, W, H - TITLE_H, C_LCD);
  edDraw();
  kbBegin(layout, acc);
  kbDrawAll();
  edStatus();
}
void edInsert(char ch) {
  if (edLen >= ED_CAP) return;
  memmove(WK.ed.buf + edCur + 1, WK.ed.buf + edCur, edLen - edCur);
  WK.ed.buf[edCur++] = ch; WK.ed.buf[++edLen] = 0; edDirty = true;
}
void edInsertText(const char* s) { while (*s) edInsert(*s++); }
void edBackspace() {
  if (!edCur) return;
  memmove(WK.ed.buf + edCur - 1, WK.ed.buf + edCur, edLen - edCur);
  edCur--; WK.ed.buf[--edLen] = 0; edDirty = true;
}
int edTick(int c, bool sp) {                        // returns KB_VIEW / KB_MENU when the app key is pressed
  if (edMove) {
    if (c) { int step = abs(c) > 2 ? c * 5 : c; edCur = constrain((int)edCur + step, 0, (int)edLen); edDraw(); }
    if (sp) { edMove = false; edDraw(); edStatus(); SFX(T_HOP); }
    return KB_NONE;
  }
  int r = kbTick(c, sp);
  switch (r) {
    case KB_TYPED: edInsert(kbChar); break;
    case KB_SPACE: edInsert(' '); break;
    case KB_ENTER: edInsert('\n'); break;
    case KB_DEL:   edBackspace(); break;
    case KB_MOVE:  edMove = true; break;
    case KB_VIEW: case KB_MENU: return r;
    default: return KB_NONE;
  }
  edDraw(); edStatus(); oledDirty = true;
  return KB_NONE;
}

// ================= WORD (a Word 95 look-alike: styles, inline bold/italic/underline, tables, charts) =================
// Documents stay plain text with light marks, so old files still open and the editor stays simple:
//   # Heading 1   ## Heading 2   - bullet   + numbered   > quote   --- line   === page break
//   <c> centred   <r> right      | a | b | table row     [chart:sheet:B]  [chart:sheet:B:line|pie]
//   inline: **bold**  *italic*  __underline__  ~~strikethrough~~  ==highlight==
// In the page view the knob walks paragraphs (the one with the blue bar is "current"); press opens
// the menu bar / toolbar; Type (the pencil) opens the knob keyboard at the end of that paragraph.
// Word 2007-365 files (.docx) open and save too (File > Save as Word .docx); see OFFICE FILES above.
enum { WDM_VIEW, WDM_EDIT, WDM_ASK, WDM_PICK, WDM_COL, WDM_SYM, WDM_PREVIEW, WDM_PIE };
enum { WQ_SAVEAS, WQ_FIND, WQ_REPL1, WQ_REPL2, WQ_TABLE, WQ_FILE, WQ_SAVEX };
enum { WK_TEXT, WK_H1, WK_H2, WK_BULLET, WK_NUM, WK_QUOTE, WK_HR, WK_BREAK, WK_TABLE, WK_CHART };
enum { ST_B = 1, ST_I = 2, ST_U = 4, ST_S = 8, ST_H = 16 };
const int WD_Y0 = 52, WD_Y1 = 225, WD_X = 16, WD_TW = 276;    // document area + text column on the page
char wdName[24] = "", wdFind[24] = "", wdChartSheet[24];
int wdMode = WDM_VIEW, wdAsk = WQ_SAVEAS, wdPara = 0, wdParas = 1, wdTop = 0, wdLines = 0, wdPickFor = 0;
int wdSym = 0, wdPrevPage = 0, wdChartKind = 0;
bool wdMarks = false, wdZoom = false, wdRuler = true;
bool wdDocx = false;                                 // this document is a Word .docx (Save keeps it one)
uint16_t wdBg = W95_PAPER;                           // the paper under the letters being drawn (yellow = highlight)
// layout cursor (globals: Arduino can't prototype struct parameters)
int wkIdx = 0, wkY = 0, wkPara = 0, wkNum = 0, wkParaL0 = 0, wkParaL1 = 0, wkLastShown = 0;
bool wkDraw = false, wkFull = false;
uint8_t wpAlign = 0, wpKind = WK_TEXT, wpBody = 0;
const int WD_LINES = 24;                             // the lines on screen: top y + paragraph (for the PC's mouse)
int16_t wdLineY[WD_LINES], wdLinePara[WD_LINES];
uint8_t wdLineN = 0;
GFXcanvas1 wdGlyph(6, 8);                            // one character, for the slanted italic
const char WD_TITLES_P[] PROGMEM = "File\nEdit\nView\nInsert\nFormat\nTools\nTable\nHelp";
const char WD_M0_P[] PROGMEM = "New\nOpen...\nSave\nSave As...\nSave as Word .docx...\n-\nProperties\nPrint Preview\n-\nClose";
const char WD_M1_P[] PROGMEM = "Type text\nUndo\n-\nCut paragraph\nCopy paragraph\nPaste\nDelete paragraph\n-\nFind...\nFind next\nReplace...\n-\nGo to top\nGo to end";
const char WD_M2_P[] PROGMEM = "Show marks\nZoom 200%\nRuler";
const char WD_M3_P[] PROGMEM = "Page Break\nDate and Time\nSymbol...\nTable...\nChart from Sheet...\nHorizontal Line\nFile...\nTable of contents";
const char WD_M4_P[] PROGMEM = "Bold\nItalic\nUnderline\n-\nNormal\nHeading 1\nHeading 2\nQuote\n-\nAlign Left\nCenter\nAlign Right\n-\nBullets\nNumbering\nChange Case\n-\nStrikethrough\nHighlight\nClear formatting";
const char WD_M5_P[] PROGMEM = "Word Count\nAutoCorrect now\nSort paragraphs A-Z\nReading time";
const char WD_M6_P[] PROGMEM = "Insert Table...\nAdd Row\nDelete Row\nTotal row (sums)\nAdd Column\nDelete Column";
const char WD_M7_P[] PROGMEM = "Formatting marks\nWriting tips\nAbout Word";
const char* const WD_MENUS[8] = {WD_M0_P, WD_M1_P, WD_M2_P, WD_M3_P, WD_M4_P, WD_M5_P, WD_M6_P, WD_M7_P};
const uint8_t WD_TOOLS[] = {IC_TYPE, 255, IC_NEW, IC_OPEN, IC_SAVE, IC_PREVIEW, 255, IC_B, IC_I, IC_U, 255, IC_LEFT, IC_CENTER, IC_RIGHT, 255,
                            IC_BULLET, IC_NUMBER, IC_TABLE, IC_CHART, 254};
const char WD_SYMS[] = "\xF8\xF1\xAB\xAC\xA8\xAD\xA4\xA5\xA0\x82\xA1\xA2\xA3\x81\xAE\xAF\x9C\x9D\x9B\x15\x14\x07\x1A\x1B\x18\x19\x03\x04\x05\x06\xEC\xF7\xF3\xF2\xE3\xE4\xE6\xF6\xFB\xFD";

uint16_t wdAccent() { return appAccent(APP_WORD); }
// ---------- paragraphs ----------
int wdParaStart(int k) {                             // byte offset where paragraph k starts
  int i = 0;
  for (int p = 0; p < k && i < edLen; i++) if (WK.ed.buf[i] == '\n') p++;
  return i;
}
int wdParaEnd(int s) { while (s < edLen && WK.ed.buf[s] != '\n') s++; return s; }
int wdParaCount() { int n = 1; for (int i = 0; i < edLen; i++) if (WK.ed.buf[i] == '\n') n++; return n; }
int wdParaOf(int pos) { int n = 0; for (int i = 0; i < pos && i < edLen; i++) if (WK.ed.buf[i] == '\n') n++; return n; }
void wdParse(const char* s, int len) {               // -> wpAlign, wpKind, wpBody (where the words start)
  int o = 0;
  wpAlign = 0; wpKind = WK_TEXT;
  if (len >= 3 && s[0] == '<' && (s[1] == 'c' || s[1] == 'r') && s[2] == '>') { wpAlign = s[1] == 'c' ? 1 : 2; o = 3; }
  const char* t = s + o;
  int l = len - o;
  if (l >= 3 && !strncmp(t, FS("---"), 3)) wpKind = WK_HR;
  else if (l >= 3 && !strncmp(t, FS("==="), 3)) wpKind = WK_BREAK;
  else if (l >= 1 && t[0] == '|') wpKind = WK_TABLE;
  else if (l > 7 && !strncmp(t, FS("[chart:"), 7)) wpKind = WK_CHART;
  else if (l >= 2 && t[0] == '#' && t[1] == '#') { wpKind = WK_H2; o += 2 + (l > 2 && t[2] == ' '); }
  else if (l >= 1 && t[0] == '#') { wpKind = WK_H1; o += 1 + (l > 1 && t[1] == ' '); }
  else if (l >= 2 && (t[0] == '-' || t[0] == '*') && t[1] == ' ') { wpKind = WK_BULLET; o += 2; }
  else if (l >= 2 && t[0] == '+' && t[1] == ' ') { wpKind = WK_NUM; o += 2; }
  else if (l >= 2 && t[0] == '>' && t[1] == ' ') { wpKind = WK_QUOTE; o += 2; }
  wpBody = o;
}
// replace bytes [a, b) of the buffer with text
bool wdSplice(int a, int b, const char* text) {
  int n = strlen(text), grow = n - (b - a);
  if (edLen + grow > ED_CAP) return false;
  memmove(WK.ed.buf + a + n, WK.ed.buf + b, edLen - b + 1);
  memcpy(WK.ed.buf + a, text, n);
  edLen += grow; edDirty = true;
  return true;
}
// ---------- inline styles ----------
bool wdMarkAt(const char* s, int i, int len, uint8_t cur, uint8_t* f, int* m) {
  if (wdMarks) return false;
  if (s[i] == '*' && i + 1 < len && s[i + 1] == '*') { *f = ST_B; *m = 2; return true; }
  if (s[i] == '_' && i + 1 < len && s[i + 1] == '_') { *f = ST_U; *m = 2; return true; }
  if (s[i] == '~' && i + 1 < len && s[i + 1] == '~') { *f = ST_S; *m = 2; return true; }
  if (s[i] == '=' && i + 1 < len && s[i + 1] == '=') { *f = ST_H; *m = 2; return true; }
  if (s[i] == '*') {
    bool opening = !(cur & ST_I);
    if (opening ? (i + 1 < len && s[i + 1] != ' ') : (i > 0 && s[i - 1] != ' ')) { *f = ST_I; *m = 1; return true; }
  }
  return false;
}
int wdBreak(const char* s, int p, int len, int cols, uint8_t st, uint8_t* stEnd, int* visOut) {   // end of this line
  int vis = 0, lastSp = -1, visSp = 0, i = p;
  uint8_t cur = st, stSp = st, f;
  int m;
  while (i < len) {
    if (wdMarkAt(s, i, len, cur, &f, &m)) { cur ^= f; i += m; continue; }
    if (vis == cols) break;
    if (s[i] == ' ') { lastSp = i; stSp = cur; visSp = vis; }
    vis++; i++;
  }
  if (i < len && lastSp > p) { *stEnd = stSp; *visOut = visSp; return lastSp; }
  *stEnd = cur; *visOut = vis;
  return i;
}
void wdItalic(int x, int y, char c, int size, uint16_t col, bool bold) {
  tft.fillRect(x, y, 6 * size, 8 * size, wdBg);
  wdGlyph.fillScreen(0);
  wdGlyph.drawChar(0, 0, c, 1, 0, 1);
  for (int r = 0; r < 8; r++) {
    int sh = (7 - r) / 3;                            // the top leans right: 0..2 px
    for (int k = 0; k < 6; k++) if (wdGlyph.getPixel(k, r)) {
      if (size == 1) { tft.drawPixel(x + k + sh, y + r, col); if (bold) tft.drawPixel(x + k + sh + 1, y + r, col); }
      else tft.fillRect(x + (k + sh) * 2, y + r * 2, bold ? 3 : 2, 2, col);
    }
  }
}
void wdSeg(int x, int y, const char* t, int n, uint8_t st, int size, uint16_t col) {
  char b[52];
  n = min(n, 51);
  memcpy(b, t, n); b[n] = 0;
  wdBg = (st & ST_H) ? W95_YEL : W95_PAPER;          // highlighter pen
  if (st & ST_I) for (int i = 0; i < n; i++) wdItalic(x + i * 6 * size, y, b[i], size, col, st & ST_B);
  else {
    putText(x, y, size, col, wdBg, b);
    if (st & ST_B) { tft.setTextSize(size); tft.setTextColor(col); tft.setCursor(x + 1, y); tft.print(b); }
  }
  wdBg = W95_PAPER;
  if (st & ST_U) tft.drawFastHLine(x, y + 8 * size - 1, n * 6 * size, col);
  if (st & ST_S) tft.drawFastHLine(x, y + 4 * size - 1, n * 6 * size, col);
}
void wdRun(const char* s, int p, int e, uint8_t st, int x, int y, int size, uint16_t col) {   // draw [p, e) with styles
  int len = e, i = p, run = p, cx = x;
  uint8_t cur = st, f;
  int m;
  while (i < e) {
    if (wdMarkAt(s, i, len, cur, &f, &m)) {
      if (i > run) { wdSeg(cx, y, s + run, i - run, cur, size, col); cx += (i - run) * 6 * size; }
      cur ^= f; i += m; run = i;
      continue;
    }
    i++;
  }
  if (i > run) wdSeg(cx, y, s + run, i - run, cur, size, col);
}
// ---------- layout ----------
bool wdEmit(int h) {                                 // one visual line of height h: true = draw it at wkY
  bool show = wkDraw && !wkFull && wkIdx >= wdTop;
  if (show && wkY + h > WD_Y1) { wkFull = true; show = false; }
  if (wkPara == wdPara) { if (wkParaL0 < 0) wkParaL0 = wkIdx; wkParaL1 = wkIdx; }
  if (show) { wkLastShown = wkIdx; if (wdLineN < WD_LINES) { wdLineY[wdLineN] = wkY; wdLinePara[wdLineN] = wkPara; wdLineN++; } }
  wkIdx++;
  return show;
}
int16_t wdMarkY[WD_LINES], wdMarkP[WD_LINES]; uint8_t wdMarkH[WD_LINES], wdMarkN = 0;   // where the bars go
void wdParaMark(int y, int h) {
  if (wkDraw && wdMarkN < WD_LINES) { wdMarkY[wdMarkN] = y; wdMarkH[wdMarkN] = h; wdMarkP[wdMarkN] = wkPara; wdMarkN++; }
  if (wkPara == wdPara && wdMode == WDM_VIEW) tft.fillRect(9, y, 3, h, W95_NAVY);
}
bool wdQuickPara(int old) {                          // both paragraphs fully on screen: just move the bar
  if (wdMarkN < 2 || wdPara <= wdMarkP[0] || wdPara >= wdMarkP[wdMarkN - 1]) return false;
  for (int k = 0; k < wdMarkN; k++) {
    if (wdMarkP[k] == old) tft.fillRect(9, wdMarkY[k], 3, wdMarkH[k], W95_PAPER);
    else if (wdMarkP[k] == wdPara) tft.fillRect(9, wdMarkY[k], 3, wdMarkH[k], W95_NAVY);
  }
  return true;
}
void wdTextPara(const char* s, int len) {
  int size = wpKind == WK_H1 ? 2 : 1;
  if (wdZoom) size = 2;
  uint8_t base = wpKind == WK_H1 ? ST_B : wpKind == WK_H2 ? (ST_B | ST_I) : wpKind == WK_QUOTE ? ST_I : 0;
  int indent = (wpKind == WK_BULLET || wpKind == WK_NUM || wpKind == WK_QUOTE) ? 14 : 0;
  int cols = (WD_TW - indent) / (6 * size), lh = size * 8 + 2;
  uint16_t col = wpKind == WK_QUOTE ? W95_SH : W95_INK;
  const char* body = s + wpBody;
  int blen = len - wpBody, p = 0;
  uint8_t st = base;
  bool first = true;
  if (wpKind == WK_NUM) wkNum++;
  if (!blen && wdEmit(lh)) { wdParaMark(wkY, lh); wkY += lh; }
  while (p < blen) {
    uint8_t stEnd; int vis;
    int e = wdBreak(body, p, blen, cols, st, &stEnd, &vis);
    if (e == p) e = p + 1;
    if (wdEmit(lh)) {
      int x = WD_X + indent + (wpAlign == 1 ? (cols - vis) * 3 * size : wpAlign == 2 ? (cols - vis) * 6 * size : 0);
      if (first && wpKind == WK_BULLET) tft.fillCircle(WD_X + 5, wkY + 3 * size, 2, W95_INK);
      if (first && wpKind == WK_NUM) { char nb[6]; snprintf_P(nb, sizeof nb, PSTR("%d."), wkNum); putText(WD_X, wkY, 1, W95_INK, W95_PAPER, nb); }
      if (wpKind == WK_QUOTE) tft.fillRect(WD_X + 4, wkY, 2, lh, W95_SH);
      wdRun(body, p, e, st, x, wkY, size, col);
      if (wdMarks && e >= blen) putText(x + vis * 6 * size, wkY, size, W95_SH, W95_PAPER, FS("\x14"));   // pilcrow
      wdParaMark(wkY, lh);
      wkY += lh;
    }
    first = false;
    st = stEnd;
    p = e;
    while (p < blen && body[p] == ' ') p++;
  }
  int after = wpKind == WK_H1 ? 5 : 3;
  if (wdEmit(after)) { if (wpKind == WK_H1) tft.drawFastHLine(WD_X, wkY + 1, WD_TW, W95_SH); wkY += after; }
}
int wdCells(const char* s, int len, int* starts, int* lens, int maxc) {   // "| a | b |" -> cell slices
  int n = 0, i = 0;
  if (i < len && s[i] == '|') i++;
  while (i < len && n < maxc) {
    int a = i;
    while (i < len && s[i] != '|') i++;
    int b = i;
    while (a < b && s[a] == ' ') a++;
    while (b > a && s[b - 1] == ' ') b--;
    if (i < len || b > a) { starts[n] = a; lens[n] = b - a; n++; }
    i++;
  }
  return n;
}
void wdTableRow(const char* s, int len, bool header) {
  int st[8], ln[8], n = wdCells(s, len, st, ln, 8);
  if (!n) return;
  if (!wdEmit(12)) return;
  int cw = WD_TW / n;
  for (int k = 0; k < n; k++) {
    int x = WD_X + k * cw;
    uint16_t bg = header ? W95_FACE : W95_PAPER;
    tft.fillRect(x, wkY, cw, 12, bg);
    tft.drawRect(x, wkY, cw + 1, 13, W95_INK);
    char b[48];
    int maxc = max(1, cw / 6 - 1);
    snprintf_P(b, sizeof b, PSTR("%.*s"), min(ln[k], maxc), s + st[k]);
    char* end; strtod(b, &end);
    bool num = b[0] && !*end;
    int tx = num ? x + cw - 2 - (int)strlen(b) * 6 : x + 2;
    putText(tx, wkY + 2, 1, W95_INK, bg, b);
    if (header) { tft.setTextColor(W95_INK); tft.setCursor(tx + 1, wkY + 2); tft.print(b); }
  }
  wdParaMark(wkY, 12);
  wkY += 12;
}
int sheetColumn(const char* name, int col, float* out, int maxn);
void wdChart(const char* s, int len) {               // [chart:name:B] / [chart:name:B:line] / [chart:name:B:pie]
  char spec[40], sheet[16] = "", t[40];
  snprintf_P(spec, sizeof spec, PSTR("%.*s"), min(len, 39), s);
  char* a = spec + 7;
  char* c2 = strchr(a, ':');
  int col = 0, kind = 0;
  if (c2) { *c2 = 0; snprintf_P(sheet, sizeof sheet, PSTR("%s"), a); col = toupper((uint8_t)c2[1]) - 'A'; kind = strstr(c2 + 1, FS("line")) ? 1 : strstr(c2 + 1, FS("pie")) ? 2 : 0; }
  if (!wdEmit(90)) return;
  float v[24];
  int n = (col >= 0 && col < 26) ? sheetColumn(sheet, col, v, 24) : -1;
  if (n < 0) { snprintf_P(t, sizeof t, PSTR("[sheet '%s' not found - save it in Excel]"), sheet); n = 0; }
  else snprintf_P(t, sizeof t, PSTR("%s - column %c"), sheet, 'A' + col);
  if (kind == 2) {
    tft.fillRect(WD_X, wkY + 2, WD_TW, 84, W95_PAPER); tft.drawRect(WD_X, wkY + 2, WD_TW, 84, W95_SH);
    putText(WD_X + 4, wkY + 5, 1, W95_INK, W95_PAPER, t);
    drawPie(WD_X + 60, wkY + 50, 32, v, n);
    for (int i = 0; i < min(n, 6); i++) { char b[16]; fmtNum(v[i], b, sizeof b, 5); tft.fillRect(WD_X + 110, wkY + 18 + i * 11, 7, 7, PIE_COL[i % 8]); putText(WD_X + 121, wkY + 18 + i * 11, 1, W95_INK, W95_PAPER, b); }
  } else drawChart(WD_X + 4, wkY + 2, WD_TW - 8, 84, v, n, kind == 1, W95_NAVY, t);
  wdParaMark(wkY, 88);
  wkY += 90;
}
void wdWalk(bool draw) {
  wkIdx = 0; wkY = WD_Y0 + 4; wkDraw = draw; wkFull = false; wkPara = 0; wkNum = 0; wkParaL0 = -1; wkParaL1 = 0; wkLastShown = 0;
  if (draw) { wdLineN = 0; wdMarkN = 0; }
  const char* b = WK.ed.buf;
  int i = 0;
  bool prevTable = false;
  while (i <= edLen) {
    int e = wdParaEnd(i);
    const char* s = b + i;
    int len = e - i;
    wdParse(s, len);
    if (wpKind != WK_NUM) wkNum = 0;
    switch (wpKind) {
      case WK_HR: if (wdEmit(9)) { tft.drawFastHLine(WD_X, wkY + 4, WD_TW, W95_INK); wdParaMark(wkY, 9); wkY += 9; } break;
      case WK_BREAK:
        if (wdEmit(12)) { for (int x = WD_X; x < WD_X + WD_TW; x += 6) tft.drawFastHLine(x, wkY + 5, 3, W95_SH); putText(WD_X + WD_TW / 2 - 30, wkY + 2, 1, W95_SH, W95_PAPER, FS("Page Break")); wdParaMark(wkY, 12); wkY += 12; }
        break;
      case WK_TABLE: wdTableRow(s + wpBody, len - wpBody, !prevTable); break;
      case WK_CHART: wdChart(s + (wpAlign ? 3 : 0), len - (wpAlign ? 3 : 0)); break;
      default: wdTextPara(s, len); break;
    }
    prevTable = wpKind == WK_TABLE;
    if (prevTable && !(e + 1 <= edLen && b[e + 1] == '|') && wdEmit(4)) wkY += 4;
    wkPara++;
    if (e >= edLen) break;
    i = e + 1;
  }
  wdLines = wkIdx;
}
// ---------- the window ----------
void wdChrome() {
  char t[40];
  snprintf_P(t, sizeof t, PSTR("Word - %s.%s%s"), wdName[0] ? wdName : FS("Document1"), wdDocx ? FS("DOCX") : FS("DOC"), edDirty ? FS(" *") : FS(""));
  for (char* p = t + 7; *p; p++) *p = toupper((uint8_t)*p);
  w95Title(t, W95_NAVY, 'W');
  w95MenuBar();
  w95Toolbar();
}
void wdRulerDraw() {
  tft.fillRect(0, 44, W, 8, W95_FACE);
  if (!wdRuler) return;
  tft.fillRect(6, 44, 296, 7, W95_PAPER);
  for (int k = 0; k <= 46; k++) { int x = WD_X + k * 6; tft.drawFastVLine(x, k % 5 ? 49 : 46, k % 5 ? 2 : 5, W95_INK); }
  tft.fillTriangle(WD_X - 3, 51, WD_X + 3, 51, WD_X, 47, W95_INK);
  tft.fillTriangle(WD_X + WD_TW - 3, 51, WD_X + WD_TW + 3, 51, WD_X + WD_TW, 47, W95_INK);
}
int wdWordCount() { int n = 0; bool in = false; for (int i = 0; i < edLen; i++) { bool ch = WK.ed.buf[i] != ' ' && WK.ed.buf[i] != '\n'; if (ch && !in) n++; in = ch; } return n; }
void wdStatus() {
  char a[40], b[24];
  int page = 1;
  for (int i = 0, p = 0; i < edLen && p < wdPara; i++) if (WK.ed.buf[i] == '\n') { p++; if (!strncmp(WK.ed.buf + i + 1, FS("==="), 3)) page++; }
  page += wdTop / 48;
  snprintf_P(a, sizeof a, PSTR("Page %d   Para %d/%d   %d words"), page, wdPara + 1, wdParas, wdWordCount());
  int hh = (clkEpoch / 3600) % 24, mm = (clkEpoch / 60) % 60;
  snprintf_P(b, sizeof b, PSTR("%s %s  %02d:%02d"), wdMarks ? FS("MRK") : FS("   "), wdZoom ? FS("200%") : FS("100%"), hh, mm);
  w95StatusBar(a, b);
}
void wdDrawDoc() {                                    // the page, scrolled so the current paragraph shows
  for (int tries = 0; tries < 3; tries++) {
    tft.fillRect(0, WD_Y0, W, WD_Y1 - WD_Y0 + 1, W95_DESK);
    tft.fillRect(6, WD_Y0, 296, WD_Y1 - WD_Y0 + 1, W95_PAPER);
    tft.drawFastVLine(302, WD_Y0, WD_Y1 - WD_Y0 + 1, W95_DK);
    wdWalk(true);
    if (wkParaL0 >= 0 && wkParaL0 < wdTop) { wdTop = wkParaL0; continue; }
    if (wkParaL0 >= 0 && wkParaL1 > wkLastShown && wkParaL0 > wdTop) { wdTop += min(wkParaL1 - wkLastShown, wkParaL0 - wdTop); continue; }
    break;
  }
  if (!edLen) {
    putText(WD_X, WD_Y0 + 8, 1, W95_SH, W95_PAPER, FS("Empty document. Press the knob, then the"));
    putText(WD_X, WD_Y0 + 20, 1, W95_SH, W95_PAPER, FS("pencil button (Type) to start writing."));
  }
  w95ScrollV(306, WD_Y0, WD_Y1 - WD_Y0 + 1, wdTop, wdLines, 16);
}
void wdDrawAll() {
  wdParas = wdParaCount();
  wdPara = constrain(wdPara, 0, wdParas - 1);
  wdChrome();
  wdRulerDraw();
  wdDrawDoc();
  wdStatus();
}
void wdRedrawCb() { wdDrawAll(); }
bool wdOnCb(int cmd) {
  int s = wdParaStart(wdPara), e = wdParaEnd(s);
  wdParse(WK.ed.buf + s, e - s);
  const char* body = WK.ed.buf + s + wpBody;
  int bl = e - s - wpBody;
  bool bold = bl >= 4 && !strncmp(body, FS("**"), 2) && !strncmp(body + bl - 2, FS("**"), 2);
  bool und = bl >= 4 && !strncmp(body, FS("__"), 2) && !strncmp(body + bl - 2, FS("__"), 2);
  bool ital = bl >= 2 && body[0] == '*' && body[1] != '*' && body[bl - 1] == '*';
  bool strike = bl >= 4 && !strncmp(body, FS("~~"), 2) && !strncmp(body + bl - 2, FS("~~"), 2);
  bool hil = bl >= 4 && !strncmp(body, FS("=="), 2) && !strncmp(body + bl - 2, FS("=="), 2);
  switch (cmd) {
    case 4 * 32 + 17: return strike;
    case 4 * 32 + 18: return hil;
    case 2 * 32 + 0: return wdMarks;
    case 2 * 32 + 1: return wdZoom;
    case 2 * 32 + 2: return wdRuler;
    case 4 * 32 + 0: case 1000 + 5: return bold;
    case 4 * 32 + 1: case 1000 + 6: return ital;
    case 4 * 32 + 2: case 1000 + 7: return und;
    case 4 * 32 + 4: return wpKind == WK_TEXT;
    case 4 * 32 + 5: return wpKind == WK_H1;
    case 4 * 32 + 6: return wpKind == WK_H2;
    case 4 * 32 + 7: return wpKind == WK_QUOTE;
    case 4 * 32 + 9: case 1000 + 8: return wpAlign == 0;
    case 4 * 32 + 10: case 1000 + 9: return wpAlign == 1;
    case 4 * 32 + 11: case 1000 + 10: return wpAlign == 2;
    case 4 * 32 + 13: case 1000 + 11: return wpKind == WK_BULLET;
    case 4 * 32 + 14: case 1000 + 12: return wpKind == WK_NUM;
  }
  return false;
}
void wdUse() {                                       // make the Win95 engine talk to Word
  w95Titles = WD_TITLES_P; w95Menus = WD_MENUS; w95Tools = WD_TOOLS; w95Redraw = wdRedrawCb; w95On = wdOnCb;
  w95Look(RGB565(43, 87, 154));
}
// ---------- files, undo, clipboard ----------
bool docxExport(const char* path);
bool wdSave() {
  if (!wdName[0] || !fsOk) return false;
  char p[40]; snprintf_P(p, sizeof p, PSTR("/docs/%s.%s"), wdName, wdDocx ? FS("docx") : FS("doc"));
  if (!wdDocx) return edSaveFile(FS("/docs"), p);
  LittleFS.mkdir(FS("/docs"));                       // a real Word document
  bool ok = docxExport(p);
  if (ok) edDirty = false;
  return ok;
}
void wdSnapshot() {                                  // one level of undo, kept in flash (no RAM)
  if (!fsOk) return;
  File f = LittleFS.open(FS("/~undo.doc"), FS("w"));
  if (f) { f.write((const uint8_t*)WK.ed.buf, edLen); f.close(); }
}
void wdUndo() {
  if (!fsOk || !LittleFS.exists(FS("/~undo.doc"))) return;
  File f = LittleFS.open(FS("/~undo.doc"), FS("r"));
  if (!f) return;
  // swap: the current text becomes the new undo (so undo twice = redo)
  File t = LittleFS.open(FS("/~redo.doc"), FS("w"));
  if (t) { t.write((const uint8_t*)WK.ed.buf, edLen); t.close(); }
  edLen = f.readBytes(WK.ed.buf, ED_CAP); WK.ed.buf[edLen] = 0; f.close();
  LittleFS.remove(FS("/~undo.doc")); LittleFS.rename(FS("/~redo.doc"), FS("/~undo.doc"));
  edDirty = true;
}
void wdClipPut(int a, int b) {
  if (!fsOk) return;
  File f = LittleFS.open(FS("/clip.txt"), FS("w"));
  if (f) { f.write((const uint8_t*)WK.ed.buf + a, b - a); f.close(); }
}
// ---------- paragraph edits ----------
void wdSetPrefix(int kind, int align) {              // restyle the current paragraph (align < 0 = keep)
  int s = wdParaStart(wdPara), e = wdParaEnd(s);
  wdParse(WK.ed.buf + s, e - s);
  if (wpKind >= WK_HR) return;                       // lines, breaks, tables and charts keep their look
  if (align < 0) align = wpAlign;
  if (kind < 0) kind = wpKind;
  char pre[10] = "";
  if (align == 1) strcat(pre, FS("<c>")); else if (align == 2) strcat(pre, FS("<r>"));
  const char* k = kind == WK_H1 ? FS("# ") : kind == WK_H2 ? FS("## ") : kind == WK_BULLET ? FS("- ") : kind == WK_NUM ? FS("+ ") : kind == WK_QUOTE ? FS("> ") : FS("");
  strcat(pre, k);
  int bodyAt = s + wpBody;
  wdSnapshot();
  wdSplice(s, bodyAt, pre);
}
void wdWrapStyle(const char* mark) {                 // toggle **..** / *..* / __..__ around the paragraph's words
  int s = wdParaStart(wdPara), e = wdParaEnd(s);
  wdParse(WK.ed.buf + s, e - s);
  if (wpKind >= WK_HR) return;
  int a = s + wpBody, n = strlen(mark), bl = e - a;
  wdSnapshot();
  if (bl >= 2 * n && !strncmp(WK.ed.buf + a, mark, n) && !strncmp(WK.ed.buf + e - n, mark, n) && !(n == 1 && WK.ed.buf[a + 1] == '*')) {
    wdSplice(e - n, e, FS("")); wdSplice(a, a + n, FS(""));
  } else { if (!wdSplice(e, e, mark)) return; wdSplice(a, a, mark); }
}
void wdInsertAfter(const char* text) {               // a new paragraph below the current one
  int e = wdParaEnd(wdParaStart(wdPara));
  char buf[104];
  snprintf_P(buf, sizeof buf, PSTR("\n%s"), text);
  wdSnapshot();
  if (wdSplice(e, e, buf)) wdPara++;
}
void wdChangeCase() {                                // UPPER -> lower -> Title
  int s = wdParaStart(wdPara), e = wdParaEnd(s);
  wdParse(WK.ed.buf + s, e - s);
  char* p = WK.ed.buf + s + wpBody;
  int n = e - s - wpBody;
  bool anyLower = false, anyUpper = false;
  for (int i = 0; i < n; i++) { if (islower((uint8_t)p[i])) anyLower = true; if (isupper((uint8_t)p[i])) anyUpper = true; }
  wdSnapshot();
  for (int i = 0; i < n; i++) {
    if (anyLower) p[i] = toupper((uint8_t)p[i]);                    // -> UPPER
    else if (anyUpper) p[i] = (i == 0 || p[i - 1] == ' ') ? p[i] : tolower((uint8_t)p[i]);   // UPPER -> Title
  }
  if (!anyLower && !anyUpper) return;
  edDirty = true;
}
void wdAutoCorrect() {                               // double spaces, " i ", sentence capitals, a few typos
  wdSnapshot();
  int fixes = 0;
  for (int i = 0; i + 1 < edLen; i++) if (WK.ed.buf[i] == ' ' && WK.ed.buf[i + 1] == ' ') { wdSplice(i, i + 1, FS("")); i--; fixes++; }
  const char* typo[6][2] = {{" teh ", " the "}, {" i ", " I "}, {" dont ", " don't "}, {" im ", " I'm "}, {" recieve", " receive"}, {" adn ", " and "}};
  for (int k = 0; k < 6; k++) {
    for (char* p = strstr(WK.ed.buf, typo[k][0]); p; p = strstr(p + 1, typo[k][0])) {
      int a = p - WK.ed.buf;
      if (wdSplice(a, a + strlen(typo[k][0]), typo[k][1])) fixes++;
    }
  }
  bool start = true;
  for (int i = 0; i < edLen; i++) {
    char c = WK.ed.buf[i];
    if (c == '\n' || c == '.' || c == '!' || c == '?') { start = true; continue; }
    if (strchr(FS("#-+>*| <"), c)) continue;
    if (start && islower((uint8_t)c)) { WK.ed.buf[i] = toupper((uint8_t)c); fixes++; }
    if (c != ' ') start = false;
  }
  if (fixes) edDirty = true;
  char m[32]; snprintf_P(m, sizeof m, PSTR("%d fixes made"), fixes);
  w95MsgBox(FS("AutoCorrect"), m, FS("(Edit > Undo puts it back)"), FS(""));
}
void wdSortBlock() {                                 // sort the run of same-kind paragraphs around the current one
  int s = wdParaStart(wdPara), e = wdParaEnd(s);
  wdParse(WK.ed.buf + s, e - s);
  int kind = wpKind, a = wdPara, b = wdPara;
  while (a > 0) { int ps = wdParaStart(a - 1), pe = wdParaEnd(ps); wdParse(WK.ed.buf + ps, pe - ps); if (wpKind != kind || pe == ps) break; a--; }
  for (;;) { int ps = wdParaStart(b + 1); if (ps >= edLen || b + 1 >= wdParas) break; int pe = wdParaEnd(ps); wdParse(WK.ed.buf + ps, pe - ps); if (wpKind != kind || pe == ps) break; b++; }
  int from = wdParaStart(a), to = wdParaEnd(wdParaStart(b));
  if (to - from >= (int)sizeof WK.ed.con - 1 || a == b) { w95MsgBox(FS("Sort"), a == b ? FS("Nothing to sort here:") : FS("That block is too long"), a == b ? FS("pick a list or block of lines") : FS("to sort (max 1100 chars)."), FS("")); return; }
  char* tmp = (char*)WK.ed.con;                       // the program console is free while Word runs
  memcpy(tmp, WK.ed.buf + from, to - from); tmp[to - from] = 0;
  const char* lines[40]; int n = 0;
  for (char* p = tmp; *p; ) {
    if (n == 40) { w95MsgBox(FS("Sort"), FS("That block has too many lines"), FS("(40 at most)."), FS("")); return; }
    lines[n++] = p;
    char* q = strchr(p, '\n');
    if (!q) break;
    *q = 0; p = q + 1;
  }
  for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) if (strcasecmp(lines[j], lines[i]) < 0) { const char* t = lines[i]; lines[i] = lines[j]; lines[j] = t; }
  wdSnapshot();
  char* out = WK.ed.buf + from;
  for (int i = 0; i < n; i++) { int l = strlen(lines[i]); memmove(out, lines[i], l); out += l; if (i < n - 1) *out++ = '\n'; }
  // (same total length, so nothing after the block moves)
  edDirty = true;
}
void wdTableRowAdd(bool remove) {
  int s = wdParaStart(wdPara), e = wdParaEnd(s);
  wdParse(WK.ed.buf + s, e - s);
  if (wpKind != WK_TABLE) { w95MsgBox(FS("Table"), FS("Put the blue bar on a table"), FS("row first (turn the knob)."), FS("")); return; }
  if (remove) { wdSnapshot(); wdSplice(s, min(e + 1, (int)edLen), FS("")); wdPara = max(0, wdPara - (e >= edLen ? 1 : 0)); return; }
  int st[8], ln[8], n = wdCells(WK.ed.buf + s, e - s, st, ln, 8);
  char row[48] = "|";
  for (int k = 0; k < n; k++) strcat(row, FS("   |"));
  wdInsertAfter(row);
}
void wdTableTotal() {
  int s = wdParaStart(wdPara), e = wdParaEnd(s);
  wdParse(WK.ed.buf + s, e - s);
  if (wpKind != WK_TABLE) { w95MsgBox(FS("Table"), FS("Put the blue bar on a table"), FS("row first (turn the knob)."), FS("")); return; }
  int a = wdPara, b = wdPara;
  while (a > 0 && WK.ed.buf[wdParaStart(a - 1)] == '|') a--;
  while (b + 1 < wdParas && WK.ed.buf[wdParaStart(b + 1)] == '|') b++;
  double sum[8] = {0}; bool any[8] = {false}; int cols = 0;
  for (int r = a; r <= b; r++) {
    int ps = wdParaStart(r), pe = wdParaEnd(ps), st[8], ln[8];
    int n = wdCells(WK.ed.buf + ps, pe - ps, st, ln, 8);
    cols = max(cols, n);
    for (int k = 0; k < n; k++) { char c[24]; snprintf_P(c, sizeof c, PSTR("%.*s"), min(ln[k], 23), WK.ed.buf + ps + st[k]); char* end; double v = strtod(c, &end); if (c[0] && !*end) { sum[k] += v; any[k] = true; } }
  }
  char row[100] = "|";
  for (int k = 0; k < cols; k++) {
    char c[16];
    if (any[k]) fmtNum(sum[k], c, sizeof c, 8); else strcpy_P(c, k ? PSTR("") : PSTR("Total"));
    size_t o = strlen(row);
    snprintf_P(row + o, sizeof row - o, PSTR(" %s |"), c);   // bounded: a very wide table just gets cut short
  }
  wdPara = b;
  wdInsertAfter(row);
}
void wdFindNext(bool fromStart) {
  if (!wdFind[0]) return;
  int n = strlen(wdFind), from = fromStart ? 0 : wdParaEnd(wdParaStart(wdPara));
  for (int pass = 0; pass < 2; pass++) {
    for (int i = from; i + n <= edLen; i++) if (!strncasecmp(WK.ed.buf + i, wdFind, n)) { wdPara = wdParaOf(i); return; }
    from = 0;
  }
  w95MsgBox(FS("Find"), FS("Word couldn't find:"), wdFind, FS(""));
}
void wdReplaceAll(const char* with) {
  int n = strlen(wdFind), count = 0;
  if (!n) return;
  wdSnapshot();
  for (int i = 0; i + n <= edLen; ) {
    if (!strncmp(WK.ed.buf + i, wdFind, n)) { if (!wdSplice(i, i + n, with)) break; i += strlen(with); count++; }
    else i++;
  }
  char m[32]; snprintf_P(m, sizeof m, PSTR("%d replacement%s made."), count, count == 1 ? FS("") : FS("s"));
  w95MsgBox(FS("Replace"), m, count ? FS("(Edit > Undo puts it back)") : FS(""), FS(""));
}
void wdProperties() {
  char a[40], b[40], c[40];
  int words = wdWordCount();
  snprintf_P(a, sizeof a, PSTR("%s.%s  -  %d paragraphs"), wdName[0] ? wdName : FS("Document1"), wdDocx ? FS("docx") : FS("doc"), wdParas);
  snprintf_P(b, sizeof b, PSTR("%d words, %u characters"), words, (unsigned)edLen);
  snprintf_P(c, sizeof c, PSTR("%u of %u bytes used (%d%%)"), (unsigned)edLen, (unsigned)ED_CAP, (int)(edLen * 100L / ED_CAP));
  w95MsgBox(FS("Properties"), a, b, c);
}
// ---------- print preview: two whole pages, words as grey bars ----------
void wdPreviewDraw() {
  tft.fillRect(0, 14, W, 226, W95_DESK);
  w95Title(FS("Word - Print Preview"), W95_NAVY, 'W');
  tft.fillRect(0, 14, W, 12, W95_FACE);
  putText(6, 16, 1, W95_INK, W95_FACE, FS("turn: pages   press: close   (bars = your lines)"));
  int pageLines = 46, line = 0, page = 0;
  for (int pg = 0; pg < 2; pg++) {
    int px = 22 + pg * 146, py = 32;
    tft.fillRect(px + 3, py + 3, 130, 186, W95_DK);
    tft.fillRect(px, py, 130, 186, W95_PAPER);
    tft.drawRect(px, py, 130, 186, W95_INK);
    char n[12]; snprintf_P(n, sizeof n, PSTR("%d"), wdPrevPage + pg + 1);
    putText(px + 62, py + 176, 1, W95_SH, W95_PAPER, n);
  }
  // walk the text: each visual line becomes a bar on the right page
  const char* b = WK.ed.buf;
  int i = 0;
  while (i <= edLen) {
    int e = wdParaEnd(i);
    wdParse(b + i, e - i);
    int vis = e - i - wpBody, cols = wpKind == WK_H1 ? 23 : 46, h = wpKind == WK_H1 ? 2 : 1;
    int nl = wpKind == WK_CHART ? 9 : max(1, (vis + cols - 1) / cols);
    if (wpKind == WK_BREAK) { page++; line = 0; }
    for (int k = 0; k < nl && wpKind != WK_BREAK; k++) {
      if (line >= pageLines) { page++; line = 0; }
      int pg = page - wdPrevPage;
      if (pg >= 0 && pg < 2) {
        int px = 22 + pg * 146 + 10, py = 32 + 10 + line * 3 * h / 1;
        int w = wpKind == WK_CHART ? 90 : (k == nl - 1 ? max(4, (vis - k * cols) * 108 / cols) : 108);
        uint16_t col = wpKind == WK_H1 || wpKind == WK_H2 ? W95_INK : wpKind == WK_CHART ? W95_NAVY : W95_SH;
        if (wpKind == WK_CHART) { if (k == 0) tft.fillRect(px + 8, py, w, 24, col); }
        else tft.fillRect(px + (wpAlign == 1 ? (108 - w) / 2 : wpAlign == 2 ? 108 - w : 0), py, w, 2 * h, col);
      }
      line += h + (wpKind == WK_H1 ? 1 : 0);
    }
    if (wpKind != WK_BREAK && wpKind != WK_TABLE) line++;
    if (e >= edLen) break;
    i = e + 1;
  }
}
// ---------- Word .docx in ----------
// Paragraph styles (headings, title, quote, lists - by the style's English built-in name, so Spanish Word's
// "Titulo 1" works too), bullets vs numbering (numbering.xml), alignment, bold / italic / underline / strike /
// highlight, tables, page breaks, horizontal lines. Pictures become "[picture]". What doesn't fit is cut off.
uint8_t dxKind, dxAlign, dxRst, dxCur, dxSkip;
int16_t dxTbl;
int dxParaAt;
bool dxInPPr, dxInRPr, dxInT, dxPrefixed, dxHasText, dxBlank, dxBorder, dxCellText, dxFull;
uint16_t dxParas;
File dxOut;                                          // Books: the .docx's text goes to this file instead of Word
bool dxToFile = false;
uint8_t dxWb[64]; uint8_t dxWn = 0;
void dxFlushOut() { if (dxWn) { if (dxOut.write(dxWb, dxWn) != dxWn) Z->stop = true; dxWn = 0; } }
void dxPut(char c) {
  if (dxToFile) { dxWb[dxWn++] = (uint8_t)c; if (dxWn == sizeof dxWb) dxFlushOut(); return; }
  if (edLen < ED_CAP) WK.ed.buf[edLen++] = c; else { dxFull = true; Z->stop = true; }
}
void dxPutP(PGM_P s) { for (char c; (c = pgm_read_byte(s)); s++) dxPut(c); }
uint8_t dxNameKind(const char* v) {                  // a paragraph style's name (or id) -> what the Psion shows
  char l[24]; int i = 0;
  for (; v[i] && i < 23; i++) l[i] = tolower((uint8_t)v[i]);
  l[i] = 0;
  if (!strcmp_P(l, PSTR("heading 1")) || !strcmp_P(l, PSTR("heading1")) || !strcmp_P(l, PSTR("title"))) return WK_H1;
  if (!strncmp_P(l, PSTR("heading"), 7) || !strcmp_P(l, PSTR("subtitle"))) return WK_H2;
  if (strstr_P(l, PSTR("quote"))) return WK_QUOTE;
  if (!strncmp_P(l, PSTR("list bullet"), 11) || !strncmp_P(l, PSTR("listbullet"), 10)) return WK_BULLET;
  if (!strncmp_P(l, PSTR("list number"), 11) || !strncmp_P(l, PSTR("listnumber"), 10)) return WK_NUM;
  return WK_TEXT;
}
uint8_t dxStyleKind(const char* id) {
  for (int i = 0; i < Z->u.d.ns; i++) if (!strcmp(Z->u.d.sid[i], id)) return Z->u.d.skind[i];
  return dxNameKind(id);                             // (no styles.xml: Word's English ids, Heading1...)
}
void dsStart(const char* n, const char* a, bool) {   // styles.xml
  auto& D = Z->u.d;
  char v[32];
  if (!strcmp_P(n, PSTR("style"))) {
    D.inStyle = xAttr(a, "type", v, sizeof v) && !strcmp_P(v, PSTR("paragraph")) && xAttr(a, "styleId", D.cur, sizeof D.cur);
    D.kind = WK_TEXT;
  } else if (D.inStyle && !strcmp_P(n, PSTR("name")) && xAttr(a, "val", v, sizeof v)) D.kind = dxNameKind(v);
  else if (D.inStyle && !strcmp_P(n, PSTR("outlineLvl")) && D.kind == WK_TEXT) D.kind = xAttrInt(a, "val", 0) ? WK_H2 : WK_H1;
}
void dsEnd(const char* n) {
  auto& D = Z->u.d;
  if (strcmp_P(n, PSTR("style"))) return;
  if (D.inStyle && D.kind != WK_TEXT && D.ns < 20) { memcpy(D.sid[D.ns], D.cur, 14); D.skind[D.ns++] = D.kind; }
  D.inStyle = false;
}
void dnStart(const char* n, const char* a, bool) {   // numbering.xml: which lists are numbered (not bullets)
  auto& D = Z->u.d;
  char v[16];
  if (!strcmp_P(n, PSTR("abstractNum"))) { D.abs = xAttrInt(a, "abstractNumId", -1); D.lvl0 = false; }
  else if (!strcmp_P(n, PSTR("lvl"))) D.lvl0 = xAttrInt(a, "ilvl", -1) == 0;
  else if (D.lvl0 && !strcmp_P(n, PSTR("numFmt")) && D.abs >= 0 && D.abs < 64 && xAttr(a, "val", v, sizeof v) &&
           strcmp_P(v, PSTR("bullet")) && strcmp_P(v, PSTR("none"))) D.absNum[D.abs >> 5] |= 1UL << (D.abs & 31);
  else if (!strcmp_P(n, PSTR("num"))) D.num = xAttrInt(a, "numId", -1);
  else if (!strcmp_P(n, PSTR("abstractNumId")) && D.num >= 0 && D.num < 64) {
    long k = xAttrInt(a, "val", -1);
    if (k >= 0 && k < 64 && (D.absNum[k >> 5] >> (k & 31) & 1)) D.numNum[D.num >> 5] |= 1UL << (D.num & 31);
  }
}
void dnEnd(const char* n) { if (!strcmp_P(n, PSTR("lvl"))) Z->u.d.lvl0 = false; if (!strcmp_P(n, PSTR("num"))) Z->u.d.num = -1; }
void dxParaBegin() { dxParaAt = edLen; dxKind = WK_TEXT; dxAlign = 0; dxPrefixed = dxHasText = dxBorder = false; dxCur = dxRst = 0; }
void dxPrefix() {                                    // "<c>" "# " "- "...: written before the first letter
  if (dxPrefixed) return;
  dxPrefixed = true;
  if (dxToFile) {                                    // Books: \1 big heading, \2 heading, \7 a bullet
    if (dxKind == WK_H1) dxPut(1); else if (dxKind == WK_H2) dxPut(2);
    else if (dxKind == WK_BULLET || dxKind == WK_NUM) { dxPut(7); dxPut(' '); }
    else if (dxKind == WK_QUOTE) dxPutP(PSTR("   "));
    return;
  }
  if (dxAlign == 1) dxPutP(PSTR("<c>")); else if (dxAlign == 2) dxPutP(PSTR("<r>"));
  switch (dxKind) {
    case WK_H1: dxPutP(PSTR("# ")); break;
    case WK_H2: dxPutP(PSTR("## ")); break;
    case WK_BULLET: dxPutP(PSTR("- ")); break;
    case WK_NUM: dxPutP(PSTR("+ ")); break;
    case WK_QUOTE: dxPutP(PSTR("> ")); break;
  }
}
int dxMarkStr(uint8_t f, char* m) {                  // bold first, italic last (so "***" reads right both ways)
  int n = 0;
  if (f & ST_B) { m[n++] = '*'; m[n++] = '*'; }
  if (f & ST_U) { m[n++] = '_'; m[n++] = '_'; }
  if (f & ST_S) { m[n++] = '~'; m[n++] = '~'; }
  if (f & ST_H) { m[n++] = '='; m[n++] = '='; }
  if (f & ST_I) m[n++] = '*';
  return n;
}
void dxMarks(uint8_t want) {                         // switch styles: closing marks go before the spaces just written
  if (dxToFile) { dxCur = want; return; }            // (Books: plain text)
  char m[12];
  uint8_t cl = dxCur & ~want, op = want & ~dxCur;
  if (cl) {
    int sp = 0;
    while (edLen - sp > dxParaAt && WK.ed.buf[edLen - 1 - sp] == ' ') sp++;
    int n = dxMarkStr(cl, m);
    if (edLen + n > ED_CAP) { dxFull = true; Z->stop = true; return; }
    memmove(WK.ed.buf + edLen - sp + n, WK.ed.buf + edLen - sp, sp);
    memcpy(WK.ed.buf + edLen - sp, m, n);
    edLen += n;
  }
  if (op) { int n = dxMarkStr(op, m); for (int i = 0; i < n; i++) dxPut(m[i]); }
  dxCur = want;
}
void dxChar(uint16_t u) {                            // one letter of the document's text
  uint8_t c = u == '\t' ? ' ' : uniCp(u);
  if (!c || c == '\n' || c == '\r') return;
  if (dxTbl > 0) { if (c == '|') c = '/'; dxPut(c); dxCellText = true; return; }
  dxPrefix();
  uint8_t want = (dxKind == WK_H1 || dxKind == WK_H2 || dxKind == WK_QUOTE) ? 0 : dxRst;   // (those are styled whole)
  if (c != ' ' && want != dxCur) dxMarks(want);
  if (c == '_') {                                    // ____ fill-in lines: drawn as a line (__ would be underline)
    uint8_t p = edLen > dxParaAt ? (uint8_t)WK.ed.buf[edLen - 1] : 0;
    if (p == '_') { WK.ed.buf[edLen - 1] = (char)0xC4; c = 0xC4; }
    else if (p == 0xC4) c = 0xC4;
  }
  dxPut(c);
  dxHasText = true;
}
void dxCharsP(PGM_P s) { for (char c; (c = pgm_read_byte(s)); s++) dxChar((uint8_t)c); }
void dxParaEnd() {
  if (dxTbl > 0) { if (dxCellText) dxPut(' '); return; }   // paragraphs inside a table cell: one line
  if (!dxHasText && !dxBorder && dxKind == WK_TEXT) {      // an empty paragraph (spacing): one, not a run of them
    if (dxBlank || edLen == 0) return;
    dxBlank = true;
  } else dxBlank = false;
  if (!dxHasText && dxBorder) dxPutP(PSTR("---")); else dxPrefix();
  dxPut('\n');
  if (dxToFile && dxKind != WK_BULLET && dxKind != WK_NUM) dxPut('\n');   // (Books: a blank line between paragraphs)
  dxParas++;
}
void dxBreak(bool page) {                            // <w:br/>: a new line (same style), or a new page
  if (dxTbl > 0) { dxPut(' '); return; }
  if (dxHasText || !page) { dxPrefix(); dxPut('\n'); }    // a line break ends this line (an empty one too)
  if (page) { if (dxToFile) dxPut('\f'); else dxPutP(PSTR("===\n")); dxBlank = true; }
  dxParaAt = edLen; dxPrefixed = dxHasText = false; dxCur = 0;
  if (dxKind == WK_BULLET || dxKind == WK_NUM) dxKind = WK_TEXT;   // (not a new list item)
}
void dxStart(const char* n, const char* a, bool empty) {
  if (dxSkip) { dxSkip++; return; }
  char v[24];
  if (!strcmp_P(n, PSTR("p"))) { if (!dxTbl) dxParaBegin(); }
  else if (!strcmp_P(n, PSTR("pPr"))) dxInPPr = true;
  else if (!strcmp_P(n, PSTR("rPr"))) dxInRPr = !dxInPPr;
  else if (!strcmp_P(n, PSTR("r"))) dxRst = 0;
  else if (!strcmp_P(n, PSTR("t"))) dxInT = true;
  else if (dxInPPr) {
    if (!strcmp_P(n, PSTR("pStyle")) && xAttr(a, "val", v, sizeof v)) dxKind = dxStyleKind(v);
    else if (!strcmp_P(n, PSTR("outlineLvl")) && dxKind == WK_TEXT) dxKind = xAttrInt(a, "val", 0) ? WK_H2 : WK_H1;
    else if (!strcmp_P(n, PSTR("numId"))) {
      long k = xAttrInt(a, "val", 0);
      if (k > 0 && (dxKind == WK_TEXT || dxKind == WK_BULLET || dxKind == WK_NUM))
        dxKind = (k < 64 && (Z->u.d.numNum[k >> 5] >> (k & 31) & 1)) ? WK_NUM : WK_BULLET;
    } else if (!strcmp_P(n, PSTR("jc")) && xAttr(a, "val", v, sizeof v))
      dxAlign = !strcmp_P(v, PSTR("center")) ? 1 : (!strcmp_P(v, PSTR("right")) || !strcmp_P(v, PSTR("end"))) ? 2 : 0;
    else if (!strcmp_P(n, PSTR("bottom"))) dxBorder = true;  // a paragraph border: Word's horizontal line
  } else if (dxInRPr) {
    uint8_t f = !strcmp_P(n, PSTR("b")) ? ST_B : !strcmp_P(n, PSTR("i")) ? ST_I : !strcmp_P(n, PSTR("u")) ? ST_U :
                (!strcmp_P(n, PSTR("strike")) || !strcmp_P(n, PSTR("dstrike"))) ? ST_S : !strcmp_P(n, PSTR("highlight")) ? ST_H : 0;
    if (f) dxRst = xOn(a) ? (dxRst | f) : (dxRst & ~f);
  }
  else if (!strcmp_P(n, PSTR("tab"))) { if (dxInT || !dxInPPr) dxChar(' '); }
  else if (!strcmp_P(n, PSTR("br")) || !strcmp_P(n, PSTR("cr"))) dxBreak(xAttr(a, "type", v, sizeof v) && !strcmp_P(v, PSTR("page")));
  else if (!strcmp_P(n, PSTR("drawing")) || !strcmp_P(n, PSTR("pict")) || !strcmp_P(n, PSTR("object"))) { dxCharsP(PSTR("[picture]")); if (!empty) dxSkip = 1; }
  else if (!strcmp_P(n, PSTR("Fallback")) || !strcmp_P(n, PSTR("txbxContent")) || !strcmp_P(n, PSTR("instrText")) ||
           !strcmp_P(n, PSTR("delText")) || !strcmp_P(n, PSTR("del"))) { if (!empty) dxSkip = 1; }   // (no doubles, no field codes, no deleted text)
  else if (!strcmp_P(n, PSTR("tbl"))) dxTbl++;
  else if (dxTbl == 1 && !strcmp_P(n, PSTR("tr"))) dxPut('|');
  else if (dxTbl == 1 && !strcmp_P(n, PSTR("tc"))) { dxPut(' '); dxCellText = false; }
}
void dxEnd(const char* n) {
  if (dxSkip) { dxSkip--; return; }
  if (!strcmp_P(n, PSTR("p"))) dxParaEnd();
  else if (!strcmp_P(n, PSTR("pPr"))) dxInPPr = false;
  else if (!strcmp_P(n, PSTR("rPr"))) dxInRPr = false;
  else if (!strcmp_P(n, PSTR("t"))) dxInT = false;
  else if (!strcmp_P(n, PSTR("tbl"))) dxTbl = max(0, dxTbl - 1);
  else if (dxTbl == 1 && !strcmp_P(n, PSTR("tc"))) { while (edLen > 1 && WK.ed.buf[edLen - 1] == ' ' && WK.ed.buf[edLen - 2] != '|') edLen--; dxPutP(PSTR(" |")); }
  else if (dxTbl == 1 && !strcmp_P(n, PSTR("tr"))) { dxPut('\n'); dxParas++; }
}
void dxText(uint16_t u) { if (dxInT) dxChar(u); }
char dxRelTarget[48];
void dxRelStart(const char* n, const char* a, bool) {   // _rels/.rels: where the main document part is
  char t[96];
  if (strcmp_P(n, PSTR("Relationship")) || !xAttr(a, "Type", t, sizeof t)) return;
  size_t l = strlen(t);
  if (l > 15 && !strcmp_P(t + l - 15, PSTR("/officeDocument"))) xAttr(a, "Target", dxRelTarget, sizeof dxRelTarget);
}
int docxImport(const char* path) {                   // -> WK.ed. 0 ok, 1 not a Word file, 2 not enough memory
  if (!zBegin()) return 2;
  memset(&Z->u.d, 0, sizeof Z->u.d);
  Z->u.d.abs = Z->u.d.num = -1;
  xText = nullptr;
  xStart = dsStart; xEnd = dsEnd; zPartP(path, PSTR("word/styles.xml"));
  xStart = dnStart; xEnd = dnEnd; zPartP(path, PSTR("word/numbering.xml"));
  edSet(nullptr);
  dxTbl = 0; dxSkip = 0; dxInPPr = dxInRPr = dxInT = dxBlank = dxFull = false; dxParas = 0;
  dxParaBegin();
  xStart = dxStart; xEnd = dxEnd; xText = dxText;
  int r = zPartP(path, PSTR("word/document.xml"));
  if (r == 1) {                                      // some programs name it differently: ask the package's index
    Z->u.d.cur[0] = 0; dxRelTarget[0] = 0;
    xStart = dxRelStart; xEnd = nullptr; xText = nullptr;
    zPartP(path, PSTR("_rels/.rels"));
    if (dxRelTarget[0]) {
      dxParaBegin();
      xStart = dxStart; xEnd = dxEnd; xText = dxText;
      r = zPart(path, dxRelTarget[0] == '/' ? dxRelTarget + 1 : dxRelTarget);
    }
  }
  while (edLen && (WK.ed.buf[edLen - 1] == '\n' || WK.ed.buf[edLen - 1] == ' ')) edLen--;
  WK.ed.buf[edLen] = 0;
  edCur = 0; edTop = 0; edDirty = false;
  zEnd();
  xStart = nullptr; xEnd = nullptr; xText = nullptr;
  return r == 1 || r == 2 ? r : 0;                   // (a damaged end: keep what came)
}
int docxToText(const char* path, const char* out) {   // Books: a .docx's text into a file. 0 ok, 1 not Word, 2 memory, 3 disk
  if (!zBegin()) return 2;
  memset(&Z->u.d, 0, sizeof Z->u.d);
  Z->u.d.abs = Z->u.d.num = -1;
  xText = nullptr;
  xStart = dsStart; xEnd = dsEnd; zPartP(path, PSTR("word/styles.xml"));
  xStart = dnStart; xEnd = dnEnd; zPartP(path, PSTR("word/numbering.xml"));
  dxOut = LittleFS.open(out, "w");
  if (!dxOut) { zEnd(); xStart = nullptr; xEnd = nullptr; return 3; }
  dxToFile = true; dxWn = 0; edLen = 0;
  dxTbl = 0; dxSkip = 0; dxInPPr = dxInRPr = dxInT = dxBlank = dxFull = false; dxParas = 0;
  dxParaBegin();
  xStart = dxStart; xEnd = dxEnd; xText = dxText;
  int r = zPartP(path, PSTR("word/document.xml"));
  if (r == 1) {
    Z->u.d.cur[0] = 0; dxRelTarget[0] = 0;
    xStart = dxRelStart; xEnd = nullptr; xText = nullptr;
    zPartP(path, PSTR("_rels/.rels"));
    if (dxRelTarget[0]) {
      dxParaBegin();
      xStart = dxStart; xEnd = dxEnd; xText = dxText;
      r = zPart(path, dxRelTarget[0] == '/' ? dxRelTarget + 1 : dxRelTarget);
    }
  }
  dxFlushOut();
  size_t sz = dxOut.size();
  dxOut.close();
  dxToFile = false; edLen = 0;
  zEnd();
  xStart = nullptr; xEnd = nullptr; xText = nullptr;
  if (r == 1 || r == 2) { LittleFS.remove(out); return r; }
  if (!sz) { LittleFS.remove(out); return 3; }
  return 0;
}
// ---------- Word .docx out ----------
void dwRun(const char* t, int n, uint8_t st) {
  if (n <= 0) return;
  zwP(PSTR("<w:r>"));
  if (st) {                                          // (Word wants rPr children in this order)
    zwP(PSTR("<w:rPr>"));
    if (st & ST_B) zwP(PSTR("<w:b/>"));
    if (st & ST_I) zwP(PSTR("<w:i/>"));
    if (st & ST_S) zwP(PSTR("<w:strike/>"));
    if (st & ST_H) zwP(PSTR("<w:highlight w:val=\"yellow\"/>"));
    if (st & ST_U) zwP(PSTR("<w:u w:val=\"single\"/>"));
    zwP(PSTR("</w:rPr>"));
  }
  zwP(PSTR("<w:t xml:space=\"preserve\">")); zwText(t, n, true); zwP(PSTR("</w:t></w:r>"));
}
void dwRuns(const char* s, int n) {                  // **bold** *italic*... -> runs
  bool keep = wdMarks; wdMarks = false;
  int i = 0, run = 0, m;
  uint8_t cur = 0, f;
  while (i < n) {
    if (wdMarkAt(s, i, n, cur, &f, &m)) { dwRun(s + run, i - run, cur); cur ^= f; i += m; run = i; continue; }
    i++;
  }
  dwRun(s + run, i - run, cur);
  wdMarks = keep;
}
int dwTable(int i) {                                 // "| a | b |" lines -> one Word table; returns where it ends
  const char* b = WK.ed.buf;
  int cols = 1, j = i;
  for (;;) {                                         // pass 1: how many columns
    int e = wdParaEnd(j);
    wdParse(b + j, e - j);
    if (wpKind != WK_TABLE) break;
    int st[8], ln[8];
    cols = max(cols, wdCells(b + j + wpBody, e - j - wpBody, st, ln, 8));
    if (e >= edLen) { j = edLen + 1; break; }
    j = e + 1;
  }
  int cw = 9360 / cols;
  zwP(PSTR("<w:tbl><w:tblPr><w:tblStyle w:val=\"TableGrid\"/><w:tblW w:w=\"0\" w:type=\"auto\"/></w:tblPr><w:tblGrid>"));
  for (int k = 0; k < cols; k++) { zwP(PSTR("<w:gridCol w:w=\"")); zwNum(cw); zwP(PSTR("\"/>")); }
  zwP(PSTR("</w:tblGrid>"));
  bool head = true;
  for (int r = i; r < j && r <= edLen; ) {           // pass 2: the rows (the first one bold, like the Psion shows it)
    int e = wdParaEnd(r);
    wdParse(b + r, e - r);
    int st[8], ln[8], nc = wdCells(b + r + wpBody, e - r - wpBody, st, ln, 8);
    zwP(PSTR("<w:tr>"));
    for (int k = 0; k < cols; k++) {
      zwP(PSTR("<w:tc><w:tcPr><w:tcW w:w=\"")); zwNum(cw); zwP(PSTR("\" w:type=\"dxa\"/></w:tcPr><w:p>"));
      if (k < nc) dwRun(b + r + wpBody + st[k], ln[k], head ? ST_B : 0);
      zwP(PSTR("</w:p></w:tc>"));
    }
    zwP(PSTR("</w:tr>"));
    head = false;
    r = e + 1;
  }
  zwP(PSTR("</w:tbl>"));
  return j;
}
void dwDoc() {                                       // word/document.xml
  zwP(PSTR("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:body>"));
  const char* b = WK.ed.buf;
  int i = 0, block = 0;
  bool prevNum = false, lastTable = false;
  while (i <= edLen) {
    int e = wdParaEnd(i);
    wdParse(b + i, e - i);
    if (wpKind == WK_TABLE) { i = dwTable(i); prevNum = false; lastTable = true; continue; }
    lastTable = false;
    uint8_t kind = wpKind, align = wpAlign;
    int body = wpBody;
    if (kind == WK_NUM && !prevNum) block++;         // each numbered list starts again at 1
    prevNum = kind == WK_NUM;
    if (kind == WK_HR) zwP(PSTR("<w:p><w:pPr><w:pBdr><w:bottom w:val=\"single\" w:sz=\"8\" w:space=\"1\" w:color=\"auto\"/></w:pBdr></w:pPr></w:p>"));
    else if (kind == WK_BREAK) zwP(PSTR("<w:p><w:r><w:br w:type=\"page\"/></w:r></w:p>"));
    else {
      zwP(PSTR("<w:p>"));
      if (kind != WK_TEXT || align) {
        zwP(PSTR("<w:pPr>"));
        if (kind == WK_H1) zwP(PSTR("<w:pStyle w:val=\"Heading1\"/>"));
        else if (kind == WK_H2) zwP(PSTR("<w:pStyle w:val=\"Heading2\"/>"));
        else if (kind == WK_QUOTE) zwP(PSTR("<w:pStyle w:val=\"Quote\"/>"));
        else if (kind == WK_BULLET || kind == WK_NUM) {
          zwP(PSTR("<w:pStyle w:val=\"ListParagraph\"/><w:numPr><w:ilvl w:val=\"0\"/><w:numId w:val=\""));
          zwNum(kind == WK_BULLET ? 1 : 1 + block); zwP(PSTR("\"/></w:numPr>"));
        }
        if (align) { zwP(PSTR("<w:jc w:val=\"")); zwP(align == 1 ? PSTR("center") : PSTR("right")); zwP(PSTR("\"/>")); }
        zwP(PSTR("</w:pPr>"));
      }
      if (kind == WK_CHART) {                        // a chart stays on the Psion: its numbers come along as text
        const char* s = b + i + (align ? 3 : 0);
        char spec[40], t[64];
        snprintf_P(spec, sizeof spec, PSTR("%.*s"), min(e - i - (align ? 3 : 0), 39), s);
        char* sh = spec + 7; char* c2 = strchr(sh, ':');
        int col = 0;
        if (c2) { *c2 = 0; col = toupper((uint8_t)c2[1]) - 'A'; }
        snprintf_P(t, sizeof t, PSTR("Chart: sheet %s, column %c"), sh, 'A' + constrain(col, 0, 25));
        dwRun(t, strlen(t), ST_I);
        float v[24];
        int nv = (col >= 0 && col < 26) ? sheetColumn(sh, col, v, 24) : -1;
        for (int k = 0; k < nv; k++) { char nb[16]; nb[0] = k ? ',' : ':'; nb[1] = ' '; fmtNum(v[k], nb + 2, sizeof nb - 2, 6); dwRun(nb, strlen(nb), 0); }
      } else dwRuns(b + i + body, e - i - body);
      zwP(PSTR("</w:p>"));
    }
    if (e >= edLen) break;
    i = e + 1;
  }
  if (lastTable) zwP(PSTR("<w:p/>"));                // (Word wants a paragraph after a table at the end)
  zwP(PSTR("<w:sectPr><w:pgSz w:w=\"12240\" w:h=\"15840\"/><w:pgMar w:top=\"1440\" w:right=\"1440\" w:bottom=\"1440\" w:left=\"1440\" "
           "w:header=\"708\" w:footer=\"708\" w:gutter=\"0\"/></w:sectPr></w:body></w:document>"));
}
void dwTypes() {
  zwP(PSTR("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
           "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"xml\" ContentType=\"application/xml\"/>"
           "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
           "<Override PartName=\"/word/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>"
           "<Override PartName=\"/word/numbering.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.numbering+xml\"/></Types>"));
}
void dwRels() {
  zwP(PSTR("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
           "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/></Relationships>"));
}
void dwDocRels() {
  zwP(PSTR("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
           "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>"
           "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/numbering\" Target=\"numbering.xml\"/></Relationships>"));
}
void dwStyles() {
  zwP(PSTR("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<w:styles xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
           "<w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:ascii=\"Calibri\" w:hAnsi=\"Calibri\" w:eastAsia=\"Calibri\" w:cs=\"Calibri\"/>"
           "<w:sz w:val=\"22\"/><w:szCs w:val=\"22\"/><w:lang w:val=\"es-MX\"/></w:rPr></w:rPrDefault>"
           "<w:pPrDefault><w:pPr><w:spacing w:after=\"160\" w:line=\"259\" w:lineRule=\"auto\"/></w:pPr></w:pPrDefault></w:docDefaults>"
           "<w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\"><w:name w:val=\"Normal\"/><w:qFormat/></w:style>"
           "<w:style w:type=\"paragraph\" w:styleId=\"Heading1\"><w:name w:val=\"heading 1\"/><w:basedOn w:val=\"Normal\"/><w:next w:val=\"Normal\"/><w:qFormat/>"
           "<w:pPr><w:keepNext/><w:spacing w:before=\"240\" w:after=\"80\"/><w:outlineLvl w:val=\"0\"/></w:pPr>"
           "<w:rPr><w:b/><w:color w:val=\"2F5496\"/><w:sz w:val=\"32\"/><w:szCs w:val=\"32\"/></w:rPr></w:style>"
           "<w:style w:type=\"paragraph\" w:styleId=\"Heading2\"><w:name w:val=\"heading 2\"/><w:basedOn w:val=\"Normal\"/><w:next w:val=\"Normal\"/><w:qFormat/>"
           "<w:pPr><w:keepNext/><w:spacing w:before=\"160\" w:after=\"80\"/><w:outlineLvl w:val=\"1\"/></w:pPr>"
           "<w:rPr><w:b/><w:i/><w:color w:val=\"2F5496\"/><w:sz w:val=\"26\"/><w:szCs w:val=\"26\"/></w:rPr></w:style>"
           "<w:style w:type=\"paragraph\" w:styleId=\"Quote\"><w:name w:val=\"Quote\"/><w:basedOn w:val=\"Normal\"/><w:next w:val=\"Normal\"/><w:qFormat/>"
           "<w:pPr><w:ind w:left=\"720\" w:right=\"720\"/></w:pPr><w:rPr><w:i/><w:color w:val=\"595959\"/></w:rPr></w:style>"
           "<w:style w:type=\"paragraph\" w:styleId=\"ListParagraph\"><w:name w:val=\"List Paragraph\"/><w:basedOn w:val=\"Normal\"/><w:qFormat/>"
           "<w:pPr><w:ind w:left=\"720\"/><w:contextualSpacing/></w:pPr></w:style>"
           "<w:style w:type=\"table\" w:default=\"1\" w:styleId=\"TableNormal\"><w:name w:val=\"Normal Table\"/><w:tblPr><w:tblInd w:w=\"0\" w:type=\"dxa\"/>"
           "<w:tblCellMar><w:top w:w=\"0\" w:type=\"dxa\"/><w:left w:w=\"108\" w:type=\"dxa\"/><w:bottom w:w=\"0\" w:type=\"dxa\"/><w:right w:w=\"108\" w:type=\"dxa\"/>"
           "</w:tblCellMar></w:tblPr></w:style>"
           "<w:style w:type=\"table\" w:styleId=\"TableGrid\"><w:name w:val=\"Table Grid\"/><w:basedOn w:val=\"TableNormal\"/><w:pPr><w:spacing w:after=\"0\" w:line=\"240\" w:lineRule=\"auto\"/></w:pPr>"
           "<w:tblPr><w:tblBorders><w:top w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/><w:left w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/>"
           "<w:bottom w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/><w:right w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/>"
           "<w:insideH w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/><w:insideV w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/>"
           "</w:tblBorders></w:tblPr></w:style></w:styles>"));
}
uint16_t dwBlocks = 0;                               // numbered lists in the document (each restarts at 1)
void dwNumbering() {
  zwP(PSTR("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<w:numbering xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
           "<w:abstractNum w:abstractNumId=\"0\"><w:multiLevelType w:val=\"hybridMultilevel\"/><w:lvl w:ilvl=\"0\"><w:start w:val=\"1\"/>"
           "<w:numFmt w:val=\"bullet\"/><w:lvlText w:val=\"\xE2\x80\xA2\"/><w:lvlJc w:val=\"left\"/><w:pPr><w:ind w:left=\"720\" w:hanging=\"360\"/></w:pPr></w:lvl></w:abstractNum>"
           "<w:abstractNum w:abstractNumId=\"1\"><w:multiLevelType w:val=\"hybridMultilevel\"/><w:lvl w:ilvl=\"0\"><w:start w:val=\"1\"/>"
           "<w:numFmt w:val=\"decimal\"/><w:lvlText w:val=\"%1.\"/><w:lvlJc w:val=\"left\"/><w:pPr><w:ind w:left=\"720\" w:hanging=\"360\"/></w:pPr></w:lvl></w:abstractNum>"
           "<w:num w:numId=\"1\"><w:abstractNumId w:val=\"0\"/></w:num>"));
  for (int k = 1; k <= dwBlocks; k++) {
    zwP(PSTR("<w:num w:numId=\"")); zwNum(1 + k);
    zwP(PSTR("\"><w:abstractNumId w:val=\"1\"/><w:lvlOverride w:ilvl=\"0\"><w:startOverride w:val=\"1\"/></w:lvlOverride></w:num>"));
  }
  zwP(PSTR("</w:numbering>"));
}
bool docxExport(const char* path) {
  dwBlocks = 0;
  bool prev = false;
  for (int i = 0; i <= edLen; ) {                    // count the numbered lists first (numbering.xml lists them)
    int e = wdParaEnd(i);
    wdParse(WK.ed.buf + i, e - i);
    if (wpKind == WK_NUM && !prev) dwBlocks++;
    prev = wpKind == WK_NUM;
    if (e >= edLen) break;
    i = e + 1;
  }
  if (!zBegin()) return false;
  File f = LittleFS.open(FS("/~save.zip"), "w");
  if (!f) { zEnd(); return false; }
  zwStart(f);
  zwPart(PSTR("[Content_Types].xml"), dwTypes);
  zwPart(PSTR("_rels/.rels"), dwRels);
  zwPart(PSTR("word/document.xml"), dwDoc);
  zwPart(PSTR("word/styles.xml"), dwStyles);
  zwPart(PSTR("word/numbering.xml"), dwNumbering);
  zwPart(PSTR("word/_rels/document.xml.rels"), dwDocRels);
  bool ok = zwSaveAs(path);
  zEnd();
  return ok;
}
void wdOpenDocx(const char* path) {                  // open a .docx (from the picker or the Files app)
  int r = docxImport(path);
  baseName(path, wdName, sizeof wdName);
  wdDocx = r == 0; wdPara = 0; wdTop = 0;
  if (r) { edSet(nullptr); wdName[0] = 0; }
  wdDrawAll();
  if (r == 2) w95MsgBox(FS("Open"), FS("Not enough free memory to open"), FS("that document right now."), FS(""));
  else if (r == 1) w95MsgBox(FS("Open"), FS("That isn't a Word .docx file"), FS("(or it's damaged)."), FS(""));
  else if (dxFull) {
    char a[40]; snprintf_P(a, sizeof a, PSTR("Only the first %u characters"), (unsigned)ED_CAP);
    w95MsgBox(FS("Opened (partly)"), a, FS("of this document fit on the Psion."), FS("Saving keeps just this part."));
  }
}
// ---------- more tools ----------
int wdPlain(const char* s, int n, char* out, int cap) {   // the words without **marks**
  bool keep = wdMarks; wdMarks = false;
  int o = 0, m; uint8_t cur = 0, f;
  for (int i = 0; i < n && o < cap - 1; ) { if (wdMarkAt(s, i, n, cur, &f, &m)) { cur ^= f; i += m; continue; } out[o++] = s[i++]; }
  out[o] = 0;
  wdMarks = keep;
  return o;
}
void wdClearFormat() {                               // Format > Clear formatting: a plain Normal paragraph
  int s = wdParaStart(wdPara), e = wdParaEnd(s);
  wdParse(WK.ed.buf + s, e - s);
  if (wpKind >= WK_HR) return;
  char* tmp = (char*)WK.ed.con;
  wdPlain(WK.ed.buf + s + wpBody, e - s - wpBody, tmp, sizeof WK.ed.con);
  wdSnapshot();
  wdSplice(s, e, tmp);
}
void wdInsertToc() {                                 // Insert > Table of contents: the headings, as a list
  char* t = (char*)WK.ed.con;
  const int cap = sizeof WK.ed.con;
  int o = snprintf_P(t, cap, PSTR("\n# Contents")), n = 0, p = 0;
  for (int i = 0; i <= edLen && o < cap - 60; p++) {
    int e = wdParaEnd(i);
    wdParse(WK.ed.buf + i, e - i);
    if ((wpKind == WK_H1 || wpKind == WK_H2) && p != wdPara) {
      char h[44];
      wdPlain(WK.ed.buf + i + wpBody, min(e - i - wpBody, 43), h, sizeof h);
      if (strcasecmp_P(h, PSTR("Contents"))) { o += snprintf_P(t + o, cap - o, wpKind == WK_H1 ? PSTR("\n- %s") : PSTR("\n-    %s"), h); n++; }
    }
    if (e >= edLen) break;
    i = e + 1;
  }
  if (!n) { w95MsgBox(FS("Table of contents"), FS("No headings yet: make some"), FS("paragraphs Heading 1 / Heading 2"), FS("(Format menu) first.")); return; }
  int e = wdParaEnd(wdParaStart(wdPara));
  wdSnapshot();
  if (!wdSplice(e, e, t)) w95MsgBox(FS("Table of contents"), FS("Not enough room left"), FS("in this document."), FS(""));
  else wdPara++;
}
void wdTableCol(bool del) {                          // Table > Add / Delete Column (the last one)
  int s = wdParaStart(wdPara), e = wdParaEnd(s);
  wdParse(WK.ed.buf + s, e - s);
  if (wpKind != WK_TABLE) { w95MsgBox(FS("Table"), FS("Put the blue bar on a table"), FS("row first (turn the knob)."), FS("")); return; }
  int a = wdPara, b = wdPara;
  while (a > 0 && WK.ed.buf[wdParaStart(a - 1)] == '|') a--;
  while (b + 1 < wdParas && WK.ed.buf[wdParaStart(b + 1)] == '|') b++;
  wdSnapshot();
  for (int r = b; r >= a; r--) {                     // bottom up: the rows above keep their places
    int ps = wdParaStart(r), pe = wdParaEnd(ps);
    if (!del) { if (!wdSplice(pe, pe, WK.ed.buf[pe - 1] == '|' ? FS("   |") : FS(" |   |"))) break; continue; }
    int q = pe - 1;
    if (q > ps && WK.ed.buf[q] == '|') q--;
    while (q > ps && WK.ed.buf[q] != '|') q--;
    if (q > ps) wdSplice(q + 1, pe, FS(""));          // "| a | b |" -> "| a |"
  }
}
// ---------- symbol picker ----------
void wdSymDraw() {
  int x0 = 40, y0 = 60;
  tft.fillRect(x0 - 6, y0 - 22, 252, 132, W95_FACE); w95Bevel(x0 - 6, y0 - 22, 252, 132, true);
  tft.fillRect(x0 - 4, y0 - 20, 248, 12, W95_NAVY);
  putText(x0, y0 - 18, 1, W95_HI, W95_NAVY, FS("Symbol"));
  for (int k = 0; k < 40; k++) {
    int x = x0 + (k % 10) * 24, y = y0 + (k / 10) * 24;
    bool sel = k == wdSym;
    tft.fillRect(x, y, 22, 22, sel ? W95_NAVY : W95_PAPER);
    tft.drawRect(x, y, 22, 22, W95_SH);
    char s[2] = {WD_SYMS[k], 0};
    putText(x + 5, y + 4, 2, sel ? W95_HI : W95_INK, sel ? W95_NAVY : W95_PAPER, s);
  }
  putText(x0, y0 + 98, 1, W95_INK, W95_FACE, FS("turn: pick   press: insert   hold: cancel"));
}
// ---------- app ----------
void wdCommand(int cmd) {
  int s = wdParaStart(wdPara), e = wdParaEnd(s);
  switch (cmd) {
    // File
    case 0: wordLeave(); edSet(FS("")); wdName[0] = 0; wdPara = 0; wdTop = 0; wdDocx = false; break;
    case 1: wdMode = WDM_PICK; wdPickFor = 0; fpAltExt = PSTR(".docx"); fpBegin(FS("Open document"), FS("/docs"), FS(".doc"), nullptr, W95_NAVY); return;
    case 2: if (!wdName[0]) { wdMode = WDM_ASK; wdAsk = WQ_SAVEAS; askBegin(FS("Save document as (name):"), FS(""), 12, W95_NAVY); return; }
            if (!wdSave()) { wdDrawAll(); w95MsgBox(FS("Save"), FS("Couldn't save the document"), FS("(flash full, or low on memory?)"), FS("")); return; }
            SFX(T_HOME); break;
    case 3: wdMode = WDM_ASK; wdAsk = WQ_SAVEAS; askBegin(FS("Save document as (name):"), wdName, 12, W95_NAVY); return;
    case 4: wdMode = WDM_ASK; wdAsk = WQ_SAVEX; askBegin(FS("Save as Word .docx (name):"), wdName, 12, W95_NAVY); return;
    case 6: wdDrawAll(); wdProperties(); return;
    case 7: case 1000 + 4: wdMode = WDM_PREVIEW; wdPrevPage = 0; wdPreviewDraw(); return;
    case 9: enterApp(APP_MENU); return;
    // Edit
    case 32 + 0: case 1000 + 0:                       // type at the end of the current paragraph
      wdSnapshot();
      edCur = e; wdMode = WDM_EDIT;
      edBegin(KB_LAYOUT_WORD, W95_NAVY, "Word");
      w95Title(FS("Word - typing ('view' key: back to the page)"), W95_NAVY, 'W');
      tft.fillRect(0, 14, W, 4, W95_FACE);
      return;
    case 32 + 1: wdUndo(); break;
    case 32 + 3: wdClipPut(s, e); wdSnapshot(); wdSplice(s, min(e + 1, (int)edLen), FS("")); break;
    case 32 + 4: wdClipPut(s, e); break;
    case 32 + 5: {                                    // paste the clipboard as a new paragraph below
      if (!fsOk) break;
      File f = LittleFS.open(FS("/clip.txt"), FS("r"));
      if (!f) break;
      char* tmp = (char*)WK.ed.con;
      int n = f.readBytes(tmp + 1, sizeof WK.ed.con - 2); f.close();
      tmp[0] = '\n'; tmp[n + 1] = 0;
      wdSnapshot();
      if (wdSplice(e, e, tmp)) wdPara++;
      break;
    }
    case 32 + 6: wdSnapshot(); wdSplice(s, min(e + 1, (int)edLen), FS("")); break;
    case 32 + 8: wdMode = WDM_ASK; wdAsk = WQ_FIND; askBegin(FS("Find what:"), wdFind, 23, W95_NAVY); return;
    case 32 + 9: wdFindNext(false); break;
    case 32 + 10: wdMode = WDM_ASK; wdAsk = WQ_REPL1; askBegin(FS("Replace: find what?"), wdFind, 23, W95_NAVY); return;
    case 32 + 12: wdPara = 0; wdTop = 0; break;
    case 32 + 13: wdPara = wdParas - 1; break;
    // View
    case 64 + 0: wdMarks = !wdMarks; break;
    case 64 + 1: wdZoom = !wdZoom; wdTop = 0; break;
    case 64 + 2: wdRuler = !wdRuler; break;
    // Insert
    case 96 + 0: wdInsertAfter(FS("===")); break;
    case 96 + 1: {
      int y, m, d; civilFromDays(clkEpoch / 86400UL, &y, &m, &d);
      char b[40]; snprintf_P(b, sizeof b, PSTR("%04d-%02d-%02d %02lu:%02lu"), y, m, d, (unsigned long)(clkEpoch / 3600 % 24), (unsigned long)(clkEpoch / 60 % 60));
      wdSnapshot(); if (wdSplice(e, e, e > s ? FS(" ") : FS(""))) { int e2 = wdParaEnd(s); wdSplice(e2, e2, b); }
      break;
    }
    case 96 + 2: wdMode = WDM_SYM; wdSymDraw(); return;
    case 96 + 3: case 192 + 0: case 1000 + 13: wdMode = WDM_ASK; wdAsk = WQ_TABLE; askBegin(FS("Table size, columns x rows (e.g. 3x4):"), FS("3x3"), 8, W95_NAVY); return;
    case 96 + 4: case 1000 + 14: wdMode = WDM_PICK; wdPickFor = 1; fpBegin(FS("Chart from which sheet?"), FS("/sheets"), FS(".csv"), nullptr, W95_NAVY); return;
    case 96 + 5: wdInsertAfter(FS("---")); break;
    case 96 + 6: wdMode = WDM_PICK; wdPickFor = 2; fpBegin(FS("Insert which document?"), FS("/docs"), FS(".doc"), nullptr, W95_NAVY); return;
    case 96 + 7: wdDrawAll(); wdInsertToc(); if (w95Msg) return; break;
    // Format
    case 128 + 0: case 1000 + 5: wdWrapStyle(FS("**")); break;
    case 128 + 1: case 1000 + 6: wdWrapStyle(FS("*")); break;
    case 128 + 2: case 1000 + 7: wdWrapStyle(FS("__")); break;
    case 128 + 4: wdSetPrefix(WK_TEXT, -1); break;
    case 128 + 5: wdSetPrefix(WK_H1, -1); break;
    case 128 + 6: wdSetPrefix(WK_H2, -1); break;
    case 128 + 7: wdSetPrefix(WK_QUOTE, -1); break;
    case 128 + 9: case 1000 + 8: wdSetPrefix(-1, 0); break;
    case 128 + 10: case 1000 + 9: wdSetPrefix(-1, 1); break;
    case 128 + 11: case 1000 + 10: wdSetPrefix(-1, 2); break;
    case 128 + 13: case 1000 + 11: wdParse(WK.ed.buf + s, e - s); wdSetPrefix(wpKind == WK_BULLET ? WK_TEXT : WK_BULLET, -1); break;
    case 128 + 14: case 1000 + 12: wdParse(WK.ed.buf + s, e - s); wdSetPrefix(wpKind == WK_NUM ? WK_TEXT : WK_NUM, -1); break;
    case 128 + 15: wdChangeCase(); break;
    case 128 + 17: wdWrapStyle(FS("~~")); break;
    case 128 + 18: wdWrapStyle(FS("==")); break;
    case 128 + 19: wdClearFormat(); break;
    // Tools
    case 160 + 0: {
      char a[40], b[40];
      snprintf_P(a, sizeof a, PSTR("Words: %d"), wdWordCount());
      snprintf_P(b, sizeof b, PSTR("Characters: %u   Paragraphs: %d"), (unsigned)edLen, wdParas);
      wdDrawAll(); w95MsgBox(FS("Word Count"), a, b, FS("")); return;
    }
    case 160 + 1: wdDrawAll(); wdAutoCorrect(); return;
    case 160 + 2: wdDrawAll(); wdSortBlock(); if (w95Msg) return; break;
    case 160 + 3: {
      int w = wdWordCount(); char a[40], b[40];
      snprintf_P(a, sizeof a, PSTR("About %d min to read"), max(1, (w + 199) / 200));
      snprintf_P(b, sizeof b, PSTR("about %d min to read aloud"), max(1, (w + 129) / 130));
      wdDrawAll(); w95MsgBox(FS("Reading time"), a, b, FS("(200 / 130 words a minute)")); return;
    }
    // Table
    case 192 + 1: wdDrawAll(); wdTableRowAdd(false); if (w95Msg) return; break;
    case 192 + 2: wdDrawAll(); wdTableRowAdd(true); if (w95Msg) return; break;
    case 192 + 3: wdDrawAll(); wdTableTotal(); if (w95Msg) return; break;
    case 192 + 4: case 192 + 5: wdDrawAll(); wdTableCol(cmd == 192 + 5); if (w95Msg) return; break;
    // Help
    case 224 + 0: wdDrawAll(); w95MsgBox(FS("Formatting marks"), FS("# H1 ## H2 - bullet + 1. > quote"), FS("--- line  === new page  <c> centre"), FS("**b** *i* __u__ ~~strike~~ ==mark==")); return;
    case 224 + 1: wdDrawAll(); w95MsgBox(FS("Writing tips"), FS("Turn the knob to pick a paragraph,"), FS("then use Format or the toolbar."), FS("The pencil button types into it.")); return;
    case 224 + 2: wdDrawAll(); w95MsgBox(FS("About Word"), FS("Word for the Psion :3  version 365"), FS("Opens + saves Word .docx files."), FS("Made with paws by Micor & Claude")); return;
    // toolbar file buttons
    case 1000 + 1: wdCommand(0); return;
    case 1000 + 2: wdCommand(1); return;
    case 1000 + 3: wdCommand(2); return;
  }
  wdDrawAll();
}
bool wordWantsBack() { return w95Mode || w95Msg || wdMode != WDM_VIEW; }
void wordBack() {
  if (w95Msg) { w95Msg = false; wdDrawAll(); return; }
  if (w95Mode) { w95Back(); return; }
  if (wdMode == WDM_ASK) askOpen = false;
  if (wdMode == WDM_PICK) fpOpen = false;
  if (wdMode == WDM_EDIT) wdPara = wdParaOf(edCur);
  wdMode = WDM_VIEW; wdDrawAll();
}
bool isDocx(const char* p) {                         // .docx .docm .dotx .dotm, any case
  size_t l = strlen(p);
  return l > 5 && (!strcasecmp_P(p + l - 5, PSTR(".docx")) || !strcasecmp_P(p + l - 5, PSTR(".docm")) ||
                   !strcasecmp_P(p + l - 5, PSTR(".dotx")) || !strcasecmp_P(p + l - 5, PSTR(".dotm")));
}
void wordEnter() {
  wdUse();
  if (openPath[0] && isDocx(openPath)) {             // a Word .docx from the Files app
    char p[64]; snprintf_P(p, sizeof p, PSTR("%s"), openPath); openPath[0] = 0;
    wkOwner = APP_WORD; wdMode = WDM_VIEW; w95Mode = 0; w95Msg = false; w95Focus = 8;
    wdOpenDocx(p);
    return;
  }
  if (openPath[0]) { edLoadFile(openPath); baseName(openPath, wdName, sizeof wdName); openPath[0] = 0; wdPara = 0; wdDocx = false; }
  else if (wkOwner != APP_WORD) {
    char p[40]; snprintf_P(p, sizeof p, PSTR("/docs/%s.%s"), wdName, wdDocx ? FS("docx") : FS("doc"));
    if (wdName[0] && fsOk && LittleFS.exists(p)) { if (wdDocx) { if (docxImport(p)) edSet(nullptr); } else edLoadFile(p); }
    else {
      wdName[0] = 0; wdDocx = false;
      edSetP(PSTR("# Hello :3\nThis is **Word 95** for the Psion. Turn the knob to pick a paragraph, press for the *menus* and the __toolbar__.\n"
            "## Lists\n- bullets start with a dash\n+ numbered items start with a plus\n+ like this\n"
            "## A table\n| Fruit | Paws |\n| apples | 3 |\n| pears | 5 |\n<c>Centred text, too.\n"));
    }
    wdPara = 0; wdTop = 0;
  }
  wkOwner = APP_WORD;
  wdMode = WDM_VIEW; w95Mode = 0; w95Msg = false; w95Focus = 8;   // first press lands on the pencil (Type)
  wdDrawAll();
}
void wordLeave() { if (edDirty) { if (!wdName[0]) strcpy_P(wdName, PSTR("untitled")); wdSave(); } }
void wordTick(uint32_t now) {
  static uint32_t lastMin = 0;
  int c = takeClicks() * ENC_DIR;
  takePress();
  bool sp = takeShort();
  wdUse();
  if (w95Msg) { w95MsgTick(c, sp); return; }
  switch (wdMode) {
    case WDM_VIEW:
      if (w95Mode) { int cmd = w95Tick(c, sp); if (cmd >= 0) wdCommand(cmd); break; }
      if (c) { int old = wdPara; wdPara = constrain(wdPara + c, 0, wdParas - 1); if (!wdQuickPara(old)) wdDrawDoc(); wdStatus(); uiClick(); }
      if (sp) w95Open();
      if (now - lastMin > 30000) { lastMin = now; wdStatus(); }
      break;
    case WDM_EDIT:
      if (edTick(c, sp) == KB_VIEW) { wdPara = wdParaOf(edCur); wdMode = WDM_VIEW; wdDrawAll(); }
      break;
    case WDM_ASK: {
      int r = askTick(c, sp);
      if (!r) break;
      wdMode = WDM_VIEW;
      if (r > 0) switch (wdAsk) {
        case WQ_SAVEAS: case WQ_SAVEX: {
          cleanName(askBuf, wdName);
          if (wdAsk == WQ_SAVEX) wdDocx = true;
          bool ok = wdSave();
          wdDrawAll();
          if (!ok) { w95MsgBox(FS("Save"), FS("Couldn't save the document"), FS("(flash full, or low on memory?)"), FS("")); return; }
          SFX(T_HOME);
          if (wdAsk == WQ_SAVEX) { char m[40]; snprintf_P(m, sizeof m, PSTR("/docs/%s.docx"), wdName); w95MsgBox(FS("Saved as Word"), m, FS("Opens in Word 2007-365 on a PC"), FS("(get it from the PC remote page).")); return; }
          break;
        }
        case WQ_FIND: snprintf_P(wdFind, sizeof wdFind, PSTR("%s"), askBuf); wdDrawAll(); wdFindNext(true); if (w95Msg) return; break;
        case WQ_REPL1: snprintf_P(wdFind, sizeof wdFind, PSTR("%s"), askBuf); wdMode = WDM_ASK; wdAsk = WQ_REPL2; askBegin(FS("Replace with:"), FS(""), 23, W95_NAVY); return;
        case WQ_REPL2: { char with[24]; snprintf_P(with, sizeof with, PSTR("%s"), askBuf); wdDrawAll(); wdReplaceAll(with); return; }
        case WQ_TABLE: {
          int cols = constrain(atoi(askBuf), 1, 6), rows = 3;
          const char* x = strchr(askBuf, 'x'); if (x) rows = constrain(atoi(x + 1), 1, 12);
          char row[48] = "|";
          for (int k = 0; k < cols; k++) { char h[8]; snprintf_P(h, sizeof h, PSTR(" %c |"), 'A' + k); strcat(row, h); }
          wdInsertAfter(row);
          strcpy(row, FS("|")); for (int k = 0; k < cols; k++) strcat(row, FS("   |"));
          for (int r2 = 1; r2 < rows; r2++) { int e = wdParaEnd(wdParaStart(wdPara)); char nb[52]; snprintf_P(nb, sizeof nb, PSTR("\n%s"), row); if (wdSplice(e, e, nb)) wdPara++; }
          break;
        }
      }
      wdDrawAll();
      break;
    }
    case WDM_PICK: {
      int r = fpTick(c, sp);
      if (r == FP_BUSY) break;
      wdMode = WDM_VIEW;
      if (r >= 0 && wdPickFor == 0 && isDocx(fpPath)) { wordLeave(); wdMode = WDM_VIEW; wdOpenDocx(fpPath); return; }
      if (r >= 0 && wdPickFor == 0) { wordLeave(); edLoadFile(fpPath); baseName(fpPath, wdName, sizeof wdName); wdPara = 0; wdTop = 0; wdDocx = false; }
      if (r >= 0 && wdPickFor == 2) {                // insert another document's text below
        File f = LittleFS.open(fpPath, FS("r"));
        if (f) {
          int e = wdParaEnd(wdParaStart(wdPara));
          wdSnapshot();
          int room = ED_CAP - edLen - 1;
          char* tmp = (char*)WK.ed.con;
          int n = f.readBytes(tmp + 1, min(room - 1, (int)sizeof WK.ed.con - 2)); f.close();
          tmp[0] = '\n'; tmp[n + 1] = 0;
          wdSplice(e, e, tmp);
        }
      }
      if (r >= 0 && wdPickFor == 1) {
        baseName(fpPath, wdChartSheet, sizeof wdChartSheet);
        wdDrawAll(); wdMode = WDM_COL;
        pmShowP(FS("Chart which column?"), PSTR("Column A\nColumn B\nColumn C\nColumn D\nColumn E\nColumn F\nColumn G\nColumn H\nCancel"), W95_NAVY);
        return;
      }
      wdDrawAll();
      break;
    }
    case WDM_COL: {
      int r = pmTick(c, sp);
      if (r < 0) break;
      if (r < 8) { wdChartKind = r; wdMode = WDM_PIE; wdDrawAll(); pmShowP(FS("Chart type"), PSTR("Columns\nLine\nPie"), W95_NAVY); return; }
      wdMode = WDM_VIEW; wdDrawAll();
      break;
    }
    case WDM_PIE: {
      int r = pmTick(c, sp);
      if (r < 0) break;
      char m[40]; snprintf_P(m, sizeof m, PSTR("[chart:%s:%c%s]"), wdChartSheet, 'A' + wdChartKind, r == 1 ? FS(":line") : r == 2 ? FS(":pie") : FS(""));
      wdInsertAfter(m);
      SFX(T_HOME);
      wdMode = WDM_VIEW; wdDrawAll();
      break;
    }
    case WDM_SYM:
      if (c) { wdSym = ((wdSym + c) % 40 + 40) % 40; wdSymDraw(); uiClick(); }
      if (sp) {
        int s = wdParaStart(wdPara), e = wdParaEnd(s);
        char b[2] = {WD_SYMS[wdSym], 0};
        wdSnapshot(); wdSplice(e, e, b);
        wdMode = WDM_VIEW; wdDrawAll();
      }
      break;
    case WDM_PREVIEW:
      if (c) { wdPrevPage = max(0, wdPrevPage + (c > 0 ? 2 : -2)); wdPreviewDraw(); uiClick(); }
      if (sp) { wdMode = WDM_VIEW; wdDrawAll(); }
      break;
  }
  if (c || sp) oledDirty = true;
}
void oledWord() {
  char l[4][24];
  snprintf_P(l[0], 24, PSTR("%.16s%s"), wdName[0] ? wdName : FS("untitled"), edDirty ? FS(" *") : FS(""));
  snprintf_P(l[1], 24, PSTR("%d words"), wdWordCount());
  snprintf_P(l[2], 24, PSTR("para %d of %d"), wdPara + 1, wdParas);
  snprintf_P(l[3], 24, PSTR("%s"), wdMode == WDM_EDIT ? (edMove ? FS("moving cursor") : FS("typing")) : w95Mode ? FS("menus") : FS("page view"));
  oledCard(FS("WORD 95"), l[0], l[1], l[2], l[3]);
}

// ================= CODE (OPL-style programming: BASIC + mini C++) =================
// Both languages are interpreted straight from the editor text. Programs share 24 numeric variables,
// a 22-line console and a few Psion-ish extras (BEEP, WAIT, PLOT, LINE, CIRCLE, RECT, COLOR).
// Hold the knob to stop a running program.
const char CODE_EX_BAS[] PROGMEM =
  "REM Paw counter :3\n"
  "CLS\n"
  "PRINT \"Hello from Psion BASIC!\"\n"
  "FOR I = 1 TO 5\n"
  "  PRINT \"paw \"; I; \"  squared = \"; I * I\n"
  "  BEEP 400 + I * 100, 60\n"
  "NEXT I\n"
  "INPUT \"Pick a number\"; N\n"
  "IF N > 10 THEN PRINT \"big number!\" ELSE PRINT \"small number\"\n"
  "COLOR 12\n"
  "FOR A = 0 TO 360 STEP 10\n"
  "  PLOT 160 + COS(A * PI / 180) * 60, 150 + SIN(A * PI / 180) * 40\n"
  "NEXT A\n"
  "PRINT \"done x3\"\n";
const char CODE_EX_CPP[] PROGMEM =
  "#include <iostream>\n"
  "using namespace std;\n"
  "\n"
  "int main() {\n"
  "  cout << \"Hello from mini C++ :3\" << endl;\n"
  "  int total = 0;\n"
  "  for (int i = 1; i <= 5; i++) {\n"
  "    total += i;\n"
  "    cout << \"i = \" << i << \"  total = \" << total << endl;\n"
  "  }\n"
  "  int n;\n"
  "  cout << \"Pick a number: \";\n"
  "  cin >> n;\n"
  "  if (n % 2 == 0) {\n"
  "    cout << n << \" is even\" << endl;\n"
  "  } else {\n"
  "    cout << n << \" is odd\" << endl;\n"
  "  }\n"
  "  color(10);\n"
  "  for (int r = 4; r < 60; r += 6) circle(160, 150, r);\n"
  "  beep(880, 100);\n"
  "  return 0;\n"
  "}\n";
// ---- more examples (Code menu > Load example...) ----
const char BAS_GUESS[] PROGMEM =
  "REM Guess the number (1-100): turn the knob to answer\nCLS\nS = RND(100) + 1\nT = 0\n"
  "PRINT \"I picked a number from 1 to 100.\"\n"
  "10 INPUT \"Your guess\"; G\nT = T + 1\n"
  "IF G < S THEN PRINT \"Higher!\": BEEP 600, 80: GOTO 10\n"
  "IF G > S THEN PRINT \"Lower!\": BEEP 300, 80: GOTO 10\n"
  "PRINT \"Yes! It was \"; S; \" - found in \"; T; \" tries x3\"\nBEEP 880, 100: BEEP 1175, 200\n";
const char BAS_TIMES[] PROGMEM =
  "REM Times table\nCLS\nINPUT \"Table of\"; N\nFOR I = 1 TO 10\n  PRINT N; \" x \"; I; \" = \"; N * I\nNEXT I\nPRINT \"done :3\"\n";
const char BAS_FIB[] PROGMEM =
  "REM Fibonacci: every number is the sum of the two before\nCLS\nA = 0: B = 1\nFOR I = 1 TO 20\n"
  "  PRINT I; \": \"; A\n  C = A + B: A = B: B = C\nNEXT I\n";
const char BAS_PRIMES[] PROGMEM =
  "REM Prime numbers up to 150\nCLS\nFOR N = 2 TO 150\n  P = 1\n  D = 2\n  WHILE D * D <= N AND P = 1\n"
  "    IF N MOD D = 0 THEN P = 0\n    D = D + 1\n  WEND\n  IF P = 1 THEN PRINT N; \" \";\nNEXT N\nPRINT\nPRINT \"x3\"\n";
const char BAS_BOUNCE[] PROGMEM =
  "REM Rainbow bounce: a ball that leaves a trail\nCLS\nX = 40: Y = 40: DX = 4: DY = 3\nFOR T = 1 TO 300\n"
  "  COLOR T MOD 15 + 1\n  CIRCLE X, Y, 6\n  X = X + DX: Y = Y + DY\n"
  "  IF X < 8 OR X > 310 THEN DX = -DX: BEEP 500, 10\n  IF Y < 8 OR Y > 210 THEN DY = -DY: BEEP 700, 10\n"
  "  WAIT 15\nNEXT T\n";
const char BAS_SPIRAL[] PROGMEM =
  "REM Spiral art\nCLS\nFOR I = 0 TO 720 STEP 4\n  COLOR I / 48 MOD 15 + 1\n  R = I / 8\n  A = I * PI / 180\n"
  "  LINE 160, 110, 160 + COS(A) * R, 110 + SIN(A) * R\nNEXT I\nPRINT \"pretty x3\"\n";
const char BAS_PIANO[] PROGMEM =
  "REM Piano: the C major scale with colored keys\nCLS\nFOR D = 0 TO 7\n  S = 2 * D - (D > 2) - (D > 6)\n"
  "  F = 262 * 2 ^ (S / 12)\n  PRINT \"note \"; D + 1; \"  \"; INT(F); \" Hz\"\n  COLOR D + 8\n"
  "  RECT 20 + D * 36, 150, 30, 50\n  BEEP F, 250\nNEXT D\n";
const char BAS_COUNT[] PROGMEM =
  "REM Countdown timer\nCLS\nINPUT \"Seconds\"; S\nFOR I = S TO 1 STEP -1\n  PRINT I; \"...\"\n  BEEP 880, 50\n"
  "  WAIT 950\nNEXT I\nPRINT \"TIME! x3\"\nFOR K = 1 TO 3: BEEP 1320, 150: WAIT 100: NEXT K\n";
const char BAS_DICE[] PROGMEM =
  "REM Roll two dice 200 times: how often is it 7?\nCLS\nS = 0\nFOR I = 1 TO 200\n  A = RND(6) + 1: B = RND(6) + 1\n"
  "  IF A + B = 7 THEN S = S + 1\nNEXT I\nPRINT \"Sevens: \"; S; \" of 200\"\n"
  "PRINT \"That's \"; ROUND(S / 2, 1); \"% (math says 16.7%)\"\n";
const char BAS_TEMP[] PROGMEM =
  "REM Celsius to Fahrenheit table\nCLS\nPRINT \"  C        F\"\nFOR C = -10 TO 40 STEP 5\n  PRINT C, C * 9 / 5 + 32\nNEXT C\n";
const char CPP_GUESS[] PROGMEM =
  "#include <iostream>\nusing namespace std;\n\n// Guess the number: answer with the knob\nint main() {\n"
  "  int secret = random(100) + 1;\n  int tries = 0;\n  int guess = 0;\n  cout << \"Guess my number (1-100)!\" << endl;\n"
  "  while (guess != secret) {\n    cout << \"Your guess: \";\n    cin >> guess;\n    tries++;\n"
  "    if (guess < secret) { cout << \"higher\" << endl; beep(600, 80); }\n"
  "    else if (guess > secret) { cout << \"lower\" << endl; beep(300, 80); }\n  }\n"
  "  cout << \"Got it in \" << tries << \" tries x3\" << endl;\n  beep(1175, 200);\n  return 0;\n}\n";
const char CPP_FIZZ[] PROGMEM =
  "#include <iostream>\nusing namespace std;\n\nint main() {\n  for (int i = 1; i <= 30; i++) {\n"
  "    if (i % 15 == 0) cout << \"FizzBuzz\";\n    else if (i % 3 == 0) cout << \"Fizz\";\n"
  "    else if (i % 5 == 0) cout << \"Buzz\";\n    else cout << i;\n    cout << \" \";\n  }\n  cout << endl;\n  return 0;\n}\n";
const char CPP_FIB[] PROGMEM =
  "#include <iostream>\nusing namespace std;\n\nint main() {\n  double a = 0, b = 1;\n  for (int i = 1; i <= 20; i++) {\n"
  "    cout << i << \": \" << a << endl;\n    double c = a + b;\n    a = b;\n    b = c;\n  }\n  return 0;\n}\n";
const char CPP_PRIMES[] PROGMEM =
  "#include <iostream>\nusing namespace std;\n\nint main() {\n  int count = 0;\n  for (int n = 2; n < 200; n++) {\n"
  "    bool prime = true;\n    for (int d = 2; d * d <= n; d++) {\n      if (n % d == 0) { prime = false; break; }\n    }\n"
  "    if (prime) { cout << n << \" \"; count++; }\n  }\n  cout << endl << count << \" primes below 200\" << endl;\n  return 0;\n}\n";
const char CPP_BOUNCE[] PROGMEM =
  "#include <iostream>\nusing namespace std;\n\n// a ball with a rainbow trail\nint main() {\n"
  "  double x = 160, y = 110, dx = 3.5, dy = 2.5;\n  for (int t = 0; t < 400; t++) {\n    color(t % 15 + 1);\n"
  "    circle(x, y, 5);\n    x += dx;\n    y += dy;\n    if (x < 6 || x > 313) { dx = -dx; beep(500, 8); }\n"
  "    if (y < 6 || y > 213) { dy = -dy; beep(700, 8); }\n    delay(15);\n  }\n  return 0;\n}\n";
const char CPP_STAR[] PROGMEM =
  "#include <iostream>\nusing namespace std;\n\n// a star burst of colored lines\nint main() {\n"
  "  for (int a = 0; a < 360; a += 6) {\n    color(a / 24 % 15 + 1);\n"
  "    line(160, 110, 160 + cos(a * PI / 180) * 100, 110 + sin(a * PI / 180) * 100);\n  }\n  return 0;\n}\n";
const char CPP_FACT[] PROGMEM =
  "#include <iostream>\nusing namespace std;\n\nint main() {\n  double f = 1;\n  for (int n = 1; n <= 15; n++) {\n"
  "    f *= n;\n    cout << n << \"! = \" << f << endl;\n  }\n  return 0;\n}\n";
const char CPP_COLLATZ[] PROGMEM =
  "#include <iostream>\nusing namespace std;\n\n// halve if even, 3n+1 if odd: it always reaches 1 (nobody knows why)\n"
  "int main() {\n  int n;\n  cout << \"Start number: \";\n  cin >> n;\n  if (n < 1) n = 27;\n  int steps = 0;\n"
  "  while (n != 1) {\n    if (n % 2 == 0) n = n / 2;\n    else n = 3 * n + 1;\n    cout << n << \" \";\n    steps++;\n  }\n"
  "  cout << endl << steps << \" steps to reach 1\" << endl;\n  return 0;\n}\n";
const char CPP_WAVES[] PROGMEM =
  "#include <iostream>\nusing namespace std;\n\n// plot sine and cosine\nint main() {\n  color(8);\n  line(0, 110, 319, 110);\n"
  "  for (int x = 0; x < 320; x++) {\n    color(12);\n    plot(x, 110 - sin(x * 0.05) * 60);\n    color(9);\n"
  "    plot(x, 110 - cos(x * 0.05) * 40);\n  }\n  return 0;\n}\n";
const char CPP_TEMP[] PROGMEM =
  "#include <iostream>\nusing namespace std;\n\nint main() {\n  cout << \"  C      F\" << endl;\n"
  "  for (int c = -10; c <= 40; c += 5) {\n    cout << c << \"   \" << c * 9.0 / 5 + 32 << endl;\n  }\n  return 0;\n}\n";
const int CODE_EX_N = 11;
const char* const CODE_EX[2][CODE_EX_N] = {
  {CODE_EX_BAS, BAS_GUESS, BAS_TIMES, BAS_FIB, BAS_PRIMES, BAS_BOUNCE, BAS_SPIRAL, BAS_PIANO, BAS_COUNT, BAS_DICE, BAS_TEMP},
  {CODE_EX_CPP, CPP_GUESS, CPP_FIZZ, CPP_FIB, CPP_PRIMES, CPP_BOUNCE, CPP_STAR, CPP_FACT, CPP_COLLATZ, CPP_WAVES, CPP_TEMP}};
const char CODE_EX_NAMES_P[2][230] PROGMEM = {
  "Paw counter (tour)\nGuess the number\nTimes table\nFibonacci\nPrime numbers\nRainbow bounce\nSpiral art\nPiano scale\nCountdown timer\nDice statistics\nCelsius table",
  "Hello + even/odd (tour)\nGuess the number\nFizzBuzz\nFibonacci\nPrime numbers\nRainbow bounce\nStar burst\nFactorials\nCollatz steps\nSine waves\nCelsius table"};

// ---------- runtime shared by both languages ----------
const int RT_VARS = 24;
struct RtVar { char n[10]; double v; bool isInt; };
RtVar rtV[RT_VARS];
int rtNV = 0, conLine = 0, conCol = 0, rtErrLine = 0, rtSkipping = 0;
bool rtStop = false, rtErrFlag = false;
char rtErrMsg[40];
uint32_t rtSteps = 0;
uint16_t rtColor = 0;
uint8_t cdLang = 0;                                  // 0 BASIC, 1 C++
const char* bp = "";                                 // BASIC position
const char* cp = "";                                 // C++ position
const uint16_t RT_PAL[16] = {
  RGB565(0, 0, 0), RGB565(0, 0, 170), RGB565(0, 170, 0), RGB565(0, 170, 170), RGB565(170, 0, 0), RGB565(170, 0, 170),
  RGB565(170, 85, 0), RGB565(170, 170, 170), RGB565(85, 85, 85), RGB565(85, 85, 255), RGB565(85, 255, 85),
  RGB565(85, 255, 255), RGB565(255, 85, 85), RGB565(255, 85, 255), RGB565(255, 255, 85), RGB565(255, 255, 255)
};

void rtError(const char* m) {
  if (rtErrFlag) return;
  rtErrFlag = true; rtStop = true;
  snprintf_P(rtErrMsg, sizeof rtErrMsg, PSTR("%s"), m);
  const char* at = cdLang ? cp : bp;
  rtErrLine = 1;
  for (const char* q = WK.ed.buf; q < at && *q; q++) if (*q == '\n') rtErrLine++;
}
int rtFind(const char* n, int len, bool create) {
  if (len > 9) len = 9;
  for (int i = 0; i < rtNV; i++) {
    if ((int)strlen(rtV[i].n) != len) continue;
    if (cdLang ? !strncmp(rtV[i].n, n, len) : !strncasecmp(rtV[i].n, n, len)) return i;
  }
  if (!create) return -1;
  if (rtNV >= RT_VARS) { rtError(FS("too many variables")); return -1; }
  memcpy(rtV[rtNV].n, n, len); rtV[rtNV].n[len] = 0; rtV[rtNV].v = 0; rtV[rtNV].isInt = false;
  return rtNV++;
}
void rtSet(int i, double v) { if (i >= 0) rtV[i].v = rtV[i].isInt ? trunc(v) : v; }
bool rtVarCb(const char* n, int len, double* v) {
  int i = rtFind(n, len, false);
  if (i >= 0) { *v = rtV[i].v; return true; }
  if (cdLang == 0 || rtSkipping) { *v = 0; return true; }   // BASIC: new variables start at 0
  return false;
}
// console: 22 lines x 50 columns under the title bar
void conDrawLine(int l) {
  tft.fillRect(0, 20 + l * 10, W, 10, E_WHITE);
  putText(6, 21 + l * 10, 1, C_INK, E_WHITE, WK.ed.con[l]);
}
void conClear() {
  memset(WK.ed.con, 0, sizeof WK.ed.con);
  conLine = 0; conCol = 0;
  tft.fillRect(0, 20, W, H - 20, E_WHITE);
}
void conNewline() {
  conCol = 0;
  if (++conLine >= 22) {
    memmove(WK.ed.con[0], WK.ed.con[1], 21 * 51);
    memset(WK.ed.con[21], 0, 51);
    conLine = 21;
    for (int l = 0; l < 22; l++) conDrawLine(l);
  }
}
void conPutc(char ch) {
  if (ch == '\n') { conNewline(); return; }
  if (conCol >= 50) conNewline();
  WK.ed.con[conLine][conCol] = ch; WK.ed.con[conLine][conCol + 1] = 0;
  tft.drawChar(6 + conCol * 6, 21 + conLine * 10, ch, C_INK, E_WHITE, 1);
  conCol++;
}
void conPrint(const char* s) { while (*s) conPutc(*s++); }
void conNum(double v) { char b[24]; fmtNum(v, b, sizeof b, 8); conPrint(b); }
bool rtPoll() {                                      // keeps WiFi/sound alive and lets you stop the program
  if (++rtSteps % 48) return !rtStop;
  yield(); updateButton(); updateSound(); tickClock();
  takeShort(); takePress();
  if (fLong) { fLong = false; rtError(FS("stopped (knob held)")); }
  return !rtStop;
}
void rtWait(uint32_t ms) {
  uint32_t t0 = millis();
  while (millis() - t0 < ms && !rtStop) { delay(1); rtSteps = 47; rtPoll(); }
}
void rtBeep(double f, double ms) { soundOn((uint16_t)constrain(f, 40.0, 8000.0)); rtWait((uint32_t)constrain(ms, 0.0, 5000.0)); soundOff(); }
double rtInput() {                                   // numbers come from the knob: turn = change, press = OK
  double v = 0;
  int row = conLine, col = conCol;
  char b[24];
  conPrint(FS("? "));
  col += 2;
  takeClicks(); takeShort();
  for (bool redraw = true;; ) {
    if (redraw) {
      snprintf_P(b, sizeof b, PSTR("%.0f   "), v);
      putText(6 + min(col, 40) * 6, 21 + row * 10, 1, rtColor, E_WHITE, b);
      putText(6, 229, 1, C_MID, E_WHITE, FS("INPUT: turn = change number   press = OK   "));
      redraw = false;
    }
    delay(1); yield(); updateButton(); updateSound();
    int c = takeClicks() * ENC_DIR;
    if (c) { v += abs(c) > 2 ? c * 10 : c; redraw = true; uiClick(); }
    if (takeShort()) break;
    if (fLong) { fLong = false; rtError(FS("stopped (knob held)")); break; }
  }
  tft.fillRect(0, 228, W, 10, E_WHITE);
  conPrint(b); conPutc('\n');
  return v;
}
// graphics use program coordinates 0..319 x 0..219 inside the console area
void gfx(int op, const double* a) {
  int x = (int)a[0], y = (int)a[1] + 20;
  switch (op) {
    case 0: tft.drawPixel(x, y, rtColor); break;
    case 1: tft.drawLine(x, y, (int)a[2], (int)a[3] + 20, rtColor); break;
    case 2: tft.drawCircle(x, y, (int)a[2], rtColor); break;
    case 3: tft.drawRect(x, y, (int)a[2], (int)a[3], rtColor); break;
    case 4: rtColor = RT_PAL[((int)a[0]) & 15]; break;
  }
}

// ---------- BASIC ----------
const int BAS_LINES = 160, BAS_STACK = 8;
uint16_t bStart[BAS_LINES];
int16_t bNum[BAS_LINES];
int bN = 0, bLine = 0, fsp = 0, gsp = 0, wsp = 0;
bool bJumped = false, bChain = false;
const char* bStmt = "";
int forVar[BAS_STACK], forLine[BAS_STACK], gsLine[BAS_STACK], wLine[BAS_STACK];
double forTo[BAS_STACK], forStep[BAS_STACK];
const char* forPos[BAS_STACK];
const char* gsPos[BAS_STACK];
const char* wPos[BAS_STACK];

void bSkip() { while (*bp == ' ' || *bp == '\t' || *bp == '\r') bp++; }
bool bEol() { bSkip(); return !*bp || *bp == '\n'; }
bool bKw(const char* w) { exP = bp; if (!exWord(w)) return false; bp = exP; return true; }
bool bChr(char c) { bSkip(); if (*bp != c) return false; bp++; return true; }
double bExpr() {
  exP = bp; exErr = nullptr; exDepth = 0;
  double v = exOr();
  bp = exP;
  if (exErr) rtError(exErr);
  return v;
}
void bToEol() { while (*bp && *bp != '\n') bp++; }
const char* basBody(int ln) {
  const char* p = WK.ed.buf + bStart[ln];
  while (*p == ' ' || *p == '\t') p++;
  if (bNum[ln] >= 0) while (isdigit((uint8_t)*p)) p++;
  return p;
}
void basIndex() {
  bN = 0;
  int i = 0;
  while (i <= edLen && bN < BAS_LINES) {
    bStart[bN] = i;
    const char* p = WK.ed.buf + i;
    while (*p == ' ' || *p == '\t') p++;
    bNum[bN] = isdigit((uint8_t)*p) ? (int16_t)atoi(p) : -1;
    bN++;
    while (i < edLen && WK.ed.buf[i] != '\n') i++;
    i++;
  }
}
void basGoto(int n) {
  for (int i = 0; i < bN; i++) if (bNum[i] == n) { bLine = i; bp = basBody(i); bJumped = true; return; }
  char m[32]; snprintf_P(m, sizeof m, PSTR("no line %d"), n); rtError(m);
}
int basName(const char** s) { bSkip(); *s = bp; if (!exAl(*bp)) return 0; while (exAlnum(*bp)) bp++; return bp - *s; }
void basAssign() {
  const char* s;
  int len = basName(&s);
  if (!len) { rtError(FS("syntax error")); return; }
  if (!bChr('=')) { rtError(FS("expected =")); return; }
  double v = bExpr();
  if (!rtErrFlag) rtSet(rtFind(s, len, true), v);
}
bool bAtElse() { exP = bp; return exWord("ELSE"); }
void basPrint() {
  bool nl = true;
  while (!rtStop) {
    if (bEol() || *bp == ':' || bAtElse()) break;
    nl = true;
    if (*bp == '"') { bp++; while (*bp && *bp != '"' && *bp != '\n') conPutc(*bp++); if (*bp == '"') bp++; }
    else { double v = bExpr(); if (rtErrFlag) return; conNum(v); }
    bSkip();
    if (*bp == ';') { bp++; nl = false; }
    else if (*bp == ',') { bp++; conPrint(FS("  ")); nl = false; }
    else break;
  }
  if (nl) conPutc('\n');
}
void basIf() {
  double c = bExpr();
  if (rtErrFlag) return;
  if (!bKw("THEN")) { rtError(FS("IF needs THEN")); return; }
  if (c != 0) { bSkip(); if (isdigit((uint8_t)*bp)) basGoto((int)bExpr()); else bChain = true; return; }
  bool str = false;                                  // false: look for ELSE on this line
  for (const char* q = bp; *q && *q != '\n'; q++) {
    if (*q == '"') str = !str;
    else if (!str && !strncasecmp_P(q, PSTR("ELSE"), 4) && !exAlnum(q[4]) && !exAlnum(q[-1])) {
      bp = q + 4; bSkip();
      if (isdigit((uint8_t)*bp)) basGoto((int)bExpr()); else bChain = true;
      return;
    }
  }
  bToEol();
}
bool basFindWend() {                                 // skip a WHILE whose condition is false
  int depth = 0;
  for (int l = bLine; l < bN; l++) {
    const char* q = l == bLine ? bp : basBody(l);
    for (; *q && *q != '\n'; q++) {
      if (q != WK.ed.buf && exAlnum(q[-1])) continue;
      if (!strncasecmp_P(q, PSTR("WHILE"), 5) && !exAlnum(q[5])) depth++;
      else if (!strncasecmp_P(q, PSTR("WEND"), 4) && !exAlnum(q[4])) {
        if (depth-- == 0) { bLine = l; bp = q + 4; bJumped = true; return true; }
      }
    }
  }
  return false;
}
void basStmt() {
  bSkip();
  while (*bp == ':') { bp++; bSkip(); }
  if (bEol()) return;
  bStmt = bp;
  double a[4];
  if (*bp == '\'' || bKw("REM")) { bToEol(); return; }
  if (bKw("PRINT") || bChr('?')) { basPrint(); return; }
  if (bKw("LET")) { basAssign(); return; }
  if (bKw("INPUT")) {
    bSkip();
    if (*bp == '"') { bp++; while (*bp && *bp != '"' && *bp != '\n') conPutc(*bp++); if (*bp == '"') bp++; bSkip(); if (*bp == ';' || *bp == ',') bp++; }
    const char* s;
    int len = basName(&s);
    if (!len) { rtError(FS("INPUT needs a variable")); return; }
    int vi = rtFind(s, len, true);
    rtSet(vi, rtInput());
    return;
  }
  if (bKw("IF")) { basIf(); return; }
  if (bKw("ELSE")) { bToEol(); return; }             // reached the ELSE part of a true IF
  if (bKw("GOTO")) { basGoto((int)bExpr()); return; }
  if (bKw("GOSUB")) {
    int n = (int)bExpr();
    if (gsp >= BAS_STACK) { rtError(FS("GOSUB too deep")); return; }
    gsLine[gsp] = bLine; gsPos[gsp++] = bp;
    basGoto(n);
    return;
  }
  if (bKw("RETURN")) {
    if (!gsp) { rtError(FS("RETURN without GOSUB")); return; }
    gsp--; bLine = gsLine[gsp]; bp = gsPos[gsp]; bJumped = true;
    return;
  }
  if (bKw("FOR")) {
    const char* s;
    int len = basName(&s);
    if (!len || !bChr('=')) { rtError(FS("FOR I = a TO b")); return; }
    double from = bExpr();
    if (!bKw("TO")) { rtError(FS("FOR needs TO")); return; }
    double to = bExpr(), step = 1;
    if (bKw("STEP")) step = bExpr();
    int vi = rtFind(s, len, true);
    if (rtErrFlag) return;
    rtSet(vi, from);
    while (fsp && forVar[fsp - 1] == vi) fsp--;          // re-entering the same loop
    if (fsp >= BAS_STACK) { rtError(FS("FOR too deep")); return; }
    forVar[fsp] = vi; forTo[fsp] = to; forStep[fsp] = step; forLine[fsp] = bLine; forPos[fsp] = bp; fsp++;
    return;
  }
  if (bKw("NEXT")) {
    const char* s;
    basName(&s);
    if (!fsp) { rtError(FS("NEXT without FOR")); return; }
    int t = fsp - 1, vi = forVar[t];
    rtSet(vi, rtV[vi].v + forStep[t]);
    double v = rtV[vi].v;
    if (forStep[t] >= 0 ? v <= forTo[t] : v >= forTo[t]) { bLine = forLine[t]; bp = forPos[t]; bJumped = true; }
    else fsp--;
    return;
  }
  if (bKw("WHILE")) {
    const char* at = bStmt;
    double c = bExpr();
    if (rtErrFlag) return;
    if (c) {
      if (wsp >= BAS_STACK) { rtError(FS("WHILE too deep")); return; }
      wLine[wsp] = bLine; wPos[wsp++] = at;
    } else if (!basFindWend()) rtError(FS("WHILE without WEND"));
    return;
  }
  if (bKw("WEND")) {
    if (!wsp) { rtError(FS("WEND without WHILE")); return; }
    wsp--; bLine = wLine[wsp]; bp = wPos[wsp]; bJumped = true;
    return;
  }
  if (bKw("END") || bKw("STOP")) { rtStop = true; return; }
  if (bKw("CLS")) { conClear(); return; }
  struct Cmd { const char* kw; int n, op; };
  static const Cmd CMDS[8] = {{"BEEP", 2, 10}, {"WAIT", 1, 11}, {"PAUSE", 1, 11}, {"PLOT", 2, 0}, {"LINE", 4, 1},
                              {"CIRCLE", 3, 2}, {"RECT", 4, 3}, {"COLOR", 1, 4}};
  for (int k = 0; k < 8; k++) {
    if (!bKw(CMDS[k].kw)) continue;
    for (int i = 0; i < CMDS[k].n; i++) { if (i && !bChr(',')) { rtError(FS("missing ,")); return; } a[i] = bExpr(); if (rtErrFlag) return; }
    if (CMDS[k].op == 10) rtBeep(a[0], a[1]);
    else if (CMDS[k].op == 11) rtWait((uint32_t)max(0.0, a[0]));
    else gfx(CMDS[k].op, a);
    return;
  }
  basAssign();                                       // A = 5 without LET
}
void basRun() {
  basIndex();
  bLine = 0; bp = basBody(0); fsp = gsp = wsp = 0;
  while (!rtStop && rtPoll()) {
    if (bEol()) { if (++bLine >= bN) break; bp = basBody(bLine); continue; }
    bJumped = false; bChain = false;
    basStmt();
    if (rtStop || bJumped || bChain) continue;
    bSkip();
    if (*bp == ':') bp++;
    else if (bAtElse()) bToEol();                    // end of a true IF ... THEN part
    else if (!bEol()) rtError(FS("unexpected text"));
  }
}

// ---------- mini C++ ----------
int cxDepth = 0;
bool cxBreak = false, cxCont = false, cxRet = false, cxNoSemi = false;
void cxWs() {
  for (;;) {
    while (*cp == ' ' || *cp == '\t' || *cp == '\n' || *cp == '\r') cp++;
    if (cp[0] == '/' && cp[1] == '/') { while (*cp && *cp != '\n') cp++; continue; }
    if (cp[0] == '/' && cp[1] == '*') { cp += 2; while (*cp && !(cp[0] == '*' && cp[1] == '/')) cp++; if (*cp) cp += 2; continue; }
    if (*cp == '#') { while (*cp && *cp != '\n') cp++; continue; }       // #include etc.
    break;
  }
}
bool cxKw(const char* w) { cxWs(); int n = strlen(w); if (strncmp(cp, w, n) || exAlnum(cp[n])) return false; cp += n; return true; }
bool cxCh(char c) { cxWs(); if (*cp != c) return false; cp++; return true; }
void cxNeed(char c) { if (!cxCh(c) && !rtStop) { char m[16]; snprintf_P(m, sizeof m, PSTR("missing %c"), c); rtError(m); } }
double cxExpr() {
  cxWs();
  exP = cp; exErr = nullptr; exDepth = 0;
  double v = exOr();
  cp = exP;
  if (exErr && !rtSkipping) rtError(exErr);
  return v;
}
void cxStd() { if (cxKw("std")) { cxCh(':'); cxCh(':'); } }
bool cxType(bool* isInt) {
  bool any = false;
  if (cxKw("const")) any = true;
  if (cxKw("unsigned") || cxKw("signed")) { any = true; *isInt = true; }
  if (cxKw("int") || cxKw("long") || cxKw("short") || cxKw("char") || cxKw("bool") || cxKw("size_t")) {
    *isInt = true; cxKw("long"); cxKw("int"); return true;
  }
  if (cxKw("float") || cxKw("double") || cxKw("auto")) { *isInt = false; return true; }
  if (cxKw("void")) { *isInt = false; return true; }
  return any;
}
void cxStmt(bool run);
void cxFunc(const char* s, int len, bool run) {     // only main() runs; other functions are skipped
  int depth = 0;
  do { if (*cp == '(') depth++; else if (*cp == ')') depth--; cp++; } while (*cp && depth);
  cxNoSemi = true;
  if (cxCh(';')) return;                             // a prototype
  bool isMain = len == 4 && !strncmp_P(s, PSTR("main"), 4);
  cxStmt(run && isMain);
  if (isMain && run) cxRet = true;
}
void cxCall(const char* s, int len, bool run) {
  double a[4] = {0, 0, 0, 0};
  int n = 0;
  if (!cxCh(')')) { do { double v = cxExpr(); if (n < 4) a[n] = v; n++; } while (cxCh(',')); cxNeed(')'); }
  if (!run || rtStop) return;
  char f[10];
  snprintf_P(f, sizeof f, PSTR("%.*s"), min(len, 9), s);
  if (!strcmp_P(f, PSTR("beep"))) rtBeep(a[0], a[1]);
  else if (!strcmp_P(f, PSTR("delay")) || !strcmp_P(f, PSTR("sleep"))) rtWait((uint32_t)max(0.0, a[0]));
  else if (!strcmp_P(f, PSTR("cls")) || !strcmp_P(f, PSTR("clear"))) conClear();
  else if (!strcmp_P(f, PSTR("plot"))) gfx(0, a);
  else if (!strcmp_P(f, PSTR("line"))) gfx(1, a);
  else if (!strcmp_P(f, PSTR("circle"))) gfx(2, a);
  else if (!strcmp_P(f, PSTR("rect"))) gfx(3, a);
  else if (!strcmp_P(f, PSTR("color"))) gfx(4, a);
  else if (!strcmp_P(f, PSTR("srand"))) randomSeed((long)a[0]);
  else rtError(FS("unknown function"));
}
void cxSimple(bool run) {                            // declaration / assignment / ++ / call, no ';'
  bool isInt = false, decl = cxType(&isInt);
  do {
    cxWs();
    int pre = 0;
    if (cp[0] == '+' && cp[1] == '+') { pre = 1; cp += 2; }
    else if (cp[0] == '-' && cp[1] == '-') { pre = -1; cp += 2; }
    cxWs();
    if (!exAl(*cp)) { if (decl || pre) rtError(FS("expected a name")); return; }
    const char* s = cp;
    while (exAlnum(*cp)) cp++;
    int len = cp - s;
    cxWs();
    if (*cp == '(') { if (decl) cxFunc(s, len, run); else { cp++; cxCall(s, len, run); } return; }
    int vi = -1;
    if (run) {
      vi = rtFind(s, len, decl);
      if (vi < 0) { rtError(FS("unknown variable")); return; }
      if (decl) rtV[vi].isInt = isInt;
    }
    if (pre) { if (run) rtSet(vi, rtV[vi].v + pre); }
    else if (cp[0] == '+' && cp[1] == '+') { cp += 2; if (run) rtSet(vi, rtV[vi].v + 1); }
    else if (cp[0] == '-' && cp[1] == '-') { cp += 2; if (run) rtSet(vi, rtV[vi].v - 1); }
    else if (cp[0] && cp[1] == '=' && strchr("+-*/%", cp[0])) {
      char op = cp[0]; cp += 2;
      double v = cxExpr();
      if (run) {
        double o = rtV[vi].v;
        o = op == '+' ? o + v : op == '-' ? o - v : op == '*' ? o * v : op == '/' ? o / v : (v ? fmod(o, v) : NAN);
        rtSet(vi, o);
      }
    }
    else if (cp[0] == '=' && cp[1] != '=') { cp++; double v = cxExpr(); if (run) rtSet(vi, v); }
    else if (decl) { if (run) rtSet(vi, 0); }
    else { rtError(FS("expected = or ++")); return; }
  } while (decl && cxCh(','));
}
void cxString(bool run) {                            // "text with \n escapes"
  cp++;
  while (*cp && *cp != '"' && *cp != '\n') {
    char ch = *cp++;
    if (ch == '\\' && *cp) { ch = *cp++; ch = ch == 'n' ? '\n' : ch == 't' ? ' ' : ch; }
    if (run) conPutc(ch);
  }
  if (*cp == '"') cp++;
}
void cxCout(bool run) {
  while (!rtStop) {
    cxWs();
    if (cp[0] != '<' || cp[1] != '<') break;
    cp += 2; cxWs(); cxStd();
    if (*cp == '"') cxString(run);
    else if (cxKw("endl")) { if (run) conPutc('\n'); }
    else if (*cp == '\'') { cp++; if (run && *cp) conPutc(*cp); if (*cp) cp++; if (*cp == '\'') cp++; }
    else { double v = cxExpr(); if (run && !rtStop) conNum(v); }
  }
  cxNeed(';');
}
void cxCin(bool run) {
  while (!rtStop) {
    cxWs();
    if (cp[0] != '>' || cp[1] != '>') break;
    cp += 2; cxWs();
    const char* s = cp;
    while (exAlnum(*cp)) cp++;
    if (cp == s) { rtError(FS("cin needs a variable")); return; }
    if (run) { int vi = rtFind(s, cp - s, true); rtSet(vi, rtInput()); }
  }
  cxNeed(';');
}
void cxStmt(bool run) {
  if (rtStop) return;
  if (run && !rtPoll()) return;
  if (++cxDepth > 10) { rtError(FS("nested too deep")); cxDepth--; return; }   // the ESP8266 stack is only 4 KB
  if (!run) rtSkipping++;
  cxWs();
  if (!*cp) {}
  else if (cxCh('{')) {
    for (;;) {
      cxWs();
      if (!*cp) { rtError(FS("missing }")); break; }
      if (cxCh('}') || rtStop) break;
      cxStmt(run && !cxBreak && !cxCont && !cxRet);
    }
  }
  else if (cxCh(';')) {}
  else if (cxKw("using")) { while (*cp && *cp != ';') cp++; cxNeed(';'); }
  else if (cxKw("if")) {
    cxNeed('(');
    double c = cxExpr();
    cxNeed(')');
    cxStmt(run && c != 0);
    if (cxKw("else")) cxStmt(run && c == 0);
  }
  else if (cxKw("while")) {
    cxNeed('(');
    const char* condPos = cp;
    for (;;) {
      cp = condPos;
      double c = cxExpr();
      cxNeed(')');
      if (!run || !c || rtStop) { cxStmt(false); break; }
      cxStmt(true);
      if (cxRet || rtStop) break;
      if (cxBreak) { cxBreak = false; break; }
      cxCont = false;
    }
  }
  else if (cxKw("do")) {
    const char* bodyPos = cp;
    for (;;) {
      cp = bodyPos;
      cxStmt(run);
      bool stop = cxRet || rtStop || cxBreak;
      cxBreak = false; cxCont = false;
      if (!cxKw("while")) { rtError(FS("do needs while")); break; }
      cxNeed('(');
      double c = cxExpr();
      cxNeed(')'); cxNeed(';');
      if (!run || stop || !c) break;
    }
  }
  else if (cxKw("for")) {
    cxNeed('(');
    cxSimple(run);
    cxNeed(';');
    const char* condPos = cp;
    rtSkipping++; cxWs(); if (*cp != ';') cxExpr(); rtSkipping--;
    cxNeed(';');
    const char* stepPos = cp;
    rtSkipping++; cxWs(); if (*cp != ')') cxSimple(false); rtSkipping--;
    cxNeed(')');
    const char* bodyPos = cp;
    for (;;) {
      cp = condPos; cxWs();
      double c = (*cp == ';') ? 1 : cxExpr();
      cp = bodyPos;
      if (!run || !c || rtStop) { cxStmt(false); break; }
      cxStmt(true);
      if (cxRet || rtStop) break;
      if (cxBreak) { cxBreak = false; break; }
      cxCont = false;
      const char* end = cp;
      cp = stepPos; cxWs(); if (*cp != ')') cxSimple(true);
      cp = end;
    }
  }
  else if (cxKw("return")) { cxWs(); if (*cp != ';') cxExpr(); if (run) cxRet = true; cxNeed(';'); }
  else if (cxKw("break")) { if (run) cxBreak = true; cxNeed(';'); }
  else if (cxKw("continue")) { if (run) cxCont = true; cxNeed(';'); }
  else {
    cxStd();
    if (cxKw("cout")) cxCout(run);
    else if (cxKw("cin")) cxCin(run);
    else { cxNoSemi = false; cxSimple(run); if (!cxNoSemi) cxNeed(';'); cxNoSemi = false; }
  }
  if (!run) rtSkipping--;
  cxDepth--;
}
void cxRun() {
  cp = WK.ed.buf; cxDepth = 0; cxBreak = cxCont = cxRet = false; rtSkipping = 0;
  while (!rtStop && !cxRet) { cxWs(); if (!*cp) break; cxStmt(true); }
}

// ---------- the Code app ----------
enum { CDP_EDIT, CDP_MENU, CDP_ASK, CDP_PICK, CDP_DONE, CDP_HELP, CDP_EXAMPLE };
char cdName[16] = "";
int cdPage = CDP_EDIT;
uint16_t cdAccent() { return appAccent(APP_CODE); }
const char* cdExt() { return cdLang ? ".cpp" : ".bas"; }
void cdLoadExample(int k) {
  char* b = WK.ed.buf;
  strncpy_P(b, CODE_EX[cdLang ? 1 : 0][constrain(k, 0, CODE_EX_N - 1)], ED_CAP);
  b[ED_CAP] = 0;
  edLen = strlen(b); edCur = 0; edTop = 0; edDirty = true;
}
bool cdSave() {
  if (!cdName[0]) return false;
  char p[40]; snprintf_P(p, sizeof p, PSTR("/code/%s%s"), cdName, cdExt());
  return edSaveFile("/code", p);
}
void cdEdit() {
  cdPage = CDP_EDIT;
  char t[24]; snprintf_P(t, sizeof t, PSTR("Code %.12s%s"), cdName[0] ? cdName : FS("untitled"), cdExt());
  tft.fillScreen(C_LCD);
  drawTitleBar(t, cdAccent());
  edBegin(KB_LAYOUT_CODE, cdAccent(), cdLang ? "C++" : "BASIC");
  oledDirty = true;
}
void cdRun() {
  if (cdName[0] && edDirty) cdSave();
  char t[24]; snprintf_P(t, sizeof t, PSTR("Run %.12s"), cdName[0] ? cdName : FS("program"));
  tft.fillScreen(E_WHITE);
  drawTitleBar(t, cdAccent());
  conClear();
  rtNV = 0; rtStop = false; rtErrFlag = false; rtSteps = 0; rtColor = C_INK;
  exVar = rtVarCb; exCell = nullptr; exDeg = false;
  stopSound();
  if (cdLang) cxRun(); else basRun();
  soundOff();
  tft.fillRect(0, 226, W, 14, rtErrFlag ? E_CORAL : appAccent(APP_CODE));
  char m[56];
  if (rtErrFlag) snprintf_P(m, sizeof m, PSTR("line %d: %.34s"), rtErrLine, rtErrMsg);
  else snprintf_P(m, sizeof m, PSTR("program finished :3   press: back to editor"));
  putText(6, 229, 1, E_ONACC, rtErrFlag ? E_CORAL : appAccent(APP_CODE), m);
  fLong = false; takeShort(); takeClicks();
  cdPage = CDP_DONE;
  oledDirty = true;
}
void codeEnter() {
  if (openPath[0]) {
    cdLang = strstr_P(openPath, PSTR(".cpp")) ? 1 : 0;
    edLoadFile(openPath); baseName(openPath, cdName, sizeof cdName); openPath[0] = 0;
  } else if (wkOwner != APP_CODE) {
    char p[40]; snprintf_P(p, sizeof p, PSTR("/code/%s%s"), cdName, cdExt());
    if (cdName[0] && fsOk && LittleFS.exists(p)) edLoadFile(p);
    else { cdName[0] = 0; cdLoadExample(0); edDirty = false; }
  }
  wkOwner = APP_CODE;
  cdEdit();
}
void codeLeave() { if (edDirty) { if (!cdName[0]) strcpy_P(cdName, PSTR("untitled")); cdSave(); } }
void codeTick(uint32_t now) {
  int c = takeClicks() * ENC_DIR;
  takePress();
  bool sp = takeShort();
  (void)now;
  switch (cdPage) {
    case CDP_EDIT:
      if (edTick(c, sp) == KB_MENU) { cdPage = CDP_MENU; pmShowP(cdLang ? FS("Code - C++") : FS("Code - BASIC"), PSTR("Run program\nSave\nSave as...\nOpen BASIC...\nOpen C++...\nNew BASIC\nNew C++\nLoad example\nLanguage help\nClose menu"), cdAccent()); }
      break;
    case CDP_MENU: {
      int r = pmTick(c, sp);
      if (r < 0) break;
      switch (r) {
        case 0: cdRun(); return;
        case 1: if (!cdName[0]) { cdPage = CDP_ASK; askBegin(FS("Save program as (name):"), FS(""), 12, cdAccent()); return; }
                cdSave(); SFX(T_HOME); break;
        case 2: cdPage = CDP_ASK; askBegin(FS("Save program as (name):"), cdName, 12, cdAccent()); return;
        case 3: case 4:
          cdPage = CDP_PICK;
          fpBegin(r == 3 ? FS("Open BASIC program") : FS("Open C++ program"), FS("/code"), r == 3 ? FS(".bas") : FS(".cpp"), nullptr, cdAccent());
          return;
        case 5: case 6: codeLeave(); cdLang = r == 6; cdName[0] = 0; edSet(cdLang ? "#include <iostream>\nusing namespace std;\n\nint main() {\n  \n  return 0;\n}\n" : "REM my program\n"); break;
        case 7: cdPage = CDP_EXAMPLE; pmShowP(cdLang ? FS("C++ examples") : FS("BASIC examples"), CODE_EX_NAMES_P[cdLang ? 1 : 0], cdAccent()); return;
        case 8: cdPage = CDP_HELP; pmShowP(cdLang ? FS("mini C++ cheat sheet") : FS("BASIC cheat sheet"), cdLang ? PSTR("cout << \"hi\" << x << endl;\ncin >> x;\nint float double bool\nif (c) .. else ..\nfor (int i=0;i<9;i++) ..\nwhile (c) ..  do..while\n+= -= ++ -- % && || !\nbeep(f,ms) delay(ms) cls()\nplot line circle rect color\nOK") : PSTR("PRINT \"hi\"; X\nINPUT \"n\"; N\nIF X>1 THEN .. ELSE ..\nFOR I=1 TO 9 STEP 2: NEXT\nWHILE c .. WEND   GOTO 10\nGOSUB 100 .. RETURN   END\nCLS  BEEP f,ms  WAIT ms\nPLOT x,y  LINE x1,y1,x2,y2\nCIRCLE x,y,r  RECT  COLOR n\nOK"), cdAccent()); return;
      }
      cdEdit();
      break;
    }
    case CDP_HELP:
      if (pmTick(c, sp) >= 0) cdEdit();
      break;
    case CDP_EXAMPLE: {
      int r = pmTick(c, sp);
      if (r < 0) break;
      codeLeave(); cdName[0] = 0; cdLoadExample(r);   // your own program was saved first (as "untitled" if it had no name)
      cdEdit();
      break;
    }
    case CDP_ASK: {
      int r = askTick(c, sp);
      if (!r) break;
      if (r > 0) { cleanName(askBuf, cdName); cdSave(); SFX(T_HOME); }
      cdEdit();
      break;
    }
    case CDP_PICK: {
      int r = fpTick(c, sp);
      if (r == FP_BUSY) break;
      if (r >= 0) { codeLeave(); cdLang = strstr_P(fpPath, PSTR(".cpp")) ? 1 : 0; edLoadFile(fpPath); baseName(fpPath, cdName, sizeof cdName); }
      cdEdit();
      break;
    }
    case CDP_DONE:
      if (sp) cdEdit();
      break;
  }
}
void oledCode() {
  char l[4][24];
  snprintf_P(l[0], 24, PSTR("%.12s%s%s"), cdName[0] ? cdName : FS("untitled"), cdExt(), edDirty ? FS(" *") : FS(""));
  snprintf_P(l[1], 24, PSTR("%s  %u chars"), cdLang ? FS("C++") : FS("BASIC"), (unsigned)edLen);
  if (cdPage == CDP_DONE) snprintf_P(l[2], 24, PSTR("%s"), rtErrFlag ? FS("stopped / error") : FS("finished OK"));
  else snprintf_P(l[2], 24, PSTR("%s"), edMove ? FS("moving cursor") : FS("editing"));
  if (cdPage == CDP_DONE && rtErrFlag) snprintf_P(l[3], 24, PSTR("line %d"), rtErrLine);
  else snprintf_P(l[3], 24, PSTR("menu key: run/save"));
  oledCard(FS("CODE"), l[0], l[1], l[2], l[3]);
}

// ================= FILES (everything stored in flash) =================
const int FM_MAX = 40, FM_VIS = 9;
enum { FMP_LIST, FMP_MENU, FMP_CONFIRM, FMP_INFO };
int fmN = 0, fmSel = 0, fmTop = 0, fmPage = FMP_LIST;
uint16_t fmAccent() { return appAccent(APP_FILES); }
void fmScanDir(const char* dir, int depth) {
  Dir d = LittleFS.openDir(dir);
  while (d.next() && fmN < FM_MAX) {
    char p[48];
    snprintf_P(p, sizeof p, PSTR("%s%s%s"), dir, strcmp_P(dir, PSTR("/")) ? FS("/") : FS(""), d.fileName().c_str());
    if (d.isDirectory()) { if (depth < 2) fmScanDir(p, depth + 1); continue; }
    snprintf_P(WK.fm.name[fmN], 48, PSTR("%s"), p);
    WK.fm.size[fmN++] = d.fileSize();
  }
}
void fmScan() { fmN = 0; if (fsOk) fmScanDir("/", 0); if (fmSel >= fmN) fmSel = max(0, fmN - 1); }
char* lowerName(const char* p) {                     // a lower-case copy (file types in any case)
  char* o = ringBuf(); int i = 0;
  for (; p[i] && i < 63; i++) o[i] = tolower((uint8_t)p[i]);
  o[i] = 0;
  return o;
}
const char* fmKind(const char* p0) {
  const char* p = lowerName(p0);
  if (isDocx(p)) return FS("Word 365");
  if (isXlsx(p)) return FS("Excel 365");
  if (strstr_P(p, PSTR(".doc"))) return "Word";
  if (strstr_P(p, PSTR(".csv"))) return "Sheet";
  if (strstr_P(p, PSTR(".val"))) return "chart data";
  if (strstr_P(p, PSTR(".sq"))) return "Music";
  if (strstr_P(p, PSTR(".bas"))) return "BASIC";
  if (strstr_P(p, PSTR(".cpp"))) return "C++";
  if (!strncmp_P(p, PSTR("/n"), 2) && strstr_P(p, PSTR(".txt"))) return "Note";
  if (strstr_P(p, PSTR("prefs"))) return "Settings";
  return "file";
}
void fmDrawRow(int s) {
  int i = fmTop + s, y = 38 + s * 19;
  ptrListSet(PL_FILES, 4, 38, 312, 19, FM_VIS, fmTop, fmSel);
  uint16_t bg = (s & 1) ? E_ROW2 : E_WHITE, acc = fmAccent();
  tft.fillRect(4, y, 312, 19, bg);
  if (i >= fmN) return;
  bool sel = i == fmSel;
  char b[16];
  putText(10, y + 6, 1, C_INK, bg, WK.fm.name[i]);
  putText(206, y + 6, 1, sel ? acc : C_MID, bg, fmKind(WK.fm.name[i]));
  snprintf_P(b, sizeof b, PSTR("%u B"), (unsigned)WK.fm.size[i]);
  putText(310 - (int)strlen(b) * 6, y + 6, 1, C_MID, bg, b);
  if (sel) { tft.drawRect(4, y, 312, 19, acc); tft.drawRect(5, y + 1, 310, 17, acc); }
}
void fmDraw() {
  tft.fillScreen(C_LCD);
  drawTitleBar(FS("Files"), fmAccent());
  tft.fillRect(4, 22, 312, 14, E_HEAD);
  putText(10, 25, 1, C_INK, E_HEAD, FS("Name"));
  putText(206, 25, 1, C_INK, E_HEAD, FS("Type"));
  putText(286, 25, 1, C_INK, E_HEAD, FS("Size"));
  tft.drawRect(3, 21, 314, 16 + FM_VIS * 19 + 1, E_RULE);
  for (int s = 0; s < FM_VIS; s++) fmDrawRow(s);
  FSInfo fi;
  char b[56];
  if (fsOk && LittleFS.info(fi)) {
    int pct = (int)(fi.usedBytes * 100 / max((size_t)1, fi.totalBytes));
    snprintf_P(b, sizeof b, PSTR("%d files   %u KB used of %u KB"), fmN, (unsigned)(fi.usedBytes / 1024), (unsigned)(fi.totalBytes / 1024));
    putText(6, 214, 1, C_MID, C_LCD, b);
    drawGauge(6, 226, 308, pct, loadColor(pct));
  } else putText(6, 214, 1, E_CORAL, C_LCD, FS("flash filesystem is not mounted"));
}
void filesEnter() {
  wkOwner = APP_FILES;
  fmPage = FMP_LIST;
  fmScan();
  fmDraw();
}
void fmOpen(const char* p0) {
  int target = 0;
  const char* p = lowerName(p0);
  if (strstr_P(p, PSTR(".doc")) || isDocx(p)) target = APP_WORD;          // (.doc and .docx)
  else if (strstr_P(p, PSTR(".csv")) || isXlsx(p)) target = APP_SHEET;
  else if (strstr_P(p, PSTR(".val"))) { snprintf_P(openPath, sizeof openPath, PSTR("%.*s.csv"), (int)(strlen(p) - 4), p); enterApp(APP_SHEET); return; }
  else if (strstr_P(p, PSTR(".bas")) || strstr_P(p, PSTR(".cpp"))) target = APP_CODE;
  else if (strstr_P(p, PSTR(".sq"))) target = APP_MUSIC;
  else if (!strncmp_P(p, PSTR("/n"), 2) && strstr_P(p, PSTR(".txt"))) { enterApp(APP_NOTES); return; }
  else if (strstr_P(p, PSTR("prefs"))) { enterApp(APP_SETTINGS); return; }
  if (!target) { pmShowP(FS("No app opens this file"), PSTR("OK"), fmAccent()); fmPage = FMP_INFO; return; }
  snprintf_P(openPath, sizeof openPath, PSTR("%s"), p0);
  enterApp(target);
}
void filesTick(uint32_t now) {
  int c = takeClicks() * ENC_DIR;
  takePress();
  bool sp = takeShort();
  (void)now;
  switch (fmPage) {
    case FMP_LIST:
      titleTimeTick();
      if (c && fmN) {
        int old = fmSel, oldTop = fmTop;
        fmSel = ((fmSel + c) % fmN + fmN) % fmN;
        if (fmSel < fmTop) fmTop = fmSel;
        else if (fmSel >= fmTop + FM_VIS) fmTop = fmSel - FM_VIS + 1;
        if (fmTop != oldTop) for (int s = 0; s < FM_VIS; s++) fmDrawRow(s);
        else { fmDrawRow(old - fmTop); fmDrawRow(fmSel - fmTop); }
        uiClick(); oledDirty = true;
      }
      if (sp && fmN) { fmPage = FMP_MENU; pmShowP(WK.fm.name[fmSel], PSTR("Open\nDelete\nInfo\nCancel"), fmAccent()); }
      break;
    case FMP_MENU: {
      int r = pmTick(c, sp);
      if (r < 0) break;
      if (r == 0) { char p[48]; snprintf_P(p, sizeof p, PSTR("%s"), WK.fm.name[fmSel]); fmOpen(p); return; }
      if (r == 1) { char t[40]; snprintf_P(t, sizeof t, PSTR("Delete %.28s?"), WK.fm.name[fmSel]); fmDraw(); fmPage = FMP_CONFIRM; pmShowP(t, PSTR("No, keep it\nYes, delete"), E_CORAL); return; }
      if (r == 2) {
        char t[40]; snprintf_P(t, sizeof t, PSTR("%s, %u bytes"), fmKind(WK.fm.name[fmSel]), (unsigned)WK.fm.size[fmSel]);
        fmDraw(); fmPage = FMP_INFO; pmShowP(t, PSTR("OK"), fmAccent()); return;
      }
      fmPage = FMP_LIST; fmDraw();
      break;
    }
    case FMP_CONFIRM: {
      int r = pmTick(c, sp);
      if (r < 0) break;
      if (r == 1) {
        const char* p = WK.fm.name[fmSel];
        LittleFS.remove(p);
        if (strstr_P(p, PSTR(".csv"))) { char v[32]; snprintf_P(v, sizeof v, PSTR("%.*s.val"), (int)(strlen(p) - 4), p); LittleFS.remove(v); }
        if (strstr_P(p, PSTR("prefs"))) prefsDefaults();
        notesScan();
        SFX(T_SPLAT);
      }
      fmPage = FMP_LIST; fmScan(); fmDraw();
      break;
    }
    case FMP_INFO:
      if (pmTick(c, sp) >= 0) { fmPage = FMP_LIST; fmDraw(); }
      break;
  }
}
void oledFiles() {
  char l[4][24];
  FSInfo fi;
  snprintf_P(l[0], 24, PSTR("%d files"), fmN);
  snprintf_P(l[1], 24, PSTR("%.21s"), fmN ? WK.fm.name[fmSel] : FS("(none)"));
  snprintf_P(l[2], 24, PSTR("%s"), fmN ? fmKind(WK.fm.name[fmSel]) : FS(""));
  if (fsOk && LittleFS.info(fi)) snprintf_P(l[3], 24, PSTR("%uK free"), (unsigned)((fi.totalBytes - fi.usedBytes) / 1024));
  else snprintf_P(l[3], 24, PSTR("no filesystem"));
  oledCard(FS("FILES"), l[0], l[1], l[2], l[3]);
}

// ================= TOOLS (a swiss-army knife of little utilities) =================
// The list opens a tool; inside a tool a long press goes back to the list (not to the home screen).
enum { TL_DICE, TL_RANDOM, TL_TALLY, TL_CONVERT, TL_METRO, TL_TONE, TL_LIGHT, TL_8PAW, TL_PASSWORD, TL_REACT, TL_SCREEN, TL_TIP, TL_N };
const char TL_NAMES_P[] PROGMEM = "Dice + coin\nRandom number\nTally counter\nUnit converter\nMetronome\nTone generator\nFlashlight\nMagic 8-paw\nPassword maker\nReaction test\nScreen test\nTip + split";
const char TL_DESC_P[] PROGMEM = "d6, 2d6, d20 or a coin\npick 1..N\nclick to count\nlength, weight, temp...\nkeep the beat\nbuzzer test + note names\nwhite, warm or red light\nask a yes/no question\nstrong random passwords\nhow fast are your paws?\ndead pixel check\nsplit the bill";
const char TL_HINT_P[] PROGMEM = "turn: dice type   press: roll\nturn: max   press: pick\nturn: +/-   press: +1\nturn: value   press: next unit\nturn: BPM   press: start/stop\nturn: Hz   press: on/off\nturn: colour   press: on/off\nask out loud, then press\nturn: length   press: new\npress to start, then wait...\nturn or press: next pattern\nturn: change   press: next field";
struct Conv { const char* a; const char* b; float k, off; };
const Conv CONVS[14] = {{"cm", "in", 0.393701f, 0}, {"in", "cm", 2.54f, 0}, {"m", "ft", 3.28084f, 0}, {"ft", "m", 0.3048f, 0},
                        {"km", "mi", 0.621371f, 0}, {"mi", "km", 1.609344f, 0}, {"kg", "lb", 2.204623f, 0}, {"lb", "kg", 0.453592f, 0},
                        {"C", "F", 1.8f, 32}, {"F", "C", 0.555556f, -17.77778f}, {"L", "gal", 0.264172f, 0}, {"gal", "L", 3.785412f, 0},
                        {"km/h", "mph", 0.621371f, 0}, {"mph", "km/h", 1.609344f, 0}};
const char PAW8_P[] PROGMEM = "Yes! x3\nNope ;-;\nAsk after my nap\nPaws up, absolutely!\nThe stars say maybe\nSigns point to treats\nHmm... not right now\nDefinitely! UwU\nMy whiskers say no\nOutlook: fluffy\nPurrhaps...\nTry asking nicely";
const uint16_t T_MET_HI[] PROGMEM = {2000, 18};
const uint16_t T_MET_LO[] PROGMEM = {1300, 12};
int tlSel = 0, tlTop = 0, tlTool = -1;
int tlDice = 0, tlD[2] = {6, 6}, tlRndMax = 100, tlRnd = 0, tlConv = 0, tlBpm = 100, tlBeat = 0, tlFreq = 440;
int tlLight = 0, tlAns = -1, tlPwLen = 12, tlReact = 0, tlBest = 0, tlResult = 0, tlPattern = 0;
int tlBill = 4000, tlTipPct = 15, tlPeople = 2, tlField = 0;     // bill in cents
float tlVal = 1;
bool tlMetOn = false, tlToneOn = false, tlLightOn = false;
uint32_t tlNext = 0, tlRollEnd = 0, tlGo = 0;
char tlPw[26] = "";

uint16_t tlAccent() { return appAccent(APP_TOOLS); }
// ---------- list ----------
void tlDrawRow(int s) {
  int i = tlTop + s, y = 22 + s * 22;
  ptrListSet(PL_TOOLS, 4, 22, 304, 22, 8, tlTop, tlSel);
  uint16_t bg = (s & 1) ? E_ROW2 : E_WHITE, acc = tlAccent();
  tft.fillRect(4, y, 304, 22, bg);
  if (i >= TL_N) return;
  bool sel = i == tlSel;
  char b[4]; snprintf_P(b, sizeof b, PSTR("%d"), (i + 1) & 15);
  tft.fillRoundRect(8, y + 3, 16, 16, 3, acc);
  putText(16 - (int)strlen(b) * 3, y + 7, 1, E_ONACC, acc, b);
  putText(32, y + 7, 1, C_INK, bg, pgmItem(TL_NAMES_P, i));
  putText(130, y + 7, 1, sel ? acc : C_MID, bg, pgmItem(TL_DESC_P, i));
  if (sel) { tft.drawRect(4, y, 304, 22, acc); tft.drawRect(5, y + 1, 302, 20, acc); }
}
void tlDrawScroll() {
  int h = 8 * 22;
  tft.fillRect(308, 22, 8, h, E_ROW2);
  int th = h * 8 / TL_N, ty = 22 + (h - th) * tlTop / (TL_N - 8);
  tft.fillRect(310, ty + 1, 4, th - 2, tlAccent());
}
void tlDrawList() {
  tft.fillScreen(C_LCD);
  drawTitleBar(FS("Tools"), tlAccent());
  tft.drawRect(3, 21, 314, 8 * 22 + 2, E_RULE);
  for (int s = 0; s < 8; s++) tlDrawRow(s);
  tlDrawScroll();
  putText(6, 200, 1, C_MID, C_LCD, FS("turn: pick a tool   press: open   hold: back"));
  drawDock(APP_TOOLS);
}

// ---------- tool screens ----------
void tlBig(const char* s, int y, int size, uint16_t col) {   // centred text on a cleared band
  tft.fillRect(0, y, W, size * 8 + 2, C_LCD);
  centerText(s, y, size, col, C_LCD);
}
void tlDie(int x, int y, int v, uint16_t acc) {             // a 60px die with pips
  tft.fillRoundRect(x, y, 60, 60, 10, E_WHITE);
  tft.drawRoundRect(x, y, 60, 60, 10, C_INK);
  const int8_t P6[7][2] = {{30, 30}, {14, 14}, {46, 46}, {46, 14}, {14, 46}, {14, 30}, {46, 30}};
  const uint8_t MASK[7] = {0, 0x01, 0x06, 0x07, 0x1E, 0x1F, 0x7E};   // which pips each face uses
  for (int k = 0; k < 7; k++) if (MASK[v % 7] & (1 << k)) tft.fillCircle(x + P6[k][0], y + P6[k][1], 6, acc);
}
void tlDrawDice() {
  uint16_t acc = tlAccent();
  tlBig(pgmItem(PSTR("one die (d6)\ntwo dice (2d6)\ntwenty-sided (d20)\ncoin flip"), tlDice), 30, 1, C_MID);
  tft.fillRect(0, 44, W, 120, C_LCD);
  char b[16];
  if (tlDice == 0) tlDie(130, 60, tlD[0], acc);
  else if (tlDice == 1) { tlDie(90, 60, tlD[0], acc); tlDie(170, 60, tlD[1], acc); snprintf_P(b, sizeof b, PSTR("total %d"), tlD[0] + tlD[1]); tlBig(b, 140, 2, C_INK); }
  else if (tlDice == 2) {
    tft.fillTriangle(160, 50, 110, 140, 210, 140, acc);
    snprintf_P(b, sizeof b, PSTR("%d"), tlD[0]);
    putText(160 - (int)strlen(b) * 12, 104, 4, E_ONACC, acc, b);
  } else {
    bool heads = tlD[0] & 1;
    tft.fillCircle(160, 100, 46, heads ? K_YELLOW : C_MID);
    tft.drawCircle(160, 100, 46, C_INK);
    if (heads) drawPaw(160, 92, 5, C_INK);
    putText(145, 150, 1, C_INK, heads ? K_YELLOW : C_MID, heads ? FS("HEADS") : FS("TAILS"));
  }
}
void tlDrawConvert() {
  const Conv& v = CONVS[tlConv];
  char a[24], b[24], line[40];
  fmtNum(tlVal, a, sizeof a, 7);
  fmtNum(tlVal * v.k + v.off, b, sizeof b, 6);
  snprintf_P(line, sizeof line, PSTR("%s %s"), a, v.a);   tlBig(line, 50, 3, C_INK);
  tlBig(FS("="), 86, 2, C_MID);
  snprintf_P(line, sizeof line, PSTR("%s %s"), b, v.b);   tlBig(line, 110, 3, tlAccent());
  snprintf_P(line, sizeof line, PSTR("conversion %d of 14: %s -> %s"), tlConv + 1, v.a, v.b);
  tlBig(line, 150, 1, C_MID);
}
void tlDrawTone() {
  static const char* NN[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  char b[32];
  snprintf_P(b, sizeof b, PSTR("%d Hz"), tlFreq);
  tlBig(b, 56, 4, tlToneOn ? tlAccent() : C_INK);
  int n = (int)lround(12 * log2(tlFreq / 440.0)) + 57;          // 57 = A4
  snprintf_P(b, sizeof b, PSTR("nearest note: %s%d"), NN[((n % 12) + 12) % 12], n / 12);
  tlBig(b, 104, 2, C_MID);
  tlBig(tlToneOn ? FS("playing") : FS("silent"), 132, 1, tlToneOn ? tlAccent() : C_MID);
}
void tlDrawTip() {
  int tip = tlBill * tlTipPct / 100, total = tlBill + tip, each = (total + tlPeople - 1) / tlPeople;
  char b[40];
  const char* lab[3] = {"Bill", "Tip", "People"};
  for (int i = 0; i < 3; i++) {
    int y = 40 + i * 26;
    bool sel = i == tlField;
    if (i == 0) snprintf_P(b, sizeof b, PSTR("%d.%02d"), tlBill / 100, tlBill % 100);
    else if (i == 1) snprintf_P(b, sizeof b, PSTR("%d%%"), tlTipPct);
    else snprintf_P(b, sizeof b, PSTR("%d"), tlPeople);
    tft.fillRect(30, y, 260, 22, sel ? tlAccent() : E_WHITE);
    tft.drawRect(30, y, 260, 22, sel ? tlAccent() : E_RULE);
    putText(40, y + 4, 2, sel ? E_ONACC : C_INK, sel ? tlAccent() : E_WHITE, lab[i]);
    putText(280 - (int)strlen(b) * 12, y + 4, 2, sel ? E_ONACC : C_INK, sel ? tlAccent() : E_WHITE, b);
  }
  snprintf_P(b, sizeof b, PSTR("tip %d.%02d   total %d.%02d"), tip / 100, tip % 100, total / 100, total % 100);
  tlBig(b, 124, 1, C_MID);
  snprintf_P(b, sizeof b, PSTR("each pays %d.%02d"), each / 100, each % 100);
  tlBig(b, 140, 2, tlAccent());
}
void tlScreenPattern() {
  const uint16_t solid[5] = {RGB565(255, 0, 0), RGB565(0, 255, 0), RGB565(0, 0, 255), RGB565(255, 255, 255), 0};
  if (tlPattern < 5) { tft.fillScreen(solid[tlPattern]); return; }
  if (tlPattern == 5) { for (int i = 0; i < 16; i++) { int v = i * 17; tft.fillRect(i * 20, 0, 20, H, RGB565(v, v, v)); } return; }
  if (tlPattern == 6) {
    const uint16_t bars[8] = {RGB565(255, 255, 255), RGB565(255, 255, 0), RGB565(0, 255, 255), RGB565(0, 255, 0),
                              RGB565(255, 0, 255), RGB565(255, 0, 0), RGB565(0, 0, 255), 0};
    for (int i = 0; i < 8; i++) tft.fillRect(i * 40, 0, 40, H, bars[i]);
    return;
  }
  tft.fillScreen(0);                                            // 7: grid
  for (int x = 0; x < W; x += 20) tft.drawFastVLine(x, 0, H, RGB565(255, 255, 255));
  for (int y = 0; y < H; y += 20) tft.drawFastHLine(0, y, W, RGB565(255, 255, 255));
}
void tlLightFill() {
  const uint16_t col[3] = {RGB565(255, 255, 255), RGB565(255, 196, 120), RGB565(255, 0, 0)};
  tft.fillScreen(col[tlLight]);
}
void tlDrawTool() {                                             // full redraw of the open tool
  char b[48];
  if (tlTool == TL_SCREEN) { tlScreenPattern(); return; }
  if (tlTool == TL_LIGHT && tlLightOn) { tlLightFill(); return; }
  tft.fillScreen(C_LCD);
  snprintf_P(b, sizeof b, PSTR("%s"), pgmItem(TL_NAMES_P, tlTool));
  drawTitleBar(b, tlAccent());
  putText(6, 200, 1, C_MID, C_LCD, pgmItem(TL_HINT_P, tlTool));
  putText(W - 6 - 10 * 6, 200, 1, C_MID, C_LCD, FS("hold: back"));
  drawDock(APP_TOOLS);
  switch (tlTool) {
    case TL_DICE: tlDrawDice(); break;
    case TL_RANDOM:
      snprintf_P(b, sizeof b, PSTR("random 1 .. %d"), tlRndMax); tlBig(b, 40, 2, C_MID);
      if (tlRnd) { snprintf_P(b, sizeof b, PSTR("%d"), tlRnd); tlBig(b, 80, 6, tlAccent()); }
      break;
    case TL_TALLY:
      snprintf_P(b, sizeof b, PSTR("%d"), P.tally); tlBig(b, 64, 7, tlAccent());
      tlBig(FS("(saved when you leave the tool)"), 150, 1, C_MID);
      break;
    case TL_CONVERT: tlDrawConvert(); break;
    case TL_METRO:
      snprintf_P(b, sizeof b, PSTR("%d BPM"), tlBpm); tlBig(b, 50, 4, tlMetOn ? tlAccent() : C_INK);
      for (int i = 0; i < 4; i++) tft.drawCircle(100 + i * 40, 130, 12, C_MID);
      tlBig(tlMetOn ? FS("ticking") : FS("stopped"), 160, 1, C_MID);
      break;
    case TL_TONE: tlDrawTone(); break;
    case TL_LIGHT: {
      const char* n[3] = {"white light", "warm light", "red night light"};
      tlBig(n[tlLight], 70, 2, C_INK); tlBig(FS("press to switch it on"), 110, 1, C_MID); tlBig(FS("(press again to switch off)"), 124, 1, C_MID);
      break;
    }
    case TL_8PAW:
      tft.fillCircle(160, 100, 56, C_INK); tft.fillCircle(160, 100, 26, E_WHITE);
      if (tlAns < 0) putText(154, 92, 2, C_INK, E_WHITE, FS("8"));
      else { drawPaw(160, 96, 3, tlAccent()); tlBig(pgmItem(PAW8_P, tlAns), 164, 2, tlAccent()); }
      break;
    case TL_PASSWORD: {
      snprintf_P(b, sizeof b, PSTR("length %d"), tlPwLen); tlBig(b, 40, 2, C_MID);
      if (tlPw[0]) tlBig(tlPw, 80, 2, C_INK);
      int pct = constrain(tlPwLen * 100 / 20, 10, 100);
      drawGauge(60, 116, 200, pct, pct < 50 ? E_CORAL : (pct < 80 ? E_AMBER : E_MINT));
      tlBig(pct < 50 ? FS("weak") : (pct < 80 ? FS("okay") : FS("strong")), 130, 1, C_MID);
      break;
    }
    case TL_REACT:
      if (tlReact == 0) tlBig(FS("press to start"), 80, 2, C_INK);
      else if (tlReact == 3) { snprintf(b, sizeof b, tlResult < 0 ? "too soon! ;-;" : "%d ms", tlResult); tlBig(b, 70, 4, tlResult < 0 ? E_CORAL : tlAccent()); }
      if (tlBest) { snprintf_P(b, sizeof b, PSTR("best %d ms"), tlBest); tlBig(b, 130, 1, C_MID); }
      break;
    case TL_TIP: tlDrawTip(); break;
  }
}
void tlRandomPw() {
  const char* set = "abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789!@#$%&*?-_";
  int n = strlen(set);
  for (int i = 0; i < tlPwLen; i++) tlPw[i] = set[ESP.random() % n];   // hardware random number generator
  tlPw[tlPwLen] = 0;
}
void toolsClose() {                                  // back to the list
  if (tlToneOn || tlMetOn) soundOff();
  tlToneOn = tlMetOn = tlLightOn = false;
  if (tlTool == TL_TALLY) prefsSave();
  tlTool = -1;
  tlDrawList();
  oledDirty = true;
}
void toolsEnter() { tlTool = -1; tlDrawList(); }
void toolsTick(uint32_t now) {
  int c = takeClicks() * ENC_DIR;
  bool pr = takePress(), sp = takeShort();
  if (tlTool < 0) {                                  // the list
    titleTimeTick();
    if (c) {
      int old = tlSel, oldTop = tlTop;
      tlSel = ((tlSel + c) % TL_N + TL_N) % TL_N;
      if (tlSel < tlTop) tlTop = tlSel; else if (tlSel >= tlTop + 8) tlTop = tlSel - 7;
      if (tlTop != oldTop) { for (int s = 0; s < 8; s++) tlDrawRow(s); tlDrawScroll(); }
      else { tlDrawRow(old - tlTop); tlDrawRow(tlSel - tlTop); }
      uiClick(); oledDirty = true;
    }
    if (sp) { tlTool = tlSel; tlAns = -1; tlReact = 0; tlDrawTool(); SFX(T_HOP); oledDirty = true; }
    return;
  }
  if (c || sp || pr) oledDirty = true;
  char b[32];
  switch (tlTool) {
    case TL_DICE:
      if (c) { tlDice = ((tlDice + c) % 4 + 4) % 4; tlDrawDice(); uiClick(); }
      if (sp && !tlRollEnd) tlRollEnd = now + 700;
      if (tlRollEnd && now >= tlNext) {             // tumbling animation
        int sides = tlDice == 2 ? 20 : (tlDice == 3 ? 2 : 6);
        tlD[0] = 1 + ESP.random() % sides; tlD[1] = 1 + ESP.random() % 6;
        tlDrawDice(); tlNext = now + 70;
        if (now >= tlRollEnd) { tlRollEnd = 0; SFX(T_HOME); } else SFX(T_TICK);
      }
      break;
    case TL_RANDOM:
      if (c) { tlRndMax = constrain(tlRndMax + (abs(c) > 2 ? c * 10 : c), 2, 9999); snprintf_P(b, sizeof b, PSTR("random 1 .. %d"), tlRndMax); tlBig(b, 40, 2, C_MID); uiClick(); }
      if (sp) { tlRnd = 1 + ESP.random() % tlRndMax; snprintf_P(b, sizeof b, PSTR("%d"), tlRnd); tlBig(b, 80, 6, tlAccent()); SFX(T_HOP); }
      break;
    case TL_TALLY:
      if (c || sp) { P.tally = constrain(P.tally + c + (sp ? 1 : 0), -9999, 30000); snprintf_P(b, sizeof b, PSTR("%d"), P.tally); tlBig(b, 64, 7, tlAccent()); uiClick(); }
      break;
    case TL_CONVERT:
      if (c) { tlVal += abs(c) > 2 ? c * 10 : c; tlDrawConvert(); uiClick(); }
      if (sp) { tlConv = (tlConv + 1) % 14; tlDrawConvert(); SFX(T_HOP); }
      break;
    case TL_METRO:
      if (c) { tlBpm = constrain(tlBpm + c, 30, 240); snprintf_P(b, sizeof b, PSTR("%d BPM"), tlBpm); tlBig(b, 50, 4, tlMetOn ? tlAccent() : C_INK); }
      if (sp) { tlMetOn = !tlMetOn; tlBeat = 0; tlNext = now; tlDrawTool(); }
      if (tlMetOn && (int32_t)(now - tlNext) >= 0) {
        tlNext += 60000UL / tlBpm;
        for (int i = 0; i < 4; i++) tft.fillCircle(100 + i * 40, 130, 11, i == tlBeat % 4 ? tlAccent() : C_LCD);
        if (tlBeat % 4 == 0) SFX(T_MET_HI); else SFX(T_MET_LO);
        tlBeat++;
      }
      break;
    case TL_TONE:                                    // no knob clicks here: they would cut the tone
      if (c) { tlFreq = constrain(tlFreq + (abs(c) > 2 ? c * 50 : c * 5), 40, 6000); if (tlToneOn) soundOn(tlFreq); tlDrawTone(); }
      if (sp) { tlToneOn = !tlToneOn; if (tlToneOn) soundOn(tlFreq); else soundOff(); tlDrawTone(); }
      break;
    case TL_LIGHT:
      if (c) { tlLight = ((tlLight + c) % 3 + 3) % 3; tlDrawTool(); }
      if (sp) { tlLightOn = !tlLightOn; tlDrawTool(); }
      break;
    case TL_8PAW:
      if (sp) {
        for (int k = 0; k < 8; k++) {                // shake!
          int dx = (k & 1) ? 6 : -6;
          tft.fillRect(90, 40, 140, 120, C_LCD);
          tft.fillCircle(160 + dx, 100, 56, C_INK); tft.fillCircle(160 + dx, 100, 26, E_WHITE);
          SFX(T_TICK); delay(60); updateSound();
        }
        tlAns = ESP.random() % 12;
        tlDrawTool(); SFX(T_YOU);
      }
      break;
    case TL_PASSWORD:
      if (c) { tlPwLen = constrain(tlPwLen + c, 6, 24); tlRandomPw(); tlDrawTool(); }
      if (sp) { tlRandomPw(); tlDrawTool(); SFX(T_HOP); }
      break;
    case TL_REACT:
      if (tlReact == 0 && sp) {
        tlReact = 1; tlGo = now + 1500 + ESP.random() % 3000;
        tft.fillRect(0, 20, W, 176, E_AMBER); centerText(FS("wait for green..."), 100, 2, C_INK, E_AMBER);
      } else if (tlReact == 1) {
        if (pr) { tlReact = 3; tlResult = -1; tlNext = now; SFX(T_WALL); tlDrawTool(); }
        else if ((int32_t)(now - tlGo) >= 0) { tlReact = 2; tft.fillRect(0, 20, W, 176, E_MINT); centerText(FS("PRESS!"), 90, 4, C_INK, E_MINT); tlGo = millis(); }
      } else if (tlReact == 2 && pr) {
        tlResult = millis() - tlGo; tlReact = 3; tlNext = now;
        if (!tlBest || tlResult < tlBest) tlBest = tlResult;
        SFX(T_HOME); tlDrawTool();
      } else if (tlReact == 3 && sp && now - tlNext > 400) { tlReact = 0; tlDrawTool(); }   // ignore the release of the timed press
      break;
    case TL_SCREEN:
      if (c || sp) { tlPattern = ((tlPattern + (c ? c : 1)) % 8 + 8) % 8; tlScreenPattern(); }
      break;
    case TL_TIP:
      if (c) {
        if (tlField == 0) tlBill = constrain(tlBill + (abs(c) > 2 ? c * 1000 : c * 50), 0, 9999900);
        else if (tlField == 1) tlTipPct = constrain(tlTipPct + c, 0, 50);
        else tlPeople = constrain(tlPeople + c, 1, 50);
        tlDrawTip(); uiClick();
      }
      if (sp) { tlField = (tlField + 1) % 3; tlDrawTip(); SFX(T_HOP); }
      break;
  }
}
void oledTools() {
  char l[4][24];
  if (tlTool < 0) {
    snprintf_P(l[0], 24, PSTR("> %.19s"), pgmItem(TL_NAMES_P, tlSel)); snprintf_P(l[1], 24, PSTR("%.21s"), pgmItem(TL_DESC_P, tlSel));
    snprintf_P(l[2], 24, PSTR("%d tools"), TL_N); snprintf_P(l[3], 24, PSTR("press: open"));
  } else {
    snprintf_P(l[0], 24, PSTR("%.21s"), pgmItem(TL_NAMES_P, tlTool));
    snprintf_P(l[1], 24, PSTR("%.21s"), pgmItem(TL_HINT_P, tlTool));
    snprintf_P(l[2], 24, PSTR("%s"), tlTool == TL_TALLY ? FS("") : (tlTool == TL_REACT && tlBest ? FS("best so far:") : FS("")));
    if (tlTool == TL_TALLY) snprintf_P(l[2], 24, PSTR("count %d"), P.tally);
    if (tlTool == TL_REACT && tlBest) snprintf_P(l[2], 24, PSTR("best %d ms"), tlBest);
    snprintf_P(l[3], 24, PSTR("hold: back to list"));
  }
  oledCard(FS("TOOLS"), l[0], l[1], l[2], l[3]);
}

// ================= MUSIC (tracker / step sequencer for the piezo) =================
// 3 tracks x 16 steps per pattern (Lead, Bass, Drums), 8 patterns, a song order of up to 32 patterns.
// Lead/bass cells: . = keep going, == = note off, C-4 etc = note. Drums: K kick, S snare, H hat, T tom, C crash.
// Lead and bass share the piezo with a fast chiptune arpeggio; a drum hit takes over for a moment.
enum { SQP_EDIT, SQP_MENU, SQP_ASK, SQP_PICK };
enum { SQA_SAVE, SQA_ORDER };
char sqName[16] = "";
int sqPage = SQP_EDIT, sqFocus = 5, sqEdit = -1, sqPat = 0, sqAsk = SQA_SAVE;
int sqStep = 0, sqOrd = 0, sqPlayPat = 0, sqShownStep = -1, sqLastNote = 38;
bool sqPlay = false, sqSongMode = false, sqDirty = false, sqLoaded = false;
uint32_t sqNext = 0, sqGate[2] = {0, 0}, sqDrumT = 0;
uint16_t sqF[2] = {0, 0}, sqOut = 0;
uint8_t sqDrum = 0;
const char SQ_NN[] = "C-C#D-D#E-F-F#G-G#A-A#B-";
const char SQ_DRUMS[] = ".KSHTC";
const uint16_t SQ_TRCOL[3] = {RGB565(80, 150, 255), RGB565(255, 140, 60), RGB565(230, 80, 120)};

uint16_t sqAccent() { return appAccent(APP_MUSIC); }
uint16_t sqHz(uint8_t v) { return (uint16_t)(440.0 * pow(2.0, (v - 2 + 24 + SQ.transpose - 69) / 12.0)); }
uint32_t sqStepMs(int step) {                       // a 16th note, with optional swing on the off-beats
  uint32_t base = 15000UL / max(30, (int)SQ.bpm);
  return (step & 1) ? base * (100 - SQ.swing) / 100 : base * (100 + SQ.swing) / 100;
}
void sqDemo() {                                     // a little starter tune so there's something to hear
  memset(&SQ, 0, sizeof SQ);
  SQ.ver = 1; SQ.bpm = 120; SQ.gate = 2; SQ.len = 4;
  const uint8_t ord[4] = {0, 0, 1, 1};
  memcpy(SQ.order, ord, 4);
  const uint8_t DEMO_L0[16] = {38, 0, 42, 0, 45, 0, 42, 0, 47, 0, 45, 0, 42, 0, 40, 0};
  const uint8_t DEMO_L1[16] = {45, 0, 47, 0, 45, 0, 42, 0, 50, 0, 49, 0, 45, 0, 0, 1};
  const uint8_t DEMO_B0[16] = {14, 0, 0, 0, 14, 0, 0, 0, 11, 0, 0, 0, 9, 0, 0, 0};
  const uint8_t DEMO_B1[16] = {19, 0, 0, 0, 19, 0, 0, 0, 21, 0, 0, 0, 14, 0, 14, 0};
  const uint8_t DEMO_D0[16] = {1, 0, 3, 0, 2, 0, 3, 0, 1, 1, 3, 0, 2, 0, 3, 3};
  memcpy(SQ.cell[0][0], DEMO_L0, 16); memcpy(SQ.cell[0][1], DEMO_B0, 16); memcpy(SQ.cell[0][2], DEMO_D0, 16);
  memcpy(SQ.cell[1][0], DEMO_L1, 16); memcpy(SQ.cell[1][1], DEMO_B1, 16); memcpy(SQ.cell[1][2], DEMO_D0, 16);
  SQ.cell[1][2][15] = 5;
}
void sqNew() { memset(&SQ, 0, sizeof SQ); SQ.ver = 1; SQ.bpm = 120; SQ.len = 1; sqName[0] = 0; sqPat = 0; sqDirty = false; }

// ---------- sound ----------
void sqSet(uint16_t f) { if (sqOut == f) return; sqOut = f; if (f) soundOn(f); else soundOff(); }
uint16_t sqDrumHz(uint32_t now) {
  uint32_t t = now - sqDrumT;
  switch (sqDrum) {
    case 1: if (t < 70) return 170 - t * 3 / 2; break;                   // kick: quick pitch drop
    case 2: if (t < 60) return 900 + random(1800); break;                // snare: noisy burst
    case 3: if (t < 22) return 5000 + random(3000); break;               // hi-hat
    case 4: if (t < 110) return 300 - t; break;                          // tom
    case 5: if (t < 180) return ((t / 4) & 1) ? 0 : 3000 + random(4000); break;   // crash
  }
  sqDrum = 0;
  return 0;
}
void sqDoStep(uint32_t now) {
  int pat = sqSongMode ? SQ.order[sqOrd] % SQ_PATS : sqPat;
  sqPlayPat = pat;
  for (int t = 0; t < 2; t++) {
    uint8_t v = SQ.cell[pat][t][sqStep];
    if (v == 1) sqF[t] = 0;
    else if (v >= 2) { sqF[t] = sqHz(v); sqGate[t] = ((SQ.gate >> t) & 1) ? now + sqStepMs(sqStep) * 55 / 100 : 0; }
  }
  if (SQ.cell[pat][2][sqStep]) { sqDrum = SQ.cell[pat][2][sqStep]; sqDrumT = now; }
}
void sqService(uint32_t now) {
  if (sqPlay && (int32_t)(now - sqNext) >= 0) {
    sqDoStep(now);
    sqNext += sqStepMs(sqStep);
    if ((int32_t)(now - sqNext) > 200) sqNext = now + sqStepMs(sqStep);     // fell behind: catch up
    if (++sqStep >= SQ_STEPS) { sqStep = 0; if (sqSongMode) sqOrd = (sqOrd + 1) % max(1, (int)SQ.len); }
  }
  for (int t = 0; t < 2; t++) if (sqGate[t] && (int32_t)(now - sqGate[t]) >= 0) { sqF[t] = 0; sqGate[t] = 0; }
  uint16_t drum = sqDrum ? sqDrumHz(now) : 0;
  uint16_t f = drum;
  if (!f) f = (sqF[0] && sqF[1]) ? (((now / 18) & 1) ? sqF[1] : sqF[0]) : (sqF[0] ? sqF[0] : sqF[1]);   // arpeggio = 2 voices on 1 piezo
  sqSet(f);
}
void sqStop() {
  sqPlay = false; sqF[0] = sqF[1] = 0; sqGate[0] = sqGate[1] = 0; sqDrum = 0;
  soundOff(); sqOut = 0;
}
void sqPreview(int t, uint8_t v) {                  // hear a cell while you edit it
  if (sqPlay || !v) return;
  uint32_t now = millis();
  if (t == 2) { sqDrum = v; sqDrumT = now; }
  else if (v >= 2) { sqF[t] = sqHz(v); sqGate[t] = now + 160; }
}

// ---------- drawing ----------
void sqCellText(int t, uint8_t v, char* top, char* bot) {
  bot[0] = 0;
  if (t == 2) { top[0] = SQ_DRUMS[v % 6]; top[1] = 0; return; }
  if (v == 0) { strcpy_P(top, PSTR(".")); return; }
  if (v == 1) { strcpy_P(top, PSTR("==")); return; }
  int n = (v - 2) % 12;
  top[0] = SQ_NN[n * 2]; top[1] = SQ_NN[n * 2 + 1]; top[2] = 0;
  bot[0] = '1' + (v - 2) / 12; bot[1] = 0;
}
void sqDrawCell(int t, int s) {
  int x = s * 20, y = 52 + t * 34, f = 5 + t * SQ_STEPS + s;
  bool foc = f == sqFocus, edit = f == sqEdit;
  uint16_t acc = sqAccent(), bg = edit ? acc : (((s / 4) & 1) ? E_ROW2 : E_WHITE);
  uint8_t v = SQ.cell[sqPat][t][s];
  char top[4], bot[3];
  sqCellText(t, v, top, bot);
  tft.fillRect(x, y, 20, 34, bg);
  tft.fillRect(x, y, 20, 2, SQ_TRCOL[t]);
  tft.drawFastVLine(x + 19, y + 2, 32, E_HEAD);
  uint16_t fg = edit ? E_ONACC : (v ? C_INK : C_MID);
  putText(x + 10 - (int)strlen(top) * 3, y + 8, 1, fg, bg, top);
  if (bot[0]) putText(x + 7, y + 20, 1, fg, bg, bot);
  if (foc && !edit) { tft.drawRect(x, y, 20, 34, acc); tft.drawRect(x + 1, y + 1, 18, 32, acc); }
}
void sqButtonLabel(int i, char* b, size_t n) {
  switch (i) {
    case 0: snprintf_P(b, n, PSTR("%s"), sqPlay ? FS("Stop") : FS("Play")); break;
    case 1: snprintf_P(b, n, PSTR("Pat %d"), sqPat + 1); break;
    case 2: snprintf_P(b, n, PSTR("BPM %d"), SQ.bpm); break;
    case 3: snprintf_P(b, n, PSTR("%s"), sqSongMode ? FS("Song") : FS("Loop pat")); break;
    default: snprintf_P(b, n, PSTR("Menu"));
  }
}
void sqDrawButton(int i) {
  int x = i * 64;
  bool foc = sqFocus == i, edit = sqEdit == i;
  uint16_t acc = sqAccent(), bg = edit ? acc : (i == 0 && sqPlay ? E_MINT : E_WHITE);
  char b[12];
  sqButtonLabel(i, b, sizeof b);
  tft.fillRect(x + 1, 20, 62, 18, bg);
  tft.drawRect(x + 1, 20, 62, 18, foc ? acc : E_RULE);
  if (foc) tft.drawRect(x + 2, 21, 60, 16, acc);
  putText(x + 32 - (int)strlen(b) * 3, 25, 1, edit ? E_ONACC : C_INK, bg, b);
}
void sqDrawInfo() {
  char b[56], top[4], bot[3];
  tft.fillRect(0, 158, W, 52, C_LCD);
  if (sqFocus >= 5) {
    int t = (sqFocus - 5) / SQ_STEPS, s = (sqFocus - 5) % SQ_STEPS;
    sqCellText(t, SQ.cell[sqPat][t][s], top, bot);
    const char* tn[3] = {"Lead", "Bass", "Drums"};
    snprintf_P(b, sizeof b, PSTR("%s  step %d  %s%s"), tn[t], s + 1, top, bot);
    putText(6, 160, 1, SQ_TRCOL[t], C_LCD, b);
    putText(6, 172, 1, C_MID, C_LCD, sqEdit >= 0 ? FS("turn: change   press: done (next step)") : FS("turn: move   press: edit   hold: back"));
  } else putText(6, 166, 1, C_MID, C_LCD, sqEdit >= 0 ? FS("turn: change   press: done") : FS("turn: move   press: use button"));
  int n = 0;
  n += snprintf_P(b, sizeof b, PSTR("song:"));
  for (int i = 0; i < SQ.len && n < 50; i++) n += snprintf_P(b + n, sizeof b - n, PSTR(" %d"), SQ.order[i] % SQ_PATS + 1);
  putText(6, 186, 1, C_INK, C_LCD, b);
  snprintf_P(b, sizeof b, PSTR("swing %d%%  lead %s  bass %s"), SQ.swing, (SQ.gate & 1) ? FS("short") : FS("long"),
             (SQ.gate & 2) ? "short" : "long");
  putText(6, 198, 1, C_MID, C_LCD, b);
}
void sqDrawGrid() {
  tft.fillRect(0, 40, W, 12, E_HEAD);
  for (int s = 0; s < SQ_STEPS; s++) {
    char b[4]; snprintf_P(b, sizeof b, PSTR("%d"), (s + 1) % 100);
    putText(s * 20 + 10 - (int)strlen(b) * 3, 42, 1, s % 4 ? C_MID : C_INK, E_HEAD, b);
  }
  for (int t = 0; t < SQ_TR; t++) for (int s = 0; s < SQ_STEPS; s++) sqDrawCell(t, s);
  tft.fillRect(0, 154, W, 3, C_LCD);
  sqShownStep = -1;
}
void sqDrawAll() {
  char t[24];
  snprintf_P(t, sizeof t, PSTR("Music %.12s%s"), sqName[0] ? sqName : FS("untitled"), sqDirty ? FS("*") : FS(""));
  tft.fillScreen(C_LCD);
  drawTitleBar(t, sqAccent());
  for (int i = 0; i < 5; i++) sqDrawButton(i);
  sqDrawGrid();
  sqDrawInfo();
}
void sqDrawFocus(int f) { if (f < 5) sqDrawButton(f); else sqDrawCell((f - 5) / SQ_STEPS, (f - 5) % SQ_STEPS); }
void sqPlayhead() {                                 // little bar under the step that is playing
  int s = sqPlay ? (sqStep + SQ_STEPS - 1) % SQ_STEPS : -1;
  if (sqPlay && sqSongMode && sqPlayPat != sqPat) s = -1;
  if (s == sqShownStep) return;
  if (sqShownStep >= 0) tft.fillRect(sqShownStep * 20, 154, 20, 3, C_LCD);
  if (s >= 0) tft.fillRect(s * 20 + 2, 154, 16, 3, sqAccent());
  sqShownStep = s;
}

// ---------- files ----------
bool sqSave() {
  if (!fsOk || !sqName[0]) return false;
  LittleFS.mkdir(FS("/music"));
  char p[40]; snprintf_P(p, sizeof p, PSTR("/music/%s.sq"), sqName);
  File f = LittleFS.open(p, FS("w"));
  if (!f) return false;
  f.write((const uint8_t*)&SQ, sizeof SQ);
  f.close();
  sqDirty = false;
  return true;
}
bool sqLoad(const char* path) {
  File f = LittleFS.open(path, FS("r"));
  if (!f) return false;
  SeqSong t;
  bool ok = f.size() == sizeof t && f.read((uint8_t*)&t, sizeof t) == (int)sizeof t && t.ver == 1;
  f.close();
  if (!ok) return false;
  SQ = t; baseName(path, sqName, sizeof sqName); sqPat = 0; sqDirty = false;
  return true;
}

// ---------- the app ----------
void musicEnter() {
  if (openPath[0]) { sqLoad(openPath); openPath[0] = 0; }
  else if (wkOwner != APP_MUSIC) {                   // the song lives in the shared area: reload it (or the demo)
    char p[40]; snprintf_P(p, sizeof p, PSTR("/music/%s.sq"), sqName);
    if (!(sqName[0] && fsOk && LittleFS.exists(p) && sqLoad(p))) sqDemo();
  }
  wkOwner = APP_MUSIC;
  sqLoaded = true;
  sqPage = SQP_EDIT; sqEdit = -1;
  sqDrawAll();
}
void musicLeave() { sqStop(); if (sqDirty) { if (!sqName[0]) strcpy_P(sqName, PSTR("untitled")); sqSave(); } }
void sqMenuDo(int r) {
  switch (r) {
    case 0: {                                       // song order typed as digits, e.g. "1 1 2 3"
      char cur[40]; int n = 0;
      for (int i = 0; i < SQ.len && n < 36; i++) n += snprintf_P(cur + n, sizeof cur - n, PSTR("%d "), SQ.order[i] % SQ_PATS + 1);
      sqPage = SQP_ASK; sqAsk = SQA_ORDER;
      askBegin(FS("Song order: pattern numbers 1-8, e.g. 1 1 2 3"), cur, 32, sqAccent());
      return;
    }
    case 1: if (sqPat < SQ_PATS - 1) { memcpy(SQ.cell[sqPat + 1], SQ.cell[sqPat], sizeof SQ.cell[0]); sqPat++; sqDirty = true; } break;
    case 2: memset(SQ.cell[sqPat], 0, sizeof SQ.cell[0]); sqDirty = true; break;
    case 3: case 4:
      for (int t = 0; t < 2; t++) for (int s = 0; s < SQ_STEPS; s++) {
        uint8_t& v = SQ.cell[sqPat][t][s];
        if (v >= 2) v = constrain(v + (r == 3 ? 1 : -1), 2, 85);
      }
      sqDirty = true;
      break;
    case 5: SQ.gate = (SQ.gate + 1) & 3; sqDirty = true; break;
    case 6: SQ.swing = SQ.swing >= 50 ? 0 : SQ.swing + 25; sqDirty = true; break;
    case 7: if (sqName[0]) { sqSave(); SFX(T_HOME); break; }       // no name yet: fall through to "save as"
    [[fallthrough]];
    case 8: sqPage = SQP_ASK; sqAsk = SQA_SAVE; askBegin(FS("Save song as (name):"), sqName, 12, sqAccent()); return;
    case 9: sqPage = SQP_PICK; fpBegin(FS("Open song"), FS("/music"), FS(".sq"), nullptr, sqAccent()); return;
    case 10: sqNew(); break;
  }
  sqDrawAll();
}
void musicTick(uint32_t now) {
  int c = takeClicks() * ENC_DIR;
  takePress();
  bool sp = takeShort();
  sqService(now);
  switch (sqPage) {
    case SQP_MENU: { int r = pmTick(c, sp); if (r >= 0) { sqPage = SQP_EDIT; sqMenuDo(r); } return; }
    case SQP_ASK: {
      int r = askTick(c, sp);
      if (!r) return;
      if (r > 0 && sqAsk == SQA_SAVE) { cleanName(askBuf, sqName); sqSave(); SFX(T_HOME); }
      if (r > 0 && sqAsk == SQA_ORDER) {
        int n = 0;
        for (const char* p = askBuf; *p && n < SQ_ORDER; p++) if (*p >= '1' && *p <= '8') SQ.order[n++] = *p - '1';
        if (n) { SQ.len = n; sqOrd = 0; sqDirty = true; }
      }
      sqPage = SQP_EDIT; sqDrawAll();
      return;
    }
    case SQP_PICK: {
      int r = fpTick(c, sp);
      if (r == FP_BUSY) return;
      if (r >= 0) { musicLeave(); sqLoad(fpPath); }
      sqPage = SQP_EDIT; sqDrawAll();
      return;
    }
  }
  if (sqEdit >= 0) {                                 // editing a value with the knob
    if (c) {
      if (sqEdit == 1) { sqPat = ((sqPat + c) % SQ_PATS + SQ_PATS) % SQ_PATS; sqDrawGrid(); }
      else if (sqEdit == 2) SQ.bpm = constrain(SQ.bpm + c, 40, 240);
      else {
        int t = (sqEdit - 5) / SQ_STEPS, s = (sqEdit - 5) % SQ_STEPS;
        uint8_t& v = SQ.cell[sqPat][t][s];
        v = t == 2 ? ((v + c) % 6 + 6) % 6 : constrain(v + c, 0, 85);
        if (t < 2 && v >= 2) sqLastNote = v;
        sqPreview(t, v);
        sqDirty = true;
      }
      sqDrawFocus(sqEdit); sqDrawInfo(); oledDirty = true;
    }
    if (sp) {
      int f = sqEdit;
      sqEdit = -1;
      if (f >= 5 && (f - 5) % SQ_STEPS < SQ_STEPS - 1) sqFocus = f + 1;     // jump to the next step
      sqDrawFocus(f); sqDrawFocus(sqFocus); sqDrawInfo();
    }
  } else {
    if (c) {
      int old = sqFocus;
      sqFocus = ((sqFocus + c) % SQ_FOCUS + SQ_FOCUS) % SQ_FOCUS;
      sqDrawFocus(old); sqDrawFocus(sqFocus); sqDrawInfo();
      if (!sqPlay) uiClick();
      oledDirty = true;
    }
    if (sp) {
      switch (sqFocus) {
        case 0:
          if (sqPlay) sqStop();
          else { sqStep = 0; sqOrd = 0; sqNext = now; sqPlay = true; }
          sqDrawButton(0);
          break;
        case 1: case 2: sqEdit = sqFocus; sqDrawButton(sqFocus); break;
        case 3: sqSongMode = !sqSongMode; sqDrawButton(3); break;
        case 4: sqPage = SQP_MENU; pmShowP(FS("Music"), PSTR("Song order...\nCopy pattern to next\nClear pattern\nTranspose up\nTranspose down\nNotes: long / short\nSwing 0 / 25 / 50%\nSave\nSave as...\nOpen...\nNew song"), sqAccent()); return;
        default: {
          int t = (sqFocus - 5) / SQ_STEPS, s = (sqFocus - 5) % SQ_STEPS;
          uint8_t& v = SQ.cell[sqPat][t][s];
          if (t < 2 && v == 0) { v = sqLastNote; sqDirty = true; }        // empty cell: start from the last note used
          sqEdit = sqFocus;
          sqPreview(t, v);
          sqDrawFocus(sqFocus); sqDrawInfo();
        }
      }
      oledDirty = true;
    }
  }
  if (sqPlay && sqSongMode && sqPlayPat != sqPat && sqEdit < 0) { sqPat = sqPlayPat; sqDrawGrid(); sqDrawButton(1); }   // follow the song
  sqPlayhead();
}
void oledMusic() {
  char l[4][24], top[4], bot[3];
  snprintf_P(l[0], 24, PSTR("%.14s%s"), sqName[0] ? sqName : FS("untitled"), sqDirty ? FS(" *") : FS(""));
  if (sqPlay) snprintf_P(l[1], 24, PSTR("> pat %d  step %d"), sqPlayPat + 1, (sqStep + 15) % 16 + 1);
  else snprintf_P(l[1], 24, PSTR("stopped  pat %d"), sqPat + 1);
  snprintf_P(l[2], 24, PSTR("BPM %d  swing %d%%"), SQ.bpm, SQ.swing);
  if (sqFocus >= 5) {
    int t = (sqFocus - 5) / SQ_STEPS, s = (sqFocus - 5) % SQ_STEPS;
    sqCellText(t, SQ.cell[sqPat][t][s], top, bot);
    snprintf_P(l[3], 24, PSTR("%s %d: %s%s"), t == 0 ? FS("lead") : (t == 1 ? FS("bass") : FS("drum")), s + 1, top, bot);
  } else snprintf_P(l[3], 24, PSTR("%s mode"), sqSongMode ? FS("song") : FS("pattern"));
  oledCard(FS("MUSIC"), l[0], l[1], l[2], l[3]);
}

// ================= WEB (weather, news, money, ISS, where am I + via the PC relay: browser, screen view, AI) =================
// Everything streams: pages are read byte by byte and turned into text on the fly, so a 50 KB web page
// never has to fit in the ESP8266's RAM. Plain-HTTP services are used where possible (no TLS memory cost).
// AI chat talks to claude_relay.py on your PC (the API key stays on the PC; see the extras folder).
enum { WB_LIST, WB_WEATHER, WB_PAGE, WB_CHAT, WB_MONEY, WB_ISS, WB_WHERE, WB_SCREEN, WB_CAST };
enum { WA_NONE, WA_SEARCH, WA_URL, WA_CHAT, WA_CITY, WA_RELAY, WA_GO, WA_SGO, WA_STYPE, WA_ASK };
const int WB_ITEMS = 14, WB_LINKS = 12, WB_TEXT = 2600;
// the list: two sections. Row -> item id (200+ = a section header)
const uint8_t WB_ROWS[WB_ITEMS] = {200, 0, 1, 4, 5, 6, 7, 201, 2, 10, 11, 3, 9, 8};
const char WB_NAMES_P[] PROGMEM = "Weather\nNews (68k.news)\nWeb browser (text)\nAI chat (Claude)\nCurrency exchange\nISS tracker\nWhere am I?\nWeather city...\nRelay address...\nRelay check\nScreen view (Edge)\nScreen share (PC)";
const char WB_DESC_P[] PROGMEM = "Open-Meteo, 4 days\nheadlines for old computers\ntext, links + pictures\ncan search the web\nlive rates, 8 currencies\nwhere the space station is\nIP location + time zone\nblank = automatic\nyour PC's IP:port\nis the PC relay OK?\nreal pages, color, clicks\nyour PC's screen, live";
const char WB_HEADS_P[] PROGMEM = "WORKS ON ITS OWN (just WiFi)\nNEEDS THE PC RELAY (start_relay.bat)";
const char WB_CUR_P[] PROGMEM = "USD\nEUR\nMXN\nGBP\nJPY\nCAD\nBRL\nCNY";
int wbPage = WB_LIST, wbSel = 1, wbTop = 0, wbAsk = WA_NONE, wbLine = 0, wbLines = 0, wbChunk = 0;
bool wbMenu = false, wbLinkMenu = false;
char wbErr[48] = "";
char wbRedir[200];
float wbRate[8];
uint32_t wbAmount = 100, wbIssAt = 0;
int16_t wbIssX[16], wbIssY[16];
uint8_t wbIssN = 0, wbIssPeople = 0;
bool (*webSink)(char) = nullptr;                    // receives every body byte; return false to stop early
int16_t wbHdrFrames = 0, wbHdrDelay = 0;            // X-Frames / X-Delay of the last reply (animated GIFs from the relay)
long wbHdrHeight = 0, wbHdrWidth = 0;               // X-Height / X-Width: the screen-view page size, sent with every slice
char wbHdrHover[40] = "";                           // X-Hover: what the screen view's mouse pointer is over
int (*webSinkStream)(WiFiClient*, int) = nullptr;   // set: reads the reply itself (pictures: straight into a row)
bool webQuiet = false;                              // no spinner (the fast screen-view refreshes)
bool webSpun = false;
bool webAsCurl = false;                             // send a curl user agent instead of a browser one
uint16_t webWaitMs = 9000;                          // give up after this long without data (Claude + web search: longer)

uint16_t wbAccent() { return appAccent(APP_WEB); }
// ---------- text helpers ----------
uint8_t utf8ToCp437(uint16_t u) {                   // the font's Spanish letters + common punctuation
  switch (u) {
    case 0xE1: return 0xA0; case 0xE9: return 0x82; case 0xED: return 0xA1; case 0xF3: return 0xA2; case 0xFA: return 0xA3;
    case 0xF1: return 0xA4; case 0xD1: return 0xA5; case 0xFC: return 0x81; case 0xDC: return 0x9A; case 0xC9: return 0x90;
    case 0xBF: return 0xA8; case 0xA1: return 0xAD; case 0xB0: return 0xF8; case 0xE7: return 0x87; case 0xE0: return 0x85;
    case 0xE8: return 0x8A; case 0xF6: return 0x94; case 0xE4: return 0x84; case 0xA0: return ' ';
    case 0x2018: case 0x2019: return '\''; case 0x201C: case 0x201D: return '"';
    case 0x2013: case 0x2014: return '-'; case 0x2026: return '.'; case 0x2022: return '*';
  }
  return u < 0x80 ? (uint8_t)u : 0;                 // 0 = drop it (emoji, CJK, arrows...)
}
uint8_t u8Need = 0;
uint16_t u8Cp = 0;
int u8Feed(uint8_t b) {                             // UTF-8 byte in -> display char out (-1 = need more)
  if (u8Need) { u8Cp = (u8Cp << 6) | (b & 0x3F); if (--u8Need) return -1; return utf8ToCp437(u8Cp); }
  if (b < 0x80) return b;
  if ((b & 0xE0) == 0xC0) { u8Cp = b & 0x1F; u8Need = 1; return -1; }
  if ((b & 0xF0) == 0xE0) { u8Cp = b & 0x0F; u8Need = 2; return -1; }
  if ((b & 0xF8) == 0xF0) { u8Cp = 0; u8Need = 3; return -1; }
  return -1;
}
void urlEncode(const char* in, char* out, size_t n) {
  char tmp[200];
  cp437ToUtf8(in, tmp, sizeof tmp);
  size_t o = 0;
  for (const uint8_t* p = (const uint8_t*)tmp; *p && o + 4 < n; p++) {
    if (isalnum(*p) || *p == '-' || *p == '_' || *p == '.') out[o++] = *p;
    else if (*p == ' ') out[o++] = '+';
    else { snprintf_P(out + o, n - o, PSTR("%%%02X"), *p); o += 3; }
  }
  out[o] = 0;
}
void webResolve(const char* base, const char* href, char* out, size_t n) {
  if (!strncmp_P(href, PSTR("http://"), 7) || !strncmp_P(href, PSTR("https://"), 8)) { snprintf_P(out, n, PSTR("%s"), href); return; }
  if (href[0] == '/' && href[1] == '/') { snprintf_P(out, n, PSTR("http:%s"), href); return; }
  const char* host = strstr_P(base, PSTR("://"));
  host = host ? host + 3 : base;
  const char* slash = strchr(host, '/');
  int hostEnd = slash ? (int)(slash - base) : (int)strlen(base);
  if (href[0] == '/') { snprintf_P(out, n, PSTR("%.*s%s"), hostEnd, base, href); return; }
  const char* last = strrchr(host, '/');
  int dirEnd = last ? (int)(last - base) + 1 : hostEnd;
  if (!last) { snprintf_P(out, n, PSTR("%.*s/%s"), hostEnd, base, href + (href[0] == '.' && href[1] == '/' ? 2 : 0)); return; }
  snprintf_P(out, n, PSTR("%.*s%s"), dirEnd, base, href + (href[0] == '.' && href[1] == '/' ? 2 : 0));
}

// ---------- the fetcher ----------
void webSpin() {                                    // a little spinner in the title bar while loading
  static uint8_t k = 0;
  k = (k + 1) % 4;
  for (int i = 0; i < 4; i++) tft.fillCircle(172 + i * 5, 9, 1, i == k ? E_BARTXT : E_DIMBAR);
  webSpun = true;
}
void webSpinClear() { if (webSpun) { tft.fillRect(169, 6, 22, 7, E_BAR); webSpun = false; } }
void NOINLINE webRedirect(const char* url, const char* loc) {   // own frame: only on the stack while redirecting
  char tmp[200];
  webResolve(url, loc, tmp, sizeof tmp);
  memcpy(wbRedir, tmp, sizeof wbRedir);
}
int webFetch(const char* url, const char* post) {
  wbErr[0] = 0;
  wbHdrFrames = wbHdrDelay = 0; wbHdrHeight = wbHdrWidth = 0; wbHdrHover[0] = 0;
  if (WiFi.status() != WL_CONNECTED) { snprintf_P(wbErr, sizeof wbErr, PSTR("no WiFi: connect in the WiFi app first")); return -1; }
  u8Need = 0;
  for (int hop = 0; hop < 3; hop++) {
    bool https = !strncmp_P(url, PSTR("https:"), 6);
    if (https && ESP.getMaxFreeBlockSize() < 16000) { snprintf_P(wbErr, sizeof wbErr, PSTR("not enough RAM for HTTPS (%uK)"), (unsigned)(ESP.getMaxFreeBlockSize() / 1024)); return -1; }
    WiFiClient plain;
    BearSSL::WiFiClientSecure* sec = nullptr;
    HTTPClient http;
    http.useHTTP10(true);                           // HTTP/1.0 = no chunked encoding, the body is plain bytes
    http.setTimeout(webWaitMs);
    http.setUserAgent(webAsCurl ? F("curl/8.4.0") : F("Mozilla/4.0 (compatible; Psion EPOC Color; ESP8266)"));
    const char* keys[6] = {"Location", "X-Frames", "X-Delay", "X-Height", "X-Width", "X-Hover"};
    http.collectHeaders(keys, 6);
    bool ok;
    if (https) {
      sec = new BearSSL::WiFiClientSecure();
      sec->setInsecure();                           // no certificate store on the device: fine for public read-only data
      sec->setBufferSizes(4096, 512);               // small TLS buffers so it fits in RAM
      ok = http.begin(*sec, url);
    } else ok = http.begin(plain, url);
    if (!ok) { snprintf_P(wbErr, sizeof wbErr, PSTR("bad address")); delete sec; webSpinClear(); return -1; }
    if (post) http.addHeader(F("Content-Type"), F("application/json"));
    if (!webQuiet) webSpin();
    int code = post ? http.POST((uint8_t*)post, strlen(post)) : http.GET();
    if (code >= 300 && code < 400 && hop < 2) {     // follow redirects (also http -> https)
      String loc = http.header("Location");
      webRedirect(url, loc.c_str());
      url = wbRedir;
      http.end(); delete sec;
      continue;
    }
    if (code <= 0) {
      snprintf_P(wbErr, sizeof wbErr, PSTR("%.40s"), http.errorToString(code).c_str());
      http.end(); delete sec; webSpinClear();
      return code;
    }
    if (code == 200) {
      wbHdrFrames = http.header("X-Frames").toInt(); wbHdrDelay = http.header("X-Delay").toInt();
      wbHdrHeight = http.header("X-Height").toInt(); wbHdrWidth = http.header("X-Width").toInt();
      if (http.hasHeader("X-Hover")) {                 // screen view: what's under the mouse pointer (Latin-1)
        String hv = http.header("X-Hover");
        size_t o = 0;
        for (unsigned i = 0; i < hv.length() && o < sizeof wbHdrHover - 1; i++) {
          uint8_t ch = hv[i], c2 = ch < 0x80 ? ch : utf8ToCp437(ch);
          if (c2 >= 32) wbHdrHover[o++] = (char)c2;
        }
        wbHdrHover[o] = 0;
      }
    }
    // getStreamPtr() is null when the server already closed and nothing is left to read: an empty reply
    // (a screen-view slice with no links, a 204...). Reading through it was a crash (exception 28).
    WiFiClient* st = http.getStreamPtr();
    int left = http.getSize();
    uint32_t last = millis(), spin = millis();       // (the spinner only shows up for slow replies)
    bool stop = false;
    while (st && !stop && left != 0 && (http.connected() || st->available())) {
      int n = st->available();
      if (n) {
        int r;
        if (webSinkStream) {                         // pictures: no copy, no buffer on the stack
          r = webSinkStream(st, n);
          if (r < 0) { stop = true; r = 0; }         // the whole picture arrived
        } else {
          uint8_t buf[64];
          r = st->read(buf, min(n, 64));
          for (int i = 0; i < r && !stop; i++) stop = !webSink((char)buf[i]);
        }
        if (left > 0) left -= r;
        if (r > 0) last = millis();
      } else {
        if (millis() - last > webWaitMs) break;
        delay(1);
      }
      if (millis() - spin > 200) {
        spin = millis(); if (!webQuiet) webSpin();
        updateButton(); updateSound(); rcService();
        if (fLong) { fLong = false; snprintf_P(wbErr, sizeof wbErr, PSTR("stopped")); stop = true; }
      }
      yield();
    }
    http.end(); delete sec;
    webSpinClear();
    if (code >= 400 && !wbErr[0]) snprintf_P(wbErr, sizeof wbErr, PSTR("server said %d"), code);
    return code;
  }
  webSpinClear();
  snprintf_P(wbErr, sizeof wbErr, PSTR("too many redirects"));
  return -1;
}

// ---------- sinks ----------
uint16_t wbLen = 0;
bool sinkRaw(char c) { if (wbLen >= WB_TEXT - 1) return false; WK.web.text[wbLen++] = c; WK.web.text[wbLen] = 0; return true; }
bool sinkText(char c) {
  int ch = u8Feed((uint8_t)c);
  if (ch <= 0 || ch == '\r') return true;
  return sinkRaw((char)ch);
}
// HTML -> text: tags dropped, block tags become line breaks, links get a [n] marker
uint8_t hsMode = 0, hsTagLen = 0, hsEntLen = 0, hsHref = 0, hsQuote = 0, hsNl = 2;
bool hsClose = false, hsSkip = false, hsInA = false, hsSpace = true, hsCapture = false, hsFound = false;
char hsTag[10], hsEnt[8], hsRoll[6];
uint16_t hsUrlLen = 0, hsTextSeen = 0, hsSkipText = 0, hsLinkNo = 0, hsWant = 0xFFFF, hsLinkGlobal[WB_LINKS];
int hsLocal = -1;
uint8_t hsLtLen = 0;
char hsImg[20];                                     // src of the <img> being read (only the relay's /img?id=N are shown)
uint8_t hsImgLen = 0, hsImgQ = 0;
bool hsImgCap = false;
void htmlReset(uint16_t skip, uint16_t want) {
  hsMode = hsTagLen = hsEntLen = hsHref = hsQuote = 0; hsNl = 2;
  hsClose = hsSkip = hsInA = hsCapture = hsFound = false; hsSpace = true;
  hsTextSeen = 0; hsSkipText = skip; hsLinkNo = 0; hsWant = want; hsLocal = -1; hsUrlLen = 0;
  memset(hsRoll, 0, sizeof hsRoll);
  wbLen = 0; WK.web.text[0] = 0; WK.web.nlinks = 0;
}
bool htmlEmit(char c) {                             // one visible character
  if (c == '\n') { if (hsNl >= 2) return true; hsNl++; hsSpace = true; }
  else if (c == ' ') { if (hsSpace) return true; hsSpace = true; hsNl = 0; }
  else { hsSpace = false; hsNl = 0; }
  if (hsTextSeen++ < hsSkipText) return true;
  if (hsInA && hsLocal >= 0 && (uint8_t)c >= ' ' && hsLtLen < 25) { WK.web.ltxt[hsLocal][hsLtLen++] = c; WK.web.ltxt[hsLocal][hsLtLen] = 0; }
  if (hsWant != 0xFFFF) return true;                // only hunting for one link's address
  return sinkRaw(c);
}
bool htmlBlockTag(const char* t) {                   // tags that start a new line
  // (plain compares: strstr_P can only search RAM text, and searching a flash list crashed with exception 3)
  switch (t[0]) {
    case 'p': return !t[1];
    case 'b': return !strcmp_P(t, PSTR("br")) || !strcmp_P(t, PSTR("blockquote"));
    case 'd': return !strcmp_P(t, PSTR("div")) || !strcmp_P(t, PSTR("dt")) || !strcmp_P(t, PSTR("dd"));
    case 'l': return !strcmp_P(t, PSTR("li"));
    case 't': return !strcmp_P(t, PSTR("tr")) || !strcmp_P(t, PSTR("table"));
    case 'h': return (t[1] >= '1' && t[1] <= '6' && !t[2]) || !strcmp_P(t, PSTR("hr"));
    case 'u': case 'o': return t[1] == 'l' && !t[2];
    case 's': return !strcmp_P(t, PSTR("section"));
    case 'a': return !strcmp_P(t, PSTR("article"));
  }
  return false;
}
void htmlTagDone() {
  hsTag[hsTagLen] = 0;
  for (int i = 0; i < hsTagLen; i++) hsTag[i] = tolower((uint8_t)hsTag[i]);
  if (!strcmp_P(hsTag, PSTR("script")) || !strcmp_P(hsTag, PSTR("style")) || !strcmp_P(hsTag, PSTR("head"))) hsSkip = !hsClose;
  if (htmlBlockTag(hsTag)) {                         // block tags become line breaks
    htmlEmit('\n');
    if (hsTag[0] == 'h' && hsTag[1] && hsTag[1] != 'r') htmlEmit('\n');
  }
  if (!strcmp_P(hsTag, PSTR("li")) && !hsClose) { htmlEmit('*'); htmlEmit(' '); }
  if (!strcmp_P(hsTag, PSTR("img")) && !strncmp_P(hsImg, PSTR("/img?id="), 8)) {   // picture: a line of \x01 + id
    htmlEmit('\n'); htmlEmit('\x01');
    for (const char* d = hsImg + 8; isdigit((uint8_t)*d); d++) htmlEmit(*d);
    htmlEmit('\n');
  }
  if (!strcmp_P(hsTag, PSTR("a"))) {
    if (!hsClose) {
      hsInA = true;
      hsLocal = -1;
      if (hsTextSeen >= hsSkipText && WK.web.nlinks < WB_LINKS && hsWant == 0xFFFF) {
        hsLocal = WK.web.nlinks++; hsLinkGlobal[hsLocal] = hsLinkNo; hsLtLen = 0; WK.web.ltxt[hsLocal][0] = 0;
      }
      hsLinkNo++;
    } else if (hsInA) {
      hsInA = false;
      if (hsLocal >= 0 && hsWant == 0xFFFF) {
        char m[6]; snprintf_P(m, sizeof m, PSTR("[%d]"), hsLocal + 1);
        for (char* p = m; *p; p++) sinkRaw(*p);
        hsSpace = false;
      }
    }
  }
}
bool sinkHtml(char c) {
  int ch = u8Feed((uint8_t)c);
  if (ch < 0) return true;
  if (hsFound) return false;                        // got the link we wanted
  if (hsMode == 1) {                                // inside <...>
    if (ch == '>') {
      if (hsCapture) { hsCapture = false; WK.web.next[hsUrlLen] = 0; hsFound = true; }
      hsImgCap = false; hsImg[hsImgLen] = 0;
      hsMode = 0; htmlTagDone(); return wbLen < WB_TEXT - 8;
    }
    if (hsTagLen == 0 && ch == '/') { hsClose = true; return true; }
    if (hsHref == 0 && hsTagLen < 9 && isalnum(ch)) { hsTag[hsTagLen++] = ch; return true; }
    hsHref = 1;                                     // tag name finished: now attributes
    if (hsCapture) {
      if (hsUrlLen == 0 && !hsQuote && (ch == '"' || ch == '\'')) { hsQuote = ch; return true; }
      if ((hsQuote && ch == hsQuote) || (!hsQuote && (ch == ' ' || ch == '>'))) { hsCapture = false; WK.web.next[hsUrlLen] = 0; hsFound = true; return true; }
      if (hsUrlLen < sizeof WK.web.next - 1) WK.web.next[hsUrlLen++] = ch;
      return true;
    }
    if (hsImgCap) {
      if (hsImgLen == 0 && !hsImgQ && (ch == '"' || ch == '\'')) { hsImgQ = ch; return true; }
      if ((hsImgQ && ch == hsImgQ) || (!hsImgQ && ch == ' ')) { hsImgCap = false; hsImg[hsImgLen] = 0; return true; }
      if (hsImgLen < sizeof hsImg - 1) hsImg[hsImgLen++] = ch;
      return true;
    }
    memmove(hsRoll, hsRoll + 1, 4); hsRoll[4] = tolower(ch); hsRoll[5] = 0;
    if (!strcmp_P(hsRoll + 1, PSTR("src=")) && hsTagLen == 3 && !strncmp_P(hsTag, PSTR("img"), 3)) { hsImgCap = true; hsImgQ = 0; hsImgLen = 0; }
    if (!strcmp_P(hsRoll, PSTR("href=")) && hsTagLen == 1 && hsTag[0] == 'a' && hsLinkNo == hsWant) { hsCapture = true; hsQuote = 0; hsUrlLen = 0; }
    return true;
  }
  if (ch == '<') { hsMode = 1; hsTagLen = 0; hsClose = false; hsHref = 0; memset(hsRoll, 0, sizeof hsRoll); hsImg[0] = 0; hsImgLen = 0; hsImgCap = false; return true; }
  if (hsSkip) return true;
  if (hsEntLen) {                                   // &amp; &lt; &#39; ...
    if (ch == ';' || hsEntLen > 6) {
      hsEnt[hsEntLen] = 0;
      char out = '?';
      if (!strcmp_P(hsEnt, PSTR("&amp"))) out = '&'; else if (!strcmp_P(hsEnt, PSTR("&lt"))) out = '<'; else if (!strcmp_P(hsEnt, PSTR("&gt"))) out = '>';
      else if (!strcmp_P(hsEnt, PSTR("&quot"))) out = '"'; else if (!strcmp_P(hsEnt, PSTR("&nbsp"))) out = ' '; else if (!strcmp_P(hsEnt, PSTR("&apos"))) out = '\'';
      else if (hsEnt[1] == '#') { int v = atoi(hsEnt + 2); out = utf8ToCp437(v) ? utf8ToCp437(v) : '?'; }
      hsEntLen = 0;
      return htmlEmit(out);
    }
    hsEnt[hsEntLen++] = ch;
    return true;
  }
  if (ch == '&') { hsEnt[0] = '&'; hsEntLen = 1; return true; }
  if (ch == '\n' || ch == '\r' || ch == '\t') ch = ' ';
  return htmlEmit(ch);
}

// ---------- shared drawing ----------
void wbTitle(const char* t) {
  tft.fillScreen(C_LCD);
  drawTitleBar(t, wbAccent());
}
void wbError() {
  tft.fillRect(10, 90, 300, 50, E_WHITE);
  tft.drawRect(10, 90, 300, 50, E_CORAL);
  putText(20, 98, 1, E_CORAL, E_WHITE, FS("Could not load:"));
  putText(20, 112, 1, C_INK, E_WHITE, wbErr);
  putText(20, 126, 1, C_MID, E_WHITE, FS("press: try again   hold: back"));
}
// ---------- pictures (only on pages read through the PC relay; it dithers them to 1-bit PBM) ----------
const int PIC_LINES = 12;                           // a picture takes 12 text lines = 120 px
uint8_t wbPicMode = 0;                              // 0 = black & white, 1 = dot matrix, 2 = color, 3 = off
uint8_t wbAnimN = 0;                                // animated pictures on screen (GIFs): replayed frame by frame
uint16_t wbAnimId[2], wbAnimFrames[2], wbAnimFrame[2], wbAnimDelay[2];
int16_t wbAnimY[2];
uint32_t wbAnimAt[2];
uint8_t wbPicN = 0;                                 // pictures on screen still waiting to load
uint16_t wbPicId[3];
int16_t wbPicY[3], wbPicLabel[3];                   // top of the picture box, y of its "loading" label
uint32_t wbLastMove = 0;
// One reader for screen-view slices, text-view pictures and GIF frames, fed straight from the connection.
// A kind line, then:  P4 "W H" + rows of bits (B&W)   R8 "W H" + W*H bytes (color, rrrgggbb)   Z8 "W H" + the same
// bytes run-length packed   Q8 / QZ "N" + N changed rectangles, each "x y w h" + its pixels (QZ: packed).
// Packed: a control byte 0..127 = that many + 1 plain bytes follow; 128..255 = the next byte, (c - 125) times.
// Rows are read (or filled) straight into one row buffer and pushed to the panel whole.
// ---------- web sound on the piezo (screen view, Live + sound; relay v8 sends "A8\nN\n" + N samples) ----------
// 8000 samples a second, 8 bits, from the page's videos. A timer interrupt hands one sample every 125 us to the
// ESP8266's sigma-delta modulator, which drives the piezo (D6) as a fast 1-bit stream: raw and crackly, but real sound.
#define GPSD  ESP8266_REG(0x368)                     // the sigma-delta register (the core keeps these names to itself)
#define GPSDP 8
#define GPSDE 16
const uint16_t AU_N = 2048;                          // 0.25 s of sound in a ring
uint8_t* auBuf = nullptr;
volatile uint16_t auHead = 0, auTail = 0;
volatile bool auPlaying = false;
uint32_t auPeriod = 20000;
bool auOn = false;
void IRAM_ATTR auIsr() {
  timer0_write(ESP.getCycleCount() + auPeriod);
  uint8_t v = 128;
  if (auPlaying) {
    if (auTail != auHead) { v = auBuf[auTail]; auTail = (auTail + 1) & (AU_N - 1); }
    else auPlaying = false;                          // ran dry: wait for a little buffer again
  }
  GPSD = (GPSD & ~0xFFUL) | v;
}
void auStart() {
  if (auOn || !P.sound) return;
  auBuf = (uint8_t*)malloc(AU_N);
  if (!auBuf) return;
  soundAllOff();
  auHead = auTail = 0; auPlaying = false;
  pinMode(BUZZER_PIN, OUTPUT);
  GPSD = (1UL << GPSDE) | (4UL << GPSDP) | 128;     // sigma-delta on, at 128 (silence)
  GPC(BUZZER_PIN) |= (1 << GPCS);                    // the piezo pin now follows the sigma-delta
  auPeriod = ESP.getCpuFreqMHz() * 125UL;            // 8 kHz
  timer0_isr_init();
  timer0_attachInterrupt(auIsr);
  timer0_write(ESP.getCycleCount() + auPeriod);
  auOn = true;
}
void auStop() {
  if (!auOn) return;
  timer0_detachInterrupt();
  GPC(BUZZER_PIN) &= ~(1 << GPCS);
  GPSD = 0;
  digitalWrite(BUZZER_PIN, LOW);
  free(auBuf); auBuf = nullptr;
  auOn = false; auPlaying = false;
}
void auPush(const uint8_t* d, int n) {
  if (!auOn) return;
  for (int i = 0; i < n; i++) {
    uint16_t nh = (auHead + 1) & (AU_N - 1);
    if (nh == auTail) break;                          // full: the rest is dropped
    static const uint8_t AU_SHIFT[3] = {0, 1, 2};    // web sound: high = as sent, mid = half, low = quarter
    auBuf[auHead] = 128 + (((int)d[i] - 128) >> AU_SHIFT[P.volume % 3]); auHead = nh;
  }
  if (!auPlaying && ((auHead - auTail) & (AU_N - 1)) > AU_N / 4) auPlaying = true;
}
uint16_t imgAudioLeft = 0;                           // bytes of an "A8" sound chunk still to come
bool imgAudio = false;
uint8_t imgState = 0, imgLineLen = 0, imgVal = 0;  // state: 0 kind, 1 size / count, 2 rectangle line, 3 pixels, 4 sound
char imgLine[24];
bool imgColor = false, imgDelta = false, imgPacked = false, imgOk = false, imgDone = false, imgScreen = false;
bool imgHalf = false;                               // screen view "fast video": 156 x 96 frames, every pixel drawn 2 x 2
bool imgStream = false;                             // live stream: frames keep coming on one connection
uint16_t imgFrames = 0;                             // frames completed on the stream
int16_t imgX, imgY, imgW, imgH, imgRow, imgBaseY, imgClipTop, imgClipBot, imgPicX;
uint16_t imgRects = 0, imgRectsTotal = 0, imgByte = 0, imgRowsIn = 0, imgLit = 0, imgRep = 0, imgNeedVal = 0;
void imgBegin(bool screen, int baseY, int clipTop, int clipBot) {
  imgState = imgLineLen = 0; imgOk = imgDone = imgDelta = imgPacked = false; imgScreen = screen;
  imgBaseY = baseY; imgClipTop = clipTop; imgClipBot = clipBot;
  imgRects = imgRectsTotal = imgRowsIn = 0;
}
uint16_t c332(uint8_t c) {                          // rrrgggbb -> the panel's 16-bit color
  uint8_t r = c >> 5, g = (c >> 2) & 7, b = c & 3;
  return (uint16_t)(((r << 2) | (r >> 1)) << 11) | (uint16_t)(((g << 3) | g) << 5) | (uint16_t)((b << 3) | (b << 1) | (b >> 1));
}
void pushRow2(int x0, int y, const uint8_t* src, int w) {   // half-resolution color row: every pixel 2 x 2
  uint32_t blk[16];
  uint16_t* bp = (uint16_t*)blk;
  tft.startWrite();
  tft.Adafruit_ST7789::setAddrWindow(x0, y, w * 2, 2);
  for (int rep = 0; rep < 2; rep++)
    for (int x = 0; x < w; x += 16) {
      int n = min(16, w - x);
      for (int i = 0; i < n; i++) { uint16_t c = c332(src[x + i]); c = (c >> 8) | (c << 8); bp[2 * i] = bp[2 * i + 1] = c; }
      while (SPI1CMD & SPIBUSY) {}
      rawBits(n * 32); rawSend(blk);
    }
  while (SPI1CMD & SPIBUSY) {}
  tft.endWrite();
  if (rcLive && !rcMutePix) rcRowOut(x0, y, src, true, w, 0, 0, 2, 1);
}
void imgDrawRow(const uint8_t* row) {
  int y;
  if (!imgScreen && wbPicMode == 1 && !imgColor) {  // dot matrix: 3 px cells, paper too (so GIF frames overwrite)
    y = imgBaseY + imgRow * 3;
    if (y < imgClipTop || y + 2 > imgClipBot) return;
    for (int x = 0; x < imgW; x++) {
      bool ink = row[x >> 3] & (0x80 >> (x & 7));
      tft.fillRect(imgPicX + x * 3, y, 2, 2, ink ? C_INK : E_WHITE);
      if (!ink) tft.drawPixel(imgPicX + x * 3, y, E_RULE);
    }
    return;
  }
  if (imgScreen && imgHalf && imgColor) {
    y = imgBaseY + (imgY + imgRow) * 2;
    if (y < imgClipTop || y + 2 > imgClipBot) return;
    pushRow2(imgX * 2, y, row, min((int)imgW, 160 - imgX));
    return;
  }
  y = imgBaseY + imgY + imgRow;
  if (y < imgClipTop || y >= imgClipBot) return;
  if (imgScreen) pushRow(imgX, y, row, imgColor, imgW, 0x0000, 0xFFFF);
  else pushRow(imgPicX, y, row, imgColor, imgW, C_INK, E_WHITE);
}
int imgHeaderLine() {                               // one header line arrived: 0 = go on, -1 = stop reading
  if (imgState == 0) {                              // what kind of picture
    if (!strcmp_P(imgLine, PSTR("A8"))) { imgAudio = true; imgState = 1; return 0; }   // sound, between two frames
    bool p4 = !strcmp_P(imgLine, PSTR("P4")), z8 = !strcmp_P(imgLine, PSTR("Z8"));
    bool q8 = !strcmp_P(imgLine, PSTR("Q8")), qz = !strcmp_P(imgLine, PSTR("QZ"));
    if (!p4 && !z8 && !q8 && !qz && strcmp_P(imgLine, PSTR("R8"))) return -1;   // an error page, not a picture
    imgColor = !p4; imgPacked = z8 || qz; imgDelta = q8 || qz;
    imgState = 1;
    return 0;
  }
  int v[4] = {0, 0, 0, 0};
  const char* q = imgLine;
  for (int k = 0; k < 4 && *q; k++) { v[k] = atoi(q); while (*q && *q != ' ') q++; while (*q == ' ') q++; }
  if (imgState == 1 && imgAudio) { imgAudioLeft = v[0]; imgState = imgAudioLeft ? 4 : 0; if (!imgAudioLeft) imgAudio = false; return 0; }
  if (imgState == 1 && imgDelta) {                  // how many changed rectangles follow (0 = nothing changed)
    imgOk = true;
    if (v[0] <= 0) { imgDone = true; return -1; }
    imgRects = imgRectsTotal = v[0];
    imgState = 2;
    return 0;
  }
  if (imgState == 1) { imgX = imgY = 0; imgW = v[0]; imgH = v[1]; imgRects = imgRectsTotal = 1; }
  else { imgX = v[0]; imgY = v[1]; imgW = v[2]; imgH = v[3]; }
  if (imgW <= 0 || imgW > 320 || imgH <= 0 || imgX < 0 || imgX + imgW > 320) return -1;
  if (!imgScreen) imgPicX = 6 + max(0, (300 - (wbPicMode == 1 && !imgColor ? imgW * 3 : imgW)) / 2);
  imgOk = true; imgRow = 0; imgByte = 0; imgLit = imgRep = imgNeedVal = 0; imgState = 3;
  return 0;
}
int imgEndRow() {                                   // a row is complete: draw it. -1 = the whole picture arrived
  imgByte = 0;
  imgDrawRow((const uint8_t*)WK.web.next);
  imgRowsIn++;
  if (++imgRow < imgH) return 0;
  if (imgDelta && --imgRects) { imgState = 2; return 0; }
  imgDone = true;
  return -1;
}
// One frame (or picture) finished. A live stream goes straight on with the next one; anything else stops here.
bool imgNextFrame() {
  if (!imgStream || !imgDone) return false;
  imgFrames++;
  imgBegin(true, imgBaseY, imgClipTop, imgClipBot);
  return true;
}
// Feeds n received bytes through the decoder. -1 = the picture is complete (or broken): stop reading.
// Works on a block at a time (the network hands over up to 512 bytes per call instead of one byte per call).
int imgFeed(const uint8_t* in, int n) {
  uint8_t* row = (uint8_t*)WK.web.next;             // one row (<= 320 bytes) at a time
  int i = 0;
  while (i < n || (imgState == 3 && imgRep)) {
    if (imgState == 4) {                            // sound bytes: to the piezo player
      int k = min(n - i, (int)imgAudioLeft);
      auPush(in + i, k);
      i += k; imgAudioLeft -= k;
      if (!imgAudioLeft) { imgState = 0; imgAudio = false; }
      continue;
    }
    if (imgState < 3) {                             // the text lines of the header
      uint8_t c = in[i++];
      if (c == '\r') continue;
      if (c != '\n') { if (imgLineLen < sizeof imgLine - 1) imgLine[imgLineLen++] = (char)c; continue; }
      imgLine[imgLineLen] = 0; imgLineLen = 0;
      if (imgHeaderLine() < 0) { if (imgNextFrame()) continue; return -1; }
      continue;
    }
    int rowBytes = imgColor ? imgW : (imgW + 7) / 8;
    if (imgRep) {                                   // the rest of a repeated byte: nothing to read
      int k = min((int)imgRep, rowBytes - (int)imgByte);
      memset(row + imgByte, imgVal, k);
      imgRep -= k; imgByte += k;
    } else if (imgNeedVal) {                        // the byte a run repeats
      imgVal = in[i++]; imgRep = imgNeedVal; imgNeedVal = 0;
      continue;
    } else if (imgPacked && !imgLit) {              // a control byte
      uint8_t c = in[i++];
      if (c < 0x80) imgLit = c + 1; else imgNeedVal = c - 125;
      continue;
    } else {                                        // plain pixels: straight into the row
      int want = min(n - i, rowBytes - (int)imgByte);
      if (imgPacked) want = min(want, (int)imgLit);
      memcpy(row + imgByte, in + i, want);
      i += want; imgByte += want;
      if (imgPacked) imgLit -= want;
    }
    if (imgByte >= rowBytes && imgEndRow() < 0) { if (imgNextFrame()) continue; return -1; }
  }
  return 0;
}
int imgSink(WiFiClient* st, int n) {                // n bytes are waiting. Returns bytes used; -1 = all of it arrived
  uint8_t in[256];
  int used = 0;
  while (n > 0) {                                   // (one reply = one picture or one stream, so reading ahead is safe)
    int r = st->read(in, min(n, (int)sizeof in));
    if (r <= 0) break;
    n -= r; used += r;
    if (imgFeed(in, r) < 0) return -1;
  }
  return used;
}
bool wbPicFetch(int id, int top, int frame) {        // one picture (or one GIF frame) into its box
  char url[112];
  bool dots = wbPicMode == 1, color = wbPicMode == 2;
  snprintf_P(url, sizeof url, PSTR("http://%s/img?id=%d&w=%d&h=%d&f=%d&c=%d&z=1"), P.relay, id, dots ? 100 : 300, dots ? 38 : 116, frame, color ? 1 : 0);
  imgBegin(false, top + 2, 22, 192);
  webSinkStream = imgSink;
  int code = webFetch(url, nullptr);
  webSinkStream = nullptr;
  webSink = sinkHtml;
  return code == 200 && imgOk;
}
void wbPicLoad() {                                  // fetch the first waiting picture
  int id = wbPicId[0], top = wbPicY[0], ly = wbPicLabel[0];
  wbPicN--;
  memmove(wbPicId, wbPicId + 1, sizeof wbPicId[0] * wbPicN);
  memmove(wbPicY, wbPicY + 1, sizeof wbPicY[0] * wbPicN);
  memmove(wbPicLabel, wbPicLabel + 1, sizeof wbPicLabel[0] * wbPicN);
  char err[48];
  memcpy(err, wbErr, sizeof err);
  if (ly >= 22) { tft.fillRect(6, ly, 120, 9, E_WHITE); putText(6, ly, 1, C_MID, E_WHITE, FS("[loading picture...]")); }
  bool ok = wbPicFetch(id, top, 0);
  if (ok && wbHdrFrames > 1 && top >= 22 && top + 120 <= 192 && wbAnimN < 2) {   // a GIF fully on screen: animate it
    int k = wbAnimN++;
    wbAnimId[k] = id; wbAnimY[k] = top; wbAnimFrames[k] = wbHdrFrames; wbAnimFrame[k] = 0;
    wbAnimDelay[k] = constrain(wbHdrDelay, 60, 2000); wbAnimAt[k] = millis() + wbAnimDelay[k];
  }
  if (!ok) {
    char why[52];
    if (strstr_P(wbErr, PSTR("501"))) snprintf_P(why, sizeof why, PSTR("[picture: pip install pillow on the PC]"));
    else if (strstr_P(wbErr, PSTR("404"))) snprintf_P(why, sizeof why, PSTR("[picture expired: reload the page]"));
    else snprintf_P(why, sizeof why, PSTR("[picture failed: %.34s]"), wbErr[0] ? wbErr : FS("no data"));
    if (ly >= 22) { tft.fillRect(6, ly, 300, 9, E_WHITE); putText(6, ly, 1, E_CORAL, E_WHITE, why); }
  } else if (ly >= 22 && imgRowsIn < 9) tft.fillRect(6, ly, 120, 9, E_WHITE);   // tiny picture: clear the label
  memcpy(wbErr, err, sizeof err);
}
void wbAnimTick(uint32_t now) {                     // next frame of an on-screen GIF, one frame per loop
  for (int i = 0; i < wbAnimN; i++) {
    if ((int32_t)(now - wbAnimAt[i]) < 0) continue;
    wbAnimFrame[i] = (wbAnimFrame[i] + 1) % max((uint16_t)1, wbAnimFrames[i]);
    char err[48];
    memcpy(err, wbErr, sizeof err);
    uint32_t t0 = millis();                         // the next frame is due one GIF delay after this one started
    bool ok = wbPicFetch(wbAnimId[i], wbAnimY[i], wbAnimFrame[i]);
    memcpy(wbErr, err, sizeof err);
    if (!ok) { wbAnimN--; for (int k = i; k < wbAnimN; k++) { wbAnimId[k] = wbAnimId[k + 1]; wbAnimY[k] = wbAnimY[k + 1]; wbAnimFrames[k] = wbAnimFrames[k + 1]; wbAnimFrame[k] = wbAnimFrame[k + 1]; wbAnimDelay[k] = wbAnimDelay[k + 1]; wbAnimAt[k] = wbAnimAt[k + 1]; } return; }
    wbAnimAt[i] = t0 + wbAnimDelay[i];
    return;
  }
}
void wbWrapLine(const char* t, int len, int start, int* lines, bool draw, int y0) {   // word wrap text, 50 columns
  int pos = 0;
  while (pos < len) {
    int n = min(50, len - pos);
    if (pos + n < len) { int cut = n; while (cut > 0 && t[pos + cut] != ' ') cut--; if (cut > 0) n = cut; }
    if (draw && *lines >= start && *lines < start + 17) {
      char out[51]; memcpy(out, t + pos, n); out[n] = 0;
      putText(6, y0 + (*lines - start) * 10, 1, C_INK, E_WHITE, out);
    }
    (*lines)++;
    pos += n;
    while (pos < len && t[pos] == ' ') pos++;
  }
}
int wbLayout(int start, bool draw) {                // draws (or just counts) the text buffer as wrapped lines
  const char* t = WK.web.text;
  int lines = 0, i = 0, n = strlen(t), pics = 0, below = 0;
  if (draw) { tft.fillRect(0, 20, W - 6, 172, E_WHITE); wbPicN = 0; wbAnimN = 0; wbLastMove = millis(); }
  while (i <= n) {
    int e = i;
    while (e < n && t[e] != '\n') e++;
    if (e == i) lines++;                           // blank line
    else if (t[i] == '\x01') {                     // picture: reserve a box, load it once the knob rests
      int h = wbPicMode == 3 ? 1 : PIC_LINES;
      bool vis = lines + h > start && lines < start + 17;
      pics++; if (lines >= start + 17) below++;
      if (draw && vis) {
        int top = 22 + (lines - start) * 10, ly = max(top, 22);
        if (wbPicMode == 3) putText(6, top, 1, C_MID, E_WHITE, FS("[picture]"));
        else {
          tft.drawRect(4, max(top, 21), 304, min(top + 120, 191) - max(top, 21), E_RULE);
          putText(6, ly + 1, 1, C_MID, E_WHITE, FS("[picture...]"));
          if (wbPicN < 3) { wbPicId[wbPicN] = atoi(t + i + 1); wbPicY[wbPicN] = top; wbPicLabel[wbPicN++] = ly + 1; }
        }
      }
      lines += h;
    }
    else wbWrapLine(t + i, e - i, start, &lines, draw, 22);
    i = e + 1;
  }
  if (draw && pics) {                               // hint in the footer: pictures further down the page
    char h[16];
    if (below) snprintf_P(h, sizeof h, PSTR("%d pic%s %c"), below, below > 1 ? FS("s") : FS(""), 25);   // 25 = down arrow
    else snprintf_P(h, sizeof h, PSTR("%d pic%s  "), pics, pics > 1 ? FS("s") : FS(""));
    tft.fillRect(250, 212, 64, 9, C_LCD);
    putText(250, 212, 1, below ? wbAccent() : C_MID, C_LCD, h);
  }
  return lines;
}
void wbDrawText(const char* status) {
  tft.fillRect(0, 194, W, 46, C_LCD);               // footer first: the layout adds its picture hint to it
  putText(6, 198, 1, C_MID, C_LCD, status);
  putText(6, 212, 1, C_MID, C_LCD, FS("turn: scroll   press: menu   hold: back"));
  wbLines = wbLayout(wbLine, true);
  int h = 170, th = wbLines > 17 ? max(8, h * 17 / wbLines) : h;
  int ty = wbLines > 17 ? 20 + (h - th) * wbLine / max(1, wbLines - 17) : 20;
  tft.fillRect(W - 6, 20, 6, h + 2, E_HEAD);
  tft.fillRect(W - 5, ty, 4, th, wbAccent());
}

// ---------- weather (Open-Meteo: free, no key, plain HTTP) ----------
void wbWeatherIcon(const char* cond, int x, int y) {
  char c[40]; int i = 0;
  for (; cond[i] && i < 39; i++) c[i] = tolower((uint8_t)cond[i]);
  c[i] = 0;
  uint16_t sun = RGB565(255, 200, 40), cloud = RGB565(200, 206, 216), rain = RGB565(70, 140, 240);
  bool hasSun = strstr_P(c, PSTR("sun")) || strstr_P(c, PSTR("clear")) || strstr_P(c, PSTR("partly"));
  bool hasCloud = strstr_P(c, PSTR("cloud")) || strstr_P(c, PSTR("overcast")) || strstr_P(c, PSTR("partly")) || strstr_P(c, PSTR("rain")) || strstr_P(c, PSTR("snow")) || strstr_P(c, PSTR("thunder")) || strstr_P(c, PSTR("drizzle")) || strstr_P(c, PSTR("shower"));
  if (hasSun) { tft.fillCircle(x + 20, y + 18, 16, sun); for (int a = 0; a < 8; a++) { float r = a * 0.785f; tft.drawLine(x + 20 + cos(r) * 20, y + 18 + sin(r) * 20, x + 20 + cos(r) * 27, y + 18 + sin(r) * 27, sun); } }
  if (hasCloud) { tft.fillCircle(x + 30, y + 36, 14, cloud); tft.fillCircle(x + 46, y + 30, 18, cloud); tft.fillCircle(x + 62, y + 38, 12, cloud); tft.fillRect(x + 30, y + 38, 34, 12, cloud); }
  if (strstr_P(c, PSTR("rain")) || strstr_P(c, PSTR("drizzle")) || strstr_P(c, PSTR("shower"))) for (int k = 0; k < 4; k++) tft.drawLine(x + 30 + k * 10, y + 54, x + 26 + k * 10, y + 64, rain);
  if (strstr_P(c, PSTR("snow")) || strstr_P(c, PSTR("sleet"))) for (int k = 0; k < 4; k++) tft.fillCircle(x + 30 + k * 10, y + 60, 2, MF_WHITE);
  if (strstr_P(c, PSTR("thunder"))) tft.fillTriangle(x + 44, y + 50, x + 36, y + 66, x + 48, y + 62, sun);
  if (strstr_P(c, PSTR("fog")) || strstr_P(c, PSTR("mist")) || strstr_P(c, PSTR("haze"))) for (int k = 0; k < 3; k++) tft.fillRect(x + 6, y + 22 + k * 12, 64, 4, cloud);
}
float wxLat = 0, wxLon = 0;
char wxPlace[32] = "", wxFor[24] = "\x01";           // place name + the city setting it was looked up for
// value of "key" (optionally inside "section":{...}); idx >= 0 picks one element of an array value
bool jsonIn(const char* section, const char* key, char* out, size_t n, int idx) {
  char k[28];
  const char* p = WK.web.text;
  out[0] = 0;
  if (section) { snprintf_P(k, sizeof k, PSTR("\"%s\":"), section); p = strstr(p, k); if (!p) return false; }
  snprintf_P(k, sizeof k, PSTR("\"%s\":"), key);
  p = strstr(p, k);
  if (!p) return false;
  p += strlen(k);
  while (*p == ' ') p++;
  if (idx >= 0) {
    if (*p != '[') return false;
    p++;
    for (int i = 0; i < idx; i++) { p = strchr(p, ','); if (!p) return false; p++; }
  }
  size_t o = 0;
  if (*p == '"') { p++; while (*p && *p != '"' && o < n - 1) out[o++] = *p++; }
  else while (*p && *p != ',' && *p != '}' && *p != ']' && o < n - 1) out[o++] = *p++;
  out[o] = 0;
  return o > 0;
}
void wxText(int c, char* o, size_t n) {              // WMO weather code -> words (the icon reads these words)
  const char* t = c == 0 ? PSTR("Clear sky") : c == 1 ? PSTR("Mainly clear") : c == 2 ? PSTR("Partly cloudy") : c == 3 ? PSTR("Overcast") :
                  c <= 48 ? PSTR("Fog") : c <= 55 ? PSTR("Drizzle") : c <= 57 ? PSTR("Icy drizzle") : c == 61 ? PSTR("Light rain") :
                  c == 63 ? PSTR("Rain") : c <= 65 ? PSTR("Heavy rain") : c <= 67 ? PSTR("Freezing rain") : c <= 77 ? PSTR("Snow") :
                  c <= 82 ? PSTR("Rain showers") : c <= 86 ? PSTR("Snow showers") : PSTR("Thunderstorm");
  strncpy_P(o, t, n - 1); o[n - 1] = 0;
}
int dowOf(int y, int m, int d) {                     // 0 = Sunday (Sakamoto)
  static const uint8_t t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y--;
  return (y + y / 4 - y / 100 + y / 400 + t[(m - 1) % 12] + d) % 7;
}
void wbWeather() {
  wbTitle(FS("Weather"));
  char* url = WK.web.next;                           // the long forecast URL lives in the shared buffer
  const size_t urlN = sizeof WK.web.next;
  bool autoLoc = !P.webCity[0];
  if (strcmp(wxFor, P.webCity) || !wxPlace[0]) {     // look the place up once per city setting
    putText(10, 30, 1, C_MID, C_LCD, autoLoc ? FS("finding where you are...") : FS("looking up the city..."));
    wbLen = 0; WK.web.text[0] = 0; webSink = sinkText;
    if (autoLoc) snprintf_P(url, urlN, PSTR("http://ip-api.com/json/?fields=city,country,lat,lon"));
    else { char city[64]; urlEncode(P.webCity, city, sizeof city); snprintf_P(url, urlN, PSTR("http://geocoding-api.open-meteo.com/v1/search?name=%s&count=1"), city); }
    int code = webFetch(url, nullptr);
    char la[16], lo[16];
    if (code != 200 || !jsonIn(nullptr, autoLoc ? FS("lat") : FS("latitude"), la, sizeof la, -1) || !jsonIn(nullptr, autoLoc ? FS("lon") : FS("longitude"), lo, sizeof lo, -1)) {
      if (!wbErr[0]) { if (autoLoc) snprintf_P(wbErr, sizeof wbErr, PSTR("couldn't find where you are")); else snprintf_P(wbErr, sizeof wbErr, PSTR("city not found: %.24s"), P.webCity); }
      wbError(); return;
    }
    wxLat = atof(la); wxLon = atof(lo);
    char nm[24], co[20];
    jsonIn(nullptr, autoLoc ? FS("city") : FS("name"), nm, sizeof nm, -1);
    jsonIn(nullptr, FS("country"), co, sizeof co, -1);
    snprintf_P(wxPlace, sizeof wxPlace, PSTR("%s%s%s"), nm, co[0] ? FS(", ") : FS(""), co);
    snprintf_P(wxFor, sizeof wxFor, PSTR("%s"), P.webCity);
  }
  putText(10, 30, 1, C_MID, C_LCD, FS("getting the forecast...    "));
  snprintf_P(url, urlN, PSTR("http://api.open-meteo.com/v1/forecast?latitude=%.3f&longitude=%.3f"
             "&current=temperature_2m,relative_humidity_2m,apparent_temperature,precipitation,weather_code,wind_speed_10m,surface_pressure,uv_index,is_day"
             "&daily=weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset,precipitation_probability_max&timezone=auto&forecast_days=4"), wxLat, wxLon);
  wbLen = 0; WK.web.text[0] = 0; webSink = sinkText;
  int code = webFetch(url, nullptr);
  char t[12], fl[12], hu[8], pr[10], wc[6], ws[10], ps[10], uv[8], day[4], b[48];
  if (code != 200 || !jsonIn(FS("current"), FS("temperature_2m"), t, sizeof t, -1)) { if (!wbErr[0]) snprintf_P(wbErr, sizeof wbErr, PSTR("no weather data (Open-Meteo)")); wbError(); return; }
  jsonIn(FS("current"), FS("apparent_temperature"), fl, sizeof fl, -1);
  jsonIn(FS("current"), FS("relative_humidity_2m"), hu, sizeof hu, -1);
  jsonIn(FS("current"), FS("precipitation"), pr, sizeof pr, -1);
  jsonIn(FS("current"), FS("weather_code"), wc, sizeof wc, -1);
  jsonIn(FS("current"), FS("wind_speed_10m"), ws, sizeof ws, -1);
  jsonIn(FS("current"), FS("surface_pressure"), ps, sizeof ps, -1);
  jsonIn(FS("current"), FS("uv_index"), uv, sizeof uv, -1);
  jsonIn(FS("current"), FS("is_day"), day, sizeof day, -1);
  char cond[20];
  int code0 = atoi(wc);
  wxText(code0, cond, sizeof cond);
  bool night = day[0] == '0';
  if (night && code0 <= 1) strcpy_P(cond, PSTR("Clear night"));
  uint16_t acc = wbAccent();
  tft.fillRect(0, 20, W, 194, C_LCD);
  putText(10, 26, 1, C_MID, C_LCD, wxPlace);
  if (night && code0 <= 1) { tft.fillCircle(38, 66, 16, RGB565(250, 236, 180)); tft.fillCircle(47, 59, 14, C_LCD); }   // moon
  else wbWeatherIcon(cond, 12, 40);
  snprintf_P(b, sizeof b, PSTR("%.0f%cC"), atof(t), 0xF8);        putText(110, 40, 4, acc, C_LCD, b);
  putText(110, 76, 2, C_INK, C_LCD, cond);
  snprintf_P(b, sizeof b, PSTR("feels like %.0f%cC"), atof(fl), 0xF8); putText(110, 96, 1, C_MID, C_LCD, b);
  char sr[24] = "", ss[24] = "";
  jsonIn(FS("daily"), FS("sunrise"), sr, sizeof sr, 0);
  jsonIn(FS("daily"), FS("sunset"), ss, sizeof ss, 0);
  for (int i = 0; i < 6; i++) {
    int x = 10 + (i % 3) * 104, y = 110 + (i / 3) * 32;
    tft.fillRect(x, y, 98, 29, E_WHITE); tft.drawRect(x, y, 98, 29, E_RULE);
    putText(x + 5, y + 4, 1, C_MID, E_WHITE, pgmItem(PSTR("humidity\nwind\nrain now\npressure\nUV index\nsun up/down"), i));
    switch (i) {
      case 0: snprintf_P(b, sizeof b, PSTR("%s%%"), hu); break;
      case 1: snprintf_P(b, sizeof b, PSTR("%s km/h"), ws); break;
      case 2: snprintf_P(b, sizeof b, PSTR("%s mm"), pr); break;
      case 3: snprintf_P(b, sizeof b, PSTR("%.0f hPa"), atof(ps)); break;
      case 4: snprintf_P(b, sizeof b, PSTR("%s"), uv); break;
      default: snprintf_P(b, sizeof b, PSTR("%.5s-%.5s"), strlen(sr) > 11 ? sr + 11 : sr, strlen(ss) > 11 ? ss + 11 : ss); break;
    }
    putText(x + 5, y + 16, 1, C_INK, E_WHITE, b);
  }
  for (int d = 0; d < 4; d++) {                      // 4-day strip
    char dt[16], hi[8], lo[8], pp[6], dc[6], name[8];
    if (!jsonIn(FS("daily"), FS("time"), dt, sizeof dt, d)) break;
    jsonIn(FS("daily"), FS("temperature_2m_max"), hi, sizeof hi, d);
    jsonIn(FS("daily"), FS("temperature_2m_min"), lo, sizeof lo, d);
    jsonIn(FS("daily"), FS("precipitation_probability_max"), pp, sizeof pp, d);
    jsonIn(FS("daily"), FS("weather_code"), dc, sizeof dc, d);
    if (d == 0) strcpy_P(name, PSTR("Today"));
    else { int w = dowOf(atoi(dt), atoi(dt + 5), atoi(dt + 8)); strncpy_P(name, PSTR("SunMonTueWedThuFriSat") + w * 3, 3); name[3] = 0; }
    int x = 8 + d * 78;
    snprintf_P(b, sizeof b, PSTR("%s %.0f/%.0f"), name, atof(hi), atof(lo));
    putText(x, 178, 1, d ? C_INK : acc, C_LCD, b);
    int pc = atoi(dc);
    snprintf_P(b, sizeof b, pc >= 51 ? PSTR("rain %s%%") : pc >= 2 ? PSTR("cloudy") : PSTR("sunny"), pp);
    putText(x, 189, 1, C_MID, C_LCD, b);
  }
  putText(6, 204, 1, C_MID, C_LCD, FS("press: refresh   hold: back   Open-Meteo"));
}

// ---------- pages: news + FrogFind ----------
// The PC relay (extras/claude_relay.py) can also search (DuckDuckGo) and read any page, HTTPS included.
// Turn a FrogFind address into the same request for the relay. Returns false if it isn't a FrogFind URL.
                            // FrogFind didn't answer this session: go straight to the PC
void wbLoadPage(int chunk) {                        // chunk = which ~2.5 KB slice of the text to show
  char title[24];
  const char* u = WK.web.url;
  snprintf_P(title, sizeof title, PSTR("Web %.9s"), strstr_P(u, PSTR("68k")) ? FS("News") : strstr_P(u, PSTR("/search?")) ? FS("search") :
             strstr_P(u, PSTR("/read?")) ? FS("text view") : strstr(u, P.relay) ? FS("relay") : FS("page"));
  wbTitle(title);
  wbChunk = chunk; wbLine = 0;
  htmlReset(chunk * (WB_TEXT - 200), 0xFFFF);
  webSink = sinkHtml;
  int code = webFetch(WK.web.url, nullptr);
  if (code != 200 && !wbLen) { wbError(); return; }
  char st[56];
  snprintf_P(st, sizeof st, PSTR("part %d   %d links   %.24s"), chunk + 1, WK.web.nlinks, WK.web.url + (strncmp_P(WK.web.url, PSTR("http://"), 7) ? 0 : 7));
  wbDrawText(st);
}
void wbOpenLink(int k) {                             // re-read the page, grab link #k's address, open it
  wbTitle(FS("Following link..."));
  htmlReset(wbChunk * (WB_TEXT - 200), hsLinkGlobal[k]);
  webSink = sinkHtml;
  webFetch(WK.web.url, nullptr);
  if (!hsFound) { snprintf_P(wbErr, sizeof wbErr, PSTR("link address not found")); wbError(); return; }
  webResolve(WK.web.url, WK.web.next, WK.web.text, WB_TEXT);   // the page text is reloaded next anyway
  snprintf_P(WK.web.url, sizeof WK.web.url, PSTR("%s"), WK.web.text);
  wbLoadPage(0);
}
void wbGo(const char* url) { snprintf_P(WK.web.url, sizeof WK.web.url, PSTR("%s"), url); wbPage = WB_PAGE; wbLoadPage(0); }

// ---------- screen view: Edge on your PC renders the page, the Psion shows it in color, 312 px wide ----------
// The relay keeps one live browser tab. The Psion asks for 192-row slices of it (1 byte per pixel, RGB332)
// and for the links/fields inside the slice; picking one clicks it (or types into it) on the PC.
const int SV_Y0 = 20, SV_H = 192, SV_W = 312;
long svY = 0, svPageH = 0, svX = 0, svPageW = SV_W;   // sideways too: pages wider than the screen (zoomed in, or wide sites)
uint16_t svZoom = 87;                               // the relay's zoom, % (it tells us with every page)
bool svSide = false;                                // the knob scrolls sideways
uint8_t svSubMenu = 0;                              // 1 = the zoom list is open
const uint8_t SV_ZOOMS[10] = {40, 50, 60, 70, 87, 100, 115, 130, 150, 200};
int svSel = -1, svN = 0;
bool svPick = false, svColor = true, svNeed = false, svOpen = false, svLive = false;
uint32_t svLiveAt = 0, svSettleEnd = 0, svFpsAt = 0;
bool svSettling = false, svForceFull = true;        // settling: refresh fast after opening a page until it stops changing
uint16_t svLastRects = 0, svFrames = 0, svFps = 0;
uint8_t svQuiet = 0;
bool svSettleSeen = false;                         // something changed while settling
uint32_t svMoveAt = 0;
char svTitle[44] = "";
int svTypeId = -1;                                  // the field being typed into
// Mouse pointer: the relay moves Edge's mouse to it (hover menus open) and draws it into the picture, so only the
// little bits that change come back. Knob: turn = up/down, push + turn = sideways, press = click, hold = off.
// The PC remote's mouse drives it too (svCurAuto: then the knob scrolls again and switches it off).
bool svCur = false, svCurAuto = false, svCurMoved = false;
int16_t svCx = SV_W / 2, svCy = SV_H / 2;           // the pointer, in the picture area
char svHover[40] = "";                              // what it's over: "link: Example", "field: search"...
const char* svRest = nullptr;                       // after svCall(): the reply's lines after title + address
void pushRow(int x0, int y, const uint8_t* src, bool color, int w, uint16_t ink, uint16_t paper) {   // one row straight to the panel
  uint32_t blk[16];                                  // (straight into the SPI FIFO: ~3x quicker than writePixels)
  uint16_t* bp = (uint16_t*)blk;
  tft.startWrite();
  tft.Adafruit_ST7789::setAddrWindow(x0, y, w, 1);
  for (int x = 0; x < w; x += 32) {
    int n = min(32, w - x);
    for (int i = 0; i < n; i++) {
      int xx = x + i;
      uint16_t c = color ? c332(src[xx]) : ((src[xx >> 3] & (0x80 >> (xx & 7))) ? ink : paper);
      bp[i] = (c >> 8) | (c << 8);
    }
    while (SPI1CMD & SPIBUSY) {}
    rawBits(n * 16); rawSend(blk);
  }
  while (SPI1CMD & SPIBUSY) {}
  tft.endWrite();
  if (rcLive && !rcMutePix) rcRowOut(x0, y, src, color, w, ink, paper, 1, 1);   // PC viewer (live video isn't mirrored)
}
// Every relay request is built in WK.web.next: 400 bytes that would otherwise sit on the 4 KB stack.
void svBuild(PGM_P cmd, const char* arg, bool raw) {   // "http://relay/rb/<cmd><arg>", arg URL-encoded unless raw
  char* u = WK.web.next;
  const size_t n = sizeof WK.web.next;
  snprintf_P(u, n, PSTR("http://%s/rb/"), P.relay);
  size_t o = strlen(u);
  strncpy_P(u + o, cmd, n - o - 1); u[n - 1] = 0;
  o = strlen(u);
  if (arg && o < n - 1) { if (raw) snprintf_P(u + o, n - o, PSTR("%s"), arg); else urlEncode(arg, u + o, n - o); }
}
bool svCall() {                                      // send the request in WK.web.next -> height, title, address
  svFooter(FS("working on it..."), FS(""));
  wbLen = 0; WK.web.text[0] = 0; webSink = sinkText;
  webWaitMs = 30000;
  int code = webFetch(WK.web.next, nullptr);
  webWaitMs = 9000;
  if (code != 200 || strncmp_P(WK.web.text, PSTR("OK "), 3)) {
    if (!wbErr[0]) snprintf_P(wbErr, sizeof wbErr, PSTR("%.44s"), WK.web.text[0] ? WK.web.text : FS("relay didn't answer"));
    return false;
  }
  long ph = 0, pw = 0; int navI = 0, zm = 0;          // "OK height navigated width zoom" (older relays: first two only)
  sscanf(WK.web.text + 3, FS("%ld %d %ld %d"), &ph, &navI, &pw, &zm);
  svPageH = ph;
  svPageW = max((long)SV_W, pw);
  if (zm > 0) svZoom = zm;
  bool nav = navI != 0;
  char* t = strchr(WK.web.text, '\n');
  char* u = t ? strchr(t + 1, '\n') : nullptr;
  svRest = nullptr;
  if (t && u) {
    *u = 0;
    snprintf_P(svTitle, sizeof svTitle, PSTR("%.43s"), t + 1);
    char* e = strchr(u + 1, '\n');
    if (e) { *e = 0; svRest = e + 1; }
    snprintf_P(WK.web.url, sizeof WK.web.url, PSTR("%s"), u + 1);
  }
  if (nav) { svY = 0; svX = 0; }
  svY = constrain(svY, 0L, max(0L, svPageH - SV_H));
  svX = constrain(svX, 0L, max(0L, svPageW - SV_W));
  svOpen = true;
  return true;
}
bool svDraw(int y0, int h, bool changes) {           // rows [y0, y0 + h) of the picture area, from the relay
  char url[150];                                     // changes: only what changed since the last whole slice
  bool d = changes && !svForceFull && y0 == 0 && h == SV_H;
  int n = snprintf_P(url, sizeof url, PSTR("http://%s/rb/img?x=%ld&y=%ld&h=%d&c=%d&a=%d&d=%d&z=1"), P.relay, svX, svY + y0, h, svColor ? 1 : 0, svLive ? 1 : 0, d ? 1 : 0);
  if (svCur && svCy >= y0 && svCy < y0 + h && n > 0 && n < (int)sizeof url)   // the mouse pointer, inside this slice
    snprintf_P(url + n, sizeof url - n, PSTR("&mx=%d&my=%d"), svCx, svCy - y0);
  imgBegin(true, SV_Y0 + y0, SV_Y0, SV_Y0 + SV_H);
  webSinkStream = imgSink;
  webQuiet = changes;                                // fast refreshes: no spinner blinking in the title bar
  webWaitMs = 30000;
  int code = webFetch(url, nullptr);
  webWaitMs = 9000;
  webQuiet = false;
  webSinkStream = nullptr;
  webSink = sinkHtml;
  bool ok = code == 200 && imgOk;
  if (ok && svCur && y0 == 0) snprintf_P(svHover, sizeof svHover, PSTR("%s"), wbHdrHover);
  svLastRects = imgRectsTotal;
  svForceFull = !(ok && imgDone);                    // anything missing: the next one must be whole again
  bool sized = false;
  if (ok && wbHdrHeight > 0 && wbHdrHeight != svPageH) { svPageH = wbHdrHeight; sized = true; }
  if (ok && wbHdrWidth >= SV_W && wbHdrWidth != svPageW) { svPageW = wbHdrWidth; sized = true; }
  if (sized) svScrollbar();
  return ok;
}
// Live: one long reply from the relay with frame after frame (whole, then only what changed). Runs until the knob is
// touched or the relay stops; no request per frame, and the PC prepares the next frame while this one is drawn.
// "Fast video" asks for half-resolution frames (each pixel drawn 2 x 2): 4x less data, so big videos keep up.
uint32_t svStreamFails = 0;
bool svStream() {
  char url[160];
  int un = snprintf_P(url, sizeof url, PSTR("http://%s/rb/live?x=%ld&y=%ld&h=%d&c=%d&s=%d&snd=%d"), P.relay, svX, svY, SV_H, svColor ? 1 : 0, P.svFast && svColor ? 2 : 1, P.webSound ? 1 : 0);
  if (svCur && un > 0 && un < (int)sizeof url) snprintf_P(url + un, sizeof url - un, PSTR("&mx=%d&my=%d"), svCx, svCy);
  WiFiClient cl;
  HTTPClient http;
  http.useHTTP10(true);
  http.setTimeout(4000);
  if (!http.begin(cl, url)) return false;
  int code = http.GET();
  if (code != 200) { http.end(); snprintf_P(wbErr, sizeof wbErr, PSTR("live: relay said %d (update the relay?)"), code); return false; }
  WiFiClient* st = http.getStreamPtr();
  imgBegin(true, SV_Y0, SV_Y0, SV_Y0 + SV_H);
  imgStream = true; imgHalf = P.svFast && svColor; imgFrames = 0;
  imgAudio = false; imgAudioLeft = 0;
  if (P.webSound) auStart();
  rcMutePix = true;                                  // a PC viewer gets a still, not every video frame
  uint8_t in[256];
  uint32_t last = millis(), fpsAt = millis();
  uint16_t framesAt = 0;
  bool ok = true;
  while (st && (http.connected() || st->available())) {
    int n = st->available();
    if (n > 0) {
      int r = st->read(in, min(n, (int)sizeof in));
      if (r > 0) { last = millis(); if (imgFeed(in, r) < 0) { ok = false; break; } }
    } else {
      if (millis() - last > 4000) { ok = false; break; }
      delay(1);
    }
    updateButton(); updateSound(); tickClock(); rcService();
    if (encCount || fPress || fShort || fLong || rcPtrMove || rcPtrClick) break;   // the knob (or the PC's mouse): hand over to svTick
    uint32_t t = millis();
    if (t - fpsAt >= 1000) {
      svFps = (uint16_t)((imgFrames - framesAt) * 1000UL / (t - fpsAt)); framesAt = imgFrames; fpsAt = t;
      svHint(); oledDirty = true;
    }
    yield();
  }
  imgStream = false; imgHalf = false;
  rcMutePix = false;
  auStop();
  http.end();
  svForceFull = true;                                // the relay may have sent a frame we never drew
  return ok || imgFrames > 0;
}
void svSettle(uint32_t ms) {                         // keep refreshing what changes for a while (pictures arriving)
  if (svForceFull) return;                           // the screen didn't load: nothing to settle
  svSettling = true; svSettleEnd = millis() + ms; svQuiet = 0; svSettleSeen = false;
  if (!svLive) { svFrames = 0; svFps = 0; svFpsAt = millis(); }
}
void svLoadEls() {                                   // the links + fields in view: "id x y w h kind text" lines
  char url[96];
  snprintf_P(url, sizeof url, PSTR("http://%s/rb/els?x=%ld&y=%ld&h=%d"), P.relay, svX, svY, SV_H);
  wbLen = 0; WK.web.text[0] = 0; webSink = sinkText;
  webFetch(url, nullptr);
  svN = 0;
  for (const char* p = WK.web.text; *p; p++) if (*p == '\n') svN++;
}
bool svEl(int i, int* id, int* x, int* y, int* w, int* h, char* kind, char* txt, size_t n) {
  const char* p = WK.web.text;
  for (int k = 0; k < i && p; k++) { p = strchr(p, '\n'); if (p) p++; }
  if (!p || !*p) return false;
  int used = 0;
  if (sscanf(p, FS("%d %d %d %d %d %c %n"), id, x, y, w, h, kind, &used) < 6) return false;
  size_t o = 0;
  for (p += used; *p && *p != '\n' && o < n - 1; p++) o++, txt[o - 1] = *p;
  txt[o] = 0;
  return true;
}
void svHBar() {                                      // sideways position, a thin bar along the top of the footer
  tft.fillRect(0, SV_Y0 + SV_H, SV_W, 3, svPageW > SV_W ? E_HEAD : C_LCD);
  if (svPageW <= SV_W) return;
  long tw = max(10L, (long)SV_W * SV_W / svPageW), tx = (SV_W - tw) * svX / max(1L, svPageW - SV_W);
  tft.fillRect(tx, SV_Y0 + SV_H, tw, 3, svSide ? E_AMBER : wbAccent());
}
void svFooter(const char* a, const char* b) {
  tft.fillRect(0, SV_Y0 + SV_H + 3, W, H - SV_Y0 - SV_H - 3, C_LCD);
  tft.fillRect(SV_W, SV_Y0 + SV_H, W - SV_W, 3, C_LCD);
  svHBar();
  putText(4, 217, 1, C_INK, C_LCD, a);
  putText(4, 229, 1, C_MID, C_LCD, b);
}
void svScrollbar() {
  tft.fillRect(312, SV_Y0, 8, SV_H, E_HEAD);
  long h = SV_H, th = svPageH > SV_H ? max(8L, h * SV_H / svPageH) : h;
  long ty = svPageH > SV_H ? SV_Y0 + (h - th) * svY / max(1L, svPageH - SV_H) : SV_Y0;
  tft.fillRect(314, ty, 5, th, svSide ? E_HEAD : wbAccent());
  svHBar();
}
void svHint() {
  char b[56];
  if (svPick) {
    int id, x, y, w, h; char k = 0, t[30] = "";
    if (svSel >= 0 && svEl(svSel, &id, &x, &y, &w, &h, &k, t, sizeof t))
      snprintf_P(b, sizeof b, PSTR("%s %.28s"), k == 'a' ? FS("link:") : k == 'i' ? FS("type into:") : k == 's' ? FS("choose:") : FS("button:"), t);
    else snprintf_P(b, sizeof b, PSTR("nothing to click here - scroll on"));
    svFooter(b, svN ? FS("turn: pick  press: go  hold: stop picking") : FS("hold: stop picking"));
  } else if (svCur) {
    snprintf_P(b, sizeof b, PSTR("%.50s"), svHover[0] ? svHover : FS("pointer: turn = up/down, push+turn = sideways"));
    char f[52];
    if (svLive || svSettling) snprintf_P(f, sizeof f, PSTR("press: click  hold: pointer off  %s %u fps"), svLive ? FS("live") : FS("loading"), svFps);
    else snprintf_P(f, sizeof f, PSTR("press: click   hold: pointer off"));
    svFooter(b, f);
  } else {
    if (svPageW > SV_W) snprintf_P(b, sizeof b, PSTR("y %ld/%ld x %ld/%ld  zoom %u%%"), svY, svPageH, svX, svPageW - SV_W, svZoom);
    else snprintf_P(b, sizeof b, PSTR("%ld/%ld px  zoom %u%%  %s"), svY, svPageH, svZoom, svColor ? FS("") : FS("B&W"));
    char f[52];
    if (svLive || svSettling) snprintf_P(f, sizeof f, PSTR("%s %u fps%s  press: menu  hold: back"), svLive ? FS("live") : FS("loading"), svFps, svLive && P.svFast ? FS(" (fast)") : FS(""));
    else if (svSide) snprintf_P(f, sizeof f, PSTR("turn: SIDEWAYS   press: menu   hold: back"));
    else snprintf_P(f, sizeof f, PSTR("turn: scroll   press: menu   hold: back"));
    svFooter(b, f);
  }
}
void svRefresh() {                                   // the whole view: picture, links, chrome
  wbPage = WB_SCREEN;
  svCurMoved = false;                                // (the pointer is drawn into this picture)
  drawTitleBar(svTitle[0] ? svTitle : FS("Screen view"), wbAccent());
  tft.fillRect(0, 18, W, 2, C_LCD);
  svScrollbar();
  svFooter(FS("loading the screen..."), FS(""));
  if (!svDraw(0, SV_H, false)) {                            // clear the picture area too: no stale menu left on it
    tft.fillRect(0, SV_Y0, 312, SV_H, C_LCD);
    svFooter(FS("couldn't get the screen:"), wbErr[0] ? wbErr : FS("is the relay running?"));
    return;
  }
  svLiveAt = millis();
  if (svPick) svLoadEls(); else svN = -1;            // (links only when picking one: one request less per screen)
  if (svSel >= svN) svSel = svN - 1;
  if (svPick && svSel >= 0) svMark(true);
  svHint();
}
void svMark(bool on) {                               // pick mode: a frame around the chosen link
  int id, x, y, w, h; char k, t[4];
  if (svSel < 0 || !svEl(svSel, &id, &x, &y, &w, &h, &k, t, sizeof t)) return;
  int top = max(0, y - 3), bot = min(SV_H, y + h + 3);
  if (bot <= top) return;
  if (!on) { svDraw(top, bot - top, false); return; }   // redraw just those rows from the relay
  int rx = max(0, x - 3), rw = min(312 - rx, w + 6);
  uint16_t acc = wbAccent();
  tft.drawRect(rx, SV_Y0 + top, rw, bot - top, acc);
  if (rw > 2 && bot - top > 2) tft.drawRect(rx + 1, SV_Y0 + top + 1, rw - 2, bot - top - 2, acc);
}
void svStartBuilt() {                               // run the request already built in WK.web.next, then show it
  wbPage = WB_SCREEN; svPick = false; svSel = -1;
  tft.fillScreen(C_LCD);
  drawTitleBar(FS("Screen view"), wbAccent());
  if (!svCall()) { svScrollbar(); svFooter(FS("couldn't open it:"), wbErr); wbError(); return; }
  svRefresh();
  svSettle(6000);                                    // keep refreshing while pictures and fonts arrive
}
void svStart(PGM_P cmd, const char* arg, bool raw) {   // open/click/back/reload/info, then show the result
  if (!svOpen && strncmp_P("open", cmd, 4)) { cmd = PSTR("open?u="); arg = "about:blank"; raw = true; }
  svBuild(cmd, arg, raw);
  svStartBuilt();
}
void svActivate() {                                  // press in pick mode: click it, or type into it
  int id, x, y, w, h; char k, t[30];
  if (svSel < 0 || !svEl(svSel, &id, &x, &y, &w, &h, &k, t, sizeof t)) return;
  if (k == 'i' || k == 's') {
    svTypeId = id;
    wbAsk = WA_STYPE;                                // the page stays visible: keys on the OLED, text in the field
    okBegin(k == 's' ? FS("Choose (type a few letters):") : FS("Type:"), FS(""), 120, x, y, w, h);
    return;
  }
  char idb[8];
  snprintf_P(idb, sizeof idb, PSTR("%d"), id);
  uiClick();
  svStart(PSTR("click?id="), idb, true);
}
// ---------- the mouse pointer ----------
void svCurOff() {
  svCur = false; svCurAuto = false; svCurMoved = false; svHover[0] = 0;
  if (svOpen) svDraw(0, SV_H, true);                 // the picture without it (only what changed comes back)
  svHint();
}
void svCurMove(int c, bool side) {                   // side: the knob is pushed in while turning
  if (side) btnUsed = true;                          // (so letting go is not a click)
  int n = abs(c), d = (c > 0 ? 1 : -1) * (n >= 3 ? n * 10 : n * 4);   // a fast spin goes further
  int16_t& p = side ? svCx : svCy;
  int lim = side ? SV_W - 1 : SV_H - 1, np = p + d;
  if (np < 0 || np > lim) {                          // past the edge: the page scrolls under the pointer instead
    long& off = side ? svX : svY;
    long top = side ? max(0L, svPageW - SV_W) : max(0L, svPageH - SV_H);
    long no = constrain(off + (d < 0 ? -1L : 1L) * (long)max(32, abs(d) * 3), 0L, top);
    if (no != off) {
      off = no; svNeed = true; svMoveAt = millis(); svScrollbar();
      char b[40]; snprintf_P(b, sizeof b, PSTR("%ld/%ld px"), side ? svX : svY, side ? svPageW - SV_W : svPageH);
      svFooter(b, FS("(let go of the knob to load)"));
    }
    np = constrain(np, 0, lim);
  }
  p = np;
  svCurMoved = true;
}
void svCurShow() {                                   // the pointer moved: the relay draws it + any hover effects
  if (!svOpen) return;
  if (!svDraw(0, SV_H, true)) { svFooter(FS("couldn't get the screen:"), wbErr[0] ? wbErr : FS("is the relay running?")); return; }
  svLiveAt = millis() + 300;                         // live video waits until the pointer rests
  svHint(); oledDirty = true;
}
void svCurClick() {                                  // a real mouse click there (relay v6); a text field -> keyboard
  if (!svOpen) return;
  char a[40];
  snprintf_P(a, sizeof a, PSTR("x=%ld&y=%ld"), svX + svCx, svY + svCy);
  svBuild(PSTR("clickat?"), a, true);
  uiClick();
  if (!svCall()) { svFooter(FS("couldn't click there:"), wbErr[0] ? wbErr : FS("update the relay (v6)?")); return; }
  int id, fx, fy, fw, fh, used = 0; char k = 0;
  if (svRest && sscanf(svRest, FS("F %d %d %d %d %d %c %n"), &id, &fx, &fy, &fw, &fh, &k, &used) >= 6) {
    char init[64]; size_t o = 0;                     // the text already in the field: edit it, don't start over
    for (const char* q = svRest + used; used && *q && *q != '\n' && o < sizeof init - 1; q++) init[o++] = *q;
    init[o] = 0;
    svCurMoved = false;
    svDraw(0, SV_H, true);                           // the page with the field selected
    svTypeId = id; wbAsk = WA_STYPE;
    okBegin(k == 's' ? FS("Choose (type a few letters):") : FS("Type:"), init, 120, (int)(fx - svX), (int)(fy - svY), fw, fh);
    return;
  }
  svRefresh(); svSettle(2500);
}
void svTick(uint32_t now, int c, bool sp) {
  if (svCur && svCurAuto && c && !btnDown) svCurOff();   // the PC's mouse had it: the knob scrolls again
  if (svCur) {
    if (c) svCurMove(c, btnDown);
    if (sp) { svCurClick(); return; }
    c = 0;
    if (svCurMoved && !svNeed) { svCurMoved = false; svCurShow(); return; }
  }
  if (svPick && !svN && c) svPick = false;           // nothing to pick here: the knob scrolls again
  if (svPick) {
    if (c && svN) {
      int ns = svSel + (c > 0 ? 1 : -1);
      if (ns >= svN && svY + SV_H < svPageH) { svMark(false); svY = min(svPageH - SV_H, svY + SV_H / 2); svSel = 0; svRefresh(); return; }
      if (ns < 0 && svY > 0) { svMark(false); svY = max(0L, svY - SV_H / 2); svSel = 0; svRefresh(); svMark(false); svSel = max(0, svN - 1); svMark(true); svHint(); return; }
      ns = constrain(ns, 0, svN - 1);
      if (ns != svSel) { svMark(false); svSel = ns; svMark(true); svHint(); uiClick(); }
    }
    if (sp) svActivate();
    return;
  }
  if (c && svSide && svPageW > SV_W) {               // sideways
    long nx = constrain(svX + (long)c * 48, 0L, svPageW - SV_W);
    if (nx != svX) { svX = nx; svNeed = true; svMoveAt = now; svScrollbar(); char b[40]; snprintf_P(b, sizeof b, PSTR("x %ld/%ld px"), svX, svPageW - SV_W); svFooter(b, FS("(let go of the knob to load)")); }
  } else if (c) {
    long ny = constrain(svY + (long)c * 48, 0L, max(0L, svPageH - SV_H));
    if (ny != svY) { svY = ny; svNeed = true; svMoveAt = now; svScrollbar(); char b[40]; snprintf_P(b, sizeof b, PSTR("%ld/%ld px"), svY, svPageH); svFooter(b, FS("(let go of the knob to load)")); }
  }
  if (svNeed && now - svMoveAt > 280) { svNeed = false; svRefresh(); svSettle(2500); }   // lazy pictures come after scrolling
  else if (!svNeed && svLive && !svSettling && !sp && (int32_t)(now - svLiveAt) >= 0) {
    if (!svStream()) {                               // the relay can't stream (older relay?) or went away
      svStreamFails++;
      svFooter(FS("live stopped:"), wbErr[0] ? wbErr : FS("is the relay running?"));
      svLiveAt = millis() + 2000;
    } else svLiveAt = millis();
    return;                                          // the knob that stopped the stream is handled next loop
  }
  else if (!svNeed && svSettling && (int32_t)(now - svLiveAt) >= 100) {
    svLiveAt = now;                                  // GIFs, videos, pictures still loading: only what changed
    if (!svDraw(0, SV_H, true)) {                    // the relay went away: say so, try again in a while
      svSettling = false; svLiveAt = millis() + 2000;
      svFooter(FS("couldn't get the screen:"), wbErr[0] ? wbErr : FS("is the relay running?"));
    } else {
      svFrames++;
      if (svLastRects) { svQuiet = 0; svSettleSeen = true; } else svQuiet++;
      uint32_t t = millis();
      bool hint = t - svFpsAt >= 1000;
      if (hint) { svFps = (uint16_t)(svFrames * 1000UL / (t - svFpsAt)); svFrames = 0; svFpsAt = t; }
      if (svSettling && ((int32_t)(t - svSettleEnd) > 0 || svQuiet >= 8)) {   // the page stopped changing
        svSettling = false; hint = true;
        if (svSettleSeen && svPick) svLoadEls();     // it moved: so may its links
      }
      if (hint) svHint();
    }
  }
  if (sp) svShowMenu();
}
void svShowMenu() {                                  // labels show the current state (zoom %, on/off)
  char it[12][28];
  const char* p[12];
  snprintf_P(it[0], 28, PSTR("Mouse pointer: %s"), svCur ? FS("on") : FS("off"));
  snprintf_P(it[1], 28, PSTR("Pick a link or field"));
  snprintf_P(it[2], 28, PSTR("Go to address or search..."));
  snprintf_P(it[3], 28, PSTR("Back"));
  snprintf_P(it[4], 28, PSTR("Reload"));
  snprintf_P(it[5], 28, PSTR("Top of the page"));
  snprintf_P(it[6], 28, PSTR("Zoom: %u%%  (change)..."), svZoom);
  snprintf_P(it[7], 28, PSTR("Knob scrolls: %s"), svSide ? FS("sideways") : FS("up/down"));
  snprintf_P(it[8], 28, PSTR("Live video: %s"), !svLive ? FS("off") : P.webSound ? FS("on + sound") : FS("on, silent"));
  snprintf_P(it[9], 28, PSTR("Picture: %s"), !svColor ? FS("black & white") : P.svFast ? FS("color, fast video") : FS("color, sharp video"));
  snprintf_P(it[10], 28, PSTR("Text view of this page"));
  snprintf_P(it[11], 28, PSTR("Ask Claude about it..."));
  for (int i = 0; i < 12; i++) p[i] = it[i];
  wbMenu = true; svSubMenu = 0;
  pmShow(FS("Screen view"), p, 12, wbAccent());
}
void svSetZoom(int z) {                              // the relay re-lays out the page right away (no restart)
  double pos = svPageH > SV_H ? (double)svY / svPageH : 0;   // keep the same spot on the page
  double posX = svPageW > SV_W ? (double)svX / svPageW : 0;
  char zb[8]; snprintf_P(zb, sizeof zb, PSTR("%d"), z);
  svBuild(PSTR("zoom?z="), zb, true);
  wbPage = WB_SCREEN; svPick = false; svSel = -1;
  if (!svCall()) { svFooter(FS("couldn't zoom:"), wbErr); return; }
  svY = constrain((long)(pos * svPageH), 0L, max(0L, svPageH - SV_H));
  svX = constrain((long)(posX * svPageW), 0L, max(0L, svPageW - SV_W));
  if (svPageW <= SV_W) svSide = false;               // nothing to scroll sideways any more
  tft.fillRect(0, SV_Y0, SV_W, SV_H, C_LCD);
  svRefresh();
  svSettle(3000);
}
void svMenu(int r) {
  if (svSubMenu == 1) {                              // the zoom list
    svSubMenu = 0;
    if (r < 10) svSetZoom(SV_ZOOMS[r]); else svRefresh();
    return;
  }
  switch (r) {
    case 0:                                           // the mouse pointer on/off (repaint whole: the menu covered the page)
      svCur = !svCur; svCurAuto = false; svCurMoved = false; svHover[0] = 0;
      if (svCur) svPick = false;
      svRefresh(); break;
    case 1: svCur = false; svPick = true; svSel = 0; svRefresh(); break;    // repaint first: the menu was drawn over the page
    case 2: svRefresh(); wbAsk = WA_SGO; svTypeId = -1; okBegin(FS("Address or search:"), FS(""), 120, -1, -1, 0, 0); break;
    case 3: svStart(PSTR("back"), nullptr, true); break;
    case 4: svStart(PSTR("reload"), nullptr, true); break;
    case 5: svY = 0; svX = 0; svRefresh(); break;
    case 6: {
      char it[10][24];
      const char* p[10];
      for (int i = 0; i < 10; i++) {
        uint8_t z = SV_ZOOMS[i];
        snprintf_P(it[i], 24, PSTR("%s%u%%%s"), z == svZoom ? FS("> ") : FS("  "), z,
                   z == 87 ? FS("  phone size") : z == 100 ? FS("  1:1") : z <= 50 ? FS("  whole page") : z > 100 ? FS("  big + sideways") : FS(""));
        p[i] = it[i];
      }
      wbMenu = true; svSubMenu = 1;
      pmShow(FS("Zoom (pixel density)"), p, 10, wbAccent());
      break;
    }
    case 7:
      if (svPageW <= SV_W) { svSide = false; svRefresh(); svFooter(FS("this page fits the screen:"), FS("nothing to scroll sideways (zoom in?)")); break; }
      svSide = !svSide; svRefresh(); break;
    case 8:                                           // off -> on, silent -> on + sound (on the piezo) -> off
      if (!svLive) { svLive = true; P.webSound = 0; } else if (!P.webSound) P.webSound = 1; else { svLive = false; P.webSound = 0; }
      prefsSave(); svFps = 0; svFrames = 0; svFpsAt = millis(); svLiveAt = millis(); svRefresh(); break;
    case 9:                                           // color + fast video -> color + sharp video -> black & white
      if (!svColor) { svColor = true; P.svFast = 1; } else if (P.svFast) P.svFast = 0; else svColor = false;
      prefsSave(); svRefresh(); break;
    case 10: {                                        // the same page as text (pictures + links)
      char* u = WK.web.next;
      const size_t n = sizeof WK.web.next;
      snprintf_P(u, n, PSTR("http://%s/read?u="), P.relay);
      size_t o = strlen(u);
      urlEncode(WK.web.url, u + o, n - o);
      wbGo(u);
      break;
    }
    case 11: wbAsk = WA_ASK; askBegin(FS("Ask Claude about this page:"), FS(""), 160, wbAccent()); break;
    default: svRefresh(); break;
  }
}

// ---------- screen share: your PC's screen on the Psion (relay v7) ----------
// Duplicate = the PC's main screen. New screen = a second monitor if Windows has one, else the relay panel's
// "Psion screen" window (a see-through frame on the desktop: what you put inside it shows here, like a small extra
// monitor). One long reply from the relay, frame after frame (only what changed), drawn across the whole width.
// Turn = zoom (fit, 2x, 3x, 4x: follows the PC's mouse), press = menu, hold = back.
const int CS_Y0 = 20, CS_H = 220;
uint8_t csMode = 0, csZoom = 1, csSub = 0;
uint16_t csFps = 0;
char csInfo[40] = "";
uint32_t csRetryAt = 0;
void csTitle() {
  char t[44];
  if (csZoom > 1) snprintf_P(t, sizeof t, PSTR("%s %ux  %u fps"), csMode ? FS("New screen") : FS("Duplicate"), csZoom, csFps);
  else snprintf_P(t, sizeof t, PSTR("%s  %u fps"), csMode ? FS("New screen") : FS("Duplicate"), csFps);
  drawTitleBar(t, wbAccent());
  tft.fillRect(0, 18, W, CS_Y0 - 18, 0);
}
void csStart() {                                     // (re)start: the stream begins on the next tick
  wbPage = WB_CAST; wbMenu = false;
  tft.fillScreen(0);
  csTitle();
  centerText(FS("connecting to your PC..."), 120, 1, C_MID, 0);
  csRetryAt = millis();
  oledDirty = true;
}
bool csStream() {
  char url[120];
  snprintf_P(url, sizeof url, PSTR("http://%s/cast/live?m=%u&z=%u&w=320&h=%d&s=%d"), P.relay, csMode, csZoom, CS_H, P.svFast ? 2 : 1);
  WiFiClient cl;
  HTTPClient http;
  http.useHTTP10(true);
  http.setTimeout(6000);
  const char* keys[1] = {"X-Cast"};
  http.collectHeaders(keys, 1);
  if (!http.begin(cl, url)) return false;
  int code = http.GET();
  if (code != 200) {
    http.end();
    if (code == 404) snprintf_P(wbErr, sizeof wbErr, PSTR("update the relay on the PC (v7)"));
    else if (code == 501) snprintf_P(wbErr, sizeof wbErr, PSTR("the PC needs: pip install pillow"));
    else snprintf_P(wbErr, sizeof wbErr, code > 0 ? PSTR("relay said %d") : PSTR("relay didn't answer"), code);
    return false;
  }
  snprintf_P(csInfo, sizeof csInfo, PSTR("%.39s"), http.header("X-Cast").c_str());
  WiFiClient* st = http.getStreamPtr();
  imgBegin(true, CS_Y0, CS_Y0, CS_Y0 + CS_H);
  imgStream = true; imgHalf = P.svFast; imgFrames = 0;
  rcMutePix = true;                                  // (a PC viewer doesn't need its own screen back)
  uint8_t in[256];
  uint32_t last = millis(), fpsAt = millis();
  uint16_t framesAt = 0;
  bool ok = true;
  while (st && (http.connected() || st->available())) {
    int n = st->available();
    if (n > 0) {
      int r = st->read(in, min(n, (int)sizeof in));
      if (r > 0) { last = millis(); if (imgFeed(in, r) < 0) { ok = false; break; } }
    } else {
      if (millis() - last > 5000) { ok = false; break; }
      delay(1);
    }
    updateButton(); updateSound(); tickClock(); rcService();
    if (encCount || fPress || fShort || fLong || rcPtrClick) break;   // the knob: zoom, menu, back
    uint32_t t = millis();
    if (t - fpsAt >= 1000) { csFps = (uint16_t)((imgFrames - framesAt) * 1000UL / (t - fpsAt)); framesAt = imgFrames; fpsAt = t; csTitle(); }
    yield();
  }
  imgStream = false; imgHalf = false;
  rcMutePix = false;
  http.end();
  return ok || imgFrames > 0;
}
void csTick(uint32_t now, int c, bool sp) {
  if (c) { uint8_t z = constrain((int)csZoom + (c > 0 ? 1 : -1), 1, 4); if (z != csZoom) { csZoom = z; tft.fillRect(0, CS_Y0, W, CS_H, 0); } csTitle(); uiClick(); }
  if (sp) { csShowMenu(); return; }
  if ((int32_t)(now - csRetryAt) < 0) return;
  if (!csStream()) {                                 // say why, try again in a moment
    csRetryAt = millis() + 3000;
    tft.fillRect(10, 100, 300, 44, E_WHITE); tft.drawRect(10, 100, 300, 44, E_CORAL);
    putText(18, 108, 1, E_CORAL, E_WHITE, FS("Couldn't share the screen:"));
    putText(18, 124, 1, C_INK, E_WHITE, wbErr[0] ? wbErr : FS("is the relay running on the PC?"));
  } else csRetryAt = millis();
  oledDirty = true;
}
void csShowMenu() {
  char it[6][28];
  const char* p[6];
  snprintf_P(it[0], 28, PSTR("%sDuplicate my screen"), csMode == 0 ? FS("> ") : FS("  "));
  snprintf_P(it[1], 28, PSTR("%sNew screen (extend)"), csMode == 1 ? FS("> ") : FS("  "));
  if (csZoom > 1) snprintf_P(it[2], 28, PSTR("Zoom: %ux (turn the knob)"), csZoom); else snprintf_P(it[2], 28, PSTR("Zoom: fit (turn the knob)"));
  snprintf_P(it[3], 28, PSTR("Video: %s"), P.svFast ? FS("fast (half res)") : FS("sharp (full res)"));
  snprintf_P(it[4], 28, PSTR("How it works"));
  snprintf_P(it[5], 28, PSTR("Stop sharing"));
  for (int i = 0; i < 6; i++) p[i] = it[i];
  wbMenu = true; csSub = 0;
  pmShow(FS("Screen share"), p, 6, wbAccent());
}
void csMenu(int r) {
  if (csSub) { csSub = 0; csStart(); return; }       // (the help card: any choice goes back to the picture)
  switch (r) {
    case 0: csMode = 0; break;
    case 1: csMode = 1; break;
    case 2: csZoom = csZoom % 4 + 1; break;
    case 3: P.svFast = !P.svFast; prefsSave(); break;
    case 4:
      wbMenu = true; csSub = 1;
      pmShowP(FS("How it works"), PSTR("Duplicate: your PC's screen\nNew screen: a 2nd monitor, or\n the relay panel's Psion window\n (drag things into that frame)\nTurn: zoom in, follows mouse\nPress: menu   Hold: back\nOK"), wbAccent());
      return;
    case 5: wbDrawList(); return;
  }
  csStart();
}

// ---------- AI chat (claude_relay.py on your PC) ----------
void wbChatLog(const char* who, const char* msg) {  // append to the chat log, dropping the oldest text if full
  int need = strlen(who) + strlen(msg) + 4;
  int len = strlen(WK.web.text);
  if (len + need >= WB_TEXT - 600) { int cut = len / 2; const char* nl = strchr(WK.web.text + cut, '\n'); cut = nl ? nl - WK.web.text + 1 : cut; memmove(WK.web.text, WK.web.text + cut, len - cut + 1); }
  len = strlen(WK.web.text);
  snprintf_P(WK.web.text + len, WB_TEXT - len, PSTR("%s%s\n\n"), who, msg);
}
void wbChatDraw() {
  wbTitle(FS("AI chat"));
  if (!WK.web.text[0]) snprintf_P(WK.web.text, WB_TEXT, PSTR("Press the knob and pick 'Type a message'.\n\nThis chat goes through the relay on your PC (%s). Claude can search the web too.\n\n"), P.relay);
  int n = wbLayout(0, false);
  wbLine = max(0, n - 17);
  char st[48]; snprintf_P(st, sizeof st, PSTR("relay %s"), P.relay);
  wbDrawText(st);
}
void wbChatSend(const char* msg, bool aboutPage) {
  wbChatLog(aboutPage ? FS("> (about this page) ") : FS("> "), msg);
  wbChatDraw();
  char* utf = (char*)WK.web.ltxt;                   // link labels aren't used in the chat: borrow them
  cp437ToUtf8(msg, utf, sizeof WK.web.ltxt);
  char* body = WK.web.next;                         // the JSON goes in the shared buffer, not on the stack
  const int n = sizeof WK.web.next;
  int o = snprintf_P(body, n, PSTR("{\"text\":\""));
  for (const char* p = utf; *p && o < n - 20; p++) {
    if (*p == '"' || *p == '\\') body[o++] = '\\';
    body[o++] = *p;
  }
  snprintf_P(body + o, n - o, aboutPage ? PSTR("\",\"page\":true}") : PSTR("\"}"));
  char url[48];
  snprintf_P(url, sizeof url, PSTR("http://%s/chat"), P.relay);
  int len = strlen(WK.web.text);
  memcpy_P(WK.web.text + len, PSTR("Claude: "), 8); wbLen = len + 8; WK.web.text[wbLen] = 0;
  webSink = sinkText;
  webWaitMs = 65000;                                 // web searches can take a while
  int code = webFetch(url, body);
  webWaitMs = 9000;
  if (code != 200) { WK.web.text[len] = 0; char m[64]; snprintf_P(m, sizeof m, PSTR("(relay error: %.40s)"), wbErr[0] ? wbErr : FS("no reply")); wbChatLog(FS(""), m); }
  else { sinkRaw('\n'); sinkRaw('\n'); }
  wbChatDraw();
  SFX(T_HOP);
}

// ---------- currency (open.er-api.com) ----------
char rtCode[4]; int rtCapture = -1; char rtNum[16]; uint8_t rtNumLen = 0, rtRoll[6];
bool sinkRates(char c) {
  if (rtCapture >= 0) {
    if (isdigit((uint8_t)c) || c == '.' || c == 'e' || c == '-' || c == '+') { if (rtNumLen < 15) rtNum[rtNumLen++] = c; return true; }
    rtNum[rtNumLen] = 0; wbRate[rtCapture] = atof(rtNum); rtCapture = -1;
  }
  memmove(rtRoll, rtRoll + 1, 5); rtRoll[5] = c;    // looking for "XXX":
  if (rtRoll[0] == '"' && rtRoll[4] == '"' && rtRoll[5] == ':')
    for (int i = 0; i < 8; i++) { const char* code = pgmItem(WB_CUR_P, i); if (!memcmp(rtRoll + 1, code, 3)) { rtCapture = i; rtNumLen = 0; } }
  return true;
}
void wbMoneyDraw() {
  tft.fillRect(0, 20, W, 194, C_LCD);
  char b[48], v[20];
  const char* base = pgmItem(WB_CUR_P, P.curBase % 8);
  snprintf_P(b, sizeof b, PSTR("%lu %s ="), (unsigned long)wbAmount, base);
  putText(10, 28, 3, wbAccent(), C_LCD, b);
  for (int i = 0, row = 0; i < 8; i++) {
    if (i == P.curBase % 8) continue;
    int y = 64 + row * 18;
    uint16_t bg = (row++ & 1) ? E_ROW2 : E_WHITE;
    tft.fillRect(10, y, 300, 18, bg);
    if (wbRate[i] > 0) { fmtNum(wbAmount * (double)wbRate[i], v, sizeof v, 8); } else strcpy_P(v, PSTR("-"));
    snprintf_P(b, sizeof b, PSTR("%s"), pgmItem(WB_CUR_P, i));
    putText(18, y + 5, 1, C_INK, bg, b);
    putText(300 - (int)strlen(v) * 12, y + 2, 2, C_INK, bg, v);
  }
  putText(6, 204, 1, C_MID, C_LCD, FS("turn: amount   press: change base currency"));
}
void wbMoney() {
  wbTitle(FS("Currency"));
  char url[64];
  snprintf_P(url, sizeof url, PSTR("http://open.er-api.com/v6/latest/%s"), pgmItem(WB_CUR_P, P.curBase % 8));
  for (int i = 0; i < 8; i++) wbRate[i] = 0;
  rtCapture = -1; memset(rtRoll, 0, sizeof rtRoll);
  webSink = sinkRates;
  int code = webFetch(url, nullptr);
  if (code != 200) { wbError(); return; }
  wbMoneyDraw();
}

// ---------- JSON helper for small replies ----------
bool jsonVal(const char* key, char* out, size_t n) {
  char k[24]; snprintf_P(k, sizeof k, PSTR("\"%s\""), key);
  const char* p = strstr(WK.web.text, k);
  if (!p) return false;
  p = strchr(p + strlen(k), ':');
  if (!p) return false;
  p++;
  while (*p == ' ') p++;
  size_t o = 0;
  if (*p == '"') { p++; while (*p && *p != '"' && o < n - 1) out[o++] = *p++; }
  else while (*p && *p != ',' && *p != '}' && o < n - 1) out[o++] = *p++;
  out[o] = 0;
  return true;
}

// ---------- ISS tracker (open-notify) ----------
// 80 x 40 land mask (4.5 degrees per dot), rasterized from Natural Earth 1:110m land (public domain)
const uint8_t WORLD_MAP[400] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xD3, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x3F, 0xFF, 0xF0, 0x1C, 0x00, 0x04, 0x00, 0x00, 0x00, 0x07, 0x7E, 0x1F, 0xE0, 0x00, 0x0C, 0x3F, 0xC1, 0x00,
  0x1F, 0xFF, 0x9F, 0x8F, 0xE0, 0x0F, 0x03, 0xFF, 0xFF, 0xFF, 0x8F, 0xFF, 0xFD, 0xCF, 0x18, 0x3F, 0xFF, 0xFF, 0xFF, 0xFF,
  0x1F, 0xFF, 0xE3, 0x04, 0x00, 0x77, 0xFF, 0xFF, 0xFF, 0xFE, 0x04, 0x1F, 0xF9, 0xE0, 0x01, 0x27, 0xFF, 0xFF, 0xFE, 0x10,
  0x00, 0x0F, 0xFF, 0xF0, 0x01, 0x7F, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x07, 0xFF, 0xD0, 0x01, 0xFF, 0xFF, 0xFF, 0xFE, 0x00,
  0x00, 0x0F, 0xFF, 0x80, 0x03, 0xFD, 0x7F, 0xFF, 0xFD, 0x00, 0x00, 0x07, 0xFE, 0x00, 0x03, 0x13, 0xFF, 0xFF, 0xE9, 0x00,
  0x00, 0x07, 0xFE, 0x00, 0x03, 0xE0, 0xFF, 0xFF, 0xE6, 0x00, 0x00, 0x03, 0xF4, 0x00, 0x03, 0xFF, 0xFF, 0xFF, 0xE0, 0x00,
  0x00, 0x01, 0xE0, 0x00, 0x0F, 0xFF, 0xFF, 0xFF, 0xE0, 0x00, 0x00, 0x00, 0xE8, 0x00, 0x0F, 0xFF, 0xF8, 0xE7, 0x00, 0x00,
  0x00, 0x00, 0x38, 0x00, 0x0F, 0xFF, 0xF0, 0xC7, 0x20, 0x00, 0x00, 0x00, 0x05, 0xC0, 0x07, 0xFF, 0xE0, 0x41, 0x00, 0x00,
  0x00, 0x00, 0x01, 0xF0, 0x07, 0xFF, 0xE0, 0x02, 0x10, 0x00, 0x00, 0x00, 0x01, 0xF8, 0x00, 0x3F, 0xC0, 0x02, 0xC0, 0x00,
  0x00, 0x00, 0x03, 0xFE, 0x00, 0x3F, 0x80, 0x02, 0x4F, 0x00, 0x00, 0x00, 0x03, 0xFF, 0x00, 0x1F, 0x80, 0x00, 0x03, 0x80,
  0x00, 0x00, 0x01, 0xFF, 0x00, 0x1F, 0x80, 0x00, 0x04, 0x00, 0x00, 0x00, 0x01, 0xFF, 0x00, 0x3F, 0xA0, 0x00, 0x1F, 0x00,
  0x00, 0x00, 0x00, 0xFE, 0x00, 0x1F, 0x20, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x00, 0xFC, 0x00, 0x1F, 0x00, 0x00, 0x7F, 0xC0,
  0x00, 0x00, 0x00, 0xF8, 0x00, 0x0E, 0x00, 0x00, 0x7F, 0xC0, 0x00, 0x00, 0x00, 0xF0, 0x00, 0x0C, 0x00, 0x00, 0x33, 0xC0,
  0x00, 0x00, 0x01, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x01, 0x80, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x82,
  0x00, 0x00, 0x01, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x01, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x7F, 0xFF, 0xFF, 0xFF, 0xF8,
  0x00, 0xFF, 0xFF, 0xC0, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0x07, 0xFF, 0xFE, 0x04, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
  0x83, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};
const uint16_t ISS_SEA = RGB565(8, 16, 40), ISS_SEADOT = RGB565(34, 66, 124), ISS_LAND = RGB565(70, 190, 100);
int wbIssLastX = -1, wbIssLastY = -1;
void wbIssCell(int c, int r) {                     // one 4x4 dot of the world map
  if (c < 0 || c >= 80 || r < 0 || r >= 40) return;
  int x = c * 4, y = 20 + r * 4;
  bool land = pgm_read_byte(&WORLD_MAP[r * 10 + c / 8]) & (0x80 >> (c & 7));
  tft.fillRect(x, y, 4, 4, ISS_SEA);
  if (land) tft.fillRect(x, y, 3, 3, ISS_LAND); else tft.drawPixel(x + 1, y + 1, ISS_SEADOT);
}
void wbIssArea(int x0, int y0, int x1, int y1) {    // redraw the dots under a rectangle (erases the old marker)
  for (int r = (y0 - 20) / 4; r <= (y1 - 20) / 4; r++)
    for (int c = x0 / 4; c <= x1 / 4; c++) wbIssCell(c, r);
}
void wbIssMap() { for (int r = 0; r < 40; r++) for (int c = 0; c < 80; c++) wbIssCell(c, r); }
void wbIss(bool first) {
  if (first) { wbTitle(FS("ISS tracker")); wbIssN = 0; wbIssLastX = -1; wbIssMap(); }
  wbLen = 0; WK.web.text[0] = 0; webSink = sinkRaw;
  if (first) {
    if (webFetch("http://api.open-notify.org/astros.json", nullptr) == 200) { char n[8]; if (jsonVal("number", n, sizeof n)) wbIssPeople = atoi(n); }
    wbLen = 0; WK.web.text[0] = 0;
  }
  int code = webFetch("http://api.open-notify.org/iss-now.json", nullptr);
  char la[16] = "", lo[16] = "";
  if (code != 200 || !jsonVal(FS("latitude"), la, sizeof la) || !jsonVal(FS("longitude"), lo, sizeof lo)) { wbError(); return; }
  float lat = atof(la), lon = atof(lo);
  int x = constrain(160 + (int)(lon * 160 / 180), 0, W - 1), y = constrain(100 - (int)(lat * 80 / 90), 20, 179);
  if (wbIssLastX >= 0) wbIssArea(wbIssLastX - 10, wbIssLastY - 10, wbIssLastX + 10, wbIssLastY + 10);
  if (wbIssN == 16) { memmove(wbIssX, wbIssX + 1, 15 * 2); memmove(wbIssY, wbIssY + 1, 15 * 2); wbIssN = 15; }
  wbIssX[wbIssN] = x; wbIssY[wbIssN++] = y;
  for (int i = 0; i + 1 < wbIssN; i++) tft.fillRect(wbIssX[i] - 1, wbIssY[i] - 1, 3, 3, K_ORANGE);   // its trail
  tft.drawCircle(x, y, 8, K_YELLOW); tft.drawCircle(x, y, 9, K_YELLOW);
  tft.fillCircle(x, y, 4, K_YELLOW);
  tft.fillRect(x - 7, y - 1, 15, 3, K_CREAM);                                                // little solar panels
  wbIssLastX = x; wbIssLastY = y;
  tft.fillRect(0, 182, W, 32, C_LCD);
  char b[56];
  snprintf_P(b, sizeof b, PSTR("lat %s   lon %s"), la, lo);       putText(6, 184, 1, C_INK, C_LCD, b);
  snprintf_P(b, sizeof b, PSTR("%d people in space right now"), wbIssPeople); putText(6, 196, 1, C_MID, C_LCD, b);
  putText(6, 208, 1, C_MID, C_LCD, FS("updates every 5 s   hold: back"));
  wbIssAt = millis();
}

// ---------- where am I (ip-api.com) ----------
char wbCity[24] = "";
long wbOffset = 0;
void wbWhere() {
  wbTitle(FS("Where am I?"));
  wbLen = 0; WK.web.text[0] = 0; webSink = sinkText;   // city names come as UTF-8 (Mérida)
  int code = webFetch("http://ip-api.com/json/?fields=status,country,regionName,city,lat,lon,timezone,offset,isp,query", nullptr);
  char v[48];
  if (code != 200 || !jsonVal(FS("city"), wbCity, sizeof wbCity)) { wbError(); return; }
  if (jsonVal(FS("offset"), v, sizeof v)) wbOffset = atol(v);
  tft.fillRect(0, 20, W, 194, C_LCD);
  for (int i = 0; i < 8; i++) {
    int y = 26 + i * 20;
    uint16_t bg = (i & 1) ? E_ROW2 : E_WHITE;
    tft.fillRect(6, y, 308, 20, bg);
    putText(12, y + 6, 1, C_MID, bg, pgmItem(PSTR("city\nregion\ncountry\ntime zone\nlatitude\nlongitude\nprovider\npublic IP"), i));
    if (!jsonVal(pgmItem(PSTR("city\nregionName\ncountry\ntimezone\nlat\nlon\nisp\nquery"), i), v, sizeof v)) v[0] = 0;
    putText(100, y + 6, 1, C_INK, bg, v);
  }
  putText(6, 196, 1, wbAccent(), C_LCD, FS("press: use this city for weather + set time zone"));
  putText(6, 208, 1, C_MID, C_LCD, FS("(IP location is approximate)"));
}

// ---------- list + app ----------
bool wbIsHead(int row) { return WB_ROWS[row] >= 200; }
void wbDrawRow(int s) {
  int i = wbTop + s, y = 22 + s * 22;
  ptrListSet(PL_WEB, 4, 22, 304, 22, 8, wbTop, wbSel);
  uint16_t bg = (s & 1) ? E_ROW2 : E_WHITE, acc = wbAccent();
  if (i < WB_ITEMS && wbIsHead(i)) {                  // section header
    tft.fillRect(4, y, 304, 22, E_HEAD);
    tft.fillRect(4, y + 20, 304, 2, acc);
    putText(10, y + 7, 1, acc, E_HEAD, pgmItem(WB_HEADS_P, WB_ROWS[i] - 200));
    return;
  }
  tft.fillRect(4, y, 304, 22, bg);
  if (i >= WB_ITEMS) return;
  int id = WB_ROWS[i];
  bool sel = i == wbSel;
  putText(14, y + 7, 1, C_INK, bg, pgmItem(WB_NAMES_P, id));
  const char* d = id == 7 ? (P.webCity[0] ? P.webCity : "automatic") : (id == 8 ? P.relay : pgmItem(WB_DESC_P, id));
  char dd[26]; snprintf_P(dd, sizeof dd, PSTR("%.24s"), d);
  putText(316 - (int)strlen(dd) * 6 - 6, y + 7, 1, sel ? acc : C_MID, bg, dd);
  if (sel) { tft.drawRect(4, y, 304, 22, acc); tft.drawRect(5, y + 1, 302, 20, acc); }
}
void wbDrawList() {
  wbPage = WB_LIST; wbMenu = wbLinkMenu = false;
  tft.fillScreen(C_LCD);
  drawTitleBar(FS("Web"), wbAccent());
  tft.drawRect(3, 21, 314, 8 * 22 + 2, E_RULE);
  for (int s = 0; s < 8; s++) wbDrawRow(s);
  bool on = WiFi.status() == WL_CONNECTED;
  putText(6, 200, 1, on ? E_MINT : E_CORAL, C_LCD, on ? FS("online - press to open") : FS("offline - connect in the WiFi app first"));
  drawDock(APP_WEB);
}
void webEnter() {
  WiFi.scanDelete();                                 // scan results from the WiFi app hold heap: give it back
  if (wkOwner != APP_WEB) { WK.web.text[0] = 0; WK.web.url[0] = 0; WK.web.nlinks = 0; svOpen = false; }
  wkOwner = APP_WEB;
  wbDrawList();
}
void wbOpen(int i, bool refresh) {                  // i = item id (see WB_NAMES_P)
  wbMenu = wbLinkMenu = false;
  switch (i) {
    case 0: wbPage = WB_WEATHER; wbWeather(); break;
    case 1: if (refresh || !strstr_P(WK.web.url, PSTR("68k"))) { wbGo("http://68k.news/"); } else { wbPage = WB_PAGE; wbLoadPage(wbChunk); } break;
    case 2:
      wbPage = WB_PAGE;
      if (WK.web.url[0] && strstr(WK.web.url, P.relay) && !refresh && (strstr_P(WK.web.url, PSTR("/read?")) || strstr_P(WK.web.url, PSTR("/search?")))) { wbLoadPage(wbChunk); break; }
      wbAsk = WA_GO; askBegin(FS("Search words or an address:"), FS(""), 60, wbAccent());
      break;
    case 3:                                          // a web page in the shared buffer isn't a chat log
      wbPage = WB_CHAT;
      if (strncmp_P(WK.web.text, PSTR("> "), 2) && !strstr_P(WK.web.text, PSTR("Claude:"))) WK.web.text[0] = 0;
      wbChatDraw();
      break;
    case 4: wbPage = WB_MONEY; wbMoney(); break;
    case 5: wbPage = WB_ISS; wbIss(true); break;
    case 6: wbPage = WB_WHERE; wbWhere(); break;
    case 7: wbAsk = WA_CITY; askBegin(FS("Weather city (blank = automatic):"), P.webCity, 23, wbAccent()); break;
    case 8: wbAsk = WA_RELAY; askBegin(FS("Relay on your PC (IP:port):"), P.relay, 23, wbAccent()); break;
    case 9: { char u[48]; snprintf_P(u, sizeof u, PSTR("http://%s/"), P.relay); wbGo(u); break; }
    case 10:
      if (svOpen) svStart(PSTR("info"), nullptr, true);
      else { wbPage = WB_SCREEN; wbAsk = WA_SGO; askBegin(FS("Address or search words:"), FS(""), 120, wbAccent()); }
      break;
    case 11: csStart(); break;
  }
  oledDirty = true;
}
// ---------- typing on a page: the keys on the OLED, the page stays on the big screen ----------
// 40 keys + 5 action keys on the 128x64 OLED. What you type shows in the page's own field (or along the bottom),
// and the relay's suggestions drop down under it (the site's own list, search ideas, words on the page).
// Hold the knob to jump into the list, turn to the one you want, press to use it; hold again = back to the keys.
bool okOpen = false, okList = false, okSugDirty = false;
uint8_t okKey = 0, okPage = 0, okSugN = 0, okSugSel = 0;
const int OK_SUG = 6, OK_N = 45;
char okSug[OK_SUG][41];
int8_t okSugPick[OK_SUG];
int8_t okPickIdx = -1;                              // the chosen suggestion's place in the page's own list (-1 = type it)
int16_t okFx = -1, okFy = 0, okFw = 0, okFh = 0;    // the field in the picture area; okFx = -1: no field (address bar)
int16_t okDropY = 0, okDropH = 0;                   // the drop-down list on screen (repainted from the relay when it shrinks)
uint32_t okTypedAt = 0;
bool okUsable() { return oledOk && P.oledOn; }
void okBegin(const char* prompt, const char* init, uint8_t maxLen, int fx, int fy, int fw, int fh) {
  if (!okUsable()) { askBegin(prompt, init, maxLen, wbAccent()); return; }   // no OLED: the big keyboard as before
  okOpen = true; okList = false; okKey = 0; okPage = 0; okSugN = 0; okSugSel = 0; okPickIdx = -1;
  okSugDirty = false; okDropH = 0;
  askAcc = wbAccent(); askMax = min((int)maxLen, 160);
  snprintf_P(askPrompt, sizeof askPrompt, PSTR("%s"), prompt);
  snprintf_P(askBuf, sizeof askBuf, PSTR("%s"), init);
  askLen = strlen(askBuf);
  okFx = fx; okFy = fy; okFw = fw; okFh = fh;
  if (okFx >= 0 && (okFy < 0 || okFy + 8 > SV_H || okFw < 12 || okFx >= SV_W)) okFx = -1;   // field off screen: bottom line only
  okDrawField(); okDrawOled();
}
void okClose() { okOpen = false; okList = false; oledDirty = true; }
void okDrawField() {                                // the text in the page's field + the line along the bottom
  uint16_t acc = wbAccent();
  tft.fillRect(0, SV_Y0 + SV_H, W, H - SV_Y0 - SV_H, E_WHITE);
  tft.drawFastHLine(0, SV_Y0 + SV_H, W, acc);
  char b[56];
  int from = askLen > 34 ? askLen - 34 : 0;
  snprintf_P(b, sizeof b, PSTR("%.15s %s"), askPrompt, askBuf + from);
  putText(4, 216, 1, C_INK, E_WHITE, b);
  int cx = 4 + (min((int)strlen(askPrompt), 15) + 1 + askLen - from) * 6;
  if (cx < W - 2) tft.fillRect(cx, 215, 2, 10, acc);
  putText(4, 229, 1, C_MID, E_WHITE, okList ? FS("LIST: turn = pick   press = use it   hold = keys") :
          okSugN ? FS("keys on the OLED   hold: pick from the list") : FS("keys on the OLED: turn + press to type"));
  if (okFx < 0) return;
  int x = okFx, y = SV_Y0 + okFy, w = min((int)okFw, SV_W - x), h = constrain((int)okFh, 10, 24);
  tft.fillRect(x, y, w, h, 0xFFFF);
  tft.drawRect(x, y, w, h, acc);
  int maxc = max(1, (w - 8) / 6);
  int f2 = askLen >= maxc ? askLen - maxc + 1 : 0;
  char t[56]; snprintf_P(t, sizeof t, PSTR("%.*s"), min(maxc, 54), askBuf + f2);
  putText(x + 3, y + (h - 8) / 2, 1, 0x0000, 0xFFFF, t);
  int cx2 = x + 3 + (askLen - f2) * 6;
  if (cx2 < x + w - 2) tft.fillRect(cx2, y + (h - 10) / 2, 2, 10, acc);
}
void okDrawSug() {                                  // the drop-down list under (or over) the field
  const int rowH = 13;
  int n = okSugN, newH = n * rowH;
  int w = okFx >= 0 ? constrain((int)okFw, 190, SV_W) : SV_W - 12;
  int x = okFx >= 0 ? min((int)okFx, SV_W - w) : 6, top;
  if (okFx >= 0) {
    int fh = constrain((int)okFh, 10, 24), below = SV_Y0 + okFy + fh + 1;
    top = below + newH <= SV_Y0 + SV_H ? below : max(SV_Y0, SV_Y0 + okFy - newH - 1);
  } else top = SV_Y0 + SV_H - newH;                 // address bar: right above the bottom line
  if (okDropH > 0 && (okDropY != top || okDropH > newH)) {   // the old list was bigger: the page comes back under it
    int y0 = okDropY - SV_Y0;
    svDraw(max(0, y0), min(SV_H - max(0, y0), (int)okDropH), false);
    if (okFx >= 0) okDrawField();
  }
  okDropY = top; okDropH = newH;
  if (!n) return;
  uint16_t acc = wbAccent();
  for (int i = 0; i < n; i++) {
    int y = top + i * rowH;
    bool sel = okList && i == okSugSel;
    uint16_t bg = sel ? acc : (i & 1 ? E_ROW2 : E_WHITE);
    tft.fillRect(x, y, w, rowH, bg);
    char t[48]; snprintf_P(t, sizeof t, PSTR("%.*s"), min(46, (w - 10) / 6), okSug[i]);
    putText(x + 5, y + 3, 1, sel ? E_ONACC : C_INK, bg, t);
  }
  tft.drawRect(x, top, w, newH, acc);
}
void okDrawOled() {                                 // the keyboard itself, on the little OLED
  if (!okUsable()) return;
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  int from = askLen > 19 ? askLen - 19 : 0;
  oled.setCursor(0, 0);
  for (int i = from; i < askLen; i++) oled.write((uint8_t)askBuf[i]);
  oled.fillRect((askLen - from) * 6, 0, 2, 8, SSD1306_WHITE);
  if (okSugN) {                                     // a little badge: suggestions are waiting
    oled.fillRect(okList ? 98 : 116, 0, okList ? 30 : 12, 9, SSD1306_WHITE);
    oled.setTextColor(SSD1306_BLACK);
    oled.setCursor(okList ? 101 : 119, 1);
    if (okList) oled.print(FS("list")); else oled.print(okSugN);
    oled.setTextColor(SSD1306_WHITE);
  }
  oled.drawFastHLine(0, 9, 128, SSD1306_WHITE);
  for (int i = 0; i < 40; i++) {
    int x = 4 + (i % 10) * 12, y = 11 + (i / 10) * 10;
    bool sel = !okList && i == okKey;
    if (sel) oled.fillRect(x, y, 11, 10, SSD1306_WHITE);
    oled.drawChar(x + 3, y + 1, (uint8_t)KB_PAGES[okPage][i], sel ? SSD1306_BLACK : SSD1306_WHITE, sel ? SSD1306_WHITE : SSD1306_BLACK, 1);
  }
  oled.drawFastHLine(0, 52, 128, SSD1306_WHITE);
  for (int a = 0; a < 5; a++) {
    int x = 1 + a * 25;
    bool sel = !okList && okKey == 40 + a;
    const char* l = a == 0 ? KB_PAGE_NAME[okPage] : pgmItem(PSTR("spc\ndel\nOK\nesc"), a - 1);
    if (sel) oled.fillRect(x, 54, 24, 10, SSD1306_WHITE);
    oled.setTextColor(sel ? SSD1306_BLACK : SSD1306_WHITE);
    oled.setCursor(x + (24 - (int)strlen(l) * 6) / 2, 55);
    oled.print(l);
  }
  oled.display();
}
void okFetchSug() {                                 // ask the relay what fits (the page's own list, search ideas...)
  okSugDirty = false;
  if (!askLen) { okSugN = 0; okDrawSug(); okDrawField(); okDrawOled(); return; }
  char* u = WK.web.next;
  const size_t n = sizeof WK.web.next;
  if (svTypeId >= 0) snprintf_P(u, n, PSTR("http://%s/rb/suggest?id=%d&t="), P.relay, svTypeId);
  else snprintf_P(u, n, PSTR("http://%s/rb/suggest?t="), P.relay);
  size_t o = strlen(u);
  urlEncode(askBuf, u + o, n - o);
  wbLen = 0; WK.web.text[0] = 0; webSink = sinkText;   // (the links list is loaded again when typing is done)
  webQuiet = true;
  int code = webFetch(u, nullptr);
  webQuiet = false;
  okSugN = 0;
  if (code == 200)
    for (const char* p = WK.web.text; *p && okSugN < OK_SUG; ) {
      const char* e = strchr(p, '\n');
      int len = e ? (int)(e - p) : (int)strlen(p), pick = atoi(p);
      const char* sp = (const char*)memchr(p, ' ', len);
      if (sp && len > 2) {
        int tl = min(40, len - (int)(sp + 1 - p));
        memcpy(okSug[okSugN], sp + 1, tl); okSug[okSugN][tl] = 0;
        okSugPick[okSugN] = (int8_t)constrain(pick, -1, 100);
        okSugN++;
      }
      if (!e) break;
      p = e + 1;
    }
  okSugSel = 0;
  if (!okSugN) okList = false;
  okDrawSug(); okDrawField(); okDrawOled();
}
void okChanged() {
  okTypedAt = millis(); okSugDirty = true; okPickIdx = -1;
  okDrawField(); okDrawOled();
}
void okType(char ch) { if (askLen < askMax) { askBuf[askLen++] = ch; askBuf[askLen] = 0; okChanged(); } }
void okHold() {                                     // hold: into the list / back to the keys / (no list) cancel
  if (okList) { okList = false; okDrawSug(); okDrawField(); okDrawOled(); return; }
  if (okSugDirty) okFetchSug();
  if (okSugN) { okList = true; okSugSel = 0; okDrawSug(); okDrawField(); okDrawOled(); SFX(T_HOP); return; }
  okClose(); wbAsk = WA_NONE;
  if (svOpen) svRefresh(); else wbDrawList();
}
int okTick(int c, bool sp) {                        // 0 typing, 1 OK (askBuf + okPickIdx), -1 cancel
  char rc = kbRemotePop();                          // typed on the PC (remote control)
  if (rc) {
    if (rc == '\b') { if (askLen) { askBuf[--askLen] = 0; okChanged(); } }
    else if (rc == '\n') {
      if (okList && okSugN) { snprintf_P(askBuf, sizeof askBuf, PSTR("%s"), okSug[okSugSel]); askLen = strlen(askBuf); okPickIdx = okSugPick[okSugSel]; }
      else okPickIdx = -1;
      okClose(); return 1;
    } else if (rc == 27) { okClose(); return -1; }
    else okType(rc);
    return 0;
  }
  if (okList) {
    if (c && okSugN) { okSugSel = ((okSugSel + c) % okSugN + okSugN) % okSugN; okDrawSug(); uiClick(); }
    if (sp && okSugN) {
      SFX(T_HOP);
      snprintf_P(askBuf, sizeof askBuf, PSTR("%s"), okSug[okSugSel]); askLen = strlen(askBuf);
      okPickIdx = okSugPick[okSugSel];
      okClose(); return 1;
    }
    return 0;
  }
  if (c) { okKey = ((okKey + c) % OK_N + OK_N) % OK_N; okDrawOled(); uiClick(); }
  if (!sp) { if (okSugDirty && !c && millis() - okTypedAt > 450) okFetchSug(); return 0; }
  SFX(T_HOP);
  if (okKey < 40) { okType(KB_PAGES[okPage][okKey]); return 0; }
  switch (okKey - 40) {
    case 0: okPage = (okPage + 1) % 3; okDrawOled(); break;
    case 1: okType(' '); break;
    case 2: if (askLen) { askBuf[--askLen] = 0; okChanged(); } break;
    case 3: okPickIdx = -1; okClose(); return 1;
    default: okClose(); return -1;
  }
  return 0;
}
bool svPtrOk() {                                     // the screen view is showing and nothing is open over it
  return app == APP_WEB && wbPage == WB_SCREEN && svOpen && wbAsk == WA_NONE && !wbMenu && !wbLinkMenu && !okOpen && !pmOpen;
}
void webBack() {
  if (okOpen) { okHold(); return; }
  if (wbPage == WB_CAST && wbMenu && !wbLinkMenu) { wbMenu = false; csSub = 0; pmOpen = false; csStart(); return; }   // hold closes the menu
  if (wbPage == WB_SCREEN && wbMenu && !wbLinkMenu) { wbMenu = false; svSubMenu = 0; pmOpen = false; svRefresh(); return; }   // hold closes the menu
  if (wbPage == WB_SCREEN && svPick && wbAsk == WA_NONE && !wbMenu) { svMark(false); svPick = false; svSel = -1; svHint(); return; }
  if (wbPage == WB_SCREEN && svCur && wbAsk == WA_NONE && !wbMenu) { svCurOff(); return; }   // hold: pointer off
  if (wbAsk != WA_NONE) {
    wbAsk = WA_NONE;
    if (wbPage == WB_SCREEN && svOpen) { svRefresh(); oledDirty = true; return; }   // typing on a page: back to the page
  }
  wbDrawList(); oledDirty = true;
}
void wbAskDone(int r) {
  int what = wbAsk;
  wbAsk = WA_NONE;
  if (r < 0) {
    if (what == WA_CHAT) wbChatDraw();
    else if (wbPage == WB_SCREEN && svOpen) svRefresh();
    else if ((what == WA_SEARCH || what == WA_URL || what == WA_ASK) && WK.web.text[0] && wbPage == WB_PAGE) wbDrawText(FS(""));
    else wbDrawList();
    return;
  }
  char* u = WK.web.next;                             // URLs are built in the shared buffer (the stack is only 4 KB)
  const int n = sizeof WK.web.next;
  switch (what) {
    case WA_GO:                                      // one box: an address if it looks like one, else a search
      if (!strchr(askBuf, ' ') && strchr(askBuf, '.')) what = WA_URL;
      // fall through
    case WA_SEARCH: case WA_URL: {
      int o = what == WA_URL ? snprintf_P(u, n, PSTR("http://%s/read?u=%s"), P.relay, strstr_P(askBuf, PSTR("://")) ? "" : "https%3A%2F%2F")
                             : snprintf_P(u, n, PSTR("http://%s/search?q="), P.relay);
      o = min(o, n - 1);
      urlEncode(askBuf, u + o, n - o);
      wbGo(u);
      break;
    }
    case WA_SGO: svStart(PSTR("open?u="), askBuf, false); break;
    case WA_STYPE: {
      char idb[16];
      snprintf_P(idb, sizeof idb, PSTR("%d&t="), svTypeId);
      svBuild(PSTR("type?id="), idb, true);
      size_t o = strlen(u);
      urlEncode(askBuf, u + o, n - o);
      if (okPickIdx >= 0) { o = strlen(u); snprintf_P(u + o, n - o, PSTR("&pick=%d"), okPickIdx); }   // the site's own list item
      okPickIdx = -1;
      svStartBuilt();
      break;
    }
    case WA_ASK: wbPage = WB_CHAT; WK.web.text[0] = 0; wbChatSend(askBuf, true); break;
    case WA_CHAT: if (askBuf[0]) wbChatSend(askBuf, false); else wbChatDraw(); break;
    case WA_CITY: snprintf_P(P.webCity, sizeof P.webCity, PSTR("%s"), askBuf); prefsSave(); wbDrawList(); break;
    case WA_RELAY: snprintf_P(P.relay, sizeof P.relay, PSTR("%s"), askBuf); prefsSave(); wbDrawList(); break;
  }
}
void NOINLINE wbLinksMenu() {                       // the 12 link labels live only while this runs
  if (!WK.web.nlinks) { wbDrawText(FS("")); return; }
  const char* it[WB_LINKS];
  char lb[WB_LINKS][30];
  for (int i = 0; i < WK.web.nlinks; i++) { snprintf_P(lb[i], 30, PSTR("%d %.21s"), i + 1, WK.web.ltxt[i]); it[i] = lb[i]; }   // 12 x 25 fits pmBuf
  wbLinkMenu = true; pmShow(FS("Open which link?"), it, WK.web.nlinks, wbAccent());
}
void NOINLINE wbPageMenu(int r) {
  switch (r) {
    case 0: wbLinksMenu(); break;
    case 1: wbLoadPage(wbChunk + 1); break;
    case 2: wbLoadPage(max(0, wbChunk - 1)); break;
    case 3: wbAsk = WA_SEARCH; askBegin(FS("Search the web:"), FS(""), 60, wbAccent()); break;
    case 4: wbAsk = WA_URL; askBegin(FS("Go to address (e.g. es.wikipedia.org):"), FS(""), 60, wbAccent()); break;
    case 5: {                                        // same page in the screen view
      const char* u = strstr_P(WK.web.url, PSTR("/read?u="));
      const char* q = strstr_P(WK.web.url, PSTR("/search?q="));
      svOpen = true; svY = 0;
      if (u) svStart(PSTR("open?u="), u + 8, true);
      else if (q) svStart(PSTR("open?u="), q + 10, true);
      else svStart(PSTR("open?u="), WK.web.url, false);
      break;
    }
    case 6: wbAsk = WA_ASK; askBegin(FS("Ask Claude about this page:"), FS(""), 160, wbAccent()); break;
    case 7: wbGo("http://68k.news/"); break;
    case 8: {
      wbPicMode = (wbPicMode + 1) % 4;
      wbLines = wbLayout(0, false);                  // pictures change size: keep the scroll position in range
      wbLine = constrain(wbLine, 0, max(0, wbLines - 17));
      wbDrawText(pgmItem(PSTR("pictures: black & white\npictures: dot matrix\npictures: color\npictures: off"), wbPicMode));
      break;
    }
    default: wbDrawText(FS("")); break;
  }
}
void webTick(uint32_t now) {
  int c = takeClicks() * ENC_DIR;
  takePress();
  bool sp = takeShort();
  if (wbAsk != WA_NONE) { int r = okOpen ? okTick(c, sp) : askTick(c, sp); if (r) wbAskDone(r); return; }
  if (wbMenu || wbLinkMenu) {
    int r = pmTick(c, sp);
    if (r < 0) return;
    bool links = wbLinkMenu;
    wbMenu = wbLinkMenu = false;
    if (wbPage == WB_CAST && !links) { csMenu(r); return; }
    if (wbPage == WB_SCREEN && !links) { svMenu(r); return; }
    if (links) { if (r < WK.web.nlinks) wbOpenLink(r); else wbDrawText(FS("")); return; }
    if (wbPage == WB_CHAT) {
      if (r == 0) { wbAsk = WA_CHAT; askBegin(FS("Message to Claude:"), FS(""), 160, wbAccent()); }
      else if (r == 1) { snprintf_P(WK.web.next, sizeof WK.web.next, PSTR("http://%s/reset"), P.relay); webSink = sinkRaw; wbLen = strlen(WK.web.text); webFetch(WK.web.next, "{}"); WK.web.text[0] = 0; wbChatDraw(); }
      else wbChatDraw();
      return;
    }
    wbPageMenu(r);
    return;
  }
  switch (wbPage) {
    case WB_LIST:
      titleTimeTick();
      if (c) {
        int old = wbSel, oldTop = wbTop;
        for (int k = 0; k < abs(c); k++)
          do { wbSel = (wbSel + (c > 0 ? 1 : -1) + WB_ITEMS) % WB_ITEMS; } while (wbIsHead(wbSel));
        if (wbSel <= wbTop) wbTop = max(0, wbSel - 1); else if (wbSel >= wbTop + 8) wbTop = wbSel - 7;
        if (wbTop != oldTop) for (int s = 0; s < 8; s++) wbDrawRow(s);
        else { wbDrawRow(old - wbTop); wbDrawRow(wbSel - wbTop); }
        uiClick(); oledDirty = true;
      }
      if (sp) wbOpen(WB_ROWS[wbSel], false);
      break;
    case WB_PAGE: case WB_CHAT:
      if (c && wbLines > 17) { wbLine = constrain(wbLine + c * 2, 0, wbLines - 17); wbLayout(wbLine, true); }
      else if (wbPicN && wbPage == WB_PAGE && now - wbLastMove > 350) wbPicLoad();   // knob resting: load a picture
      else if (wbAnimN && wbPage == WB_PAGE && !sp) wbAnimTick(now);                  // ...then keep GIFs moving
      if (sp) {
        if (wbErr[0] && !wbLen && wbPage == WB_PAGE) { wbLoadPage(wbChunk); break; }
        if (wbPage == WB_CHAT) { wbMenu = true; pmShowP(FS("AI chat"), PSTR("Type a message\nNew chat (forget)\nClose menu"), wbAccent()); }
        else { wbMenu = true; pmShowP(FS("Page"), PSTR("Open a link...\nNext part\nPrevious part\nSearch the web...\nGo to address...\nScreen view of this page\nAsk Claude about this page...\nNews front page\nPictures: B&W/dots/color/off\nClose menu"), wbAccent()); }
      }
      break;
    case WB_SCREEN: svTick(now, c, sp); break;
    case WB_CAST: csTick(now, c, sp); break;
    case WB_WEATHER: if (sp) wbWeather(); break;
    case WB_MONEY:
      if (c && !wbErr[0]) { long step = wbAmount >= 1000 ? 100 : (wbAmount >= 100 ? 10 : 1); wbAmount = constrain((long)wbAmount + c * step * (abs(c) > 2 ? 10 : 1), 1L, 9999999L); wbMoneyDraw(); }
      if (sp) { if (!wbErr[0]) P.curBase = (P.curBase + 1) % 8; prefsSave(); wbMoney(); }
      break;
    case WB_ISS: if (now - wbIssAt > 5000 || sp) wbIss(false); break;
    case WB_WHERE:
      if (sp && wbCity[0]) {
        snprintf_P(P.webCity, sizeof P.webCity, PSTR("%s"), wbCity);
        int8_t tz = (int8_t)constrain(wbOffset / 3600, -12L, 14L);
        clkEpoch += (int32_t)(tz - P.tz) * 3600L; P.tz = tz;
        prefsSave(); forceClockRedraw(); drawTitleTime(); SFX(T_HOME);
        putText(6, 196, 1, E_MINT, C_LCD, FS("saved! weather city + time zone updated  "));
      } else if (sp) wbWhere();
      break;
  }
}
void oledWeb() {
  char l[4][24];
  if (wbPage == WB_CAST) {                           // screen share
    snprintf_P(l[0], 24, PSTR("%s"), csMode ? FS("new screen") : FS("duplicate"));
    if (csZoom > 1) snprintf_P(l[1], 24, PSTR("zoom %ux  %u fps"), csZoom, csFps); else snprintf_P(l[1], 24, PSTR("zoom fit  %u fps"), csFps);
    snprintf_P(l[2], 24, PSTR("%.21s"), csInfo[0] ? csInfo : FS("connecting..."));
    snprintf_P(l[3], 24, PSTR("turn:zoom press:menu"));
    oledCard(FS("SCREEN SHARE"), l[0], l[1], l[2], l[3]);
    return;
  }
  snprintf_P(l[0], 24, PSTR("%.21s"), pgmItem(WB_NAMES_P, WB_ROWS[wbSel] < 200 ? WB_ROWS[wbSel] : 0));
  snprintf_P(l[1], 24, PSTR("%s  stack %u"), WiFi.status() == WL_CONNECTED ? FS("online") : FS("offline"), (unsigned)ESP.getFreeContStack());
  if (wbPage == WB_SCREEN && svCur) snprintf_P(l[2], 24, PSTR("%.21s"), svHover[0] ? svHover : FS("mouse pointer on"));
  else snprintf_P(l[2], 24, PSTR("%.21s"), wbErr[0] ? wbErr : (wbPage == WB_PAGE || wbPage == WB_SCREEN ? WK.web.url + 7 : FS("")));
  snprintf_P(l[3], 24, PSTR("RAM %uK  block %uK"), (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getMaxFreeBlockSize() / 1024));
  oledCard(FS("WEB"), l[0], l[1], l[2], l[3]);
}

// ================= NETLAB (WiFi + network debugging) =================
// A bench of little network tests. The list opens a test; hold goes back to the list. Results that
// need memory (scan lists, graphs) live in the shared work area, so each test starts fresh.
enum { NL_INFO, NL_SIGNAL, NL_CHANNELS, NL_PINGGW, NL_PINGNET, NL_DNS, NL_INTERNET, NL_SPEED, NL_PORTS, NL_LAN,
       NL_NTP, NL_RADIO, NL_EVENTS, NL_RECONNECT, NL_N };
const char NL_NAMES_P[] PROGMEM = "Link info\nSignal meter\nChannel scan\nPing the router\nPing the internet\nDNS lookup\n"
                                  "Internet check\nSpeed test\nPort check\nWho's on my WiFi?\nNTP time\nRadio settings\nEvent log\nReconnect test";
const char NL_DESC_P[] PROGMEM = "SSID, IP, DNS, MAC...\nlive dBm graph + beeps\nwho uses which channel\n20 pings: loss + jitter\n"
                                 "1.1.1.1, 8.8.8.8, google\nhow fast names resolve\ncaptive portal, public IP\ndownload speed, 2 ways\n"
                                 "router + relay ports\nLAN scan by ARP\nnetwork clock + offset\nTX power, sleep, 802.11\nconnects, drops + why\nhow fast WiFi comes back";
const char NL_HINT_P[] PROGMEM = "updates every second\npress: beeps on/off\nturn: scroll   press: scan again\npress: ping again\n"
                                 "press: next target\npress: run again\npress: run again\npress: run again\npress: run again\n"
                                 "turn: scroll   press: scan again\npress: set the clock from it\nturn: pick   press: change\n"
                                 "updates live\npress: reconnect now";
int nlSel = 0, nlTop = 0, nlTest = -1, nlStep = 0, nlScroll = 0, nlTarget = 0, nlRow = 0;
uint32_t nlT0 = 0, nlNext = 0;
bool nlBeep = false, nlBusy = false;
float nlTxPower = 20.5f;
// connection history, kept for the whole session (not in the shared work area)
char nlEv[8][34];
uint8_t nlEvN = 0, nlLastReason = 0;
uint16_t nlDrops = 0;
uint32_t nlConnAt = 0;
WiFiEventHandler nlH1, nlH2, nlH3;
// ICMP ping through the SDK
struct ping_option nlPingOpt;
volatile int16_t nlPingMs = -2;                     // -2 waiting, -1 timed out, else ms

uint16_t nlAccent() { return appAccent(APP_NETLAB); }
void nlReasonText(int r, char* o, size_t n) {       // own buffer: this also runs from the WiFi event task
  PGM_P t = nullptr;
  switch (r) {
    case 2: t = PSTR("auth expired"); break;
    case 3: t = PSTR("router kicked us"); break;
    case 4: t = PSTR("idle too long"); break;
    case 8: t = PSTR("we left"); break;
    case 15: case 204: t = PSTR("handshake failed (password?)"); break;
    case 200: t = PSTR("lost the router's beacon"); break;
    case 201: t = PSTR("network not found"); break;
    case 202: t = PSTR("wrong password"); break;
    case 203: t = PSTR("router refused us"); break;
  }
  if (t) { strncpy_P(o, t, n - 1); o[n - 1] = 0; } else snprintf_P(o, n, PSTR("reason %d"), r);
}
const char* nlReason(int r) { char* o = ringBuf(); nlReasonText(r, o, 64); return o; }
void nlAddEv(const char* s) {
  if (nlEvN == 8) { memmove(nlEv[0], nlEv[1], sizeof nlEv[0] * 7); nlEvN = 7; }
  uint32_t t = millis() / 1000;
  snprintf_P(nlEv[nlEvN++], sizeof nlEv[0], PSTR("%02lu:%02lu %.26s"), (unsigned long)(t / 60 % 100), (unsigned long)(t % 60), s);
}
void nlInitEvents() {                               // called from setup(): log every connect / drop
  nlH1 = WiFi.onStationModeConnected([](const WiFiEventStationModeConnected& e) {
    char b[34]; snprintf_P(b, sizeof b, PSTR("joined %.12s ch%d"), e.ssid.c_str(), e.channel); nlAddEv(b);
  });
  nlH2 = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected& e) {
    if (nlConnAt) { nlDrops++; nlLastReason = e.reason; char why[28], b[34]; nlReasonText(e.reason, why, sizeof why); snprintf_P(b, sizeof b, PSTR("drop: %s"), why); nlAddEv(b); }
    nlConnAt = 0;
  });
  nlH3 = WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP& e) {
    nlConnAt = millis(); char b[34]; snprintf_P(b, sizeof b, PSTR("got IP %s"), e.ip.toString().c_str()); nlAddEv(b);
  });
}
void nlPingRecv(void*, void* resp) {
  struct ping_resp* r = (struct ping_resp*)resp;
  nlPingMs = r->ping_err == -1 ? -1 : (int16_t)min((uint32_t)30000, (uint32_t)r->resp_time);
}
void nlPingSent(void*, void*) {}
void nlPingStart(IPAddress ip) {
  memset(&nlPingOpt, 0, sizeof nlPingOpt);
  nlPingOpt.count = 1;
  nlPingOpt.coarse_time = 1;
  nlPingOpt.ip = (uint32_t)ip;
  nlPingMs = -2;
  ping_regist_recv(&nlPingOpt, nlPingRecv);
  ping_regist_sent(&nlPingOpt, nlPingSent);
  ping_start(&nlPingOpt);
}
int nlQuality(int rssi) { return constrain(2 * (rssi + 100), 0, 100); }
const char* nlQualityWord(int rssi) {
  return rssi >= -55 ? FS("excellent") : rssi >= -67 ? FS("good") : rssi >= -75 ? FS("okay") : rssi >= -85 ? FS("weak") : FS("very weak");
}

// ---------- list ----------
void nlDrawRow(int s) {
  int i = nlTop + s, y = 22 + s * 22;
  ptrListSet(PL_NETLAB, 4, 22, 304, 22, 8, nlTop, nlSel);
  uint16_t bg = (s & 1) ? E_ROW2 : E_WHITE, acc = nlAccent();
  tft.fillRect(4, y, 304, 22, bg);
  if (i >= NL_N) return;
  bool sel = i == nlSel;
  char b[4]; snprintf_P(b, sizeof b, PSTR("%d"), (i + 1) % 100);
  tft.fillRoundRect(8, y + 3, 16, 16, 3, acc);
  putText(16 - (int)strlen(b) * 3, y + 7, 1, E_ONACC, acc, b);
  putText(32, y + 7, 1, C_INK, bg, pgmItem(NL_NAMES_P, i));
  putText(146, y + 7, 1, sel ? acc : C_MID, bg, pgmItem(NL_DESC_P, i));
  if (sel) { tft.drawRect(4, y, 304, 22, acc); tft.drawRect(5, y + 1, 302, 20, acc); }
}
void nlDrawList() {
  nlTest = -1;
  tft.fillScreen(C_LCD);
  drawTitleBar(FS("NetLab"), nlAccent());
  tft.drawRect(3, 21, 314, 8 * 22 + 2, E_RULE);
  for (int s = 0; s < 8; s++) nlDrawRow(s);
  int h = 8 * 22, th = h * 8 / NL_N, ty = 22 + (h - th) * nlTop / (NL_N - 8);
  tft.fillRect(308, 22, 8, h, E_ROW2);
  tft.fillRect(310, ty + 1, 4, th - 2, nlAccent());
  bool on = WiFi.status() == WL_CONNECTED;
  char b[52];
  if (on) snprintf_P(b, sizeof b, PSTR("%.16s  %d dBm  %s"), WiFi.SSID().c_str(), WiFi.RSSI(), WiFi.localIP().toString().c_str());
  else snprintf_P(b, sizeof b, PSTR("not connected - most tests need WiFi"));
  putText(6, 200, 1, on ? C_MID : E_CORAL, C_LCD, b);
  drawDock(APP_NETLAB);
}

// ---------- shared drawing ----------
void nlFrame() {                                    // title + empty page + hint line
  tft.fillScreen(C_LCD);
  char t[32]; snprintf_P(t, sizeof t, PSTR("NetLab: %s"), pgmItem(NL_NAMES_P, nlTest));
  drawTitleBar(t, nlAccent());
  putText(6, 226, 1, C_MID, C_LCD, pgmItem(NL_HINT_P, nlTest));
  putText(250, 226, 1, C_MID, C_LCD, FS("hold: back"));
}
void nlRowKV(int r, const char* k, const char* v, uint16_t vc) {   // a "label  value" ledger row
  int y = 24 + r * 14;
  uint16_t bg = (r & 1) ? E_ROW2 : E_WHITE;
  tft.fillRect(4, y, 312, 14, bg);
  putText(8, y + 3, 1, C_MID, bg, k);
  putText(92, y + 3, 1, vc, bg, v);
}
void nlStatus(const char* s, uint16_t col) { tft.fillRect(0, 210, W, 12, C_LCD); putText(6, 212, 1, col, C_LCD, s); }
bool nlNeedWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  centerText(FS("not connected to WiFi"), 100, 1, E_CORAL, C_LCD);
  centerText(FS("connect in the WiFi app first"), 116, 1, C_MID, C_LCD);
  return false;
}
void nlBar(int x, int y, int w, int h, int pct, uint16_t col) {
  tft.drawRect(x, y, w, h, E_RULE);
  int f = (w - 2) * constrain(pct, 0, 100) / 100;
  tft.fillRect(x + 1, y + 1, f, h - 2, col);
  tft.fillRect(x + 1 + f, y + 1, w - 2 - f, h - 2, E_WHITE);
}

// ---------- 0: link info ----------
void nlInfoDraw() {
  char v[48];
  bool on = WiFi.status() == WL_CONNECTED;
  uint16_t acc = nlAccent();
  int r = 0, rssi = WiFi.RSSI();
  snprintf_P(v, sizeof v, PSTR("%.30s"), on ? WiFi.SSID().c_str() : FS("(not connected)"));      nlRowKV(r++, FS("network"), v, on ? C_INK : E_CORAL);
  snprintf_P(v, sizeof v, PSTR("%s  ch %d"), WiFi.BSSIDstr().c_str(), WiFi.channel());    nlRowKV(r++, FS("router"), v, C_INK);
  snprintf_P(v, sizeof v, PSTR("%d dBm  %s (%d%%)"), rssi, nlQualityWord(rssi), nlQuality(rssi)); nlRowKV(r++, FS("signal"), v, acc);
  int pm = WiFi.getPhyMode();
  snprintf_P(v, sizeof v, PSTR("802.11%c  TX %.1f dBm"), pm == WIFI_PHY_MODE_11B ? 'b' : pm == WIFI_PHY_MODE_11G ? 'g' : 'n', nlTxPower); nlRowKV(r++, FS("radio"), v, C_INK);
  snprintf_P(v, sizeof v, PSTR("%s / %s"), WiFi.localIP().toString().c_str(), WiFi.subnetMask().toString().c_str()); nlRowKV(r++, FS("IP / mask"), v, C_INK);
  snprintf_P(v, sizeof v, PSTR("%s"), WiFi.gatewayIP().toString().c_str());               nlRowKV(r++, FS("gateway"), v, C_INK);
  snprintf_P(v, sizeof v, PSTR("%s  %s"), WiFi.dnsIP(0).toString().c_str(), WiFi.dnsIP(1).toString().c_str()); nlRowKV(r++, FS("DNS"), v, C_INK);
  snprintf_P(v, sizeof v, PSTR("%s"), WiFi.macAddress().c_str());                          nlRowKV(r++, FS("my MAC"), v, C_INK);
  snprintf_P(v, sizeof v, PSTR("%s"), WiFi.hostname().c_str());                             nlRowKV(r++, FS("hostname"), v, C_INK);
  uint32_t up = nlConnAt ? (millis() - nlConnAt) / 1000 : 0;
  snprintf_P(v, sizeof v, PSTR("%lum %02lus   drops: %u"), (unsigned long)(up / 60), (unsigned long)(up % 60), nlDrops); nlRowKV(r++, FS("connected"), v, C_INK);
  snprintf_P(v, sizeof v, PSTR("%s"), nlLastReason ? nlReason(nlLastReason) : FS("none yet"));  nlRowKV(r++, FS("last drop"), v, C_INK);
  snprintf_P(v, sizeof v, PSTR("%s"), pgmItem(PSTR("none (fastest)\nlight\nmodem (saves power)"), constrain((int)WiFi.getSleepMode(), 0, 2)));      nlRowKV(r++, FS("sleep mode"), v, C_INK);
  snprintf_P(v, sizeof v, PSTR("%u KB free, %s"), (unsigned)(ESP.getFreeHeap() / 1024), WiFi.getAutoReconnect() ? FS("auto-reconnect on") : FS("auto-reconnect off"));
  nlRowKV(r++, FS("device"), v, C_INK);
}

// ---------- 1: signal meter ----------
void nlSignalTick(uint32_t now) {
  if (now < nlNext) return;
  nlNext = now + 250;
  int rssi = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : -100;
  int8_t* h = WK.nl.hist;
  memmove(h, h + 1, 149);
  h[149] = (int8_t)constrain(rssi, -100, -20);
  int n = min(150, (int)WK.nl.n + 1); WK.nl.n = n;
  int mn = 0, mx = -100; long sum = 0;
  for (int i = 150 - n; i < 150; i++) { mn = min(mn, (int)h[i]); mx = max(mx, (int)h[i]); sum += h[i]; }
  if (n == 1) mn = mx = rssi;
  uint16_t acc = nlAccent();
  char b[40];
  tft.fillRect(0, 24, W, 44, C_LCD);
  snprintf_P(b, sizeof b, PSTR("%d dBm"), rssi);
  putText(10, 26, 4, acc, C_LCD, b);
  snprintf_P(b, sizeof b, PSTR("%s  %d%%"), nlQualityWord(rssi), nlQuality(rssi));
  putText(200, 30, 1, C_INK, C_LCD, b);
  snprintf_P(b, sizeof b, PSTR("min %d  max %d  avg %ld"), mn, mx, sum / n);
  putText(200, 44, 1, C_MID, C_LCD, b);
  putText(200, 56, 1, nlBeep ? acc : C_MID, C_LCD, nlBeep ? FS("beeps: on") : FS("beeps: off"));
  nlBar(10, 70, 300, 12, nlQuality(rssi), acc);
  // graph: -100 dBm (bottom) .. -20 dBm (top), two pixels per sample
  const int gx = 10, gy = 88, gh = 112;
  tft.fillRect(gx, gy, 300, gh, E_WHITE);
  for (int d = -90; d <= -30; d += 20) { int y = gy + gh - (d + 100) * gh / 80; tft.drawFastHLine(gx, y, 300, E_ROW2); char l[6]; snprintf_P(l, sizeof l, PSTR("%d"), d); putText(gx + 2, y - 8, 1, C_MID, E_WHITE, l); }
  for (int i = 150 - n + 1; i < 150; i++) {
    int x0 = gx + (i - 1) * 2, x1 = gx + i * 2;
    tft.drawLine(x0, gy + gh - (h[i - 1] + 100) * gh / 80, x1, gy + gh - (h[i] + 100) * gh / 80, acc);
  }
  tft.drawRect(gx, gy, 300, gh, E_RULE);
  if (nlBeep && WiFi.status() == WL_CONNECTED) { soundOn(400 + nlQuality(rssi) * 16); delay(6); soundOff(); }   // higher = stronger
}

// ---------- 2: channel scan ----------
void nlScanStart() { WiFi.scanDelete(); WiFi.scanNetworks(true, true); nlBusy = true; nlScroll = 0; nlStatus("scanning... (a few seconds)", C_MID); }
void nlScanDraw() {
  uint16_t acc = nlAccent();
  int n = WK.nl.n;
  int cnt[14] = {0}, best[14];
  for (int c = 0; c < 14; c++) best[c] = -100;
  for (int i = 0; i < n; i++) { int c = constrain((int)WK.nl.ch[i], 1, 13); cnt[c]++; best[c] = max(best[c], (int)WK.nl.rssi[i]); }
  // chart: one bar per channel, height = how many networks, colour = strongest one
  tft.fillRect(0, 22, W, 78, C_LCD);
  int mx = 1; for (int c = 1; c <= 13; c++) mx = max(mx, cnt[c]);
  for (int c = 1; c <= 13; c++) {
    int x = 8 + (c - 1) * 23, bh = cnt[c] * 54 / mx;
    tft.fillRect(x, 80 - bh, 19, bh, c == WiFi.channel() && WiFi.status() == WL_CONNECTED ? E_CORAL : acc);
    char l[4]; snprintf_P(l, sizeof l, PSTR("%d"), c);
    putText(x + 10 - (int)strlen(l) * 3, 84, 1, C_INK, C_LCD, l);
    if (cnt[c]) { snprintf_P(l, sizeof l, PSTR("%d"), cnt[c]); putText(x + 10 - (int)strlen(l) * 3, 70 - bh, 1, C_MID, C_LCD, l); }
  }
  // best of the non-overlapping channels 1 / 6 / 11: least signal energy within +-4 channels
  double load[3] = {0, 0, 0};
  const int cands[3] = {1, 6, 11};
  for (int i = 0; i < n; i++) for (int k = 0; k < 3; k++) if (abs((int)WK.nl.ch[i] - cands[k]) <= 4) load[k] += pow(10.0, WK.nl.rssi[i] / 10.0);
  int bk = 0; for (int k = 1; k < 3; k++) if (load[k] < load[bk]) bk = k;
  char b[52];
  snprintf_P(b, sizeof b, PSTR("%d networks. Calmest channel for a router: %d"), n, cands[bk]);
  tft.fillRect(0, 96, W, 12, C_LCD);
  putText(6, 97, 1, C_INK, C_LCD, b);
  for (int r = 0; r < 7; r++) {
    int i = nlScroll + r, y = 110 + r * 14;
    uint16_t bg = (r & 1) ? E_ROW2 : E_WHITE;
    tft.fillRect(4, y, 312, 14, bg);
    if (i >= n) continue;
    const char* enc = WK.nl.enc[i] == ENC_TYPE_NONE ? FS("open") : WK.nl.enc[i] == ENC_TYPE_WEP ? FS("WEP!") : WK.nl.enc[i] == ENC_TYPE_TKIP ? FS("WPA") : FS("WPA2");
    snprintf_P(b, sizeof b, PSTR("%-20s ch%-2d %4d dBm %s"), WK.nl.ssid[i][0] ? WK.nl.ssid[i] : FS("(hidden)"), WK.nl.ch[i], WK.nl.rssi[i], enc);
    putText(8, y + 3, 1, C_INK, bg, b);
    nlBar(270, y + 3, 40, 8, nlQuality(WK.nl.rssi[i]), acc);
  }
}
void nlScanCollect() {
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_FAILED) { nlBusy = false; nlStatus(FS("scan failed - press to try again"), E_CORAL); return; }
  if (n < 0) return;
  nlBusy = false;
  n = min(n, 30);
  // strongest first
  int order[30];
  for (int i = 0; i < n; i++) order[i] = i;
  for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) if (WiFi.RSSI(order[j]) > WiFi.RSSI(order[i])) { int t = order[i]; order[i] = order[j]; order[j] = t; }
  for (int k = 0; k < n; k++) {
    int i = order[k];
    snprintf_P(WK.nl.ssid[k], sizeof WK.nl.ssid[k], PSTR("%.20s"), WiFi.SSID(i).c_str());
    WK.nl.rssi[k] = (int8_t)WiFi.RSSI(i); WK.nl.ch[k] = WiFi.channel(i); WK.nl.enc[k] = WiFi.encryptionType(i);
  }
  WK.nl.n = n;
  WiFi.scanDelete();
  nlScanDraw();
  nlStatus(FS("done"), C_MID);
}

// ---------- 3 + 4: ping ----------
const char NL_PING_P[] PROGMEM = "1.1.1.1 (Cloudflare)\n8.8.8.8 (Google DNS)\ngoogle.com";
void nlPingBegin() {
  WK.nl.n = 0; nlStep = 0; nlT0 = 0;
  tft.fillRect(0, 22, W, 186, C_LCD);
  IPAddress ip;
  char b[48];
  if (nlTest == NL_PINGGW) { ip = WiFi.gatewayIP(); snprintf_P(b, sizeof b, PSTR("pinging your router %s"), ip.toString().c_str()); }
  else snprintf_P(b, sizeof b, PSTR("pinging %s"), pgmItem(NL_PING_P, nlTarget));
  putText(8, 26, 1, C_INK, C_LCD, b);
  nlBusy = true; nlNext = 0;
}
IPAddress nlPingIp() {
  if (nlTest == NL_PINGGW) return WiFi.gatewayIP();
  if (nlTarget == 0) return IPAddress(1, 1, 1, 1);
  if (nlTarget == 1) return IPAddress(8, 8, 8, 8);
  IPAddress ip; if (!WiFi.hostByName(FS("google.com"), ip, 3000)) ip = IPAddress(0, 0, 0, 0);
  return ip;
}
void nlPingDraw() {
  uint16_t acc = nlAccent();
  int n = WK.nl.n, ok = 0, mn = 30000, mx = 0; long sum = 0, jit = 0; int prev = -1, jn = 0;
  for (int i = 0; i < n; i++) {
    int v = WK.nl.ms[i];
    if (v < 0) { prev = -1; continue; }
    ok++; sum += v; mn = min(mn, v); mx = max(mx, v);
    if (prev >= 0) { jit += abs(v - prev); jn++; }
    prev = v;
  }
  // bars: 20 pings, height = time (log-ish scale up to 500 ms), red = lost
  const int gx = 10, gy = 42, gh = 100;
  tft.fillRect(gx, gy, 300, gh, E_WHITE);
  tft.drawRect(gx, gy, 300, gh, E_RULE);
  for (int i = 0; i < n; i++) {
    int x = gx + 4 + i * 14, v = WK.nl.ms[i];
    if (v < 0) { tft.fillRect(x, gy + gh - 20, 10, 18, E_CORAL); continue; }
    int bh = constrain((int)(sqrt((double)v) * (gh - 6) / sqrt(500.0)), 2, gh - 4);
    tft.fillRect(x, gy + gh - 2 - bh, 10, bh, acc);
  }
  char b[56];
  tft.fillRect(0, 146, W, 60, C_LCD);
  snprintf_P(b, sizeof b, PSTR("sent %d   received %d   lost %d%%"), n, ok, n ? (n - ok) * 100 / n : 0);
  putText(8, 150, 1, C_INK, C_LCD, b);
  if (ok) { snprintf_P(b, sizeof b, PSTR("min %d  avg %ld  max %d ms   jitter %ld ms"), mn, sum / ok, mx, jn ? jit / jn : 0L); putText(8, 164, 1, C_INK, C_LCD, b); }
  if (n >= 20) {
    int loss = (n - ok) * 100 / n;
    long avg = ok ? sum / ok : 999;
    const char* verdict = loss > 10 ? FS("lots of loss: interference or a weak signal") : avg > 150 ? FS("slow answers: busy network or far away") :
                          (jn && jit / jn > 30) ? FS("jumpy times: calls and games may stutter") : FS("looks healthy");
    putText(8, 182, 1, loss > 10 ? E_CORAL : acc, C_LCD, verdict);
  }
}
void nlPingTick(uint32_t now) {
  if (!nlBusy) return;
  if (nlStep == 1) {                                  // waiting for the answer
    if (nlPingMs == -2 && now - nlT0 < 2500) return;
    WK.nl.ms[WK.nl.n++] = nlPingMs == -2 ? -1 : nlPingMs;
    nlStep = 0; nlNext = now + 300;
    nlPingDraw();
    if (WK.nl.n >= 20) { nlBusy = false; SFX(T_HOP); }
    return;
  }
  if (now < nlNext) return;
  IPAddress ip = nlPingIp();
  if (!(uint32_t)ip) { nlStatus(FS("couldn't look up the name"), E_CORAL); nlBusy = false; return; }
  nlPingStart(ip);
  nlT0 = now; nlStep = 1;
}

// ---------- 5: DNS ----------
const char NL_DNS_P[] PROGMEM = "google.com\nwikipedia.org\ngithub.com\ncloudflare.com\napi.open-meteo.com\npool.ntp.org\nexample.com\nthis.name.does.not.exist";
void nlDnsRun() {
  tft.fillRect(0, 22, W, 186, C_LCD);
  char v[48];
  snprintf_P(v, sizeof v, PSTR("asking %s"), WiFi.dnsIP(0).toString().c_str());
  nlRowKV(0, FS("DNS server"), v, C_INK);
  for (int i = 0; i < 8; i++) {
    const char* host = pgmItem(NL_DNS_P, i);
    char h[28]; snprintf_P(h, sizeof h, PSTR("%s"), host);
    IPAddress ip;
    uint32_t t = millis();
    bool ok = WiFi.hostByName(h, ip, 4000);
    t = millis() - t;
    if (ok) snprintf_P(v, sizeof v, PSTR("%-15s %4lu ms%s"), ip.toString().c_str(), (unsigned long)t, t < 3 ? FS(" (cached)") : FS(""));
    else snprintf_P(v, sizeof v, PSTR("not found (%lu ms)%s"), (unsigned long)t, i == 7 ? FS(" - correct!") : FS(""));
    char k[16]; snprintf_P(k, sizeof k, PSTR("%.14s"), h);
    nlRowKV(1 + i, k, v, ok || i == 7 ? C_INK : E_CORAL);
    yield();
  }
}

// ---------- 6: internet check ----------
void nlInternetRun() {
  tft.fillRect(0, 22, W, 186, C_LCD);
  int r = 0;
  char v[52];
  IPAddress gw = WiFi.gatewayIP();
  nlPingStart(gw);
  uint32_t t = millis();
  while (nlPingMs == -2 && millis() - t < 2500) { delay(5); }
  if (nlPingMs >= 0) snprintf_P(v, sizeof v, PSTR("answers in %d ms"), nlPingMs); else snprintf_P(v, sizeof v, PSTR("no answer to ping (may be blocked)"));
  nlRowKV(r++, FS("router"), v, nlPingMs >= 0 ? C_INK : E_AMBER);
  IPAddress ip;
  bool dns = WiFi.hostByName(FS("connectivitycheck.gstatic.com"), ip, 4000);
  nlRowKV(r++, FS("DNS"), dns ? FS("working") : FS("NOT working - names won't resolve"), dns ? C_INK : E_CORAL);
  wbLen = 0; WK.web.text[0] = 0; webSink = sinkRaw;
  int code = webFetch(FS("http://connectivitycheck.gstatic.com/generate_204"), nullptr);
  if (code == 204) snprintf_P(v, sizeof v, PSTR("yes - the internet works"));
  else if (code > 0) snprintf_P(v, sizeof v, PSTR("captive portal? got %d instead of 204"), code);
  else snprintf_P(v, sizeof v, PSTR("no: %.36s"), wbErr);
  nlRowKV(r++, FS("online"), v, code == 204 ? nlAccent() : E_CORAL);
  wbLen = 0; WK.web.text[0] = 0; webSink = sinkText;
  code = webFetch(FS("http://ip-api.com/json/?fields=query,isp,org,as,city,country"), nullptr);
  char a[40];
  if (code == 200) {
    if (jsonVal(FS("query"), a, sizeof a)) nlRowKV(r++, FS("public IP"), a, C_INK);
    if (jsonVal(FS("isp"), a, sizeof a)) nlRowKV(r++, FS("provider"), a, C_INK);
    if (jsonVal(FS("as"), a, sizeof a)) { a[34] = 0; nlRowKV(r++, FS("network"), a, C_INK); }
    char c1[24] = "", c2[20] = "";
    jsonVal(FS("city"), c1, sizeof c1); jsonVal(FS("country"), c2, sizeof c2);
    snprintf_P(v, sizeof v, PSTR("%s, %s"), c1, c2);
    nlRowKV(r++, FS("seen from"), v, C_INK);
  } else nlRowKV(r++, FS("public IP"), FS("couldn't ask ip-api.com"), E_AMBER);
  snprintf_P(v, sizeof v, PSTR("%s"), WiFi.localIP().toString().c_str());
  nlRowKV(r++, FS("local IP"), v, C_INK);
  bool natted = WiFi.localIP()[0] == 192 || WiFi.localIP()[0] == 10 || WiFi.localIP()[0] == 172;
  nlRowKV(r++, FS("behind NAT"), natted ? FS("yes (normal at home)") : FS("no / unusual"), C_INK);
}

// ---------- 7: speed test ----------
bool nlSpeedOne(const char* url, int row, const char* label) {   // download, live progress bar, KB/s
  HTTPClient http;
  WiFiClient cl;
  char v[52];
  nlRowKV(row, label, FS("connecting..."), C_MID);
  if (!http.begin(cl, url)) { nlRowKV(row, label, FS("bad address"), E_CORAL); return false; }
  http.setTimeout(8000);
  int code = http.GET();
  if (code != 200) { snprintf_P(v, sizeof v, PSTR("failed: %s"), code > 0 ? String(code).c_str() : http.errorToString(code).c_str()); nlRowKV(row, label, v, E_CORAL); http.end(); return false; }
  int total = http.getSize(), got = 0;
  WiFiClient* s = http.getStreamPtr();               // (null if the reply was empty and the server already closed)
  uint8_t* buf = (uint8_t*)WK.web.text;              // 2.6 KB scratch: bigger reads, nothing on the stack
  uint32_t t0 = millis(), last = t0, shown = 0;
  while (s && http.connected() && (total < 0 || got < total) && millis() - last < 6000 && millis() - t0 < 20000) {
    int n = s->available();
    if (n) { got += s->read(buf, min(n, (int)sizeof WK.web.text)); last = millis(); }
    else delay(1);
    if (millis() - shown > 150) {
      shown = millis();
      nlBar(92, 26 + row * 14, 140, 9, total > 0 ? (int)((long)got * 100 / total) : 50, nlAccent());
      updateButton(); updateSound();
      if (fLong) { fLong = false; break; }
    }
    yield();
  }
  http.end();
  float sec = max(1UL, millis() - t0) / 1000.0f;
  float kbs = got / 1024.0f / sec;
  snprintf_P(v, sizeof v, PSTR("%.0f KB/s = %.2f Mbit/s (%d KB)"), kbs, kbs * 0.008192f, got / 1024);
  nlRowKV(row, label, v, nlAccent());
  return true;
}
void nlSpeedRun() {
  tft.fillRect(0, 22, W, 186, C_LCD);
  nlRowKV(0, FS("test"), FS("downloading for real - takes ~10 s"), C_MID);
  nlSpeedOne(FS("http://speed.cloudflare.com/__down?bytes=1000000"), 1, "internet");
  char url[64];
  snprintf_P(url, sizeof url, PSTR("http://%s/speed?kb=512"), P.relay);
  nlSpeedOne(url, 2, FS("your PC"));
  nlRowKV(4, FS("note"), FS("the ESP8266 tops out around 1-4 Mbit/s;"), C_MID);
  nlRowKV(5, FS(""), FS("a weak signal or a busy channel lowers it"), C_MID);
}

// ---------- 8: port check ----------
void nlPortsRun() {
  tft.fillRect(0, 22, W, 186, C_LCD);
  const uint16_t ports[7] = {22, 23, 53, 80, 443, 1900, 8080};
  IPAddress gw = WiFi.gatewayIP();
  char v[48], k[24];
  snprintf_P(v, sizeof v, PSTR("router %s"), gw.toString().c_str());
  nlRowKV(0, FS("checking"), v, C_INK);
  int r = 1;
  for (int i = 0; i < 7; i++) {
    WiFiClient c;
    c.setTimeout(700);
    uint32_t t = millis();
    bool open = c.connect(gw, ports[i]);
    t = millis() - t;
    c.stop();
    snprintf_P(k, sizeof k, PSTR("%u %s"), ports[i], pgmItem(PSTR("SSH\ntelnet\nDNS\nweb admin\nweb (TLS)\nUPnP\nalt web"), i));
    snprintf_P(v, sizeof v, open ? PSTR("open (%lu ms)") : PSTR("closed"), (unsigned long)t);
    nlRowKV(r++, k, v, open ? nlAccent() : C_MID);
    yield();
  }
  char host[24]; snprintf_P(host, sizeof host, PSTR("%s"), P.relay);
  char* colon = strchr(host, ':');
  uint16_t port = colon ? atoi(colon + 1) : 8787;
  if (colon) *colon = 0;
  IPAddress rip;
  WiFiClient c;
  c.setTimeout(1500);
  bool ok = rip.fromString(host) && c.connect(rip, port);
  c.stop();
  snprintf_P(k, sizeof k, PSTR("%u relay"), port);
  snprintf_P(v, sizeof v, ok ? PSTR("open - the PC relay is running") : PSTR("closed - relay off or firewall?"));
  nlRowKV(r++, k, v, ok ? nlAccent() : E_CORAL);
}

// ---------- 9: who's on my WiFi (ARP sweep of the /24) ----------
void nlLanBegin() {
  WK.nl.n = 0; nlStep = 1; nlNext = 0; nlBusy = true; nlScroll = 0;
  tft.fillRect(0, 22, W, 186, C_LCD);
  putText(8, 26, 1, C_INK, C_LCD, FS("asking every address on your network..."));
}
void nlLanAdd(uint8_t host, const uint8_t* mac) {
  for (int i = 0; i < WK.nl.n; i++) if (WK.nl.host[i] == host) return;
  if (WK.nl.n >= 48) return;
  WK.nl.host[WK.nl.n] = host;
  memcpy(WK.nl.mac[WK.nl.n], mac, 6);
  WK.nl.n++;
}
void nlLanDraw() {
  char b[56];
  IPAddress me = WiFi.localIP(), gw = WiFi.gatewayIP();
  char relayHost[24]; snprintf_P(relayHost, sizeof relayHost, PSTR("%s"), P.relay);
  char* colon = strchr(relayHost, ':'); if (colon) *colon = 0;
  IPAddress rip; rip.fromString(relayHost);
  tft.fillRect(0, 40, W, 168, C_LCD);
  for (int r = 0; r < 11; r++) {
    int i = nlScroll + r, y = 40 + r * 15;
    uint16_t bg = (r & 1) ? E_ROW2 : E_WHITE;
    tft.fillRect(4, y, 312, 15, bg);
    if (i >= WK.nl.n) continue;
    uint8_t h = WK.nl.host[i];
    const uint8_t* m = WK.nl.mac[i];
    const char* tag = h == me[3] ? FS("this Psion") : h == gw[3] ? FS("router") : h == rip[3] ? FS("your PC (relay)") : (m[0] & 2) ? FS("phone? (random MAC)") : FS("");
    snprintf_P(b, sizeof b, PSTR("%d.%d.%d.%-3d %02X:%02X:%02X:%02X:%02X:%02X %s"), me[0], me[1], me[2], h, m[0], m[1], m[2], m[3], m[4], m[5], tag);
    putText(8, y + 4, 1, tag[0] ? nlAccent() : C_INK, bg, b);
  }
}
void nlLanTick(uint32_t now) {
  if (!nlBusy || now < nlNext) return;
  IPAddress me = WiFi.localIP();
  struct netif* nif = netif_default;
  if (!nif) { nlBusy = false; return; }
  if (nlStep > 1) {                                   // collect answers to the last batch
    for (size_t i = 0; i < ARP_TABLE_SIZE; i++) {
      ip4_addr_t* ip; struct netif* n; struct eth_addr* eth;
      if (etharp_get_entry(i, &ip, &n, &eth) == 1) {
        uint32_t a = ip4_addr_get_u32(ip);
        if ((a & 0xFFFFFF) == ((uint32_t)me & 0xFFFFFF)) nlLanAdd((uint8_t)(a >> 24), eth->addr);
      }
    }
  }
  if (nlStep > 254) {
    uint8_t mm[6]; WiFi.macAddress(mm); nlLanAdd(me[3], mm);
    for (int i = 0; i < WK.nl.n; i++) for (int j = i + 1; j < WK.nl.n; j++) if (WK.nl.host[j] < WK.nl.host[i]) {
      uint8_t t = WK.nl.host[i]; WK.nl.host[i] = WK.nl.host[j]; WK.nl.host[j] = t;
      uint8_t tm[6]; memcpy(tm, WK.nl.mac[i], 6); memcpy(WK.nl.mac[i], WK.nl.mac[j], 6); memcpy(WK.nl.mac[j], tm, 6);
    }
    nlBusy = false;
    char b[48]; snprintf_P(b, sizeof b, PSTR("found %d devices (some hide from ARP)"), WK.nl.n);
    tft.fillRect(0, 22, W, 16, C_LCD); putText(8, 26, 1, C_INK, C_LCD, b);
    nlLanDraw(); SFX(T_HOP);
    return;
  }
  for (int k = 0; k < 8 && nlStep <= 254; k++, nlStep++) {    // ask the next 8 addresses
    if (nlStep == me[3]) continue;
    ip4_addr_t a;
    IP4_ADDR(&a, me[0], me[1], me[2], nlStep);
    etharp_request(nif, &a);
  }
  nlBar(8, 40, 304, 10, nlStep * 100 / 255, nlAccent());
  nlNext = now + 280;
}

// ---------- 10: NTP ----------
uint32_t nlNtpUnix = 0;
void nlNtpRun() {
  tft.fillRect(0, 22, W, 186, C_LCD);
  nlNtpUnix = 0;
  IPAddress srv;
  char v[48];
  if (!WiFi.hostByName(FS("pool.ntp.org"), srv, 4000)) { nlRowKV(0, FS("server"), FS("couldn't look up pool.ntp.org"), E_CORAL); return; }
  snprintf_P(v, sizeof v, PSTR("pool.ntp.org = %s"), srv.toString().c_str());
  nlRowKV(0, FS("server"), v, C_INK);
  WiFiUDP udp;
  udp.begin(2390);
  uint8_t pkt[48];
  memset(pkt, 0, sizeof pkt);
  pkt[0] = 0xE3; pkt[2] = 6; pkt[3] = 0xEC;          // client, version 4
  uint32_t t0 = millis();
  udp.beginPacket(srv, 123); udp.write(pkt, 48); udp.endPacket();
  int got = 0;
  while (millis() - t0 < 2500) { if (udp.parsePacket() >= 48) { got = udp.read(pkt, 48); break; } delay(2); }
  uint32_t rtt = millis() - t0;
  udp.stop();
  if (got < 48) { nlRowKV(1, FS("answer"), FS("none in 2.5 s (UDP 123 blocked?)"), E_CORAL); return; }
  uint32_t secs = ((uint32_t)pkt[40] << 24) | ((uint32_t)pkt[41] << 16) | ((uint32_t)pkt[42] << 8) | pkt[43];
  nlNtpUnix = secs - 2208988800UL + rtt / 2000;
  uint32_t local = nlNtpUnix + P.tz * 3600L;
  snprintf_P(v, sizeof v, PSTR("%lu ms round trip, stratum %d"), (unsigned long)rtt, pkt[1]);
  nlRowKV(1, FS("answer"), v, C_INK);
  snprintf_P(v, sizeof v, PSTR("%02lu:%02lu:%02lu (UTC%+d)"), (unsigned long)(local / 3600 % 24), (unsigned long)(local / 60 % 60), (unsigned long)(local % 60), P.tz);
  nlRowKV(2, FS("network time"), v, nlAccent());
  snprintf_P(v, sizeof v, PSTR("%02lu:%02lu:%02lu"), (unsigned long)(clkEpoch / 3600 % 24), (unsigned long)(clkEpoch / 60 % 60), (unsigned long)(clkEpoch % 60));
  nlRowKV(3, FS("this Psion"), v, C_INK);
  long off = (long)clkEpoch - (long)local;
  snprintf_P(v, sizeof v, PSTR("%+ld s %s"), off, labs(off) <= 1 ? FS("(spot on)") : off > 0 ? FS("(Psion is ahead)") : FS("(Psion is behind)"));
  nlRowKV(4, FS("difference"), v, labs(off) <= 1 ? C_INK : E_AMBER);
}

// ---------- 11: radio settings ----------
const float NL_TX[6] = {20.5f, 17.0f, 14.0f, 10.0f, 5.0f, 0.0f};
void nlRadioDraw() {
  char v[48];
  int ti = 0; for (int i = 0; i < 6; i++) if (fabs(NL_TX[i] - nlTxPower) < 0.1f) ti = i;
  int pm = WiFi.getPhyMode();
  for (int r = 0; r < 3; r++) {
    if (r == 0) snprintf_P(v, sizeof v, PSTR("%.1f dBm%s"), NL_TX[ti], ti ? FS("") : FS(" (max)"));
    if (r == 1) snprintf_P(v, sizeof v, PSTR("%s"), pgmItem(PSTR("none: fastest replies\nlight: saves a little\nmodem: saves the most"), constrain((int)WiFi.getSleepMode(), 0, 2)));
    if (r == 2) snprintf_P(v, sizeof v, PSTR("802.11%c%s"), pm == WIFI_PHY_MODE_11B ? 'b' : pm == WIFI_PHY_MODE_11G ? 'g' : 'n', pm == WIFI_PHY_MODE_11B ? FS(" (longest reach)") : pm == WIFI_PHY_MODE_11N ? FS(" (fastest)") : FS(""));
    nlRowKV(r, pgmItem(PSTR("TX power\nsleep mode\nWiFi mode"), r), v, r == nlRow ? nlAccent() : C_INK);
    if (r == nlRow) tft.drawRect(4, 24 + r * 14, 312, 14, nlAccent());
  }
  int rssi = WiFi.RSSI();
  snprintf_P(v, sizeof v, PSTR("%d dBm  %s"), rssi, nlQualityWord(rssi));
  nlRowKV(4, FS("signal now"), v, C_INK);
  nlRowKV(6, FS("tip"), FS("less TX power = less battery, less range"), C_MID);
  nlRowKV(7, FS(""), FS("changes last until the Psion restarts"), C_MID);
}
void nlRadioChange() {
  if (nlRow == 0) {
    int ti = 0; for (int i = 0; i < 6; i++) if (fabs(NL_TX[i] - nlTxPower) < 0.1f) ti = i;
    nlTxPower = NL_TX[(ti + 1) % 6];
    WiFi.setOutputPower(nlTxPower);
  } else if (nlRow == 1) WiFi.setSleepMode((WiFiSleepType_t)((WiFi.getSleepMode() + 1) % 3));
  else {
    int pm = WiFi.getPhyMode();
    WiFi.setPhyMode(pm == WIFI_PHY_MODE_11N ? WIFI_PHY_MODE_11G : pm == WIFI_PHY_MODE_11G ? WIFI_PHY_MODE_11B : WIFI_PHY_MODE_11N);
  }
  nlRadioDraw();
}

// ---------- 12: event log ----------
void nlEventsDraw() {
  char v[48];
  uint32_t up = nlConnAt ? (millis() - nlConnAt) / 1000 : 0;
  snprintf_P(v, sizeof v, nlConnAt ? PSTR("connected for %lum %02lus") : PSTR("not connected"), (unsigned long)(up / 60), (unsigned long)(up % 60));
  nlRowKV(0, FS("now"), v, nlConnAt ? nlAccent() : E_CORAL);
  snprintf_P(v, sizeof v, PSTR("%u   last: %s"), nlDrops, nlLastReason ? nlReason(nlLastReason) : FS("-"));
  nlRowKV(1, FS("drops"), v, C_INK);
  for (int i = 0; i < 8; i++) nlRowKV(3 + i, i == 0 ? FS("events") : FS(""), i < nlEvN ? nlEv[nlEvN - 1 - i] : FS(""), C_INK);
}

// ---------- 13: reconnect test ----------
void nlReconnectTick(uint32_t now) {
  if (!nlBusy) return;
  char v[40];
  if (nlStep == 0) { WiFi.reconnect(); nlStep = 1; nlT0 = now; nlRowKV(1, FS("status"), FS("dropped on purpose..."), E_AMBER); return; }
  if (nlStep == 1 && WiFi.status() != WL_CONNECTED) nlStep = 2;
  bool back = nlStep == 2 && WiFi.status() == WL_CONNECTED;
  if (back || now - nlT0 > 20000) {
    nlBusy = false;
    if (back) snprintf_P(v, sizeof v, PSTR("back online in %.1f s"), (now - nlT0) / 1000.0f);
    else snprintf_P(v, sizeof v, PSTR("not back after 20 s"));
    nlRowKV(1, FS("status"), v, back ? nlAccent() : E_CORAL);
    if (back) SFX(T_HOP); else SFX(T_HIT);
    return;
  }
  snprintf_P(v, sizeof v, PSTR("waiting... %.1f s"), (now - nlT0) / 1000.0f);
  nlRowKV(2, FS("timer"), v, C_INK);
}

// ---------- app ----------
void nlOpen(int t) {
  nlTest = t; nlStep = 0; nlBusy = false; nlNext = 0; nlScroll = 0; nlRow = 0;
  WK.nl.n = 0;
  nlFrame();
  if (t != NL_EVENTS && t != NL_RADIO && t != NL_INFO && !nlNeedWiFi()) return;
  switch (t) {
    case NL_INFO: nlInfoDraw(); break;
    case NL_SIGNAL: break;
    case NL_CHANNELS: nlScanStart(); break;
    case NL_PINGGW: case NL_PINGNET: nlPingBegin(); break;
    case NL_DNS: nlDnsRun(); break;
    case NL_INTERNET: nlStatus(FS("checking..."), C_MID); nlInternetRun(); nlStatus(FS("done"), C_MID); break;
    case NL_SPEED: nlSpeedRun(); break;
    case NL_PORTS: nlStatus(FS("knocking on doors..."), C_MID); nlPortsRun(); nlStatus(FS("done"), C_MID); break;
    case NL_LAN: nlLanBegin(); break;
    case NL_NTP: nlNtpRun(); break;
    case NL_RADIO: nlRadioDraw(); break;
    case NL_EVENTS: nlEventsDraw(); break;
    case NL_RECONNECT:
      nlRowKV(0, FS("what"), FS("drops WiFi, then times the way back"), C_INK);
      nlRowKV(1, FS("status"), FS("press to start"), C_MID);
      break;
  }
}
void netlabEnter() { wkOwner = APP_NETLAB; nlDrawList(); }
void netlabBack() { nlDrawList(); }
void netlabTick(uint32_t now) {
  int c = takeClicks() * ENC_DIR;
  takePress();
  bool sp = takeShort();
  if (nlTest < 0) {
    if (c) {
      int old = nlSel, oldTop = nlTop;
      nlSel = ((nlSel + c) % NL_N + NL_N) % NL_N;
      if (nlSel < nlTop) nlTop = nlSel; else if (nlSel >= nlTop + 8) nlTop = nlSel - 7;
      if (nlTop != oldTop) nlDrawList(); else { nlDrawRow(old - nlTop); nlDrawRow(nlSel - nlTop); }
      uiClick(); oledDirty = true;
    }
    if (sp) { SFX(T_HOP); nlOpen(nlSel); oledDirty = true; }
    return;
  }
  switch (nlTest) {
    case NL_INFO: if (now >= nlNext) { nlNext = now + 1000; nlInfoDraw(); } break;
    case NL_SIGNAL: nlSignalTick(now); if (sp) nlBeep = !nlBeep; break;
    case NL_CHANNELS:
      if (nlBusy) nlScanCollect();
      if (c && WK.nl.n > 7) { nlScroll = constrain(nlScroll + c, 0, WK.nl.n - 7); nlScanDraw(); }
      if (sp && !nlBusy) nlScanStart();
      break;
    case NL_PINGGW: case NL_PINGNET:
      nlPingTick(now);
      if (sp) { if (nlTest == NL_PINGNET) nlTarget = (nlTarget + 1) % 3; nlPingBegin(); }
      break;
    case NL_DNS: if (sp) nlDnsRun(); break;
    case NL_INTERNET: if (sp) { nlInternetRun(); nlStatus(FS("done"), C_MID); } break;
    case NL_SPEED: if (sp) nlSpeedRun(); break;
    case NL_PORTS: if (sp) { nlPortsRun(); nlStatus(FS("done"), C_MID); } break;
    case NL_LAN:
      nlLanTick(now);
      if (c && !nlBusy && WK.nl.n > 11) { nlScroll = constrain(nlScroll + c, 0, WK.nl.n - 11); nlLanDraw(); }
      if (sp && !nlBusy) nlLanBegin();
      break;
    case NL_NTP:
      if (sp && nlNtpUnix) { clkEpoch = nlNtpUnix + P.tz * 3600L; clkMs = millis(); forceClockRedraw(); drawTitleTime(); nlStatus(FS("clock set from the network"), nlAccent()); SFX(T_HOP); }
      else if (sp) nlNtpRun();
      break;
    case NL_RADIO:
      if (c) { nlRow = ((nlRow + c) % 3 + 3) % 3; nlRadioDraw(); uiClick(); }
      if (sp) nlRadioChange();
      break;
    case NL_EVENTS: if (now >= nlNext) { nlNext = now + 1000; nlEventsDraw(); } break;
    case NL_RECONNECT:
      if (sp && !nlBusy) { nlBusy = true; nlStep = 0; nlRowKV(2, FS("timer"), FS(""), C_INK); }
      nlReconnectTick(now);
      break;
  }
}
void oledNetlab() {
  char l[4][24];
  bool on = WiFi.status() == WL_CONNECTED;
  snprintf_P(l[0], 24, PSTR("%.21s"), pgmItem(NL_NAMES_P, nlTest < 0 ? nlSel : nlTest));
  snprintf_P(l[1], 24, PSTR("%.21s"), on ? WiFi.SSID().c_str() : FS("not connected"));
  snprintf_P(l[2], 24, PSTR("%d dBm  ch %d"), on ? WiFi.RSSI() : 0, on ? WiFi.channel() : 0);
  snprintf_P(l[3], 24, PSTR("drops %u  heap %uK"), nlDrops, (unsigned)(ESP.getFreeHeap() / 1024));
  oledCard(FS("NETLAB"), l[0], l[1], l[2], l[3]);
}

// ================= MICOR RUN: a 3D run through a hexagonal space tunnel =================
// Micor the white wolf runs down a six-sided tunnel floating in space. Turn the knob to step sideways: past the
// edge of the floor you run up onto the next wall and the whole tunnel rolls so it becomes the new floor.
// Press to jump over gaps, grab the golden tiles. The tunnel is made up as you go and gets harder with distance.
// Real 3D, per pixel: the camera sits on the tunnel's axis, so for each pixel the wall it sees is the one whose
// normal points most its way; depth = apothem * focal / (ray . normal) comes from a reciprocal table, the position
// along the wall from (ray . tangent) / (ray . normal). Rows are worked out 32 pixels at a time straight into the
// SPI FIFO while the previous 32 are still being sent, so drawing and sending overlap (~25-30 frames a second).
const int RUN_TOP = 16, RUN_H = 224, RUN_CX = 160, RUN_CY = 112;
const int RUN_F = 150;                              // focal length (pixels); the apothem (centre to wall) is 1 unit
const int32_t RUN_ZP8 = 348;                        // Micor's distance ahead of the camera (1.36 units, Q8)
const int RUN_DFAR = 14, RUN_DMAX = 700;            // dot/16384 below 14 = farther than 22 units: fog
const int RUN_SW = 44, RUN_SH = 40;                 // the wolf sprite
enum { RS_TITLE, RS_PLAY, RS_OVER };
enum { RP_SOLID, RP_HOLES, RP_GAP, RP_WALLS, RP_SPIRAL, RP_ISLANDS };
int runState = RS_TITLE;
GFXcanvas8* runSpr[3] = {nullptr, nullptr, nullptr};   // run A, run B, jump
uint16_t runSprPal[9];
uint32_t runCam8 = 0, runGen = 0, runLast = 0, runHudAt = 0, runZoneAt = 0, runStartMs = 0;
float runSpeed = 4.5f, runS = 0, runST = 0, runH = 0, runVH = 0, runRoll = 0, runRollT = 0, runPalRoll = 99;
int runWall = 0, runSafe = 1, runPat = RP_SOLID, runPatLeft = 40, runGems = 0, runZone = -1, runFrame = 0;
uint8_t runMissing = 0;                             // walls left out by the "walls" pattern (bit per wall)
bool runAir = false, runFalling = false, runNewBest = false;
int runLives = 3;                                    // falls left (easy 5, normal 3, hard 1)
uint32_t runCoyote = 0, runSafeUntil = 0;           // a moment over a hole before falling; no falling after a rescue
uint16_t runFarCol, runStarCol, runSpaceCol;
const uint16_t T_GEM[] PROGMEM = {1568, 30, 2093, 60};
const uint16_t T_SWISH[] PROGMEM = {700, 18};
// zone themes: tile A, tile B, edge lines, golden tile, fog, space (RGB)
const uint8_t RUN_THEMES[5][6][3] PROGMEM = {
  {{205, 228, 248}, {168, 200, 238}, { 80, 120, 200}, {255, 214,  90}, { 18,  26,  58}, {  6,  10,  30}},   // Frost
  {{ 80, 222, 190}, { 58, 168, 205}, {150,  90, 255}, {255, 222, 100}, { 26,  10,  52}, {  8,   4,  26}},   // Aurora
  {{255, 150,  72}, {230, 100,  62}, {110,  28,  40}, {255, 242, 150}, { 40,   8,   8}, { 18,   4,   4}},   // Ember
  {{255,  92, 200}, { 92, 200, 255}, { 44,  22,  86}, {255, 255, 120}, { 30,   0,  40}, { 10,   0,  20}},   // Neon
  {{ 72,  92, 152}, { 52,  72, 122}, {160, 205, 255}, {255, 200,  80}, {  4,   6,  16}, {  2,   2,   8}}};  // Midnight
const char RUN_ZONES_P[] PROGMEM = "FROST\nAURORA\nEMBER\nNEON\nMIDNIGHT";
const char RUN_LEVELS_P[] PROGMEM = "EASY\nNORMAL\nHARD";
uint16_t runSwap(uint16_t c) { return (uint16_t)((c >> 8) | (c << 8)); }   // the SPI FIFO sends the low byte first
uint16_t runRGB(int r, int g, int b) { return runSwap(tft.map(pk565(r, g, b))); }
float runLevelK() { const float k[3] = {0.7f, 0.85f, 1.05f}; return k[P.runLevel % 3]; }   // (gentler since 3.4)
uint16_t runAccent() { return appAccent(APP_RUN); }
uint32_t runMeters() { return runCam8 >> 7; }       // one ring = 0.5 units = 1 metre

// ---------- the wolf: drawn once with shapes into three little 8-bit sprites, then outlined ----------
void runDrawWolf(GFXcanvas8& g, int f) {
  g.fillScreen(0);                                  // seen from behind: tail curled up on the right, ears up
  bool jump = f == 2;
  int tl = jump ? -3 : (f == 1 ? -1 : 0);           // the tail lifts when jumping and bobs while running
  g.fillCircle(31, 29 + tl, 4, 3); g.fillCircle(35, 24 + tl, 5, 2); g.fillCircle(38, 17 + tl, 5, 2);   // bushy tail
  g.fillCircle(38, 11 + tl, 4, 2); g.fillCircle(37, 7 + tl, 3, 4);
  g.fillCircle(36, 25 + tl, 2, 3);
  if (jump) {                                       // legs tucked, both paw pads showing
    g.fillRoundRect(13, 30, 7, 7, 3, 3); g.fillRoundRect(25, 30, 7, 7, 3, 3);
    g.fillRoundRect(14, 34, 5, 3, 1, 5); g.fillRoundRect(26, 34, 5, 3, 1, 5);
  } else {                                          // one hind leg pushes off, the other swings up (pad showing)
    bool l = f == 1;
    g.fillRoundRect(13, l ? 29 : 31, 7, l ? 7 : 9, 3, 3);
    g.fillRoundRect(25, l ? 31 : 29, 7, l ? 9 : 7, 3, 3);
    if (l) g.fillRoundRect(14, 33, 5, 3, 1, 5); else g.fillRoundRect(26, 33, 5, 3, 1, 5);
  }
  g.fillRoundRect(9, 17, 26, 18, 8, 1);             // body: a thin dark line keeps it apart from the tail
  g.fillRoundRect(10, 19, 24, 15, 7, 3);            // ...with a soft shadow down its left side
  g.fillRoundRect(11, 18, 23, 14, 7, 2);
  g.fillTriangle(12, 13, 14, 2, 20, 8, 2); g.fillTriangle(32, 13, 30, 2, 24, 8, 2);      // tall ears
  g.fillTriangle(14, 10, 15, 5, 18, 8, 4); g.fillTriangle(30, 10, 29, 5, 26, 8, 4);      // backs of the ears
  g.fillCircle(23, 12, 9, 1);                        // head: outlined so it stands out from the back
  g.fillCircle(22, 13, 8, 3); g.fillCircle(23, 12, 8, 2);                                // shaded on the left
  g.fillTriangle(15, 12, 15, 19, 10, 18, 2); g.fillTriangle(29, 12, 29, 19, 34, 18, 2); // cheek fluff
  g.fillTriangle(18, 19, 22, 23, 26, 19, 3);         // the ruff where the head meets the back
  uint8_t* b = g.getBuffer();                        // outline: transparent pixels next to fur
  for (int y = 0; y < RUN_SH; y++)
    for (int x = 0; x < RUN_SW; x++) {
      uint8_t* p = b + y * RUN_SW + x;
      if (*p) continue;
      bool edge = (x > 0 && p[-1] > 1 && p[-1] < 9) || (x < RUN_SW - 1 && p[1] > 1 && p[1] < 9) ||
                  (y > 0 && p[-RUN_SW] > 1 && p[-RUN_SW] < 9) || (y < RUN_SH - 1 && p[RUN_SW] > 1 && p[RUN_SW] < 9);
      if (edge) *p = 9;
    }
  for (int i = 0; i < RUN_SW * RUN_SH; i++) if (b[i] == 9) b[i] = 1;
}
bool runMakeSprites() {
  for (int f = 0; f < 3; f++) {
    if (!runSpr[f]) runSpr[f] = new GFXcanvas8(RUN_SW, RUN_SH);
    if (!runSpr[f] || !runSpr[f]->getBuffer()) return false;
    runDrawWolf(*runSpr[f], f);
  }
  return true;
}
void runFreeSprites() { for (int f = 0; f < 3; f++) { delete runSpr[f]; runSpr[f] = nullptr; } }

// ---------- colours: per wall (lit by which way it faces), per tile kind, per fog step ----------
void runPalette() {
  int z = runZone % 5;
  uint8_t th[6][3];
  memcpy_P(th, RUN_THEMES[z], sizeof th);
  static const uint8_t FOG[8] = {0, 34, 67, 101, 134, 168, 201, 235};   // how much fog colour, of 256
  for (int i = 0; i < 6; i++) {
    int lit = 179 + (int)(77 * sinf(1.5707963f + i * 1.0471976f + runRoll));   // of 256: floor 1.0, ceiling 0.4
    for (int t = 0; t < 4; t++)
      for (int f = 0; f < 8; f++) {
        int l = t == 3 ? 256 : lit, k = FOG[f];                     // golden tiles glow: no shading
        int c[3];
        for (int ch = 0; ch < 3; ch++) c[ch] = ((th[t][ch] * l >> 8) * (256 - k) + th[4][ch] * k) >> 8;
        WK.run.pal[i][t][f] = runRGB(c[0], c[1], c[2]);
      }
  }
  runFarCol = runRGB(th[4][0], th[4][1], th[4][2]);
  runSpaceCol = runRGB(th[5][0], th[5][1], th[5][2]);
  runStarCol = runRGB(230, 236, 255);
  runPalRoll = runRoll;
}

// ---------- the tunnel, made up ring by ring (64 rings kept, about 44 in view) ----------
void runPickPattern() {
  float d = min(1.0f, runGen / 3200.0f);             // 0 at the start, 1 after ~3 km
  int r = random(100);
  if (runPat == RP_GAP) { runPat = RP_SOLID; runPatLeft = 5 + random(4); return; }   // land safely after a gap
  if (r < 30 - (int)(d * 12)) { runPat = RP_SOLID; runPatLeft = 8 + random(10); }
  else if (r < 52) { runPat = RP_HOLES; runPatLeft = 8 + random(14); }
  else if (r < 58 + (int)(d * 6)) { runPat = RP_GAP; runPatLeft = 1 + (d > 0.6f && random(3) == 0); }
  else if (r < 78) {
    runPat = RP_WALLS; runPatLeft = 10 + random(14);
    int keep = runSafe / 3;                          // drop 2-4 walls, never the one with the safe lane
    runMissing = 0;
    for (int n = 2 + random(1 + (int)(d * 2.5f)); n > 0; n--) { int w = random(6); if (w != keep) runMissing |= 1 << w; }
  }
  else if (r < 90) { runPat = RP_SPIRAL; runPatLeft = 12 + random(16); }
  else { runPat = RP_ISLANDS; runPatLeft = 8 + random(12); }
}
void runGenRing() {
  uint8_t* t = WK.run.tiles[runGen & 63];
  if (runPatLeft <= 0) runPickPattern();
  float d = min(1.0f, runGen / 3200.0f);
  for (int l = 0; l < 18; l++) {
    uint8_t v = 1;
    switch (runPat) {
      case RP_HOLES:   v = random(100) < 8 + (int)(d * 20) ? 0 : 1; break;
      case RP_GAP:     v = 0; break;
      case RP_WALLS:   v = (runMissing >> (l / 3)) & 1 ? 0 : 1; break;
      case RP_SPIRAL:  v = ((l + (int)(runGen / 2)) % 6) < 2 + (d < 0.5f) ? 1 : 0; break;
      case RP_ISLANDS: v = random(100) < 50 - (int)(d * 10) ? 1 : 0; break;
    }
    if (v && random(100) < 3) v = 2;                 // a golden tile
    t[l] = v;
  }
  if (runPat != RP_GAP) {                            // the safe lane (and usually a neighbour) is always there
    if (!t[runSafe]) t[runSafe] = random(100) < 12 ? 2 : 1;
    for (int k = 1; k < 18; k += 16) { int nb = (runSafe + k) % 18; if (!t[nb]) t[nb] = 1; }   // both neighbours: a 3-lane path
    if (runGen % 4 == 0 && random(100) < 30) runSafe = (runSafe + (random(2) ? 1 : 17)) % 18;   // the path wanders (slowly)
  }
  if (runGen < 30) for (int l = 0; l < 18; l++) t[l] = 1;   // a solid run-up at the start
  runGen++; runPatLeft--;
}
uint8_t runTileAt(uint32_t z8, int wall, int32_t s8) {
  int lane = constrain((int)(((s8 + 148) * 3 * 7085) >> 21), 0, 2);
  return WK.run.tiles[(z8 >> 7) & 63][wall * 3 + lane];
}

// ---------- drawing: straight into the SPI FIFO, 32 pixels at a time ----------
static inline void runSpiBits(uint32_t bits) {
  const uint32_t mask = ~((SPIMMOSI << SPILMOSI) | (SPIMMISO << SPILMISO));
  bits--;
  SPI1U1 = (SPI1U1 & mask) | (bits << SPILMOSI) | (bits << SPILMISO);
}
static inline void runSpiSend(const uint32_t* w) {   // 64 bytes; returns at once, the hardware sends while we work
  while (SPI1CMD & SPIBUSY) {}
  volatile uint32_t* fifo = &SPI1W0;
  for (int i = 0; i < 16; i++) fifo[i] = w[i];
  SPI1CMD |= SPIBUSY;
}
void runRender() {
  if (fabsf(runRoll - runPalRoll) > 0.004f) runPalette();
  int32_t nx[6], ny[6];
  for (int i = 0; i < 6; i++) {
    float th = 1.5707963f + i * 1.0471976f + runRoll;
    nx[i] = (int32_t)(cosf(th) * 16384); ny[i] = (int32_t)(sinf(th) * 16384);
  }
  // where Micor and the shadow go on screen (the floor wall's normal + tangent at this roll)
  float fnx = nx[runWall] / 16384.0f, fny = ny[runWall] / 16384.0f, s = runS / 256.0f, zp = RUN_ZP8 / 256.0f;
  float h = runFalling ? runH : max(0.0f, runH);
  int px = RUN_CX + (int)(RUN_F * ((1 - h) * fnx + s * fny) / zp), py = RUN_CY + (int)(RUN_F * ((1 - h) * fny - s * fnx) / zp);
  int shx = RUN_CX + (int)(RUN_F * (fnx + s * fny) / zp), shy = RUN_CY + (int)(RUN_F * (fny - s * fnx) / zp);
  int shrx = runFalling ? 0 : max(5, 15 - (int)(h * 14)), shry = 4;
  const uint8_t* spr = runSpr[runAir || runFalling ? 2 : runFrame]->getBuffer();
  int sx = px - RUN_SW / 2, sy = py - RUN_SH + 2;
  const uint32_t cam = runCam8;
  const uint16_t* R = WK.run.recip;
  static uint32_t mirAt = 0;
  static uint16_t mirRow[160];
  bool mir = rcLive && millis() - mirAt > 300;
  if (mir) mirAt = millis();
  tft.startWrite();
  tft.Adafruit_ST7789::setAddrWindow(0, RUN_TOP, W, RUN_H);
  runSpiBits(512);
  uint32_t blk[16];
  uint16_t* bp = (uint16_t*)blk;
  for (int y = RUN_TOP; y < H; y++) {
    int32_t dy2 = 2 * (y - RUN_CY) + 1, dx2 = 1 - 2 * RUN_CX;
    int k = 0;
    int32_t dC = INT32_MIN;
    for (int i = 0; i < 6; i++) { int32_t d = dx2 * nx[i] + dy2 * ny[i]; if (d > dC) { dC = d; k = i; } }
    int kp = (k + 5) % 6, kn = (k + 1) % 6;
    int32_t dP = dx2 * nx[kp] + dy2 * ny[kp], dN = dx2 * nx[kn] + dy2 * ny[kn];
    int32_t iC = 2 * nx[k], iP = 2 * nx[kp], iN = 2 * nx[kn];
    int32_t tC = dx2 * ny[k] - dy2 * nx[k], itC = 2 * ny[k];
    int shL = 9999, shR = -1;                        // the shadow's span on this row
    if (shrx && y >= shy - shry && y <= shy + shry) {
      float q = (float)(y - shy) / shry;
      int half = (int)(shrx * sqrtf(max(0.0f, 1 - q * q)));
      shL = shx - half; shR = shx + half;
    }
    const uint8_t* srow = (y >= sy && y < sy + RUN_SH) ? spr + (y - sy) * RUN_SW : nullptr;
    for (int bx = 0; bx < W; bx += 32) {
      for (int i = 0; i < 32; i++) {
        while (dN > dC || dP > dC) {                 // crossed into the next wall along this row
          if (dN > dC) { kp = k; dP = dC; iP = iC; k = kn; dC = dN; iC = iN; kn = (k + 1) % 6; dN = dx2 * nx[kn] + dy2 * ny[kn]; iN = 2 * nx[kn]; }
          else { kn = k; dN = dC; iN = iC; k = kp; dC = dP; iC = iP; kp = (k + 5) % 6; dP = dx2 * nx[kp] + dy2 * ny[kp]; iP = 2 * nx[kp]; }
          tC = dx2 * ny[k] - dy2 * nx[k]; itC = 2 * ny[k];
        }
        uint16_t col;
        int32_t D = dC >> 14;
        if (D < RUN_DFAR) col = runFarCol;
        else {
          if (D > RUN_DMAX) D = RUN_DMAX;
          int32_t z8 = R[D] - (((int32_t)(R[D] - R[D + 1]) * ((dC >> 6) & 255)) >> 8);
          int32_t t3 = ((((tC >> 8) * z8 * 437) >> 23) + 148) * 3;
          if (t3 < 0) t3 = 0; else if (t3 > 887) t3 = 887;
          int lane = (t3 * 7085) >> 21, u = t3 - lane * 296;
          uint32_t v8 = z8 + cam;
          uint8_t tile = WK.run.tiles[(v8 >> 7) & 63][k * 3 + lane];
          int x = bx + i;
          if (!tile) {                               // a gap: space and a few stars
            uint32_t hsh = (uint32_t)(x * 0x9E37 + y * 0x79B9) & 0x3FF;
            col = hsh < 3 ? runStarCol : runSpaceCol;
          } else {
            int fog = (z8 * 47) >> 15;
            if (x >= shL && x <= shR) fog += 3;      // Micor's shadow
            if (fog > 7) fog = 7;
            int type = (u < 10 || u > 285 || (v8 & 127) < 8) ? 2 : (tile == 2 ? 3 : (int)(((v8 >> 7) + lane) & 1));
            col = WK.run.pal[k][type][fog];
          }
        }
        bp[i] = col;
        dx2 += 2; dC += iC; dP += iP; dN += iN; tC += itC;
      }
      if (srow && bx + 32 > sx && bx < sx + RUN_SW)  // Micor on top
        for (int x = max(bx, sx); x < min(bx + 32, sx + RUN_SW); x++) { uint8_t c = srow[x - sx]; if (c) bp[x - bx] = runSprPal[c]; }
      if (mir && !(y & 1)) for (int i = 0; i < 32; i += 2) mirRow[(bx + i) >> 1] = runSwap(bp[i]);
      runSpiSend(blk);
    }
    if (mir && !(y & 1)) { rcWindowS(0, y, W, 2, 2); rcPixels(mirRow, 160); }
  }
  while (SPI1CMD & SPIBUSY) {}
  tft.endWrite();
}

// ---------- HUD, title, game over ----------
void runHud(bool force) {
  static uint32_t lastM = 0xFFFFFFFF;
  static int lastG = -1;
  uint32_t m = runMeters();
  static int lastL = -1;
  if (!force && m == lastM && runGems == lastG && runZoneAt == 0 && runLives == lastL) return;
  lastL = runLives;
  lastM = m; lastG = runGems;
  char b[40];
  if (force) { tft.fillRect(0, 0, W, RUN_TOP, K_SPACE); tft.drawFastHLine(0, RUN_TOP - 1, W, runAccent()); }
  snprintf_P(b, sizeof b, PSTR("%5lu m "), (unsigned long)m);
  putText(4, 4, 1, K_CREAM, K_SPACE, b);
  snprintf_P(b, sizeof b, PSTR("gems %-3d"), runGems);
  putText(60, 4, 1, K_YELLOW, K_SPACE, b);
  if (runZoneAt && millis() - runZoneAt < 2500) { snprintf_P(b, sizeof b, PSTR("ZONE: %-9s"), pgmItem(RUN_ZONES_P, runZone % 5)); putText(124, 4, 1, K_CYAN, K_SPACE, b); }
  else { if (runZoneAt) { runZoneAt = 0; } snprintf_P(b, sizeof b, PSTR("%-15s"), pgmItem(RUN_ZONES_P, runZone % 5)); putText(124, 4, 1, K_DIM, K_SPACE, b); }
  snprintf_P(b, sizeof b, PSTR("best %u"), P.runBest);
  putText(316 - (int)strlen(b) * 6, 4, 1, K_DIM, K_SPACE, b);
  for (int k = 0; k < 5; k++) tft.fillCircle(222 + k * 7, 8, 2, k < runLives ? K_PINK : K_SPACE);   // lives
}
void runTitle() {
  runState = RS_TITLE;
  tft.filter = P.gamePal;
  tft.fillScreen(K_SPACE);
  for (int i = 0; i < 60; i++) tft.drawPixel(random(W), random(H), i % 5 ? K_DIM : K_CREAM);
  uint16_t acc = runAccent();
  for (int r = 0; r < 6; r++) {                      // a hexagon tunnel going into the distance
    int rad = 118 - r * 19;
    uint16_t c = r % 2 ? acc : K_CYAN;
    for (int i = 0; i < 6; i++) {
      float a0 = i * 1.0471976f + 0.5236f, a1 = a0 + 1.0471976f;
      tft.drawLine(220 + (int)(cosf(a0) * rad), 120 + (int)(sinf(a0) * rad * 0.85f), 220 + (int)(cosf(a1) * rad), 120 + (int)(sinf(a1) * rad * 0.85f), c);
    }
  }
  micorFace(14, 56, 2, 0, false);
  putText(14, 14, 3, K_CREAM, K_SPACE, FS("MICOR RUN"));
  putText(16, 42, 1, K_CYAN, K_SPACE, FS("a 3D run through space"));
  char b[40];
  snprintf_P(b, sizeof b, PSTR("< %s >"), pgmItem(RUN_LEVELS_P, P.runLevel % 3));
  putText(14, 174, 2, K_YELLOW, K_SPACE, b);
  snprintf_P(b, sizeof b, PSTR("best: %u m"), P.runBest);
  putText(14, 196, 1, K_CREAM, K_SPACE, b);
  putText(14, 212, 1, K_DIM, K_SPACE, FS("turn: move   press: jump / start"));
  putText(14, 224, 1, K_DIM, K_SPACE, FS("turn here: difficulty   hold: menu"));
  oledDirty = true;
}
void runStart() {
  if (!runMakeSprites()) { runFreeSprites(); tft.fillRect(10, 100, 300, 30, K_SPACE); putText(16, 110, 1, E_CORAL, K_SPACE, FS("not enough RAM for the wolf (turn PC remote off?)")); return; }
  const uint16_t SPR[9] = {0, pk565(36, 32, 60), pk565(250, 250, 255), pk565(212, 218, 236), pk565(160, 168, 192),
                           pk565(255, 150, 185), pk565(40, 40, 60), pk565(60, 120, 230), pk565(196, 202, 218)};
  for (int i = 0; i < 9; i++) runSprPal[i] = runSwap(tft.map(SPR[i]));
  WK.run.recip[0] = 65535;
  for (int d = 1; d < 702; d++) WK.run.recip[d] = (uint16_t)min(65535L, 76800L / d);
  runState = RS_PLAY;
  runCam8 = 0; runGen = 0; runSafe = 1; runPat = RP_SOLID; runPatLeft = 40; runMissing = 0;
  runSpeed = 4.2f * runLevelK(); runS = runST = 0; runH = runVH = 0; runAir = runFalling = runNewBest = false;
  runLives = P.runLevel % 3 == 0 ? 5 : P.runLevel % 3 == 1 ? 3 : 1; runCoyote = 0; runSafeUntil = 0;
  runWall = 0; runRoll = runRollT = 0; runGems = 0; runZone = 0; runZoneAt = millis(); runPalRoll = 99;
  while (runGen < 60) runGenRing();
  runPalette();
  tft.fillRect(0, 0, W, RUN_TOP, K_SPACE);
  runHud(true);
  runLast = runStartMs = millis();
  startAppMusic(APP_RUN);
  SFX(T_START);
  mood = ":3"; oledDirty = true;
}
void runOver() {
  runState = RS_OVER;
  uint32_t m = runMeters() + runGems * 10;
  runNewBest = m > P.runBest;
  if (runNewBest) { P.runBest = (uint16_t)min(m, (uint32_t)65000); prefsSave(); }
  stopSound();
  if (runNewBest) SFX(T_WIN); else SFX(T_LOSE);
  mood = runNewBest ? "x3" : ";-;";
  uint16_t col = runNewBest ? K_YELLOW : K_PINK;
  tft.fillRoundRect(40, 70, 240, 104, 10, K_SPACE);
  tft.drawRoundRect(40, 70, 240, 104, 10, col);
  tft.drawRoundRect(42, 72, 236, 100, 8, K_DIM);
  drawPaw(62, 92, 2, col); drawPaw(258, 92, 2, col);
  centerText(runNewBest ? FS("NEW RECORD! x3") : FS("LOST IN SPACE ;-;"), 86, 2, col, K_SPACE);
  char b[40];
  snprintf_P(b, sizeof b, PSTR("%lu m + %d gems = %lu"), (unsigned long)runMeters(), runGems, (unsigned long)m);
  centerText(b, 116, 1, K_CREAM, K_SPACE);
  snprintf_P(b, sizeof b, PSTR("best %u"), P.runBest);
  centerText(b, 130, 1, K_CYAN, K_SPACE);
  centerText(FS("press: run again   hold: title"), 152, 1, K_DIM, K_SPACE);
  oledDirty = true;
}

// ---------- one step of the game ----------
void runStep(float dt, int clicks, bool tap) {
  runSpeed = min(10.0f * runLevelK(), runSpeed + 0.022f * runLevelK() * dt);
  runCam8 += (uint32_t)(runSpeed * dt * 256);
  while (runGen < (runCam8 >> 7) + 52) runGenRing();
  int zone = (int)(runMeters() / 500);
  if (zone != runZone) { runZone = zone; runZoneAt = millis(); runPalRoll = 99; SFX(T_WAVE); }
  if (runFalling) {                                  // through a gap: off into space
    runVH -= 8.0f * dt; runH += runVH * dt;
    if (runH < -1.3f) { if (--runLives > 0) runRescue(); else runOver(); }
    return;
  }
  if (clicks) runST += clicks * 50 * ENC_DIR;        // two clicks = one lane
  float a = min(1.0f, dt * 18);
  runS += (runST - runS) * a;
  if (runS > 148) { runWall = (runWall + 5) % 6; runS -= 296; runST -= 296; runRollT += 1.0471976f; SFX(T_SWISH); }
  else if (runS < -148) { runWall = (runWall + 1) % 6; runS += 296; runST += 296; runRollT -= 1.0471976f; SFX(T_SWISH); }
  runRoll += (runRollT - runRoll) * min(1.0f, dt * 14);
  if (fabsf(runRollT) > 100) { float k = 6.2831853f * (int)(runRollT / 6.2831853f); runRollT -= k; runRoll -= k; }   // keep it small
  if (tap && !runAir) { runAir = true; runVH = 3.7f; runCoyote = 0; SFX(T_HOP); }   // (a longer, floatier jump)
  if (runAir) {
    runVH -= 8.0f * dt; runH += runVH * dt;
    if (runH <= 0) { runH = 0; runVH = 0; runAir = false; }
  }
  if (!runAir) {
    uint32_t z = runCam8 + RUN_ZP8;
    uint8_t t = runTileAt(z, runWall, (int32_t)runS);
    if (t == 0 && millis() < runSafeUntil) t = 1;    // just rescued: the floor holds for a moment
    if (t == 0) {                                    // over a hole: a split second to still jump ("coyote time")
      if (!runCoyote) runCoyote = millis();
      else if (millis() - runCoyote > 140) { runFalling = true; runVH = 0; runCoyote = 0; mood = ";-;"; SFX(T_SPLASH); }
    } else runCoyote = 0;
    if (t == 2) {
      int lane = constrain((int)((((int32_t)runS + 148) * 3 * 7085) >> 21), 0, 2);
      WK.run.tiles[(z >> 7) & 63][runWall * 3 + lane] = 1;
      runGems++; mood = "^w^"; SFX(T_GEM);
    }
  }
  if (!runAir) runFrame = (millis() / 90) & 1;       // legs: 11 steps a second
}
void runRescue() {                                   // a life lost: a solid bridge appears under Micor
  runFalling = false; runAir = false; runH = 0; runVH = 0; runCoyote = 0;
  uint32_t ring = (runCam8 + RUN_ZP8) >> 7;
  for (uint32_t r = ring - 2; r < ring + 16; r++) for (int l = 0; l < 18; l++) WK.run.tiles[r & 63][l] = 1;
  runSafeUntil = millis() + 1500;
  runSpeed = max(4.2f * runLevelK(), runSpeed * 0.85f);
  mood = "^w^"; SFX(T_WAVE);
  runHud(true);
}
void runTick(uint32_t now) {
  int clicks = takeClicks();
  bool down = takePress();                           // jump on the press itself, not when the knob comes back up
  bool tap = takeShort();
  if (runState == RS_TITLE) {
    if (clicks) { P.runLevel = (P.runLevel + (clicks > 0 ? 1 : 2)) % 3; prefsSave(); uiClick(); runTitle(); }
    if (tap) runStart();
    return;
  }
  if (runState == RS_OVER) { if (tap) runStart(); return; }
  float dt = constrain((now - runLast) / 1000.0f, 0.0f, 0.06f);
  runLast = now;
  runStep(dt, clicks, down);
  if (runState != RS_PLAY) return;
  runRender();
  if (now - runHudAt >= 200) { runHudAt = now; runHud(false); }
  static uint32_t oledAt = 0;
  if (now - oledAt >= 1000) { oledAt = now; oledDirty = true; }
}
void runEnter() {
  wkOwner = APP_RUN;
  runTitle();
}
void runLeave() { runFreeSprites(); stopSound(); }
bool runWantsBack() { return runState != RS_TITLE; }
void runBack() { stopSound(); startAppMusic(APP_RUN); runTitle(); }
void oledRun() {
  char b[28];
  oledHead(FS("MICOR RUN"));
  snprintf_P(b, sizeof b, PSTR("%lum"), (unsigned long)runMeters()); oledBig(b);
  snprintf_P(b, sizeof b, PSTR("gems %d  speed %d"), runGems, (int)(runSpeed * 10)); oledLine(44, b);
  snprintf_P(b, sizeof b, PSTR("%s  best %u"), pgmItem(RUN_LEVELS_P, P.runLevel % 3), P.runBest); oledLine(56, b);
}

// ================= BOOKS: an e-book reader =================
// On the Psion, no PC needed: .txt / .md books of any size, read straight from flash a page at a time, and Word .docx
// (its text is pulled out once into /bkc and kept). Found in /books, /docs and the top folder. Turn = page, press =
// menu, hold = back. The place in every book is remembered (/bkpos.txt). With the relay (v9): the "Psion Books" folder
// in the PC's Documents - PDF, Word, Excel, PowerPoint - drawn exactly as on the PC (pictures, charts, cover pages),
// shown in the Web app's screen view (zoom, scroll; hold comes back here).
enum { BK_LIST, BK_PC, BK_READ, BK_MENU, BK_GOTO, BK_MSG, BK_IMG };
const int BK_VIS = 9, BK_ROWH = 20, BK_Y = 22, BK_TOP = 20, BK_BOT = 222;
#define BKL WK.bk
uint8_t bkMode = BK_LIST, bkMsgBack = BK_LIST;
int bkSel = 0, bkTop = 0, bkN = 0, bkPcN = 0, bkGoPct = 0, bkTurns = 0;
bool bkReturn = false, bkRaw = false;              // bkReturn: a PC book is in the screen view; bkRaw: the file is CP437
char bkPath[48] = "", bkSrc[48] = "", bkTitle[30] = "";
uint32_t bkSize = 0, bkOff = 0, bkNext = 0;
uint32_t bkHist[40];
uint8_t bkHn = 0;
uint16_t bkBg, bkInk, bkHead;
bool bkListMode() { return bkMode == BK_LIST || bkMode == BK_PC; }
uint16_t bkAccent() { return appAccent(APP_BOOKS); }
const char* bkBase(const char* p) { const char* b = strrchr(p, '/'); return b ? b + 1 : p; }
bool bkExt(const char* n, PGM_P e) { size_t l = strlen(n), k = strlen_P(e); return l > k && !strcasecmp_P(n + l - k, e); }
bool bkIsDocx(const char* n) { return bkExt(n, PSTR(".docx")) || bkExt(n, PSTR(".docm")); }
bool bkIsImg(const char* n) { return bkExt(n, PSTR(".bmp")); }

// ---------- the list ----------
void bkScanDir(const char* d) {
  Dir dir = LittleFS.openDir(d);
  while (dir.next() && bkN < 36) {
    if (dir.isDirectory()) continue;
    String fn = dir.fileName();
    const char* f = fn.c_str();
    if (f[0] == '~' || f[0] == '.' || !strcasecmp_P(f, PSTR("bkpos.txt"))) continue;
    if (!bkExt(f, PSTR(".txt")) && !bkExt(f, PSTR(".md")) && !bkIsDocx(f) && !bkIsImg(f)) continue;
    int n = snprintf_P(BKL.name[bkN], 48, PSTR("%s%s%s"), d, d[1] ? "/" : "", f);
    if (n < 48) bkN++;
  }
}
void bkScan() { bkN = 0; bkScanDir("/books"); bkScanDir("/docs"); bkScanDir("/"); }
int bkRows() { return bkMode == BK_PC ? bkPcN : bkN + 1; }
void bkDrawRow(int s) {
  int i = bkTop + s, y = BK_Y + s * BK_ROWH;
  bool on = i == bkSel;
  uint16_t acc = bkAccent(), bg = on ? acc : (s & 1 ? E_ROW2 : E_WHITE), fg = on ? E_ONACC : C_INK;
  tft.fillRect(4, y, 312, BK_ROWH, bg);
  if (i >= bkRows()) return;
  char t[48]; const char* tag;
  if (bkMode == BK_LIST && i == 0) { snprintf_P(t, sizeof t, PSTR("PC: the Psion Books folder")); tag = "PC"; }
  else {
    const char* n = BKL.name[bkMode == BK_PC ? i : i - 1];
    snprintf_P(t, sizeof t, PSTR("%.40s"), bkMode == BK_PC ? n : bkBase(n));
    const char* dot = strrchr(n, '.');
    tag = dot ? dot + 1 : "";
  }
  putText(12, y + 6, 1, fg, bg, t);
  char g[6]; int k = 0;
  for (; tag[k] && k < 4; k++) g[k] = toupper((uint8_t)tag[k]);
  g[k] = 0;
  putText(308 - k * 6, y + 6, 1, on ? E_ONACC : acc, bg, g);
  if (on) ptrListSet(PL_BOOKS, 4, BK_Y, 312, BK_ROWH, BK_VIS, bkTop, bkSel);
}
void bkDrawList() {
  bkMode = bkMode == BK_PC ? BK_PC : BK_LIST;
  tft.fillScreen(C_LCD);
  drawTitleBar(bkMode == BK_PC ? FS("Books on the PC") : FS("Books"), bkAccent());
  int n = bkRows();
  if (bkSel >= n) bkSel = max(0, n - 1);
  if (bkSel < bkTop) bkTop = bkSel;
  if (bkSel >= bkTop + BK_VIS) bkTop = bkSel - BK_VIS + 1;
  for (int s = 0; s < BK_VIS; s++) bkDrawRow(s);
  ptrListSet(PL_BOOKS, 4, BK_Y, 312, BK_ROWH, BK_VIS, bkTop, bkSel);
  if (bkMode == BK_LIST && !bkN) putText(12, BK_Y + BK_ROWH + 6, 1, C_MID, E_WHITE, FS("nothing yet: .txt, .docx or pictures in /books"));
  if (bkMode == BK_PC && !bkPcN) putText(12, BK_Y + 6, 1, C_MID, E_WHITE, FS("the folder is empty: Documents\\Psion Books"));
  putText(6, 204, 1, C_MID, C_LCD, FS("turn: choose  press: read  hold: back"));
  drawDock(APP_BOOKS);
  oledDirty = true;
}
void bkListMove(int c) {
  int n = bkRows();
  if (!n) return;
  int old = bkSel, oldTop = bkTop;
  bkSel = ((bkSel + c) % n + n) % n;
  if (bkSel < bkTop) bkTop = bkSel;
  else if (bkSel >= bkTop + BK_VIS) bkTop = bkSel - BK_VIS + 1;
  if (bkTop != oldTop) for (int s = 0; s < BK_VIS; s++) bkDrawRow(s);
  else { bkDrawRow(old - bkTop); bkDrawRow(bkSel - bkTop); }
  uiClick(); oledDirty = true;
}
void bkMsg(const char* a, const char* b, uint8_t back) {   // a message box; press goes back to the list
  tft.fillRect(20, 80, 280, 72, E_WHITE); tft.drawRect(20, 80, 280, 72, bkAccent());
  tft.fillRect(20, 80, 280, 14, bkAccent());
  putText(26, 83, 1, E_ONACC, bkAccent(), FS("Books"));
  putText(28, 102, 1, C_INK, E_WHITE, a);
  putText(28, 116, 1, C_MID, E_WHITE, b);
  putText(28, 136, 1, C_MID, E_WHITE, FS("press: OK"));
  bkMode = BK_MSG; bkMsgBack = back;
}

// ---------- where you were in each book (/bkpos.txt: "offset path" lines, newest first) ----------
uint32_t bkPosGet(const char* path) {
  File f = LittleFS.open(F("/bkpos.txt"), "r");
  if (!f) return 0;
  uint32_t r = 0;
  while (f.available()) {
    String l = f.readStringUntil('\n');
    int sp = l.indexOf(' ');
    if (sp > 0 && l.substring(sp + 1) == path) { r = strtoul(l.c_str(), nullptr, 10); break; }
  }
  f.close();
  return r;
}
void bkPosSet(const char* path, uint32_t off) {
  String keep;
  File f = LittleFS.open(F("/bkpos.txt"), "r");
  if (f) {
    for (int n = 0; f.available() && n < 24; ) {
      String l = f.readStringUntil('\n');
      int sp = l.indexOf(' ');
      if (sp > 0 && l.substring(sp + 1) != path) { keep += l; keep += '\n'; n++; }
    }
    f.close();
  }
  File w = LittleFS.open(F("/bkpos.txt"), "w");
  if (!w) return;
  w.printf("%lu %s\n", (unsigned long)off, path);
  w.print(keep);
  w.close();
}

// ---------- reading: one page laid out (and drawn) from a byte offset ----------
struct BkIn { File f; uint8_t b[128]; int n, i; uint32_t pos; } bkR;   // (one reader, shared)
int bkByte() {
  BkIn& r = bkR;
  if (r.i >= r.n) { r.n = r.f.read(r.b, sizeof r.b); r.i = 0; if (r.n <= 0) { r.n = 0; return -1; } }
  r.pos++;
  return r.b[r.i++];
}
int bkPeek() {
  BkIn& r = bkR;
  if (r.i >= r.n) { r.n = r.f.read(r.b, sizeof r.b); r.i = 0; if (r.n <= 0) { r.n = 0; return -1; } }
  return r.b[r.i];
}
int bkChar() {                                     // the next letter in the font's code page (-1 = end)
  int b = bkByte();
  if (b < 0x80 || bkRaw) return b;
  int need = (b & 0xE0) == 0xC0 ? 1 : (b & 0xF0) == 0xE0 ? 2 : (b & 0xF8) == 0xF0 ? 3 : 0;
  uint32_t u = need == 1 ? (b & 0x1F) : need == 2 ? (b & 0x0F) : (b & 0x07);
  int k = 0;
  for (; k < need; k++) { int nb = bkPeek(); if (nb < 0 || (nb & 0xC0) != 0x80) break; u = (u << 6) | (bkByte() & 0x3F); }
  if (!need || k < need) u = b;                      // not UTF-8: Latin-1 / Windows text
  if (u > 0xFFFF) return '?';
  uint8_t c = utf8ToCp437((uint16_t)u);
  return c ? c : (u == 0x2022 ? 7 : '?');
}
void bkColors() {
  switch (P.bkTheme % 3) {
    case 1: bkBg = RGB565(244, 236, 214); bkInk = RGB565(70, 50, 30); bkHead = RGB565(150, 70, 30); break;
    case 2: bkBg = RGB565(16, 16, 22); bkInk = RGB565(205, 200, 185); bkHead = RGB565(240, 170, 90); break;
    default: bkBg = 0xFFFF; bkInk = RGB565(24, 24, 30); bkHead = bkAccent();
  }
}
uint32_t bkLayout(uint32_t start, bool draw) {       // -> where the next page starts
  BkIn& r = bkR;
  r.f = LittleFS.open(bkSrc, "r");
  if (!r.f) return bkSize;
  r.f.seek(start); r.n = r.i = 0; r.pos = start;
  const int sz = P.bkFont ? 2 : 1, cols = sz == 1 ? 51 : 25, lh = sz == 1 ? 11 : 20, rows = (BK_BOT - BK_TOP - 2) / lh;
  char line[54], word[54];
  int ll = 0, wl = 0, row = 0;
  uint8_t style = 0;
  uint32_t wordAt = start, next = 0;
  bool full = false;
  if (draw) tft.fillRect(0, BK_TOP, W, BK_BOT - BK_TOP, bkBg);
  auto emit = [&]() {
    while (ll && line[ll - 1] == ' ') ll--;
    if (draw && ll) {
      line[ll] = 0;
      uint16_t c = style ? bkHead : bkInk;
      int y = BK_TOP + 3 + row * lh;
      putText(6, y, sz, c, bkBg, line);
      if (style) { tft.setTextSize(sz); tft.setTextColor(c); tft.setCursor(7, y); tft.print(line); }   // bold: drawn twice
    }
    row++; ll = 0;
  };
  auto flushWord = [&]() -> bool {                  // false: the page is full (the word starts the next one)
    if (!wl) return true;
    if (ll && ll + wl > cols) { emit(); if (row >= rows) { next = wordAt; return false; } }
    memcpy(line + ll, word, wl); ll += wl; wl = 0;
    return true;
  };
  for (;;) {
    uint32_t at = r.pos;
    int c = bkChar();
    if (c < 0) { if (!flushWord()) break; if (ll && row < rows) emit(); next = r.pos; break; }
    if (c == '\r') continue;
    if (c == 1 || c == 2) { if (!ll && !wl) style = c; continue; }
    if (c == '\f') { if (!flushWord()) { full = true; break; } if (ll) emit(); if (row) { next = r.pos; full = true; break; } continue; }
    if (c == '\n') {
      if (!flushWord()) { full = true; break; }
      if (!row && !ll) continue;                      // (no blank lines at the top of a page)
      emit(); style = 0;
      if (row >= rows) { next = r.pos; full = true; break; }
      continue;
    }
    if (c == ' ' || c == '\t') { if (!flushWord()) { full = true; break; } if (ll && ll < cols) line[ll++] = ' '; continue; }
    if (c < 32 && c != 7) continue;
    if (!wl) wordAt = at;
    word[wl++] = (char)c;
    if (wl >= cols) { if (!flushWord()) { full = true; break; } }
  }
  (void)full;
  r.f.close();
  return next > start ? next : min(bkSize, start + 1);
}
uint32_t bkAlign(uint32_t o) {                       // a good place to start a page: after a space or line end
  if (!o) return 0;
  File f = LittleFS.open(bkSrc, "r");
  if (!f) return o;
  f.seek(o);
  for (int k = 0; k < 300 && f.available(); k++) { int b = f.read(); o++; if (b == ' ' || b == '\n') break; }
  f.close();
  return min(o, bkSize);
}
void bkFooter() {
  int pct = bkSize ? (int)((uint64_t)bkOff * 100 / bkSize) : 100;
  if (bkNext >= bkSize) pct = 100;
  tft.fillRect(0, BK_BOT, W, H - BK_BOT, C_LCD);
  tft.fillRect(0, BK_BOT, W * pct / 100, 2, bkAccent());
  char b[60];
  snprintf_P(b, sizeof b, PSTR("%3d%%  %.30s"), pct, bkTitle);
  putText(6, BK_BOT + 6, 1, C_INK, C_LCD, b);
  putText(W - 6 - 13 * 6, BK_BOT + 6, 1, C_MID, C_LCD, bkNext >= bkSize ? FS("  the end :3") : FS("press: menu"));
}
void bkShow() {
  bkColors();
  bkNext = bkLayout(bkOff, true);
  bkFooter();
  oledDirty = true;
}
void bkSave() { if (bkPath[0]) bkPosSet(bkPath, bkOff); }
void bkPageNext(int k) {
  if (bkNext >= bkSize) { SFX(T_TICK); return; }
  while (k-- > 0 && bkNext < bkSize) {
    if (bkHn == 40) { memmove(bkHist, bkHist + 1, 39 * 4); bkHn = 39; }
    bkHist[bkHn++] = bkOff;
    bkOff = bkNext;
    if (k) bkNext = bkLayout(bkOff, false);
  }
  bkShow(); uiClick();
  if (++bkTurns % 5 == 0) bkSave();
}
void bkPagePrev(int k) {
  if (!bkOff) { SFX(T_TICK); return; }
  while (k-- > 0 && bkOff) {
    if (bkHn) { bkOff = bkHist[--bkHn]; continue; }
    uint32_t est = (bkNext > bkOff ? bkNext - bkOff : 1200) * 3 / 2 + 64;
    uint32_t t = bkOff > est ? bkAlign(bkOff - est) : 0, nx;
    while ((nx = bkLayout(t, false)) < bkOff && nx > t) t = nx;   // the page that ends where this one starts
    bkOff = t;
  }
  bkShow(); uiClick();
  if (++bkTurns % 5 == 0) bkSave();
}
void bkOpenLocal(const char* path) {
  snprintf_P(bkPath, sizeof bkPath, PSTR("%s"), path);
  snprintf_P(bkTitle, sizeof bkTitle, PSTR("%s"), bkBase(path));
  bkRaw = false;
  snprintf_P(bkSrc, sizeof bkSrc, PSTR("%s"), path);
  if (bkIsDocx(path)) {                              // a Word file: its text, pulled out once
    File f = LittleFS.open(path, "r");
    uint32_t h = 2166136261UL ^ (f ? f.size() : 0);
    if (f) f.close();
    for (const char* q = path; *q; q++) h = (h ^ (uint8_t)*q) * 16777619UL;
    snprintf_P(bkSrc, sizeof bkSrc, PSTR("/bkc/%08lx.txt"), (unsigned long)h);
    bkRaw = true;
    if (!LittleFS.exists(bkSrc)) {
      tft.fillRect(20, 96, 280, 40, E_WHITE); tft.drawRect(20, 96, 280, 40, bkAccent());
      putText(30, 104, 1, C_INK, E_WHITE, FS("Getting the book ready..."));
      putText(30, 118, 1, C_MID, E_WHITE, FS("(the first time only)"));
      LittleFS.mkdir("/bkc");
      int r = docxToText(path, bkSrc);
      if (r) {
        bkDrawList();
        bkMsg(r == 1 ? FS("That isn't a Word file I can read.") : r == 2 ? FS("Not enough memory right now.") : FS("Not enough space in flash."),
              FS("(try again after a restart)"), BK_LIST);
        return;
      }
    }
  }
  File f = LittleFS.open(bkSrc, "r");
  bkSize = f ? f.size() : 0;
  if (f) f.close();
  if (!bkSize) { bkDrawList(); bkMsg(FS("This book is empty."), FS(""), BK_LIST); return; }
  bkOff = bkPosGet(bkPath);
  if (bkOff >= bkSize) bkOff = 0;
  bkHn = 0; bkTurns = 0;
  bkMode = BK_READ;
  tft.fillScreen(C_LCD);
  drawTitleBar(bkTitle, bkAccent());
  bkShow();
}
void bkMenu() {
  static char it[6][28];
  static const char* p[6];
  snprintf_P(it[0], 28, PSTR("Go to..."));
  snprintf_P(it[1], 28, PSTR("Text: %s"), P.bkFont ? FS("large") : FS("small"));
  snprintf_P(it[2], 28, PSTR("Colours: %s"), pgmItem(PSTR("paper\nsepia\nnight"), P.bkTheme % 3));
  snprintf_P(it[3], 28, PSTR("Back to the start"));
  snprintf_P(it[4], 28, PSTR("Back to the list"));
  snprintf_P(it[5], 28, PSTR("Close menu"));
  for (int i = 0; i < 6; i++) p[i] = it[i];
  bkMode = BK_MENU;
  pmShow(bkTitle, p, 6, bkAccent());
}
void bkGotoDraw() {
  tft.fillRect(40, 90, 240, 60, E_WHITE); tft.drawRect(40, 90, 240, 60, bkAccent());
  char b[24]; snprintf_P(b, sizeof b, PSTR("Go to %3d%%"), bkGoPct);
  putText(56, 98, 2, C_INK, E_WHITE, b);
  tft.fillRect(56, 122, 208, 6, E_ROW2); tft.fillRect(56, 122, 208 * bkGoPct / 100, 6, bkAccent());
  putText(56, 136, 1, C_MID, E_WHITE, FS("turn: where   press: go"));
}
void bkReadRedraw() { tft.fillScreen(C_LCD); drawTitleBar(bkTitle, bkAccent()); bkShow(); bkMode = BK_READ; }

// ---------- pictures: .bmp (1, 4, 8, 16, 24 or 32 bits, not compressed), shrunk to fit the screen ----------
// PC remote's file manager turns uploaded .jpg / .png / .gif / .webp into screen-sized 16-bit .bmp files in the browser,
// so photos work without a JPEG decoder on the ESP8266. Turn = next / previous picture, press = name + size, hold = back.
uint32_t bmOff, bmRow;
int32_t bmW, bmH;
uint16_t bmBpp, bmComp;
bool bmTop, bm555, bmInfo = false;
uint16_t* bmPal() { return (uint16_t*)(BKL.io + 640); }   // 256 colours (the row buffer is BKL.io[0..640))
uint32_t bmU32(const uint8_t* b) { return b[0] | (b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24); }
const char* bmLoad(const char* path) {               // -> nullptr ok, else why not
  File f = LittleFS.open(path, "r");
  if (!f) return PSTR("can't open the file");
  uint8_t h[70];
  int n = f.read(h, sizeof h);
  if (n < 54 || h[0] != 'B' || h[1] != 'M') { f.close(); return PSTR("not a .bmp picture"); }
  bmOff = bmU32(h + 10);
  uint32_t hs = bmU32(h + 14);
  bmW = (int32_t)bmU32(h + 18); bmH = (int32_t)bmU32(h + 22);
  bmBpp = h[28] | (h[29] << 8); bmComp = (uint16_t)bmU32(h + 30);
  bmTop = bmH < 0; if (bmH < 0) bmH = -bmH;
  if (bmW <= 0 || bmH <= 0 || bmW > 8000 || bmH > 8000) { f.close(); return PSTR("odd picture size"); }
  if (!(bmComp == 0 || (bmComp == 3 && (bmBpp == 16 || bmBpp == 32))) ||
      !(bmBpp == 1 || bmBpp == 4 || bmBpp == 8 || bmBpp == 16 || bmBpp == 24 || bmBpp == 32)) { f.close(); return PSTR("compressed .bmp: save it plain"); }
  bm555 = bmBpp == 16 && !(bmComp == 3 && n >= 58 && bmU32(h + 54) == 0xF800);
  if (bmBpp <= 8) {                                  // the palette, as screen colours
    int k = 1 << bmBpp, used = (int)bmU32(h + 46);
    if (used > 0 && used < k) k = used;
    f.seek(14 + hs);
    uint8_t q[4];
    for (int i = 0; i < k; i++) { f.read(q, 4); bmPal()[i] = RGB565(q[2], q[1], q[0]); }
  }
  f.close();
  bmRow = (((uint32_t)bmW * bmBpp + 31) / 32) * 4;
  return nullptr;
}
void bmDraw(const char* path) {
  tft.fillScreen(0);
  int ow = bmW, oh = bmH;
  if (ow > W || oh > H) {                            // shrink to fit, keep the shape
    if ((int32_t)bmW * H > (int32_t)bmH * W) { ow = W; oh = max(1, (int)((int32_t)bmH * W / bmW)); }
    else { oh = H; ow = max(1, (int)((int32_t)bmW * H / bmH)); }
  }
  int x0 = (W - ow) / 2, y0 = (H - oh) / 2;
  uint16_t* row = (uint16_t*)BKL.io;
  BkIn& r = bkR;
  r.f = LittleFS.open(path, "r");
  if (!r.f) return;
  for (int oy = 0; oy < oh; oy++) {
    int32_t sy = (int32_t)oy * bmH / oh, fr = bmTop ? sy : bmH - 1 - sy;
    r.f.seek(bmOff + (uint32_t)fr * bmRow); r.n = r.i = 0; r.pos = 0;
    uint32_t at = 0;                                 // bytes of this row read so far
    int lastByte = 0;
    for (int ox = 0; ox < ow; ox++) {
      uint32_t sx = (uint32_t)ox * bmW / ow, bit = sx * bmBpp, need = bit / 8;
      while (at < need) { bkByte(); at++; }
      uint16_t c;
      if (bmBpp >= 24) {
        int b = bkByte(), g = bkByte(), rr = bkByte(); at += 3;
        if (bmBpp == 32) { bkByte(); at++; }
        c = RGB565(rr, g, b);
      } else if (bmBpp == 16) {
        int lo = bkByte(), hi = bkByte(); at += 2;
        uint16_t v = lo | (hi << 8);
        c = bm555 ? (uint16_t)(((v & 0x7C00) << 1) | ((v & 0x03E0) << 1) | (v & 0x1F)) : v;
      } else {
        if (at == need) { lastByte = bkByte(); at++; }  // (1 / 4 / 8 bits: several pixels share a byte)
        int sh = 8 - bmBpp - (int)(bit & 7), idx = (lastByte >> sh) & ((1 << bmBpp) - 1);
        c = bmPal()[idx];
      }
      row[ox] = c;
    }
    if (rcLive) { rcWindow(x0, y0 + oy, ow, 1); rcPixels(row, ow); }
    for (int i = 0; i < ow; i++) row[i] = (row[i] >> 8) | (row[i] << 8);
    tft.startWrite();
    tft.Adafruit_ST7789::setAddrWindow(x0, y0 + oy, ow, 1);
    rawPixels(row, ow);
    tft.endWrite();
    if ((oy & 15) == 15) yield();
  }
  r.f.close();
}
int bkImgIndex = -1;                                 // which list entry is on screen
void bkImgInfo() {
  char b[64];
  int k = 0, n = 0;
  for (int i = 0; i < bkN; i++) if (bkIsImg(BKL.name[i])) { n++; if (i <= bkImgIndex) k = n; }
  tft.fillRect(0, H - 16, W, 16, 0);
  snprintf_P(b, sizeof b, PSTR("%.30s  %ldx%ld  %d/%d"), bkBase(BKL.name[bkImgIndex]), (long)bmW, (long)bmH, k, n);
  putText(4, H - 12, 1, 0xFFFF, 0, b);
}
void bkOpenImg(int i) {
  bkImgIndex = i;
  const char* why = bmLoad(BKL.name[i]);
  if (why) { bkMode = BK_LIST; bkDrawList(); bkMsg(FS("Can't show that picture:"), fstr(why), BK_LIST); return; }
  bkMode = BK_IMG;
  bmDraw(BKL.name[i]);
  if (bmInfo) bkImgInfo();
  oledDirty = true;
}
void bkImgStep(int d) {                              // the next / previous picture in the list
  if (bkN < 2) return;
  int i = bkImgIndex;
  for (int k = 0; k < bkN; k++) {
    i = ((i + d) % bkN + bkN) % bkN;
    if (bkIsImg(BKL.name[i])) { if (i != bkImgIndex) { uiClick(); bkOpenImg(i); } return; }
  }
}
// ---------- the PC's "Psion Books" folder (relay v9) ----------
int bkGet(const char* path, char* out, int n, uint16_t ms) {   // a relay request -> text in out
  out[0] = 0;
  if (WiFi.status() != WL_CONNECTED) return -1;
  WiFiClient cl;
  HTTPClient http;
  http.useHTTP10(true);
  http.setTimeout(ms);
  char url[96];
  snprintf_P(url, sizeof url, PSTR("http://%s%s"), P.relay, path);
  if (!http.begin(cl, url)) return -1;
  int code = http.GET();
  if (code != 200) { http.end(); return code > 0 ? -code : -1; }
  WiFiClient* st = http.getStreamPtr();
  int len = 0;
  uint32_t t = millis();
  while (st && len < n - 1 && (http.connected() || st->available()) && millis() - t < ms) {
    int a = st->available();
    if (a) len += st->read((uint8_t*)out + len, min(a, n - 1 - len)); else { delay(5); yield(); }
  }
  out[len] = 0;
  http.end();
  return len;
}
void bkOpenPcList() {
  tft.fillRect(20, 96, 280, 30, E_WHITE); tft.drawRect(20, 96, 280, 30, bkAccent());
  putText(30, 107, 1, C_INK, E_WHITE, FS("Asking the PC for its books..."));
  int n = bkGet("/bk/list", BKL.io, sizeof BKL.io, 8000);
  if (n < 0) {
    bkDrawList();
    bkMsg(WiFi.status() != WL_CONNECTED ? FS("No WiFi: connect in the WiFi app.") : FS("The relay didn't answer."),
          FS("start the relay (v9) on the PC"), BK_LIST);
    return;
  }
  bkPcN = 0;
  for (char* l = BKL.io; *l && bkPcN < 36; ) {
    char* e = strchr(l, '\n');
    if (e) *e = 0;
    snprintf_P(BKL.name[bkPcN++], 48, PSTR("%s"), l);
    if (!e) break;
    l = e + 1;
  }
  bkMode = BK_PC; bkSel = 0; bkTop = 0;
  bkDrawList();
}
void bkOpenPc(int i) {
  tft.fillRect(20, 90, 280, 44, E_WHITE); tft.drawRect(20, 90, 280, 44, bkAccent());
  putText(30, 98, 1, C_INK, E_WHITE, FS("Getting it ready on the PC..."));
  putText(30, 112, 1, C_MID, E_WHITE, FS("(Office files take a few seconds)"));
  char path[24];
  snprintf_P(path, sizeof path, PSTR("/bk/prep?i=%d"), i);
  int n = bkGet(path, BKL.io, 200, 65000);
  if (n < 2 || strncmp_P(BKL.io, PSTR("ok"), 2)) {
    char* e = strchr(BKL.io, '\n');
    bkDrawList();
    bkMsg(FS("The PC couldn't open it:"), n < 0 ? FS("the relay didn't answer") : e ? e + 1 : BKL.io, BK_PC);
    return;
  }
  char u[64];
  snprintf_P(u, sizeof u, PSTR("http://%s/bv?i=%d"), P.relay, i);
  bkReturn = true;
  enterApp(APP_WEB);
  svOpen = true; svY = 0;
  svStart(PSTR("open?u="), u, false);
}

// ---------- the app ----------
void booksEnter() {
  wkOwner = APP_BOOKS;
  LittleFS.mkdir("/books");
  bkMode = BK_LIST;
  bkScan();
  bkDrawList();
}
bool booksWantsBack() { return bkMode != BK_LIST; }
void booksBack() {
  switch (bkMode) {
    case BK_READ: bkSave(); bkMode = BK_LIST; bkScan(); bkDrawList(); break;
    case BK_MENU: case BK_GOTO: bkReadRedraw(); break;
    case BK_MSG: bkMode = bkMsgBack; if (bkMode == BK_LIST) bkScan(); bkDrawList(); break;
    case BK_IMG: bkMode = BK_LIST; bkSel = bkImgIndex + 1; bkDrawList(); break;
    default: bkMode = BK_LIST; bkScan(); bkDrawList();
  }
}
void booksTick(uint32_t now) {
  int c = takeClicks() * ENC_DIR;
  takePress();
  bool sp = takeShort();
  (void)now;
  switch (bkMode) {
    case BK_LIST: case BK_PC:
      titleTimeTick();
      if (c) bkListMove(c);
      if (sp) {
        if (bkMode == BK_LIST) {
          if (bkSel == 0) bkOpenPcList();
          else if (bkSel - 1 < bkN) { if (bkIsImg(BKL.name[bkSel - 1])) bkOpenImg(bkSel - 1); else bkOpenLocal(BKL.name[bkSel - 1]); }
        }
        else if (bkSel < bkPcN) bkOpenPc(bkSel);
      }
      break;
    case BK_READ:
      if (c > 0) bkPageNext(min(c, 5)); else if (c < 0) bkPagePrev(min(-c, 5));
      if (sp) bkMenu();
      break;
    case BK_MENU: {
      int r = pmTick(c, sp);
      if (r < 0) break;
      if (r == 0) { bkGoPct = bkSize ? (int)((uint64_t)bkOff * 100 / bkSize) : 0; bkReadRedraw(); bkMode = BK_GOTO; bkGotoDraw(); break; }
      if (r == 1) { P.bkFont ^= 1; prefsSave(); bkHn = 0; }
      if (r == 2) { P.bkTheme = (P.bkTheme + 1) % 3; prefsSave(); }
      if (r == 3) { bkOff = 0; bkHn = 0; }
      if (r == 4) { bkSave(); bkMode = BK_LIST; bkScan(); bkDrawList(); break; }
      bkReadRedraw();
      if (r == 1 || r == 2) bkMenu();                // (see the change, keep choosing)
      break;
    }
    case BK_GOTO:
      if (c) { bkGoPct = constrain(bkGoPct + (abs(c) > 1 ? c * 5 : c), 0, 100); bkGotoDraw(); uiClick(); }
      if (sp) {
        bkHn = 0;
        bkOff = bkGoPct >= 100 ? bkSize : bkAlign((uint32_t)((uint64_t)bkSize * bkGoPct / 100));
        if (bkOff >= bkSize) { bkNext = bkSize; bkMode = BK_READ; bkPagePrev(1); break; }   // the end: show the last page
        bkReadRedraw();
      }
      break;
    case BK_MSG:
      if (sp) booksBack();
      break;
    case BK_IMG:
      if (c) bkImgStep(c > 0 ? 1 : -1);
      if (sp) { bmInfo = !bmInfo; if (bmInfo) bkImgInfo(); else bmDraw(BKL.name[bkImgIndex]); }
      break;
  }
}
void oledBooks() {
  char l[4][24];
  if (bkMode == BK_IMG && bkImgIndex >= 0) {
    snprintf_P(l[0], 24, PSTR("%.21s"), bkBase(BKL.name[bkImgIndex]));
    snprintf_P(l[1], 24, PSTR("%ld x %ld, %u bits"), (long)bmW, (long)bmH, bmBpp);
    snprintf_P(l[2], 24, PSTR("turn: next picture"));
    snprintf_P(l[3], 24, PSTR("press: name  hold: back"));
  } else if (bkMode == BK_LIST || bkMode == BK_PC || bkMode == BK_MSG) {
    snprintf_P(l[0], 24, PSTR("%d on the Psion"), bkN);
    snprintf_P(l[1], 24, PSTR("txt, md, docx, bmp"));
    snprintf_P(l[2], 24, PSTR("PC: PDF, Office"));
    snprintf_P(l[3], 24, PSTR("press: read"));
  } else {
    int pct = bkSize ? (int)((uint64_t)bkOff * 100 / bkSize) : 0;
    snprintf_P(l[0], 24, PSTR("%.21s"), bkTitle);
    snprintf_P(l[1], 24, PSTR("%d%% read"), bkNext >= bkSize ? 100 : pct);
    snprintf_P(l[2], 24, PSTR("text %s, %s"), P.bkFont ? FS("large") : FS("small"), pgmItem(PSTR("paper\nsepia\nnight"), P.bkTheme % 3));
    snprintf_P(l[3], 24, PSTR("turn: page"));
  }
  oledCard(FS("BOOKS"), l[0], l[1], l[2], l[3]);
}

// ================= PC REMOTE: see + drive the Psion from a PC browser, and its files over WiFi =================
// WiFi app > "PC remote + files" turns it on (it shows the address + PIN). On the PC open http://<Psion IP>/
//  - port 80: the page, knob + keyboard input (/api/key) and the file manager (/api/ls get put rm mv)
//  - port 81: one viewer's screen stream. The ESP8266 has no RAM for a copy of the screen, so it sends the drawing
//    calls themselves (boxes, pixels, text characters, picture rows) and the OLED, and the browser redraws them.
//    When a viewer connects, the app on screen redraws itself once. Games aren't mirrored (far too much data).
// Message format (little-endian): 1 box x2 y1 w2 h1 c2 | 2 pixel x2 y1 c2 | 3 char x2 y2 ch1 fg2 bg2 size1 |
// 4 window x2 y2 w2 h2 | 5 pixels n2 + n colors | 6 OLED 1024 bytes | 7 status len1 + text | 8 clear
ESP8266WebServer* rcWeb = nullptr;
WiFiServer* rcSrv = nullptr;
WiFiClient rcCli;
uint8_t* rcBuf = nullptr;                           // only allocated while a viewer is connected
uint16_t rcLen = 0;
const uint16_t RC_BUF = 1400;
bool rcWantRedraw = false, rcFsChanged = false, rcUpOk = false, rcBusy = false;
uint16_t rcWx, rcWy, rcWw, rcWh;
uint8_t rcWs = 1;                                   // the window's pixel size (2: each pixel is 2 x 2 on the PC)
bool rcWinNew = false;
uint32_t rcSentAt = 0;
File rcUp;
const char RC_PAGE[] PROGMEM = R"rcpage(<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Psion remote</title><style>
:root{--bg:#14161c;--card:#1e2129;--line:#2e3340;--ink:#e8eaf0;--mid:#9aa1b2;--acc:#8b6cff;--ok:#3ec484;--bad:#e84c3d}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:14px system-ui,sans-serif}
header{display:flex;gap:12px;align-items:center;padding:10px 16px;border-bottom:1px solid var(--line)}
h1{font-size:16px;margin:0}#dot{width:9px;height:9px;border-radius:50%;background:var(--bad)}#st{color:var(--mid)}
main{display:flex;flex-wrap:wrap;gap:16px;padding:16px}.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:12px}
#left{flex:0 1 664px;min-width:0}#right{flex:1 1 320px;min-width:0}
canvas{image-rendering:pixelated;display:block;width:100%;background:#000;border-radius:6px}
#tft{max-width:640px;aspect-ratio:4/3;outline:none;cursor:default}#tft:focus{box-shadow:0 0 0 2px var(--acc)}
#oled{max-width:384px;aspect-ratio:2/1;margin-top:10px}
.row{display:flex;flex-wrap:wrap;gap:8px;margin-top:10px;align-items:center}
button,input,select{font:inherit;color:var(--ink);background:#262a35;border:1px solid var(--line);border-radius:7px;padding:7px 11px}
button:hover{border-color:var(--acc)}button.p{background:var(--acc);border-color:var(--acc);color:#fff}
input[type=text]{flex:1;min-width:120px}.hint{color:var(--mid);font-size:12px;margin-top:8px;line-height:1.5}
table{width:100%;border-collapse:collapse;margin-top:8px}td{padding:5px 4px;border-top:1px solid var(--line);word-break:break-all}
td.s{color:var(--mid);text-align:right;white-space:nowrap}td.a{white-space:nowrap;text-align:right}td.a button{padding:3px 7px;font-size:12px}
#bar{height:6px;background:#262a35;border-radius:3px;overflow:hidden;margin-top:6px}#bar i{display:block;height:100%;background:var(--acc)}
#pinbox{position:fixed;inset:0;background:#000a;display:none;align-items:center;justify-content:center}
#pinbox .card{width:280px}#pinbox input{width:100%;margin:10px 0;font-size:22px;text-align:center;letter-spacing:6px}
#note{color:var(--mid);margin-top:6px;min-height:18px}
</style></head><body>
<header><div id="dot"></div><h1>Psion remote</h1><span id="st">connecting...</span></header>
<main><section id="left" class="card"><canvas id="tft" width="320" height="240" tabindex="0"></canvas>
<div id="note"></div><canvas id="oled" width="128" height="64"></canvas>
<div class="row"><button data-k="L">&#8634; turn</button><button data-k="R">turn &#8635;</button>
<button class="p" data-k="P">press</button><button data-k="H">hold (back)</button><button id="redraw">redraw</button></div>
<div class="row"><input id="txt" type="text" placeholder="type text into the Psion..."><button id="send">send</button></div>
<div class="hint">The mouse works on the Psion screen: point and click on web pages (links, menus, text boxes),
popup menus, the home screen, and Word + Excel (menus, toolbar, paragraphs, cells; click twice to type).
Mouse wheel or arrow keys = turn the knob, Enter = press, right-click or Esc = hold (back).
Letters type into any text box that is open on the Psion. Word .docx and Excel .xlsx files uploaded to /
go to /docs and /sheets.</div></section>
<section id="right" class="card"><div class="row" style="margin-top:0"><b style="flex:1">Files</b><button id="ref">refresh</button></div>
<div id="bar"><i></i></div><div class="hint" id="space"></div>
<div class="row"><select id="dir"></select><input id="up" type="file" multiple><button class="p" id="upb">upload</button></div>
<table id="files"></table></section></main>
<div id="pinbox"><div class="card"><b>PIN</b><div class="hint" id="pinmsg">The PIN is on the Psion: WiFi &gt; PC remote.</div>
<input id="pin" inputmode="numeric" maxlength="4"><button class="p" id="pinok" style="width:100%">connect</button></div></div>
<script>
const $=s=>document.querySelector(s),W=320,H=240;
const FH='00000000003e5b4f5b3e3e6b4f6b3e1c3e7c3e1c183c7e3c181c577d571c1c5e7f5e1c00183c1800ffe7c3e7ff0018241800ffe7dbe7ff30483a060e2629792926407f050507407f05253f5a3ce73c5a7f3e1c1c08081c1c3e7f14227f22145f5f005f5f06097f017f006689956a606060606094a2ffa29408047e040810207e201008082a1c08081c2a08081e101010100c1e0c1e0c30383e3830060e3e0e06000000000000005f00000007000700147f147f14242a7f2a12231308646236495620500008070300001c2241000041221c002a1c7f1c2a08083e080800807030000808080808000060600020100804023e5149453e00427f400072494949462141494d331814127f1027454545393c4a49493141211109073649494936464949291e0000140000004034000000081422411414141414004122140802015909063e415d594e7c1211127c7f494949363e414141227f4141413e7f494949417f090909013e414151737f0808087f00417f41002040413f017f081422417f404040407f021c027f7f0408107f3e4141413e7f090909063e4151215e7f09192946264949493203017f01033f4040403f1f2040201f3f4038403f631408146303047804036159494d43007f4141410204081020004141417f04020102044040404040000307080020545478407f284444383844444428384444287f385454541800087e090218a4a49c787f0804047800447d40002040403d007f1028440000417f40007c047804787c080404783844444438fc1824241818242418fc7c08040408485454542404043f44243c4040207c1c2040201c3c4030403c44281028444c9090907c4464544c4400083641000000770000004136080002010204023c2623263c1ea1a161123a4040207a385454555921555579412254547842215554784020545579400c1e5272123955555559395454545939555454580000457c410002457d420001457c407d1211127df0282528f07c545545002054547c547c0a097f4932494949323a4444443a324a4848303a4141217a3a42402078009da0a07d3d4242423d3d4040403d3c24ff2424487e4943662b2ffc2f2bff0929f620c0887e090320545479410000447d413048484a32384040227a007a0a0a727d0d19317d2629292f28262929292630484d4020380808080808080808382f10c8acba2f102834fa00007b000008142a142222142a14085500550055aa55aa55aaff55ff55ff000000ff00101010ff00141414ff001010ff00ff1010f010f0141414fc001414f700ff0000ff00ff1414f404fc141417101f10101f101f1414141f00101010f0000000001f101010101f10101010f010000000ff101010101010101010ff10000000ff140000ff00ff00001f10170000fc04f414141710171414f404f40000ff00f714141414141414f700f7141414171410101f101f141414f4141010f010f000001f101f0000001f14000000fc140000f010f01010ff10ff141414ff141010101f00000000f010fffffffffff0f0f0f0f0ffffff0000000000ffff0f0f0f0f0f3844443844fc4a4a4a347e02020606027e027e0263554941633844443c04407e201e2006027e020299a5e7a5991c2a492a1c4c7201724c304a4d4d303048784830bc625a463d3e494949007e0101017e2a2a2a2a2a44445f444440514a444040444a51400000ff0103e080ff000008086b6b083612362436060f090f06000018180000001010003040ff0101001f01011e00191d1712003c3c3c3c0000000000';const F=new Uint8Array(1280);for(let i=0;i<1280;i++)F[i]=parseInt(FH.substr(i*2,2),16);
let pin=localStorage.getItem('psionPin')||'';
const tft=$('#tft'),tc=tft.getContext('2d'),img=tc.createImageData(W,H),px=new Uint32Array(img.data.buffer);
const oc=$('#oled'),oxc=oc.getContext('2d'),oim=oxc.createImageData(128,64),opx=new Uint32Array(oim.data.buffer);
let dirty=1,odirty=1,wx=0,wy=0,ww=1,wh=1,wi=0,ws=1;
const rgb=c=>{const r=c>>11&31,g=c>>5&63,b=c&31;return(0xff000000|(b<<3|b>>2)<<16|(g<<2|g>>4)<<8|(r<<3|r>>2))>>>0};
function rect(x,y,w,h,c){const v=rgb(c);for(let j=y;j<y+h&&j<H;j++)px.fill(v,j*W+x,j*W+Math.min(W,x+w));dirty=1}
function chr(x,y,ch,fg,bg,s){const f=rgb(fg),b=rgb(bg),o=fg!==bg;
for(let i=0;i<6;i++){let l=i<5?F[ch*5+i]:0;for(let j=0;j<8;j++,l>>=1){const on=l&1;if(!on&&!o)continue;const v=on?f:b;
for(let a=0;a<s;a++)for(let d=0;d<s;d++){const X=x+i*s+a,Y=y+j*s+d;if(X>=0&&Y>=0&&X<W&&Y<H)px[Y*W+X]=v}}}dirty=1}
function oled(b,p){for(let y=0;y<64;y++)for(let x=0;x<128;x++)opx[y*128+x]=(b[p+x+(y>>3)*128]>>(y&7))&1?0xfffff0c8:0xff100c08;odirty=1}
const u16=(b,p)=>b[p]|b[p+1]<<8,i16=(b,p)=>(b[p]|b[p+1]<<8)<<16>>16;
function parse(b){let p=0;for(;;){if(p>=b.length)return p;const op=b[p],n=b.length-p;
if(op==1){if(n<9)return p;rect(u16(b,p+1),b[p+3],u16(b,p+4),b[p+6],u16(b,p+7));p+=9}
else if(op==2){if(n<6)return p;const x=u16(b,p+1),y=b[p+3];if(x<W&&y<H){px[y*W+x]=rgb(u16(b,p+4));dirty=1}p+=6}
else if(op==3){if(n<11)return p;chr(i16(b,p+1),i16(b,p+3),b[p+5],u16(b,p+6),u16(b,p+8),b[p+10]||1);p+=11}
else if(op==4||op==9){const L=op==9?10:9;if(n<L)return p;wx=u16(b,p+1);wy=u16(b,p+3);ww=Math.max(1,u16(b,p+5));wh=u16(b,p+7);ws=op==9?b[p+9]:1;ww=Math.max(1,Math.round(ww/ws));wi=0;p+=L}
else if(op==5){if(n<3)return p;const k=u16(b,p+1);if(n<3+2*k)return p;for(let i=0;i<k;i++,wi++){const v=rgb(u16(b,p+3+2*i)),X0=wx+(wi%ww)*ws,Y0=wy+(wi/ww|0)*ws;
for(let a=0;a<ws;a++)for(let c=0;c<ws;c++){const X=X0+c,Y=Y0+a;if(X<W&&Y<H)px[Y*W+X]=v}}dirty=1;p+=3+2*k}
else if(op==6){if(n<1025)return p;oled(b,p+1);p+=1025}
else if(op==7){if(n<2||n<2+b[p+1])return p;const t=new TextDecoder().decode(b.subarray(p+2,p+2+b[p+1]));$('#st').textContent=t;
p+=2+b[p+1]}
else if(op==8){px.fill(0xff000000);dirty=1;p+=1}else{return b.length}}}
function draw(){if(dirty){tc.putImageData(img,0,0);dirty=0}if(odirty){oxc.putImageData(oim,0,0);odirty=0}requestAnimationFrame(draw)}draw();
async function stream(){if(!pin){askPin('');return}
try{const r=await fetch('http://'+location.hostname+':81/?pin='+pin,{cache:'no-store'});
if(r.status==403){askPin('Wrong PIN - check the Psion.');return}
$('#dot').style.background='var(--ok)';const rd=r.body.getReader();let buf=new Uint8Array(0);
for(;;){const{value,done}=await rd.read();if(done)break;const nb=new Uint8Array(buf.length+value.length);nb.set(buf);nb.set(value,buf.length);
buf=nb.subarray(parse(nb))}}catch(e){}
$('#dot').style.background='var(--bad)';$('#st').textContent='reconnecting...';setTimeout(stream,1500)}
let q=Promise.resolve();
function key(k,x=''){q=q.then(()=>fetch('/api/key?pin='+pin+'&k='+k+x).then(r=>{if(r.status==403)askPin('Wrong PIN.')}).catch(()=>{}))}
document.querySelectorAll('[data-k]').forEach(b=>b.onclick=()=>key(b.dataset.k));
$('#redraw').onclick=()=>fetch('/api/redraw?pin='+pin);
let wd=0,wt=0;tft.addEventListener('wheel',e=>{e.preventDefault();wd+=e.deltaMode?e.deltaY*40:e.deltaY;
if(!wt)wt=setTimeout(()=>{const d=Math.round(wd);wd=0;wt=0;if(d)key('W','&d='+d)},40)},{passive:false});
const at=e=>{const r=tft.getBoundingClientRect();return'&x='+Math.min(W-1,Math.max(0,Math.floor((e.clientX-r.left)*W/r.width)))+
'&y='+Math.min(H-1,Math.max(0,Math.floor((e.clientY-r.top)*H/r.height)))};
let mv='',mb=0;function move(){if(mb||!mv||!pin)return;const a=mv;mv='';mb=1;
fetch('/api/key?pin='+pin+'&k=M'+a).catch(()=>{}).finally(()=>{mb=0;setTimeout(move,30)})}
tft.onmousemove=e=>{mv=at(e);move()};
tft.onclick=e=>{tft.focus();mv='';key('C',at(e))};tft.oncontextmenu=e=>{e.preventDefault();key('H')};
tft.onkeydown=e=>{const k=e.key;if(k=='ArrowRight'||k=='ArrowDown')key('R');else if(k=='ArrowLeft'||k=='ArrowUp')key('L');
else if(k=='Enter')key('E');else if(k=='Escape')key('H');else if(k=='Backspace')key('B');
else if(k.length==1&&!e.ctrlKey&&!e.metaKey&&!e.altKey)key('T','&t='+encodeURIComponent(k));else return;e.preventDefault()};
$('#send').onclick=()=>{const t=$('#txt').value;if(t){key('T','&t='+encodeURIComponent(t));$('#txt').value=''}};
$('#txt').onkeydown=e=>{if(e.key=='Enter')$('#send').onclick()};
function askPin(m){$('#pinmsg').textContent=m||'The PIN is on the Psion: WiFi > PC remote.';$('#pinbox').style.display='flex';$('#pin').focus()}
$('#pinok').onclick=()=>{pin=$('#pin').value.trim();localStorage.setItem('psionPin',pin);$('#pinbox').style.display='none';stream();files()};
$('#pin').onkeydown=e=>{if(e.key=='Enter')$('#pinok').onclick()};
const kb=s=>s<1024?s+' B':(s/1024).toFixed(s<10240?1:0)+' KB';
async function files(){if(!pin)return;try{const r=await fetch('/api/ls?pin='+pin);if(r.status==403){askPin('Wrong PIN.');return}
const d=await r.json(),t=$('#files'),dirs=new Set(['/']);t.innerHTML='';
d.files.sort((a,b)=>a.p.localeCompare(b.p)).forEach(f=>{dirs.add(f.p.slice(0,f.p.lastIndexOf('/')+1)||'/');const tr=t.insertRow();
tr.insertCell().textContent=f.p;const s=tr.insertCell();s.className='s';s.textContent=kb(f.s);const a=tr.insertCell();a.className='a';
const g=document.createElement('button');g.textContent='get';g.onclick=()=>location.href='/api/get?pin='+pin+'&p='+encodeURIComponent(f.p);
const m=document.createElement('button');m.textContent='rename';m.onclick=async()=>{const n=prompt('New name (full path):',f.p);
if(n&&n!=f.p){await fetch('/api/mv?pin='+pin+'&p='+encodeURIComponent(f.p)+'&to='+encodeURIComponent(n));files()}};
const x=document.createElement('button');x.textContent='delete';x.onclick=async()=>{if(confirm('Delete '+f.p+'?')){
await fetch('/api/rm?pin='+pin+'&p='+encodeURIComponent(f.p));files()}};a.append(g,' ',m,' ',x)});
const sel=$('#dir'),cur=sel.value;sel.innerHTML='';[...dirs].sort().forEach(v=>sel.add(new Option(v,v)));if(cur)sel.value=cur;
$('#bar i').style.width=(100*d.used/d.total).toFixed(1)+'%';$('#space').textContent=kb(d.used)+' used of '+kb(d.total)+' - '+d.files.length+' files'}catch(e){}}
$('#ref').onclick=files;
async function toBmp(f){const im=await createImageBitmap(f),s=Math.min(1,320/im.width,240/im.height),w=Math.max(1,Math.round(im.width*s)),h=Math.max(1,Math.round(im.height*s)),c=document.createElement('canvas');c.width=w;c.height=h;const x=c.getContext('2d');x.drawImage(im,0,0,w,h);const p=x.getImageData(0,0,w,h).data,rb=(w*2+3)&~3,sz=66+rb*h,b=new DataView(new ArrayBuffer(sz));b.setUint16(0,0x4D42,true);b.setUint32(2,sz,true);b.setUint32(10,66,true);b.setUint32(14,40,true);b.setInt32(18,w,true);b.setInt32(22,-h,true);b.setUint16(26,1,true);b.setUint16(28,16,true);b.setUint32(30,3,true);b.setUint32(34,rb*h,true);b.setUint32(54,0xF800,true);b.setUint32(58,0x07E0,true);b.setUint32(62,0x001F,true);for(let y=0;y<h;y++)for(let i=0;i<w;i++){const o=(y*w+i)*4;b.setUint16(66+y*rb+i*2,((p[o]&0xF8)<<8)|((p[o+1]&0xFC)<<3)|(p[o+2]>>3),true)}return new Blob([b.buffer])}
$('#upb').onclick=async()=>{const fs=$('#up').files;for(let f of fs){const pic=/\.(jpe?g|png|gif|webp)$/i.test(f.name);if(pic)f=new File([await toBmp(f)],f.name.replace(/\.[^.]+$/,'.bmp'));const fd=new FormData();fd.append('f',f,f.name);
let d=$('#dir').value||'/';if(d=='/'){if(/\.docx?$/i.test(f.name))d='/docs/';else if(/\.(xlsx|csv)$/i.test(f.name))d='/sheets/'}
await fetch('/api/put?pin='+pin+'&d='+encodeURIComponent(d),{method:'POST',body:fd})}$('#up').value='';files()};
stream();files();tft.focus();
</script></body></html>)rcpage";

bool rcViewer() { return rcBuf && rcCli.connected(); }
void rcDrop() { rcCli.stop(); free(rcBuf); rcBuf = nullptr; rcLen = 0; rcLive = false; }
void rcFlush() {
  if (!rcBuf || !rcLen) return;
  if (!rcCli.connected() || rcCli.write(rcBuf, rcLen) != rcLen) { rcDrop(); return; }   // gone or stuck: let it go
  rcLen = 0; rcSentAt = millis();
}
void rcPut(const uint8_t* d, uint16_t n) {
  while (rcBuf && n) {
    if (rcLen == RC_BUF) { rcFlush(); continue; }
    uint16_t k = min((uint16_t)(RC_BUF - rcLen), n);
    memcpy(rcBuf + rcLen, d, k); rcLen += k; d += k; n -= k;
  }
}
void rcRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
  if (rcMute || !rcBuf) return;
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > W) w = W - x;
  if (y + h > H) h = H - y;
  if (w <= 0 || h <= 0) return;
  uint8_t b[9] = {1, (uint8_t)x, (uint8_t)(x >> 8), (uint8_t)y, (uint8_t)w, (uint8_t)(w >> 8), (uint8_t)h, (uint8_t)c, (uint8_t)(c >> 8)};
  rcPut(b, 9);
}
void rcPixel(int16_t x, int16_t y, uint16_t c) {
  if (rcMute || !rcBuf || x < 0 || y < 0 || x >= W || y >= H) return;
  uint8_t b[6] = {2, (uint8_t)x, (uint8_t)(x >> 8), (uint8_t)y, (uint8_t)c, (uint8_t)(c >> 8)};
  rcPut(b, 6);
}
void rcChar(int16_t x, int16_t y, uint8_t ch, uint16_t fg, uint16_t bg, uint8_t size) {
  if (rcMute || !rcBuf || x >= W || y >= H || x + 6 * size <= 0 || y + 8 * size <= 0) return;
  uint8_t b[11] = {3, (uint8_t)x, (uint8_t)(x >> 8), (uint8_t)y, (uint8_t)(y >> 8), ch, (uint8_t)fg, (uint8_t)(fg >> 8), (uint8_t)bg, (uint8_t)(bg >> 8), size};
  rcPut(b, 11);
}
void rcWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h) { rcWx = x; rcWy = y; rcWw = w; rcWh = h; rcWs = 1; rcWinNew = true; }
void rcWindowS(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t sc) { (void)sc; rcWindow(x, y, w, h); }   // (plain windows only)
// a picture row for the PC viewer: n pixels of src (every step-th), each sc x sc on the PC
void rcRowOut(int x0, int y, const uint8_t* src, bool color, int n, uint16_t ink, uint16_t paper, int sc, int step) {
  if (!rcBuf || rcMute) return;
  rcWindow(x0, y, n * sc, sc);
  uint16_t buf[40];
  for (int r = 0; r < sc; r++)
    for (int i = 0; i < n; ) {
      int k = min(40 / sc, n - i);
      for (int j = 0; j < k; j++) {
        int xx = (i + j) * step;
        uint16_t c = color ? c332(src[xx]) : ((src[xx >> 3] & (0x80 >> (xx & 7))) ? ink : paper);
        for (int q = 0; q < sc; q++) buf[j * sc + q] = c;
      }
      rcPixels(buf, k * sc);
      i += k;
    }
}
void rcPixels(const uint16_t* p, uint32_t n) {
  if (rcMute || !rcBuf || !n) return;
  if (rcWinNew) {                                   // op 4 = a window, op 9 = a window of big (rcWs x rcWs) pixels
    uint8_t b[10] = {(uint8_t)(rcWs > 1 ? 9 : 4), (uint8_t)rcWx, (uint8_t)(rcWx >> 8), (uint8_t)rcWy, (uint8_t)(rcWy >> 8), (uint8_t)rcWw, (uint8_t)(rcWw >> 8), (uint8_t)rcWh, (uint8_t)(rcWh >> 8), rcWs};
    rcPut(b, rcWs > 1 ? 10 : 9); rcWinNew = false;
  }
  while (n && rcBuf) {
    uint16_t k = (uint16_t)min(n, (uint32_t)600);
    uint8_t b[3] = {5, (uint8_t)k, (uint8_t)(k >> 8)};
    rcPut(b, 3); rcPut((const uint8_t*)p, k * 2);
    p += k; n -= k;
  }
}
void rcOled(const uint8_t* buf) { if (!rcBuf) return; uint8_t op = 6; rcPut(&op, 1); rcPut(buf, 1024); }
void rcStatus() {                                   // the app name for the viewer's title line
  if (!rcBuf) return;
  char t[48];
  snprintf_P(t, sizeof t, PSTR("%s%s"), app == APP_MENU ? FS("System") : appName(app - 1), gameApp(app) ? FS(" (games aren't mirrored)") : FS(""));
  uint8_t h[2] = {7, (uint8_t)strlen(t)};
  rcPut(h, 2); rcPut((const uint8_t*)t, h[1]);
}
void rcAppChanged() { rcLive = rcViewer() && !gameApp(app); rcStatus(); }   // games aren't mirrored (too much data)
bool rcUiBusy() {                                   // a popup or a keyboard is open: redrawing the app would lose it
  return pmOpen || okOpen || fpOpen || (app == APP_WEB && wbAsk != WA_NONE) || (app == APP_WIFI && spPage == SP_KEYS) || gameSetupActive;
}
void rcRedraw() {                                   // the viewer just connected (or asked): draw the screen once more
  if (!rcLive) { rcStatus(); return; }
  if (app == APP_WEB && wbPage == WB_SCREEN && svOpen) { svRefresh(); return; }
  if (app == APP_WEB && wbPage == WB_CAST) { csTitle(); return; }   // (screen share: the picture keeps coming)
  enterApp(app);
}
bool rcAuth() {
  if ((uint16_t)rcWeb->arg(F("pin")).toInt() == P.rcPin) return true;
  rcWeb->send(403, F("text/plain"), F("wrong PIN"));
  return false;
}
void rcTypeUtf8(const String& s) {                  // UTF-8 from the browser -> the font's characters
  for (unsigned i = 0; i < s.length(); ) {
    uint8_t b = s[i];
    uint16_t u = b;
    int len = b < 0x80 ? 1 : (b & 0xE0) == 0xC0 ? 2 : (b & 0xF0) == 0xE0 ? 3 : 4;
    if (len == 2 && i + 1 < s.length()) u = ((b & 0x1F) << 6) | (s[i + 1] & 0x3F);
    else if (len == 3 && i + 2 < s.length()) u = ((b & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F);
    i += len;
    uint8_t c = utf8ToCp437(u);
    if (u == '\n') c = '\n';
    if (c) kbRemotePush((char)c);
  }
}
bool rcTextBox() { return okOpen || millis() - kbLastTick < 200 || (app == APP_CALC && cPage == CP_CALC); }
void rcApiKey() {
  if (!rcAuth()) return;
  String k = rcWeb->arg(F("k"));
  int n = constrain((int)rcWeb->arg(F("n")).toInt(), 1, 30);
  char c = k.length() ? k[0] : 0;
  if (c == 'R' || c == 'L') { int d = (c == 'R' ? n : -n) * ENC_DIR; noInterrupts(); encCount += d; interrupts(); }
  else if (c == 'W') {                              // the mouse wheel (Settings > PC mouse wheel = how far a click turns)
    static int16_t acc = 0;
    static const int16_t STEP[5] = {240, 160, 100, 60, 35};
    acc = constrain(acc + (int)rcWeb->arg(F("d")).toInt(), -3000, 3000);
    int st = STEP[P.mWheel % 5], k = acc / st;
    if (k) { acc -= k * st; noInterrupts(); encCount += k * ENC_DIR; interrupts(); }
  }
  else if (c == 'P') { fPress = true; fShort = true; }
  else if (c == 'H') fLong = true;
  else if (c == 'T') rcTypeUtf8(rcWeb->arg(F("t")));
  else if (c == 'B') kbRemotePush('\b');
  else if (c == 'E') { if (rcTextBox()) kbRemotePush('\n'); else { fPress = true; fShort = true; } }
  else if (c == 'M' || c == 'C') {                  // the PC's mouse: moved / clicked at x, y (screen pixels)
    rcPx = constrain((int)rcWeb->arg(F("x")).toInt(), -1, W - 1);
    rcPy = constrain((int)rcWeb->arg(F("y")).toInt(), -1, H - 1);
    if (c == 'C') rcPtrClick = true; else rcPtrMove = true;
  }
  rcWeb->send(200, F("text/plain"), F("ok"));
}
void rcLsDir(const char* dir, int depth, bool& first) {
  Dir d = LittleFS.openDir(dir);
  while (d.next()) {
    char p[72];
    snprintf_P(p, sizeof p, PSTR("%s%s%s"), dir, strcmp_P(dir, PSTR("/")) ? FS("/") : FS(""), d.fileName().c_str());
    if (d.isDirectory()) { if (depth < 3) rcLsDir(p, depth + 1, first); continue; }
    for (char* q = p; *q; q++) if (*q == '"' || *q == '\\') *q = '_';
    char line[104];
    snprintf_P(line, sizeof line, PSTR("%s{\"p\":\"%s\",\"s\":%u}"), first ? FS("") : FS(","), p, (unsigned)d.fileSize());
    first = false;
    rcWeb->sendContent(line);
  }
}
void rcApiLs() {
  if (!rcAuth()) return;
  rcWeb->setContentLength(CONTENT_LENGTH_UNKNOWN);
  rcWeb->send(200, F("application/json"), F("{\"files\":["));
  bool first = true;
  if (fsOk) rcLsDir("/", 0, first);
  FSInfo fi = {};
  if (fsOk) LittleFS.info(fi);
  char tail[64];
  snprintf_P(tail, sizeof tail, PSTR("],\"total\":%u,\"used\":%u}"), (unsigned)fi.totalBytes, (unsigned)fi.usedBytes);
  rcWeb->sendContent(tail);
  rcWeb->sendContent(F(""));
}
bool rcPathOk(const String& p) { return fsOk && p.length() > 1 && p.length() < 64 && p[0] == '/' && p.indexOf(F("..")) < 0; }
void rcApiGet() {
  if (!rcAuth()) return;
  String p = rcWeb->arg(F("p"));
  if (!rcPathOk(p) || !LittleFS.exists(p)) { rcWeb->send(404, F("text/plain"), F("no such file")); return; }
  File f = LittleFS.open(p, "r");
  rcWeb->sendHeader(F("Content-Disposition"), String(F("attachment; filename=\"")) + p.substring(p.lastIndexOf('/') + 1) + '"');
  rcWeb->streamFile(f, F("application/octet-stream"));
  f.close();
}
void rcApiRm() {
  if (!rcAuth()) return;
  String p = rcWeb->arg(F("p"));
  bool ok = rcPathOk(p) && LittleFS.remove(p);
  rcFsChanged = true;
  rcWeb->send(ok ? 200 : 400, F("text/plain"), ok ? F("ok") : F("couldn't delete"));
}
void rcMkdirs(const String& path) {                 // every folder on the way to a file
  for (int i = 1; i < (int)path.length(); i++)
    if (path[i] == '/') { String d = path.substring(0, i); if (!LittleFS.exists(d)) LittleFS.mkdir(d); }
}
void rcApiMv() {
  if (!rcAuth()) return;
  String p = rcWeb->arg(F("p")), to = rcWeb->arg(F("to"));
  bool ok = rcPathOk(p) && rcPathOk(to) && LittleFS.exists(p);
  if (ok) { rcMkdirs(to); ok = LittleFS.rename(p, to); }
  rcFsChanged = true;
  rcWeb->send(ok ? 200 : 400, F("text/plain"), ok ? F("ok") : F("couldn't rename"));
}
void rcUpload() {                                   // a file arriving in pieces (multipart form)
  HTTPUpload& u = rcWeb->upload();
  if (u.status == UPLOAD_FILE_START) {
    rcUpOk = false;
    if ((uint16_t)rcWeb->arg(F("pin")).toInt() != P.rcPin || !fsOk) return;
    String dir = rcWeb->arg(F("d"));
    if (!dir.length() || dir[0] != '/') dir = String('/') + dir;
    if (!dir.endsWith(F("/"))) dir += '/';
    String name;
    for (unsigned i = 0; i < u.filename.length() && name.length() < 40; i++) {
      char c = u.filename[i];
      if (isalnum((uint8_t)c) || c == '.' || c == '-' || c == '_') name += c; else if (c == ' ') name += '_';
    }
    if (!name.length()) name = F("upload.bin");
    String path = dir + name;
    if (!rcPathOk(path)) return;
    rcMkdirs(path);
    rcUp = LittleFS.open(path, "w");
    rcUpOk = (bool)rcUp;
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (rcUp && rcUp.write(u.buf, u.currentSize) != u.currentSize) rcUpOk = false;   // flash full
  } else if (u.status == UPLOAD_FILE_END || u.status == UPLOAD_FILE_ABORTED) {
    if (rcUp) rcUp.close();
    if (u.status == UPLOAD_FILE_ABORTED) rcUpOk = false;
  }
}
void rcStart() {
  rcWeb = new ESP8266WebServer(80);
  rcWeb->on(F("/"), HTTP_GET, []() { rcWeb->send_P(200, PSTR("text/html"), RC_PAGE); });
  rcWeb->on(F("/api/key"), HTTP_GET, rcApiKey);
  rcWeb->on(F("/api/redraw"), HTTP_GET, []() { if (rcAuth()) { rcWantRedraw = true; rcWeb->send(200, F("text/plain"), F("ok")); } });
  rcWeb->on(F("/api/ls"), HTTP_GET, rcApiLs);
  rcWeb->on(F("/api/get"), HTTP_GET, rcApiGet);
  rcWeb->on(F("/api/rm"), HTTP_GET, rcApiRm);
  rcWeb->on(F("/api/mv"), HTTP_GET, rcApiMv);
  rcWeb->on(F("/api/put"), HTTP_POST, []() {
    if (!rcAuth()) return;
    rcFsChanged = true;
    rcWeb->send(rcUpOk ? 200 : 400, F("text/plain"), rcUpOk ? F("ok") : F("upload failed (flash full?)"));
  }, rcUpload);
  rcWeb->onNotFound([]() { rcWeb->send(404, F("text/plain"), F("not found")); });
  rcWeb->begin();
  rcSrv = new WiFiServer(81);
  rcSrv->begin();
  rcSrv->setNoDelay(true);
}
void rcStop() {
  if (rcBuf) rcDrop();
  if (rcWeb) { rcWeb->stop(); delete rcWeb; rcWeb = nullptr; }
  if (rcSrv) { rcSrv->stop(); delete rcSrv; rcSrv = nullptr; }
}
void rcAccept() {                                   // a viewer opens the screen stream: "GET /?pin=1234"
  WiFiClient c = rcSrv->accept();
  if (!c) return;
  c.setTimeout(400);
  String req = c.readStringUntil('\n');
  for (uint32_t t = millis(); c.connected() && millis() - t < 400; ) { String l = c.readStringUntil('\n'); if (l.length() <= 1) break; }
  char want[12];
  snprintf_P(want, sizeof want, PSTR("pin=%u"), P.rcPin);
  int at = req.indexOf(want);
  if (at < 0 || isdigit((uint8_t)req[at + strlen(want)])) {
    c.print(F("HTTP/1.1 403 Forbidden\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: 9\r\nConnection: close\r\n\r\nwrong PIN"));
    c.stop();
    return;
  }
  if (rcBuf) rcDrop();                              // one viewer at a time: the newest wins
  rcBuf = (uint8_t*)malloc(RC_BUF);
  if (!rcBuf) { c.stop(); return; }
  rcCli = c; rcLen = 0;
  rcCli.setNoDelay(true);
  rcCli.setTimeout(1500);
  rcCli.print(F("HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nAccess-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n"));
  uint8_t clr = 8;
  rcPut(&clr, 1);
  rcAppChanged();
  if (oledOk) rcOled(oled.getBuffer());
  rcWantRedraw = true;
}
void rcService() {                                  // every loop, and from inside long waits (web pages, live video)
  if (!P.rcOn || WiFi.status() != WL_CONNECTED) { if (rcWeb) rcStop(); return; }
  if (rcBusy) return;                               // (never inside itself)
  rcBusy = true;
  if (!rcWeb) rcStart();
  rcWeb->handleClient();
  if (rcSrv->hasClient()) rcAccept();
  if (rcBuf && !rcCli.connected()) rcDrop();
  if (rcLen && millis() - rcSentAt > 25) rcFlush();
  rcBusy = false;
}
void rcSetOn(bool on) {
  P.rcOn = on;
  if (on && (P.rcPin < 1000 || P.rcPin > 9999)) P.rcPin = 1000 + (uint16_t)(ESP.random() % 9000);
  if (!on) rcStop();
  prefsSave();
}

// ---------- the PC's mouse on the Psion screen ----------
// Moving it: the web screen view's pointer follows it, popup menus / Word + Excel drop-downs / home screen apps
// light up under it. Clicking: a real click on web pages; the item under it in menus, the home screen, the
// Word + Excel menu bar, drop-downs, toolbar, paragraphs (Word) and cells (Excel); anywhere else = a knob press.
int pmAt(int x, int y) {                            // the popup menu item under (x, y), -1 = none
  if (!pmOpen || x < pmX || x >= pmX + PM_W || y < pmY + 16) return -1;
  int i = (y - pmY - 16) / 16;
  return i < pmN ? i : -1;
}
void pmPoint(int i) { if (i >= 0 && i != pmSel) { int o = pmSel; pmSel = i; pmDrawItem(o); pmDrawItem(i); } }
int w95DropAt(int x, int y) {                       // the open drop-down's item under (x, y): -1 separator, -2 outside
  int n = w95ItemCount(), dx = w95DropX(), w = w95DropW();
  if (dx + w > W - 2) dx = W - 2 - w;
  if (x < dx || x >= dx + w || y < 28 || y >= w95ItemY(n)) return -2;
  for (int i = 0; i < n; i++) if (y < w95ItemY(i + 1)) return w95Sep(i) ? -1 : i;
  return -2;
}
bool w95App() { return (app == APP_WORD && wdMode == WDM_VIEW) || (app == APP_SHEET && xlMode == XLM_GRID); }
void w95PtrHover(int x, int y) {
  if (w95Mode != 2 || w95Msg) return;
  int i = w95DropAt(x, y);
  if (i >= 0 && i != w95Item) { w95DrawItem(w95Item, false); w95Item = i; w95DrawItem(i, true); }
}
bool w95PtrClick(int x, int y) {                    // the menu bar, a drop-down, the toolbar (true = the click was used)
  if (y >= 14 && y < 26) {
    for (int i = 0; i < w95NM; i++) {
      int w = strlen(pgmItem(w95Titles, i)) * 6 + 8;
      if (x < w95MenuX[i] || x >= w95MenuX[i] + w) continue;
      bool same = w95Mode == 2 && w95Focus == i;
      if (w95Mode == 2) { w95Mode = 0; if (w95Redraw) w95Redraw(); }   // close the one that's open
      if (same) return true;
      w95Mode = 2; w95Focus = i; w95Item = 0; while (w95Sep(w95Item)) w95Item++;
      w95MenuBar(); w95Toolbar(); w95DrawDrop(); SFX(T_HOP);
      return true;
    }
    return true;
  }
  if (w95Mode == 2) {
    int i = w95DropAt(x, y);
    if (i >= 0) { if (i != w95Item) { w95DrawItem(w95Item, false); w95Item = i; w95DrawItem(i, true); } fShort = true; return true; }
    if (i == -1) return true;                       // a separator line
    w95Mode = 0; if (w95Redraw) w95Redraw();        // a click anywhere else closes it
    return true;
  }
  if (y >= 27 && y < 43) {
    for (int t = 0; t < w95NT; t++) if (x >= w95ToolX[t] && x < w95ToolX[t] + 16) {
      w95Mode = 1; w95Focus = w95NM + t; w95MenuBar(); w95Toolbar(); fShort = true;   // the app runs it next loop
      return true;
    }
    return true;
  }
  if (w95Mode == 1) { w95Mode = 0; w95MenuBar(); w95Toolbar(); }   // the bar had the focus: back to the document
  return false;
}
bool wdPtrClick(int x, int y) {                     // Word: a click picks the paragraph, a second one types into it
  if (y < WD_Y0 || y > WD_Y1 || x >= 302) return false;
  int para = -1;
  for (int i = 0; i < wdLineN && y >= wdLineY[i]; i++) para = wdLinePara[i];
  if (para < 0) return true;
  if (para == wdPara) { wdCommand(32 + 0); return true; }
  wdPara = para; wdDrawDoc(); wdStatus(); uiClick(); oledDirty = true;
  return true;
}
bool xlPtrClick(int x, int y) {                     // Excel: a click picks the cell, a second one edits it
  if (y >= 44 && y < 58) { xlEditCell(); return true; }             // the formula bar
  int row, col = -1;
  if (y >= XL_GY && y < XL_GY + XL_VR * XL_RH) row = xlTop + (y - XL_GY) / XL_RH;
  else if (y >= 58 && y < XL_GY) row = xlCur / XL_COLS;             // a column letter: that column, this row
  else return false;
  if (x < XL_GX) col = xlCur % XL_COLS;                              // a row number: that row, this column
  else for (int c = xlLeft, cx = XL_GX; c < XL_COLS && cx < XL_GX + XL_GW; cx += WK.xl.colw[c], c++)
    if (x < cx + WK.xl.colw[c]) { col = c; break; }
  if (col < 0 || row >= XL_ROWS) return true;
  int pos = row * XL_COLS + col;
  if (pos == xlCur && !xlSel) { xlEditCell(); return true; }
  xlCur = pos; xlScrollTo(); shDrawGrid(); shDrawFormulaBar(); shStatus(); uiClick(); oledDirty = true;
  return true;
}
bool menuRowAt(int x, int y, int* i) {
  int top = LEDGER_Y + LEDGER_HEAD;
  if (x < LEDGER_X || x >= LEDGER_X + LEDGER_W - 8 || y < top || y >= top + MENU_VIS * LEDGER_ROW) return false;
  *i = menuTop + (y - top) / LEDGER_ROW;
  return *i < MENU_N;
}
void menuPtr(int x, int y, bool click) {            // the home screen: the app list and the dock
  if (saverOn) { if (click) { fPress = true; fShort = true; } return; }
  if (P.deskStyle % DS_N != DS_LEDGER) { deskPtr(x, y, click); return; }
  int i;
  if (menuRowAt(x, y, &i)) {
    if (click) { menuSel = i; uiClick(); enterApp(1 + i); return; }
    if (i == menuSel) return;
    int old = menuSel; menuSel = i;                  // hover: the selection follows the mouse
    if (old >= menuTop && old < menuTop + MENU_VIS) drawLedgerRow(old - menuTop);
    drawLedgerRow(i - menuTop);
    int ot = dockIndexOf(1 + old), nt = dockIndexOf(1 + i);
    if (ot != nt) { drawDockTab(ot, false); drawDockTab(nt, true); }
    drawTitleAccent(menuAccent()); oledDirty = true;
    return;
  }
  if (click && y >= DOCK_Y && x / DOCK_TAB < 7) { int a = DOCK_APPS[x / DOCK_TAB]; menuSel = a - 1; uiClick(); enterApp(a); }
}
bool ptrListLive() {                                // is the list the mouse knows about still the one on screen?
  if (ptrL.app != app || pmOpen) return false;
  switch (ptrL.kind) {
    case PL_PICKER: return fpOpen;
    case PL_FILES: return fmPage == FMP_LIST;
    case PL_SETTINGS: return !prTyping && !prEditTz;
    case PL_NOTES: return npPage == NP_LIST;
    case PL_WIFI: return spPage == SP_MAIN;
    case PL_WIFISCAN: return spPage == SP_SCAN;
    case PL_TOOLS: return tlTool < 0;
    case PL_BOOKS: return app == APP_BOOKS && bkListMode();
    case PL_WEB: return wbPage == WB_LIST && wbAsk == WA_NONE && !wbMenu && !wbLinkMenu;
    case PL_NETLAB: return nlTest < 0;
    case PL_CALCLOG: return cPage == CP_LOG;
  }
  return false;
}
void ptrService() {                                 // from loop(): never in the middle of a page load
  if (!rcPtrMove && !rcPtrClick) return;
  bool click = rcPtrClick;
  rcPtrMove = rcPtrClick = false;
  int x = rcPx, y = rcPy;
  static int16_t lastX = -1, lastY = -1, mAcc = 0;
  if (click) idleSince = millis();
  if (gameApp(app) && !gameSetupActive) {            // games: moving the mouse turns the knob (Pong: up/down)
    if (!click && lastX >= 0 && x >= 0) {
      static const int8_t STEP[5] = {24, 16, 10, 6, 3};
      mAcc += app == APP_PONG ? y - lastY : x - lastX;
      int st = STEP[P.mMove % 5], n = mAcc / st;
      if (n) { mAcc -= n * st; noInterrupts(); encCount += n * ENC_DIR; interrupts(); }
    }
    lastX = x; lastY = y;
    if (click) { fPress = true; fShort = true; }
    return;
  }
  lastX = -1;
  if (x < 0 || y < 0 || gameSetupActive) { if (click) { fPress = true; fShort = true; } return; }
  if (pmOpen) { int i = pmAt(x, y); pmPoint(i); if (click && i >= 0) fShort = true; return; }
  if (app == APP_MENU) { menuPtr(x, y, click); return; }
  if (svPtrOk()) {
    if (x < SV_W && y >= SV_Y0 && y < SV_Y0 + SV_H) {
      if (!svCur) { svCur = true; svCurAuto = true; svPick = false; }
      svCx = x; svCy = y - SV_Y0;
      if (click) { svCurMoved = false; svCurClick(); } else svCurMoved = true;
    } else if (click) { fPress = true; fShort = true; }   // title bar / footer: the menu, like a knob press
    return;
  }
  if (w95App() && !w95Msg) {
    if (!click) { w95PtrHover(x, y); return; }
    if (w95PtrClick(x, y)) return;
    if (app == APP_WORD) wdPtrClick(x, y); else xlPtrClick(x, y);
    return;
  }
  if (ptrListLive() && x >= ptrL.x && x < ptrL.x + ptrL.w && y >= ptrL.y && y < ptrL.y + ptrL.rh * ptrL.vis) {
    if (!click) return;                              // a list: click = pick that row, click it again = open it
    int d = ptrL.top + (y - ptrL.y) / ptrL.rh - ptrL.sel;
    if (!d) { fPress = true; fShort = true; return; }
    noInterrupts(); encCount += d * ENC_DIR; interrupts();
    return;
  }
  if (click) { fPress = true; fShort = true; }      // anywhere else: a knob press
}

void drawOled() {
  if (!oledOk || !P.oledOn || saverOn) return;     // the screensaver draws the OLED itself
  if (okOpen) { okDrawOled(); return; }           // typing on a web page: the OLED is the keyboard
  char b[28];
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  if (ringOn) {                                   // timer / alarm ringing beats everything
    int h, m, s; getHMS(&h, &m, &s);
    char t[12]; snprintf_P(t, sizeof t, PSTR("%02d:%02d"), dispHour(h) & 31, m & 63);
    oledCard(FS("RING RING"), ringWhat, t, FS(""), FS("press to stop"));
    oled.display();
    return;
  }
  if (gameSetupActive) {
    int a = gameSetupApp;
    char l[4][24];
    snprintf_P(l[0], 24, PSTR("DIFFICULTY %10s"), SET_LEVELS[gameDifficulty[a] % 3]);
    snprintf_P(l[1], 24, PSTR("SPEED      %10s"), SET_SPEEDS[gameSpeed[a] % 3]);
    if (a == APP_RACE) snprintf_P(l[2], 24, PSTR("%-7s %13s"), RACE_MODES[raceMode % 2], RACE_TRACKS[raceTrack % 3]);
    else               snprintf_P(l[2], 24, PSTR("turn:pick  press:set"));
    snprintf_P(l[3], 24, PSTR("> %s"), setupItemName(gameSetupSel));
    oledCard(a == APP_RACE ? FS("RALLY") : appName(a - 1), l[0], l[1], l[2], l[3]);
    oled.display();
    return;
  }
  switch (app) {
    case APP_MENU: {
      int h, m, s; getHMS(&h, &m, &s);
      int a = 1 + menuSel;
      char l[4][24];
      snprintf_P(l[0], 24, PSTR("%.21s"), pgmItem(APP_DESC_P, menuSel));
      if (a >= APP_PONG && a <= APP_RACE) {
        snprintf_P(l[1], 24, PSTR("DIFFICULTY %10s"), SET_LEVELS[gameDifficulty[a] % 3]);
        snprintf_P(l[2], 24, PSTR("SPEED      %10s"), SET_SPEEDS[gameSpeed[a] % 3]);
      } else {
        snprintf_P(l[1], 24, PSTR("TYPE       %10s"), appKind(menuSel));
        if (a == APP_WIFI) snprintf_P(l[2], 24, PSTR("WiFi %.16s"), WiFi.status() == WL_CONNECTED ? WiFi.SSID().c_str() : FS("offline"));
        else snprintf_P(l[2], 24, PSTR("CPU %3d%%  RAM %3uK"), cpuLoad, (unsigned)(freeHeapNow / 1024));
      }
      snprintf_P(l[3], 24, PSTR("%02d:%02d    app %u of %u"), (h & 31), (m & 63), (unsigned)(menuSel + 1) % 100, (unsigned)MENU_N % 100);
      oledCard(appName(menuSel), l[0], l[1], l[2], l[3]);
      break;
    }
    case APP_PONG:
      oled.setCursor(0, 0); oled.print(FS("YOU")); oled.setCursor(52, 0); oled.print(mood); oled.setCursor(104, 0); oled.print(FS("CPU"));
      snprintf_P(b, sizeof b, PSTR("%d - %d"), pScore, cScore); oledBig(b);
      snprintf_P(b, sizeof b, PSTR("rally %d  best %d"), rally, bestRally); oledLine(44, b);
      pongStatus(b, sizeof b); oledLine(56, b);
      break;
    case APP_FROGGER:
      oledHead(FS("FROGGER"));
      snprintf_P(b, sizeof b, PSTR("%d"), fScore); oledBig(b);
      snprintf_P(b, sizeof b, PSTR("lives %d   level %d"), fLives, fLevel); oledLine(44, b);
      snprintf_P(b, sizeof b, PSTR("home %d/5  hold=menu"), fHome); oledLine(56, b);
      break;
    case APP_GALAGA:
      oledHead(FS("GALAGA"));
      snprintf_P(b, sizeof b, PSTR("%d"), gScore); oledBig(b);
      snprintf_P(b, sizeof b, PSTR("lives %d   wave %d"), gLives, gWave); oledLine(44, b);
      oledLine(56, FS("fox vs the mice! x3"));
      break;
    case APP_FOREST:
      oledForest();
      break;
    case APP_RACE:
      oledHead(raceMode ? FS("RACE") : FS("FREEWAY"));
      if (raceMode) snprintf_P(b, sizeof b, PSTR("P%d/3"), racePosition());
      else          snprintf_P(b, sizeof b, PSTR("%dm"), (int)(raceDistance / 10));
      oledBig(b);
      if (raceMode) snprintf_P(b, sizeof b, PSTR("%d%%  speed %d"), (int)(min(raceDistance, RACE_LEN) * 100 / RACE_LEN), (int)(raceSpeed * 20));
      else          snprintf_P(b, sizeof b, PSTR("best %dm  speed %d"), raceBest, (int)(raceSpeed * 20));
      oledLine(44, b);
      snprintf_P(b, sizeof b, PSTR("HP %d  turbo %d"), raceLives, racePowerups); oledLine(56, b);
      break;
    case APP_CLOCK: oledClock(); break;
    case APP_TOOLS: oledTools(); break;
    case APP_MUSIC: oledMusic(); break;
    case APP_WEB: oledWeb(); break;
    case APP_NETLAB: oledNetlab(); break;
    case APP_RUN: oledRun(); break;
    case APP_BOOKS: oledBooks(); break;
    case APP_WIFI:
      oledWifi();
      break;
    case APP_SETTINGS:
      oledPrefs();
      break;
    case APP_NOTES:
      oledNotes();
      break;
    case APP_WORD:  oledWord(); break;
    case APP_SHEET: oledSheet(); break;
    case APP_CALC:  oledCalc(); break;
    case APP_CODE:  oledCode(); break;
    case APP_FILES: oledFiles(); break;
    case APP_STATS:
      oledHead(FS("STATS"));
      oled.setTextSize(2);
      snprintf_P(b, sizeof b, PSTR("CPU %d%%"), cpuLoad); oled.setCursor(0, 14); oled.print(b);
      oled.setTextSize(1);
      snprintf_P(b, sizeof b, PSTR("RAM %d%% used %uK free"), ramUsed, (unsigned)(freeHeapNow / 1024)); oledLine(40, b);
      snprintf_P(b, sizeof b, PSTR("%uMHz  %d loops/s"), (unsigned)ESP.getCpuFreqMHz(), loopHz); oledLine(52, b);
      break;
  }
  oled.display();
}

// ================= SLEEP + ALWAYS-ON OLED =================
// Sleep works like a phone's power button: hold the knob 2.5 s (or Settings > Sleep now, Seven's Start menu > Sleep,
// or Settings > Sleep after N idle minutes). The big screen switches off (it keeps its picture), the OLED shows the
// always-on display (or goes dark). A press or a turn wakes it, right where it was - that press does nothing else.
bool sleeping = false;
uint32_t lastInputMs = 0, aodAt = 0;
int8_t aodDx = 0, aodDy = 0;
int aodLastMin = -1;
const uint8_t AOD_CONTRAST[5] = {0, 4, 30, 120, 255};
const uint8_t SLEEP_MIN[6] = {0, 1, 2, 5, 10, 30};
void oledLevel(int lvl) {                            // 0..4; the faintest also lowers pre-charge + VCOMH (really dim)
  oled.ssd1306_command(SSD1306_SETCONTRAST); oled.ssd1306_command(AOD_CONTRAST[lvl % 5]);
  oled.ssd1306_command(SSD1306_SETPRECHARGE); oled.ssd1306_command(lvl <= 1 ? 0x22 : 0xF1);
  oled.ssd1306_command(SSD1306_SETVCOMDETECT); oled.ssd1306_command(lvl <= 1 ? 0x00 : 0x40);
}
void oledNormal() {
  oled.ssd1306_command(SSD1306_SETPRECHARGE); oled.ssd1306_command(0xF1);
  oled.ssd1306_command(SSD1306_SETVCOMDETECT); oled.ssd1306_command(0x40);
  oled.ssd1306_command(SSD1306_SETCONTRAST); oled.ssd1306_command(OLED_CONTRAST[P.contrast % 3]);
}
void aodWolf(int x, int y) {                         // a tiny Micor for the OLED (24 x 22)
  oled.fillTriangle(x + 2, y + 9, x + 4, y, x + 10, y + 6, SSD1306_WHITE);
  oled.fillTriangle(x + 22, y + 9, x + 20, y, x + 14, y + 6, SSD1306_WHITE);
  oled.fillRoundRect(x + 2, y + 5, 21, 15, 7, SSD1306_WHITE);
  oled.fillCircle(x + 8, y + 11, 2, SSD1306_BLACK); oled.fillCircle(x + 16, y + 11, 2, SSD1306_BLACK);
  oled.drawPixel(x + 9, y + 10, SSD1306_WHITE); oled.drawPixel(x + 17, y + 10, SSD1306_WHITE);
  oled.fillRect(x + 11, y + 15, 3, 2, SSD1306_BLACK);
  oled.drawLine(x + 9, y + 18, x + 12, y + 17, SSD1306_BLACK); oled.drawLine(x + 12, y + 17, x + 15, y + 18, SSD1306_BLACK);
}
void aodDraw() {
  int h, m, s; getHMS(&h, &m, &s);
  if (m != aodLastMin) {                             // move a little every minute: no burned-in pixels
    aodLastMin = m;
    if (P.aodStyle % AOD_N == AOD_TINY) { aodDx = random(0, 70); aodDy = random(0, 46); }
    else { aodDx = random(-3, 4); aodDy = random(-2, 3); }
  }
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  char t[12], d[24];
  snprintf_P(t, sizeof t, P.h12 ? PSTR("%d:%02d") : PSTR("%02d:%02d"), dispHour(h) & 31, m & 63);
  fmtDate(d, sizeof d, (int)(clkEpoch / 86400UL));
  int st = P.aodStyle % AOD_N;
  if (st == AOD_BIG) {
    oled.setTextSize(3);
    int w = strlen(t) * 18;
    oled.setCursor(64 - w / 2 + aodDx, 12 + aodDy); oled.print(t);
    oled.setTextSize(1);
    if (P.aodSecs) { char b[4]; snprintf_P(b, sizeof b, PSTR("%02d"), s & 63); oled.setCursor(64 + w / 2 + aodDx + 2, 26 + aodDy); oled.print(b); }
    if (P.aodDate) { oled.setCursor(64 - (int)strlen(d) * 3 + aodDx, 44 + aodDy); oled.print(d); }
  } else if (st == AOD_ANALOG) {
    int cx = (P.aodDate ? 34 : 64) + aodDx, cy = 32 + aodDy;
    oled.drawCircle(cx, cy, 27, SSD1306_WHITE);
    for (int k = 0; k < 12; k++) { float a = k * 0.5236f; oled.drawPixel(cx + (int)(24 * sinf(a)), cy - (int)(24 * cosf(a)), SSD1306_WHITE); }
    float ah = ((h % 12) + m / 60.0f) * 0.5236f, am = m * 0.10472f;
    oled.drawLine(cx, cy, cx + (int)(14 * sinf(ah)), cy - (int)(14 * cosf(ah)), SSD1306_WHITE);
    oled.drawLine(cx, cy, cx + (int)(22 * sinf(am)), cy - (int)(22 * cosf(am)), SSD1306_WHITE);
    if (P.aodSecs) { float as = s * 0.10472f; oled.drawPixel(cx + (int)(25 * sinf(as)), cy - (int)(25 * cosf(as)), SSD1306_WHITE); }
    oled.fillCircle(cx, cy, 1, SSD1306_WHITE);
    if (P.aodDate) { oled.setTextSize(2); oled.setCursor(70 + aodDx, 14 + aodDy); oled.print(t); oled.setTextSize(1); oled.setCursor(70 + aodDx, 38 + aodDy); oled.print(d + (P.dateFmt == 0 ? 4 : 0)); }
  } else if (st == AOD_MICOR) {
    aodWolf(4 + aodDx, 18 + aodDy);
    oled.setTextSize(2); oled.setCursor(36 + aodDx, 14 + aodDy); oled.print(t);
    oled.setTextSize(1);
    if (P.aodSecs) { char b[4]; snprintf_P(b, sizeof b, PSTR(":%02d"), s & 63); oled.print(b); }
    if (P.aodDate) { oled.setCursor(36 + aodDx, 36 + aodDy); oled.print(d); }
  } else {                                           // tiny + drifting
    oled.setTextSize(1);
    oled.setCursor(aodDx, aodDy); oled.print(t);
    if (P.aodSecs) { char b[4]; snprintf_P(b, sizeof b, PSTR(":%02d"), s & 63); oled.print(b); }
    if (P.aodDate) { oled.setCursor(aodDx, aodDy + 10); oled.print(d + (P.dateFmt == 0 ? 4 : 0)); }
  }
  oled.display();
}
void sleepEnter() {
  if (sleeping) return;
  sleeping = true;
  soundAllOff();
  tft.sendCommand(ST77XX_DISPOFF);                   // the panel keeps its picture while it sleeps
  tft.sendCommand(ST77XX_SLPIN);
  blSet(false);                                      // (only with TFT_BL_PIN: else the backlight stays lit)
  if (oledOk) {
    if (P.oledOn && P.aodOn) { oledLevel(P.aodBright); aodAt = 0; aodLastMin = -1; }
    else oled.ssd1306_command(SSD1306_DISPLAYOFF);
  }
  takeClicks(); fPress = fShort = fLong = false;
  btnUsed = true;                                    // (the knob may still be held: letting go is not a click)
}
void sleepWake() {
  sleeping = false;
  tft.sendCommand(ST77XX_SLPOUT);
  delay(120);
  tft.sendCommand(ST77XX_DISPON);
  blSet(true);
  if (oledOk) { oledNormal(); oled.ssd1306_command(P.oledOn ? SSD1306_DISPLAYON : SSD1306_DISPLAYOFF); }
  lastInputMs = idleSince = millis();
  oledDirty = true;
}
void sleepTick(uint32_t now) {                       // loop() while asleep
  if (encCount || fPress || fShort || fLong || rcPtrClick) {
    takeClicks(); fPress = fShort = fLong = false; rcPtrClick = false;
    btnUsed = true;                                  // the waking press does nothing else
    sleepWake();
    return;
  }
  if (oledOk && P.oledOn && P.aodOn && (!aodAt || now - aodAt >= (P.aodSecs ? 1000UL : 10000UL))) { aodAt = now; aodDraw(); }
}
void aodPreview() {                                  // Settings: 6 seconds of the always-on display (a press ends it)
  if (!oledOk) return;
  oledLevel(P.aodBright); aodLastMin = -1;
  fPress = false;
  for (uint32_t t = millis(); millis() - t < 6000 && !fPress; ) {
    aodDraw();
    for (int k = 0; k < 20 && !fPress; k++) { delay(50); updateButton(); }
  }
  oledNormal();
  btnUsed = true;
}

// ================= APP SWITCHING / SETUP / LOOP =================
void startSelectedGame() {
  gameSetupActive = false;
  tft.filter = P.gamePal;                         // Settings > Game colours
  fPress = false; fShort = false; fLong = false;
  takeClicks();
  startAppMusic(app);
  switch (app) {
    case APP_PONG:    pongNew(); break;
    case APP_FROGGER: fNew(); break;
    case APP_GALAGA:  gNew(); break;
    case APP_FOREST:  foNewGame(); break;
    case APP_RACE:    raceStart(); break;
  }
  oledDirty = true;
}
// There's no serial monitor (RX/TX run the knob), so a crash explains itself on the screen at the next boot.
// RTC memory survives crashes, watchdog resets and usually short power dips (not a real power-off):
// word 0 = 0xA5 + the app that was open, word 1 = a magic number, word 2 = lowest free heap / stack seen.
const uint32_t RTC_MAGIC = 0x50C0FFEEUL;
uint32_t rtcW[3] = {0, 0, 0};
uint32_t lastCrashAddr = 0;                        // the memory address a crashing read/write tried to use
uint32_t diagMinHeap = 0xFFFFFFFFUL, diagMinStack = 0xFFFFFFFFUL;
bool diagLast = false;                              // the RTC notes were from our own last run
const char* excName(int c) {
  switch (c) {
    case 0: return "illegal instruction";
    case 2: return "instruction fetch";
    case 3: return "load/store error";
    case 6: return "divide by zero";
    case 9: return "unaligned access";
    case 28: return "null pointer read";
    case 29: return "null pointer write";
  }
  return "exception";
}
void diagRead() {                                   // boot: why did we restart?
  ESP.rtcUserMemoryRead(0, rtcW, sizeof rtcW);
  diagLast = rtcW[1] == RTC_MAGIC;
  rst_info* ri = ESP.getResetInfoPtr();
  int r = ri ? (int)ri->reason : 0;
  lastCrash[0] = 0;
  if (r == REASON_EXCEPTION_RST) {                  // code + address first, so a short screen line never cuts them off
    snprintf_P(lastCrash, sizeof lastCrash, PSTR("crash %u at %08X: %s"), (unsigned)ri->exccause, (unsigned)ri->epc1, excName(ri->exccause));
    lastCrashAddr = ri->excvaddr;
  }
  else if (r == REASON_SOFT_WDT_RST) snprintf_P(lastCrash, sizeof lastCrash, PSTR("froze: software watchdog"));
  else if (r == REASON_WDT_RST) snprintf_P(lastCrash, sizeof lastCrash, PSTR("froze: hardware watchdog"));
  else if (r == REASON_EXT_SYS_RST) snprintf_P(lastCrash, sizeof lastCrash, PSTR("reset pin or power dip"));
  else if (r == REASON_DEFAULT_RST && diagLast) snprintf_P(lastCrash, sizeof lastCrash, PSTR("power dip while running"));
  rtcW[1] = RTC_MAGIC; rtcW[2] = 0xFFFFFFFFUL;
  ESP.rtcUserMemoryWrite(0, rtcW, sizeof rtcW);
}
void diagTick(uint32_t now) {                       // once a second: remember the lowest heap / stack seen
  static uint32_t at = 0;
  if (now - at < 1000) return;
  at = now;
  uint32_t h = ESP.getFreeHeap(), st = ESP.getFreeContStack();
  if (h < diagMinHeap || st < diagMinStack) {
    diagMinHeap = min(diagMinHeap, h); diagMinStack = min(diagMinStack, st);
    uint32_t hv = diagMinHeap / 16, sv = diagMinStack;
    if (hv > 0xFFFF) hv = 0xFFFF;
    if (sv > 0xFFFF) sv = 0xFFFF;
    rtcW[2] = hv | (sv << 16);
    ESP.rtcUserMemoryWrite(2, &rtcW[2], sizeof rtcW[2]);
  }
}
void crashReport() {
  if (!lastCrash[0] || strstr_P(lastCrash, PSTR("reset pin"))) return;   // the reset button only shows in the boot info
  int last = (rtcW[0] >> 24) == 0xA5 ? (int)(rtcW[0] & 0xFF) : 0;
  bool power = strstr_P(lastCrash, PSTR("power")) != nullptr;
  char b[56];
  tft.fillRect(14, 50, 292, 150, E_WHITE);
  tft.drawRect(14, 50, 292, 150, E_CORAL);
  tft.fillRect(14, 50, 292, 16, E_CORAL);
  putText(22, 54, 1, E_ONACC, E_CORAL, power ? FS("The Psion lost power for a moment ;-;") : FS("The Psion restarted after a problem ;-;"));
  putText(22, 74, 1, C_INK, E_WHITE, lastCrash);
  if (strstr_P(lastCrash, PSTR("crash"))) {
    char a[40]; snprintf_P(a, sizeof a, PSTR("memory address it used: %08X"), (unsigned)lastCrashAddr);
    putText(22, 160, 1, C_MID, E_WHITE, a);
  }
  snprintf_P(b, sizeof b, PSTR("app: %s"), last >= 1 && last <= MENU_N ? appName(last - 1) : FS("home screen"));
  putText(22, 88, 1, C_INK, E_WHITE, b);
  if (diagLast && rtcW[2] != 0xFFFFFFFFUL) {
    snprintf_P(b, sizeof b, PSTR("lowest before: RAM %u B, stack %u B"), (unsigned)((rtcW[2] & 0xFFFF) * 16), (unsigned)(rtcW[2] >> 16));
    putText(22, 102, 1, C_INK, E_WHITE, b);
  }
  if (power) {
    putText(22, 120, 1, C_MID, E_WHITE, FS("Check the USB cable or the battery: the"));
    putText(22, 132, 1, C_MID, E_WHITE, FS("supply voltage dipped for a moment. Wi-Fi"));
    putText(22, 144, 1, C_MID, E_WHITE, FS("peaks need a supply that keeps up."));
  } else {
    putText(22, 120, 1, C_MID, E_WHITE, FS("(send these numbers to Claude to fix it)"));
    putText(22, 132, 1, C_MID, E_WHITE, FS("also in WiFi > About device"));
  }
  putText(22, 180, 1, C_MID, E_WHITE, FS("press the knob to go on"));
  uint32_t t = millis();
  while (millis() - t < 20000 && digitalRead(BTN_PIN) == HIGH) { delay(10); yield(); }
  for (uint32_t t2 = millis(); digitalRead(BTN_PIN) == LOW && millis() - t2 < 3000; ) { delay(10); yield(); }
}
void enterApp(int a) {
  tft.filter = 0;
  if (app != a) {                                 // leaving an app: keep its work, free its big buffers
    if (app == APP_NOTES) notesLeave();
    if (app == APP_WORD) wordLeave();
    if (app == APP_SHEET) sheetLeave();
    if (app == APP_CODE) codeLeave();
    if (app == APP_MUSIC) musicLeave();
    if (app == APP_TOOLS) { if (tlTool == TL_TALLY) prefsSave(); tlToneOn = tlMetOn = false; tlTool = -1; }
    if (app == APP_GALAGA) gFreeSprites();
    if (app == APP_RACE && raceCv) { delete raceCv; raceCv = nullptr; }
    if (app == APP_RUN) runLeave();
  }
  app = a;
  rcAppChanged();                                 // PC viewer: games aren't mirrored, everything else is
  { rtcW[0] = 0xA5000000UL | (uint32_t)a; ESP.rtcUserMemoryWrite(0, &rtcW[0], sizeof rtcW[0]); }   // survives a crash restart
  fPress = false; fShort = false; fLong = false;
  takeClicks();
  stopSound();
  mood = ":3";
  forceClockRedraw();
  if (a >= APP_PONG && a <= APP_RACE) {
    gameSetupActive = true;
    gameSetupApp = a;
    gameSetupSel = SET_DIFFICULTY;
    if (a == APP_RACE) raceState = RACE_SETUP;
    switch (a) {
      case APP_PONG:    pongEnter(); break;
      case APP_FROGGER: froggerEnter(); break;
      case APP_GALAGA:  if (!spr[0]) gBuildSprites(); galaEnter(); break;
      case APP_FOREST:  forestEnter(); break;
    }
    drawGameSetup();
  } else {
    gameSetupActive = false;
    startAppMusic(a);
    switch (a) {
      case APP_MENU:    menuEnter(); break;
      case APP_CLOCK:   clockAppEnter(); break;
      case APP_STATS:   statsEnter(); break;
      case APP_WIFI: wifiEnter(); break;
      case APP_SETTINGS: prefsEnter(); break;
      case APP_NOTES: notesEnter(); break;
      case APP_WORD: wordEnter(); break;
      case APP_SHEET: sheetEnter(); break;
      case APP_CALC: calcEnter(); break;
      case APP_CODE: codeEnter(); break;
      case APP_FILES: filesEnter(); break;
      case APP_TOOLS: toolsEnter(); break;
      case APP_MUSIC: musicEnter(); break;
      case APP_WEB: webEnter(); break;
      case APP_NETLAB: netlabEnter(); break;
      case APP_RUN: runEnter(); break;
      case APP_BOOKS: booksEnter(); break;
    }
  }
  oledDirty = true;
}

void setup() {
  if (CPU_TURBO && system_get_cpu_freq() < 160) system_update_cpu_freq(160);
  nlInitEvents();                                    // NetLab's connection log starts at power-on
  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT, ENC_DT == 16 ? INPUT : INPUT_PULLUP);   // D0 has no pull-up: add 10k to 3V3
  pinMode(BTN_PIN, INPUT_PULLUP);
  encPrev = (digitalRead(ENC_CLK) << 1) | digitalRead(ENC_DT);
  attachInterrupt(digitalPinToInterrupt(ENC_CLK), encISR, CHANGE);
  if (ENC_DT_IRQ) attachInterrupt(digitalPinToInterrupt(ENC_DT), encISR, CHANGE);

  Wire.begin(D2, D1);
  Wire.setClock(OLED_I2C_HZ);
  oledOk = oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  oled.cp437(true);

  if (TFT_BL_PIN >= 0) { pinMode(BL_PIN, OUTPUT); digitalWrite(BL_PIN, TFT_BL_ACTIVE_LOW ? LOW : HIGH); }   // backlight on
  tft.init(240, 320);
  tft.setSPISpeed(TFT_SPI_HZ);                   // delete this line if it gives a compile error
  tft.setRotation(1);
  // Real CP437 character codes. Adafruit GFX's default keeps an old off-by-one for codes 176-255, so pi
  // (0xE3) came out as a sigma, the root sign as a small n, x^2 as a block and the degree sign as a dot.
  tft.cp437(true);

  pinMode(BUZZER_PIN, OUTPUT);                   // claim D6 AFTER tft.init()
  digitalWrite(BUZZER_PIN, LOW);

  tft.setTextWrap(false);                        // long text is clipped, never wrapped over other widgets
  fsOk = LittleFS.begin();                       // prefs + notes live in flash
  prefsLoad();
  applySettings();
  notesScan();
  diagRead();                                    // why did we restart? (shown on the boot screen)
  drawSplash();
  crashReport();
  for (uint32_t t = millis(); digitalRead(BTN_PIN) == LOW && millis() - t < 3000; ) delay(5);   // a skip press isn't a menu click

  randomSeed(ESP.getCycleCount());
  clockInitFromCompile();
  WiFi.persistent(true);                         // Settings app saves the network in flash
  WiFi.mode(WIFI_STA);
  if (WIFI_SSID[0])          WiFi.begin(WIFI_SSID, WIFI_PASS);
  else if (WiFi.SSID().length()) WiFi.begin();
  else                       WiFi.mode(WIFI_OFF);

  winStartUs = micros();
  enterApp(APP_MENU);
}

void loop() {
  uint32_t t0 = micros();
  uint32_t now = millis();
  updateButton();
  tickClock();
  ntpTick();
  updateSound();
  diagTick(now);                                 // lowest heap/stack, kept in RTC memory for the crash report

  clockService(now);                             // the timer and alarm ring in every app
  static bool ringSwallow = false;                // the press that stops the ringing does nothing else
  if (ringOn && (fPress || fShort || fLong)) { ringStop(); fPress = fShort = fLong = false; ringSwallow = true; }
  if (ringSwallow && fShort) { fShort = false; ringSwallow = false; }
  if (encCount || fPress || fShort || fLong || rcPtrMove || rcPtrClick) lastInputMs = now;
  if (fSleep) { fSleep = false; if (!sleeping) { sleepEnter(); return; } }
  if (sleeping) {
    if (ringOn) sleepWake();                         // an alarm wakes it
    else { sleepTick(now); rcService(); delay(5); return; }
  }
  if (P.sleepAfter && !ringOn && now - lastInputMs > SLEEP_MIN[P.sleepAfter % 6] * 60000UL) { sleepEnter(); return; }
  if (fLong) {
    fLong = false;
    if (app == APP_TOOLS && tlTool >= 0) toolsClose();   // inside a tool: back to the tool list
    else if (app == APP_NETLAB && nlTest >= 0) netlabBack();
    else if (app == APP_WORD && wordWantsBack()) wordBack();
    else if (app == APP_SHEET && sheetWantsBack()) sheetBack();
    else if (app == APP_WEB && bkReturn && wbPage == WB_SCREEN && wbAsk == WA_NONE && !pmOpen) { bkReturn = false; enterApp(APP_BOOKS); bkMode = BK_PC; bkOpenPcList(); }
    else if (app == APP_WEB && (wbPage != WB_LIST || wbAsk != WA_NONE || wbMenu || wbLinkMenu)) webBack();
    else if (app == APP_CALC && calcWantsBack()) calcBack();      // graph/log/edit pages: back to the calculator
    else if (app == APP_RUN && runWantsBack()) runBack();         // running: back to the title screen
    else if (app == APP_BOOKS && booksWantsBack()) booksBack();
    else if (app != APP_MENU) enterApp(APP_MENU);
    else if (!saverOn) deskHold();                   // home screen: Seven's Start menu
  }

  if (gameSetupActive) gameSetupTick();
  else {
    switch (app) {
      case APP_MENU:    menuTick(); break;
      case APP_PONG:    pongTick(now); break;
      case APP_FROGGER: froggerTick(now); break;
      case APP_GALAGA:  galaTick(now); break;
      case APP_FOREST:  forestTick(now); break;
      case APP_RACE:    raceTick(now); break;
      case APP_CLOCK:   clockAppTick(now); break;
      case APP_STATS:   statsTick(); break;
      case APP_WIFI: wifiTick(now); break;
      case APP_SETTINGS: prefsTick(now); break;
      case APP_NOTES: notesTick(now); break;
      case APP_WORD: wordTick(now); break;
      case APP_SHEET: sheetTick(now); break;
      case APP_CALC: calcTick(now); break;
      case APP_CODE: codeTick(now); break;
      case APP_FILES: filesTick(now); break;
      case APP_TOOLS: toolsTick(now); break;
      case APP_MUSIC: musicTick(now); break;
      case APP_WEB: webTick(now); break;
      case APP_NETLAB: netlabTick(now); break;
      case APP_RUN: runTick(now); break;
      case APP_BOOKS: booksTick(now); break;
    }
  }

  // A full OLED refresh blocks I2C for ~25 ms, so action games refresh it less often (no stutter).
  bool action = !gameSetupActive && ((app >= APP_PONG && app <= APP_RACE && app != APP_FOREST) || app == APP_RUN);
  if (oledDirty && now - lastOled >= (action ? 400UL : 120UL)) { oledDirty = false; lastOled = now; drawOled(); }

  rcService();                                   // PC remote: page, knob/keys from the PC, files, screen stream
  ptrService();                                  // ...and its mouse
  if (rcWantRedraw && !rcUiBusy()) { rcWantRedraw = false; rcRedraw(); }
  if (rcFsChanged) { rcFsChanged = false; notesScan(); }

  busyUs += micros() - t0;
  loopCount++;
  statsWindow();
  delay(1);
}