// Mars OS 1.3, supplied firmware adapted for Ruta Marciana.
// Independent dual-ST7789 target: 320x240 + 240x240, original GPIO and shell.
// Local offline A* is not Python FAST/SAFE. Phone guide: MAPA > Guia GPS del celular.
// Full setup and limits: docs/RELOJ_MARS_OS.md.
#include <SPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <esp_wifi.h>
#include <esp_heap_caps.h>
#include <soc/rtc_io_reg.h>
#include <soc/sens_reg.h>
#include <time.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "marte_core.h"
#include "font5x7.h"
#include "../psion-micor-spaceapps/navigator_ca.h"
#include "oled240.h"                             // the small screen (GMT130-V1.0, 240 x 240): shared with the Psion firmware

const char OS_VERSION[] = "1.3";
// ---------- pins (the Psion Micor's) ----------
#define TFT_CS      5               // ST7789 240x320: SCL D18, SDA D23, RST -> 3V3
#define TFT_DC      4
#define TFT_BL      32              // backlight (PWM)
#define TFT_SPI_HZ  80000000UL      // lower to 40000000 if the picture glitches
#define ENC_CLK     27              // KY-040 knob
#define ENC_DT      14
#define BTN_PIN     13              // the knob's button
#define PAD_UP      19              // cross pad, each button to GND
#define PAD_DOWN    17
#define PAD_LEFT    16
#define PAD_RIGHT   15
#define PAD_OK      33
#define DAC_L       25              // speaker / amp left (DAC1)
#define DAC_R       26              // right (DAC2)
#define BAT_PIN     34              // battery / 2 (100k + 100k)
#define OLED_SCK    22              // the small screen, GMT130-V1.0 240x240 (ST7789, no CS): SCL D22, SDA D21, DC D2,
#define OLED_MOSI   21              // RES -> EN (or 3V3), BLK -> D32 with the big screen's BL; 10k from D22 to 3V3
#define OLED_DC     2
#define OLED_ROTATE 0               // 0..3: try another if its picture is turned
#define OLED_BGR    false           // true if red and blue come out swapped
#define OLED_SPI_HZ 40000000UL
const int W = 320, H = 240;

// ---------- colours: Ruta Marciana' own (src/style.css) in a Psion frame ----------
#define RGB(r, g, b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
const uint16_t C_BG = RGB(16, 20, 20), C_PANEL = RGB(23, 28, 28), C_PANEL2 = RGB(32, 39, 38), C_LINE = RGB(48, 55, 55),
               C_TEXT = RGB(233, 235, 229), C_MUTED = RGB(153, 165, 160), C_ORANGE = RGB(241, 133, 89), C_CYAN = RGB(158, 217, 203),
               C_YELLOW = RGB(223, 195, 110), C_BAR = RGB(9, 12, 12), C_GREEN = RGB(62, 196, 132), C_RED = RGB(232, 76, 61),
               C_ONOR = RGB(28, 32, 29);   // text on orange (the web's primary button)

// ================= SETTINGS (flash: Preferences "marteos") =================
struct Settings {
  uint8_t ver, bright, volume, stereo, knobRev, flip, panelInv, oled, dimAfter, sleepAfter, conn, wifiOn, mapGrey, steepHide, speedI, lim, sync;
  char relay[40];                                // conn 0: the Mars relay on your PC ("192.168.1.23:8790")
  char web[72];                                  // conn 1: the web's server itself ("https://..." or "192.168.1.23:5173")
  uint16_t lastSite;
};
Settings S = {4, 3, 2, 1, 0, 0, 1, 1, 2, 0, 1, 1, 0, 0, 3, 20, 0, "192.168.1.10:8790", "https://rutamarciana.onrender.com", 0};
Preferences prefs;
void saveSettings() { prefs.putBytes("s", &S, sizeof S); }
void loadSettings() {
  Settings t;
  if (prefs.getBytesLength("s") == sizeof t && prefs.getBytes("s", &t, sizeof t) == sizeof t && t.ver == S.ver) S = t;
  if(!S.web[0]) snprintf(S.web,sizeof S.web,"https://rutamarciana.onrender.com");
  S.relay[sizeof S.relay - 1] = 0; S.web[sizeof S.web - 1] = 0;
  if (S.lim < 5 || S.lim > 40) S.lim = 20;
  if (S.conn > 1) S.conn = 0;
}

// ================= SCREEN =================
Adafruit_ST7789 tft(TFT_CS, TFT_DC, -1);
// Opaque letters (Adafruit's 5x7 font, CP437), each sent as one window: ~10x quicker than drawing them pixel by pixel.
void putText(int x, int y, int s, uint16_t fg, uint16_t bg, const char* t) {
  static uint16_t px[18 * 24];
  int w = 6 * s, h = 8 * s;
  for (; *t; t++, x += w) {
    if (x + w > W || x < 0) return;
    const uint8_t* g = FONT5X7 + (uint8_t)*t * 5;
    for (int i = 0; i < 6; i++) {
      uint8_t line = i < 5 ? g[i] : 0;
      for (int j = 0; j < 8; j++) {
        uint16_t v = (line >> j) & 1 ? fg : bg;
        for (int a = 0; a < s; a++) for (int b = 0; b < s; b++) px[(j * s + a) * w + i * s + b] = v;
      }
    }
    tft.startWrite(); tft.setAddrWindow(x, y, w, h); tft.writePixels(px, w * h); tft.endWrite();
  }
}
int textW(const char* t, int s) { return (int)strlen(t) * 6 * s; }
void putRight(int xr, int y, int s, uint16_t fg, uint16_t bg, const char* t) { putText(xr - textW(t, s), y, s, fg, bg, t); }
void putCenter(int cx, int y, int s, uint16_t fg, uint16_t bg, const char* t) { putText(cx - textW(t, s) / 2, y, s, fg, bg, t); }
void putWrap(int x, int y, int maxc, int lines, int dy, uint16_t fg, uint16_t bg, const char* t) {   // left-aligned, broken at spaces
  char l[60];
  for (int at = 0, k = 0; t[at] && k < lines; k++, y += dy) { at = mrWrap(t, at, maxc, l); putText(x, y, 1, fg, bg, l); }
}
void drawIcon(const uint16_t* rows, int x, int y, int s, uint16_t c) {   // 16 x 16 pixel art, runs of ink as rectangles
  for (int r = 0; r < 16; r++)
    for (int k = 0; k < 16; ) {
      if (!(rows[r] & (0x8000 >> k))) { k++; continue; }
      int st = k;
      while (k < 16 && (rows[r] & (0x8000 >> k))) k++;
      tft.fillRect(x + st * s, y + r * s, (k - st) * s, s, c);
    }
}
const uint16_t ICONS[9][16] = {
  {0x03C0, 0x0420, 0x0990, 0x0A50, 0x0990, 0x0420, 0x0240, 0x0180, 0x0000, 0xC30C, 0xB4B2, 0x8843, 0x8842, 0x8842, 0xFFFF, 0x0000},   // map
  {0x4000, 0x7F80, 0x7FE0, 0x7FF0, 0x7FF0, 0x7FE0, 0x7F80, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0xF000, 0x0000},   // missions
  {0x0100, 0x07C0, 0x1930, 0x2108, 0x27C8, 0x4924, 0x4BA4, 0xFEFE, 0x4BA4, 0x4924, 0x27C8, 0x2108, 0x1930, 0x07C0, 0x0100, 0x0000},   // sites
  {0x07E0, 0x1FF8, 0x3CFC, 0x787E, 0x7CFF, 0xF7FF, 0xE3FF, 0xF7F3, 0xFFE1, 0xFFE1, 0x7FF3, 0x67FE, 0x21FC, 0x19F8, 0x07E0, 0x0000},   // Mars
  {0x0000, 0x0000, 0x0FF0, 0x300C, 0x4002, 0x87E1, 0x1818, 0x2004, 0x07E0, 0x0810, 0x0000, 0x0180, 0x03C0, 0x0180, 0x0000, 0x0000},   // wifi
  {0x0240, 0x0E70, 0x0FF0, 0x07E0, 0x6FF6, 0x7E7E, 0xFC3F, 0x381C, 0x381C, 0xFC3F, 0x7E7E, 0x6FF6, 0x07E0, 0x0FF0, 0x0E70, 0x0240},   // settings
  {0x2004, 0x300C, 0x3BDC, 0x3FFC, 0x7FFE, 0x700E, 0xE007, 0xE427, 0xE007, 0xE3C7, 0x700E, 0x7FFE, 0x3FFC, 0x0FF0, 0x3FFC, 0x7FFE},   // the wolf in a space helmet
  {0x0000, 0x0180, 0x03C0, 0x07E0, 0x0E70, 0x1C38, 0x381C, 0x718E, 0xE3C7, 0xFFFF, 0x4924, 0xFFFF, 0x2492, 0xFFFF, 0x0000, 0x0000},   // 3D view
  {0x0000, 0x0780, 0x0FC0, 0x3FE0, 0x7FF8, 0xFFFC, 0xFFFE, 0x7FFC, 0x0000, 0xFF3C, 0x0000, 0x3FCE, 0x0000, 0xF1FC, 0x0000, 0x0000},   // dust / weather
};
void drawWolf(int x, int y, int s) {             // Micor, space wolf: the helmet in cream, the eyes orange
  drawIcon(ICONS[6], x, y, s, C_TEXT);
  tft.fillRect(x + 5 * s, y + 7 * s, s, s, C_ORANGE); tft.fillRect(x + 10 * s, y + 7 * s, s, s, C_ORANGE);
}

// ================= BACKLIGHT, BATTERY =================
const uint8_t BL_DUTY[5] = {12, 35, 80, 150, 255};
const uint8_t BL_GLIMMER = 6;                    // asleep: the big screen is off, the small one shows the time on the shared BL
uint8_t blNow = 0;
void blWrite(int v) { blNow = (uint8_t)v; analogWrite(TFT_BL, v); }
float batAvg = 0;
void batTick(uint32_t now) {                     // a reading a second, smoothed over ~10 s
  static uint32_t at = 0;
  if (batAvg > 0 && now - at < 1000) return;
  at = now;
  int mv = (int)analogReadMilliVolts(BAT_PIN) * 2;
  batAvg = batAvg > 0 ? batAvg + (mv - batAvg) * 0.1f : mv;
}
int batMv() { if (batAvg <= 0) batTick(millis()); return (int)batAvg; }
int batPct() { return constrain((batMv() - 3300) * 100 / 850, 0, 100); }   // Li-ion 3.3 .. 4.15 V
bool batThere() { return batMv() > 2900; }       // (under that: no cell on the line, USB only)

// ================= THE SMALL SCREEN: GMT130-V1.0, 240 x 240 (oled240.h) =================
// Everything still draws a 128 x 64 picture (OledCanvas: Adafruit GFX, the SSD1306's layout); oled240.h shows it 1.875x
// bigger and smooth, between a coloured bar above (time, battery) and a dark one below (Wi-Fi, connection), and sends
// it from a task of its own (core 0), setting the panel up again every 5 s. Its backlight is the big screen's (D32).
OledCanvas oled;
void oledShow() { o240Show(oled.getBuffer()); }   // the picture in oled's buffer -> the small screen's task
void oledCard(const char* title, const char* a, const char* b, const char* c, const char* d) {   // the Psion's OLED card
  oled.clearDisplay(); oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(2); oled.setCursor(0, 0); oled.print(title);
  oled.setTextSize(1); oled.drawFastHLine(0, 17, 128, SSD1306_WHITE);
  oled.setCursor(0, 21); oled.print(a); oled.setCursor(0, 32); oled.print(b);
  oled.setCursor(0, 43); oled.print(c); oled.setCursor(0, 54); oled.print(d);
}
bool oledDirty = true;

// ================= INPUT: knob (interrupts), its button + the cross pad's OK, the arrows =================
volatile int32_t encCount = 0;
volatile int8_t encSub = 0;
volatile uint8_t encPrev = 3;
const int8_t ENC_TABLE[16] = {0, 1, -1, 0, -1, 0, 0, 1, 1, 0, 0, -1, 0, -1, 1, 0};
void IRAM_ATTR encISR() {
  uint32_t in = REG_READ(GPIO_IN_REG);
  uint8_t s = (((in >> ENC_CLK) & 1) << 1) | ((in >> ENC_DT) & 1);
  encSub += ENC_TABLE[(encPrev << 2) | s];
  encPrev = s;
  if (s != 3) return;                            // count only back at rest (bounce cancels itself)
  if (encSub >= 2) encCount = encCount + 1; else if (encSub <= -2) encCount = encCount - 1;
  encSub = 0;
}
int takeClicks() { noInterrupts(); int32_t c = encCount; encCount = 0; interrupts(); return (int)c * (S.knobRev ? -1 : 1); }
enum { PD_NONE, PD_UP, PD_DOWN, PD_LEFT, PD_RIGHT };
const int8_t PAD_PINS[4] = {PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT};
uint8_t padEv = PD_NONE, padHeld = PD_NONE, padRep = 0;
uint8_t padOkSt = 0, padDirOut = 0;              // (the Psion's 4.1.1 rule: OK that comes with an arrow is the arrow's)
uint32_t padOkT = 0;
uint8_t padScan() {
  uint8_t dirs = 0;
  for (int i = 0; i < 4; i++) if (digitalRead(PAD_PINS[i]) == LOW) dirs |= 1 << i;
  bool ok = digitalRead(PAD_OK) == LOW;
  uint32_t t = millis();
  padDirOut &= dirs;
  if (!ok) padOkSt = 0;
  else if (padOkSt == 0) { padOkSt = dirs ? 3 : 1; padOkT = t; }
  else if (padOkSt == 1) { if (dirs) padOkSt = 3; else if (t - padOkT >= 50) padOkSt = 2; }
  else if (padOkSt == 2 && t - padOkT < 250) padDirOut |= dirs;
  return dirs & ~padDirOut;
}
int padTake() { int e = padEv; padEv = PD_NONE; return e; }
void padPoll(uint32_t now) {                     // a tap = one step; held = repeats, quicker after 8
  static uint8_t cand = 0, stable = 0;
  static uint32_t tCand = 0, nextRep = 0;
  uint8_t raw = 0, b = padScan();
  for (int i = 0; i < 4; i++) if (b >> i & 1) raw = i + 1;
  if (raw != cand) { cand = raw; tCand = now; }
  if (cand != stable && now - tCand >= 20) {
    stable = cand; padHeld = stable;
    if (stable) { padEv = stable; padRep = 0; nextRep = now + 330; }
  } else if (stable && (int32_t)(now - nextRep) >= 0) {
    if (!padEv) padEv = stable;
    if (padRep < 255) padRep++;
    nextRep = now + (padRep > 8 ? 40 : 85);
  }
}
bool fShort = false, fLong = false, fSleep = false;
bool takeShort() { bool r = fShort; fShort = false; return r; }
uint32_t lastInput = 0;
void buttonPoll() {                              // click = OK; hold 0.6 s = back; hold 2.5 s = sleep
  static bool last = true, used = false, sleepSent = false;
  static uint32_t tDown = 0, tChange = 0;
  uint32_t t = millis();
  bool up = digitalRead(BTN_PIN) && padOkSt != 2;
  if (up != last && t - tChange > 30) {
    tChange = t; last = up;
    if (!up) { tDown = t; used = false; sleepSent = false; }
    else if (!used) fShort = true;
  }
  if (!last && !used && t - tDown >= 600) { used = true; fLong = true; }
  if (!last && !sleepSent && t - tDown >= 2500) { sleepSent = true; fSleep = true; }
}

// ================= SOUND: a 16 kHz tone on the DACs (the Psion's way: resting at mid-level, no thump) =================
// Stereo: a left turn beeps on the left speaker, a right turn on the right one (Settings: Stereo). Quiet rest: after 4 s
// of silence the DACs glide down to 0 V (no hum from the radios), and back up just before the next sound.
hw_timer_t* tgTimer = nullptr;
volatile uint32_t tgPh = 0, tgInc = 0;
volatile int16_t tgAmp = 0, tgTarget = 0;
volatile uint8_t tgPanL = 255, tgPanR = 255;
bool spkReady = false;
uint8_t spkLevel = 0;
uint32_t spkSoundAt = 0;
inline void dacL(uint8_t v) { SET_PERI_REG_BITS(RTC_IO_PAD_DAC1_REG, RTC_IO_PDAC1_DAC, v, RTC_IO_PDAC1_DAC_S); }
inline void dacR(uint8_t v) { SET_PERI_REG_BITS(RTC_IO_PAD_DAC2_REG, RTC_IO_PDAC2_DAC, v, RTC_IO_PDAC2_DAC_S); }
inline void dacBoth(uint8_t v) { dacL(v); dacR(v); }
void tgIsr() {                                   // one sample: a square wave with smooth edges, faded in / out
  int16_t a = tgAmp, t = tgTarget;
  if (a < t) { a += 6; if (a > t) a = t; } else if (a > t) { a -= 6; if (a < t) a = t; }
  tgAmp = a;
  uint32_t was = tgPh;
  tgPh += tgInc;
  bool hi = tgPh & 0x80000000u;
  int v = hi ? a : -a;
  if (hi != (bool)(was & 0x80000000u)) {
    uint32_t into = tgPh & 0x7FFFFFFFu;
    int d = (int)((into >> 12) * (uint32_t)(2 * a) / ((tgInc >> 12) | 1));
    if (d > 2 * a) d = 2 * a;
    v = hi ? d - a : a - d;
  }
  dacL((uint8_t)(128 + v * tgPanL / 255));
  dacR((uint8_t)(128 + v * tgPanR / 255));
}
void spkUp() {                                   // power-on: both DACs rise slowly to mid-level (~0.26 s)
  dacWrite(DAC_L, 0); dacWrite(DAC_R, 0);
  for (int v = 1; v <= 128; v++) { dacBoth(v); delay(2); }
  spkReady = true; spkLevel = 128; spkSoundAt = millis();
}
void spkWake() {
  spkSoundAt = millis();
  if (!spkReady || spkLevel >= 128) return;
  int from = spkLevel;
  for (int i = 1; i <= 60; i++) { float t = i / 60.f; t = t * t * (3.f - 2.f * t); dacBoth((uint8_t)(from + (128 - from) * t)); delayMicroseconds(1000); }
  spkLevel = 128;
}
const uint16_t* sndTune = nullptr;
uint8_t sndLen = 0, sndIdx = 0, sndPan = 0;      // pan: 0 both, 1 left, 2 right
uint32_t sndEnd = 0, sndOffAt = 0;
bool sndOn = false;
void toneOn(uint16_t f, uint8_t pan) {
  static const uint8_t AMP[4] = {0, 25, 55, 110};
  if (!S.volume || !spkReady) return;
  spkWake();
  bool st = S.stereo;
  tgPanL = !st || pan != 2 ? 255 : 0;
  tgPanR = !st ? 255 : pan != 1 ? 255 : 0;
  tgInc = (uint32_t)((uint64_t)f * 4294967296ULL / 16129);
  tgTarget = AMP[S.volume & 3];
  if (!tgTimer) {
    tgTimer = timerBegin(1000000);
    if (!tgTimer) return;
    timerAttachInterrupt(tgTimer, &tgIsr);
    timerAlarm(tgTimer, 62, true, 0);
  }
  sndOn = true;
}
void toneOff() { tgTarget = 0; if (sndOn) sndOffAt = millis(); sndOn = false; }
void sndStart() {
  uint16_t f = sndTune[sndIdx * 2], ms = sndTune[sndIdx * 2 + 1];
  if (f) toneOn(f, sndPan); else toneOff();
  sndEnd = millis() + ms;
}
void play(const uint16_t* t, uint8_t n, uint8_t pan) { sndTune = t; sndLen = n; sndIdx = 0; sndPan = pan; sndStart(); }
#define PLAY(t) play(t, sizeof(t) / sizeof(t[0]) / 2, 0)
#define PLAYP(t, p) play(t, sizeof(t) / sizeof(t[0]) / 2, p)
void sndTick(uint32_t now) {
  if (sndTune && (int32_t)(now - sndEnd) >= 0) {
    if (++sndIdx < sndLen) sndStart(); else { sndTune = nullptr; toneOff(); }
  }
  if (tgTimer && !sndOn && now - sndOffAt > 250) { timerEnd(tgTimer); tgTimer = nullptr; tgAmp = 0; dacBoth(spkLevel); }
  static uint32_t at = 0;                        // the quiet rest: down to 0 V after 4 s without a sound
  if (spkReady && !tgTimer && spkLevel && now - spkSoundAt > 4000 && now - at >= 4) { at = now; dacBoth(--spkLevel); }
}
const uint16_t T_CLICK[] = {2400, 3};
const uint16_t T_OK[] = {880, 25};
const uint16_t T_NO[] = {220, 120};
const uint16_t T_BOOT[] = {523, 90, 659, 90, 784, 90, 1047, 200};
const uint16_t T_GOOD[] = {784, 70, 1047, 140};
const uint16_t T_AHEAD[] = {988, 45};
const uint16_t T_LEFT[] = {784, 70, 0, 25, 587, 110};     // falling = left
const uint16_t T_RIGHT[] = {587, 70, 0, 25, 784, 110};    // rising = right
const uint16_t T_BACK[] = {784, 60, 587, 60, 784, 60, 587, 90};
const uint16_t T_STOP[] = {659, 80, 784, 80, 1047, 150};
const uint16_t T_END[] = {523, 100, 659, 100, 784, 100, 1047, 300};
const uint16_t T_LOW[] = {440, 120, 0, 60, 330, 200};
void click() { PLAY(T_CLICK); }

