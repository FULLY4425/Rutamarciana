#pragma once
#include "pov_render.h"
// ================= SPACE APPS: ROVER (POV del rover en Jezero) =================
// Módulo 3: "¿qué voy a encontrar?". La ventana izquierda muestra la ortofoto
// real de HiRISE (tiles de LittleFS, fase 2) con la ruta encima y el marcador
// del rover; el panel derecho da telemetría de la simulación de pov_core
// (fase 1). La ruta viene del mapa si registró povc::setPovProvider; si no,
// DEMO determinista SIEMPRE etiquetada como tal (nunca como dato del mapa).
// Sin red, sin String, sin escrituras: la app solo lee. Redibujo incremental
// (solo lo que cambió) y animación a <=10 fps.
enum { RV_POV, RV_CARD };                              // páginas: POV y TARJETA de llegada
const int  RV_WIN_W = 200, RV_WIN_H = 160;             // ventana de cámara (px de pantalla)
const int  RV_WIN_Y = TITLE_H;                         // justo bajo la barra de título
const int  RV_SCRUB_M = 20;                            // metros por click del knob (scrubbing)
const int  RV_SNAP_M = 12;                             // snap del scrub a parada (12 < 20: se puede salir)
const long RV_TX0_M = 4587066, RV_TY0_M = 1096874;     // transform del activo (x en u=0, y en v=0)
const uint16_t RV_NO_TILE = 0x8410;                    // gris cuando faltan los tiles
pov::Route rvRoute;                                    // ruta activa (mapa o demo)
pov::Sim   rvSim;
float rvDwell[pov::MAX_WPT];                           // dwell por parada (del presupuesto, si lo publicó)
int   rvStops[pov::MAX_WPT], rvStopN = 0, rvSel = 0;   // índices de las paradas (waypoints)
int   rvPage = RV_POV, rvCx = -99999, rvCy = -99999, rvMx = -99999, rvMy = -99999;
int   rvLastAt = -1;
uint32_t rvPanelSig = 0xFFFFFFFFu, rvLastDraw = 0, rvLastTick = 0, rvBannerUntil = 0;
bool  rvFresh = true, rvNoTiles = false, rvBannerOn = false;
char  rvNote[48] = "", rvBanner[40] = "";
uint16_t rvSeg[64];                                    // un segmento de fila de tile (128 B)

// Restrict this module's text to its panel; leave other apps' helpers untouched.
void rvText(int x,int y,int size,uint16_t fg,uint16_t bg,const char* text){
 const int right=rvPage==RV_POV&&rvRoute.valid&&x<RV_WIN_W?RV_WIN_W:W-4;
 if(x<0||y<TITLE_H||y+8*size>DOCK_Y||size<1)return;
 int count=(right-x)/(6*size);if(count<1)return;if(count>52)count=52;
 char line[53];const size_t length=strlen(text);size_t take=length<(size_t)count?length:(size_t)count;
 memcpy(line,text,take);line[take]=0;if(length>take&&take>=3)memcpy(line+take-3,"...",3);
 putText(x,y,size,fg,bg,line);
}
void rvWrap(const char* text,int x,int y,int columns,int rows,uint16_t fg,uint16_t bg){
 if(columns>50)columns=50;
 for(int row=0;row<rows&&*text;row++){
  while(*text==' ')text++;
  size_t length=strlen(text),take=length<(size_t)columns?length:(size_t)columns;
  if(length>take&&text[take]!=' '){size_t cut=take;while(cut&&text[cut]!=' ')cut--;if(cut)take=cut;}
  char line[51];memcpy(line,text,take);line[take]=0;rvText(x,y+row*10,1,fg,bg,line);text+=take;
 }
}
void rvLine(int x0,int y0,int x1,int y1,uint16_t color,const pov::Cam& cam){
 y0-=RV_WIN_Y;y1-=RV_WIN_Y;
 if(pov_render::clipLine(x0,y0,x1,y1,cam.w,cam.h))tft.drawLine(x0,y0+RV_WIN_Y,x1,y1+RV_WIN_Y,color);
}

