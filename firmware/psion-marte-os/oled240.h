// ===================== oled240.h: the small screen, now a GMT130-V1.0 (240 x 240) =====================
// (Psion Micor 4.4 / Mars OS 1.2) The 0.96" I2C OLED (SSD1306, 128 x 64) is gone. In its place: a GMT130-V1.0, a 1.3"
// IPS screen of 240 x 240 with an ST7789, on SPI WITHOUT a CS pin. With no CS it can't share the big screen's bus, so it
// gets the ESP32's second SPI bus, on the two pins the OLED used. Its 7 pins:
//     GND -> GND    VCC -> 3V3    SCL -> D22    SDA -> D21    RES -> EN (the ESP32's reset; or 3V3)
//     DC  -> D2     BLK -> D32 (together with the big screen's BL: both dim and sleep together; or 3V3 = always lit)
//   Recommended: a 10k resistor from SCL (D22) to 3V3. With no CS, the screen counts clock pulses from its reset on, and
//   one stray pulse while the ESP32 starts would shift every byte after it; the resistor keeps the clock line still.
//   D2 is also the DevKit's blue LED: it flickers while the small screen is being drawn (that's normal).
// Everything still draws the way it did: a 128 x 64 one-colour picture (OledCanvas: the same Adafruit GFX calls, the
// same buffer layout as the SSD1306's). This file shows it 1.875 times bigger (240 x 120) and smooth: each new pixel
// mixes the old pixels it covers (in linear light), so letters keep even strokes. Above it a coloured bar, below it a
// dark one: the sketch fills in their text (O240Bars). A task of its own sends to the panel, only what changed, and
// every 5 s it sets the panel up again and sends it all (a panel that restarted after a supply dip is back by itself).
// The bus is the Arduino core's own SPI driver (no DMA: then no new code in the ESP32's full IRAM); the sending task
// runs on core 0, next to Wi-Fi, so loop() on core 1 doesn't wait for it.
#pragma once
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "esp32-hal-spi.h"

#define SSD1306_BLACK   0                          // (the old OLED's colour names: the drawing code still uses them)
#define SSD1306_WHITE   1
#define SSD1306_INVERSE 2

// ---- the 128 x 64 picture everything draws into ----
class OledCanvas : public Adafruit_GFX {
 public:
  OledCanvas() : Adafruit_GFX(128, 64) {}
  void drawPixel(int16_t x, int16_t y, uint16_t c) override {
    if ((uint16_t)x >= 128 || (uint16_t)y >= 64) return;
    uint8_t* b = &buf[x + (y >> 3) * 128];
    uint8_t m = (uint8_t)(1 << (y & 7));
    if (c == SSD1306_WHITE) *b |= m; else if (c == SSD1306_BLACK) *b &= (uint8_t)~m; else *b ^= m;
  }
  void fillScreen(uint16_t c) override { memset(buf, c == SSD1306_WHITE ? 0xFF : 0, sizeof buf); }
  void clearDisplay() { memset(buf, 0, sizeof buf); }
  uint8_t* getBuffer() { return buf; }
 private:
  uint8_t buf[1024];                               // 8 bands of 8 rows; a byte = 8 rows of one column, bit 0 on top
};

// ---- the bars above and below the picture (the sketch fills them in; o240SetBars) ----
struct O240Bars {
  char tl[14], tr[10];                             // top bar: left (big letters), right
  char bl[22], br[10];                             // bottom bar: left, right
  uint16_t top, topInk, bot, botInk;               // colours (RGB565)
  bool show;                                       // false: no bars (the picture's background colour there)
};

// ---- layout of the 240 x 240 screen ----
const int O240_TOP_H = 48;                         // rows 0..47: the top bar
const int O240_PIC_Y = 60;                         // rows 60..179: the picture (8 bands of 15 rows)
const int O240_BOT_Y = 192;                        // rows 192..239: the bottom bar