// ================= WI-FI (only for the Ruta Marciana server) =================
// (Standing rule of the Psion firmware: 240 MHz BEFORE a radio starts. This OS stays at 240 MHz anyway.)
void radioPrep() { if (getCpuFrequencyMhz() != 240) setCpuFrequencyMhz(240); }
bool wifiSaved() { wifi_config_t c; return esp_wifi_get_config(WIFI_IF_STA, &c) == ESP_OK && c.sta.ssid[0]; }
bool online() { return WiFi.status() == WL_CONNECTED; }
uint32_t wdOffAt = 0, wdGap = 20000;
uint16_t wdTries = 0;
bool wifiJoining = false;
void wifiStart() {
  if (!S.wifiOn) { WiFi.mode(WIFI_OFF); return; }
  radioPrep();
  WiFi.persistent(true);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  if (wifiSaved()) WiFi.begin();
  configTzTime("CST6", "pool.ntp.org");          // (the title bar's clock: central Mexico, no daylight saving)
}
void wifiWatch(uint32_t now) {                   // (the Psion 4.3.1 watchdog) offline 20 s with a saved network: try again
  static uint32_t at = 0;
  if (now - at < 1000) return;
  at = now;
  if (!S.wifiOn || WiFi.getMode() == WIFI_OFF || wifiJoining || WiFi.scanComplete() == WIFI_SCAN_RUNNING || !wifiSaved()) { wdOffAt = 0; return; }
  if (online()) { wdOffAt = 0; wdGap = 20000; wdTries = 0; return; }
  if (!wdOffAt) { wdOffAt = now | 1; return; }
  if (now - wdOffAt < wdGap) return;
  wdOffAt = now | 1; wdGap = wdGap >= 150000 ? 300000 : wdGap * 2;
  if (++wdTries % 3) WiFi.reconnect();
  else { WiFi.mode(WIFI_OFF); radioPrep(); WiFi.mode(WIFI_STA); WiFi.begin(); }
}
bool clockText(char* b, int n) {                 // "14:05" once the time came from the internet
  struct tm t;
  if (!getLocalTime(&t, 0) || t.tm_year < 120) return false;
  snprintf(b, n, "%02d:%02d", t.tm_hour, t.tm_min);
  return true;
}

// ================= CHROME (1.3: calm and clean): a title bar, the content, one hint line =================
const int TOP = 22, FOOT = 226;                  // the content sits between the title bar and the hint line
// The language (1.3): Espanol / English, every text on both screens. Saved on its own (Preferences "lang"), so the
// settings block keeps its layout and nothing you set before is lost. mrLang lives in marte_core.h (the directions).
inline const char* L(const char* es, const char* en) { return mrLang ? en : es; }
void drawBattery(int x, int y) {
  int p = batPct();
  uint16_t c = !batThere() ? C_MUTED : p > 50 ? C_GREEN : p > 20 ? C_YELLOW : C_RED;
  tft.fillRect(x, y, 27, 12, C_BAR); tft.drawRect(x, y, 24, 12, c); tft.fillRect(x + 24, y + 3, 2, 6, c);
  char b[6]; snprintf(b, sizeof b, batThere() ? "%d" : "USB", p);
  putCenter(x + 12, y + 2, 1, c, C_BAR, b);
}
char titleNow[24] = "";
void drawStatus() {                              // the title bar's right end: a Wi-Fi dot, the battery, the clock
  char b[8];
  tft.fillRect(190, 0, W - 190, TOP, C_BAR);
  bool clk = clockText(b, sizeof b);
  int x = W - 6;
  if (clk) { x -= textW(b, 2); putText(x, 4, 2, C_TEXT, C_BAR, b); x -= 8; }
  x -= 27; drawBattery(x, 5);
  tft.fillCircle(x - 9, 11, 3, online() ? C_GREEN : C_LINE);   // green: Wi-Fi connected
}
void drawTitle(const char* t) {
  snprintf(titleNow, sizeof titleNow, "%s", t);
  tft.fillRect(0, 0, W, TOP, C_BAR);
  tft.fillRect(0, 0, 4, TOP, C_ORANGE);
  putText(12, 4, 2, C_TEXT, C_BAR, t);
  drawStatus();
}
void drawHints(const char* t) {                  // the one hint line, the same place on every screen
  tft.fillRect(0, FOOT, W, H - FOOT, C_BAR);
  putText(8, FOOT + 4, 1, C_MUTED, C_BAR, t);
}
// The weather's name in the language: the web's scenarios are clear / dust / storm; any other keeps the web's words.
const char* wxKeyName(const char* key) {
  if (!strcmp(key, "clear")) return L("Despejado", "Clear");
  if (!strcmp(key, "dust")) return L("Polvo", "Dust");
  if (!strcmp(key, "storm")) return L("Tormenta", "Storm");
  return nullptr;
}
void wxShort(char* out, int n) {                 // one word for the weather now
  const char* k = wxKeyName(mrEnv.key);
  if (k) { snprintf(out, n, "%s", k); return; }
  int i = 0;
  while (mrEnv.label[i] && mrEnv.label[i] != ' ' && i < n - 1) { out[i] = mrEnv.label[i]; i++; }
  out[i] = 0;
}
void wxLabel(const char* key, const char* label, char* out, int n) {   // a scenario's whole name
  if (mrLang && !strcmp(key, "clear")) snprintf(out, n, "Clear (simulated)");
  else if (mrLang && !strcmp(key, "dust")) snprintf(out, n, "Dusty air (simulated)");
  else if (mrLang && !strcmp(key, "storm")) snprintf(out, n, "Local dust storm (simulated)");
  else snprintf(out, n, "%s", label);
}

// ---------- a box with a message (press to close) ----------
bool msgOpen = false;
void msgBox(const char* title, const char* text, uint16_t accent) {
  int x = 22, y = 52, w = W - 44, h = 140;
  tft.fillRect(x + 4, y + 4, w, h, RGB(4, 6, 6));
  tft.fillRect(x, y, w, h, C_PANEL); tft.drawRect(x, y, w, h, accent);
  tft.fillRect(x + 1, y + 1, w - 2, 16, accent);
  putText(x + 8, y + 5, 1, C_ONOR, accent, title);
  putWrap(x + 10, y + 26, 44, 9, 11, C_TEXT, C_PANEL, text);
  putText(x + 10, y + h - 14, 1, C_MUTED, C_PANEL, L("OK: cerrar", "OK: close"));
  msgOpen = true;
  oledDirty = true;                              // (1.0 left the OLED on "espera..." after a server answer)
}
void msgBox(const char* title, const char* text) { msgBox(title, text, C_ORANGE); }
// ---------- the popup menu (the Psion's, in the supplied shell colours) ----------
const int PM_MAX = 12, PM_W = 210;
char pmItem[PM_MAX][36], pmTitle[32];
int pmN = 0, pmSel = 0, pmX = 0, pmY = 0;
bool pmOpen = false;
void pmDrawItem(int i) {
  int y = pmY + 17 + i * 15;
  bool s = i == pmSel;
  tft.fillRect(pmX + 1, y, PM_W - 2, 15, s ? C_ORANGE : C_PANEL);
  putText(pmX + 9, y + 4, 1, s ? C_ONOR : C_TEXT, s ? C_ORANGE : C_PANEL, pmItem[i]);
}
void pmShow(const char* title) {                 // pmItem[] / pmN filled
  pmSel = 0; pmOpen = true;
  snprintf(pmTitle, sizeof pmTitle, "%s", title);
  int h = 17 + pmN * 15 + 3;
  pmX = (W - PM_W) / 2; pmY = max(22, (H - h) / 2);
  tft.fillRect(pmX + 4, pmY + 4, PM_W, h, RGB(4, 6, 6));
  tft.fillRect(pmX, pmY, PM_W, h, C_PANEL); tft.drawRect(pmX, pmY, PM_W, h, C_LINE);
  tft.fillRect(pmX + 1, pmY + 1, PM_W - 2, 14, C_BAR);
  putText(pmX + 6, pmY + 4, 1, C_ORANGE, C_BAR, pmTitle);
  for (int i = 0; i < pmN; i++) pmDrawItem(i);
}
int pmTick() {                                   // -1 while open, else the chosen item
  int c = takeClicks(), pd = padTake();
  if (pd == PD_UP || pd == PD_LEFT) c--;
  if (pd == PD_DOWN || pd == PD_RIGHT) c++;
  if (c) { int o = pmSel; pmSel = ((pmSel + c) % pmN + pmN) % pmN; if (o != pmSel) { pmDrawItem(o); pmDrawItem(pmSel); click(); } }
  if (!takeShort()) return -1;
  pmOpen = false; PLAY(T_OK);
  return pmSel;
}

// ---------- the knob keyboard (Wi-Fi password, mission code, the PC's address) ----------
const char* const KB_PAGES[3] = {"abcdefghijklmnopqrstuvwxyz0123456789-_.@", "ABCDEFGHIJKLMNOPQRSTUVWXYZ!#$%&*+=/:;?,~", "0123456789.:/-_()[]{}<>'\"|\\`^\xA4\xA5\xA0\x82\xA1\xA2\xA3\x81!?"};
enum { KB_PAGE = 40, KB_SPACE, KB_DEL, KB_OK, KB_CANCEL, KB_N };
bool kbOpen = false;
int kbKey = 0, kbPage = 0, kbMax = 32, kbPurpose = 0;
char kbBuf[72], kbPrompt[48];
void kbRect(int i, int* x, int* y, int* w, int* h) {
  if (i < 40) { *x = 5 + (i % 10) * 31; *y = 70 + (i / 10) * 24; *w = 29; *h = 22; }
  else { *x = 5 + (i - 40) * 62; *y = 70 + 4 * 24; *w = 60; *h = 24; }
}
void kbDrawKey(int i) {
  int x, y, w, h; kbRect(i, &x, &y, &w, &h);
  bool s = i == kbKey;
  uint16_t bg = s ? C_ORANGE : i < 40 ? C_PANEL2 : C_PANEL, fg = s ? C_ONOR : C_TEXT;
  tft.fillRect(x, y, w, h, bg);
  if (i < 40) { char c[2] = {KB_PAGES[kbPage][i], 0}; putText(x + (w - 12) / 2, y + 3, 2, fg, bg, c); }
  else {
    static const char* const KL[2][5] = {{"", "espacio", "borrar", "OK", "cancelar"}, {"", "space", "delete", "OK", "cancel"}};
    const char* l = i == KB_PAGE ? (kbPage == 0 ? "ABC" : kbPage == 1 ? "123" : "abc") : KL[mrLang][i - 40];
    putCenter(x + w / 2, y + 8, 1, !s && i == KB_OK ? C_GREEN : !s && i == KB_CANCEL ? C_RED : fg, bg, l);
  }
}
void kbDrawField() {
  tft.fillRect(5, 40, W - 10, 24, C_PANEL2); tft.drawRect(5, 40, W - 10, 24, C_ORANGE);
  int n = strlen(kbBuf), from = n > 24 ? n - 24 : 0;
  bool hide = kbPurpose == 1 && n;                 // (a password: shown, so you can check it, but small)
  putText(10, 44, 2, C_TEXT, C_PANEL2, kbBuf + from);
  tft.fillRect(10 + (n - from) * 12, 44, 2, 16, C_ORANGE);
  (void)hide;
}
void kbBegin(int purpose, const char* prompt, const char* init, int maxLen, int page) {
  kbOpen = true; kbPurpose = purpose; kbMax = min(maxLen, (int)sizeof kbBuf - 1); kbKey = 0; kbPage = page;
  snprintf(kbPrompt, sizeof kbPrompt, "%s", prompt);
  snprintf(kbBuf, sizeof kbBuf, "%s", init);
  tft.fillRect(0, TOP, W, H - TOP, C_BG);
  putText(6, 28, 1, C_ORANGE, C_BG, kbPrompt);
  kbDrawField();
  for (int i = 0; i < KB_N; i++) kbDrawKey(i);
  drawHints(L("flechas / perilla: tecla   OK: escribir   mantener: salir", "arrows / knob: key   OK: type   hold: cancel"));
}
int kbTick() {                                   // 0 typing, 1 done (kbBuf), -1 cancelled
  int pd = padTake(), c = takeClicks(), old = kbKey;
  if (pd) {
    if (kbKey < 40) {
      int r = kbKey / 10, col = kbKey % 10;
      if (pd == PD_LEFT) kbKey = r * 10 + (col + 9) % 10;
      else if (pd == PD_RIGHT) kbKey = r * 10 + (col + 1) % 10;
      else if (pd == PD_UP) kbKey = r ? kbKey - 10 : 40 + min(col / 2, 4);
      else kbKey = r < 3 ? kbKey + 10 : 40 + min(col / 2, 4);
    } else {
      int a = kbKey - 40;
      if (pd == PD_LEFT) kbKey = 40 + (a + 4) % 5; else if (pd == PD_RIGHT) kbKey = 40 + (a + 1) % 5;
      else kbKey = pd == PD_UP ? 30 + a * 2 : a * 2;
    }
  }
  if (c) kbKey = ((kbKey + c) % KB_N + KB_N) % KB_N;
  if (kbKey != old) { kbDrawKey(old); kbDrawKey(kbKey); click(); }
  if (!takeShort()) return 0;
  PLAY(T_OK);
  int n = strlen(kbBuf);
  if (kbKey < 40) { if (n < kbMax) { kbBuf[n] = KB_PAGES[kbPage][kbKey]; kbBuf[n + 1] = 0; } kbDrawField(); return 0; }
  switch (kbKey) {
    case KB_PAGE: kbPage = (kbPage + 1) % 3; for (int i = 0; i < KB_N; i++) kbDrawKey(i); return 0;
    case KB_SPACE: if (n < kbMax) { kbBuf[n] = ' '; kbBuf[n + 1] = 0; } kbDrawField(); return 0;
    case KB_DEL: if (n) kbBuf[n - 1] = 0; kbDrawField(); return 0;
    case KB_OK: kbOpen = false; return 1;
    default: kbOpen = false; return -1;
  }
}

// ================= THE SCREENS =================
enum { SC_HOME, SC_MAP, SC_MISSIONS, SC_VIEW, SC_WEATHER, SC_SITES, SC_FACTS, SC_WIFI, SC_SET, SC_ABOUT, SC_GUIDE };
int scr = SC_HOME;
void go(int s);
#include "ruta_guide.h"
const int HM_N = 8;                              // (the home grid = SC_MAP .. SC_SET in order; ABOUT is in SETTINGS)
const uint8_t HOME_ICON[HM_N] = {0, 1, 7, 8, 2, 3, 4, 5};
const char* homeName(int i) {
  static const char* const N[2][HM_N] = {{"MAPA", "MISIONES", "VISTA 3D", "CLIMA", "SITIOS", "MARTE", "WI-FI", "AJUSTES"},
                                         {"MAP", "MISSIONS", "3D VIEW", "WEATHER", "SITES", "MARS", "WI-FI", "SETTINGS"}};
  return N[mrLang][i];
}
const char* homeDesc(int i) {
  static const char* const D[2][HM_N] = {
    {"Planea y camina rutas en Glen Torridon", "Guarda tus rutas y comp\xA0rtelas con la web", "Tu ruta en el 3D de la web (con el relay)",
     "Polvo y tormentas: cambian tu ruta", "Los 136 lugares que estudi\xA2 Curiosity", "Datos de NASA sobre el planeta",
     "La red para el relay y el servidor", "Idioma, pantalla, sonido y conexi\xA2n"},
    {"Plan and walk routes in Glen Torridon", "Save your routes and share them with the web", "Your route in the web's 3D (with the relay)",
     "Dust and storms: they change your route", "The 136 places Curiosity studied", "NASA facts about the planet",
     "The network for the relay and the server", "Language, screen, sound and connection"}};
  return D[mrLang][i];
}
int homeSel = 0;
// ---------- live sync with the web (through the Mars relay: see SYNC below) ----------
bool syncFail = false;                           // the last try didn't reach the relay / the web
bool syncOn() { return S.sync && S.conn == 0; }