uint16_t rvAccent() { return appAccent(APP_ROVER); }

// ---- tiles de la ortofoto (data/pov, fase 2): RGB565 LE, rejilla 8x8 ----
// Llena dst[0..w-1] con la fila v del mapa, columnas u0..u0+w-1 (u,v en 0..499,
// w <= 64). Sin tiles disponibles pinta gris y deja constancia en rvNoTiles.
bool rvRowBlit(int u0, int v, int w, uint16_t* dst) {
  if(!dst||u0<0||v<0||v>=pov::MAP_PX||w<1||w>64||u0+w>pov::MAP_PX)return false;
  bool ok = fsOk;
  int c = pov::tileCol(u0), done = 0, ox = u0 - pov::TILE_PX * c;
  while (done < w) {
    int take = pov::tileW(c) - ox;
    if (take > w - done) take = w - done;
    if (ok) {
      char p[18];
      snprintf_P(p, sizeof p, PSTR("/pov/r%02dc%02d.bin"), pov::tileRow(v), c);
      File f = LittleFS.open(p, FS("r"));
      if (f && f.seek((uint32_t)pov::tileByteOff(u0 + done, v)) &&
          f.read((uint8_t*)(dst + done), (size_t)(take * 2)) == (size_t)(take * 2)) { /* fila buena */ }
      else ok = false;
      if (f) f.close();
    }
    if (!ok) for (int i = done; i < done + take; i++) dst[i] = RV_NO_TILE;
    done += take; c++; ox = 0;
  }
  if (!ok) rvNoTiles = true;
  return ok;
}

// Empuja una fila de píxeles nativos a la TFT (espejo al visor PC + swap +
// SPI): el mismo camino que usa el visor de imágenes de Books.
void rvPush(int x, int y, int w, uint16_t* px) {
  if (rcLive) { rcWindow(x, y, w, 1); rcPixels(px, w); }
  for (int i = 0; i < w; i++) px[i] = (uint16_t)((px[i] >> 8) | (px[i] << 8));
  tft.startWrite();
  tft.Adafruit_ST7789::setAddrWindow(x, y, w, 1);
  rawPixels(px, w);
  tft.endWrite();
}

// Blit completo de la ventana de cámara, tile-major: un open por tile y lecturas
// secuenciales por fila (la ventana toca <=5x4 tiles; shortcut: si algún día hay
// que acelerar, el rect de cada tile es contiguo y admite una sola lectura).
void rvBlitWindow(const pov::Cam& cam) {
  for (int r = pov::tileRow(cam.y); r <= pov::tileRow(cam.y + cam.h - 1); r++) {
    for (int c = pov::tileCol(cam.x); c <= pov::tileCol(cam.x + cam.w - 1); c++) {
      int tx = pov::TILE_PX * c - cam.x;                        // esquina del tile en pantalla
      int ty = pov::TILE_PX * r - cam.y + RV_WIN_Y;
      int tw = pov::tileW(c), th = pov::tileH(r);
      int x0 = tx > 0 ? tx : 0, x1 = tx + tw < cam.w ? tx + tw : cam.w;
      int y0 = ty > RV_WIN_Y ? ty : RV_WIN_Y, y1 = ty + th < RV_WIN_Y + cam.h ? ty + th : RV_WIN_Y + cam.h;
      if (x1 <= x0 || y1 <= y0) continue;
      int take = x1 - x0;
      char p[18];
      snprintf_P(p, sizeof p, PSTR("/pov/r%02dc%02d.bin"), r, c);
      File f = fsOk ? LittleFS.open(p, FS("r")) : File();
      bool fok = (bool)f;
      if (!fok && fsOk) rvNoTiles = true;
      for (int sy = y0; sy < y1; sy++) {
        if (fok) fok = f.seek((uint32_t)(((sy-ty)*tw+(x0-tx))*2)) &&
                       f.read((uint8_t*)rvSeg, (size_t)(take*2)) == (size_t)(take*2);
        if (!fok) { for (int i = 0; i < take; i++) rvSeg[i] = RV_NO_TILE; rvNoTiles = true; }
        rvPush(x0, sy, take, rvSeg);
      }
      if (f) f.close();
    }
  }
}

