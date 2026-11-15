#line 1 "/home/fox/.hermes/kanban/workspaces/t_7658b621/spaceapps/psion-micor-spaceapps/spaceapps_apps.h"
// =====================================================================
// spaceapps_apps.h — apps Space Apps para Psion Micor 4.3.1 ESP32
// MISION (presupuesto EVA + slot de mapa) y BITACORA (registro EVA).
// Va en un header (no en el .ino) porque el generador de prototipos de
// Arduino mutila bloques nuevos dentro del sketch; incluido en el .ino
// justo antes de drawOled(), cuando todas las helpers del shell existen.
// =====================================================================
#ifndef SPACEAPPS_APPS_H
#define SPACEAPPS_APPS_H


// --- orden interno: prototipos ---
uint16_t btAccent(); uint16_t msAccent();
void msCompute(); void msDraw(); void msDrawMapa(); void msDraw();
void bitacoraEnter(); void bitacoraLeave(); bool bitacoraWantsBack(); void bitacoraBack();
void bitacoraTick(uint32_t now); void oledBitacora();
void misionEnter(); void misionLeave(); bool misionWantsBack(); void misionBack();
void misionTick(uint32_t now); void oledMision();
bool btLoad(int id); bool btSave(); void btScan(); void btFallback(); void btManifest();
void btExport(int mode); void btDraw(); bool btWrite(const char* path, const char* data);
bool btNextIdentity(char* out, size_t n);
void btPath(char* p, size_t n, int id);
void roverEnter(); void roverLeave(); bool roverWantsBack(); void roverBack();
void roverTick(uint32_t now); void oledRover(); void roverLoad();

// ================= SPACE APPS: MISION (presupuesto EVA + slot de mapa) =================
// Dos pantallas hermanas de una sola experiencia "Misión": PRESUPUESTO (este módulo)
// y MAPA (del compañero: no implementado aquí — solo su contrato en mision_map_contract.h).
// El presupuesto NO recalcula rutas: consume mision::RouteInput si el mapa registró un
// proveedor; si no, usa los valores manuales editables (/eva.cfg, minutos en pantalla,
// segundos en el núcleo eva_budget.h). GO / JUST / NO-GO según eva_budget.h.
enum { MS_PRESU, MS_MAPA };
enum { MS_PAGE, MS_ROUTE, MS_OUT, MS_BACK, MS_STOPS, MS_DWELL, MS_OVH, MS_RES, MS_LIM, MS_ROWS };
struct MisionCfg { uint8_t ver, route; uint16_t outMin, backMin, dwellMin, ovhMin, resMin, limMin; uint8_t stops; };
const uint8_t MISION_CFG_VER = 1;
MisionCfg MC;
int msPage = MS_PRESU, msFocus = MS_PAGE, msEdit = -1;
bool msFresh = true;
eva::Result msR[2];                                 // [0] = ruta elegida, [1] = la otra (comparación)
char msNote[48] = "";
const char MS_ROWS_P[] PROGMEM = "Pantalla\nRuta (mapa)\nIda\nVuelta\nParadas\nCiencia/parada\nOverhead\nReserva\nLimite EVA";
const char MS_UNITS_P[] PROGMEM = "\nFAST | SAFE\nmin\nmin\n\nmin\nmin\nmin\nmin";
const char MS_PAGES_P[] PROGMEM = "Presupuesto EVA\nMapa (compañero)";

void msDefaults() {
  memset(&MC, 0, sizeof MC);
  MC.ver = MISION_CFG_VER; MC.route = (uint8_t)mision::FAST;
  MC.outMin = 10; MC.backMin = 10; MC.stops = 2; MC.dwellMin = 15;
  MC.ovhMin = 10; MC.resMin = 20; MC.limMin = 120;
}
void msLoad() {
  msDefaults();
  if (!fsOk) return;
  File f = LittleFS.open(FS("/eva.cfg"), FS("r"));
  if (!f) return;
  MisionCfg t;
  if (f.size() == sizeof t && f.read((uint8_t*)&t, sizeof t) == sizeof t && t.ver == MISION_CFG_VER) {
    t.stops = constrain(t.stops, 0, eva::MAX_STOPS);
    t.route = t.route > (uint8_t)mision::SAFE ? 0 : t.route;
    t.outMin = constrain(t.outMin, 0, 600); t.backMin = constrain(t.backMin, 0, 600);
    t.dwellMin = constrain(t.dwellMin, 0, 600); t.ovhMin = constrain(t.ovhMin, 0, 600);
    t.resMin = constrain(t.resMin, 0, 600); t.limMin = constrain(t.limMin, 1, 1440);
    MC = t;
  }
  f.close();
}
void msSave() {
  if (!fsOk) return;
  File f = LittleFS.open(FS("/eva.cfg"), FS("w"));
  if (f) { f.write((const uint8_t*)&MC, sizeof MC); f.close(); }
}
uint16_t msAccent() { return appAccent(APP_MISION); }
uint16_t msStatusColor(int st) { return st == eva::GO ? E_MINT : st == eva::JUST ? E_AMBER : E_CORAL; }
const char* msStatusName(int st) { return st == eva::GO ? FS("GO") : st == eva::JUST ? FS("JUST") : FS("NO-GO"); }