// ---------- the map (the old MARTE app, now the heart of this OS) ----------
enum { MR_MAP, MR_WALK, MR_SLOPE, MR_INFO };
const int MR_MX = 3, MR_MY = 25, MR_PX = 251, MR_BY = 172;   // (1.3: under the taller title bar)
const uint16_t MR_SPEEDS[5] = {1, 5, 20, 100, 200};
uint8_t mrMode = MR_MAP;
int mrCx = 10, mrCy = 26, mrSiteI = 0;
double mrWalked = 0, mrSegAt = 0;
int mrSeg = 0, mrCueKey = -1, mrWx = -1, mrWy = -1;
uint32_t mrLast = 0, mrCueAt = 0, mrPanelAt = 0;
bool mrPaused = false, mrDone = false;
char mrMsg[110] = "";
uint16_t mrColor(int x, int y) {
  uint16_t z = mrM->z[y * MR_W + x];
  if (z & 0x8000) return C_ORANGE;                               // the route
  const float k = 4.f / (200.f * (float)MR_CELL);                // slope per cm of difference, heights x4 so the relief shows
  float gx = (mrZat(x + 1, y) - mrZat(x - 1, y)) * k, gy = (mrZat(x, y + 1) - mrZat(x, y - 1)) * k;
  float sh = (gx + gy + 1.f) / (1.7320508f * sqrtf(gx * gx + gy * gy + 1.f));   // light from the north-west
  float b = constrain(0.3f + 1.2f * sh, 0.2f, 1.35f);
  int r, g, bl;
  if (S.mapGrey) { r = g = bl = (int)(165 * b); }                // grey, like the HiRISE photo
  else {                                                         // Ruta Marciana's height colours (terrain-style.js)
    static const uint8_t PAL[6][3] = {{40, 73, 79}, {66, 113, 108}, {137, 149, 105}, {190, 150, 105}, {227, 194, 148}, {248, 240, 219}};
    float t = (z & 0x3FFF) * (5.f / 14631.f);
    int i = min(4, (int)t); float f = t - i;
    r = (int)((PAL[i][0] + (PAL[i + 1][0] - PAL[i][0]) * f) * b);
    g = (int)((PAL[i][1] + (PAL[i + 1][1] - PAL[i][1]) * f) * b);
    bl = (int)((PAL[i][2] + (PAL[i + 1][2] - PAL[i][2]) * f) * b);
  }
  return RGB(min(r, 255), min(g, 255), min(bl, 255));
}
inline bool mrSteep(int x, int y) { return !S.steepHide && !(mrM->z[y * MR_W + x] & 0xC000); }   // a yellow checker
void mrMarks(int x0, int y0, int x1, int y1) {   // sites, stops, cursor, walker: whatever touches the cells
  for (int s = 0; s < MR_NSITES; s++) {
    int x = MR_SITES[s].x, y = MR_SITES[s].y;
    if (x >= x0 && x <= x1 && y >= y0 && y <= y1 && !(mrM->z[y * MR_W + x] & 0x8000)) tft.fillRect(MR_MX + 2 * x, MR_MY + 2 * y, 2, 2, C_CYAN);
  }
  MrRoute& R = mrM->r;
  for (int k = 0; k < R.nStops; k++) {
    int x = R.stop[k] % MR_W, y = R.stop[k] / MR_W;
    if (x < x0 - 2 || x > x1 + 2 || y < y0 - 2 || y > y1 + 2) continue;
    tft.fillRect(MR_MX + 2 * x - 2, MR_MY + 2 * y - 2, 6, 6, C_BG);
    tft.fillRect(MR_MX + 2 * x - 1, MR_MY + 2 * y - 1, 4, 4, k ? C_TEXT : C_ORANGE);
  }
  if (mrMode != MR_WALK && mrCx >= x0 - 2 && mrCx <= x1 + 2 && mrCy >= y0 - 2 && mrCy <= y1 + 2) {
    tft.drawRect(MR_MX + 2 * mrCx - 3, MR_MY + 2 * mrCy - 3, 8, 8, C_TEXT);
    tft.drawRect(MR_MX + 2 * mrCx - 4, MR_MY + 2 * mrCy - 4, 10, 10, C_BG);
  }
  if (mrMode == MR_WALK && mrWx >= 0) {
    int cx = (mrWx - MR_MX) / 2, cy = (mrWy - MR_MY) / 2;
    if (cx >= x0 - 3 && cx <= x1 + 3 && cy >= y0 - 3 && cy <= y1 + 3) {   // the astronaut: white helmet, orange visor
      tft.fillCircle(mrWx, mrWy, 3, C_TEXT);
      tft.fillRect(mrWx - 1, mrWy - 1, 3, 2, C_ORANGE);
    }
  }
}
void mrDrawCells(int x0, int y0, int x1, int y1) {
  x0 = max(0, x0); y0 = max(0, y0); x1 = min(MR_W - 1, x1); y1 = min(MR_H - 1, y1);
  if (x0 > x1 || y0 > y1) return;
  int w = (x1 - x0 + 1) * 2;
  static uint16_t row[MR_W * 2], row2[MR_W * 2];
  tft.startWrite();
  tft.setAddrWindow(MR_MX + 2 * x0, MR_MY + 2 * y0, w, (y1 - y0 + 1) * 2);
  for (int y = y0; y <= y1; y++) {
    for (int x = x0; x <= x1; x++) {
      uint16_t c = mrColor(x, y), *a = row + (x - x0) * 2, *b = row2 + (x - x0) * 2;
      if (mrInZone(y * MR_W + x)) { uint16_t d = (c >> 1) & 0x7BEF; a[0] = b[1] = C_RED; a[1] = b[0] = d; }   // closed by the weather
      else if (mrSteep(x, y)) { uint16_t d = (c >> 1) & 0x7BEF; a[0] = b[1] = C_YELLOW; a[1] = b[0] = d; }
      else a[0] = a[1] = b[0] = b[1] = c;
    }
    tft.writePixels(row, w);
    tft.writePixels(row2, w);
  }
  tft.endWrite();
  mrMarks(x0, y0, x1, y1);
}
void mrDrawMap() { mrDrawCells(0, 0, MR_W - 1, MR_H - 1); }
void mrSide() {                                  // (1.3) what the cursor is on: its name, two numbers, the slope
  char b[40], nm[MR_NAME];
  int x = MR_PX + 4, y = MR_MY + 3, s = mrSiteAt(mrCx, mrCy);
  tft.fillRect(MR_PX, MR_MY - 1, W - MR_PX, MR_H * 2 + 2, C_PANEL);
  if (s >= 0) {
    const MrSite& T = MR_SITES[s];
    snprintf(b, sizeof b, "%s %d", L("SITIO", "SITE"), s + 1); putText(x, y, 1, C_ORANGE, C_PANEL, b); y += 14;
    mrName(s, nm, sizeof nm);
    for (int at = 0, k = 0; nm[at] && k < 4; k++, y += 10) { at = mrWrap(nm, at, 10, b); putText(x, y, 1, C_TEXT, C_PANEL, b); }
    y += 8;
    snprintf(b, sizeof b, "%s %d", L("vetas", "veins"), T.veins); putText(x, y, 1, C_CYAN, C_PANEL, b); y += 10;
    snprintf(b, sizeof b, "%s %d", L("n\xA2" "dulos", "nodules"), T.nodules); putText(x, y, 1, C_CYAN, C_PANEL, b);
  } else {
    putText(x, y, 1, C_ORANGE, C_PANEL, L("PUNTO", "POINT")); y += 14;
    putText(x, y, 1, C_MUTED, C_PANEL, L("sin sitio", "no site"));
  }
  double sl = mrSlope(mrCx, mrCy);
  putText(x, MR_MY + MR_H * 2 - 24, 1, C_MUTED, C_PANEL, L("pendiente", "slope"));
  snprintf(b, sizeof b, "%d\xF8", (int)lround(sl));
  putText(x, MR_MY + MR_H * 2 - 13, 1, sl > S.lim ? C_YELLOW : C_TEXT, C_PANEL, b);
}
void mrMsgLines(int y, uint16_t c) {             // a message, on up to two lines
  char l1[56];
  int at = mrWrap(mrMsg, 0, 51, l1); putText(6, y, 1, c, C_BG, l1);
  if (mrMsg[at]) { mrWrap(mrMsg, at, 51, l1); putText(6, y + 9, 1, c, C_BG, l1); }
}
void mrBottomMap() {                             // (1.3) the stops, the route in big letters, one quiet line
  MrRoute& R = mrM->r;
  char b[64], nm[MR_NAME];
  tft.fillRect(0, MR_BY, W, FOOT - MR_BY, C_BG);
  if (!R.nStops) snprintf(b, sizeof b, "%s", L("Sin ruta: elige un lugar y pulsa OK.", "No route yet: pick a spot and press OK."));
  else {
    b[0] = 0;
    for (int k = 0; k < R.nStops && (int)strlen(b) < 51; k++) {
      mrStopName(k, nm, sizeof nm);
      int l = strlen(b);
      snprintf(b + l, sizeof b - l, "%s%s", k ? " > " : "", nm);
    }
    if (strlen(b) > 51) strcpy(b + 49, "..");
  }
  putText(6, MR_BY + 4, 1, R.nStops ? C_TEXT : C_MUTED, C_BG, b);
  if (R.nStops >= 2 && R.ok) {
    snprintf(b, sizeof b, "%.2f km  +%.1f m  %d min", R.dist / 1000, R.climb, R.minutes);
    putText(6, MR_BY + 17, 2, C_ORANGE, C_BG, b);
  } else if (R.nStops == 1) putText(6, MR_BY + 21, 1, C_MUTED, C_BG, L("Agrega otra parada para ver la ruta.", "Add one more stop to see the route."));
  if (mrMsg[0]) mrMsgLines(MR_BY + 36, C_YELLOW);
  else {                                         // the quiet line: the slope limit, the weather, live
    char wx[20] = "";
    if (strcmp(mrEnv.key, "clear")) wxShort(wx, sizeof wx);
    snprintf(b, sizeof b, "%s %d\xF8%s%s%s", L("pendiente m\xA0x.", "max slope"), S.lim, wx[0] ? "   " : "", wx,
             !syncOn() ? "" : syncFail ? "   WEB?" : L("   EN VIVO", "   LIVE"));
    putText(6, MR_BY + 40, 1, wx[0] ? C_YELLOW : C_MUTED, C_BG, b);
  }
  drawHints(L("OK: men\xA3   perilla: sitios   mantener: inicio", "OK: menu   knob: sites   hold: home"));
}
void mrNav(int* idx, double* rem) {              // navigation.js's update(): the direction now and how far it is
  MrRoute& R = mrM->r;
  int i = R.nSteps - 1;
  if (!mrDone) { for (i = 1; i < R.nSteps - 1 && R.step[i].dist < mrWalked - 3; i++) {} }
  *idx = i; *rem = max(0.0, (double)R.step[i].dist - mrWalked);
}
void mrBottomWalk() {                            // (1.3) the next direction, big; how far to go
  MrRoute& R = mrM->r;
  char b[120], l[56];
  int idx; double rem; mrNav(&idx, &rem);
  tft.fillRect(0, MR_BY, W, FOOT - MR_BY, C_BG);
  snprintf(b, sizeof b, "%s x%u%s", L("CAMINANDO", "WALKING"), MR_SPEEDS[S.speedI], mrPaused ? L("   PAUSA", "   PAUSED") : "");
  putText(6, MR_BY + 4, 1, C_ORANGE, C_BG, b);
  if (!mrDone) { mrDist(R.dist3 - mrWalked, l, sizeof l); snprintf(b, sizeof b, "%s %s", l, L("al destino", "to go")); putRight(W - 6, MR_BY + 4, 1, C_MUTED, C_BG, b); }
  if (mrDone) snprintf(b, sizeof b, "%s", L("Llegaste", "You made it"));
  else if (rem <= 6) snprintf(b, sizeof b, "%s", L("Ahora", "Now"));
  else { mrDist(rem, l, sizeof l); snprintf(b, sizeof b, "%s %s", L("En", "In"), l); }
  putText(6, MR_BY + 17, 2, C_TEXT, C_BG, b);
  mrStepText(idx, !mrDone, b, sizeof b);
  int at = mrWrap(b, 0, 51, l); putText(6, MR_BY + 37, 1, C_CYAN, C_BG, l);
  if (b[at]) { mrWrap(b, at, 51, l); putText(6, MR_BY + 46, 1, C_CYAN, C_BG, l); }
  drawHints(mrDone ? L("OK: volver al mapa", "OK: back to the map") : L("perilla: velocidad   OK: pausa   mantener: salir", "knob: speed   OK: pause   hold: stop"));
}
void mrInfo() {                                  // (1.3) help, short
  static const char* const T[2][13] = {
    {"OJO DE MARTE  -  Ruta Marciana", "",
     "Glen Torridon, cr\xA0ter Gale: por donde anduvo", "Curiosity. Relieve real de HiRISE (NASA/JPL/UA).",
     "Turquesa: los 136 sitios de ChemCam.", "Amarillo: m\xA0s empinado que tu l\xA1mite.", "",
     "Flechas: mover. Perilla: de sitio en sitio.", "OK: men\xA3. Mantener: volver.",
     "Caminar: perilla = velocidad, OK = pausa.", "Pitidos: bajando = izquierda, subiendo = derecha.", "",
     "Simulaci\xA2n educativa, sin afiliaci\xA2n con la NASA."},
    {"OJO DE MARTE  -  Ruta Marciana", "",
     "Glen Torridon, Gale crater: where Curiosity", "drove. Real ground from HiRISE (NASA/JPL/UA).",
     "Teal: the 136 sites ChemCam studied.", "Yellow: steeper than your limit.", "",
     "Arrows: move. Knob: from site to site.", "OK: menu. Hold: back.",
     "Walking: knob = speed, OK = pause.", "Beeps: falling = left, rising = right.", "",
     "An educational simulation, not affiliated with NASA."}};
  tft.fillRect(0, TOP, W, FOOT - TOP, C_BG);
  for (int i = 0; i < 13; i++) putText(10, TOP + 10 + i * 14, 1, i ? C_TEXT : C_ORANGE, C_BG, T[mrLang][i]);
  drawHints(L("OK / mantener: volver al mapa", "OK / hold: back to the map"));
}
void mrSlopePanel() {                            // (1.3) the limit, big
  char b[16];
  tft.fillRect(0, MR_BY, W, FOOT - MR_BY, C_BG);
  putText(6, MR_BY + 4, 1, C_ORANGE, C_BG, L("PENDIENTE MAXIMA", "MAXIMUM SLOPE"));
  snprintf(b, sizeof b, "%d\xF8", S.lim); putText(6, MR_BY + 18, 3, C_TEXT, C_BG, b);
  putText(84, MR_BY + 20, 1, C_MUTED, C_BG, L("de 5 a 40 grados", "from 5 to 40 degrees"));
  putText(84, MR_BY + 31, 1, C_MUTED, C_BG, L("amarillo = muy empinado", "yellow = too steep"));
  drawHints(L("perilla / flechas: cambiar   OK: usar", "knob / arrows: change   OK: use it"));
}
void mrDrawAll() {
  tft.fillScreen(C_BG);
  drawTitle(mrMode == MR_INFO ? L("Ayuda", "Help") : L("Mapa", "Map"));
  oledDirty = true;
  if (mrMode == MR_INFO) { mrInfo(); return; }
  tft.drawRect(MR_MX - 1, MR_MY - 1, MR_W * 2 + 2, MR_H * 2 + 2, C_LINE);
  mrDrawMap();
  mrSide();
  if (mrMode == MR_WALK) mrBottomWalk(); else if (mrMode == MR_SLOPE) mrSlopePanel(); else mrBottomMap();
}
void mrSay(const char* s) { snprintf(mrMsg, sizeof mrMsg, "%s", s); if (scr == SC_MAP && mrMode == MR_MAP) mrBottomMap(); oledDirty = true; }
void mrFailText(int r, int bad, char* m, int n) {   // a route that couldn't be made: why (planner.py's words)
  char a[MR_NAME], b[MR_NAME];
  if (r == -1) snprintf(m, n, L("Sin RAM para la ruta (%u KB libres).", "No RAM for the route (%u KB free)."), (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024));
  else if (r == -2) snprintf(m, n, "%s", L("Ruta demasiado larga para el Psion: usa menos paradas.", "Too long a route for the Psion: use fewer stops."));
  else { mrStopName(bad - 1, a, sizeof a); mrStopName(bad, b, sizeof b);
         snprintf(m, n, L("No hay paso entre %s y %s con %d\xF8: sube la pendiente o elige otro punto.", "No way from %s to %s under %d\xF8: raise the slope or pick another spot."), a, b, S.lim); }
}
void mrFail(int r, int bad) { char m[110]; mrFailText(r, bad, m, sizeof m); PLAY(T_NO); mrSay(m); }
int mrRoute(int from, int* bad) {                // the route from stop `from` on, timed for the serial monitor
  uint32_t t = millis();
  int r = mrSegments(from, bad);
  mrFinish();
  Serial.printf("marte: route %d stops, %u cells, %lu ms, RAM free %u KB, biggest %u KB\n", mrM->r.nStops, mrM->r.len,
                (unsigned long)(millis() - t), (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024));
  return r;
}
void routeSaveLast();
void mrAddStop() {
  MrRoute& R = mrM->r;
  int c = mrCy * MR_W + mrCx, bad = 0;
  if (R.nStops && R.stop[R.nStops - 1] == c) { mrSay(L("Ya es la \xA3ltima parada.", "That's already the last stop.")); return; }
  if (R.nStops >= MR_STOPS) { mrSay(L("M\xA0ximo 10 paradas.", "10 stops at most.")); return; }
  if (!mrOk(c) && mrInZone(c)) { char m[96], w[24]; wxShort(w, sizeof w); snprintf(m, sizeof m, L("Zona cerrada por el clima (%s).", "Closed by the weather (%s)."), w); PLAY(T_NO); mrSay(m); return; }
  if (!mrOk(c)) { char m[64]; snprintf(m, sizeof m, L("Muy empinado aqu\xA1 (%d\xF8, tu l\xA1mite es %d\xF8).", "Too steep here (%d\xF8, your limit is %d\xF8)."), (int)lround(mrSlope(mrCx, mrCy)), S.lim); PLAY(T_NO); mrSay(m); return; }
  R.stop[R.nStops++] = c;
  mrNameStop(R.nStops - 1);
  int r = mrRoute(R.nStops - 1, &bad);
  if (r <= 0 && bad == R.nStops - 1) { R.nStops--; R.len = R.stopAt[R.nStops - 1] + 1; R.ok = 1; mrFinish(); }
  mrDrawMap();
  if (r <= 0) mrFail(r, bad); else { PLAY(T_OK); mrBottomMap(); routeSaveLast(); }
}
void mrDropLast() {
  MrRoute& R = mrM->r;
  if (!R.nStops) return;
  R.nStops--;
  if (R.ok) { R.len = R.nStops ? R.stopAt[R.nStops - 1] + 1 : 0; mrFinish(); }
  else { int bad = 0, r = mrRoute(1, &bad); if (r <= 0) { mrDrawAll(); mrFail(r, bad); return; } }
  routeSaveLast();
  mrDrawAll();
}
int mrReplanAll(int* bad) {                      // the whole route again (a new limit, a loaded mission)
  mrM->r.lim = S.lim;
  return mrRoute(1, bad);
}
void mrSetLim(int lim) {                         // a new slope limit: the cells you can stand on, then the route again
  S.lim = lim; saveSettings();
  mrSetLimit(lim);
  mrMsg[0] = 0;
  int bad = 0, r = 1;
  if (mrM->r.nStops >= 2) r = mrReplanAll(&bad); else { mrM->r.lim = lim; mrFinish(); }
  mrDrawMap();
  if (r <= 0) mrFail(r, bad); else mrBottomMap();
  routeSaveLast();
}
void mrExample() {                               // a local example: Stornoway -> Whaligoe at 20 degrees
  MrRoute& R = mrM->r;
  R.nStops = 2; R.ok = 0;
  R.stop[0] = MR_SITES[MR_EX_A].y * MR_W + MR_SITES[MR_EX_A].x;
  R.stop[1] = MR_SITES[MR_EX_B].y * MR_W + MR_SITES[MR_EX_B].x;
  mrNameStop(0); mrNameStop(1);
  mrCx = MR_SITES[MR_EX_B].x; mrCy = MR_SITES[MR_EX_B].y; mrSiteI = MR_EX_B;
  if (S.lim != 20) { S.lim = 20; saveSettings(); mrSetLimit(20); }
  int bad = 0, r = mrReplanAll(&bad);
  mrDrawMap();
  if (r <= 0) mrFail(r, bad);
  else { snprintf(mrMsg, sizeof mrMsg, "%s", L("Ejemplo local: Stornoway > Whaligoe.", "Local example: Stornoway > Whaligoe.")); mrBottomMap(); }
  mrSide();
  routeSaveLast();
}
void mrWalkStart() {
  mrMode = MR_WALK; mrWalked = 0; mrSeg = 0; mrSegAt = 0; mrDone = false; mrPaused = false; mrCueKey = -1;
  mrLast = millis(); mrWx = -1;
  mrDrawCells(mrCx - 3, mrCy - 3, mrCx + 3, mrCy + 3);
  mrBottomWalk();
  PLAY(T_STOP);
  oledDirty = true;
}
void mrWalkEnd() {
  mrMode = MR_MAP;
  int ox = mrWx, oy = mrWy; mrWx = -1;
  if (ox >= 0) mrDrawCells((ox - MR_MX) / 2 - 3, (oy - MR_MY) / 2 - 3, (ox - MR_MX) / 2 + 3, (oy - MR_MY) / 2 + 3);
  mrDrawCells(mrCx - 3, mrCy - 3, mrCx + 3, mrCy + 3);
  mrBottomMap();
  oledDirty = true;
}
void srvCheck();
void missionSave();
uint8_t mrMenuLvl = 0;                           // (1.3) 0: the map menu, 1: More options...
bool mrOnLastStop() { MrRoute& R = mrM->r; return R.nStops && R.stop[R.nStops - 1] == mrCy * MR_W + mrCx; }
void mrMenu() {
  mrMenuLvl = 0;
  snprintf(pmItem[0], 36, "%s", mrOnLastStop() ? L("Quitar esta parada", "Remove this stop") : L("Parada aqu\xA1", "Stop here"));
  snprintf(pmItem[1], 36, "%s", L("Caminar la ruta", "Walk the route"));
  snprintf(pmItem[2], 36, "%s", L("Deshacer la \xA3ltima parada", "Undo the last stop"));
  snprintf(pmItem[3], 36, "%s", L("Borrar la ruta", "Clear the route"));
  snprintf(pmItem[4], 36, "%s", L("Guardar en Misiones", "Save to Missions"));
  snprintf(pmItem[5], 36, "%s", L("M\xA0s opciones...", "More options..."));
  snprintf(pmItem[6],36,"%s",L("Guia GPS del celular","Phone GPS guide"));
  pmN = 7;
  pmShow(L("MAPA", "MAP"));
}
void mrMenuMore() {
  mrMenuLvl = 1;
  snprintf(pmItem[0], 36, "%s: %d\xF8", L("Pendiente m\xA0xima", "Maximum slope"), S.lim);
  snprintf(pmItem[1], 36, "%s", L("Comprobar con el servidor", "Check with the server"));
  snprintf(pmItem[2], 36, "%s", L("Ruta de ejemplo de la web", "The web's example route"));
  snprintf(pmItem[3], 36, "%s: %s", L("Mapa", "Map"), S.mapGrey ? L("gris", "grey") : L("colores", "colours"));
  snprintf(pmItem[4], 36, "%s: %s", L("Zonas empinadas", "Steep areas"), S.steepHide ? L("ocultas", "hidden") : L("visibles", "shown"));
  snprintf(pmItem[5], 36, "%s: %s", L("En vivo con la web", "Live with the web"), S.sync ? L("s\xA1", "on") : L("no", "off"));
  snprintf(pmItem[6], 36, "%s", L("Ayuda", "Help"));
  pmN = 7;
  pmShow(L("MAS OPCIONES", "MORE OPTIONS"));
}
void mrMenuPick(int ch) {
  MrRoute& R = mrM->r;
  mrMsg[0] = 0;
  if (mrMenuLvl == 0) switch (ch) {
    case 0: if (mrOnLastStop()) mrDropLast(); else { mrDrawAll(); mrAddStop(); } return;
    case 1:
      mrDrawAll();
      if (R.ok && R.nStops >= 2 && R.nSteps >= 2) mrWalkStart(); else { PLAY(T_NO); mrSay(L("Primero arma una ruta: dos paradas o m\xA0s.", "First make a route: two stops or more.")); }
      return;
    case 2: mrDropLast(); return;
    case 3: R.nStops = 0; R.len = 0; R.ok = 0; mrFinish(); routeSaveLast(); break;
    case 4: mrDrawAll(); missionSave(); return;
    case 5: mrDrawAll(); mrMenuMore(); return;
    case 6: go(SC_GUIDE); return;
  } else switch (ch) {
    case 0: mrMode = MR_SLOPE; break;
    case 1: mrDrawAll(); srvCheck(); return;
    case 2: mrDrawAll(); mrExample(); return;
    case 3: S.mapGrey ^= 1; saveSettings(); break;
    case 4: S.steepHide ^= 1; saveSettings(); break;
    case 5:
      S.sync ^= 1; saveSettings(); mrDrawAll();
      if (S.sync && S.conn) { msgBox(L("EN VIVO", "LIVE"), L("En vivo con la web necesita el relay: AJUSTES > Conexi\xA2n: Relay.", "Live with the web needs the relay: SETTINGS > Connection: Relay.")); return; }
      if (S.sync) syncStart();
      return;
    case 6: mrMode = MR_INFO; break;
  }
  mrDrawAll();
}
void mrWalkTick(uint32_t now) {
  MrRoute& R = mrM->r;
  int c = takeClicks(), pd = padTake();
  if (pd == PD_LEFT || pd == PD_DOWN) c--;
  if (pd == PD_RIGHT || pd == PD_UP) c++;
  if (c) { S.speedI = constrain(S.speedI + (c > 0 ? 1 : -1), 0, 4); click(); mrPanelAt = 0; }
  if (takeShort()) {
    if (mrDone) { mrWalkEnd(); return; }
    mrPaused = !mrPaused; mrPanelAt = 0; click();
  }
  float dt = min(0.1f, (now - mrLast) / 1000.f);
  mrLast = now;
  double v = mrEnv.speed * 1000.0 / 3600.0 * MR_SPEEDS[S.speedI];
  if (!mrPaused && !mrDone) {
    mrWalked += dt * v;
    if (mrWalked >= R.dist3) { mrWalked = R.dist3; mrDone = true; mrPanelAt = 0; PLAY(T_END); }
    while (mrSeg < R.len - 1 && mrSegAt + mrSeg3(mrSeg) <= mrWalked) { mrSegAt += mrSeg3(mrSeg); mrSeg++; }
  }
  int a = R.path[mrSeg], b = R.path[min(mrSeg + 1, R.len - 1)];
  double f = mrSeg < R.len - 1 ? constrain((mrWalked - mrSegAt) / mrSeg3(mrSeg), 0.0, 1.0) : 0.0;
  int px = MR_MX + 1 + (int)lround(2 * (a % MR_W + (b % MR_W - a % MR_W) * f));
  int py = MR_MY + 1 + (int)lround(2 * (a / MR_W + (b / MR_W - a / MR_W) * f));
  if (px != mrWx || py != mrWy) {
    int ox = mrWx, oy = mrWy;
    mrWx = px; mrWy = py;
    if (ox >= 0) mrDrawCells((ox - MR_MX) / 2 - 2, (oy - MR_MY) / 2 - 2, (ox - MR_MX) / 2 + 2, (oy - MR_MY) / 2 + 2);
    mrMarks((px - MR_MX) / 2, (py - MR_MY) / 2, (px - MR_MX) / 2, (py - MR_MY) / 2);
  }
  int idx; double rem; mrNav(&idx, &rem);        // sounds, like the web's voice: near a direction (50 m+) and right at it
  if (!mrPaused && !mrDone) {
    bool nowPh = rem <= 6;
    int key = idx * 2 + nowPh;
    if (key != mrCueKey && rem <= max(50.0, v * 6) && now - mrCueAt > 1200) {
      mrCueKey = key; mrCueAt = now;
      int t = R.step[idx].kind & 15;
      if (!nowPh) PLAY(T_AHEAD);
      else if (R.step[idx].kind & 0x60) PLAY(T_STOP);
      else if (t == 7) PLAY(T_BACK);
      else if (t >= 4) PLAYP(T_RIGHT, 2);        // (stereo: on the right speaker)
      else if (t) PLAYP(T_LEFT, 1);              // (on the left one)
      mrPanelAt = 0;
    }
  }
  if (now - mrPanelAt >= 250) { mrPanelAt = now; mrBottomWalk(); oledDirty = true; }
}
void mapEnter() { if (mrMode == MR_WALK && !mrM->r.ok) mrMode = MR_MAP; mrDrawAll(); }
bool mapBack() {                                 // hold: true = it went back inside the map
  if (mrMode == MR_WALK) { mrWalkEnd(); return true; }
  if (mrMode == MR_SLOPE) { mrMode = MR_MAP; mrDrawAll(); mrSetLim(S.lim); return true; }
  if (mrMode == MR_INFO) { mrMode = MR_MAP; mrDrawAll(); return true; }
  return false;
}
void mapTick(uint32_t now) {
  if (pmOpen) { int ch = pmTick(); if (ch >= 0) mrMenuPick(ch); return; }
  if (mrMode == MR_WALK) { mrWalkTick(now); return; }
  if (mrMode == MR_INFO) { takeClicks(); padTake(); if (takeShort()) { mrMode = MR_MAP; mrDrawAll(); } return; }
  if (mrMode == MR_SLOPE) {
    int c = takeClicks(), pd = padTake(), v = S.lim;
    c += pd == PD_RIGHT ? 1 : pd == PD_LEFT ? -1 : pd == PD_UP ? 5 : pd == PD_DOWN ? -5 : 0;
    if (c) { v = constrain(v + c, 5, 40); if (v != S.lim) { S.lim = v; click(); mrSlopePanel(); oledDirty = true; } }
    if (takeShort()) { mrMode = MR_MAP; PLAY(T_OK); mrDrawAll(); mrSetLim(S.lim); }
    return;
  }
  int ox = mrCx, oy = mrCy;
  if (int pd = padTake()) {
    int st = padRep > 8 ? 3 : 1;
    if (pd == PD_LEFT) mrCx -= st; else if (pd == PD_RIGHT) mrCx += st; else if (pd == PD_UP) mrCy -= st; else mrCy += st;
    mrCx = constrain(mrCx, 0, MR_W - 1); mrCy = constrain(mrCy, 0, MR_H - 1);
  }
  if (int c = takeClicks()) {
    int s = mrSiteAt(mrCx, mrCy);
    mrSiteI = ((s >= 0 ? s : mrSiteI) + (c > 0 ? 1 : -1) + MR_NSITES) % MR_NSITES;
    mrCx = MR_SITES[mrSiteI].x; mrCy = MR_SITES[mrSiteI].y;
    click();
  }
  if (mrCx != ox || mrCy != oy) {
    int s = mrSiteAt(mrCx, mrCy);
    if (s >= 0) mrSiteI = s;
    mrDrawCells(ox - 3, oy - 3, ox + 3, oy + 3);
    mrMarks(mrCx, mrCy, mrCx, mrCy);
    mrSide();
    if (mrMsg[0]) { mrMsg[0] = 0; mrBottomMap(); }
    oledDirty = true;
  }
  if (takeShort()) mrMenu();
}
void oledMap() {
  char b[28], n[MR_NAME];
  MrRoute& R = mrM->r;
  oled.clearDisplay(); oled.setTextColor(SSD1306_WHITE);
  if (mrMode == MR_WALK) {                       // turn by turn: how far (big), what to do, how far to go
    int idx; double rem; mrNav(&idx, &rem);
    oled.setTextSize(2); oled.setCursor(0, 0);
    if (mrDone) oled.print(L("Llegaste", "Arrived"));
    else if (rem <= 6) oled.print(L("Ahora", "Now"));
    else { mrDist(rem, n, sizeof n); snprintf(b, sizeof b, "%s %s", L("En", "In"), n); oled.print(b); }
    oled.setTextSize(1);
    char t[120];
    mrStepText(idx, !mrDone, t, sizeof t);
    int at = 0;
    for (int k = 0; k < 3 && t[at]; k++) { at = mrWrap(t, at, 21, b); oled.setCursor(0, 22 + k * 10); oled.print(b); }
    oled.setCursor(0, 56);
    if (mrDone) oled.print(L("OK: volver al mapa", "OK: back to the map"));
    else { mrDist(R.dist3 - mrWalked, n, sizeof n); snprintf(b, sizeof b, "%s %s%s", n, L("al destino", "to go"), mrPaused ? L(" (pausa)", " (paused)") : ""); oled.print(b); }
    return;
  }
  char l[3][24];
  int s = mrSiteAt(mrCx, mrCy);
  if (s >= 0) { mrName(s, n, sizeof n); snprintf(l[0], 24, "%.21s", n); } else snprintf(l[0], 24, "%s", L("un punto sin sitio", "a spot, no site"));
  if (R.ok && R.nStops >= 2) snprintf(l[1], 24, "%.2f km  %d min", R.dist / 1000, R.minutes);
  else snprintf(l[1], 24, L("%d paradas", "%d stops"), R.nStops);
  snprintf(l[2], 24, "%s %d\xF8", L("pendiente m\xA0x.", "max slope"), S.lim);
  oledCard(L("MAPA", "MAP"), l[0], l[1], l[2], "");
}