// Blit de un rect pequeño de la ventana (para restaurar el marcador viejo).
void rvBlitRect(int x0, int y0, int w, int h, const pov::Cam& cam) {
  if (x0 < 0) { w += x0; x0 = 0; }
  if (y0 < RV_WIN_Y) { h += y0 - RV_WIN_Y; y0 = RV_WIN_Y; }
  if (x0 + w > RV_WIN_W) w = RV_WIN_W - x0;
  if (y0 + h > RV_WIN_Y + RV_WIN_H) h = RV_WIN_Y + RV_WIN_H - y0;
  if (w <= 0 || h <= 0 || w > 64) return;
  for (int sy = y0; sy < y0 + h; sy++) {
    rvRowBlit(cam.x + x0, cam.y + (sy - RV_WIN_Y), w, rvSeg);
    rvPush(x0, sy, w, rvSeg);
  }
}

// Overlay de la ruta: trazo de 2 px del accent + waypoints (círculo 4 px y
// etiqueta S1..Sn). Idempotente: redibujarlo entero repone lo borrado.
void rvDrawOverlay(const pov::Cam& cam) {
  uint16_t acc = rvAccent();
  for (int i = 0; i + 1 < rvRoute.n; i++) {
    int x1 = (int)rvRoute.u[i] - cam.x, y1 = (int)rvRoute.v[i] - cam.y + RV_WIN_Y;
    int x2 = (int)rvRoute.u[i + 1] - cam.x, y2 = (int)rvRoute.v[i + 1] - cam.y + RV_WIN_Y;
    if ((x1 < -24 && x2 < -24) || (x1 > cam.w + 24 && x2 > cam.w + 24) ||
        (y1 < RV_WIN_Y - 24 && y2 < RV_WIN_Y - 24) || (y1 > RV_WIN_Y + cam.h + 24 && y2 > RV_WIN_Y + cam.h + 24)) continue;
    rvLine(x1,y1,x2,y2,acc,cam);
    rvLine(x1,y1+1,x2,y2+1,acc,cam);
  }
  for (int i = 0; i < rvRoute.n; i++) {
    int x = (int)rvRoute.u[i] - cam.x, y = (int)rvRoute.v[i] - cam.y + RV_WIN_Y;
    if (x<4||x+4>=cam.w||y<RV_WIN_Y+4||y+4>=RV_WIN_Y+cam.h)continue;
    tft.drawCircle(x, y, 4, acc);
    tft.fillCircle(x, y, 2, acc);
    if (rvRoute.stop[i] && y >= RV_WIN_Y + 12 &&
        x + 12 + (int)strlen(rvRoute.name[i]) * 6 <= RV_WIN_W)
      rvText(x + 7, y - 4, 1, 0xFFFF, 0x0000, rvRoute.name[i]);   // etiqueta legible sobre la foto
  }
}

// Marcador del rover: triángulo blanco con borde negro orientado al heading.
void rvDrawMarker(const pov::Cam& cam) {
  int x = (int)rvSim.u - cam.x, y = (int)rvSim.v - cam.y + RV_WIN_Y;
  float hr = rvSim.heading_deg * 0.01745329f;
  float dx = sinf(hr), dy = -cosf(hr);                   // heading 0 = norte = arriba
  int tx = x + (int)lroundf(dx * 7), ty = y + (int)lroundf(dy * 7);
  int b1x = x + (int)lroundf(-dx * 4 - dy * 4), b1y = y + (int)lroundf(-dy * 4 + dx * 4);
  int b2x = x + (int)lroundf(-dx * 4 + dy * 4), b2y = y + (int)lroundf(-dy * 4 - dx * 4);
  tx=pov_render::bound(tx,0,cam.w-1);b1x=pov_render::bound(b1x,0,cam.w-1);b2x=pov_render::bound(b2x,0,cam.w-1);
  ty=pov_render::bound(ty,RV_WIN_Y,RV_WIN_Y+cam.h-1);b1y=pov_render::bound(b1y,RV_WIN_Y,RV_WIN_Y+cam.h-1);b2y=pov_render::bound(b2y,RV_WIN_Y,RV_WIN_Y+cam.h-1);
  tft.fillTriangle(tx, ty, b1x, b1y, b2x, b2y, 0xFFFF);
  tft.drawTriangle(tx, ty, b1x, b1y, b2x, b2y, 0x0000);
}