// Rellena eva::Input de una variante: mapa si hay proveedor, si no valores manuales.
bool msBuildInput(int variant, eva::Input* in) {
  memset(in, 0, sizeof *in);
  in->n_stops = MC.stops;
  in->overhead_s = MC.ovhMin * 60.0f;
  in->reserve_s = MC.resMin * 60.0f;
  in->limit_s = MC.limMin * 60.0f;
  mision::RouteInput ri;
  if (mision::provide(&ri, variant)) {              // el compañero conectó su mapa
    in->travel_out_s = ri.travel_out_s;
    in->travel_back_s = ri.travel_back_s;
    in->n_stops = constrain(ri.n_stops, 0, eva::MAX_STOPS);
    for (int i = 0; i < in->n_stops && i < eva::MAX_STOPS; i++) in->dwell_s[i] = ri.dwell_s[i];
    return true;
  }
  in->travel_out_s = MC.outMin * 60.0f;             // manual: mismos minutos para cada parada
  in->travel_back_s = MC.backMin * 60.0f;
  for (int i = 0; i < in->n_stops; i++) in->dwell_s[i] = MC.dwellMin * 60.0f;
  return false;
}
void msCompute() {                                  // ambos estados GO/JUST/NO-GO, siempre
  eva::Input a, b;                                  // msR queda indexado POR VARIANTE
  bool fromMapF = msBuildInput((int)mision::FAST, &a);
  bool fromMapS = msBuildInput((int)mision::SAFE, &b);
  eva::compute(&a, &msR[(int)mision::FAST]);
  eva::compute(&b, &msR[(int)mision::SAFE]);
  bool fromMap = MC.route == (uint8_t)mision::FAST ? fromMapF : fromMapS;
  snprintf_P(msNote, sizeof msNote, PSTR("%s"), mision::mapProvider ? (fromMap ? FS("mapa: conectado") : FS("mapa: sin datos")) : FS("mapa: pendiente (manual)"));
}

