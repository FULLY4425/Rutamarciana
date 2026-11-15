// =====================================================================
// pov_core.h — núcleo PURO del POV del rover (módulo 3, Space Apps)
// Sin dependencias de Arduino: mismo código en el ESP32 y en pruebas de
// host (g++). Toda la GEOMETRÍA y la SIMULACIÓN vive aquí; la pantalla
// vive en spaceapps_apps.h (fase 3) y el pipeline de tiles aparte (fase 2).
//
// UNIDADES (del activo assets/jezero_ortho_4m.json, 500x500 px @ 4 m/px):
//   u,v     px sobre la ortofoto (0..499); v crece hacia el SUR
//   s_m     metros recorridos sobre la polilínea (px * 4)
//   t_s     segundos de la línea de tiempo (t_s del proveedor o demo)
//   heading grados brújula: 0 = norte (v decrece), 90 = este (u crece)
//
// FUENTES DE RUTA (honestidad de la casa):
//   povc::provide() con valid=true  ->  ruta del mapa (source = SRC_MAP)
//   cualquier otro caso             ->  ruta DEMO determinista (SRC_DEMO)
//                                       que la UI marca "DEMO", nunca como
//                                       datos del mapa.
// La simulación avanza por LONGITUD DE ARCO a speed_ms (escala de tiempo
// x1/x10/x100 para que la demo sea visible); el reloj t_s se interpola de
// la línea de tiempo del proveedor (dwell incluido en los t_s de llegada).
// =====================================================================
#ifndef POV_CORE_H
#define POV_CORE_H

#include "pov_contract.h"                 // povc:: + mision:: (tiempos)

#include <cmath>
#include <cstdint>
#include <cstdio>