void rvFmtHMS(float s, char* b, size_t n) {             // h:mm:ss (o m:ss si < 1 h)
  if (!(s > 0.0f)) s = 0.0f;                            // NaN/negativo -> 0
  if(s>1e9f)s=1e9f;
  int t = (int)(s + 0.5f);
  if (t >= 3600) snprintf_P(b, n, PSTR("%d:%02d:%02d"), t / 3600, (t / 60) % 60, t % 60);
  else snprintf_P(b, n, PSTR("%d:%02d"), t / 60, t % 60);
}

// Firma compacta de la telemetría: si no cambia, no se redibuja el panel.
uint32_t rvSig() {
  pov::Next n = pov::nextStop(&rvRoute, rvSim.s_m);
  uint32_t s = (uint32_t)((int)(rvSim.s_m * 10.0f) & 0xFFFF);
  s |= (uint32_t)(n.valid ? n.idx + 1 : 0) << 16;
  s |= (uint32_t)(rvSim.moving ? 1 : 0) << 20;
  s |= (uint32_t)(rvSim.done ? 1 : 0) << 21;
  s |= (uint32_t)rvSim.scale << 22;
  return s;
}

void rvDrawPanel() {                                    // telemetría (panel derecho, 112 px)
  const int PX = RV_WIN_W + 4, PW = W - PX - 4;
  tft.fillRect(PX - 2, TITLE_H, PW + 2, DOCK_Y - TITLE_H, C_LCD);
  tft.drawFastVLine(PX - 2, TITLE_H, DOCK_Y - TITLE_H, E_RULE);
  char b[26];
  rvText(PX, 23, 1, C_MID, C_LCD, FS("SIMULACION"));
  if (rvRoute.total_m >= 1000.0f)
    snprintf_P(b, sizeof b, PSTR("dist %.2f/%.2f km"), rvSim.s_m / 1000.0f, rvRoute.total_m / 1000.0f);
  else snprintf_P(b, sizeof b, PSTR("dist %d/%d m"), (int)(rvSim.s_m + 0.5f), (int)(rvRoute.total_m + 0.5f));
  rvText(PX, 38, 1, C_INK, C_LCD, b);
  pov::Next n = pov::nextStop(&rvRoute, rvSim.s_m);
  if (n.valid) { char e[14]; rvFmtHMS(n.eta_s, e, sizeof e); snprintf_P(b, sizeof b, PSTR("eta %s a %s"), e, rvRoute.name[n.idx]); }
  else snprintf_P(b, sizeof b, PSTR("%s"), FS("eta -- (fin)"));
  rvText(PX, 52, 1, C_INK, C_LCD, b);
  snprintf_P(b, sizeof b, PSTR("vel %.2f m/s"), rvRoute.speed_ms);
  rvText(PX, 66, 1, C_INK, C_LCD, b);
  snprintf_P(b, sizeof b, PSTR("escala x%d"), rvSim.scale);
  rvText(PX, 80, 1, C_INK, C_LCD, b);
  rvText(PX, 94, 1, rvSim.done ? E_CORAL : rvSim.moving ? E_MINT : E_AMBER, C_LCD,
          rvSim.done ? FS("FIN DE RUTA") : rvSim.moving ? FS("EN MARCHA") : FS("PAUSA"));
  rvText(PX, 112, 1, C_MID, C_LCD, rvNote);
  if (rvRoute.source == pov::SRC_DEMO) rvText(PX, 126, 1, E_AMBER, C_LCD, FS("RUTA DEMO"));
  if (rvNoTiles) rvText(PX, 140, 1, E_AMBER, C_LCD, FS("ortofoto ausente"));
  rvText(PX, 168, 1, C_MID, C_LCD, FS("gira: recorre ruta"));
  rvText(PX, 182, 1, C_MID, C_LCD, FS("pulsa: pausa/play"));
  rvText(PX, 196, 1, C_MID, C_LCD, FS("en parada: tarjeta"));
}