static spi_t* o240Spi = nullptr;
static int8_t o240Dc = -1;
static uint8_t o240Mad = 0;
static uint16_t o240Xo = 0, o240Yo = 0;
static const uint8_t* o240Font = nullptr;          // 5 x 7, 5 bytes a letter, bit 0 on top (Adafruit's glcdfont)
static portMUX_TYPE o240Mux = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t o240TaskH = nullptr;
static uint8_t o240Pend[1024], o240Work[1024], o240Shown[1024];
static O240Bars o240BarsPend, o240BarsWork, o240BarsShown;
static volatile bool o240HasPic = false, o240HasBars = false, o240WantOn = true;
static volatile uint16_t o240Ink = 0xFFFF, o240Paper = 0x0000;
static bool o240On = false, o240Full = true, o240NeedDispOn = false;
static uint32_t o240SetAt = 0, o240OffAt = 0;
static uint16_t o240Lut[65];                       // 0..64 of 64 covered -> colour (bytes already in sending order)
static uint16_t o240LutInk = 0, o240LutPaper = 0;
static bool o240LutOk = false;
static uint16_t o240Strip[240 * 5];                // 5 rows of pixels on their way out (4-byte aligned: uint16 pairs)
static uint8_t o240Xs[240], o240Xw[240], o240Ys[15], o240Yw[15];
static uint32_t o240Frames = 0;                    // pictures sent (Settings / host test)