namespace pov {

const int   MAX_WPT       = povc::MAX_WPT; // START + paradas + END (= 10)
const int   MAP_PX        = 500;           // ortofoto 500x500 px
const float M_PER_PX      = 4.0f;          // 4 m/px (jezero_ortho_4m.json)
const float DEMO_SPEED_MS = 0.04f;         // rover típico 0.02-0.05 m/s

enum Error {
  OK = 0,
  ERR_NULL = 1,                            // puntero nulo
  ERR_INVALID = 2,                         // PovInput.valid == false
  ERR_NWPT = 3,                            // n fuera de 2..MAX_WPT
  ERR_RANGE = 4,                           // u,v fuera de 0..499 px
  ERR_NONFINITE = 5,                       // NaN/Inf en t_s o speed
  ERR_ORDER = 6,                           // t_s decreciente (llegadas)
  ERR_SPEED = 7                            // speed_ms <= 0
};
enum Source { SRC_NONE = 0, SRC_MAP = 1, SRC_DEMO = 2 };

inline bool fin(float v) {                 // finito y no NaN (como eva::fin)
  return v == v && v < 1e30f && v > -1e30f;
}

struct Route {
  // Copia plana y geometría precalculada de un povc::PovInput.
  int   n;
  int   variant;
  float u[MAX_WPT], v[MAX_WPT];            // px
  float t_s[MAX_WPT];                      // tiempo acumulado de llegada
  char  name[MAX_WPT][16];
  bool  stop[MAX_WPT];                     // true = parada científica (no START/END)
  float seg_m[MAX_WPT - 1];                // longitud de cada tramo (m)
  float seg_heading[MAX_WPT - 1];          // heading de cada tramo (deg)
  float cum_m[MAX_WPT];                    // distancia acumulada (m), cum_m[0]=0
  float total_m;                           // longitud total (m)
  float total_s;                           // duración de la línea de tiempo (s)
  float speed_ms;                          // velocidad nominal (m/s)
  int   source;                            // Source: SRC_NONE/MAP/DEMO
  bool  valid;
  int   error;                             // Error (0 = OK)
};

// ---- validar y precalcular; el proveedor del mapa es frontera de
// ---- confianza: datos ausentes/inválidos NUNCA producen números.
inline int build(const povc::PovInput* in, Route* r) {
  if (!in || !r) return ERR_NULL;
  Route z{};
  *r = z;                                  // sin campos heredados de otra build
  r->error = OK;
  if (!in->valid)                                    { r->error = ERR_INVALID;    return ERR_INVALID; }
  if (in->n < 2 || in->n > povc::MAX_WPT)            { r->error = ERR_NWPT;       return ERR_NWPT; }
  if (!fin(in->speed_ms))                            { r->error = ERR_NONFINITE;  return ERR_NONFINITE; }
  if (in->speed_ms <= 0.0f)                          { r->error = ERR_SPEED;      return ERR_SPEED; }
  for (int i = 0; i < in->n; i++) {
    if (in->wpt[i].u < 0 || in->wpt[i].u > MAP_PX - 1 ||
        in->wpt[i].v < 0 || in->wpt[i].v > MAP_PX - 1) { r->error = ERR_RANGE;   return ERR_RANGE; }
    if (!fin(in->wpt[i].t_s))                        { r->error = ERR_NONFINITE;  return ERR_NONFINITE; }
    if (i > 0 && in->wpt[i].t_s < in->wpt[i - 1].t_s) { r->error = ERR_ORDER;    return ERR_ORDER; }
  }
  r->n = in->n;
  r->variant = in->variant;
  r->speed_ms = in->speed_ms;
  r->total_s = in->wpt[in->n - 1].t_s - in->wpt[0].t_s;
  for (int i = 0; i < r->n; i++) {
    r->u[i] = (float)in->wpt[i].u;
    r->v[i] = (float)in->wpt[i].v;
    r->t_s[i] = in->wpt[i].t_s;
    r->stop[i] = (i > 0 && i < r->n - 1);  // START/END nunca son paradas
    for (int k = 0; k < 16; k++) {
      r->name[i][k] = in->wpt[i].name[k];
      if (!r->name[i][k]) break;
    }
    r->name[i][15] = 0;
  }
  r->cum_m[0] = 0.0f;
  float h = 0.0f;
  for (int i = 0; i + 1 < r->n; i++) {
    float du = r->u[i + 1] - r->u[i], dv = r->v[i + 1] - r->v[i];
    float px = sqrtf(du * du + dv * dv);
    if (px > 0.0001f) {                    // tramo nulo: hereda el heading anterior (o 0)
      h = atan2f(du, -dv) * 57.2957795f;   // 0 = norte, horario
      if (h < 0) h += 360.0f;
    }
    r->seg_heading[i] = h;
    r->seg_m[i] = px * M_PER_PX;
    r->cum_m[i + 1] = r->cum_m[i] + r->seg_m[i];
  }
  r->total_m = r->cum_m[r->n - 1];
  r->valid = true;
  return OK;
}

// ---- tramo de la polilínea que contiene s_m (YA clamped a 0..total_m) ----
inline int segOf(const Route* r, float s_m) {
  int i = 0;
  while (i + 1 < r->n && r->cum_m[i + 1] <= s_m) i++;
  return (i + 1 < r->n) ? i : r->n - 2;    // al final: último tramo
}

// ---- posición (u,v px) y heading a una distancia recorrida (m) ----
inline bool posAtM(const Route* r, float s_m, float* u, float* v, float* heading_deg) {
  if (!r || !r->valid || !u || !v) return false;
  if (s_m < 0.0f) s_m = 0.0f;
  if (s_m > r->total_m) s_m = r->total_m;
  int seg = segOf(r, s_m);
  if (seg < 0) seg = 0;
  float a = 0.0f;
  if (r->seg_m[seg] > 0.0001f) a = (s_m - r->cum_m[seg]) / r->seg_m[seg];
  if (a < 0.0f) a = 0.0f;
  if (a > 1.0f) a = 1.0f;
  *u = r->u[seg] + (r->u[seg + 1] - r->u[seg]) * a;
  *v = r->v[seg] + (r->v[seg + 1] - r->v[seg]) * a;
  if (heading_deg) *heading_deg = r->seg_heading[seg];
  return true;
}

// ---- reloj de la línea de tiempo a una distancia recorrida (m);
// ---- interpolación lineal del t_s del proveedor dentro del tramo
inline float tAtM(const Route* r, float s_m) {
  if (!r || !r->valid) return 0.0f;
  if (s_m < 0.0f) s_m = 0.0f;
  if (s_m > r->total_m) s_m = r->total_m;
  int seg = segOf(r, s_m);
  if (seg < 0) seg = 0;
  float a = 0.0f;
  if (r->seg_m[seg] > 0.0001f) a = (s_m - r->cum_m[seg]) / r->seg_m[seg];
  if (a < 0.0f) a = 0.0f;
  if (a > 1.0f) a = 1.0f;
  return r->t_s[seg] + (r->t_s[seg + 1] - r->t_s[seg]) * a;
}

// ---- estado de la simulación (avanza por longitud de arco) ----
struct Sim {
  float u, v;                              // px (float)
  float heading_deg;                       // 0..360 (0 = norte)
  float s_m;                               // distancia recorrida (m)
  float t_s;                               // reloj de la ruta (interpolado)
  int   scale;                             // escala de tiempo: 1, 10, 100
  bool  moving;                            // pausa/play
  bool  done;                              // llegó al final de la ruta
};

// ---- salto a una distancia recorrida (scrubbing con el knob) ----
inline void simSeek(Sim* s, const Route* r, float s_m) {
  if (!s || !r || !r->valid) return;
  if (s_m < 0.0f) s_m = 0.0f;
  if (s_m > r->total_m) s_m = r->total_m;
  s->s_m = s_m;
  s->done = s_m >= r->total_m - 0.0001f;
  posAtM(r, s_m, &s->u, &s->v, &s->heading_deg);
  s->t_s = tAtM(r, s_m);
}

inline void simInit(Sim* s, const Route* r) {
  if (!s) return;
  Sim z{};
  *s = z;
  s->scale = 1;
  s->moving = true;
  if (r && r->valid) simSeek(s, r, 0.0f);
}

// ---- un tick de animación: dt en segundos REALES; avanza speed*scale*dt ----
inline void simTick(Sim* s, const Route* r, float dt_real_s) {
  if (!s || !r || !r->valid || !s->moving || s->done) return;
  if (!(dt_real_s > 0.0f) || !fin(dt_real_s)) return;
  simSeek(s, r, s->s_m + r->speed_ms * (float)s->scale * dt_real_s);
}

// ---- ventana de cámara w×h centrada en (u,v), clamped a 500×500 ----
struct Cam { int x, y, w, h; };

inline Cam camera(float u, float v, int w, int h) {
  Cam c{};
  if (!(u == u)) u = (float)MAP_PX / 2.0f;      // NaN -> centro (defensivo)
  if (!(v == v)) v = (float)MAP_PX / 2.0f;
  if (u < 0.0f) u = 0.0f;
  if (u > (float)MAP_PX) u = (float)MAP_PX;
  if (v < 0.0f) v = 0.0f;
  if (v > (float)MAP_PX) v = (float)MAP_PX;
  if (w < 1) w = 1;
  if (h < 1) h = 1;
  if (w > MAP_PX) w = MAP_PX;
  if (h > MAP_PX) h = MAP_PX;
  int x = (int)u - w / 2, y = (int)v - h / 2;
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x + w > MAP_PX) x = MAP_PX - w;
  if (y + h > MAP_PX) y = MAP_PX - h;
  c.x = x; c.y = y; c.w = w; c.h = h;
  return c;
}