// ---------- missions on the Psion (/marte/mNN.txt) + the last route (/marte/last.txt) ----------
const int MS_MAX = 30;
bool fsOk = false;
uint8_t msHave[MS_MAX + 1];                      // 1 = /marte/mNN.txt exists
int msCount = 0;
bool syncApplying = false, syncDirty = false;     // (a route that came from the web isn't sent back to it)
uint32_t syncDirtyAt = 0;
void routeSaveLast() {                           // after every change to the route: saved, and (live) sent to the web
  if (!syncApplying) { syncDirty = true; syncDirtyAt = millis(); }
  if (!fsOk) return;
  char t[700];
  int n = mrMissionText(t, sizeof t);
  File f = LittleFS.open("/marte/last.txt", "w");
  if (f) { if (n) f.write((const uint8_t*)t, n); f.close(); }
}
bool missionLoadText(const char* path, MrReply* r) {
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  char t[700];
  int n = f.read((uint8_t*)t, sizeof t - 1);
  f.close();
  t[n > 0 ? n : 0] = 0;
  return mrMissionRead(t, r);
}
void missionScan() {
  msCount = 0;
  for (int i = 1; i <= MS_MAX; i++) {
    char p[24]; snprintf(p, sizeof p, "/marte/m%02d.txt", i);
    msHave[i] = fsOk && LittleFS.exists(p);
    msCount += msHave[i];
  }
}
// Loads stops + limit into the route and works it out here. -> the route's result (1 = whole)
int routeTake(const MrReply& r, int* bad) {
  mrTakeStops(r);
  if (S.lim != mrM->r.lim) { S.lim = mrM->r.lim; saveSettings(); mrSetLimit(S.lim); }
  mrMode = MR_MAP;
  mrCx = r.stop[r.nStops - 1] % MR_W; mrCy = r.stop[r.nStops - 1] / MR_W;
  int res = mrRoute(1, bad);
  routeSaveLast();
  return res;
}

// ---------- the Ruta Marciana server (through the relay, or straight) ----------
// POST /api/psion/missions {stops, maxSlopeDegrees, weather} -> the server works the route out again (planner.py) and
// answers its numbers + a CRC of its cells + a 4-letter code for the web; GET /api/psion/missions/<code> -> a mission
// from the web. With the relay these go to the relay, which passes them to the web it has open.
void serverName(char* b, int n) {
  if (!S.conn) snprintf(b, n, "Relay (%s)", S.relay);
  else if (S.web[0]) { const char* h = strstr(S.web, "://"); snprintf(b, n, "Web (%.40s)", h ? h + 3 : S.web); }
  else snprintf(b, n, "%s", L("Web: sin direcci\xA2n (AJUSTES)", "Web: no address (SETTINGS)"));
}
bool baseUrl(char* b, int n) {                   // where requests go; false = no address yet
  if (!S.conn) { snprintf(b, n, "http://%s", S.relay); return S.relay[0] != 0; }
  if (!S.web[0]) return false;
  bool lan = isdigit((unsigned char)S.web[0]) || !strncmp(S.web, "localhost", 9);
  snprintf(b, n, "%s%s", strstr(S.web, "://") ? "" : lan ? "http://" : "https://", S.web);
  int l = strlen(b);
  if (l && b[l - 1] == '/') b[l - 1] = 0;
  return true;
}
void busyBox(const char* title, const char* line) {   // while the Psion waits for the server
  tft.fillRect(22, 80, W - 44, 64, C_PANEL); tft.drawRect(22, 80, W - 44, 64, C_CYAN);
  putText(32, 90, 1, C_CYAN, C_PANEL, title);
  putWrap(32, 106, 42, 3, 11, C_TEXT, C_PANEL, line);
  oledCard(L("SERVIDOR", "SERVER"), title, L("espera...", "wait..."), "", "");
  oledShow();
}
// -> the HTTP status (or < 0: no connection) and the answer in out
bool srvQuiet = false;                           // (live sync asks every 2 s: not on the serial monitor)
int srvCall(const char* path, const char* body, String& out) {
  if (!online()) { out = L("{\"error\":\"Sin Wi-Fi: Inicio > WI-FI.\"}", "{\"error\":\"No Wi-Fi: Home > WI-FI.\"}"); return -1; }
  char url[200];
  if (!baseUrl(url, 120)) { out = L("{\"error\":\"Falta la direcci\\u00f3n: AJUSTES > Conexi\\u00f3n.\"}", "{\"error\":\"No address yet: SETTINGS > Connection.\"}"); return -2; }
  strncat(url, path, sizeof url - strlen(url) - 1);
  bool https = !strncmp(url, "https://", 8);
  WiFiClient plain;
  WiFiClientSecure sec;
  HTTPClient http;
  http.useHTTP10(true);
  http.setTimeout(65000);                        // (a free web server can take up to a minute to wake up; 65 s = the most)
  http.setConnectTimeout(https ? 20000 : 4000);
  http.setUserAgent("RutaMarciana-MarsOS/1.3");
  bool ok;
  if (https) { sec.setCACert(RUTA_ROOT_CA); ok = http.begin(sec, url); }   // Verify the shared CA roots; never disable TLS checks.
  else ok = http.begin(plain, url);
  if (!ok) { out = L("{\"error\":\"Direcci\\u00f3n del servidor no v\\u00e1lida.\"}", "{\"error\":\"Not a valid server address.\"}"); return -2; }
  int code;
  if (body) { http.addHeader("Content-Type", "application/json"); code = http.POST((uint8_t*)body, strlen(body)); }
  else code = http.GET();
  if (code > 0) out = http.getString();
  else out = String(L("{\"error\":\"Sin conexi\\u00f3n con el servidor (", "{\"error\":\"Can't reach the server (")) + http.errorToString(code) + ").\"}";
  http.end();
  if (!srvQuiet) Serial.printf("marte: %s %s -> %d (%u bytes)\n", body ? "POST" : "GET", url, code, (unsigned)out.length());
  return code;
}
void srvCheck() {                                // the route here vs the server's (and a code for the web)
  MrRoute& R = mrM->r;
  if (!R.ok || R.nStops < 2) { PLAY(T_NO); msgBox(L("COMPROBAR", "CHECK"), L("Primero arma una ruta en el mapa: dos paradas o m\xA0s.", "First make a route on the map: two stops or more.")); return; }
  char body[3200];
  if (!mrRequestJson(body, sizeof body)) { msgBox(L("COMPROBAR", "CHECK"), L("La misi\xA2n no cabe en el mensaje.", "The mission doesn't fit in the message.")); return; }
  char sv[64]; serverName(sv, sizeof sv);
  busyBox(L("ENVIANDO LA MISION", "SENDING THE MISSION"), sv);
  String ans;
  int code = srvCall("/api/psion/missions", body, ans);
  MrReply rp;
  bool parsed = mrParseReply(ans.c_str(), &rp);
  char m[300];
  if (code == 404) { PLAY(T_NO); msgBox(L("SERVIDOR", "SERVER"), L("Este servidor a\xA3n no tiene el buz\xA2n del Psion (/api/psion/missions): actual\xA1zalo con la versi\xA2n de Ruta Marciana que lo trae.", "This server doesn't have the Psion's mailbox yet (/api/psion/missions): update it to the Ruta Marciana version that has it.")); return; }
  if (code != 200 || !parsed) { PLAY(T_NO); snprintf(m, sizeof m, "%s", rp.error[0] ? rp.error : L("El servidor no contest\xA2 bien.", "The server didn't answer properly.")); msgBox(L("SERVIDOR", "SERVER"), m, C_RED); return; }
  char crc[12]; snprintf(crc, sizeof crc, "%08lx", (unsigned long)mrPathCrc());
  bool same = !strcmp(crc, rp.crc) && rp.cells == R.len && rp.minutes == R.minutes && fabs(rp.distance - R.dist) < 0.01 && fabs(rp.ascent - R.climb) < 0.01;
  if (same) snprintf(m, sizeof m, L("La misma ruta que el servidor: %.2f km, +%.1f m, %d min.  C\xA2" "digo para la web: %s  (en la web: Abrir del Psion). Vale 24 h.",
                                    "Same cells and metrics as server: %.2f km, +%.1f m, %d min.  Code for the web: %s  (on the web: Open from the Psion). Valid 24 h."),
                     R.dist / 1000, R.climb, R.minutes, rp.code);
  else snprintf(m, sizeof m, L("DIFERENTE. Psion: %.2f km, %d min. Servidor: %.2f km, %d min. C\xA2" "digo: %s", "DIFFERENT. Psion: %.2f km, %d min. Server: %.2f km, %d min. Code: %s"),
                R.dist / 1000, R.minutes, rp.distance / 1000, rp.minutes, rp.code);
  if (same) PLAY(T_GOOD); else PLAY(T_NO);
  msgBox(same ? L("COMPROBADA", "CHECKED") : L("NO COINCIDE", "NOT THE SAME"), m, same ? C_GREEN : C_RED);
}
void envApply(const MrEnv& e, const char* js);
void srvFetch(const char* code) {                // a mission from the web: its stops, then the route worked out here
  char path[40]; snprintf(path, sizeof path, "/api/psion/missions/%s", code);
  char sv[64]; serverName(sv, sizeof sv);
  busyBox(L("RECIBIENDO", "RECEIVING"), sv);
  String ans;
  int st = srvCall(path, nullptr, ans);
  MrReply rp;
  bool parsed = mrParseReply(ans.c_str(), &rp);
  char m[300];
  if (st != 200 || !parsed || rp.nStops < 2) {
    PLAY(T_NO);
    snprintf(m, sizeof m, "%s", rp.error[0] ? rp.error : L("No lleg\xA2 una misi\xA2n v\xA0lida.", "No valid mission came."));
    msgBox(L("RECIBIR", "RECEIVE"), m, C_RED);
    return;
  }
  MrEnv fromWeb;
  if(!mrParseEnv(ans.c_str(), &fromWeb, nullptr, 0)){msgBox("MISION","Entorno no valido / Invalid environment");return;}
  envApply(fromWeb, ans.c_str());
  int bad = 0, r = routeTake(rp, &bad);
  MrRoute& R = mrM->r;
  char crc[12]; snprintf(crc, sizeof crc, "%08lx", (unsigned long)mrPathCrc());
  bool same = r > 0 && !strcmp(crc, rp.crc) && rp.cells == R.len && rp.minutes == R.minutes;
  if (r <= 0) { char f[110]; mrFailText(r, bad, f, sizeof f); snprintf(mrMsg, sizeof mrMsg, L("Misi\xA2n %s: %s", "Mission %s: %s"), rp.code, f); }
  else snprintf(mrMsg, sizeof mrMsg, L("Misi\xA2n %s de la web: %s (%.2f km, %d min).", "Mission %s from the web: %s (%.2f km, %d min)."), rp.code,
                same ? L("la misma ruta que el servidor", "the same cells and metrics as the server") : L("\xA1ojo! otra ruta que el servidor", "careful: not the server's route"),
                R.dist / 1000, R.minutes);
  if (same) PLAY(T_GOOD); else PLAY(T_NO);
  go(SC_MAP);
}

