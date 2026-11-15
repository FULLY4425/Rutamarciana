// Pruebas de host para pov_core.h + pov_contract.h (núcleo puro del POV)
#include "../psion-micor-spaceapps/pov_core.h"
#include "../psion-micor-spaceapps/mision_map_contract.h"
#include <cmath>
#include <cstdio>
#include <cstring>

static int fails = 0, total = 0;
#define CHECK(cond, name) do { total++; if (!(cond)) { fails++; printf("FALLO: %s\n", name); } } while (0)

using namespace pov;

static void setw(povc::PovInput* p, int i, int u, int v, float t, const char* nm) {
  p->wpt[i].u = (int16_t)u; p->wpt[i].v = (int16_t)v; p->wpt[i].t_s = t;
  snprintf(p->wpt[i].name, sizeof p->wpt[i].name, "%s", nm);
}

// Esqueleto válido de 2 waypoints ((0,0)->(100,0), 400 m, 100 s): cada
// prueba lo muta para el caso que quiere probar.
static povc::PovInput mkPov() {
  povc::PovInput p{};
  p.valid = true; p.variant = 0; p.n = 2; p.speed_ms = 2.0f;
  setw(&p, 0, 0, 0, 0.0f, "START");
  setw(&p, 1, 100, 0, 100.0f, "END");
  return p;
}

// ---- fakes de proveedores (contratos) ----
static int povFakeMode = 0;                 // 0 ok / 1 retFalse / 2 validFalse / 3 n=1
static bool fakePov(povc::PovInput* out, int variant) {
  if (povFakeMode == 1) return false;
  out->valid = (povFakeMode != 2);
  out->variant = variant;
  out->speed_ms = 0.04f;
  if (povFakeMode == 3) { out->n = 1; return out->valid; }
  out->n = 3;
  setw(out, 0, 80, 420, 0.0f, "START");
  setw(out, 1, 300, 150, 900.0f, "S1");
  setw(out, 2, 80, 420, 1800.0f, "END");
  return out->valid;
}

static mision::RouteInput gRi{};
static bool fakeMap(mision::RouteInput* out, int variant) {
  *out = gRi;
  out->variant = variant;
  return gRi.valid;
}

static bool samePov(const povc::PovInput& a, const povc::PovInput& b) {
  if (a.valid != b.valid || a.variant != b.variant || a.n != b.n) return false;
  if (a.speed_ms != b.speed_ms) return false;
  for (int i = 0; i < a.n; i++)
    if (memcmp(&a.wpt[i], &b.wpt[i], sizeof(povc::Waypoint)) != 0) return false;
  return true;
}

static float headAt(const Route& r, float s_m) {
  float u, v, h = -1.0f;
  posAtM(&r, s_m, &u, &v, &h);
  return h;
}