static inline uint16_t o240Swap(uint16_t c) { return (uint16_t)((c << 8) | (c >> 8)); }
static void o240Cmd(uint8_t c, const uint8_t* d = nullptr, int n = 0) {
  digitalWrite(o240Dc, LOW);
  spiWriteByteNL(o240Spi, c);
  if (n) { digitalWrite(o240Dc, HIGH); spiWriteNL(o240Spi, d, n); digitalWrite(o240Dc, LOW); }
}
static void o240Window(int x0, int y0, int x1, int y1) {   // then the pixels, row by row (o240Send)
  x0 += o240Xo; x1 += o240Xo; y0 += o240Yo; y1 += o240Yo;
  uint8_t a[4] = {(uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1};
  uint8_t b[4] = {(uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1};
  o240Cmd(0x2A, a, 4);                             // CASET
  o240Cmd(0x2B, b, 4);                             // RASET
  digitalWrite(o240Dc, LOW);
  spiWriteByteNL(o240Spi, 0x2C);                   // RAMWR: everything after it is pixels
  digitalWrite(o240Dc, HIGH);
}
static void o240Send(int px) { spiWriteNL(o240Spi, o240Strip, (uint32_t)px * 2); }
static void o240EndPixels() { digitalWrite(o240Dc, LOW); }   // (DC rests low: the blue LED is off between pictures)

static void o240Setup(bool reset, bool dispOn) {   // every setting the panel needs
  if (reset) { o240Cmd(0x01); vTaskDelay(pdMS_TO_TICKS(150)); }   // SWRESET
  o240Cmd(0x11);                                   // SLPOUT (no effect when it's awake: then nothing happens)
  vTaskDelay(pdMS_TO_TICKS(10));
  uint8_t v = 0x55;
  o240Cmd(0x3A, &v, 1);                            // COLMOD: 16 bits a pixel
  o240Cmd(0x36, &o240Mad, 1);                      // MADCTL: which way round
  o240Cmd(0x21);                                   // INVON: this IPS panel shows true colours with inversion on
  o240Cmd(0x13);                                   // NORON
  if (dispOn) o240Cmd(0x29);                       // DISPON
}

// ---- colours: in linear light, so a half-covered pixel looks half as bright ----
static float o240Lin(int c, int bits) { float f = c / (float)((1 << bits) - 1); return powf(f, 2.2f); }
static void o240MakeLut(uint16_t ink, uint16_t paper) {
  float ir = o240Lin(ink >> 11, 5), ig = o240Lin((ink >> 5) & 63, 6), ib = o240Lin(ink & 31, 5);
  float pr = o240Lin(paper >> 11, 5), pg = o240Lin((paper >> 5) & 63, 6), pb = o240Lin(paper & 31, 5);
  for (int v = 0; v <= 64; v++) {
    float a = v / 64.0f;
    int r = (int)(powf(a * ir + (1 - a) * pr, 1 / 2.2f) * 31 + 0.5f);
    int g = (int)(powf(a * ig + (1 - a) * pg, 1 / 2.2f) * 63 + 0.5f);
    int b = (int)(powf(a * ib + (1 - a) * pb, 1 / 2.2f) * 31 + 0.5f);
    o240Lut[v] = o240Swap((uint16_t)((r << 11) | (g << 5) | b));
  }
  o240LutInk = ink; o240LutPaper = paper; o240LutOk = true;
}
// The scale: 128 -> 240 is 8 -> 15. A new pixel covers 8/15 of an old one, so at most two old ones in each direction;
// o240Xs = the first of them, o240Xw = how much of it (of 8), the rest is the next one. 16 old columns make exactly 30
// new ones and a band of 8 rows exactly 15, so a band or a group of 16 columns can be redrawn on its own.
static void o240MakeScale() {
  for (int x = 0; x < 240; x++) {
    int s = 8 * x, i = s / 15, e = s + 8, cut = 15 * (i + 1);
    o240Xs[x] = (uint8_t)i; o240Xw[x] = (uint8_t)((e <= cut ? e : cut) - s);
    if (x < 15) { o240Ys[x] = o240Xs[x]; o240Yw[x] = o240Xw[x]; }
  }
}
static void o240Fill(int x0, int y0, int x1, int y1, uint16_t c) {
  int w = x1 - x0 + 1;
  uint16_t s = o240Swap(c);
  o240Window(x0, y0, x1, y1);
  for (int y = y0; y <= y1; y += 5) {
    int n = y1 - y + 1 < 5 ? y1 - y + 1 : 5;
    for (int i = 0; i < w * n; i++) o240Strip[i] = s;
    o240Send(w * n);
  }
  o240EndPixels();
}
static void o240Band(const uint8_t* pic, int p, int g0, int g1) {   // band p, 16-column groups g0..g1
  int x0 = g0 * 30, x1 = g1 * 30 + 29, w = x1 - x0 + 1;
  const uint8_t* col = pic + p * 128;
  o240Window(x0, O240_PIC_Y + p * 15, x1, O240_PIC_Y + p * 15 + 14);
  for (int j0 = 0; j0 < 15; j0 += 5) {
    uint16_t* out = o240Strip;
    for (int j = j0; j < j0 + 5; j++) {
      uint8_t m0 = (uint8_t)(1 << o240Ys[j]), wy0 = o240Yw[j], wy1 = (uint8_t)(8 - wy0);
      uint8_t m1 = wy1 ? (uint8_t)(m0 << 1) : 0;
      for (int x = x0; x <= x1; x++) {
        int c = o240Xs[x];
        uint8_t wx0 = o240Xw[x], wx1 = (uint8_t)(8 - wx0);
        uint8_t a = col[c], b = wx1 ? col[c + 1] : 0;
        int v = ((a & m0) ? wx0 * wy0 : 0) + ((b & m0) ? wx1 * wy0 : 0) + ((a & m1) ? wx0 * wy1 : 0) + ((b & m1) ? wx1 * wy1 : 0);
        *out++ = o240Lut[v];
      }
    }
    o240Send(5 * w);
  }
  o240EndPixels();
}
static void o240Picture(const uint8_t* pic, bool all) {   // only the bands / column groups that changed
  for (int p = 0; p < 8; p++) {
    const uint8_t* a = pic + p * 128;
    uint8_t* s = o240Shown + p * 128;
    int g0 = 0, g1 = 7;
    if (!all) {
      int c0 = 0;
      while (c0 < 128 && a[c0] == s[c0]) c0++;
      if (c0 == 128) continue;
      int c1 = 127;
      while (a[c1] == s[c1]) c1--;
      g0 = c0 >> 4; g1 = c1 >> 4;
    }
    o240Band(pic, p, g0, g1);
    memcpy(s, a, 128);
  }
  o240Frames++;
}
static void o240Text(uint16_t* line, int row, int x, int y, int sz, const char* s, uint16_t c) {   // one pixel row of a text
  if (!s[0] || row < y || row >= y + 7 * sz) return;
  int fr = (row - y) / sz;
  for (; *s && x < 240; s++, x += 6 * sz) {
    const uint8_t* g = o240Font + (uint8_t)*s * 5;
    for (int k = 0; k < 5; k++)
      if (pgm_read_byte(g + k) >> fr & 1)
        for (int i = 0; i < sz; i++) { int px = x + k * sz + i; if (px >= 0 && px < 240) line[px] = c; }
  }
}
static int o240RightX(const char* s, int sz) { return 240 - 8 - ((int)strlen(s) * 6 * sz - sz); }
static void o240Bar(bool top, const O240Bars& b) {
  int y0 = top ? 0 : O240_BOT_Y, h = top ? O240_TOP_H : 240 - O240_BOT_Y;
  uint16_t bg = o240Swap(b.show ? (top ? b.top : b.bot) : o240LutPaper), ink = o240Swap(top ? b.topInk : b.botInk);
  const char* l = top ? b.tl : b.bl;
  const char* r = top ? b.tr : b.br;
  int lsz = top ? 3 : 2, ly = (h - 7 * lsz) / 2, ry = (h - 14) / 2;
  o240Window(0, y0, 239, y0 + h - 1);
  for (int y = 0; y < h; y += 5) {
    int n = h - y < 5 ? h - y : 5;
    for (int i = 0; i < n; i++) {
      uint16_t* line = o240Strip + i * 240;
      for (int x = 0; x < 240; x++) line[x] = bg;
      if (b.show) { o240Text(line, y + i, 8, ly, lsz, l, ink); o240Text(line, y + i, o240RightX(r, 2), ry, 2, r, ink); }
    }
    o240Send(240 * n);
  }
  o240EndPixels();
}
static bool o240SameBar(bool top, const O240Bars& a, const O240Bars& b) {
  if (a.show != b.show) return false;
  if (top) return !strcmp(a.tl, b.tl) && !strcmp(a.tr, b.tr) && a.top == b.top && a.topInk == b.topInk;
  return !strcmp(a.bl, b.bl) && !strcmp(a.br, b.br) && a.bot == b.bot && a.botInk == b.botInk;
}

// ---- the task: everything that goes to the panel goes from here ----
static void o240Service() {
  uint32_t now = millis();
  bool pic = false, bars = false;
  portENTER_CRITICAL(&o240Mux);
  if (o240HasPic) { memcpy(o240Work, o240Pend, 1024); o240HasPic = false; pic = true; }
  if (o240HasBars) { o240BarsWork = o240BarsPend; o240HasBars = false; bars = true; }
  bool on = o240WantOn;
  uint16_t ink = o240Ink, paper = o240Paper;
  portEXIT_CRITICAL(&o240Mux);
  if (!on) {                                       // off: dark, and the panel asleep (it uses next to nothing)
    if (o240On) { o240Cmd(0x28); o240Cmd(0x10); o240On = false; o240OffAt = now; }
    return;
  }
  if (!o240On) {                                   // on again: awake, drawn whole, then shown
    uint32_t t = now - o240OffAt;
    if (t < 120) vTaskDelay(pdMS_TO_TICKS(120 - t));   // (the panel needs 120 ms between sleeping and waking)
    o240Cmd(0x11); vTaskDelay(pdMS_TO_TICKS(10));
    o240On = true; o240Full = true; o240NeedDispOn = true;
  }
  if (!o240LutOk || ink != o240LutInk || paper != o240LutPaper) { o240MakeLut(ink, paper); o240Full = true; }
  if (now - o240SetAt >= 5000) { o240SetAt = now; o240Setup(false, !o240NeedDispOn); o240Full = true; }
  (void)pic; (void)bars;
  if (o240Full) {
    o240Full = false;
    o240Bar(true, o240BarsWork);
    o240Fill(0, O240_TOP_H, 239, O240_PIC_Y - 1, paper);
    o240Picture(o240Work, true);
    o240Fill(0, O240_PIC_Y + 120, 239, O240_BOT_Y - 1, paper);
    o240Bar(false, o240BarsWork);
    o240BarsShown = o240BarsWork;
  } else {
    if (!o240SameBar(true, o240BarsWork, o240BarsShown)) o240Bar(true, o240BarsWork);
    if (!o240SameBar(false, o240BarsWork, o240BarsShown)) o240Bar(false, o240BarsWork);
    o240BarsShown = o240BarsWork;
    o240Picture(o240Work, false);
  }
  if (o240NeedDispOn) { o240NeedDispOn = false; o240Cmd(0x29); }
}
static void o240TaskFn(void*) {
  for (;;) { ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500)); o240Service(); }
}