// ---------- the Mars relay: the web's weather, live plan sync, the 3D view ----------
// (extras/mars_relay/mars_relay.py on your PC keeps the Ojo de Marte web open in Edge; it answers small JSON + pictures)
void envSave(const char* js) {                   // Save only environment, not the full shared mission.
  if (!fsOk) return;
  static MrEnv e;if(!mrParseEnv(js,&e,nullptr,0))return;
  static char packed[2048];int n=snprintf(packed,sizeof packed,"{\"weather\":\"%s\",\"label\":\"Escenario educativo\",\"sourceType\":\"synthetic\",\"speedKmH\":%.6f,\"restrictions\":[",e.key,e.speed);
  for(int k=0;k<e.nZones&&n<(int)sizeof packed;k++)n+=snprintf(packed+n,sizeof packed-n,"%s{\"lat\":%.12f,\"lon\":%.12f,\"radiusMeters\":%.3f}",k?",":"",e.zone[k].lat,e.zone[k].lon,e.zone[k].r);
  if(n<(int)sizeof packed)n+=snprintf(packed+n,sizeof packed-n,"]}");
  if(n>=(int)sizeof packed)return;
  File f = LittleFS.open("/marte/env.json", "w");
  if (f) { f.write((const uint8_t*)packed,n); f.close(); }
}
void envLoad() {
  mrEnvClear();
  if (!fsOk || !LittleFS.exists("/marte/env.json")) return;
  File f = LittleFS.open("/marte/env.json", "r");
  if (!f) return;
  static char t[2048];
  int n = f.read((uint8_t*)t, sizeof t - 1);
  f.close();
  t[n > 0 ? n : 0] = 0;
  MrEnv e;
  if (mrParseEnv(t, &e, nullptr, 0)) mrEnv = e;
}
void envApply(const MrEnv& e, const char* js) {  // a new weather: other cells closed, another speed -> the route again
  bool same = !strcmp(e.key, mrEnv.key) && e.nZones == mrEnv.nZones && e.speed == mrEnv.speed;
  for (int k = 0; same && k < e.nZones; k++) same = e.zone[k].lat == mrEnv.zone[k].lat && e.zone[k].lon == mrEnv.zone[k].lon && e.zone[k].r == mrEnv.zone[k].r;
  mrEnv = e;
  if (js) envSave(js);
  if (same) return;
  mrSetLimit(S.lim);
  int bad = 0, r = 1;
  if (mrM->r.nStops >= 2) r = mrReplanAll(&bad); else mrFinish();
  if (r <= 0) { char m[110]; mrFailText(r, bad, m, sizeof m); snprintf(mrMsg, sizeof mrMsg, "%s", m); }
  routeSaveLast();
}
bool relayOnly(char* err, int n) {               // the relay's extras need the relay
  if (S.conn) { snprintf(err, n, "%s", L("Esto llega por el relay: AJUSTES > Conexi\xA2n: Relay.", "This comes through the relay: SETTINGS > Connection: Relay.")); return false; }
  if (!online()) { snprintf(err, n, "%s", L("Sin Wi-Fi: Inicio > WI-FI.", "No Wi-Fi: Home > WI-FI.")); return false; }
  return true;
}
bool weatherCall(const char* key, char* err, int n) {   // key = null: GET the web's weather; else POST: the web switches
  if (!relayOnly(err, n)) return false;
  char path[48];
  snprintf(path, sizeof path, key ? "/mr/weather?key=%s" : "/mr/weather", key ? key : "");
  String ans;
  int st = srvCall(path, key ? "" : nullptr, ans);
  MrEnv e;
  if (!mrParseEnv(ans.c_str(), &e, err, n) || st != 200) { if (!err[0]) snprintf(err, n, L("El relay no contest\xA2 (%d).", "The relay didn't answer (%d)."), st); return false; }
  envApply(e, ans.c_str());
  return true;
}
// ---- SYNC: the route on the Psion and the plan on the web are copied both ways (only with the relay) ----
int syncVer = -1;                                // the web plan's number we have (-1: none yet -> the start rule)
uint32_t syncAt = 0;
int syncPull() {                                 // -> 1 = the route here changed, 0 = nothing new, -1 = no answer
  char path[40];
  snprintf(path, sizeof path, "/mr/plan?since=%d", syncVer);
  String ans;
  srvQuiet = true; int st = srvCall(path, nullptr, ans); srvQuiet = false;
  MrReply rp;
  bool ok = st == 200 && mrParseReply(ans.c_str(), &rp);
  if (syncFail != !ok) { syncFail = !ok; if (scr == SC_MAP && mrMode == MR_MAP && !pmOpen && !msgOpen && !kbOpen) mrBottomMap(); }
  if (!ok || rp.version == syncVer) return ok ? 0 : -1;
  MrRoute& R = mrM->r;
  bool first = syncVer < 0;
  if (first && rp.nStops == 0 && R.nStops) { syncVer=rp.version; syncDirty = true; syncDirtyAt = 0; return 0; }   // start: an empty web gets the Psion's route
  syncApplying = true;
  int changed = 0;
  if (rp.weather[0]) { char e[100]; if (!weatherCall(nullptr,e,sizeof e)) { syncApplying=false;return -1; } changed=1; }
  syncVer=rp.version;
  bool same = rp.nStops == R.nStops && (rp.nStops < 2 || rp.lim == R.lim);
  for (int k = 0; same && k < rp.nStops; k++) same = rp.stop[k] == R.stop[k];
  if (!same) {
    int bad = 0, r = 1;
    if (rp.nStops) r = routeTake(rp, &bad);
    else { R.nStops = 0; R.len = 0; R.ok = 0; mrFinish(); routeSaveLast(); }
    if (r <= 0) { char f[110]; mrFailText(r, bad, f, sizeof f); snprintf(mrMsg, sizeof mrMsg, L("De la web: %s", "From the web: %s"), f); }
    else if (R.nStops >= 2) snprintf(mrMsg, sizeof mrMsg, L("La web cambi\xA2 la ruta: %d paradas, %.2f km, %d min.", "The web changed the route: %d stops, %.2f km, %d min."), R.nStops, R.dist / 1000, R.minutes);
    else snprintf(mrMsg, sizeof mrMsg, L("La web cambi\xA2 la ruta: %d parada(s).", "The web changed the route: %d stop(s)."), R.nStops);
    changed = 1;
  }
  syncApplying = false;
  return changed;
}
void syncPush() {                                // the route here -> the web (it plans it too)
  char body[3200];
  if (!mrRequestJson(body, sizeof body)) return;
  String ans;
  srvQuiet = true; int st = srvCall("/mr/plan", body, ans); srvQuiet = false;
  MrReply rp;
  bool ok = st == 200 && mrParseReply(ans.c_str(), &rp);
  if (ok) syncVer = rp.version;
  if (syncFail != !ok) { syncFail = !ok; if (scr == SC_MAP && mrMode == MR_MAP && !pmOpen && !msgOpen && !kbOpen) mrBottomMap(); }
}
void syncStart() {                               // "En vivo" switched on: the web's plan now (or the Psion's, if the web has none)
  if (!online()) { msgBox(L("EN VIVO", "LIVE"), L("Sin Wi-Fi: Inicio > WI-FI.", "No Wi-Fi: Home > WI-FI.")); return; }
  busyBox(L("EN VIVO CON LA WEB", "LIVE WITH THE WEB"), L("Leyendo la ruta de la web...", "Reading the web's route..."));
  syncVer = -1; syncDirty = false;
  int r = syncPull();
  if (syncDirty) { syncDirty = false; syncPush(); }
  redraw();
  if (r < 0) { PLAY(T_NO); msgBox(L("EN VIVO", "LIVE"), L("El relay no contest\xA2: revisa AJUSTES > Direcci\xA2n del relay y que la ventana del Mars relay est\x82 abierta.", "The relay didn't answer: check SETTINGS > Relay address, and that the Mars relay window is open on your PC."), C_RED); }
  else PLAY(T_GOOD);
}
// ---- the 3D view: the relay draws the web's 3D view in Edge and sends the pixels (RGB565), one row at a time ----
bool relayImage(const char* path, const char* body, int y0, int h, char* err, int en) {
  char url[220];
  snprintf(url, sizeof url, "http://%s%s", S.relay, path);
  WiFiClient cl;
  HTTPClient http;
  http.useHTTP10(true);
  http.setTimeout(30000);
  http.setConnectTimeout(4000);
  http.setUserAgent("RutaMarciana-MarsOS/1.3");
  if (!http.begin(cl, url)) { snprintf(err, en, "%s", L("Direcci\xA2n del relay no v\xA0lida.", "Not a valid relay address.")); return false; }
  http.addHeader("Content-Type", "application/json");
  int code = http.POST((uint8_t*)body, strlen(body));
  if (code != 200) {
    MrReply r;
    if (code > 0) { String s = http.getString(); mrParseReply(s.c_str(), &r); snprintf(err, en, "%s", r.error[0] ? r.error : L("El relay no pudo.", "The relay couldn't.")); }
    else snprintf(err, en, L("El relay no contest\xA2 (%s).", "The relay didn't answer (%s)."), http.errorToString(code).c_str());
    http.end();
    return false;
  }
  WiFiClient* st = http.getStreamPtr();
  st->setTimeout(5000);
  static uint16_t row[320];
  bool ok = true;
  for (int y = 0; y < h && ok; y++) {
    ok = st->readBytes((uint8_t*)row, sizeof row) == sizeof row;
    if (ok) { tft.startWrite(); tft.setAddrWindow(0, y0 + y, 320, 1); tft.writePixels(row, 320); tft.endWrite(); }
  }
  http.end();
  if (!ok) snprintf(err, en, "%s", L("La imagen lleg\xA2 incompleta.", "The picture came incomplete."));
  return ok;
}

// ---------- VISTA 3D: the web's 3D view of your route (Edge on your PC draws it; the Psion shows it) ----------
const char* v3Name(int i) {
  static const char* const N[2][5] = {{"vista general", "sobre la ruta", "sigue al astronauta", "a ras de suelo", "globo de Marte"},
                                      {"overview", "over the route", "follow the astronaut", "ground level", "Mars globe"}};
  return N[mrLang][i];
}
const char* const V3_MODE[5] = {"terrain&focus=center", "terrain&focus=route", "follow", "surface", "globe"};
const int V3_Y = 22, V3_H = 190;
int v3Mode = 1;
float v3Yaw = 0, v3Pitch = 0.9f, v3Zoom = 1.0f, v3P = 0;   // (zoom 1 = the whole route: the web fits it)
bool v3Want = false, v3Shown = false;
uint32_t v3At = 0;
char v3Err[110] = "";
bool v3Follow() { return v3Mode == 2 || v3Mode == 3; }
void v3Status(const char* busy) {                // (1.3) the view's name (and how far along), then the hint line
  char b[60];
  tft.fillRect(0, V3_Y + V3_H, W, FOOT - V3_Y - V3_H, C_BAR);
  if (busy) snprintf(b, sizeof b, "%s  -  %s", v3Name(v3Mode), busy);
  else if (v3Follow()) snprintf(b, sizeof b, "%s  -  %d%%", v3Name(v3Mode), (int)lround(v3P * 100));
  else snprintf(b, sizeof b, "%s", v3Name(v3Mode));
  putText(8, V3_Y + V3_H + 4, 1, busy ? C_YELLOW : C_CYAN, C_BAR, b);
  drawHints(v3Follow() ? L("perilla: avanzar   OK: men\xA3   mantener: inicio", "knob: move along   OK: menu   hold: home")
                       : L("flechas: girar   perilla: zoom   OK: men\xA3", "arrows: turn   knob: zoom   OK: menu"));
}
void v3Message(const char* m) {                  // no picture: why
  tft.fillRect(0, V3_Y, W, V3_H, C_BG);
  drawIcon(ICONS[7], W / 2 - 24, V3_Y + 30, 3, C_LINE);
  putWrap(20, V3_Y + 96, 46, 6, 11, C_TEXT, C_BG, m);
  v3Shown = false;
}
void v3Draw() {
  tft.fillScreen(C_BG); drawTitle(L("Vista 3D", "3D view"));
  if (!v3Shown) v3Message(v3Err[0] ? v3Err : L("La vista 3D de la web, dibujada por el relay en tu PC.", "The web's 3D view, drawn by the relay on your PC."));
  v3Status(nullptr);
  oledDirty = true;
}
void v3Fetch() {
  v3Want = false;
  char err[110] = "";
  if (!relayOnly(err, sizeof err)) { snprintf(v3Err, sizeof v3Err, "%s", err); v3Message(err); v3Status(nullptr); return; }
  v3Status(L("cargando...", "loading..."));
  char path[180], body[3200];
  snprintf(path, sizeof path, "/mr/view3d?w=320&h=%d&mode=%s&yaw=%.2f&pitch=%.2f&zoom=%.2f&p=%.3f", V3_H, V3_MODE[v3Mode], v3Yaw, v3Pitch, v3Zoom, v3P);
  if (!mrRequestJson(body, sizeof body)) snprintf(body, sizeof body, "{}");   // (your route: the relay shows this one)
  uint32_t t = millis();
  bool ok = relayImage(path, body, V3_Y, V3_H, err, sizeof err);
  Serial.printf("marte: 3D view %s in %lu ms\n", ok ? "ok" : err, (unsigned long)(millis() - t));
  if (ok) { v3Shown = true; v3Err[0] = 0; }
  else { snprintf(v3Err, sizeof v3Err, "%s", err); PLAY(T_NO); v3Message(err); }
  v3Status(nullptr);
  oledDirty = true;
}
void v3Enter() { v3Shown = false; v3Err[0] = 0; v3Draw(); v3Fetch(); }
void v3Tick(uint32_t now) {
  if (pmOpen) {
    int ch = pmTick();
    if (ch < 0) return;
    if (ch < 5) { v3Mode = ch; if (ch == 0) { v3Yaw = 0; v3Pitch = 1.3f; v3Zoom = 1.0f; } else if (ch == 1) { v3Pitch = 0.9f; v3Zoom = 1.0f; } else if (ch < 4 && v3P < 0.05f) v3P = 0.05f; }
    else if (ch == 6) { v3Yaw = 0; v3Pitch = 0.9f; v3Zoom = 1.0f; v3P = 0; v3Mode = 1; }
    v3Shown = false; v3Draw(); v3Fetch();
    return;
  }
  int c = takeClicks(), pd = padTake();
  if (pd || c) {
    if (v3Follow()) v3P = constrain(v3P + 0.04f * (c + (pd == PD_RIGHT || pd == PD_UP) - (pd == PD_LEFT || pd == PD_DOWN)), 0.f, 1.f);
    else {
      if (pd == PD_LEFT) v3Yaw -= 0.35f; else if (pd == PD_RIGHT) v3Yaw += 0.35f;
      else if (pd == PD_UP) v3Pitch = min(1.56f, v3Pitch + 0.15f); else if (pd == PD_DOWN) v3Pitch = max(0.1f, v3Pitch - 0.15f);
      if (c) v3Zoom = constrain(v3Zoom * powf(1.2f, (float)c), 0.4f, 6.f);
    }
    click(); v3Want = true; v3At = now; v3Status(L("suelta para ver", "let go to see")); oledDirty = true;
  }
  if (takeShort()) {
    for (int i = 0; i < 5; i++) snprintf(pmItem[i], 36, "%s%s", i == v3Mode ? "> " : "  ", v3Name(i));
    snprintf(pmItem[5], 36, "%s", L("Actualizar la imagen", "Refresh the picture")); snprintf(pmItem[6], 36, "%s", L("Empezar de nuevo", "Start over"));
    pmN = 7; pmShow(L("VISTA 3D", "3D VIEW"));
    return;
  }
  if (v3Want && now - v3At > 450) v3Fetch();     // (a picture once you stop pressing: each takes ~1 s)
}
void oledView() {
  char a[24], b[24];
  snprintf(a, sizeof a, "%.21s", v3Name(v3Mode));
  if (v3Follow()) snprintf(b, sizeof b, L("%d%% del camino", "%d%% of the way"), (int)lround(v3P * 100)); else snprintf(b, sizeof b, "zoom %.1fx", v3Zoom);
  oledCard(L("VISTA 3D", "3D VIEW"), a, b, v3Shown ? "" : L("sin imagen", "no picture"), "");
}