void msFmtMMSS(float s, char* b, size_t n) {        // segundos -> m:ss (pantalla en minutos)
  int m = (int)(s / 60.0f + 0.5f);
  snprintf_P(b, n, PSTR("%d:%02d"), m / 60, m % 60);
}
void msDraw() {
  tft.fillRect(0, TITLE_H, W, DOCK_Y - TITLE_H, C_LCD);
  char b[40];
  const int RX = 154, RW = W - RX - 4;
  // --- filas de la izquierda (navegación / edición) ---
  for (int i = 0; i < MS_ROWS; i++) {
    int y = 22 + i * 17;
    uint16_t bg = i == msFocus ? msAccent() : E_WHITE;
    tft.fillRect(4, y, 146, 18, bg);
    putText(8, y + 3, 1, i == msFocus ? E_ONACC : C_INK, bg, pgmItem(MS_ROWS_P, i));
    if (i >= MS_ROUTE) {
      const char* u = pgmItem(MS_UNITS_P, i);
      if (u && u[0]) putText(84, y + 3, 1, i == msFocus ? E_ONACC : C_MID, bg, u);
    }
    if (i == MS_PAGE) putText(112, y + 3, 1, i == msFocus ? E_ONACC : msAccent(), bg, msPage == MS_PRESU ? FS("PRESU") : FS("GUIA"));
    else if (i == MS_ROUTE) putText(112, y + 3, 1, i == msFocus ? E_ONACC : C_INK, bg, MC.route == (uint8_t)mision::FAST ? FS("FAST") : FS("SAFE"));
    else {
      int v = 0;
      switch (i) {
        case MS_OUT: v = MC.outMin; break; case MS_BACK: v = MC.backMin; break;
        case MS_STOPS: v = MC.stops; break; case MS_DWELL: v = MC.dwellMin; break;
        case MS_OVH: v = MC.ovhMin; break; case MS_RES: v = MC.resMin; break; case MS_LIM: v = MC.limMin; break;
      }
      snprintf_P(b, sizeof b, PSTR("%d"), v);
      putText(120, y + 3, 1, i == msFocus ? E_ONACC : C_INK, bg, b);
      if (i == msEdit) tft.drawRect(4, y, 146, 18, E_CORAL);   // editando: borde rojo
    }
  }
  // --- resultados de la derecha (ambas variantes; * = la elegida) ---
  tft.drawRect(RX, 22, RW, 9 * 17, E_RULE);
  putText(RX + 6, 26, 1, C_INK, C_LCD, FS("PRESUPUESTO EVA"));
  int y = 40;
  for (int k = 0; k < 2; k++) {
    const char* nm = k == (int)mision::FAST ? FS("FAST") : FS("SAFE");
    putText(RX + 6, y, 1, C_MID, C_LCD, nm);
    if (k == MC.route) putText(RX + 36, y, 1, msAccent(), C_LCD, FS("*"));
    eva::Result* r = &msR[k];
    if (r->valid) {
      snprintf_P(b, sizeof b, PSTR("total %d:%02d  margen %d:%02d"), (int)(r->total_s / 3600), (int)(r->total_s / 60) % 60, (int)(r->margin_s / 3600), (int)(r->margin_s / 60) % 60);
      putText(RX + 6, y + 10, 1, C_INK, C_LCD, b);
      uint16_t c = msStatusColor(r->status);
      tft.fillRect(RX + RW - 44, y, 40, 20, c);
      putText(RX + RW - 38, y + 5, 1, E_WHITE, c, msStatusName(r->status));
    } else {
      putText(RX + 6, y + 10, 1, E_CORAL, C_LCD, FS("datos invalidos"));
    }
    y += 26;
  }
  // --- desglose de la ruta elegida ---
  eva::Result* r = &msR[MC.route];
  if (r->valid) {
    char t1[12], t2[12], t3[12], t4[12];
    msFmtMMSS(r->travel_s, t1, sizeof t1);
    msFmtMMSS(r->science_s, t2, sizeof t2);
    msFmtMMSS(r->overhead_s, t3, sizeof t3);
    msFmtMMSS(r->total_s, t4, sizeof t4);
    snprintf_P(b, sizeof b, PSTR("viaje %s  ciencia %s  ovh %s  TOTAL %s"), t1, t2, t3, t4);
    putText(4, 198, 1, C_INK, C_LCD, b);
    snprintf_P(b, sizeof b, PSTR("limite %d min  reserva %d min  usable %d min"),
               (int)r->limit_s / 60, (int)r->reserve_s / 60, (int)r->usable_s / 60);
    putText(4, 186, 1, C_MID, C_LCD, b);
  }
  putText(4, 208, 1, C_MID, C_LCD, msNote);          // estado del contrato con el mapa
  oledDirty = true;
}
#include "navigator_link.h"
void msDrawMapa() { ruta_link::draw(); }
void misionEnter() {
  mision::setMapProvider(ruta_link::provide);
  wkOwner = APP_MISION;
  msLoad();
  tft.fillScreen(C_LCD);
  drawTitleBar(FS("Mision"), msAccent());
  drawDock(APP_MISION);
  msCompute();
  msEdit = -1;
  if (msFocus >= MS_ROWS) msFocus = MS_PAGE;
  if (msPage == MS_MAPA) msDrawMapa(); else msDraw();
  msFresh = false;
}
void misionLeave() { ruta_link::editing=false; msSave(); pmOpen = false; askOpen = false; wkOwner = 0; }
bool misionWantsBack() { return msPage == MS_MAPA; }
void misionBack() { ruta_link::editing=false; askOpen=false; msPage = MS_PRESU; msFresh = true; misionEnter(); }
void misionTick(uint32_t now) {
  (void)now;
  int c = takeClicks() * ENC_DIR;
  takePress();
  bool sp = takeShort();
  titleTimeTick();
  if (msFresh) { msFresh = false; msPage == MS_MAPA ? msDrawMapa() : msDraw(); }
  if (msPage == MS_MAPA) {
    ruta_link::tick(c, sp);
    return;
  }
  static uint32_t navBudgetAt=0;
  if(msEdit<0 && (uint32_t)(now-navBudgetAt)>=1000){navBudgetAt=now;msCompute();msDraw();}
  int old = msFocus;
  if (c) {
    if (msEdit >= 0) {                               // editando: el knob cambia el valor
      int* v = nullptr;
      if (msEdit == MS_OUT) v = (int*)&MC.outMin;
      else if (msEdit == MS_BACK) v = (int*)&MC.backMin;
      else if (msEdit == MS_DWELL) v = (int*)&MC.dwellMin;
      else if (msEdit == MS_OVH) v = (int*)&MC.ovhMin;
      else if (msEdit == MS_RES) v = (int*)&MC.resMin;
      else if (msEdit == MS_LIM) v = (int*)&MC.limMin;
      else if (msEdit == MS_STOPS) v = (int*)&MC.stops;
      if (v) *v = constrain(*v + c, msEdit == MS_LIM ? 1 : 0, msEdit == MS_STOPS ? eva::MAX_STOPS : msEdit == MS_LIM ? 1440 : 600);
      msCompute(); msDraw(); return;
    } else {
      msFocus = ((msFocus + c) % MS_ROWS + MS_ROWS) % MS_ROWS;
      if (old != msFocus) { uiClick(); msDraw(); }
    }
  }
  if (!sp) return;
  if (msEdit >= 0) { msEdit = -1; msSave(); msDraw(); return; }   // OK sale de la edición y guarda
  if (msFocus == MS_PAGE) { msPage = msPage == MS_PRESU ? MS_MAPA : MS_PRESU; msPage == MS_MAPA ? msDrawMapa() : msDraw(); return; }
  if (msFocus == MS_ROUTE) { MC.route ^= 1; msSave(); msCompute(); msDraw(); return; }   // FAST <-> SAFE
  msEdit = msFocus; msDraw();                        // OK entra a editar el valor
}
void oledMision() {
  if (msPage == MS_MAPA) { ruta_link::oled(); return; }
  eva::Result* r = &msR[MC.route];
  char l[4][24];
  snprintf_P(l[0], 24, PSTR("%s"), msPage == MS_MAPA ? FS("mapa: en obra") : (r->valid ? msStatusName(r->status) : FS("datos invalidos")));
  snprintf_P(l[1], 24, PSTR("%s"), msPage == MS_MAPA ? FS("contrato x confirmar") : (r->valid ? "ver pantalla p/detail" : FS("revisa los campos")));
  snprintf_P(l[2], 24, PSTR("%.23s"), msNote);
  snprintf_P(l[3], 24, PSTR("%s"), FS("girar / pulsar"));
  oledCard(FS("MISION"), l[0], l[1], l[2], l[3]);
}