// ---- for the sketch ----
// rotate: 0..3 = 0 / 90 / 180 / 270 degrees (try another if the picture is upside down or sideways);
// bgr: true if red and blue come out swapped. Call o240EarlyPins() first thing in setup(), o240Begin() later.
static void o240EarlyPins(int8_t sck) { pinMode(sck, OUTPUT); digitalWrite(sck, HIGH); }   // the clock's resting level (SPI mode 3)
static bool o240Begin(int8_t sck, int8_t mosi, int8_t dc, uint32_t hz, uint8_t rotate, bool bgr, const uint8_t* font5x7,
                      int core = 0, int prio = 1) {
  static const uint8_t MAD[4] = {0x00, 0x60, 0xC0, 0xA0};   // MADCTL for 0 / 90 / 180 / 270 (the panel's RAM is 240 x 320)
  o240Mad = (uint8_t)(MAD[rotate & 3] | (bgr ? 0x08 : 0));
  o240Xo = (rotate & 3) == 3 ? 80 : 0; o240Yo = (rotate & 3) == 2 ? 80 : 0;
  o240Font = font5x7; o240Dc = dc;
  o240MakeScale();
  pinMode(dc, OUTPUT); digitalWrite(dc, LOW);
  o240EarlyPins(sck);
  o240Spi = spiStartBus(HSPI, spiFrequencyToClockDiv(NULL, hz), SPI_MODE3, SPI_MSBFIRST);   // mode 3 BEFORE the pins:
  if (!o240Spi) return false;                      // the clock never drops (no CS: a stray pulse would shift the bytes)
  spiAttachSCK(o240Spi, sck);
  spiAttachMOSI(o240Spi, mosi);
  o240Setup(true, false);                          // reset, set up; shown after the first picture is drawn (no noise)
  o240On = true; o240Full = true; o240NeedDispOn = true; o240SetAt = millis();
  memset(&o240BarsWork, 0, sizeof o240BarsWork);
  memset(&o240BarsShown, 0, sizeof o240BarsShown);
  return xTaskCreatePinnedToCore(o240TaskFn, "oled240", 3072, nullptr, prio, &o240TaskH, core) == pdPASS;
}
static void o240Kick() { if (o240TaskH) xTaskNotifyGive(o240TaskH); }
static void o240Show(const uint8_t* pic) {         // a new 128 x 64 picture (1024 bytes, the SSD1306's layout)
  portENTER_CRITICAL(&o240Mux);
  memcpy(o240Pend, pic, 1024); o240HasPic = true;   // (the newest picture wins if the task is still busy)
  portEXIT_CRITICAL(&o240Mux);
  o240Kick();
}
static void o240SetBars(const O240Bars& b) {
  portENTER_CRITICAL(&o240Mux);
  o240BarsPend = b; o240HasBars = true;
  portEXIT_CRITICAL(&o240Mux);
  o240Kick();
}
static void o240Power(bool on) { if (o240WantOn != on) { o240WantOn = on; o240Kick(); } }
static void o240Colors(uint16_t ink, uint16_t paper) {
  if (o240Ink == ink && o240Paper == paper) return;
  portENTER_CRITICAL(&o240Mux);
  o240Ink = ink; o240Paper = paper;
  portEXIT_CRITICAL(&o240Mux);
  o240Kick();
}
static uint16_t o240Rgb(int r, int g, int b) { return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)); }
static uint16_t o240Dim(uint16_t c, int k) {       // a colour at k / 255 of its brightness
  int r = (c >> 11) * k / 255, g = ((c >> 5) & 63) * k / 255, b = (c & 31) * k / 255;
  return (uint16_t)((r << 11) | (g << 5) | b);
}