// ---------- CLIMA: the weather the web has on (today made-up training scenarios; later NASA data) ----------
int wxSel = 0;
int wxRows() { return mrEnv.nScen + 1; }        // the web's scenarios + "update"
const int WX_Y = 122, WX_ROW = 20;
void wxDrawRow(int i) {
  int y = WX_Y + i * WX_ROW;
  bool sel = i == wxSel, cur = i < mrEnv.nScen && !strcmp(mrEnv.scenKey[i], mrEnv.key);
  uint16_t bg = sel ? C_ORANGE : C_PANEL;
  tft.fillRect(8, y, W - 16, WX_ROW - 2, bg);
  char b[56], lb[48];
  if (i < mrEnv.nScen) { wxLabel(mrEnv.scenKey[i], mrEnv.scenLabel[i], lb, sizeof lb); snprintf(b, sizeof b, "%s%.44s", cur ? "\xFB " : "  ", lb); }
  else snprintf(b, sizeof b, "  %s", L("Actualizar desde la web", "Update from the web"));
  putText(14, y + 5, 1, sel ? C_ONOR : cur ? C_GREEN : C_TEXT, bg, b);
}
void wxDraw() {                                  // (1.3) one card: the weather, its speed, closed zones; the choices under it
  tft.fillScreen(C_BG); drawTitle(L("Clima", "Weather"));
  tft.fillRect(8, 30, W - 16, 82, C_PANEL);
  bool clear = !strcmp(mrEnv.key, "clear");
  uint16_t acc = clear ? C_CYAN : mrEnv.nZones ? C_RED : C_YELLOW;
  drawIcon(ICONS[8], 22, 47, 3, acc);
  char b[64], w[24];
  wxShort(w, sizeof w);
  putText(84, 42, 2, acc, C_PANEL, w);
  if (mrEnv.nZones) snprintf(b, sizeof b, L("%.1f km/h   %d zona(s) cerrada(s)", "%.1f km/h   %d closed zone(s)"), mrEnv.speed, mrEnv.nZones);
  else snprintf(b, sizeof b, L("%.1f km/h   nada cerrado", "%.1f km/h   nothing closed"), mrEnv.speed);
  putText(84, 66, 1, C_TEXT, C_PANEL, b);
  if (!strcmp(mrEnv.source, "synthetic")) putText(84, 80, 1, C_MUTED, C_PANEL, L("simulado, para entrenar", "simulated, for training"));
  wxSel = min(wxSel, wxRows() - 1);
  for (int i = 0; i < wxRows() && i < 4; i++) wxDrawRow(i);
  drawHints(L("OK: usar en la web   mantener: inicio", "OK: use it on the web   hold: home"));
  oledDirty = true;
}
void wxEnter() {
  char err[100] = "";
  if (!S.conn && online()) { busyBox(L("CLIMA", "WEATHER"), L("Leyendo el clima de la web...", "Reading the web's weather...")); weatherCall(nullptr, err, sizeof err); }
  wxDraw();
  if (err[0]) msgBox(L("CLIMA", "WEATHER"), err, C_RED);
}
void wxTick() {
  int c = takeClicks(), pd = padTake();
  if (pd == PD_UP || pd == PD_LEFT) c--;
  if (pd == PD_DOWN || pd == PD_RIGHT) c++;
  int rows = min(wxRows(), 4);
  if (c) { int o = wxSel; wxSel = constrain(wxSel + c, 0, rows - 1); if (o != wxSel) { wxDrawRow(o); wxDrawRow(wxSel); click(); oledDirty = true; } }
  if (!takeShort()) return;
  PLAY(T_OK);
  char err[100] = "";
  const char* key = wxSel < mrEnv.nScen ? mrEnv.scenKey[wxSel] : nullptr;
  char k2[16] = "";
  if (key) { snprintf(k2, sizeof k2, "%s", key); key = k2; }
  busyBox(L("CLIMA", "WEATHER"), key ? L("La web cambia el clima y planea de nuevo...", "The web changes the weather and plans again...") : L("Leyendo el clima de la web...", "Reading the web's weather..."));
  bool ok = weatherCall(key, err, sizeof err);
  wxDraw();
  if (!ok) { PLAY(T_NO); msgBox(L("CLIMA", "WEATHER"), err, C_RED); return; }
  PLAY(T_GOOD);
  MrRoute& R = mrM->r;
  char m[160], lb[48];
  wxLabel(mrEnv.key, mrEnv.label, lb, sizeof lb);
  if (R.ok && R.nStops >= 2) snprintf(m, sizeof m, L("%s. Tu ruta: %.2f km, %d min (a %.1f km/h).", "%s. Your route: %.2f km, %d min (at %.1f km/h)."), lb, R.dist / 1000, R.minutes, mrEnv.speed);
  else if (R.nStops >= 2) snprintf(m, sizeof m, "%s. %s", lb, mrMsg[0] ? mrMsg : L("Tu ruta ya no tiene paso.", "Your route has no way through now."));
  else snprintf(m, sizeof m, "%s.", lb);
  msgBox(L("CLIMA", "WEATHER"), m, mrEnv.nZones ? C_YELLOW : C_GREEN);
}
void oledWeather() {
  char w[24], a[24], b[24];
  wxShort(w, sizeof w);
  snprintf(a, sizeof a, "%.1f km/h", mrEnv.speed);
  snprintf(b, sizeof b, L("zonas cerradas: %d", "closed zones: %d"), mrEnv.nZones);
  oledCard(w, a, b, !strcmp(mrEnv.source, "synthetic") ? L("simulado", "simulated") : "", "");
}

// ---------- MISIONES ----------
int msSel = 0, msTop = 0, msActFor = 0;
int msRows() { return 3 + msCount; }
int msFileAt(int row) { int k = row - 3; for (int i = 1; i <= MS_MAX; i++) if (msHave[i] && k-- == 0) return i; return 0; }
void missionSave() {
  MrRoute& R = mrM->r;
  if (R.nStops < 2) { PLAY(T_NO); msgBox(L("GUARDAR", "SAVE"), L("Primero arma una ruta: dos paradas o m\xA0s.", "First make a route: two stops or more.")); return; }
  if (!fsOk) { msgBox(L("GUARDAR", "SAVE"), L("No hay \xA0rea de archivos.", "No file area."), C_RED); return; }
  missionScan();
  int slot = 0;
  for (int i = 1; i <= MS_MAX && !slot; i++) if (!msHave[i]) slot = i;
  if (!slot) { PLAY(T_NO); msgBox(L("GUARDAR", "SAVE"), L("Ya hay 30 misiones: borra alguna en MISIONES.", "There are 30 missions already: delete one in MISSIONS.")); return; }
  char p[24], t[700], m[96];
  snprintf(p, sizeof p, "/marte/m%02d.txt", slot);
  int n = mrMissionText(t, sizeof t);
  File f = LittleFS.open(p, "w");
  bool ok = f && n && f.write((const uint8_t*)t, n) == (size_t)n;
  if (f) f.close();
  missionScan();
  snprintf(m, sizeof m, ok ? L("Guardada como misi\xA2n %d (%d paradas).", "Saved as mission %d (%d stops).") : L("No se pudo guardar.", "Couldn't save it."), slot, R.nStops);
  if (ok) PLAY(T_GOOD); else PLAY(T_NO);
  msgBox(L("GUARDAR", "SAVE"), m, ok ? C_GREEN : C_RED);
}
void msRowText(int row, char* a, int an, char* b, int bn) {
  b[0] = 0;
  if (row == 0) { snprintf(a, an, "%s", L("Guardar la ruta de ahora", "Save the route you have")); return; }
  if (row == 1) { snprintf(a, an, "%s", L("Comprobar con el servidor", "Check it with the server")); return; }
  if (row == 2) { snprintf(a, an, "%s", L("Recibir de la web (c\xA2" "digo)", "Get one from the web (code)")); return; }
  int i = msFileAt(row);
  char p[24]; snprintf(p, sizeof p, "/marte/m%02d.txt", i);
  MrReply r;
  if (!missionLoadText(p, &r)) { snprintf(a, an, L("Misi\xA2n %d (da\xA4" "ada)", "Mission %d (damaged)"), i); return; }
  char n0[MR_NAME], n1[MR_NAME];
  mrShow(r.name[0], n0, sizeof n0); mrShow(r.name[r.nStops - 1], n1, sizeof n1);
  snprintf(a, an, "%.14s > %.14s", n0, n1);
  snprintf(b, bn, L("%d paradas", "%d stops"), r.nStops);
}
const int MS_VIS = 7, MS_Y = 30, MS_ROW = 27;
void msDrawRow(int s) {
  int row = msTop + s, y = MS_Y + s * MS_ROW;
  bool sel = row == msSel;
  uint16_t bg = sel ? C_ORANGE : C_PANEL;
  tft.fillRect(8, y, W - 16, MS_ROW - 3, row < msRows() ? bg : C_BG);
  if (row >= msRows()) {                          // (no missions yet: say so, once)
    if (row == 3 && !msCount) putText(16, y + 8, 1, C_MUTED, C_BG, L("A\xA3n no hay misiones guardadas.", "No saved missions yet."));
    return;
  }
  char a[44], b[24];
  msRowText(row, a, sizeof a, b, sizeof b);
  drawIcon(ICONS[row < 3 ? 1 : 0], 16, y + 4, 1, sel ? C_ONOR : row < 3 ? C_ORANGE : C_CYAN);
  putText(40, y + 8, 1, sel ? C_ONOR : C_TEXT, bg, a);
  if (b[0]) putRight(W - 16, y + 8, 1, sel ? C_ONOR : C_MUTED, bg, b);
}
void msDraw() {
  tft.fillScreen(C_BG); drawTitle(L("Misiones", "Missions"));
  for (int s = 0; s < MS_VIS; s++) msDrawRow(s);
  drawHints(L("OK: elegir   mantener: inicio", "OK: choose   hold: home"));
  oledDirty = true;
}
void msEnter() { missionScan(); msSel = min(msSel, msRows() - 1); msTop = min(msTop, msSel); msDraw(); }
void msTick() {
  if (pmOpen) {
    int ch = pmTick();
    if (ch < 0) return;
    char p[24]; snprintf(p, sizeof p, "/marte/m%02d.txt", msActFor);
    MrReply r;
    if (ch == 0 || ch == 1) {
      if (!missionLoadText(p, &r)) { msDraw(); msgBox(L("MISION", "MISSION"), L("No se pudo leer.", "Couldn't read it."), C_RED); return; }
      int bad = 0, res = routeTake(r, &bad);
      if (res <= 0) { char f[110]; mrFailText(res, bad, f, sizeof f); snprintf(mrMsg, sizeof mrMsg, "%s", f); }
      else snprintf(mrMsg, sizeof mrMsg, L("Misi\xA2n %d: %.2f km, +%.1f m, %d min.", "Mission %d: %.2f km, +%.1f m, %d min."), msActFor, mrM->r.dist / 1000, mrM->r.climb, mrM->r.minutes);
      if (ch == 0) { go(SC_MAP); return; }
      msDraw(); srvCheck(); return;
    }
    if (ch == 2) { LittleFS.remove(p); missionScan(); msSel = min(msSel, msRows() - 1); }
    msDraw();
    return;
  }
  int c = takeClicks(), pd = padTake();
  if (pd == PD_UP || pd == PD_LEFT) c--;
  if (pd == PD_DOWN || pd == PD_RIGHT) c++;
  if (c) {
    int o = msSel; msSel = constrain(msSel + c, 0, msRows() - 1);
    if (msSel != o) {
      click();
      if (msSel < msTop || msSel >= msTop + MS_VIS) { msTop = constrain(msSel - MS_VIS / 2, 0, max(0, msRows() - MS_VIS)); for (int s = 0; s < MS_VIS; s++) msDrawRow(s); }
      else { msDrawRow(o - msTop); msDrawRow(msSel - msTop); }
      oledDirty = true;
    }
  }
  if (!takeShort()) return;
  PLAY(T_OK);
  if (msSel == 0) { missionSave(); return; }
  if (msSel == 1) { srvCheck(); return; }
  if (msSel == 2) { kbBegin(2, L("C\xA2" "digo de 4 letras (web: Enviar al Psion):", "4-letter code (web: Send to the Psion):"), "", 4, 1); return; }
  msActFor = msFileAt(msSel);
  snprintf(pmItem[0], 36, "%s", L("Abrir en el mapa", "Open on the map")); snprintf(pmItem[1], 36, "%s", L("Abrir y comprobar", "Open and check"));
  snprintf(pmItem[2], 36, "%s", L("Borrar", "Delete"));
  pmN = 3;
  char t[24]; snprintf(t, sizeof t, L("MISION %d", "MISSION %d"), msActFor);
  pmShow(t);
}
void oledMissions() {
  char a[44], b[24], l[3][24];
  msRowText(msSel, a, sizeof a, b, sizeof b);
  int at = 0;
  for (int k = 0; k < 2; k++) { at = mrWrap(a, at, 21, l[k]); }
  snprintf(l[2], 24, L("guardadas: %d", "saved: %d"), msCount);
  oledCard(L("MISIONES", "MISSIONS"), l[0], l[1], b[0] ? b : "", l[2]);
}

// ---------- SITIOS: the 136 ChemCam sites, in Curiosity's order ----------
int stSel = 0, stTop = 0;
const int ST_VIS = 9, ST_Y = 28, ST_ROW = 21;
void stDrawRow(int s) {
  int i = stTop + s, y = ST_Y + s * ST_ROW;
  bool sel = i == stSel;
  uint16_t bg = sel ? C_ORANGE : (s & 1 ? C_PANEL : C_BG);
  tft.fillRect(8, y, W - 16, ST_ROW - 1, bg);
  if (i >= MR_NSITES) return;
  char b[40], nm[MR_NAME];
  mrName(i, nm, sizeof nm);
  snprintf(b, sizeof b, "%3d", i + 1); putText(12, y + 6, 1, sel ? C_ONOR : C_MUTED, bg, b);
  snprintf(b, sizeof b, "%.26s", nm); putText(42, y + 6, 1, sel ? C_ONOR : C_TEXT, bg, b);
  snprintf(b, sizeof b, "sol %u", MR_SITES[i].sol0); putRight(W - 14, y + 6, 1, sel ? C_ONOR : C_MUTED, bg, b);
}
void stDraw() {
  tft.fillScreen(C_BG); drawTitle(L("Sitios", "Sites"));
  for (int s = 0; s < ST_VIS; s++) stDrawRow(s);
  drawHints(L("OK: ver en el mapa   mantener: inicio", "OK: show on the map   hold: home"));
  oledDirty = true;
}
void stTick() {
  int c = takeClicks(), pd = padTake();
  if (pd == PD_UP) c--; else if (pd == PD_DOWN) c++; else if (pd == PD_LEFT) c -= ST_VIS; else if (pd == PD_RIGHT) c += ST_VIS;
  if (c) {
    int o = stSel; stSel = constrain(stSel + c, 0, MR_NSITES - 1);
    if (stSel != o) {
      click();
      if (stSel < stTop || stSel >= stTop + ST_VIS) { stTop = constrain(stSel - ST_VIS / 2, 0, MR_NSITES - ST_VIS); for (int s = 0; s < ST_VIS; s++) stDrawRow(s); }
      else { stDrawRow(o - stTop); stDrawRow(stSel - stTop); }
      oledDirty = true;
    }
  }
  if (takeShort()) { mrSiteI = stSel; mrCx = MR_SITES[stSel].x; mrCy = MR_SITES[stSel].y; mrMode = MR_MAP; mrMsg[0] = 0; PLAY(T_OK); go(SC_MAP); }
}
void oledSites() {
  char n[MR_NAME], a[24], b[24], c[24], d[24];
  const MrSite& T = MR_SITES[stSel];
  mrName(stSel, n, sizeof n);
  snprintf(a, sizeof a, "%.21s", n);
  snprintf(b, sizeof b, L("%d objetivo(s)", "%d target(s)"), T.n);
  snprintf(c, sizeof c, L("vetas %d  n\xA2" "dulos %d", "veins %d  nodules %d"), T.veins, T.nodules);
  snprintf(d, sizeof d, T.sol1 != T.sol0 ? "sol %u..%u" : "sol %u", T.sol0, T.sol1);
  char t[12]; snprintf(t, sizeof t, "%s %d", L("SITIO", "SITE"), stSel + 1);
  oledCard(t, a, b, c, d);
}

// ---------- MARTE: the four facts the web shows (NASA's Mars facts) ----------
int fcSel = 0;
const char* const MR_FACTS_EN[4][3] = {           // (1.3) the same four facts in English
  {"A day on Mars", "\xF7 24.6 h",
   "NASA calls Martian days sols, about 24.6 hours long. The sols on the map are days of the Curiosity mission."},
  {"A thin atmosphere", "Mostly CO2",
   "The air is mostly carbon dioxide, nitrogen and argon. A human walk needs life support; this app doesn't model a suit."},
  {"Extreme temperatures", "-153 to 20 \xF8" "C",
   "A general range NASA gives for Mars. Not a forecast nor what was measured on our route; it doesn't change the time."},
  {"A watery past", "Valleys \xFA deltas \xFA lakes",
   "NASA describes valleys, deltas, old lake beds and minerals made with liquid water. That's why veins and nodules matter, without saying they prove life."},
};
const char* fact(int i, int k) { return mrLang ? MR_FACTS_EN[i][k] : MR_FACTS[i][k]; }
void fcDraw() {                                  // (1.3) the icon, the title, the number big, the text; dots for the page
  tft.fillScreen(C_BG); drawTitle(L("Marte", "Mars"));
  tft.fillRect(8, 30, W - 16, 180, C_PANEL);
  drawIcon(ICONS[3], 22, 44, 3, C_ORANGE);
  putText(84, 46, 1, C_ORANGE, C_PANEL, fact(fcSel, 0));
  int vs = textW(fact(fcSel, 1), 2) <= W - 100 ? 2 : 1;
  putText(84, 62, vs, C_TEXT, C_PANEL, fact(fcSel, 1));
  putWrap(22, 108, 46, 6, 13, C_TEXT, C_PANEL, fact(fcSel, 2));
  for (int i = 0; i < MR_NFACTS; i++) tft.fillCircle(W / 2 - (MR_NFACTS - 1) * 7 + i * 14, 200, 3, i == fcSel ? C_ORANGE : C_LINE);
  drawHints(L("perilla / flechas: siguiente   mantener: inicio", "knob / arrows: next   hold: home"));
  oledDirty = true;
}
void fcTick() {
  int c = takeClicks(), pd = padTake();
  if (pd == PD_UP || pd == PD_LEFT) c--;
  if (pd == PD_DOWN || pd == PD_RIGHT) c++;
  if (takeShort()) c++;
  if (c) { fcSel = ((fcSel + c) % MR_NFACTS + MR_NFACTS) % MR_NFACTS; click(); fcDraw(); }
}