// ---- próxima parada desde s_m ----
struct Next {
  int   idx;                               // waypoint de la parada (-1 = ninguna)
  float dist_m;                            // metros restantes por la polilínea
  float eta_s;                             // ETA a velocidad NOMINAL (s de misión, sin escala)
  bool  valid;
};

inline Next nextStop(const Route* r, float s_m) {
  Next n{};
  n.idx = -1;
  if (!r || !r->valid) return n;
  if (s_m < 0.0f) s_m = 0.0f;
  for (int i = 0; i < r->n; i++) {
    if (r->stop[i] && r->cum_m[i] > s_m + 0.0001f) {
      n.idx = i;
      n.dist_m = r->cum_m[i] - s_m;
      n.eta_s = r->speed_ms > 0.0f ? n.dist_m / r->speed_ms : 0.0f;
      n.valid = true;
      return n;
    }
  }
  return n;                                // sin próxima parada (fin o sin paradas)
}

// ---- LCG diminuto y determinista para la poligonal demo (seed FIJA) ----
inline uint32_t demoStep(uint32_t& s) {
  s = s * 1664525u + 1013904223u;
  return (s >> 16) & 0x3FFu;               // 0..1023
}

// ---- ruta DEMO determinista (fallback honesto, marcada SRC_DEMO) ----
// Poligonal sintética sobre la ortofoto (posiciones NUNCA del mapa) que
// respeta los tiempos de mision::RouteInput cuando el mapa los publicó:
// la ida (START..S_n) se reparte proporcional a la longitud, la vuelta
// (S_n..END) lleva travel_back_s y cada dwell se añade tras llegar a su
// parada. Sin proveedor de tiempos: reloj = longitud / DEMO_SPEED_MS.
inline int demoRoute(povc::PovInput* out, int variant) {
  if (!out) return ERR_NULL;
  out->valid = false;
  out->variant = variant;
  out->n = 0;
  out->speed_ms = DEMO_SPEED_MS;

  // 1) tiempos del presupuesto si el mapa los publicó (y son sanos)
  mision::RouteInput ri{};
  bool haveTimes = mision::provide(&ri, variant);
  if (haveTimes && (!fin(ri.travel_out_s) || ri.travel_out_s < 0.0f ||
                    !fin(ri.travel_back_s) || ri.travel_back_s < 0.0f)) haveTimes = false;
  for (int i = 0; haveTimes && i < ri.n_stops && i < mision::MAX_STOPS; i++)
    if (!fin(ri.dwell_s[i]) || ri.dwell_s[i] < 0.0f) haveTimes = false;

  int ns = haveTimes ? ri.n_stops : 3;     // 3 paradas por defecto
  if (ns < 0) ns = 0;
  if (ns > mision::MAX_STOPS) ns = mision::MAX_STOPS;
  const int n = ns + 2;

  // 2) poligonal sintética determinista (nunca del mapa)
  const int MARGIN = 60, SPAN = 380;       // px: paradas en 60..440, lejos del borde
  uint32_t seed = 0x4A455A33u;             // "JEZ3" — seed fija
  out->wpt[0].u = 80;  out->wpt[0].v = 420;  out->wpt[0].t_s = 0.0f;   // base
  snprintf(out->wpt[0].name, sizeof out->wpt[0].name, "START");
  for (int i = 1; i <= ns; i++) {
    int u = MARGIN + (int)(demoStep(seed) % (uint32_t)SPAN);
    int v = MARGIN + (int)(demoStep(seed) % (uint32_t)SPAN);
    if ((u > out->wpt[i - 1].u ? u - out->wpt[i - 1].u : out->wpt[i - 1].u - u) +
        (v > out->wpt[i - 1].v ? v - out->wpt[i - 1].v : out->wpt[i - 1].v - v) < 40) {
      u += 41;                             // separación mínima entre waypoints
      if (u > MARGIN + SPAN) u -= SPAN + 1;
    }
    out->wpt[i].u = (int16_t)u;
    out->wpt[i].v = (int16_t)v;
    out->wpt[i].t_s = 0.0f;
    snprintf(out->wpt[i].name, sizeof out->wpt[i].name, "S%d", i);
  }
  if (ns == 0) {                           // caminata sin paradas: diagonal fija
    out->wpt[n - 1].u = 420;               // shortcut: sin paradas no hay vuelta a
    out->wpt[n - 1].v = 80;                // cerrar; el reloj lleva ida+vuelta
  } else {
    out->wpt[n - 1].u = 80;                // la vuelta cierra en la base
    out->wpt[n - 1].v = 420;
  }
  out->wpt[n - 1].t_s = 0.0f;
  snprintf(out->wpt[n - 1].name, sizeof out->wpt[n - 1].name, "END");
  out->n = n;

  // 3) tiempos acumulados de llegada (t_s de cada waypoint)
  float segm[povc::MAX_WPT - 1];
  float sumOut = 0.0f;                     // metros de ida (START..S_ns)
  for (int i = 0; i + 1 < n; i++) {
    float du = (float)(out->wpt[i + 1].u - out->wpt[i].u);
    float dv = (float)(out->wpt[i + 1].v - out->wpt[i].v);
    segm[i] = sqrtf(du * du + dv * dv) * M_PER_PX;
    if (i < ns) sumOut += segm[i];
  }
  float t = 0.0f;
  for (int i = 0; i + 1 < n; i++) {
    float leg;
    if (!haveTimes)     leg = segm[i] / DEMO_SPEED_MS;
    else if (ns == 0)   leg = ri.travel_out_s + ri.travel_back_s;
    else if (i < ns)    leg = sumOut > 0.001f
                            ? ri.travel_out_s * segm[i] / sumOut
                            : ri.travel_out_s / (float)ns;
    else                leg = ri.travel_back_s;               // S_ns -> END
    t += leg;
    out->wpt[i + 1].t_s = t;
    if (haveTimes && i < ns) t += ri.dwell_s[i];              // dwell EN la parada i+1
  }
  out->valid = true;
  return OK;
}