void rvDrawStrip(bool ban) {                            // franja bajo la ventana: banner o ayuda + progreso
  const int SY = RV_WIN_Y + RV_WIN_H;
  if (ban) {
    tft.fillRect(0, SY, RV_WIN_W, 14, 0x0000);
    rvText(4, SY + 3, 1, rvAccent(), 0x0000, rvBanner);
  } else {
    tft.fillRect(0, SY, RV_WIN_W, 14, C_LCD);
    rvText(4, SY + 3, 1, C_MID, C_LCD, FS("gira: ruta  pulsa: pausa"));
  }
  const int BY = SY + 18;
  tft.fillRect(0, BY, RV_WIN_W, 8, C_LCD);
  tft.drawRect(0, BY, RV_WIN_W, 8, E_RULE);
  int fw = rvRoute.total_m > 0.01f ? (int)((float)RV_WIN_W * rvSim.s_m / rvRoute.total_m) : 0;
  if (fw > RV_WIN_W - 2) fw = RV_WIN_W - 2;
  if (fw > 0) tft.fillRect(1, BY + 2, fw, 4, rvAccent());
  for (int i = 0; i < rvRoute.n; i++) {
    if (!rvRoute.stop[i] || rvRoute.total_m <= 0.01f) continue;
    int x = (int)((float)RV_WIN_W * rvRoute.cum_m[i] / rvRoute.total_m);
    if (x > 0 && x < RV_WIN_W - 1) tft.drawFastVLine(x, BY, 8, C_MID);
  }
}

void rvBannerAt(int k, uint32_t now) {                  // llegada a una parada: banner breve
  char d[14];
  if (rvDwell[k] >= 90.0f) snprintf_P(d, sizeof d, PSTR("%d min"), (int)(rvDwell[k] / 60.0f + 0.5f));
  else if (rvDwell[k] > 0.5f) snprintf_P(d, sizeof d, PSTR("%d s"), (int)(rvDwell[k] + 0.5f));
  else snprintf_P(d, sizeof d, PSTR("%s"), FS("--"));
  snprintf_P(rvBanner, sizeof rvBanner, PSTR(">> %s  dwell %s"), rvRoute.name[k], d);
  rvBannerUntil = now + 2500;
}

int rvAtStop() {                                        // última parada ya alcanzada (-1 = ninguna)
  int k = -1;
  for (int i = 0; i < rvRoute.n; i++)
    if (rvRoute.stop[i] && rvRoute.cum_m[i] <= rvSim.s_m + 0.001f) k = i;
  return k;
}

void rvDrawErr() {                                      // mapa malformado: error honesto, sin demo silenciosa
  tft.fillRect(0, TITLE_H, W, DOCK_Y - TITLE_H, C_LCD);
  rvText(8, 44, 2, E_CORAL, C_LCD, FS("Ruta invalida"));
  rvText(8, 72, 1, C_INK, C_LCD, rvNote);
  rvWrap(FS("El mapa entrego una geometria que no pasa la validacion de pov_core. Sin datos del mapa no hay simulacion: no se inventa una ruta."), 8, 92, 50, 5, C_MID, C_LCD);
  rvText(8, 160, 1, C_MID, C_LCD, FS("pulsa para reintentar"));
  rvText(4, 186, 1, C_MID, C_LCD, FS("NASA/JPL-Caltech/University of Arizona/USGS"));
  rvText(4, 198, 1, C_MID, C_LCD, FS("HiRISE Jezero TRN mosaic, Fergason et al. 2020"));
  oledDirty = true;
}