// ---------- WIFI ----------
int wfSel = 0, wfMode = 0, wfN = 0, wfPick = -1;   // wfMode 0: the page, 1: scanning / the list
char wfSsid[16][33];
int8_t wfRssi[16];
uint8_t wfLock[16];
void wfDraw() {                                  // (1.3) four rows and one status line; the list of networks
  tft.fillScreen(C_BG); drawTitle("Wi-Fi");
  char b[64];
  if (wfMode == 0) {
    const char* rows[4] = {"Wi-Fi", L("Buscar redes", "Find networks"), L("Olvidar la red guardada", "Forget the saved network"), L("Probar la conexi\xA2n", "Test the connection")};
    for (int i = 0; i < 4; i++) {
      int y = 32 + i * 28; bool s = i == wfSel;
      uint16_t bg = s ? C_ORANGE : C_PANEL;
      tft.fillRect(8, y, W - 16, 24, bg);
      putText(16, y + 8, 1, s ? C_ONOR : C_TEXT, bg, rows[i]);
      if (i == 0) putRight(W - 16, y + 8, 1, s ? C_ONOR : S.wifiOn ? C_GREEN : C_RED, bg, S.wifiOn ? L("encendido", "on") : L("apagado", "off"));
    }
    int y = 152;
    if (online()) { snprintf(b, sizeof b, L("Conectado a %.30s", "Connected to %.30s"), WiFi.SSID().c_str()); putText(12, y, 1, C_GREEN, C_BG, b); }
    else putText(12, y, 1, C_YELLOW, C_BG, !S.wifiOn ? L("Wi-Fi apagado.", "Wi-Fi is off.") : wifiSaved() ? L("Sin conexi\xA2n: lo intenta solo.", "Not connected: it keeps trying.") : L("Sin red guardada: Buscar redes.", "No saved network: Find networks."));
    drawHints(L("OK: elegir   mantener: inicio", "OK: choose   hold: home"));
  } else {
    if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) putText(12, 40, 1, C_CYAN, C_BG, L("Buscando redes...", "Looking for networks..."));
    else if (!wfN) putText(12, 40, 1, C_YELLOW, C_BG, L("No encontr\x82 redes. OK: buscar otra vez.", "No networks found. OK: look again."));
    for (int i = 0; i < wfN && i < 8; i++) {
      int y = 30 + i * 24; bool s = i == wfSel;
      uint16_t bg = s ? C_ORANGE : C_PANEL;
      tft.fillRect(8, y, W - 16, 21, bg);
      snprintf(b, sizeof b, "%.30s%s", wfSsid[i], wfLock[i] ? " *" : "");
      putText(16, y + 7, 1, s ? C_ONOR : C_TEXT, bg, b);
      int bars = wfRssi[i] > -55 ? 4 : wfRssi[i] > -65 ? 3 : wfRssi[i] > -75 ? 2 : 1;   // the signal, as bars
      for (int k = 0; k < 4; k++) { int hh = 3 + k * 2; tft.fillRect(W - 34 + k * 4, y + 15 - hh, 3, hh, k < bars ? (s ? C_ONOR : C_TEXT) : C_LINE); }
    }
    drawHints(L("* = con contrase\xA4" "a   OK: conectar   mantener: volver", "* = password   OK: join   hold: back"));
  }
  oledDirty = true;
}
void wfJoin(const char* ssid, const char* pass) {
  char m[96];
  busyBox(L("CONECTANDO", "CONNECTING"), ssid);
  radioPrep();
  if (WiFi.getMode() == WIFI_OFF) { WiFi.mode(WIFI_STA); }
  wifiJoining = true;
  WiFi.persistent(true);
  WiFi.begin(ssid, pass);
  uint32_t t = millis();
  while (millis() - t < 15000 && WiFi.status() != WL_CONNECTED) { delay(100); }
  wifiJoining = false;
  wfMode = 0; wfSel = 0;
  wfDraw();
  if (online()) { PLAY(T_GOOD); snprintf(m, sizeof m, L("Conectado a %s.", "Connected to %s."), ssid); msgBox("WI-FI", m, C_GREEN); }
  else { PLAY(T_NO); msgBox("WI-FI", L("No se conect\xA2. Revisa la contrase\xA4" "a y que la red sea de 2.4 GHz.", "It didn't connect. Check the password, and that it's a 2.4 GHz network."), C_RED); }
}
void srvHealth() {
  char sv[64]; serverName(sv, sizeof sv);
  busyBox(L("PROBANDO LA CONEXION", "TESTING THE CONNECTION"), sv);
  String ans;
  char m[260];
  uint16_t col;
  if (!S.conn) {                                 // the relay: is it there, and has it got the web open (with the hook)?
    int st = srvCall("/mr/hello", nullptr, ans);
    char web[72] = "", err[120] = "";
    bool webOk = false, mars = false;
    MrJson j = {ans.c_str(), false};
    if (st == 200 && mrJsIs(j, '{')) do {
      char k[16]; mrJsString(j, k, sizeof k);
      if (j.bad || !mrJsIs(j, ':')) break;
      if (!strcmp(k, "web")) mrJsString(j, web, sizeof web);
      else if (!strcmp(k, "error")) mrJsString(j, err, sizeof err);
      else if (!strcmp(k, "webOk")) webOk = mrJsBool(j);
      else if (!strcmp(k, "relay")) { char v[8]; mrJsString(j, v, sizeof v); mars = !strcmp(v, "mars"); }
      else mrJsSkip(j);
    } while (!j.bad && mrJsIs(j, ','));
    if (st != 200 || !mars) {
      MrReply r; mrParseReply(ans.c_str(), &r);
      snprintf(m, sizeof m, L("El relay %s no responde%s%s. Abre el Mars relay en tu PC (start_mars_relay.bat) y copia la direcci\xA2n que muestra.",
                              "The relay %s doesn't answer%s%s. Open the Mars relay on your PC (start_mars_relay.bat) and copy the address it shows."),
               S.relay, r.error[0] ? ": " : "", r.error[0] ? r.error : "");
      col = C_RED;
    } else if (webOk) { snprintf(m, sizeof m, L("Relay listo, con la web %.60s abierta: en vivo, clima y vista 3D.", "The relay is ready, with the web %.60s open: live, weather and 3D view."), web); col = C_GREEN; }
    else { snprintf(m, sizeof m, L("El relay responde, pero la web %.50s no: %.120s", "The relay answers, but the web %.50s doesn't: %.120s"), web, err); col = C_YELLOW; }
  } else {
    int st = srvCall("/api/health", nullptr, ans);
    bool psion = ans.indexOf("\"psion\"") >= 0;
    if (st == 200) snprintf(m, sizeof m, L("%s responde. %s", "%s answers. %s"), sv, psion ? L("Tiene el buz\xA2n del Psion: puedes enviar y recibir misiones.", "It has the Psion's mailbox: you can send and get missions.") : L("Pero a\xA3n no tiene el buz\xA2n del Psion (actualiza Ruta Marciana).", "But it doesn't have the Psion's mailbox yet (update Ruta Marciana)."));
    else { MrReply r; mrParseReply(ans.c_str(), &r); snprintf(m, sizeof m, L("%s no responde: %s", "%s doesn't answer: %s"), sv, r.error[0] ? r.error : "error"); }
    col = st == 200 && psion ? C_GREEN : st == 200 ? C_YELLOW : C_RED;
  }
  if (col == C_GREEN) PLAY(T_GOOD); else PLAY(T_NO);
  if (scr == SC_WIFI) wfDraw(); else if (scr == SC_SET) go(SC_SET);
  msgBox(L("CONEXION", "CONNECTION"), m, col);
}
void wfTick() {
  if (wfMode == 1 && WiFi.scanComplete() >= 0 && wfN < 0) {
    int n = WiFi.scanComplete();
    wfN = 0;
    for (int i = 0; i < n && wfN < 16; i++) {
      bool dup = false;
      for (int k = 0; k < wfN; k++) if (!strcmp(wfSsid[k], WiFi.SSID(i).c_str())) dup = true;
      if (dup || !WiFi.SSID(i).length()) continue;
      snprintf(wfSsid[wfN], 33, "%s", WiFi.SSID(i).c_str()); wfRssi[wfN] = WiFi.RSSI(i); wfLock[wfN] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
      wfN++;
    }
    WiFi.scanDelete();
    wfDraw();
  }
  int c = takeClicks(), pd = padTake();
  if (pd == PD_UP || pd == PD_LEFT) c--;
  if (pd == PD_DOWN || pd == PD_RIGHT) c++;
  int rows = wfMode == 0 ? 4 : max(1, min(wfN, 8));
  if (c) { int o = wfSel; wfSel = constrain(wfSel + c, 0, rows - 1); if (o != wfSel) { click(); wfDraw(); } }
  if (!takeShort()) return;
  PLAY(T_OK);
  if (wfMode == 1) {
    if (wfN <= 0) { wfN = -1; radioPrep(); WiFi.scanNetworks(true); wfDraw(); return; }
    wfPick = wfSel;
    if (!wfLock[wfPick]) { wfJoin(wfSsid[wfPick], ""); return; }
    char pr[48]; snprintf(pr, sizeof pr, L("Contrase\xA4" "a de %.24s:", "Password for %.24s:"), wfSsid[wfPick]);
    kbBegin(1, pr, "", 63, 0);
    return;
  }
  switch (wfSel) {
    case 0: S.wifiOn ^= 1; saveSettings(); if (S.wifiOn) wifiStart(); else { WiFi.disconnect(false, false); WiFi.mode(WIFI_OFF); } wfDraw(); break;
    case 1:
      if (!S.wifiOn) { S.wifiOn = 1; saveSettings(); wifiStart(); }
      radioPrep(); if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);
      wfMode = 1; wfSel = 0; wfN = -1; WiFi.scanNetworks(true); wfDraw(); break;
    case 2: WiFi.disconnect(false, true); wfDraw(); msgBox("WI-FI", L("Red olvidada (borrada de la memoria).", "Network forgotten (erased from memory).")); break;   // (erases: the Psion's 4.3.1 fix)
    case 3: srvHealth(); break;
  }
}
bool wfBack() { if (wfMode == 1) { wfMode = 0; wfSel = 1; wfDraw(); return true; } return false; }
void oledWifi() {
  char a[24];
  if (online()) snprintf(a, sizeof a, "%.21s", WiFi.SSID().c_str());
  else snprintf(a, sizeof a, "%s", S.wifiOn ? L("sin conexi\xA2n", "not connected") : L("apagado", "off"));
  oledCard("WI-FI", a, S.conn ? L("conexi\xA2n: web", "connection: web") : L("conexi\xA2n: relay", "connection: relay"), "", "");
}

// ---------- AJUSTES (1.3): the same settings, in groups, the language first ----------
enum { SE_LANG, SE_BRIGHT, SE_FLIP, SE_PANEL, SE_OLED, SE_DIM, SE_SLEEP, SE_VOL, SE_STEREO, SE_KNOB, SE_CONN, SE_RELAY, SE_WEB, SE_SYNC, SE_TEST, SE_ABOUT, SE_N };
const int8_t SE_LIST[] = {-1, SE_LANG, -2, SE_BRIGHT, SE_FLIP, SE_PANEL, SE_OLED, SE_DIM, SE_SLEEP, -3, SE_VOL, SE_STEREO, SE_KNOB,
                          -4, SE_CONN, SE_RELAY, SE_WEB, SE_SYNC, SE_TEST, -5, SE_ABOUT};   // (< 0: a group's heading)
const int SE_LN = sizeof SE_LIST;
const uint8_t DIM_S[5] = {0, 15, 30, 60, 120}, SLEEP_M[5] = {0, 2, 5, 10, 30};
int seSel = 1, seTop = 0;                        // (positions in SE_LIST)
const int SE_VIS = 8, SE_Y = 28, SE_ROW = 20;
int seItem() { return SE_LIST[seSel]; }
const char* seName(int i) {
  switch (i) {
    case SE_LANG: return "Idioma / Language";       // (both: you can find it in either)
    case SE_BRIGHT: return L("Brillo", "Brightness");
    case SE_FLIP: return L("Orientaci\xA2n", "Orientation");
    case SE_PANEL: return L("Colores del panel", "Panel colours");
    case SE_OLED: return L("Pantalla chica", "Small screen");
    case SE_DIM: return L("Atenuar sin uso", "Dim when idle");
    case SE_SLEEP: return L("Dormir sin uso", "Sleep when idle");
    case SE_VOL: return L("Volumen", "Volume");
    case SE_STEREO: return L("Est\x82reo", "Stereo");
    case SE_KNOB: return L("Giro de la perilla", "Knob direction");
    case SE_CONN: return L("Conexi\xA2n", "Connection");
    case SE_RELAY: return L("Direcci\xA2n del relay", "Relay address");
    case SE_WEB: return L("Direcci\xA2n de la web", "Web address");
    case SE_SYNC: return L("En vivo con la web", "Live with the web");
    case SE_TEST: return L("Probar la conexi\xA2n", "Test the connection");
    default: return L("Acerca de Mars OS", "About Mars OS");
  }
}
const char* seHead(int h) {
  switch (h) {
    case -1: return "IDIOMA / LANGUAGE";
    case -2: return L("PANTALLA", "SCREEN");
    case -3: return L("SONIDO Y PERILLA", "SOUND AND KNOB");
    case -4: return L("CONEXION", "CONNECTION");
    default: return L("SISTEMA", "SYSTEM");
  }
}
bool seChangeable(int i) { return i != SE_RELAY && i != SE_WEB && i != SE_TEST && i != SE_ABOUT; }
void seValue(int i, char* b, int n) {
  static const char* const VOL[2][4] = {{"apagado", "bajo", "medio", "alto"}, {"off", "low", "medium", "high"}};
  switch (i) {
    case SE_LANG: snprintf(b, n, "%s", mrLang ? "English" : "Espa\xA4ol"); break;
    case SE_BRIGHT: snprintf(b, n, "%d / 5", S.bright + 1); break;
    case SE_VOL: snprintf(b, n, "%s", VOL[mrLang][S.volume & 3]); break;
    case SE_STEREO: snprintf(b, n, "%s", S.stereo ? L("giros por lado", "turns by side") : L("los dos lados", "both sides")); break;
    case SE_KNOB: snprintf(b, n, "%s", S.knobRev ? L("al rev\x82s", "reversed") : "normal"); break;
    case SE_FLIP: snprintf(b, n, "%s", S.flip ? L("girada 180\xF8", "turned 180\xF8") : "normal"); break;
    case SE_PANEL: snprintf(b, n, "%s", S.panelInv ? L("IPS (normal)", "IPS (normal)") : L("invertidos", "inverted")); break;
    case SE_OLED: snprintf(b, n, "%s", S.oled ? L("encendida", "on") : L("apagada", "off")); break;
    case SE_DIM: if (DIM_S[S.dimAfter % 5]) snprintf(b, n, L("a los %d s", "after %d s"), DIM_S[S.dimAfter % 5]); else snprintf(b, n, "%s", L("nunca", "never")); break;
    case SE_SLEEP: if (SLEEP_M[S.sleepAfter % 5]) snprintf(b, n, L("a los %d min", "after %d min"), SLEEP_M[S.sleepAfter % 5]); else snprintf(b, n, "%s", L("nunca", "never")); break;
    case SE_CONN: snprintf(b, n, "%s", S.conn ? L("Web directa", "Web direct") : L("Relay (mi PC)", "Relay (my PC)")); break;
    case SE_RELAY: snprintf(b, n, "%.22s", S.relay); break;
    case SE_WEB: { const char* h = strstr(S.web, "://"); snprintf(b, n, "%.22s", !S.web[0] ? L("(sin direcci\xA2n)", "(none)") : h ? h + 3 : S.web); break; }
    case SE_SYNC: snprintf(b, n, "%s", S.sync ? L("s\xA1", "on") : L("no", "off")); break;
    case SE_ABOUT: snprintf(b, n, "Mars OS %s", OS_VERSION); break;
    default: b[0] = 0; break;
  }
}
const char* seHelp(int i) {
  switch (i) {
    case SE_LANG: return L("Cambia todos los textos (tambi\x82n la pantalla chica).", "Changes every text (the small screen too).");
    case SE_CONN: return L("Relay: tu PC abre la web (en vivo, clima, 3D). Web directa: solo el servidor.", "Relay: your PC opens the web (live, weather, 3D). Web direct: only the server.");
    case SE_RELAY: return L("La que muestra la ventana del Mars relay, p. ej. 192.168.1.23:8790", "The one the Mars relay window shows, e.g. 192.168.1.23:8790");
    case SE_WEB: return L("Solo para Web directa. Si la web se muda, escribe la nueva (https://...).", "Only for Web direct. If the web moves, type the new one (https://...).");
    case SE_SYNC: return L("Tu ruta y la de la web se copian solas (con el relay).", "Your route and the web's copy each other (with the relay).");
    case SE_STEREO: return L("Con dos bocinas: el pitido de un giro suena de su lado.", "With two speakers: a turn's beep comes from its side.");
    default: return "";
  }
}
void seDrawRow(int s) {
  int k = seTop + s, y = SE_Y + s * SE_ROW;
  if (k >= SE_LN) { tft.fillRect(8, y, W - 16, SE_ROW, C_BG); return; }
  int i = SE_LIST[k];
  if (i < 0) {                                   // a group's heading
    tft.fillRect(8, y, W - 16, SE_ROW, C_BG);
    putText(10, y + 9, 1, C_ORANGE, C_BG, seHead(i));
    return;
  }
  bool sel = k == seSel;
  uint16_t bg = sel ? C_ORANGE : C_PANEL;
  tft.fillRect(8, y, W - 16, SE_ROW - 2, bg);
  tft.fillRect(8, y + SE_ROW - 2, W - 16, 2, C_BG);
  char v[32]; seValue(i, v, sizeof v);
  putText(16, y + 6, 1, sel ? C_ONOR : C_TEXT, bg, seName(i));
  if (sel && seChangeable(i)) { char w[40]; snprintf(w, sizeof w, "< %s >", v); putRight(W - 14, y + 6, 1, C_ONOR, bg, w); }
  else putRight(W - 16, y + 6, 1, sel ? C_ONOR : C_CYAN, bg, v);
}
void seHelpLine() {
  tft.fillRect(0, 196, W, FOOT - 196, C_BG);
  putWrap(10, 198, 50, 2, 11, C_MUTED, C_BG, seHelp(seItem()));
}
void seDraw() {
  tft.fillScreen(C_BG); drawTitle(L("Ajustes", "Settings"));
  for (int s = 0; s < SE_VIS; s++) seDrawRow(s);
  seHelpLine();
  drawHints(L("perilla: mover   flechas / OK: cambiar", "knob: move   arrows / OK: change"));
  oledDirty = true;
}
void applyScreen() {
  tft.setRotation(S.flip ? 3 : 1);
  tft.invertDisplay(S.panelInv);
  o240Power(S.oled != 0);                        // (the small screen's task puts the panel to sleep / wakes it)
}
void seChange(int d) {
  switch (seItem()) {
    case SE_LANG: mrLang ^= 1; prefs.putUChar("lang", mrLang); break;
    case SE_BRIGHT: S.bright = constrain(S.bright + d, 0, 4); blWrite(BL_DUTY[S.bright]); break;
    case SE_VOL: S.volume = (S.volume + d + 4) % 4; break;
    case SE_STEREO: S.stereo ^= 1; PLAYP(T_LEFT, 1); break;
    case SE_KNOB: S.knobRev ^= 1; break;
    case SE_FLIP: S.flip ^= 1; applyScreen(); break;
    case SE_PANEL: S.panelInv ^= 1; applyScreen(); break;
    case SE_OLED: S.oled ^= 1; applyScreen(); break;
    case SE_DIM: S.dimAfter = (S.dimAfter + d + 5) % 5; break;
    case SE_SLEEP: S.sleepAfter = (S.sleepAfter + d + 5) % 5; break;
    case SE_CONN: S.conn ^= 1; syncVer = -1; break;
    case SE_SYNC: S.sync ^= 1; syncVer = -1; break;
  }
  saveSettings();
}
void seTick() {
  int c = takeClicks(), pd = padTake();
  if (pd == PD_UP) c--; else if (pd == PD_DOWN) c++;
  if (c) {                                       // move, skipping the headings
    int o = seSel, k = seSel, d = c > 0 ? 1 : -1;
    for (int n = abs(c); n > 0; n--) { int t = k + d; while (t >= 0 && t < SE_LN && SE_LIST[t] < 0) t += d; if (t >= 0 && t < SE_LN) k = t; }
    seSel = k;
    if (o != seSel) {
      click();
      int top = seTop;
      if (seSel < seTop + 1) seTop = max(0, seSel - 1);          // (the group's heading stays in sight)
      else if (seSel >= seTop + SE_VIS) seTop = seSel - SE_VIS + 1;
      if (top != seTop) for (int s = 0; s < SE_VIS; s++) seDrawRow(s);
      else { seDrawRow(o - seTop); seDrawRow(seSel - seTop); }
      seHelpLine();
      oledDirty = true;
    }
  }
  bool full = false;
  if ((pd == PD_LEFT || pd == PD_RIGHT) && seChangeable(seItem())) {
    seChange(pd == PD_RIGHT ? 1 : -1); click();
    full = seItem() == SE_FLIP || seItem() == SE_PANEL || seItem() == SE_LANG;
    if (full) seDraw(); else seDrawRow(seSel - seTop);
    oledDirty = true;
  }
  if (!takeShort()) return;
  PLAY(T_OK);
  int it = seItem();
  if (it == SE_RELAY) { kbBegin(3, L("Direcci\xA2n del relay (IP:puerto de su ventana):", "Relay address (IP:port, as its window shows):"), S.relay, 39, 2); return; }
  if (it == SE_WEB) { kbBegin(4, L("Direcci\xA2n de la web (https://... o IP:puerto):", "Web address (https://... or IP:port):"), S.web, 71, 0); return; }
  if (it == SE_ABOUT) { go(SC_ABOUT); return; }
  if (it == SE_TEST) { srvHealth(); return; }
  seChange(1);
  if (it == SE_FLIP || it == SE_PANEL || it == SE_LANG) seDraw(); else seDrawRow(seSel - seTop);
  oledDirty = true;
}
void oledSet() {
  char v[32], a[24];
  seValue(seItem(), v, sizeof v);
  snprintf(a, sizeof a, "%.21s", seName(seItem()));
  oledCard(L("AJUSTES", "SETTINGS"), a, v, "", "");
}