// ================= SPACE APPS: BITACORA (registro EVA) =================
// Puerto del diseño legado (captura por pickers, archivo = fuente de verdad,
// sync al relay con reintento manual) sobre el shell 4.3.1 ESP32 y el núcleo
// puro bitacora_core.h. Sin String: buffers acotados, escritura atómica.
const int BT_MAX = 12, BT_WMAX = 16;
const char BT_STOPS_P[]   PROGMEM = "START\nS1\nS2\nS3\nS4\nS5\nEND\nOTRO";
const char BT_TYPES_P[]   PROGMEM = "ROCA\nSUELO\nVETA\nNODULO\nPOLVO\nIMAGEN\nOTRO";
const char BT_ACTIONS_P[] PROGMEM = "MUESTRA\nFOTO\nMEDICION\nSIN HALLAZGO";
const char BT_STATES_P[]  PROGMEM = "NORMAL\nCONTRATIEMPO\nPELIGRO\nREPLANIFICACION";
enum { BT_LIST, BT_ENTRIES, BT_PICK, BT_OTHER, BT_MENU, BT_WALKS_MENU };
int btPage = BT_LIST, btSel = 0, btTop = 0, btId = -1, btCount = 0, btField = 0, btPickIdx = 0;
int btIds[BT_MAX], btWaypointCount = 0;
char btWaypoints[BT_WMAX][24], btValues[4][24], btOther[101], btTime[9], btStatus[48] = "";
char btData[btx::MAX_FILE + 1];                        // la caminata abierta (flash es la verdad)
char btXbuf[btx::MAX_FILE * 2 + 8];                    // export JSON/CSV (solo durante el export)