// Dibujo incremental: ventana completa solo si la cámara se movió; si solo se
// movió el marcador, se restaura su caja vieja y se repone el overlay.
void rvDrawPov(bool force) {
  if (!rvRoute.valid) { rvDrawErr(); return; }
  pov::Cam cam = pov::camera(rvSim.u, rvSim.v, RV_WIN_W, RV_WIN_H);
  int mx = (int)rvSim.u - cam.x, my = (int)rvSim.v - cam.y + RV_WIN_Y;
  bool full = force || cam.x != rvCx || cam.y != rvCy;
  bool moved = mx != rvMx || my != rvMy;
  uint32_t sig = rvSig();
  bool ban = millis() < rvBannerUntil;
  if (!full && !moved && sig == rvPanelSig && ban == rvBannerOn) return;   // nada cambió
  if (full) {
    rvCx = cam.x; rvCy = cam.y;
    rvBlitWindow(cam);
    rvDrawOverlay(cam);
    rvDrawMarker(cam);
  } else if (moved) {
    rvBlitRect(rvMx - 9, rvMy - 9, 18, 18, cam);
    rvDrawOverlay(cam);
    rvDrawMarker(cam);
  }
  rvMx = mx; rvMy = my;
  if (full || sig != rvPanelSig) rvDrawPanel();
  if (full || sig != rvPanelSig || ban != rvBannerOn) rvDrawStrip(ban);
  rvPanelSig = sig;
  rvBannerOn = ban;
  oledDirty = true;
}

void rvDrawCard() {                                     // TARJETA de llegada (por parada)
  tft.fillRect(0, TITLE_H, W, DOCK_Y - TITLE_H, C_LCD);
  if (!rvRoute.valid) { rvDrawErr(); return; }
  char b[48];
  if (!rvStopN) {
    rvText(8, 44, 2, C_INK, C_LCD, FS("Sin paradas"));
    rvText(8, 72, 1, C_MID, C_LCD, FS("La ruta no tiene paradas cientificas."));
  } else {
    int k = rvStops[rvSel];
    rvText(8, 24, 2, rvAccent(), C_LCD, rvRoute.name[k]);
    snprintf_P(b, sizeof b, PSTR("parada %d de %d  %s"), rvSel + 1, rvStopN, rvRoute.variant == 1 ? FS("SAFE") : FS("FAST"));
    rvText(8, 44, 1, C_MID, C_LCD, b);
    if (rvRoute.source == pov::SRC_DEMO) rvText(8, 58, 1, E_AMBER, C_LCD, FS("RUTA DEMO"));
    rvText(8, 76, 1, C_MID, C_LCD, FS("POSICION"));
    snprintf_P(b, sizeof b, PSTR("u %d  v %d px"), (int)rvRoute.u[k], (int)rvRoute.v[k]);
    rvText(8, 88, 1, C_INK, C_LCD, b);
    snprintf_P(b, sizeof b, PSTR("x %ld m   y %ld m"),
               RV_TX0_M + (long)(rvRoute.u[k] * 4.0f + 0.5f), RV_TY0_M - (long)(rvRoute.v[k] * 4.0f + 0.5f));
    rvText(8, 100, 1, C_INK, C_LCD, b);
    rvText(8, 118, 1, C_MID, C_LCD, FS("LLEGADA"));
    char t[14]; rvFmtHMS(rvRoute.t_s[k] - rvRoute.t_s[0], t, sizeof t);
    snprintf_P(b, sizeof b, PSTR("t+ %s"), t);
    rvText(8, 130, 1, C_INK, C_LCD, b);
    if (rvDwell[k] > 0.5f) { char d[14]; rvFmtHMS(rvDwell[k], d, sizeof d); snprintf_P(b, sizeof b, PSTR("dwell %s"), d); }
    else snprintf_P(b, sizeof b, PSTR("%s"), FS("dwell --"));
    rvText(8, 142, 1, C_INK, C_LCD, b);
    snprintf_P(b, sizeof b, PSTR("desde %s: %d m"), rvRoute.name[k - 1], (int)(rvRoute.cum_m[k] - rvRoute.cum_m[k - 1] + 0.5f));
    rvText(8, 154, 1, C_INK, C_LCD, b);
    snprintf_P(b, sizeof b, PSTR("gira: parada  pulsa: escala x%d"), rvSim.scale);
    rvText(8, 170, 1, C_MID, C_LCD, b);
  }
  rvText(4, 186, 1, C_MID, C_LCD, FS("NASA/JPL-Caltech/University of Arizona/USGS"));
  rvText(4, 198, 1, C_MID, C_LCD, FS("HiRISE Jezero TRN mosaic, Fergason et al. 2020"));
  oledDirty = true;
}