// ---- una sola llamada para la UI: mapa si conectó, demo si no ----
inline int loadRoute(Route* r, int variant) {
  if (!r) return ERR_NULL;
  povc::PovInput in{};
  bool fromMap = povc::provide(&in, variant);
  if (!fromMap) demoRoute(&in, variant);
  int e = build(&in, r);
  if (e == OK && r->valid) r->source = fromMap ? SRC_MAP : SRC_DEMO;
  return e;                                // mapa malformado: error honesto, sin demo silenciosa
}

// ---- tiles de la fase 2 (LittleFS /pov/): geometría del mapeo (u,v) -> tile ----
// Rejilla 8x8 de tiles de 64 px; la última fila/columna cubre el borde real de
// 52 px (7*64 + 52 = 500). El I/O de LittleFS vive en la UI; solo la matemática
// del mapeo es de aquí (testeable en host).
const int TILE_PX   = 64;                         // lado del tile completo
const int TILE_GRID = 8;                          // rejilla 8x8
const int TILE_EDGE = 52;                         // borde real de la última fila/col
inline int tileW(int c) { return c == TILE_GRID - 1 ? TILE_EDGE : TILE_PX; }
inline int tileH(int r) { return r == TILE_GRID - 1 ? TILE_EDGE : TILE_PX; }
inline int tileCol(int u) {
  int c = u / TILE_PX;
  return c > TILE_GRID - 1 ? TILE_GRID - 1 : c < 0 ? 0 : c;   // clamp defensivo
}
inline int tileRow(int v) {
  int r = v / TILE_PX;
  return r > TILE_GRID - 1 ? TILE_GRID - 1 : r < 0 ? 0 : r;
}
inline long tileByteOff(int u, int v) {           // offset en BYTES de (u,v) dentro de SU tile
  return (long)((v % TILE_PX) * tileW(tileCol(u)) + (u % TILE_PX)) * 2;
}

} // namespace pov
#endif