uint16_t btAccent() { return appAccent(APP_BITACORA); }
void btPath(char* p, size_t n, int id) { snprintf_P(p, n, PSTR("/b%d.txt"), id); }
// rename sin destruir el destino: si falla queda la última versión buena.
bool btWrite(const char* path, const char* data) {
  if (!fsOk) return false;
  char tmp[16];
  snprintf_P(tmp, sizeof tmp, PSTR("%s.tmp"), path);
  File f = LittleFS.open(tmp, FS("w"));
  if (!f) return false;
  size_t len = strlen(data);
  bool ok = f.write((const uint8_t*)data, len) == len;
  f.flush(); f.close();
  if (!ok || !LittleFS.rename(tmp, path)) { LittleFS.remove(tmp); return false; }
  return true;
}
// Identidad reservada ANTES de crear la caminata: huecos seguros, contadores reutilizados no.
bool btNextIdentity(char* out, size_t n) {
  const char* path = "/btseq.txt";
  uint32_t seq = 0;
  if (!fsOk || !out) return false;
  if (LittleFS.exists(path)) {
    File f = LittleFS.open(path, FS("r"));
    if (!f || f.size() == 0 || f.size() > 10) { if (f) f.close(); return false; }
    char v[12]; size_t sz = f.size();
    if (f.read((uint8_t*)v, sz) != sz) { f.close(); return false; }
    f.close(); v[sz] = 0;
    for (size_t i = 0; i < sz; i++) {
      char ch = v[i];
      if (ch < '0' || ch > '9' || seq > (UINT32_MAX - (uint32_t)(ch - '0')) / 10) return false;
      seq = seq * 10 + (uint32_t)(ch - '0');
    }
  }
  if (seq == UINT32_MAX) return false;
  seq++;
  char sv[12];
  snprintf_P(sv, sizeof sv, PSTR("%lu"), (unsigned long)seq);
  if (!btWrite(path, sv)) return false;
  snprintf_P(out, n, PSTR("1%010lu%010lu"), (unsigned long)(ESP.getEfuseMac() & 0xFFFFFFFFUL), (unsigned long)seq);
  return true;
}
void btScan() {
  btCount = 0;
  for (int i = 0; fsOk && i < BT_MAX; i++) { char p[16]; btPath(p, sizeof p, i); if (LittleFS.exists(p)) btIds[btCount++] = i; }
}
void btFallback() {
  btWaypointCount = 8;
  for (int i = 0; i < btWaypointCount; i++) snprintf_P(btWaypoints[i], sizeof btWaypoints[0], PSTR("%s"), pgmItem(BT_STOPS_P, i));
}
void btManifest() {                                  // catálogo de paradas; sin red: fallback PROGMEM
  if (WiFi.status() != WL_CONNECTED) { btFallback(); snprintf_P(btStatus, sizeof btStatus, PSTR("Sin red: lista offline")); return; }
  char url[48];
  snprintf_P(url, sizeof url, PSTR("http://%s/api/manifest"), P.relay);
  WiFiClient client; HTTPClient http;
  http.setTimeout(2500);
  if (!http.begin(client, url)) { btFallback(); snprintf_P(btStatus, sizeof btStatus, PSTR("Sin red: lista offline")); return; }
  int code = http.GET();
  if (code != 200) { http.end(); if (code < 0 || code == 404) { btFallback(); snprintf_P(btStatus, sizeof btStatus, PSTR("Sin red: lista offline")); } return; }
  int length = http.getSize();                       // solo respuesta acotada (chunked: offline)
  if (length <= 0 || length > 200000) { http.end(); return; }
  WiFiClient* stream = http.getStreamPtr();
  stream->setTimeout(2500);
  struct Parser {
    WiFiClient* s; int left, ch, count; bool found; char names[BT_WMAX][24];
    Parser(WiFiClient* st, int n) : s(st), left(n), ch(-2), count(0), found(false) {}
    int peek() {
      if (ch != -2) return ch;
      if (!left) return ch = -1;
      uint32_t start = millis();
      while (!s->available()) { if (millis() - start >= 2500) return ch = -1; delay(1); }
      int c = s->read(); if (c >= 0) --left; return ch = c;
    }
    int take() { int c = peek(); ch = -2; return c; }
    void ws() { while (peek() == ' ' || peek() == '\n' || peek() == '\r' || peek() == '\t') take(); }
    bool str(char* out, int cap, bool& overflow) {
      if (take() != '"') return false;
      int n = 0; overflow = false;
      for (;;) {
        int c = take(); if (c < 32) return false;
        if (c == '"') { if (out) out[n] = 0; return true; }
        if (c == '\\') {
          c = take();
          if (c == 'u') { c = 0; for (int i = 0; i < 4; i++) { int h = take(); int v = h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : -1; if (v < 0) return false; c = c * 16 + v; } if (c > 127) c = 128; }
          else if (c == 'b') c = '\b'; else if (c == 'f') c = '\f'; else if (c == 'n') c = '\n'; else if (c == 'r') c = '\r'; else if (c == 't') c = '\t';
          else if (c != '"' && c != '\\' && c != '/') return false;
        }
        if (out) { if (!c) overflow = true; else if (n + 1 < cap) out[n++] = (char)c; else overflow = true; }
      }
    }
    bool value(int context, int depth) {
      if (depth > 24) return false;
      ws(); int c = peek();
      if (context == 1 && c != '{') return false;
      if (context == 2 && c != '[') return false;
      if (context == 3 && c != '{') return false;
      if (c == '{') {
        take(); ws(); bool named = false;
        if (peek() == '}') { take(); return context != 3; }
        for (;;) {
          char key[32]; bool over;
          if (!str(key, sizeof key, over)) return false;
          ws(); if (take() != ':') return false; ws();
          if (context == 1 && !over && !strcmp(key, "waypoints")) {
            if (found) return false;
            found = true; if (!value(2, depth + 1)) return false;
          } else if (context == 3 && !over && !strcmp(key, "name")) {
            if (named || count >= BT_WMAX - 1 || !str(names[count], sizeof names[0], over) || over || !names[count][0]) return false;
            for (int i = 0; names[count][i]; i++) { unsigned char x = names[count][i]; if (x < 32 || x > 126 || x == '|' || x == '=' || x == '\\') return false; }
            for (int i = 0; i < count; i++) if (!strcmp(names[i], names[count])) return false;
            count++; named = true;
          } else if (!value(0, depth + 1)) return false;
          ws(); c = take(); if (c == '}') return context != 3 || named; if (c != ',') return false; ws();
        }
      }
      if (c == '[') {
        take(); ws(); if (peek() == ']') { take(); return context != 2; }
        for (;;) { if (!value(context == 2 ? 3 : 0, depth + 1)) return false; ws(); c = take(); if (c == ']') return true; if (c != ',') return false; }
      }
      if (c == '"') { bool over; return str(nullptr, 0, over); }
      if (c == 't' || c == 'f' || c == 'n') { const char* p = c == 't' ? "true" : c == 'f' ? "false" : "null"; while (*p) if (take() != *p++) return false; return true; }
      if (c == '-') { take(); c = peek(); }
      if (c == '0') take(); else { if (c < '1' || c > '9') return false; do { take(); c = peek(); } while (c >= '0' && c <= '9'); }
      if (peek() == '.') { take(); c = peek(); if (c < '0' || c > '9') return false; do { take(); c = peek(); } while (c >= '0' && c <= '9'); }
      c = peek(); if (c == 'e' || c == 'E') { take(); c = peek(); if (c == '+' || c == '-') take(); c = peek(); if (c < '0' || c > '9') return false; do { take(); c = peek(); } while (c >= '0' && c <= '9'); }
      return true;
    }
  } parser(stream, length);
  bool ok = parser.value(1, 0); parser.ws();
  ok = ok && parser.peek() == -1 && parser.left == 0 && parser.found && parser.count > 0;
  http.end();
  if (!ok) return;
  bool hasOther = false;
  for (int i = 0; i < parser.count; i++) { strcpy(btWaypoints[i], parser.names[i]); if (!strcmp(parser.names[i], "OTRO")) hasOther = true; }
  if (!hasOther) strcpy(btWaypoints[parser.count++], "OTRO");
  btWaypointCount = parser.count;
  btStatus[0] = 0;
}
bool btLoad(int id) {
  if (!fsOk) return false;
  char p[16]; btPath(p, sizeof p, id);
  File f = LittleFS.open(p, FS("r"));
  if (!f || f.size() > btx::MAX_FILE) { if (f) f.close(); return false; }
  size_t sz = f.size();
  if (f.read((uint8_t*)btData, sz) != sz) { f.close(); return false; }
  f.close(); btData[sz] = 0; btId = id;
  return btx::validWalk(btData);
}
bool btSave() { char p[16]; btPath(p, sizeof p, btId); return btId >= 0 && btWrite(p, btData); }
void btExport(int mode) {                            // 0 JSON, 1 CSV, 2 POST al relay
  if (!btLoad(btId)) { snprintf_P(btStatus, sizeof btStatus, PSTR("Error al leer")); return; }
  if (mode == 2) {
    if (WiFi.status() != WL_CONNECTED) { snprintf_P(btStatus, sizeof btStatus, PSTR("Sin red: pendiente")); return; }
    int n = btx::jsonExport(btData, btXbuf, sizeof btXbuf);
    if (n < 0) { snprintf_P(btStatus, sizeof btStatus, PSTR("Error al generar JSON")); return; }
    char url[48];
    snprintf_P(url, sizeof url, PSTR("http://%s/api/bitacora"), P.relay);
    WiFiClient client; HTTPClient http;
    http.setTimeout(3000);
    if (!http.begin(client, url)) { snprintf_P(btStatus, sizeof btStatus, PSTR("Relay no disponible")); return; }
    http.addHeader(FS("Content-Type"), FS("application/json"));
    int code = http.POST((uint8_t*)btXbuf, n);
    http.end();
    if (code >= 200 && code < 300) {
      btx::setSyncFlag(btData, true);
      if (btSave()) snprintf_P(btStatus, sizeof btStatus, PSTR("Sincronizada"));
      else snprintf_P(btStatus, sizeof btStatus, PSTR("Error al guardar sync"));
    } else snprintf_P(btStatus, sizeof btStatus, PSTR("Envio fallido: pendiente"));
  } else {
    int n = mode == 0 ? btx::jsonExport(btData, btXbuf, sizeof btXbuf) : btx::csvExport(btData, btXbuf, sizeof btXbuf);
    if (n < 0) { snprintf_P(btStatus, sizeof btStatus, PSTR("Error al exportar")); return; }
    char p[20];
    snprintf_P(p, sizeof p, PSTR("/b%d.%s"), btId, mode == 0 ? "json" : "csv");
    bool ok = btWrite(p, btXbuf);
    snprintf_P(btStatus, sizeof btStatus, PSTR("%s"), ok ? FS("Exportado a FILES") : FS("Error al exportar"));
  }
}
void btDraw() {
  tft.fillScreen(C_LCD);
  drawTitleBar(FS("Bitacora"), btAccent());
  drawDock(APP_BITACORA);
  char b[70];
  if (btPage == BT_LIST) {
    btScan(); if (btSel > btCount) btSel = btCount;
    if (btSel < btTop) btTop = btSel;
    if (btSel >= btTop + 7) btTop = btSel - 6;
    for (int r = 0; r < 7 && r + btTop <= btCount; r++) {
      int i = r + btTop;
      if (!i) snprintf_P(b, sizeof b, PSTR("%s"), !fsOk ? FS("Flash no montada") : btCount == BT_MAX ? FS("(lleno - borra una)") : FS("+ Nueva caminata"));
      else {
        char p[16]; btPath(p, sizeof p, btIds[i - 1]);
        char head[48] = "";
        File f = LittleFS.open(p, FS("r"));
        if (f) { size_t sz = f.size(); if (sz && sz < sizeof head) { if (f.read((uint8_t*)head, sz) == sz) { head[sz] = 0; char* nl = strchr(head, '\n'); if (nl) *nl = 0; } } f.close(); }
        char fecha[24] = "";
        if (strncmp(head, "fecha=", 6) == 0) snprintf_P(fecha, sizeof fecha, PSTR("%.10s"), head + 6);
        snprintf_P(b, sizeof b, PSTR("#%d %.10s  %s"), btIds[i - 1], fecha, btx::syncFlag(head) ? FS("OK") : FS("PEND"));
      }
      uint16_t bg = i == btSel ? btAccent() : E_WHITE;
      tft.fillRect(4, 26 + r * 22, 312, 21, bg);
      putText(10, 32 + r * 22, 1, i == btSel ? E_ONACC : C_INK, bg, b);
    }
  } else if (btPage == BT_ENTRIES) {
    int count = btx::entryCount(btData);
    if (btSel > count + 1) btSel = count + 1;
    if (btSel < btTop) btTop = btSel;
    if (btSel >= btTop + 7) btTop = btSel - 6;
    for (int r = 0; r < 7 && r + btTop <= count + 1; r++) {
      int i = r + btTop;
      if (!i) snprintf_P(b, sizeof b, PSTR("+ Nueva entrada"));
      else if (i == count + 1) snprintf_P(b, sizeof b, PSTR("Exportar / borrar / volver"));
      else {
        char line[btx::MAX_LINE], par[24], tip[16];
        btx::entryAt(btData, i - 1, line, sizeof line);
        btx::fieldOf(line, "parada", par, sizeof par);
        btx::fieldOf(line, "tipo", tip, sizeof tip);
        snprintf_P(b, sizeof b, PSTR("%.8s %.11s %.10s"), line, par, tip);
      }
      uint16_t bg = i == btSel ? btAccent() : E_WHITE;
      tft.fillRect(4, 26 + r * 22, 312, 21, bg);
      putText(10, 32 + r * 22, 1, i == btSel ? E_ONACC : C_INK, bg, b);
    }
  } else if (btPage == BT_PICK) {
    const char* labels[] = {FS("PARADA"), FS("TIPO"), FS("ACCION"), FS("ESTADO")};
    putText(8, 25, 1, C_INK, C_LCD, labels[btField % 4]);
    int n = btField == 0 ? btWaypointCount : 7;
    int top = btPickIdx < 5 ? 0 : btPickIdx - 5;
    for (int i = top; i < n && i < top + 7; i++) {
      const char* v = btField == 0 ? btWaypoints[i] : pgmItem(btField == 1 ? BT_TYPES_P : btField == 2 ? BT_ACTIONS_P : BT_STATES_P, i);
      uint16_t bg = i == btPickIdx ? btAccent() : E_WHITE;
      tft.fillRect(4, 38 + (i - top) * 20, 312, 19, bg);
      putText(10, 43 + (i - top) * 20, 1, i == btPickIdx ? E_ONACC : C_INK, bg, v);
    }
  }
  putText(6, 198, 1, C_MID, C_LCD, btStatus);
  oledDirty = true;
}
bool btManifestTried = false;                       // el GET del manifest es perezoso: solo cuando el
                                                    // usuario abre el picker de paradas (la UI nunca se
                                                    // bloquea por red al entrar a la app)