// ---------- ACERCA ----------
void abDraw() {                                  // (1.3) Micor, the name, the version, the battery; the credits, short
  tft.fillScreen(C_BG); drawTitle(L("Acerca", "About"));
  tft.fillRect(8, 30, 112, 112, C_PANEL);
  drawWolf(16, 38, 6);
  char b[64];
  putText(132, 36, 2, C_ORANGE, C_BG, "OJO DE MARTE");
  snprintf(b, sizeof b, "Mars OS %s - Psion Micor", OS_VERSION); putText(132, 58, 1, C_TEXT, C_BG, b);
  putText(132, 70, 1, C_MUTED, C_BG, L("de Ruta Marciana", "by Ruta Marciana"));
  putText(132, 82, 1, C_MUTED, C_BG, L("Micor el lobo espacial :3", "Micor the space wolf :3"));
  if (batThere()) snprintf(b, sizeof b, L("bater\xA1" "a %d%% (%d.%02d V)", "battery %d%% (%d.%02d V)"), batPct(), batMv() / 1000, batMv() % 1000 / 10);
  else snprintf(b, sizeof b, "%s", L("con USB", "on USB"));
  putText(132, 104, 1, C_TEXT, C_BG, b);
  snprintf(b, sizeof b, L("misiones guardadas: %d", "saved missions: %d"), msCount); putText(132, 116, 1, C_TEXT, C_BG, b);
  static const char* const T[2][4] = {
    {"Terreno: HiRISE (NASA/JPL/University of Arizona).", "Sitios: Curiosity ChemCam, Gasda et al.",
     "Rutas y palabras: Ojo de Marte (Ruta Marciana).", "Simulaci\xA2n educativa; sin afiliaci\xA2n con la NASA."},
    {"Ground: HiRISE (NASA/JPL/University of Arizona).", "Sites: Curiosity ChemCam, Gasda et al.",
     "Routes and words: Ojo de Marte (Ruta Marciana).", "Educational simulation; not affiliated with NASA."}};
  for (int i = 0; i < 4; i++) putText(10, 156 + i * 14, 1, i < 2 ? C_MUTED : C_TEXT, C_BG, T[mrLang][i]);
  drawHints(L("mantener: volver", "hold: back"));
  oledDirty = true;
}

// ---------- INICIO (1.3): eight big tiles, a line that says what the chosen one does ----------
const int HM_X = 10, HM_Y = 30, HM_W = 72, HM_H = 80, HM_GAP = 4;
void hmDrawTile(int i) {
  int x = HM_X + (i % 4) * (HM_W + HM_GAP), y = HM_Y + (i / 4) * (HM_H + HM_GAP);
  bool sel = i == homeSel;
  uint16_t acc = i == 0 ? C_ORANGE : i < 6 ? C_CYAN : C_MUTED, bg = sel ? C_ORANGE : C_PANEL;
  tft.fillRect(x, y, HM_W, HM_H, bg);
  drawIcon(ICONS[HOME_ICON[i]], x + (HM_W - 32) / 2, y + 14, 2, sel ? C_ONOR : acc);
  putCenter(x + HM_W / 2, y + 60, 1, sel ? C_ONOR : C_TEXT, bg, homeName(i));
}
void hmDesc() {
  tft.fillRect(0, 200, W, FOOT - 200, C_BG);
  putCenter(W / 2, 208, 1, C_MUTED, C_BG, homeDesc(homeSel));
}
void hmDraw() {
  tft.fillScreen(C_BG); drawTitle("Ojo de Marte");
  for (int i = 0; i < HM_N; i++) hmDrawTile(i);
  hmDesc();
  drawHints(L("OK: abrir   mantener 2 s: dormir", "OK: open   hold 2 s: sleep"));
  oledDirty = true;
}
void hmTick() {
  int c = takeClicks(), pd = padTake();
  if (pd == PD_LEFT) c--; else if (pd == PD_RIGHT) c++; else if (pd == PD_UP || pd == PD_DOWN) c += 4;   // (two rows: up / down = the other row)
  if (c) {
    int o = homeSel; homeSel = ((homeSel + c) % HM_N + HM_N) % HM_N;
    if (o != homeSel) { hmDrawTile(o); hmDrawTile(homeSel); hmDesc(); click(); oledDirty = true; }
  }
  if (takeShort()) { PLAY(T_OK); go(homeSel + 1); }
}
void oledHome() {                                // the chosen tile, big, and what it does
  char l[3][24];
  const char* d = homeDesc(homeSel);
  int at = 0;
  for (int k = 0; k < 3; k++) { if (d[at]) at = mrWrap(d, at, 21, l[k]); else l[k][0] = 0; }
  oledCard(homeName(homeSel), l[0], l[1], l[2], "");
}

// ---------- screen switching, the OLED, sleep ----------
void go(int s) {
  pmOpen = false; msgOpen = false; kbOpen = false;
  if (scr == SC_MAP && s != SC_MAP && mrMode == MR_WALK) mrMode = MR_MAP;
  scr = s;
  takeClicks(); padTake(); fShort = false;
  switch (s) {
    case SC_GUIDE: watch_guide::draw(); break;
    case SC_HOME: hmDraw(); break;
    case SC_MAP: mapEnter(); break;
    case SC_MISSIONS: msEnter(); break;
    case SC_VIEW: v3Enter(); break;
    case SC_WEATHER: wxEnter(); break;
    case SC_SITES: stSel = mrSiteI; stTop = constrain(stSel - ST_VIS / 2, 0, MR_NSITES - ST_VIS); stDraw(); break;
    case SC_FACTS: fcDraw(); break;
    case SC_WIFI: wfMode = 0; wfSel = 0; wfDraw(); break;
    case SC_SET: seDraw(); break;
    case SC_ABOUT: abDraw(); break;
  }
  oledDirty = true;
}
void redraw() { int s = scr; scr = -1; go(s); }  // (after a box closes or the screen wakes)
bool sleeping = false;
uint32_t batWarnAt = 0;
uint8_t batWarned = 0;
bool batWarnShow = false;
void oledBars() {                                // the small screen's bars: time + battery above, Wi-Fi + connection below
  O240Bars b;
  memset(&b, 0, sizeof b);
  b.show = !sleeping;                            // (asleep: only the time, big, in the middle)
  bool warn = batWarnShow || (batThere() && batPct() <= 10);
  char t[8];
  if (warn) snprintf(b.tl, sizeof b.tl, "%s", L("BATERIA", "BATTERY")); else if (clockText(t, sizeof t)) snprintf(b.tl, sizeof b.tl, "%s", t); else snprintf(b.tl, sizeof b.tl, "%s", L("MARTE", "MARS"));
  if (batThere()) snprintf(b.tr, sizeof b.tr, "%d%%", batPct()); else snprintf(b.tr, sizeof b.tr, "USB");
  if (online()) snprintf(b.bl, sizeof b.bl, "%.13s", WiFi.SSID().c_str()); else snprintf(b.bl, sizeof b.bl, "%s", S.wifiOn ? L("sin Wi-Fi", "no Wi-Fi") : L("Wi-Fi apagado", "Wi-Fi off"));
  snprintf(b.br, sizeof b.br, "%s", S.conn ? "WEB" : !syncOn() ? "RELAY" : syncFail ? "WEB?" : L("VIVO", "LIVE"));
  b.top = warn ? C_RED : C_ORANGE; b.topInk = warn ? C_TEXT : C_ONOR;
  b.bot = C_PANEL2; b.botInk = online() ? C_CYAN : C_MUTED;
  o240SetBars(b);
}
void drawOled() {
  if (!S.oled) return;
  oledBars();
  if (sleeping) {                                // asleep: the time, or a small wolf
    oled.clearDisplay(); oled.setTextColor(SSD1306_WHITE);
    char b[8];
    if (clockText(b, sizeof b)) { oled.setTextSize(3); oled.setCursor(19, 20); oled.print(b); }
    else { oled.setTextSize(1); oled.setCursor(40, 28); oled.print("zzz :3"); }
    oledShow();
    return;
  }
  if (batWarnShow) {
    char v[24]; snprintf(v, sizeof v, "%d.%02d V (%d%%)", batMv() / 1000, batMv() % 1000 / 10, batPct());
    oledCard(batWarned >= 2 ? L("BATERIA!", "BATTERY!") : L("BATERIA", "BATTERY"), batWarned >= 2 ? L("casi vacia", "almost empty") : L("baja", "low"), v, L("cargala pronto", "charge it soon"), "");
    oledShow(); return;
  }
  if (kbOpen) { char a[24]; snprintf(a, sizeof a, "%.21s", kbBuf); oledCard(L("TECLADO", "KEYBOARD"), a, "", "", ""); oledShow(); return; }
  switch (scr) {
    case SC_GUIDE: watch_guide::small(); break;
    case SC_HOME: oledHome(); break;
    case SC_MAP: oledMap(); break;
    case SC_MISSIONS: oledMissions(); break;
    case SC_VIEW: oledView(); break;
    case SC_WEATHER: oledWeather(); break;
    case SC_SITES: oledSites(); break;
    case SC_FACTS: { char a[24]; snprintf(a, sizeof a, "%.21s", fact(fcSel, 0)); oledCard(L("MARTE", "MARS"), a, fact(fcSel, 1), "", L("datos de NASA", "NASA facts")); break; }
    case SC_WIFI: oledWifi(); break;
    case SC_SET: oledSet(); break;
    default: { char b[24]; snprintf(b, sizeof b, "Mars OS %s", OS_VERSION); oledCard(L("ACERCA", "ABOUT"), b, "Ojo de Marte", "Ruta Marciana", "Micor :3"); break; }
  }
  oledShow();
}
void sleepEnter() {
  sleeping = true;
  routeSaveLast();
  blWrite(S.oled ? BL_GLIMMER : 0);              // (the small screen shares the backlight: a glimmer for its clock)
  tft.enableSleep(true);
  oledDirty = true;
}
void sleepWake() {
  sleeping = false;
  tft.enableSleep(false);
  blWrite(BL_DUTY[S.bright]);
  takeClicks(); padTake(); fShort = fLong = fSleep = false;
  lastInput = millis();
  oledDirty = true;
}
void batWarnTick(uint32_t now) {                 // (the Psion's 4.2.1 warnings) 3.50 V: once; 3.40 V: every 5 minutes
  if (!batThere()) return;
  int mv = batMv();
  if (mv >= 3700) batWarned = 0;
  int lvl = mv <= 3400 ? 2 : mv <= 3500 ? 1 : 0;
  if (lvl > batWarned || (lvl == 2 && now - batWarnAt > 300000UL)) {
    batWarned = max((int)batWarned, lvl); batWarnAt = now; batWarnShow = true; PLAY(T_LOW); oledDirty = true;
  }
  if (batWarnShow && now - batWarnAt > 6000) { batWarnShow = false; oledDirty = true; }
}

void syncTick(uint32_t now) {                    // live sync: send a change of yours after 0.7 s, ask the web every 2 s
  if (!syncOn() || !online() || sleeping || pmOpen || msgOpen || kbOpen || scr==SC_GUIDE) return;
  if (scr == SC_MAP && mrMode == MR_WALK) return;   // (no stops in the walk's animation)
  if (syncDirty && now - syncDirtyAt > 700 && syncVer >= 0) { syncDirty = false; syncPush(); syncAt = now; return; }
  if (now - syncAt < 2000) return;
  syncAt = now;
  if (syncPull() > 0) {
    if (scr == SC_MAP) mapEnter(); else if (scr == SC_WEATHER) wxDraw();
    oledDirty = true; PLAY(T_AHEAD);
  }
  if (syncDirty && syncVer >= 0) { syncDirty = false; syncPush(); }   // (the start rule: an empty web gets your route)
}

// ================= START =================
void splash() {
  tft.fillScreen(C_BG);
  tft.fillRect(0, 0, W, 3, C_ORANGE);
  drawWolf(24, 56, 7);
  putText(150, 66, 1, C_ORANGE, C_BG, "MARS / EXPLORER");
  putText(150, 80, 3, C_TEXT, C_BG, "OJO DE");
  putText(150, 106, 3, C_TEXT, C_BG, "MARTE");
  char b[48]; snprintf(b, sizeof b, "Psion Micor  \xFA  Mars OS %s", OS_VERSION);
  putText(150, 140, 1, C_MUTED, C_BG, b);
  putText(150, 152, 1, C_MUTED, C_BG, L("de Ruta Marciana", "by Ruta Marciana"));
  putText(24, 196, 1, C_CYAN, C_BG, "GLEN TORRIDON  04.73\xF8S 137.39\xF8" "E  \xFA  CRATER GALE");
  oledCard(L("MARTE", "MARS"), "Ojo de Marte", "Ruta Marciana", "Psion Micor", L("arrancando...", "starting..."));
  oledShow();
}
void setup() {
  o240EarlyPins(OLED_SCK);                       // first thing: the small screen's clock line high (no CS: no stray pulse)
  setCpuFrequencyMhz(240);
  Serial.begin(115200);
  Serial.printf("\nPsion Micor - Mars OS %s (Ojo de Marte)\n", OS_VERSION);
  prefs.begin("marteos", false);
  loadSettings();
  mrLang = prefs.getUChar("lang", 0) ? 1 : 0;     // (1.3) the language, saved apart from the settings block
  pinMode(ENC_CLK, INPUT_PULLUP); pinMode(ENC_DT, INPUT_PULLUP); pinMode(BTN_PIN, INPUT_PULLUP);
  encPrev = (digitalRead(ENC_CLK) << 1) | digitalRead(ENC_DT);
  attachInterrupt(digitalPinToInterrupt(ENC_CLK), encISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_DT), encISR, CHANGE);
  pinMode(TFT_BL, OUTPUT); blWrite(BL_DUTY[S.bright]);
  tft.init(240, 320);
  tft.setSPISpeed(TFT_SPI_HZ);
  applyScreen();
  for (int8_t p : PAD_PINS) pinMode(p, INPUT_PULLUP);      // (after tft.init(): D19 is the screen's unused SPI input otherwise)
  pinMode(PAD_OK, INPUT_PULLUP);
  oled.cp437(true);
  o240Colors(C_TEXT, C_BG);                      // cream letters on the web's dark background
  if (!o240Begin(OLED_SCK, OLED_MOSI, OLED_DC, OLED_SPI_HZ, OLED_ROTATE, OLED_BGR, FONT5X7)) Serial.printf("marte: small screen didn't start\n");
  splash();
  spkUp();
  PLAY(T_BOOT);
  uint32_t t0 = millis();
  mrM = (MrMem*)calloc(1, sizeof(MrMem));        // the map + the route: kept all the time (20 KB)
  if (!mrM) { putText(24, 214, 1, C_RED, C_BG, L("Sin RAM para el mapa.", "No RAM for the map.")); for (;;) delay(1000); }
  mrUnpack();
  mrM->r.lim = S.lim;
  fsOk = LittleFS.begin(false);                   // (the Psion's file area: the same partition; nothing of yours is erased)
  if (fsOk) LittleFS.mkdir("/marte");
  envLoad();                                     // the last weather from the web (zones closed + speed), or clear
  mrSetLimit(S.lim);
  missionScan();
  MrReply last;
  int bad = 0;
  if (fsOk && missionLoadText("/marte/last.txt", &last)) routeTake(last, &bad);   // the route you had before
  else mrFinish();
  mrSiteI = S.lastSite < MR_NSITES ? S.lastSite : 0;
  if (!mrM->r.nStops) { mrCx = MR_SITES[mrSiteI].x; mrCy = MR_SITES[mrSiteI].y; }
  Serial.printf("marte: map ready in %lu ms, RAM free %u KB\n", (unsigned long)(millis() - t0), (unsigned)(ESP.getFreeHeap() / 1024));
  syncDirty = false;                             // (the route it started with isn't news for the web)
  wifiStart();
  while (millis() - t0 < 1500) { sndTick(millis()); delay(10); }
  lastInput = millis();
  go(SC_HOME);
}

void loop() {
  uint32_t now = millis();
  buttonPoll();
  padPoll(now);
  sndTick(now);
  batTick(now);
  batWarnTick(now);
  wifiWatch(now);
  if (encCount || fShort || fLong || padEv || fSleep) lastInput = now;
  if (sleeping) {
    if (fShort || fLong || encCount || padEv) { sleepWake(); redraw(); }
    static uint32_t at = 0;
    if (now - at > 20000) { at = now; oledDirty = true; }
    if (oledDirty) { oledDirty = false; drawOled(); }
    delay(20);
    return;
  }
  if (fSleep) { fSleep = false; fLong = false; sleepEnter(); return; }
  if (SLEEP_M[S.sleepAfter % 5] && now - lastInput > SLEEP_M[S.sleepAfter % 5] * 60000UL && !(scr == SC_MAP && mrMode == MR_WALK)) { sleepEnter(); return; }
  // dim when idle (never during a walk)
  int full = BL_DUTY[S.bright];
  bool dim = DIM_S[S.dimAfter % 5] && now - lastInput > DIM_S[S.dimAfter % 5] * 1000UL && !(scr == SC_MAP && mrMode == MR_WALK);
  int want = dim ? max(4, full * 3 / 10) : full;
  static uint32_t blAt = 0;
  if (blNow != want && now - blAt >= 20) { blAt = now; blWrite(want > blNow ? want : max(want, (int)blNow - 4)); }

  if (fLong) {                                   // hold = back
    fLong = false;
    if (msgOpen) { msgOpen = false; redraw(); }
    else if (kbOpen) { kbOpen = false; redraw(); }
    else if (pmOpen) { pmOpen = false; redraw(); }
    else if (scr == SC_MAP && mapBack()) {}
    else if (scr == SC_WIFI && wfBack()) {}
    else if (scr == SC_ABOUT) go(SC_SET);
    else if (scr != SC_HOME) { if (scr == SC_MAP) { S.lastSite = mrSiteI; saveSettings(); routeSaveLast(); } go(SC_HOME); }
  }
  if (msgOpen) { takeClicks(); padTake(); if (takeShort()) { msgOpen = false; PLAY(T_OK); redraw(); } }
  else if (kbOpen) {
    int r = kbTick();
    if (r) {
      int purpose = kbPurpose;
      if (r > 0 && purpose == 1) wfJoin(wfSsid[wfPick], kbBuf);
      else if (r > 0 && purpose == 2) {
        char code[8]; int n = 0;
        for (const char* p = kbBuf; *p && n < 4; p++) if (*p != ' ') code[n++] = (char)toupper((unsigned char)*p);
        code[n] = 0;
        redraw();
        if (n == 4) srvFetch(code); else msgBox(L("RECIBIR", "RECEIVE"), L("El c\xA2" "digo tiene 4 letras (ej. K7Q2).", "The code has 4 letters (e.g. K7Q2)."));
      } else if (r > 0 && purpose == 3) { snprintf(S.relay, sizeof S.relay, "%s", kbBuf); syncVer = -1; saveSettings(); redraw(); }
      else if (r > 0 && purpose == 5) { ruta_link::connect(kbBuf); redraw(); }
      else if (r > 0 && purpose == 4) { snprintf(S.web, sizeof S.web, "%s", kbBuf); saveSettings(); redraw(); }
      else redraw();
    }
  } else switch (scr) {
    case SC_GUIDE: watch_guide::tick(now); break;
    case SC_HOME: hmTick(); break;
    case SC_MAP: mapTick(now); break;
    case SC_MISSIONS: msTick(); break;
    case SC_VIEW: v3Tick(now); break;
    case SC_WEATHER: wxTick(); break;
    case SC_SITES: stTick(); break;
    case SC_FACTS: fcTick(); break;
    case SC_WIFI: wfTick(); break;
    case SC_SET: seTick(); break;
    default: takeClicks(); padTake(); takeShort(); break;
  }
  syncTick(now);
  static bool wasOnline = false;                 // (1.0 kept "sin conexi\xA2n" on the home panel until you moved)
  if (online() != wasOnline) { wasOnline = online(); if (!pmOpen && !msgOpen && !kbOpen && !(scr == SC_MAP && mrMode == MR_INFO)) drawStatus(); oledDirty = true; }
  static uint32_t statusAt = 0;                  // the title bar's Wi-Fi / battery / clock
  if (now - statusAt > 10000 && !pmOpen && !msgOpen && !kbOpen && !(scr == SC_MAP && mrMode == MR_INFO)) { statusAt = now; drawStatus(); }
  static uint32_t oledAt = 0, barsAt = 0;
  if (oledDirty && now - oledAt >= 120) { oledDirty = false; oledAt = now; drawOled(); }
  if (now - barsAt >= 5000 && S.oled) { barsAt = now; oledBars(); }   // (the time, the battery, Wi-Fi on the small screen's bars)
  delay(1);
}