int main() {
  Route r{};
  float u, v, h;

  // 1) Contrato povc: proveedor registrado vs no registrado.
  povc::setPovProvider(nullptr);
  { povc::PovInput pi{};
    CHECK(!povc::provide(&pi, 0), "povc: sin proveedor -> false"); }
  povFakeMode = 0;
  povc::setPovProvider(fakePov);
  { povc::PovInput pi{};
    CHECK(povc::provide(&pi, 0) && pi.valid && pi.n == 3, "povc: proveedor ok -> true");
    CHECK(pi.wpt[1].u == 300 && fabsf(pi.wpt[1].t_s - 900) < 0.01f, "povc: campos copiados");
    CHECK(!povc::provide(nullptr, 0), "povc: salida nula -> false"); }
  povFakeMode = 1;
  { povc::PovInput pi{};
    CHECK(!povc::provide(&pi, 0), "povc: proveedor devuelve false -> false"); }
  povFakeMode = 2;
  { povc::PovInput pi{};
    CHECK(!povc::provide(&pi, 0), "povc: valid=false -> false"); }
  povc::setPovProvider(nullptr);
  { povc::PovInput pi{};
    CHECK(!povc::provide(&pi, 1), "povc: desregistrado -> false"); }

  // 2) build: validación (el proveedor del mapa es frontera de confianza).
  { povc::PovInput p = mkPov();
    CHECK(build(nullptr, &r) == ERR_NULL, "build: entrada nula");
    CHECK(build(&p, nullptr) == ERR_NULL, "build: salida nula");
    p.valid = false;
    CHECK(build(&p, &r) == ERR_INVALID && !r.valid, "build: PovInput invalido");
    p = mkPov(); p.n = 1;
    CHECK(build(&p, &r) == ERR_NWPT && !r.valid, "build: n < 2");
    p = mkPov(); p.n = povc::MAX_WPT + 1;
    CHECK(build(&p, &r) == ERR_NWPT, "build: n > MAX_WPT");
    p = mkPov(); p.speed_ms = 0;
    CHECK(build(&p, &r) == ERR_SPEED, "build: velocidad 0");
    p = mkPov(); p.speed_ms = NAN;
    CHECK(build(&p, &r) == ERR_NONFINITE, "build: velocidad NaN");
    p = mkPov(); setw(&p, 1, 500, 0, 100.0f, "END");
    CHECK(build(&p, &r) == ERR_RANGE, "build: u=500 fuera de 0..499");
    p = mkPov(); setw(&p, 1, 10, -1, 100.0f, "END");
    CHECK(build(&p, &r) == ERR_RANGE, "build: v negativo");
    p = mkPov(); setw(&p, 1, 10, 0, NAN, "END");
    CHECK(build(&p, &r) == ERR_NONFINITE, "build: t_s NaN");
    p = mkPov(); setw(&p, 1, 10, 0, -5.0f, "END");
    CHECK(build(&p, &r) == ERR_ORDER, "build: t_s decreciente");
    p = mkPov();
    CHECK(build(&p, &r) == OK && r.valid && r.error == OK, "build: ruta valida");
    CHECK(fabsf(r.total_m - 400) < 0.01f && fabsf(r.total_s - 100) < 0.01f, "build: 100 px -> 400 m, 100 s");
    CHECK(strcmp(r.name[0], "START") == 0 && strcmp(r.name[1], "END") == 0, "build: nombres copiados");
    CHECK(!r.stop[0] && !r.stop[1], "build: START/END no son paradas");
    p = mkPov(); setw(&p, 1, 0, 0, 100.0f, "END");        // waypoint duplicado
    CHECK(build(&p, &r) == OK && r.valid && fabsf(r.total_m) < 0.01f, "build: waypoint duplicado tolerado");
    CHECK(fabsf(r.seg_heading[0]) < 0.01f, "build: tramo nulo hereda heading 0"); }

  // 3) Interpolación de la polilínea y metros (px -> m con 4 m/px).
  { povc::PovInput p = mkPov();                            // (0,0)->(100,0): 400 m
    build(&p, &r);
    CHECK(posAtM(&r, 0, &u, &v, &h) && fabsf(u) < 0.01f && fabsf(v) < 0.01f, "interp: START exacto");
    CHECK(posAtM(&r, 200, &u, &v, &h) && fabsf(u - 50) < 0.01f && fabsf(v) < 0.01f, "interp: punto medio");
    CHECK(posAtM(&r, 400, &u, &v, &h) && fabsf(u - 100) < 0.01f, "interp: END exacto");
    CHECK(posAtM(&r, -50, &u, &v, &h) && fabsf(u) < 0.01f, "interp: s negativo clampa");
    CHECK(posAtM(&r, 99999, &u, &v, &h) && fabsf(u - 100) < 0.01f, "interp: s excesivo clampa");
    CHECK(posAtM(&r, 100, &u, &v, nullptr) && fabsf(u - 25) < 0.01f, "interp: heading opcional"); }
  { povc::PovInput p = mkPov(); p.n = 3;                   // codo: (0,0)->(0,100)->(100,100)
    setw(&p, 1, 0, 100, 100.0f, "S1");
    setw(&p, 2, 100, 100, 200.0f, "END");
    build(&p, &r);
    CHECK(fabsf(r.cum_m[1] - 400) < 0.01f && fabsf(r.cum_m[2] - 800) < 0.01f, "interp: distancia acumulada");
    CHECK(fabsf(r.seg_m[0] - 400) < 0.01f && fabsf(r.seg_m[1] - 400) < 0.01f, "interp: tramos de 400 m");
    CHECK(posAtM(&r, 400, &u, &v, &h) && fabsf(u) < 0.01f && fabsf(v - 100) < 0.01f, "interp: waypoint del codo");
    CHECK(posAtM(&r, 500, &u, &v, &h) && fabsf(u - 25) < 0.01f && fabsf(v - 100) < 0.01f, "interp: cuarto del 2o tramo");
    CHECK(posAtM(&r, 600, &u, &v, &h) && fabsf(u - 50) < 0.01f, "interp: mitad del 2o tramo"); }
  { povc::PovInput p = mkPov(); setw(&p, 1, 100, 100, 100.0f, "END");   // diagonal
    build(&p, &r);
    CHECK(fabsf(r.total_m - 565.6854f) < 0.01f, "interp: diagonal 100*raiz(2) px -> 565.69 m");
    CHECK(posAtM(&r, r.total_m / 2, &u, &v, &h) && fabsf(u - 50) < 0.01f && fabsf(v - 50) < 0.01f, "interp: mitad de la diagonal"); }

  // 4) Heading (brújula: 0 = norte, 90 = este, sentido horario).
  { povc::PovInput p = mkPov(); setw(&p, 1, 100, 0, 100.0f, "E");
    build(&p, &r); CHECK(fabsf(headAt(r, 200) - 90) < 0.01f, "heading: este = 90");
    p = mkPov(); setw(&p, 0, 0, 100, 0.0f, "N"); setw(&p, 1, 0, 0, 100.0f, "E");
    build(&p, &r); CHECK(fabsf(headAt(r, 200) - 0) < 0.01f, "heading: norte = 0");
    p = mkPov(); setw(&p, 1, 0, 100, 100.0f, "S");
    build(&p, &r); CHECK(fabsf(headAt(r, 200) - 180) < 0.01f, "heading: sur = 180");
    p = mkPov(); setw(&p, 0, 100, 0, 0.0f, "W"); setw(&p, 1, 0, 0, 100.0f, "E");
    build(&p, &r); CHECK(fabsf(headAt(r, 200) - 270) < 0.01f, "heading: oeste = 270");
    p = mkPov(); p.n = 3;                                  // codo sur->este
    setw(&p, 1, 0, 100, 100.0f, "S1"); setw(&p, 2, 100, 100, 200.0f, "END");
    build(&p, &r);
    CHECK(fabsf(headAt(r, 0) - 180) < 0.01f, "heading: inicio mira al primer tramo (sur)");
    CHECK(fabsf(headAt(r, 400) - 90) < 0.01f, "heading: en el codo mira al tramo siguiente (este)");
    CHECK(fabsf(headAt(r, 800) - 90) < 0.01f, "heading: al final conserva el ultimo tramo"); }

  // 5) Ventana de cámara clamped a los 500x500 de la ortofoto.
  { Cam c = camera(250, 250, 200, 160);
    CHECK(c.x == 150 && c.y == 170 && c.w == 200 && c.h == 160, "camara: centrada");
    c = camera(50, 50, 200, 160);
    CHECK(c.x == 0 && c.y == 0, "camara: borde NW clampa a 0");
    c = camera(490, 490, 200, 160);
    CHECK(c.x == 300 && c.y == 340, "camara: borde SE clampa a 500-w");
    c = camera(0, 0, 200, 160);
    CHECK(c.x == 0 && c.y == 0 && c.w == 200 && c.h == 160, "camara: esquina (0,0)");
    c = camera(250, 250, 700, 700);
    CHECK(c.w == 500 && c.h == 500 && c.x == 0 && c.y == 0, "camara: ventana mayor que el mapa");
    c = camera(250, 250, 0, -5);
    CHECK(c.w >= 1 && c.h >= 1, "camara: tamano minimo 1 px");
    c = camera(NAN, -5.0f, 200, 160);
    CHECK(c.x == 150 && c.y == 0, "camara: NaN/inf defensivo sin salirse");
    bool ok = true;
    const float probe[6][2] = {{0,0},{499,499},{250,250},{488,3},{2,497},{500,500}};
    for (int i = 0; i < 6; i++) {
      Cam k = camera(probe[i][0], probe[i][1], 320, 240);
      if (k.x < 0 || k.y < 0 || k.x + k.w > 500 || k.y + k.h > 500) ok = false;
    }
    CHECK(ok, "camara: invariante 0<=x, x+w<=500, 0<=y, y+h<=500"); }

  // 6) Simulación: tick por longitud de arco, escala de tiempo, pausa, seek.
  { povc::PovInput p = mkPov(); build(&p, &r);            // 400 m, 2 m/s, reloj 0..100 s
    Sim s;
    simInit(&s, &r);
    CHECK(fabsf(s.s_m) < 0.01f && fabsf(s.u) < 0.01f && s.moving && !s.done && s.scale == 1,
          "sim: init en START");
    CHECK(fabsf(s.heading_deg - 90) < 0.01f, "sim: heading inicial");
    simTick(&s, &r, 10.0f);                                // 2 m/s * 1 * 10 s = 20 m
    CHECK(fabsf(s.s_m - 20) < 0.01f && fabsf(s.u - 5) < 0.01f, "sim: tick avanza 20 m (5 px)");
    CHECK(fabsf(s.t_s - 5) < 0.01f, "sim: reloj interpolado (20 m de 400 -> 5 s)");
    s.scale = 10;
    simTick(&s, &r, 10.0f);                                // 2*10*10 = 200 m -> s = 220
    CHECK(fabsf(s.s_m - 220) < 0.01f && fabsf(s.u - 55) < 0.01f, "sim: escala x10");
    s.scale = 100;
    simTick(&s, &r, 2.0f);                                 // 2*100*2 = 400 m -> final
    CHECK(s.done && fabsf(s.u - 100) < 0.01f, "sim: escala x100 llega al final");
    simTick(&s, &r, 5.0f);
    CHECK(s.done && fabsf(s.s_m - r.total_m) < 0.01f, "sim: tick tras done no avanza");
    s.moving = false;
    simSeek(&s, &r, 200.0f);                               // scrubbing disponible en pausa
    CHECK(fabsf(s.s_m - 200) < 0.01f && !s.done, "sim: seek en pausa (knob)");
    simTick(&s, &r, 5.0f);
    CHECK(fabsf(s.s_m - 200) < 0.01f, "sim: pausa congela el tick");
    CHECK(fabsf(s.t_s - 50) < 0.01f, "sim: seek actualiza el reloj");
    simSeek(&s, &r, -5.0f);
    CHECK(fabsf(s.s_m) < 0.01f && fabsf(s.u) < 0.01f, "sim: seek negativo clampa a START"); }

  // 7) Ruta DEMO determinista (fallback honesto).
  povc::setPovProvider(nullptr);
  mision::setMapProvider(nullptr);
  { povc::PovInput p{}, q{};
    CHECK(demoRoute(&p, 0) == OK && p.valid, "demo: genera y valida");
    CHECK(p.n == 5, "demo: 3 paradas por defecto -> 5 waypoints");
    CHECK(strcmp(p.wpt[0].name, "START") == 0 && strcmp(p.wpt[4].name, "END") == 0, "demo: nombres START/END");
    CHECK(strcmp(p.wpt[1].name, "S1") == 0 && strcmp(p.wpt[3].name, "S3") == 0, "demo: nombres S1..S3");
    CHECK(fabsf(p.speed_ms - 0.04f) < 1e-6f, "demo: velocidad 0.04 m/s");
    bool inMap = true;
    for (int i = 0; i < p.n; i++)
      if (p.wpt[i].u < 0 || p.wpt[i].u > 499 || p.wpt[i].v < 0 || p.wpt[i].v > 499) inMap = false;
    CHECK(inMap, "demo: waypoints dentro de 0..499 px");
    demoRoute(&q, 0);
    CHECK(samePov(p, q), "demo: determinista (misma seed)");
    CHECK(build(&p, &r) == OK && r.valid, "demo: build OK");
    CHECK(r.total_m > 0, "demo: longitud total > 0");
    bool segsPos = true, tsAsc = true;
    for (int i = 0; i + 1 < r.n; i++) {
      if (r.seg_m[i] <= 0) segsPos = false;
      if (r.t_s[i + 1] <= r.t_s[i]) tsAsc = false;
    }
    CHECK(segsPos, "demo: todos los tramos > 0 (separacion minima)");
    CHECK(tsAsc, "demo: t_s estrictamente creciente");
    CHECK(r.stop[1] && r.stop[2] && r.stop[3] && !r.stop[0] && !r.stop[4], "demo: flags de parada");
    CHECK(fabsf(r.total_s - r.total_m / 0.04f) < 0.5f, "demo sin mapa: reloj = longitud/velocidad");
    demoRoute(&q, 1);
    CHECK(q.variant == 1, "demo: variante passthrough"); }
  // 7b) Demo CON tiempos del mapa (mision::RouteInput).
  { gRi = mision::RouteInput{};
    gRi.valid = true; gRi.n_stops = 2;
    gRi.travel_out_s = 1200; gRi.travel_back_s = 800;
    gRi.dwell_s[0] = 600; gRi.dwell_s[1] = 300;
    mision::setMapProvider(fakeMap);
    povc::PovInput p{};
    demoRoute(&p, 0);
    CHECK(p.n == 4, "demo+mision: n = n_stops+2");
    CHECK(fabsf(p.wpt[3].t_s - 2900) < 0.05f, "demo+mision: t END = ida+dwell+vuelta (2900)");
    CHECK(fabsf(p.wpt[2].t_s - 1800) < 0.05f, "demo+mision: t S2 = ida+dwell1 (1800)");
    CHECK(fabsf(p.wpt[1].t_s + (p.wpt[2].t_s - p.wpt[1].t_s - 600) - 1200) < 0.1f,
          "demo+mision: ida repartida entre tramos = travel_out");
    CHECK(p.wpt[1].t_s > 0 && p.wpt[1].t_s < 1200, "demo+mision: primer tramo proporcional");
    gRi.n_stops = 0; gRi.travel_out_s = 1000; gRi.travel_back_s = 500;
    demoRoute(&p, 0);
    CHECK(p.n == 2 && fabsf(p.wpt[1].t_s - 1500) < 0.05f, "demo+mision: 0 paradas -> todo el EVA");
    CHECK(p.wpt[1].u != 80 || p.wpt[1].v != 420, "demo+mision: 0 paradas no degenera");
    gRi.n_stops = 8; gRi.travel_out_s = 4800; gRi.travel_back_s = 1600;
    for (int i = 0; i < 8; i++) gRi.dwell_s[i] = 60;
    demoRoute(&p, 0);
    CHECK(p.n == povc::MAX_WPT && build(&p, &r) == OK && r.valid, "demo+mision: 8 paradas = MAX_WPT");
    gRi.valid = false;
    demoRoute(&p, 0);
    CHECK(p.n == 5, "demo+mision: RouteInput invalido -> demo pura");
    gRi.valid = true; gRi.n_stops = 2; gRi.travel_out_s = -5; gRi.dwell_s[0] = 600;
    demoRoute(&p, 0);
    CHECK(p.n == 5 && fabsf(p.wpt[4].t_s - p.wpt[3].t_s) > 0, "demo+mision: tiempos insanos -> geometrica");
    mision::setMapProvider(nullptr); }

  // 8) Próxima parada: distancia restante y ETA.
  { povc::PovInput p = mkPov(); p.n = 4; p.speed_ms = 2.0f;
    setw(&p, 1, 0, 100, 100.0f, "S1");
    setw(&p, 2, 100, 100, 200.0f, "S2");
    setw(&p, 3, 100, 0, 300.0f, "END");
    build(&p, &r);                                         // cum: 0, 400, 800, 1200 m
    Next n = nextStop(&r, 0);
    CHECK(n.valid && n.idx == 1 && fabsf(n.dist_m - 400) < 0.01f && fabsf(n.eta_s - 200) < 0.01f,
          "next: primera parada a 400 m, ETA 200 s");
    CHECK(strcmp(r.name[n.idx], "S1") == 0, "next: nombre de la parada");
    n = nextStop(&r, 399);
    CHECK(n.idx == 1 && fabsf(n.dist_m - 1) < 0.01f && fabsf(n.eta_s - 0.5f) < 0.01f, "next: a 1 m de S1");
    n = nextStop(&r, 400);
    CHECK(n.idx == 2 && fabsf(n.dist_m - 400) < 0.01f, "next: al alcanzar S1 -> S2");
    n = nextStop(&r, 700);
    CHECK(n.idx == 2 && fabsf(n.dist_m - 100) < 0.01f && fabsf(n.eta_s - 50) < 0.01f, "next: S2 a 100 m");
    n = nextStop(&r, 800);
    CHECK(!n.valid && n.idx == -1, "next: ultima parada alcanzada -> ninguna");
    n = nextStop(&r, 1500);
    CHECK(!n.valid, "next: ruta terminada -> ninguna"); }

  // 9) loadRoute: mapa conectado vs demo (honestidad de la fuente).
  povc::setPovProvider(nullptr);
  mision::setMapProvider(nullptr);
  { Route lr;
    CHECK(loadRoute(&lr, 0) == OK && lr.valid && lr.source == SRC_DEMO, "load: sin proveedores -> DEMO");
    povFakeMode = 0;
    povc::setPovProvider(fakePov);
    CHECK(loadRoute(&lr, 0) == OK && lr.source == SRC_MAP && lr.n == 3, "load: povc conectado -> MAP");
    povFakeMode = 2;
    CHECK(loadRoute(&lr, 0) == OK && lr.source == SRC_DEMO, "load: povc invalido -> DEMO");
    povFakeMode = 3;
    CHECK(loadRoute(&lr, 0) != OK && !lr.valid, "load: povc malformado -> error, sin demo silenciosa");
    povc::setPovProvider(nullptr); }

  // 10) Tiles de la fase 2: geometría del mapeo (u,v) -> tile (borde real 52 px).
  {
    CHECK(TILE_PX * (TILE_GRID - 1) + TILE_EDGE == MAP_PX, "tiles: 7*64+52 cubre los 500 px");
    int sw = 0, sh = 0;
    for (int c = 0; c < TILE_GRID; c++) sw += tileW(c);
    for (int r = 0; r < TILE_GRID; r++) sh += tileH(r);
    CHECK(sw == MAP_PX && sh == MAP_PX, "tiles: anchos/altos de la rejilla suman 500");
    CHECK(tileW(0) == 64 && tileW(7) == 52 && tileH(0) == 64 && tileH(7) == 52, "tiles: borde de 52 solo en la ultima fila/col");
    CHECK(tileCol(0) == 0 && tileCol(63) == 0 && tileCol(64) == 1 && tileCol(499) == 7, "tiles: columnas 0..7");
    CHECK(tileCol(500) == 7 && tileCol(-3) == 0, "tiles: clamp defensivo fuera de rango");
    CHECK(tileRow(447) == 6 && tileRow(448) == 7 && tileRow(499) == 7, "tiles: filas 0..7");
    CHECK(tileByteOff(0, 0) == 0, "tiles: offset (0,0) = 0");
    CHECK(tileByteOff(64, 0) == 0, "tiles: (64,0) abre el tile r0c1 en 0");
    CHECK(tileByteOff(100, 70) == (70 % 64) * 64 * 2 + (100 % 64) * 2, "tiles: offset salta filas de tile");
    CHECK(tileByteOff(499, 499) == 5406, "tiles: ultimo pixel (499,499) en 5406 (archivo de 5408 B)");
    int maxC = 0, maxR = 0;
    for (int x = 0; x + 200 <= MAP_PX; x++) { int n = tileCol(x + 199) - tileCol(x) + 1; if (n > maxC) maxC = n; }
    for (int y = 0; y + 160 <= MAP_PX; y++) { int n = tileRow(y + 159) - tileRow(y) + 1; if (n > maxR) maxR = n; }
    CHECK(maxC <= 5 && maxR <= 4, "tiles: ventana 200x160 toca <=5 columnas y <=4 filas");
  }

  printf("%d/%d pruebas OK\n", total - fails, total);
  return fails ? 1 : 0;
}