void bitacoraEnter() {
  if (wkOwner != APP_BITACORA) {
    wkOwner = APP_BITACORA; btPage = BT_LIST; btSel = btTop = 0; btStatus[0] = 0;
    if (!btWaypointCount) btFallback();             // catálogo local inmediato; la red se pide al usar el picker
  }
  if (btPage == BT_OTHER) { char saved[101]; snprintf_P(saved, sizeof saved, PSTR("%.100s"), askBuf); askBegin(FS("Otros (opcional, OK guarda)"), saved, 100, btAccent()); }
  else if (btPage == BT_MENU || btPage == BT_WALKS_MENU) {
    int page = btPage;
    btPage = page == BT_MENU ? BT_ENTRIES : BT_LIST; btDraw(); btPage = page;
    int selected = pmSel; pmDraw(FS("Caminata"), btAccent()); pmSel = selected; pmDrawItem(0); pmDrawItem(pmSel);
  } else btDraw();
}
void bitacoraLeave() { pmOpen = false; askOpen = false; btId = -1; btData[0] = 0; wkOwner = 0; }
bool bitacoraWantsBack() { return btPage != BT_LIST; }
void bitacoraBack() { btPage = BT_LIST; btSel = btTop = 0; btDraw(); }
void oledBitacora() {
  char a[24], b[24];
  snprintf_P(a, sizeof a, PSTR("%s"), btPage == BT_LIST ? FS("Caminatas") : btPage == BT_PICK ? FS("Seleccionar campo") : btPage == BT_OTHER ? FS("Otros") : FS("Entradas"));
  snprintf_P(b, sizeof b, PSTR("%.23s"), btStatus);
  oledCard(FS("BITACORA"), a, b, fsOk ? FS("Flash OK") : FS("Flash no montada"), FS("girar / pulsar"));
}
void bitacoraTick(uint32_t now) {
  (void)now;
  int c = takeClicks() * ENC_DIR;
  takePress();
  bool sp = takeShort();
  titleTimeTick();
  if (btPage == BT_MENU || btPage == BT_WALKS_MENU) {
    bool walks = btPage == BT_WALKS_MENU;
    int r = pmTick(c, sp);
    if (r < 0) return;
    if (walks && r == 0) { btPage = BT_ENTRIES; btSel = btTop = 0; btDraw(); return; }
    if (walks) { if (r == 5) { btPage = BT_LIST; btDraw(); return; } r--; }
    if (r < 3) btExport(r);
    else if (r == 3) {
      char p[16]; btPath(p, sizeof p, btId);
      if (fsOk && LittleFS.remove(p)) { btId = -1; btData[0] = 0; snprintf_P(btStatus, sizeof btStatus, PSTR("Caminata borrada")); btPage = BT_LIST; btSel = btTop = 0; btDraw(); return; }
      snprintf_P(btStatus, sizeof btStatus, PSTR("Error al borrar"));
    } else if (r == 4) { btPage = BT_LIST; btSel = btTop = 0; btDraw(); return; }
    btPage = walks ? BT_LIST : BT_ENTRIES; btDraw(); return;
  }
  if (btPage == BT_OTHER) {
    int r = askTick(c, sp);
    if (!r) return;
    if (r < 0) { snprintf_P(btStatus, sizeof btStatus, PSTR("Entrada cancelada")); btPage = BT_ENTRIES; btSel = btTop = 0; btDraw(); return; }
    char otros[101];
    for (int i = 0; askBuf[i]; i++) if (askBuf[i] == '|' || askBuf[i] == '=' || askBuf[i] == '\n' || askBuf[i] == '\r') askBuf[i] = ' ';
    snprintf_P(otros, sizeof otros, PSTR("%.100s"), askBuf);
    btx::Entry e{btTime, btValues[0], btValues[1], btValues[2], btValues[3], otros};
    int err = 0;
    bool ok = btx::appendEntry(btData, sizeof btData, &e, &err);
    if (ok) ok = btSave();
    if (!ok) { btLoad(btId); snprintf_P(btStatus, sizeof btStatus, PSTR("Error: entrada no guardada")); }
    else snprintf_P(btStatus, sizeof btStatus, PSTR("Guardada; pendiente"));
    btPage = BT_ENTRIES; btSel = btTop = 0; btDraw(); return;
  }
  int n = btPage == BT_LIST ? btCount + 1 : btPage == BT_ENTRIES ? btx::entryCount(btData) + 2 : (btField == 0 ? btWaypointCount : 7);
  int& sel = btPage == BT_PICK ? btPickIdx : btSel;
  if (c && n) { sel = ((sel + c) % n + n) % n; btDraw(); }
  if (!sp) return;
  if (btPage == BT_LIST) {
    if (!fsOk) { snprintf_P(btStatus, sizeof btStatus, PSTR("Flash no montada")); btDraw(); return; }
    if (!btSel) {
      if (btCount >= BT_MAX) return;
      int id = 0;
      for (; id < BT_MAX; id++) { char p[16]; btPath(p, sizeof p, id); if (!LittleFS.exists(p)) break; }
      char identity[24];
      if (id >= BT_MAX || !btNextIdentity(identity, sizeof identity)) { snprintf_P(btStatus, sizeof btStatus, PSTR("Error al reservar ID")); btDraw(); return; }
      char fecha[16]; int y2, m2, d2;
      civilFromDays((uint32_t)(clkEpoch / 86400UL), &y2, &m2, &d2);
      snprintf_P(fecha, sizeof fecha, PSTR("%04d-%02d-%02d"), y2, m2, d2);
      char titulo[40];
      snprintf_P(titulo, sizeof titulo, PSTR("Caminata %d"), id);
      char head[128];
      if (btx::headerBuild(fecha, titulo, identity, head, sizeof head) < 0) { snprintf_P(btStatus, sizeof btStatus, PSTR("Error al crear")); btDraw(); return; }
      btId = id;
      memcpy(btData, head, strlen(head) + 1);
      if (!btSave()) { snprintf_P(btStatus, sizeof btStatus, PSTR("Error al crear")); btDraw(); return; }
    } else {
      if (!btLoad(btIds[btSel - 1])) { snprintf_P(btStatus, sizeof btStatus, PSTR("Error al abrir")); btDraw(); return; }
      btPage = BT_WALKS_MENU;
      pmShowP(FS("Caminata"), PSTR("Abrir\nExportar JSON\nExportar CSV\nEnviar al relay\nBorrar caminata\nCancelar"), btAccent());
      oledDirty = true; return;
    }
    btPage = BT_ENTRIES; btSel = btTop = 0; btDraw(); return;
  }
  if (btPage == BT_ENTRIES) {
    if (!btSel) {
      int h, m, s; getHMS(&h, &m, &s);
      snprintf_P(btTime, sizeof btTime, PSTR("%02d:%02d:%02d"), h, m, s);
      btOther[0] = 0; btField = btPickIdx = 0;
      btPage = BT_PICK; btDraw();
    } else if (btSel == btx::entryCount(btData) + 1) {
      btPage = BT_MENU;
      pmShowP(FS("Caminata"), PSTR("Exportar JSON\nExportar CSV\nEnviar al relay\nBorrar caminata\nVolver a caminatas\nCancelar"), btAccent());
      oledDirty = true;
    }
    return;
  }
  if (btPage == BT_PICK) {
    if (btField == 0 && !btManifestTried) {         // primera elección de parada: 1 intento acotado (2.5 s)
      btManifestTried = true;
      btManifest();
      btDraw();
    }
    const char* v = btField == 0 ? btWaypoints[btPickIdx] : pgmItem(btField == 1 ? BT_TYPES_P : btField == 2 ? BT_ACTIONS_P : BT_STATES_P, btPickIdx);
    snprintf_P(btValues[btField], sizeof btValues[0], PSTR("%s"), v);
    btPickIdx = 0;
    if (++btField == 4) { btPage = BT_OTHER; askBegin(FS("Otros (opcional, OK guarda)"), "", 100, btAccent()); oledDirty = true; }
    else btDraw();
  }
}


#include "rover_app.h"

#endif