void roverLoad() {                                      // perezoso y SIN red: mapa si conectó, demo si no
  rvPage = RV_POV; rvSel = 0; rvStopN = 0; rvNoTiles = false;
  rvCx = rvCy = rvMx = rvMy = -99999;
  rvPanelSig = 0xFFFFFFFFu; rvBannerUntil = 0; rvBannerOn = false;
  bool povcIn = povc::povProvider != nullptr;
  int variant=MC.route==mision::SAFE?mision::SAFE:mision::FAST;
  int e = pov::loadRoute(&rvRoute, variant);                  // variante FAST (provisional, como mision)
  mision::RouteInput ri{};
  bool hasDwell = mision::provide(&ri, variant);              // dwell: del presupuesto, si el mapa lo publicó
  for (int i = 0; i < pov::MAX_WPT; i++) { rvDwell[i] = 0.0f; rvStops[i] = 0; }
  if (rvRoute.valid) {
    for (int i = 0; i < rvRoute.n; i++) {
      if (!rvRoute.stop[i]) continue;
      rvStops[rvStopN++] = i;
      int si = i - 1;                                   // wpt i (S_i) <-> ri.dwell_s[i-1]
      if (hasDwell && si >= 0 && si < mision::MAX_STOPS) rvDwell[i] = ri.dwell_s[si];
    }
    snprintf_P(rvNote, sizeof rvNote, PSTR("%s"),
               rvRoute.source == pov::SRC_MAP ? FS("mapa: conectado")
               : povcIn ? FS("mapa: pendiente") : FS("mapa: demo"));
  } else {
    snprintf_P(rvNote, sizeof rvNote, PSTR("mapa: invalido (%d)"), e);
  }
  pov::simInit(&rvSim, &rvRoute);
  rvSim.scale = 100;                                    // x1 es la escala real (0.04 m/s): invisible en vivo
  rvLastAt = rvAtStop();
}

void roverEnter() {
  wkOwner = APP_ROVER;
  if (rvFresh) { rvFresh = false; roverLoad(); }
  tft.fillScreen(C_LCD);
  drawTitleBar(FS("Rover"), rvAccent());
  drawDock(APP_ROVER);
  if (rvPage == RV_CARD) rvDrawCard(); else rvDrawPov(true);
  rvLastDraw = rvLastTick = millis();
}
void roverLeave() { pmOpen = false; askOpen = false; wkOwner = 0; rvFresh = true; }
bool roverWantsBack() { return rvPage == RV_CARD; }
void roverBack() { rvPage = RV_POV; roverEnter(); }     // tarjeta -> POV (mismo estado, sin recargar)

