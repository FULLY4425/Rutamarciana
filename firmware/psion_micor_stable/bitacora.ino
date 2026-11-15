// BITACORA: the flash file is authoritative; sync is explicitly retried.
const int BT_MAX = 12, BT_LIMIT = 3000, BT_WMAX = 16;
const char BT_STOPS[] PROGMEM = "START\nS1\nS2\nS3\nS4\nS5\nEND\nOTRO";
const char BT_TYPES[] PROGMEM = "ROCA\nSUELO\nVETA\nNODULO\nPOLVO\nIMAGEN\nOTRO";
const char BT_ACTIONS[] PROGMEM = "MUESTRA\nFOTO\nMEDICION\nSIN HALLAZGO";
const char BT_STATES[] PROGMEM = "NORMAL\nCONTRATIEMPO\nPELIGRO\nREPLANIFICACION";
enum { BT_LIST, BT_ENTRIES, BT_PICK, BT_OTHER, BT_MENU, BT_WALKS_MENU };
int btPage = BT_LIST, btSel = 0, btTop = 0, btId = -1, btCount = 0, btField = 0, btPick = 0;
int btIds[BT_MAX], btWaypointCount = 0;
char btWaypoints[BT_WMAX][24], btValues[4][24], btOther[101], btTime[9], btStatus[40] = "";
String btData;