void oledRover() {
  char a[24], b[24], c2[24];
  if (!rvRoute.valid) {
    snprintf_P(a, sizeof a, PSTR("%s"), FS("ruta invalida"));
    snprintf_P(b, sizeof b, PSTR("%s"), rvNote);
    snprintf_P(c2, sizeof c2, PSTR("%s"), FS("pulsa reintentar"));
  } else if (rvPage == RV_CARD) {
    snprintf_P(a, sizeof a, PSTR("%s"), rvStopN ? FS("tarjeta de llegada") : FS("sin paradas"));
    snprintf_P(b, sizeof b, PSTR("gira parada  escala x%d"), rvSim.scale);
    snprintf_P(c2, sizeof c2, PSTR("%.23s"), rvNote);
  } else {
    pov::Next n = pov::nextStop(&rvRoute, rvSim.s_m);
    snprintf_P(a, sizeof a, PSTR("%s"), rvSim.done ? FS("fin de ruta") : rvSim.moving ? FS("en marcha") : FS("pausa"));
    if (n.valid) { char e[14]; rvFmtHMS(n.eta_s, e, sizeof e); snprintf_P(b, sizeof b, PSTR("a %s eta %s"), rvRoute.name[n.idx], e); }
    else snprintf_P(b, sizeof b, PSTR("%s"), FS("sin parada proxima"));
    snprintf_P(c2, sizeof c2, PSTR("%.23s"), rvNote);
  }
  oledCard(FS("ROVER"), a, b, c2, FS("girar / pulsar"));
}

void roverTick(uint32_t now) {
  int c = takeClicks() * ENC_DIR;
  takePress();
  bool sp = takeShort();
  titleTimeTick();
  if (!rvRoute.valid) {                                 // mapa malformado: reintentar bajo demanda
    if (sp) { roverLoad(); if (rvPage == RV_CARD) rvDrawCard(); else rvDrawPov(true); }
    return;
  }
  if (rvPage == RV_CARD) {
    if (c && rvStopN) { rvSel = ((rvSel + c) % rvStopN + rvStopN) % rvStopN; rvDrawCard(); }
    if (sp) {                                           // escala de tiempo x1/x10/x100 (honestidad de sim)
      rvSim.scale = rvSim.scale == 1 ? 10 : rvSim.scale == 10 ? 100 : 1;
      rvDrawCard();
    }
    return;
  }
  if (now - rvLastTick > 1000) rvLastTick = now;         // tras una pausa larga: sin saltos
  float dt = (float)(now - rvLastTick) * 0.001f;
  rvLastTick = now;
  if (c) {                                               // scrubbing manual: toma el control (pausa)
    rvSim.moving = false;
    pov::simSeek(&rvSim, &rvRoute, rvSim.s_m + (float)c * RV_SCRUB_M);
    int at = rvAtStop();                                // snap: cruzar una parada la deja exacta
    pov::Next nx = pov::nextStop(&rvRoute, rvSim.s_m);
    if (at >= 0 && rvSim.s_m - rvRoute.cum_m[at] < RV_SNAP_M)
      pov::simSeek(&rvSim, &rvRoute, rvRoute.cum_m[at]);
    else if (nx.valid && nx.dist_m < RV_SNAP_M)
      pov::simSeek(&rvSim, &rvRoute, rvRoute.cum_m[nx.idx]);
  }
  if (sp) {
    int at = rvAtStop();
    if (at >= 0 && rvSim.s_m - rvRoute.cum_m[at] < 1.0f) {   // parado en una parada: su tarjeta
      for (int i = 0; i < rvStopN; i++) if (rvStops[i] == at) { rvSel = i; break; }
      rvPage = RV_CARD;
      rvDrawCard();
      return;
    }
    if (rvSim.done) { pov::simSeek(&rvSim, &rvRoute, 0.0f); rvSim.moving = true; }   // al final: reinicia
    else rvSim.moving = !rvSim.moving;
  }
  pov::simTick(&rvSim, &rvRoute, dt);
  int at = rvAtStop();
  if (at > rvLastAt && at >= 0) rvBannerAt(at, now);    // llegó a una parada (avanzando)
  rvLastAt = at;
  if (now - rvLastDraw >= 100) { rvLastDraw = now; rvDrawPov(false); }   // <= 10 fps
}