void btPath(char *p, size_t n, int id) { snprintf_P(p, n, PSTR("/b%d.txt"), id); }
// rename without removing the destination: failure preserves the last good version.
bool btWrite(const char *path, const String &data) {
  if (!fsOk) return false;
  String tmp = String(path) + ".tmp";
  File f = LittleFS.open(tmp, "w");
  if (!f) return false;
  bool ok = f.write((const uint8_t*)data.c_str(), data.length()) == data.length();
  f.flush(); f.close();
  if (!ok || !LittleFS.rename(tmp, path)) { LittleFS.remove(tmp); return false; }
  return true;
}
// Reserve before creating the walk; gaps are safe, reused counters are not.
bool btNextIdentity(String &identity) {
  const char *path = "/btseq.txt"; uint32_t seq = 0;
  if (!fsOk) return false;
  if (LittleFS.exists(path)) {
    File f = LittleFS.open(path, "r");
    if (!f || f.size() == 0 || f.size() > 10) { if (f) f.close(); return false; }
    size_t size = f.size(); String value = f.readString(); f.close();
    if (value.length() != size) return false;
    for (unsigned i = 0; i < value.length(); ++i) {
      char c = value[i];
      if (c < '0' || c > '9' || seq > (UINT32_MAX - (c - '0')) / 10) return false;
      seq = seq * 10 + (c - '0');
    }
  }
  if (seq == UINT32_MAX) return false;
  ++seq;
  if (!btWrite(path, String((unsigned long)seq))) return false;
  char id[24]; snprintf_P(id, sizeof id, PSTR("1%010lu%010lu"), (unsigned long)ESP.getChipId(), (unsigned long)seq);
  identity = id; return true;
}
void btScan() {
  btCount = 0;
  for (int i = 0; fsOk && i < BT_MAX; ++i) { char p[20]; btPath(p, sizeof p, i); if (LittleFS.exists(p)) btIds[btCount++] = i; }
}
void btFallback() {
  btWaypointCount = 8;
  for (int i = 0; i < btWaypointCount; ++i) snprintf_P(btWaypoints[i], sizeof btWaypoints[i], PSTR("%s"), pgmItem(BT_STOPS, i));
}
// Parse the complete response before publishing any of its waypoint names.
void btManifest() {
  btWaypointCount = 1; strcpy(btWaypoints[0], "OTRO");
  strcpy(btStatus, "Error manifest: solo OTRO");
  if (WiFi.status() != WL_CONNECTED) { btFallback(); strcpy(btStatus, "Sin red: lista offline"); return; }
  WiFiClient client; HTTPClient http; http.setTimeout(2500);
  if (!http.begin(client, "http://10.42.0.1:8080/api/manifest")) { btFallback(); strcpy(btStatus, "Sin red: lista offline"); return; }
  int code = http.GET();
  if (code != 200) { http.end(); if (code < 0 || code == 404) { btFallback(); strcpy(btStatus, "Sin red: lista offline"); } return; }
  int length = http.getSize();
  // shortcut: require a bounded Content-Length, upgrade for chunked responses.
  if (length <= 0 || length > 2097152) { http.end(); return; }
  WiFiClient *stream = http.getStreamPtr(); stream->setTimeout(2500);
  struct Parser {
    WiFiClient *s; int left, ch, count; bool found; char names[BT_WMAX][24];
    Parser(WiFiClient *stream, int n) : s(stream), left(n), ch(-2), count(0), found(false) {}
    int peek() {
      if (ch != -2) return ch;
      if (!left) return ch = -1;
      uint32_t start = millis();
      while (!s->available()) { if (millis() - start >= 2500) return ch = -1; delay(1); }
      int c = s->read(); if (c >= 0) --left; return ch = c;
    }
    int take() { int c = peek(); ch = -2; return c; }
    void ws() { while (peek() == ' ' || peek() == '\n' || peek() == '\r' || peek() == '\t') take(); }
    bool str(char *out, int cap, bool &overflow) {
      if (take() != '"') return false;
      int n = 0; overflow = false;
      for (;;) {
        int c = take(); if (c < 32) return false;
        if (c == '"') { if (out) out[n] = 0; return true; }
        if (c == '\\') {
          c = take();
          if (c == 'u') { c = 0; for (int i = 0; i < 4; ++i) { int h = take(); int v = h >= '0' && h <= '9' ? h-'0' : h >= 'a' && h <= 'f' ? h-'a'+10 : h >= 'A' && h <= 'F' ? h-'A'+10 : -1; if (v < 0) return false; c = c * 16 + v; } if (c > 127) c = 128; }
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
          char key[32]; bool over; if (!str(key, sizeof key, over)) return false;
          ws(); if (take() != ':') return false; ws();
          if (context == 1 && !over && !strcmp(key, "waypoints")) {
            if (found) return false;
            found = true; if (!value(2, depth + 1)) return false;
          } else if (context == 3 && !over && !strcmp(key, "name")) {
            if (named || count >= BT_WMAX - 1 || !str(names[count], sizeof names[0], over) || over || !names[count][0]) return false;
            for (int i = 0; names[count][i]; ++i) { unsigned char x = names[count][i]; if (x < 32 || x > 126 || x == '|' || x == '=' || x == '\\') return false; }
            for (int i = 0; i < count; ++i) if (!strcmp(names[i], names[count])) return false;
            ++count; named = true;
          } else if (!value(0, depth + 1)) return false;
          ws(); c = take(); if (c == '}') return context != 3 || named; if (c != ',') return false; ws();
        }
      }
      if (c == '[') {
        take(); ws(); if (peek() == ']') { take(); return context != 2; }
        for (;;) { if (!value(context == 2 ? 3 : 0, depth + 1)) return false; ws(); c = take(); if (c == ']') return true; if (c != ',') return false; }
      }
      if (c == '"') { bool over; return str(nullptr, 0, over); }
      if (c == 't' || c == 'f' || c == 'n') { const char *p = c == 't' ? "true" : c == 'f' ? "false" : "null"; while (*p) if (take() != *p++) return false; return true; }
      if (c == '-') { take(); c = peek(); }
      if (c == '0') take(); else { if (c < '1' || c > '9') return false; do { take(); c = peek(); } while (c >= '0' && c <= '9'); }
      if (peek() == '.') { take(); c = peek(); if (c < '0' || c > '9') return false; do { take(); c = peek(); } while (c >= '0' && c <= '9'); }
      c = peek(); if (c == 'e' || c == 'E') { take(); c = peek(); if (c == '+' || c == '-') take(); c = peek(); if (c < '0' || c > '9') return false; do { take(); c = peek(); } while (c >= '0' && c <= '9'); }
      return true;
    }
  } parser(stream, length);
  bool ok = parser.value(1, 0); parser.ws(); ok = ok && parser.peek() == -1 && parser.left == 0 && parser.found && parser.count > 0;
  http.end(); if (!ok) return;
  bool hasOther = false;
  for (int i = 0; i < parser.count; ++i) { strcpy(btWaypoints[i], parser.names[i]); if (!strcmp(parser.names[i], "OTRO")) hasOther = true; }
  if (!hasOther) strcpy(btWaypoints[parser.count++], "OTRO");
  btWaypointCount = parser.count; btStatus[0] = 0;
}
int btBodyStart() { int p = -1; for (int i = 0; i < 3; ++i) { p = btData.indexOf('\n', p + 1); if (p < 0) return 0; } ++p; if (btData.substring(p, p + 3) == "id=") { p = btData.indexOf('\n', p); if (p < 0) return 0; ++p; } return p; }
String btIdentity() {
  int p = -1; for (int i = 0; i < 3; ++i) { p = btData.indexOf('\n', p + 1); if (p < 0) return ""; }
  ++p;
  if (btData.substring(p, p + 3) != "id=") return String(btId); // legacy relay identity
  int end = btData.indexOf('\n', p); if (end <= p + 3) return "";
  String id = btData.substring(p + 3, end);
  if (id.length() != 21 || id[0] != '1') return "";
  for (unsigned i = 0; i < id.length(); ++i) if (id[i] < '0' || id[i] > '9') return "";
  return id;
}
int btEntries() { int count = 0, pos = btBodyStart(); if (!pos) return 0; for (unsigned i = pos; i < btData.length(); ++i) if (btData[i] == '\n') ++count; return count; }
bool btLoad(int id) {
  if (!fsOk) return false;
  char p[20]; btPath(p, sizeof p, id); File f = LittleFS.open(p, "r");
  if (!f || f.size() > BT_LIMIT) { if (f) f.close(); return false; }
  size_t size = f.size(); btData = f.readString(); f.close(); btId = id;
  return btData.length() == size && btData.startsWith("fecha=") && btData.indexOf("\nsync=") >= 0 && btBodyStart() && btIdentity().length();
}
bool btSave() { char p[20]; btPath(p, sizeof p, btId); return btId >= 0 && btData.length() <= BT_LIMIT && btWrite(p, btData); }
void btSyncFlag(bool synced) { int p = btData.indexOf("\nsync="); if (p >= 0 && p + 6 < (int)btData.length()) btData.setCharAt(p + 6, synced ? '1' : '0'); }
String btQuote(const String &s) {
  String out = "\"";
  for (unsigned i = 0; i < s.length(); ++i) { char c = s[i]; if (c == '"' || c == '\\') out += '\\'; if ((uint8_t)c < 32) out += ' '; else out += c; }
  return out + '"';
}
String btFieldValue(const String &line, const char *key) {
  String marker = String("|") + key + "="; int p = line.indexOf(marker); if (p < 0) return "";
  p += marker.length(); int e = line.indexOf('|', p); return line.substring(p, e < 0 ? line.length() : e);
}
String btJson() {
  int e = btData.indexOf('\n'); String date = btData.substring(6, e);
  String out = String("{\"id\":") + btQuote(btIdentity()) + ",\"fecha\":" + btQuote(date) + ",\"entradas\":[";
  int p = btBodyStart(); bool first = true;
  while (p > 0 && p < (int)btData.length()) {
    int end = btData.indexOf('\n', p); if (end < 0) break;
    String line = btData.substring(p, end); if (!first) out += ','; first = false;
    out += String("{\"hora\":") + btQuote(line.substring(0, 8));
    const char *keys[] = {"parada", "tipo", "accion", "estado", "otros"};
    for (int i = 0; i < 5; ++i) out += String(",\"") + keys[i] + "\":" + btQuote(btFieldValue(line, keys[i]));
    out += '}'; p = end + 1;
  }
  return out + "]}";
}
String btCsvCell(const String &s) { String out = "\""; for (unsigned i = 0; i < s.length(); ++i) { if (s[i] == '"') out += '"'; out += s[i]; } return out + '"'; }
String btCsv() {
  String out = "hora,parada,tipo,accion,estado,otros\r\n";
  int p = btBodyStart();
  while (p > 0 && p < (int)btData.length()) { int end = btData.indexOf('\n', p); if (end < 0) break;
    String line = btData.substring(p, end); out += btCsvCell(line.substring(0, 8));
    const char *keys[] = {"parada", "tipo", "accion", "estado", "otros"};
    for (int i = 0; i < 5; ++i) out += String(",") + btCsvCell(btFieldValue(line, keys[i]));
    out += "\r\n"; p = end + 1;
  } return out;
}
void btExport(int mode) {
  if (!btLoad(btId)) { strcpy(btStatus, "Error al leer"); return; }
  if (mode == 2) {
    if (WiFi.status() != WL_CONNECTED) { strcpy(btStatus, "Sin red: pendiente"); return; }
    WiFiClient client; HTTPClient http; http.setTimeout(3000);
    if (!http.begin(client, "http://10.42.0.1:8080/api/bitacora")) { strcpy(btStatus, "Relay no disponible"); return; }
    http.addHeader("Content-Type", "application/json"); int code = http.POST(btJson()); http.end();
    if (code >= 200 && code < 300) { btSyncFlag(true); if (btSave()) strcpy(btStatus, "Sincronizada"); else strcpy(btStatus, "Error al guardar sync"); }
    else strcpy(btStatus, "Envio fallido: pendiente");
  } else {
    char p[24]; snprintf_P(p, sizeof p, PSTR("/b%d.%s"), btId, mode == 0 ? "json" : "csv");
    bool ok = btWrite(p, mode == 0 ? btJson() : btCsv()); strcpy(btStatus, ok ? "Exportado a FILES" : "Error al exportar");
  }
}
void btDraw() {
  tft.fillScreen(C_LCD); drawTitleBar(FS("Bitacora"), appAccent(APP_BITACORA)); drawDock(APP_BITACORA);
  char b[70];
  if (btPage == BT_LIST) {
    btScan(); if (btSel > btCount) btSel = btCount;
    if (btSel < btTop) btTop = btSel; if (btSel >= btTop + 7) btTop = btSel - 6;
    for (int r = 0; r < 7 && r + btTop <= btCount; ++r) { int i = r + btTop;
      if (!i) snprintf_P(b, sizeof b, PSTR("%s"), !fsOk ? "Flash no montada" : btCount == BT_MAX ? "(lleno - borra una)" : "+ Nueva caminata");
      else { char p[20]; btPath(p, sizeof p, btIds[i-1]); File f = LittleFS.open(p, "r"); String date = f ? f.readStringUntil('\n') : String(""); String sync = f ? f.readStringUntil('\n') : String(""); if (f) f.close(); snprintf_P(b, sizeof b, PSTR("#%d %.22s  %s"), btIds[i-1], date.c_str() + (date.startsWith("fecha=") ? 6 : 0), sync == "sync=1" ? "OK" : "PEND"); }
      uint16_t bg = i == btSel ? appAccent(APP_BITACORA) : E_WHITE; tft.fillRect(4, 26 + r*22, 312, 21, bg); putText(10, 32 + r*22, 1, i == btSel ? E_ONACC : C_INK, bg, b);
    }
  } else if (btPage == BT_ENTRIES) {
    int count = btEntries(); if (btSel > count + 1) btSel = count + 1;
    if (btSel < btTop) btTop = btSel; if (btSel >= btTop + 7) btTop = btSel - 6;
    int pos = btBodyStart();
    for (int r = 0; r < 7 && r + btTop <= count + 1; ++r) { int i = r + btTop;
      if (!i) strcpy(b, "+ Nueva entrada"); else if (i == count + 1) strcpy(b, "Exportar / borrar / volver");
      else { int p = pos; for (int j = 1; j < i && p > 0; ++j) p = btData.indexOf('\n', p) + 1; int e = btData.indexOf('\n', p); String line = btData.substring(p, e); snprintf_P(b, sizeof b, PSTR("%.8s %.18s %.12s"), line.c_str(), btFieldValue(line, "parada").c_str(), btFieldValue(line, "tipo").c_str()); }
      uint16_t bg = i == btSel ? appAccent(APP_BITACORA) : E_WHITE; tft.fillRect(4, 26 + r*22, 312, 21, bg); putText(10, 32+r*22, 1, i == btSel ? E_ONACC : C_INK, bg, b);
    }
  } else if (btPage == BT_PICK) {
    const char *labels[] = {"PARADA", "TIPO", "ACCION", "ESTADO"}; putText(8, 25, 1, C_INK, C_LCD, labels[btField]);
    int n = btField == 0 ? btWaypointCount : btField == 1 ? 7 : 4; int top = max(0, btPick - 5);
    for (int i = top; i < n && i < top + 7; ++i) { const char *v = btField == 0 ? btWaypoints[i] : pgmItem(btField == 1 ? BT_TYPES : btField == 2 ? BT_ACTIONS : BT_STATES, i); uint16_t bg = i == btPick ? appAccent(APP_BITACORA) : E_WHITE; tft.fillRect(4, 38+(i-top)*20, 312, 19, bg); putText(10, 43+(i-top)*20, 1, i == btPick ? E_ONACC : C_INK, bg, v); }
  }
  putText(6, 198, 1, C_MID, C_LCD, btStatus); oledDirty = true;
}
void bitacoraEnter() {
  if (wkOwner != APP_BITACORA) { wkOwner = APP_BITACORA; btPage = BT_LIST; btSel = btTop = 0; btStatus[0] = 0; btManifest(); }
  if (btPage == BT_OTHER) { char saved[101]; snprintf_P(saved, sizeof saved, PSTR("%.100s"), askBuf); askBegin(FS("Otros (opcional, OK para guardar)"), saved, 100, appAccent(APP_BITACORA)); }
  else if (btPage == BT_MENU || btPage == BT_WALKS_MENU) { int page = btPage; btPage = page == BT_MENU ? BT_ENTRIES : BT_LIST; btDraw(); btPage = page; int selected = pmSel; pmDraw(FS("Caminata"), appAccent(APP_BITACORA)); pmSel = selected; pmDrawItem(0); pmDrawItem(pmSel); }
  else btDraw();
}
void bitacoraLeave() { pmOpen = false; askOpen = false; btData = ""; wkOwner = 0; }
void oledBitacora() { char a[24], b[24]; snprintf_P(a, sizeof a, PSTR("%s"), btPage == BT_LIST ? "Caminatas" : btPage == BT_PICK ? "Seleccionar campo" : btPage == BT_OTHER ? "Otros" : "Entradas"); snprintf_P(b, sizeof b, PSTR("%.23s"), btStatus); oledCard(FS("BITACORA"), a, b, fsOk ? "Flash OK" : "Flash no montada", "girar / pulsar"); }
void bitacoraTick(uint32_t now) {
  int c = takeClicks() * ENC_DIR; takePress(); bool sp = takeShort(); titleTimeTick(); (void)now;
  if (btPage == BT_MENU || btPage == BT_WALKS_MENU) { bool walks = btPage == BT_WALKS_MENU; int r = pmTick(c, sp); if (r < 0) return;
    if (walks && r == 0) { btPage = BT_ENTRIES; btSel = btTop = 0; btDraw(); return; }
    if (walks) { if (r == 5) { btPage = BT_LIST; btDraw(); return; } --r; }
    if (r < 3) btExport(r); else if (r == 3) { char p[20]; btPath(p, sizeof p, btId); if (fsOk && LittleFS.remove(p)) { btData = ""; strcpy(btStatus, "Caminata borrada"); btPage = BT_LIST; btSel = btTop = 0; btDraw(); return; } strcpy(btStatus, "Error al borrar"); }
    else if (r == 4) { btPage = BT_LIST; btSel = btTop = 0; btDraw(); return; }
    btPage = walks ? BT_LIST : BT_ENTRIES; btDraw(); return;
  }
  if (btPage == BT_OTHER) { int r = askTick(c, sp); if (!r) return; if (r < 0) { strcpy(btStatus, "Entrada cancelada"); btPage = BT_ENTRIES; btSel = btTop = 0; btDraw(); return; } if (r > 0) { for (int i = 0; askBuf[i]; ++i) if (askBuf[i] == '|' || askBuf[i] == '=' || askBuf[i] == '\n' || askBuf[i] == '\r') askBuf[i] = ' '; snprintf_P(btOther, sizeof btOther, PSTR("%.100s"), askBuf); }
    String line = String(btTime) + "|parada=" + btValues[0] + "|tipo=" + btValues[1] + "|accion=" + btValues[2] + "|estado=" + btValues[3] + "|otros=" + btOther + "\n";
    size_t expected = strlen(btTime) + 8 + strlen(btValues[0]) + 6 + strlen(btValues[1]) + 8 + strlen(btValues[2]) + 8 + strlen(btValues[3]) + 7 + strlen(btOther) + 1;
    bool ok = line.length() == expected && btData.length() + expected <= BT_LIMIT && btData.concat(line);
    if (ok) { btSyncFlag(false); ok = btSave(); }
    if (!ok) { btLoad(btId); strcpy(btStatus, "Error: entrada no guardada"); } else strcpy(btStatus, "Guardada; pendiente"); btPage = BT_ENTRIES; btSel = btTop = 0; btDraw(); return;
  }
  int n = btPage == BT_LIST ? btCount + 1 : btPage == BT_ENTRIES ? btEntries() + 2 : btField == 0 ? btWaypointCount : btField == 1 ? 7 : 4;
  int &sel = btPage == BT_PICK ? btPick : btSel; if (c && n) { sel = ((sel + c) % n + n) % n; btDraw(); }
  if (!sp) return;
  if (btPage == BT_LIST) {
    if (!fsOk) { strcpy(btStatus, "Flash no montada"); btDraw(); return; }
    if (!btSel) { if (btCount >= BT_MAX) return; int id = 0; for (; id < BT_MAX; ++id) { char p[20]; btPath(p, sizeof p, id); if (!LittleFS.exists(p)) break; }
      String identity; if (id >= BT_MAX || !btNextIdentity(identity)) { strcpy(btStatus, "Error al reservar ID"); btDraw(); return; }
      btId = id; int y, m, d; civilFromDays(clkEpoch / 86400UL, &y, &m, &d); char h[60]; snprintf_P(h, sizeof h, PSTR("fecha=%04d-%02d-%02d\nsync=0\ntitulo=Caminata %d\n"), y, m, d, id); btData = String(h) + "id=" + identity + "\n"; if (!btSave()) { strcpy(btStatus, "Error al crear"); btDraw(); return; }
    } else {
      if (!btLoad(btIds[btSel-1])) { strcpy(btStatus, "Error al abrir"); btDraw(); return; }
      btPage = BT_WALKS_MENU; pmShowP(FS("Caminata"), PSTR("Abrir\nExportar JSON\nExportar CSV\nEnviar al relay\nBorrar caminata\nCancelar"), appAccent(APP_BITACORA)); oledDirty = true; return;
    }
    btPage = BT_ENTRIES; btSel = btTop = 0; btDraw(); return;
  }
  if (btPage == BT_ENTRIES) {
    if (!btSel) { int h,m,s; getHMS(&h,&m,&s); snprintf_P(btTime, sizeof btTime, PSTR("%02d:%02d:%02d"), h,m,s); btOther[0] = 0; btField = btPick = 0; btPage = BT_PICK; btDraw(); }
    else if (btSel == btEntries() + 1) { btPage = BT_MENU; pmShowP(FS("Caminata"), PSTR("Exportar JSON\nExportar CSV\nEnviar al relay\nBorrar caminata\nVolver a caminatas\nCancelar"), appAccent(APP_BITACORA)); oledDirty = true; }
    return;
  }
  if (btPage == BT_PICK) {
    const char *v = btField == 0 ? btWaypoints[btPick] : pgmItem(btField == 1 ? BT_TYPES : btField == 2 ? BT_ACTIONS : BT_STATES, btPick);
    snprintf_P(btValues[btField], sizeof btValues[0], PSTR("%s"), v); btPick = 0;
    if (++btField == 4) { btPage = BT_OTHER; askBegin(FS("Otros (opcional, OK para guardar)"), "", 100, appAccent(APP_BITACORA)); oledDirty = true; } else btDraw();
  }
}
