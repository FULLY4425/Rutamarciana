// Local offline planner from the supplied Mars OS firmware, adapted to Ruta Marciana assets.
// This A* and fixed-speed estimate differs from the Python FAST/SAFE planner.
// Shared missions exchange stops and restrictions; private phone guidance uses RUTA1.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include "marte_data.h"
#if defined(ESP_PLATFORM)
#pragma GCC push_options
#pragma GCC optimize("fp-contract=off")       // (the same roundings as the PC test: no fused multiply-add)
#endif

const int MR_PATH_MAX = 1100, MR_STOPS = 10, MR_STEPS = 64, MR_HEAP = 1024, MR_NAME = 28;   // (test's worst: 469 cells, 356 open)
struct MrStep { uint16_t at; uint8_t kind, stop; float dist; };   // at: index in the path; dist: metres from the start (3D)
// kind: 1-3 = slightly / plain / sharply left, 4-6 = the same to the right, 7 = turn around (0 = no turn),
//       + 0x10 = the start, 0x20 = a stop on the way, 0x40 = the end (stop = which stop)
struct MrRoute {
  uint16_t path[MR_PATH_MAX];                   // cells (y * MR_W + x), first stop to last
  uint16_t stop[MR_STOPS], stopAt[MR_STOPS];    // the stops (cells) and where each one is in the path
  char name[MR_STOPS][MR_NAME];                 // their names (CP437): a target's own name, or the point's name
  uint8_t custom[MR_STOPS];                     // 1 = a point of your own (not a ChemCam target)
  MrStep step[MR_STEPS];
  uint16_t len; uint8_t nStops, nSteps, lim, ok; // lim: slope limit (degrees); ok: the path reaches every stop
  double dist, climb, dist3;                    // metres: on the map, uphill, along the ground (3D, for walking)
  int minutes;
};
struct MrMem { uint16_t z[MR_N]; MrRoute r; };  // z: cm above MR_BASE_CM (bits 0-13), 0x4000 = you can stand here, 0x8000 = route
MrMem* mrM = nullptr;
int mrPeak = 0;                                 // (the most cells the open list ever held: the test reports it)
const int8_t MR_DX[8] = {1, -1, 0, 0, 1, 1, -1, -1}, MR_DY[8] = {0, 0, 1, -1, 1, -1, 1, -1};   // local planner's order
inline int mrH(int i) { return mrM->z[i] & 0x3FFF; }
inline bool mrOk(int i) { return mrM->z[i] & 0x4000; }
inline int mrZat(int x, int y) { x = x < 0 ? 0 : x >= MR_W ? MR_W - 1 : x; y = y < 0 ? 0 : y >= MR_H ? MR_H - 1 : y; return mrH(y * MR_W + x); }

// ---------- the ground ----------
inline void mrUnpack() {                       // marte_data.h's heights -> z[] (see make_marte_data.py)
  uint16_t* z = mrM->z;
  const uint8_t* p = MR_TERRAIN;
  for (int y = 0, i = 0; y < MR_H; y++)
    for (int x = 0; x < MR_W; x++, i++) {
      uint8_t c = *p++;
      if (c == 0x80) { z[i] = (uint16_t)(p[0] << 8 | p[1]); p += 2; continue; }
      int g = !x ? (y ? z[i - MR_W] : 0) : !y ? z[i - 1] : z[i - 1] + z[i - MR_W] - z[i - MR_W - 1];
      z[i] = (uint16_t)(g + (int8_t)c);
    }
}
inline double mrE(int x, int y) {              // height in metres: exactly the number terrain.json has (cm / 100, rounded once)
  x = x < 0 ? 0 : x >= MR_W ? MR_W - 1 : x; y = y < 0 ? 0 : y >= MR_H ? MR_H - 1 : y;
  return (MR_BASE_CM + mrH(y * MR_W + x)) / 100.0;
}
inline double mrSlope(int x, int y) {          // local planner's slope_at(), degrees
  double gx = (mrE(x + 1, y) - mrE(x - 1, y)) / (x > 0 && x < MR_W - 1 ? 2 * MR_CELL_X : MR_CELL_X);
  double gy = (mrE(x, y + 1) - mrE(x, y - 1)) / (y > 0 && y < MR_H - 1 ? 2 * MR_CELL : MR_CELL);
  return atan(hypot(gx, gy)) * (180.0 / M_PI);
}

// ---------- the weather: the web's "environment" (educational environment: zones you can't enter + the speed) ----------
// It comes from the web through the Mars relay (the web chooses it); the Psion keeps the last one. Today the web's
// scenarios are made up for training (sourceType "synthetic"); when NASA data replaces them, the web hands over the
// same shape (restrictions + speedKmH) and the Psion's routes follow.
const int MR_ZONES = 8, MR_SCEN = 6;
struct MrZone { double lat, lon, r; char name[40]; };   // r: metres
struct MrEnv {
  char key[24], label[48], vis[72], notice[180], source[16];
  double speed;                                  // km/h
  int nZones, nScen;
  MrZone zone[MR_ZONES];
  char scenKey[MR_SCEN][16], scenLabel[MR_SCEN][44];   // the scenarios the web offers (to pick from the Psion)
};
MrEnv mrEnv = {"clear", "Despejado", "", "", "", 2.0, 0, 0, {}, {}, {}};
uint8_t mrZoneBit[(MR_N + 7) / 8];               // 1 = inside a restricted zone (local planner's blocked_cells())
inline bool mrInZone(int i) { return mrZoneBit[i >> 3] >> (i & 7) & 1; }
inline void mrZoneCells() {
  memset(mrZoneBit, 0, sizeof mrZoneBit);
  for (int k = 0; k < mrEnv.nZones; k++) {
    const MrZone& z = mrEnv.zone[k];
    double cx = (z.lon - MR_WEST) / MR_STEP, cy = (MR_NORTH - z.lat) / MR_STEP, rad = z.r / fmin(MR_CELL, MR_CELL_X);
    int y0 = (int)floor(cy - rad), y1 = (int)ceil(cy + rad), x0 = (int)floor(cx - rad), x1 = (int)ceil(cx + rad);
    for (int y = y0 < 0 ? 0 : y0; y <= y1 && y < MR_H; y++)
      for (int x = x0 < 0 ? 0 : x0; x <= x1 && x < MR_W; x++)
        if (hypot((x - cx)*MR_CELL_X, (y - cy)*MR_CELL) <= z.r) mrZoneBit[(y * MR_W + x) >> 3] |= 1 << ((y * MR_W + x) & 7);
  }
}
inline void mrSetLimit(int lim) {              // the cells you can stand on at this slope limit (local planner's passes())
  mrZoneCells();
  for (int y = 0, i = 0; y < MR_H; y++)
    for (int x = 0; x < MR_W; x++, i++) {
      if (mrSlope(x, y) <= lim && !mrInZone(i)) mrM->z[i] |= 0x4000; else mrM->z[i] &= ~0x4000;
    }
}

// ---------- local planner's find_path() ----------
// Its cost is metres + 3 x the height change, so two ways are often exactly as good: which one wins then comes down to
// the last bit of the sums and to Python's heap order (f, then x, then y). Both are copied (double maths on the same
// heights), so the Psion picks the web's way. The open list keeps one entry a cell (pos[] says where; Python leaves
// stale copies behind and skips them later: the same order). RAM for a moment: 2 x 8 KB + 2 KB + 17 KB + 8.6 KB = ~44 KB.
struct MrAstar {
  double* hf; double* hg; uint16_t* hi; uint16_t* pos; uint8_t* par; int hn, e;   // par: the move that got here (0-7), 0x80 = done
  double heur(int i) const { int dx = i % MR_W - e % MR_W, dy = i / MR_W - e / MR_W; return hypot(dx * MR_CELL_X, dy * MR_CELL); }
  bool less(int a, int b) const {              // Python compares (f, (x, y)): f, then x, then y
    if (hf[a] != hf[b]) return hf[a] < hf[b];
    int xa = hi[a] % MR_W, xb = hi[b] % MR_W;
    return xa != xb ? xa < xb : hi[a] < hi[b];
  }
  void swap(int a, int b) {
    double f = hf[a]; hf[a] = hf[b]; hf[b] = f; f = hg[a]; hg[a] = hg[b]; hg[b] = f;
    uint16_t t = hi[a]; hi[a] = hi[b]; hi[b] = t; pos[hi[a]] = a; pos[hi[b]] = b;
  }
  void up(int k) { while (k && less(k, (k - 1) / 2)) { swap(k, (k - 1) / 2); k = (k - 1) / 2; } }
  void down(int k) {
    for (;;) {
      int l = 2 * k + 1, s = k;
      if (l < hn && less(l, s)) s = l;
      if (l + 1 < hn && less(l + 1, s)) s = l + 1;
      if (s == k) return;
      swap(k, s); k = s;
    }
  }
  int pop(double* g) {
    int i = hi[0]; *g = hg[0]; pos[i] = 0xFFFF;
    if (--hn) { hf[0] = hf[hn]; hg[0] = hg[hn]; hi[0] = hi[hn]; pos[hi[0]] = 0; down(0); }
    return i;
  }
  int trace(int s, uint16_t* out, int room) {
    int n = 1;
    for (int i = e; i != s; n++) { int k = par[i] & 7; i -= MR_DY[k] * MR_W + MR_DX[k]; }
    if (n > room) return -2;
    for (int i = e, j = n - 1; ; j--) { out[j] = (uint16_t)i; if (i == s) break; int k = par[i] & 7; i -= MR_DY[k] * MR_W + MR_DX[k]; }
    return n;
  }
  // -> cells in the path (both ends included) written to out; 0 = no way through, -1 = no RAM, -2 = too long / too big
  int find(int s, int end, int lim, uint16_t* out, int room) {
    if (!mrOk(s) || !mrOk(end)) return 0;
    e = end; hn = 0;
    const double m = MR_CELL, md = hypot(MR_CELL_X, MR_CELL), tl = tan(lim * (M_PI / 180.0));   // (planner: hypot(1, 1) * m)
    hf = (double*)malloc(MR_HEAP * sizeof(double)); hg = (double*)malloc(MR_HEAP * sizeof(double));
    hi = (uint16_t*)malloc(MR_HEAP * sizeof(uint16_t)); pos = (uint16_t*)malloc(MR_N * sizeof(uint16_t)); par = (uint8_t*)malloc(MR_N);
    int r = -1;
    if (hf && hg && hi && pos && par) {
      memset(pos, 0xFF, MR_N * sizeof(uint16_t)); memset(par, 0x7F, MR_N);
      hf[0] = hg[0] = 0; hi[0] = (uint16_t)s; pos[s] = 0; hn = 1; r = 0;   // Python: queue [(0, start)], scores {start: 0}
      while (hn) {
        double gc;
        int cur = pop(&gc);
        if (cur == end) { r = trace(s, out, room); break; }
        par[cur] |= 0x80;
        int cx = cur % MR_W, cy = cur / MR_W;
        double ec = mrE(cx, cy);
        for (int k = 0; k < 8; k++) {
          int nx = cx + MR_DX[k], ny = cy + MR_DY[k];
          if (nx < 0 || ny < 0 || nx >= MR_W || ny >= MR_H) continue;
          int n = ny * MR_W + nx;
          if ((par[n] & 0x80) || !mrOk(n)) continue;
          if (k >= 4 && (!mrOk(cy * MR_W + nx) || !mrOk(ny * MR_W + cx))) continue;   // no squeezing past a corner
          double d = k >= 4 ? md : MR_DX[k] ? MR_CELL_X : m, grade = fabs(mrE(nx, ny) - ec) / d;
          if (grade > tl) continue;                                                   // the step itself too steep
          double cost = gc + d * (1 + 3 * grade);
          int p = pos[n];
          if (p != 0xFFFF) {                                                          // already waiting: a better way?
            if (cost < hg[p]) { hg[p] = cost; hf[p] = cost + heur(n); par[n] = (uint8_t)k; up(p); }
          } else {
            if (hn == MR_HEAP) { r = -2; hn = 0; break; }
            hf[hn] = cost + heur(n); hg[hn] = cost; hi[hn] = (uint16_t)n; pos[n] = (uint16_t)hn; par[n] = (uint8_t)k;
            up(hn++);
            if (hn > mrPeak) mrPeak = hn;
          }
        }
      }
    }
    free(hf); free(hg); free(hi); free(pos); free(par);
    return r;
  }
};
// local planner's plan_mission(), from stop `from` on (the path up to it stays, if the route was whole). 1 = every stop
// reached; else the A* result (0 / -1 / -2) and *bad = the stop it couldn't reach (the path ends at the stop before it).
inline int mrSegments(int from, int* bad) {
  MrRoute& R = mrM->r;
  if (!R.ok || from < 1) from = 1;
  R.ok = 0;
  if (from == 1) { R.len = 0; if (R.nStops) { R.path[0] = R.stop[0]; R.stopAt[0] = 0; R.len = 1; } }
  else R.len = R.stopAt[from - 1] + 1;
  for (int k = from; k < R.nStops; k++) {
    int n = MrAstar().find(R.stop[k - 1], R.stop[k], R.lim, R.path + R.len - 1, MR_PATH_MAX - (R.len - 1));
    if (n <= 0) { *bad = k; return n; }
    R.len += n - 1; R.stopAt[k] = R.len - 1;
  }
  R.ok = 1;
  return 1;
}

// ---------- directions.js ----------
inline double mrSeg3(int i) {                  // the ground distance from path[i] to path[i + 1] (3D)
  const uint16_t* P = mrM->r.path;
  double dx = (P[i + 1] % MR_W - P[i] % MR_W) * MR_CELL_X, dy = (P[i + 1] / MR_W - P[i] / MR_W) * MR_CELL;
  double dz = (mrH(P[i + 1]) - mrH(P[i])) / 100.0;
  return sqrt(dx * dx + dy * dy + dz * dz);
}
inline void mrSimplify(int a0, int b0, uint8_t* keep) {   // simplifiedIndices(): Douglas-Peucker, 1.05 cells
  const uint16_t* P = mrM->r.path;
  int st[48][2], sp = 0;
  keep[a0 >> 3] |= 1 << (a0 & 7); keep[b0 >> 3] |= 1 << (b0 & 7);
  st[sp][0] = a0; st[sp][1] = b0; sp++;
  while (sp) {
    sp--;
    int a = st[sp][0], b = st[sp][1], at = -1;
    if (b - a < 2) continue;
    double x = P[a] % MR_W, y = P[a] / MR_W, dx = P[b] % MR_W - x, dy = P[b] / MR_W - y, len = dx * dx + dy * dy, max = 1.05 * 1.05;
    for (int i = a + 1; i < b; i++) {
      double px = P[i] % MR_W - x, py = P[i] / MR_W - y;
      double t = len ? fmax(0.0, fmin(1.0, (px * dx + py * dy) / len)) : 0.0;
      double d = (px - t * dx) * (px - t * dx) + (py - t * dy) * (py - t * dy);
      if (d > max) { max = d; at = i; }
    }
    if (at < 0) continue;
    keep[at >> 3] |= 1 << (at & 7);
    if (sp + 2 > 48) { for (int i = a + 1; i < b; i++) keep[i >> 3] |= 1 << (i & 7); continue; }   // (never on real routes)
    st[sp][0] = a; st[sp][1] = at; sp++;
    st[sp][0] = at; st[sp][1] = b; sp++;
  }
}
inline uint8_t mrTurn(int a, int b, int c) {   // turn(): 0 = under 30 degrees
  const uint16_t* P = mrM->r.path;
  double ax = P[b] % MR_W - (double)(P[a] % MR_W), ay = P[b] / MR_W - (double)(P[a] / MR_W);
  double bx = P[c] % MR_W - (double)(P[b] % MR_W), by = P[c] / MR_W - (double)(P[b] / MR_W);
  double ang = atan2(ax * by - ay * bx, ax * bx + ay * by) * 180 / M_PI, am = fabs(ang);
  if (am < 30) return 0;
  if (am > 155) return 7;
  return (ang > 0 ? 4 : 1) + (am < 60 ? 0 : am > 120 ? 2 : 1);
}
// After the path: local planner's route_metrics() + duration and buildDirections(); marks the route on the map.
inline void mrFinish() {
  MrRoute& R = mrM->r;
  for (int i = 0; i < MR_N; i++) mrM->z[i] &= 0x7FFF;
  for (int i = 0; i < R.len; i++) mrM->z[R.path[i]] |= 0x8000;
  R.dist = R.climb = R.dist3 = 0; R.minutes = 0; R.nSteps = 0;
  long up = 0;
  for (int i = 1; i < R.len; i++) {
    int a = R.path[i - 1], b = R.path[i];
    R.dist += hypot((b % MR_W - a % MR_W) * MR_CELL_X, (b / MR_W - a / MR_W) * MR_CELL);
    up += mrH(b) > mrH(a) ? mrH(b) - mrH(a) : 0;
  }
  R.climb = up / 100.0;
  if (!R.ok || R.nStops < 2 || R.len < 2) return;
  R.minutes = (int)ceil(R.dist / (mrEnv.speed * 1000) * 60 + (R.nStops - 1) * 10);   // (the weather's speed)
  uint8_t keep[(MR_PATH_MAX + 7) / 8];
  memset(keep, 0, sizeof keep);
  for (int k = 1; k < R.nStops; k++) mrSimplify(R.stopAt[k - 1], R.stopAt[k], keep);
  #define MR_KEPT(i) (keep[(i) >> 3] & (1 << ((i) & 7)))
  R.step[R.nSteps++] = {0, 0x10, 0, 0.f};
  double run = 0;
  int prev = 0, ns = 1;
  for (int i = 1; i < R.len; i++) {
    run += mrSeg3(i - 1);
    if (!MR_KEPT(i) || i == R.len - 1) continue;
    int nx = i + 1;
    while (!MR_KEPT(nx)) nx++;
    uint8_t t = mrTurn(prev, i, nx);
    while (ns < R.nStops - 1 && R.stopAt[ns] < i) ns++;
    bool at = ns < R.nStops - 1 && R.stopAt[ns] == i;
    if ((at || t) && R.nSteps < MR_STEPS - 1) R.step[R.nSteps++] = {(uint16_t)i, (uint8_t)(t | (at ? 0x20 : 0)), (uint8_t)(at ? ns : 0), (float)run};
    prev = i;
  }
  #undef MR_KEPT
  R.dist3 = run;
  R.step[R.nSteps++] = {(uint16_t)(R.len - 1), 0x40, (uint8_t)(R.nStops - 1), (float)run};
}

// ---------- the language (Mars OS 1.3): 0 = Spanish (the web's own words), 1 = English ----------
uint8_t mrLang = 0;
#define MRL(es, en) (mrLang ? (en) : (es))
// ---------- names + words ----------
inline int mrSiteAt(int x, int y) { for (int s = 0; s < MR_NSITES; s++) if (MR_SITES[s].x == x && MR_SITES[s].y == y) return s; return -1; }
inline const char* mrSiteRaw(int site) { return MR_NAMES + MR_SITES[site].name; }   // the target's own name (with '_')
inline void mrShow(const char* raw, char* out, int n) {   // a name to show: '_' as a space ("punto 3" = "point 3" in English)
  int i = 0, o = 0;
  if (mrLang && !strncmp(raw, "punto ", 6) && n > 6) { memcpy(out, "point ", 6); i = o = 6; }
  for (; raw[i] && o < n - 1; i++) out[o++] = raw[i] == '_' ? ' ' : raw[i];
  out[o] = 0;
}
inline void mrName(int site, char* out, int n) { mrShow(mrSiteRaw(site), out, n); }
inline void mrStopName(int k, char* out, int n) { mrShow(mrM->r.name[k], out, n); }
inline void mrNameStop(int k) {                // a new stop's name: its site's target, or "punto N"
  MrRoute& R = mrM->r;
  int c = R.stop[k], s = mrSiteAt(c % MR_W, c / MR_W);
  R.custom[k] = s < 0;
  if (s >= 0) snprintf(R.name[k], MR_NAME, "%s", mrSiteRaw(s)); else snprintf(R.name[k], MR_NAME, "punto %d", k + 1);
}
inline void mrDist(double m, char* out, int n) {   // distanceText(), short
  if (m >= 1000) snprintf(out, n, "%.1f km", m / 1000); else snprintf(out, n, "%d m", (int)lround(m / 5) * 5 > 0 ? (int)lround(m / 5) * 5 : 0);
}
inline int mrWrap(const char* s, int at, int maxc, char* out) {   // the next line of s from `at` (broken at a space) -> next start
  while (s[at] == ' ') at++;
  int n = strlen(s + at), cut = maxc;
  if (n <= maxc) cut = n;
  else { while (cut > 0 && s[at + cut] != ' ') cut--; if (!cut) cut = maxc; }
  memcpy(out, s + at, cut); out[cut] = 0;
  return at + cut;
}
inline void mrStepText(int si, bool ahead, char* out, int n) {   // local guidance's words ("Llegaste" -> "Llegaras" on the way)
  const MrStep& S = mrM->r.step[si];
  int t = S.kind & 15;
  char turn[44] = "", name[MR_NAME] = "";
  static const char* const HOW[3] = {"ligeramente ", "", "pronunciadamente "};
  if (S.kind & 0x60) mrStopName(S.stop, name, sizeof name);
  if (mrLang) {                                 // English (1.3)
    static const char* const HOW_EN[3] = {"slightly ", "", "sharply "};
    if (t == 7) snprintf(turn, sizeof turn, "Turn around");
    else if (t) snprintf(turn, sizeof turn, "Turn %s%s", HOW_EN[(t - 1) % 3], t > 3 ? "right" : "left");
    const char* got = ahead ? "You'll reach" : "You reached";
    if (S.kind & 0x10) snprintf(out, n, "Start the walk and follow the marked route");
    else if (S.kind & 0x20) snprintf(out, n, "%s %s. %s", got, name, t ? turn : "Keep on the marked route");
    else if (S.kind & 0x40) snprintf(out, n, "%s %s. End of the route", got, name);
    else snprintf(out, n, "%s", turn);
    return;
  }
  if (t == 7) snprintf(turn, sizeof turn, "Da media vuelta");
  else if (t) snprintf(turn, sizeof turn, "Gira %sa la %s", HOW[(t - 1) % 3], t > 3 ? "derecha" : "izquierda");
  const char* got = ahead ? "Llegar\xA0s" : "Llegaste";
  if (S.kind & 0x10) snprintf(out, n, "Inicia el recorrido y sigue la ruta marcada");
  else if (S.kind & 0x20) snprintf(out, n, "%s a %s. %s", got, name, t ? turn : "Contin\xA3" "a por la ruta marcada");
  else if (S.kind & 0x40) snprintf(out, n, "%s a %s. Fin del recorrido", got, name);
  else snprintf(out, n, "%s", turn);
}

// ---------- text: UTF-8 (the server) <-> CP437 (the screens' font) ----------
static const uint16_t MR_CP_UNI[] = {0xE1, 0xE9, 0xED, 0xF3, 0xFA, 0xFC, 0xF1, 0xD1, 0xBF, 0xA1, 0xC9, 0xB0, 0xB7, 0xE0, 0xE8, 0xE7, 0xC7, 0xE4, 0xF6, 0xDC};
static const uint8_t MR_CP_437[] = {0xA0, 0x82, 0xA1, 0xA2, 0xA3, 0x81, 0xA4, 0xA5, 0xA8, 0xAD, 0x90, 0xF8, 0xFA, 0x85, 0x8A, 0x87, 0x80, 0x84, 0x94, 0x9A};
inline uint8_t mrToCp437(uint32_t u) {          // one character; '?' if the font hasn't got it
  if (u < 0x80) return (uint8_t)u;
  for (unsigned i = 0; i < sizeof MR_CP_437; i++) if (MR_CP_UNI[i] == u) return MR_CP_437[i];
  if (u == 0xC1 || u == 0xCD || u == 0xD3 || u == 0xDA) return u == 0xC1 ? 'A' : u == 0xCD ? 'I' : u == 0xD3 ? 'O' : 'U';
  return '?';
}
inline int mrToUtf8(uint8_t c, char* out) {     // one CP437 character -> UTF-8 bytes
  uint32_t u = c;
  if (c >= 0x80) { u = '?'; for (unsigned i = 0; i < sizeof MR_CP_437; i++) if (MR_CP_437[i] == c) { u = MR_CP_UNI[i]; break; } }
  if (u < 0x80) { out[0] = (char)u; return 1; }
  out[0] = (char)(0xC0 | (u >> 6)); out[1] = (char)(0x80 | (u & 0x3F)); return 2;
}

// ---------- the server's JSON (a small reader: just what psion_share.py sends) ----------
struct MrJson { const char* p; bool bad; };
inline void mrJsWs(MrJson& j) { while (*j.p == ' ' || *j.p == '\n' || *j.p == '\r' || *j.p == '\t') j.p++; }
inline bool mrJsIs(MrJson& j, char c) { mrJsWs(j); if (*j.p == c) { j.p++; return true; } return false; }
inline void mrJsString(MrJson& j, char* out, int n) {   // a string -> CP437 (out may be null: skipped)
  int o = 0;
  mrJsWs(j);
  if (*j.p != '"') { j.bad = true; return; }
  j.p++;
  while (*j.p && *j.p != '"') {
    uint32_t u;
    if (*j.p == '\\') {
      j.p++;
      char e = *j.p ? *j.p++ : 0;
      if (e == 'u') { u = 0; for (int k = 0; k < 4 && *j.p; k++) { char h = *j.p++; u = u * 16 + (h <= '9' ? h - '0' : (h | 32) - 'a' + 10); } }
      else u = e == 'n' ? '\n' : e == 't' ? ' ' : (uint8_t)e;
    } else {
      uint8_t b = (uint8_t)*j.p++;
      if (b < 0x80) u = b;
      else if ((b & 0xE0) == 0xC0 && *j.p) { u = ((b & 0x1F) << 6) | (*j.p++ & 0x3F); }
      else if ((b & 0xF0) == 0xE0 && j.p[0] && j.p[1]) { u = ((b & 0x0F) << 12) | ((j.p[0] & 0x3F) << 6) | (j.p[1] & 0x3F); j.p += 2; }
      else { u = '?'; while ((*j.p & 0xC0) == 0x80) j.p++; }
    }
    if (out && o < n - 1) out[o++] = (char)mrToCp437(u);
  }
  if (*j.p == '"') j.p++; else j.bad = true;
  if (out) out[o] = 0;
}
inline double mrJsNumber(MrJson& j) {
  mrJsWs(j);
  char* end;
  double v = strtod(j.p, &end);
  if (end == j.p) { j.bad = true; return 0; }
  j.p = end;
  return v;
}
inline void mrJsSkip(MrJson& j, int depth = 0) { // any value
  mrJsWs(j);
  if (depth > 8) { j.bad = true; return; }
  if (*j.p == '"') { mrJsString(j, nullptr, 0); return; }
  if (*j.p == '{' || *j.p == '[') {
    char close = *j.p == '{' ? '}' : ']';
    j.p++;
    if (mrJsIs(j, close)) return;
    do {
      if (close == '}') { mrJsString(j, nullptr, 0); if (!mrJsIs(j, ':')) { j.bad = true; return; } }
      mrJsSkip(j, depth + 1);
    } while (!j.bad && mrJsIs(j, ','));
    if (!mrJsIs(j, close)) j.bad = true;
    return;
  }
  if (!strncmp(j.p, "true", 4)) j.p += 4; else if (!strncmp(j.p, "false", 5)) j.p += 5; else if (!strncmp(j.p, "null", 4)) j.p += 4;
  else mrJsNumber(j);
}
inline bool mrJsBool(MrJson& j) { mrJsWs(j); if (!strncmp(j.p, "true", 4)) { j.p += 4; return true; } mrJsSkip(j); return false; }
struct MrReply {                                // what POST / GET /api/psion/missions answers (or {"error": ...})
  char code[8], crc[12], error[120], weather[24]; // weather: the relay's plan says which weather the web has on
  int lim, minutes, cells, nStops, version;      // version: the relay's plan number (live sync)
  double distance, ascent;
  uint16_t stop[MR_STOPS]; char name[MR_STOPS][MR_NAME]; uint8_t custom[MR_STOPS];
};
inline bool mrParseReply(const char* js, MrReply* r) {   // false = not JSON we understand (r->error says so)
  memset(r, 0, sizeof *r);
  MrJson j = {js, false};
  if (!mrJsIs(j, '{')) { snprintf(r->error, sizeof r->error, "%s", MRL("El servidor no contest\xA2 JSON.", "The server didn't answer in JSON.")); return false; }
  if (!mrJsIs(j, '}')) do {
    char key[24];
    mrJsString(j, key, sizeof key);
    if (j.bad || !mrJsIs(j, ':')) { j.bad = true; break; }
    if (!strcmp(key, "code")) mrJsString(j, r->code, sizeof r->code);
    else if (!strcmp(key, "pathCrc")) mrJsString(j, r->crc, sizeof r->crc);
    else if (!strcmp(key, "error")) mrJsString(j, r->error, sizeof r->error);
    else if (!strcmp(key, "maxSlopeDegrees")) r->lim = (int)lround(mrJsNumber(j));
    else if (!strcmp(key, "version")) r->version = (int)lround(mrJsNumber(j));
    else if (!strcmp(key, "weatherKey")) mrJsString(j, r->weather, sizeof r->weather);
    else if (!strcmp(key, "minutes")) r->minutes = (int)lround(mrJsNumber(j));
    else if (!strcmp(key, "cells")) r->cells = (int)lround(mrJsNumber(j));
    else if (!strcmp(key, "distance")) r->distance = mrJsNumber(j);
    else if (!strcmp(key, "ascent")) r->ascent = mrJsNumber(j);
    else if (!strcmp(key, "stops")) {
      if (!mrJsIs(j, '[')) { j.bad = true; break; }
      if (!mrJsIs(j, ']')) { do {
        int x = -1, y = -1, k = r->nStops;
        char nm[MR_NAME] = "";
        bool cu = false;
        if (!mrJsIs(j, '{')) { j.bad = true; break; }
        if (!mrJsIs(j, '}')) do {
          char sk[12];
          mrJsString(j, sk, sizeof sk);
          if (j.bad || !mrJsIs(j, ':')) { j.bad = true; break; }
          if (!strcmp(sk, "name")) mrJsString(j, nm, sizeof nm);
          else if (!strcmp(sk, "x")) x = (int)lround(mrJsNumber(j));
          else if (!strcmp(sk, "y")) y = (int)lround(mrJsNumber(j));
          else if (!strcmp(sk, "custom")) cu = mrJsBool(j);
          else mrJsSkip(j);
        } while (!j.bad && mrJsIs(j, ','));
        if (!j.bad && !mrJsIs(j, '}')) j.bad = true;
        if (j.bad) break;
        if (x < 0 || y < 0 || x >= MR_W || y >= MR_H || k >= MR_STOPS) { j.bad = true; break; }
        r->stop[k] = (uint16_t)(y * MR_W + x); r->custom[k] = cu;
        memcpy(r->name[k], nm, MR_NAME);
        r->nStops++;
      } while (!j.bad && mrJsIs(j, ','));
      if (!j.bad && !mrJsIs(j, ']')) j.bad = true; }
    } else mrJsSkip(j);
  } while (!j.bad && mrJsIs(j, ','));
  if (!j.bad && !mrJsIs(j, '}')) j.bad = true;
  if (j.bad) { snprintf(r->error, sizeof r->error, "%s", MRL("Respuesta del servidor que no entiendo.", "An answer from the server I can't read.")); return false; }
  return true;
}
// The mission as the server wants it: {"stops": ["Target", {"type": "custom", ...}], "maxSlopeDegrees": 20}
inline int mrRequestJson(char* out, int n) {     // -> length (0 = didn't fit)
  MrRoute& R = mrM->r;
  int o = snprintf(out, n, "{\"stops\":[");
  for (int k = 0; k < R.nStops && o < n; k++) {
    char nm[MR_NAME * 3 + 4];
    int q = 0;
    for (const char* p = R.name[k]; *p && q < (int)sizeof nm - 4; p++) {
      uint8_t c = (uint8_t)*p;
      if (c == '"' || c == '\\') { nm[q++] = '\\'; nm[q++] = (char)c; }
      else if (c < 0x20) nm[q++] = ' ';
      else q += mrToUtf8(c, nm + q);
    }
    nm[q] = 0;
    int x = R.stop[k] % MR_W, y = R.stop[k] / MR_W;
    if (R.custom[k]) o += snprintf(out + o, n - o, "%s{\"type\":\"custom\",\"name\":\"%s\",\"lat\":%.12f,\"lon\":%.12f}", k ? "," : "", nm,
                                   MR_NORTH - y * MR_STEP, MR_WEST + x * MR_STEP);
    else o += snprintf(out + o, n - o, "%s\"%s\"", k ? "," : "", nm);
  }
  if (o < n) o += snprintf(out + o, n - o, "],\"maxSlopeDegrees\":%d", R.lim);
  if (o < n && mrEnv.key[0]) o += snprintf(out + o, n - o, ",\"weather\":\"%s\"", mrEnv.key);   // (plain ASCII keys)
  if (o < n) o += snprintf(out + o, n - o, ",\"environment\":{\"weather\":\"%s\",\"blocked_zones\":[",mrEnv.key);
  for(int k=0;k<mrEnv.nZones && o<n;k++){
    const MrZone& z=mrEnv.zone[k];
    o+=snprintf(out+o,n-o,"%s{\"center\":[%.8f,%.8f],\"radius_m\":%.3f,\"kind\":\"manual_obstacle\"}",k?",":"",(MR_NORTH-z.lat)/MR_STEP,(z.lon-MR_WEST)/MR_STEP,z.r);
  }
  if (o < n) o += snprintf(out + o, n - o, "]}}");
  return o < n ? o : 0;
}
// The web's weather, as the Mars relay hands it over (GET /mr/weather): local planner's environment + the scenario list.
//   {"weather": "storm", "label": "...", "visibility": "...", "notice": "...", "sourceType": "synthetic", "speedKmH": 1,
//    "restrictions": [{"name": "...", "lat": -4.73, "lon": 137.39, "radiusMeters": 133.5}], "scenarios": [{"key": "clear", "label": "..."}]}
inline bool mrParseEnv(const char* js, MrEnv* e, char* err, int errN) {
  MrEnv t;
  memset(&t, 0, sizeof t);
  t.speed = -1;
  if (err && errN) err[0] = 0;
  MrJson j = {js, false};
  if (!mrJsIs(j, '{')) { if (err) snprintf(err, errN, "%s", MRL("El relay no contest\xA2 JSON.", "The relay didn't answer in JSON.")); return false; }
  if (!mrJsIs(j, '}')) do {
    char key[24];
    mrJsString(j, key, sizeof key);
    if (j.bad || !mrJsIs(j, ':')) { j.bad = true; break; }
    if (!strcmp(key, "weather")) mrJsString(j, t.key, sizeof t.key);
    else if (!strcmp(key, "label")) mrJsString(j, t.label, sizeof t.label);
    else if (!strcmp(key, "visibility")) mrJsString(j, t.vis, sizeof t.vis);
    else if (!strcmp(key, "notice")) mrJsString(j, t.notice, sizeof t.notice);
    else if (!strcmp(key, "sourceType")) mrJsString(j, t.source, sizeof t.source);
    else if (!strcmp(key, "speedKmH")) t.speed = mrJsNumber(j);
    else if (!strcmp(key, "error")) { char m[120]; mrJsString(j, m, sizeof m); if (err) snprintf(err, errN, "%s", m); }
    else if (!strcmp(key, "restrictions") || !strcmp(key, "scenarios")) {
      bool zones = key[0] == 'r';
      if (!mrJsIs(j, '[')) { j.bad = true; break; }
      if (!mrJsIs(j, ']')) { do {
        MrZone z = {0, 0, -1, ""};
        char sk[16] = "", sl[44] = "";
        if (!mrJsIs(j, '{')) { j.bad = true; break; }
        if (!mrJsIs(j, '}')) do {
          char k2[16];
          mrJsString(j, k2, sizeof k2);
          if (j.bad || !mrJsIs(j, ':')) { j.bad = true; break; }
          if (zones && !strcmp(k2, "lat")) z.lat = mrJsNumber(j);
          else if (zones && !strcmp(k2, "lon")) z.lon = mrJsNumber(j);
          else if (zones && !strcmp(k2, "radiusMeters")) z.r = mrJsNumber(j);
          else if (zones && !strcmp(k2, "name")) mrJsString(j, z.name, sizeof z.name);
          else if (!zones && !strcmp(k2, "key")) mrJsString(j, sk, sizeof sk);
          else if (!zones && !strcmp(k2, "label")) mrJsString(j, sl, sizeof sl);
          else mrJsSkip(j);
        } while (!j.bad && mrJsIs(j, ','));
        if (!j.bad && !mrJsIs(j, '}')) j.bad = true;
        if (j.bad) break;
        if (zones) { if (z.r < 0 || t.nZones >= MR_ZONES) { j.bad = true; break; } t.zone[t.nZones++] = z; }
        else if (t.nScen < MR_SCEN && sk[0]) { snprintf(t.scenKey[t.nScen], 16, "%s", sk); snprintf(t.scenLabel[t.nScen], 44, "%s", sl[0] ? sl : sk); t.nScen++; }
      } while (!j.bad && mrJsIs(j, ','));
      if (!j.bad && !mrJsIs(j, ']')) j.bad = true; }
    } else mrJsSkip(j);
  } while (!j.bad && mrJsIs(j, ','));
  if (!j.bad && !mrJsIs(j, '}')) j.bad = true;
  if (j.bad || !t.key[0] || !(t.speed > 0)) { if (err && !err[0]) snprintf(err, errN, "%s", MRL("El clima de la web no se entiende.", "The web's weather can't be read.")); return false; }
  for (unsigned i = 0; t.key[i]; i++) if (!isalnum((unsigned char)t.key[i]) && t.key[i] != '-' && t.key[i] != '_') return false;
  *e = t;
  return true;
}
inline void mrEnvClear() {                       // no weather: the web's "clear" (2 km/h, nothing closed)
  MrEnv t;
  memset(&t, 0, sizeof t);
  snprintf(t.key, sizeof t.key, "clear"); snprintf(t.label, sizeof t.label, "Despejado"); t.speed = 2.0;
  for (int i = 0; i < mrEnv.nScen; i++) { memcpy(t.scenKey[i], mrEnv.scenKey[i], 16); memcpy(t.scenLabel[i], mrEnv.scenLabel[i], 44); }
  t.nScen = mrEnv.nScen;
  mrEnv = t;
}
inline uint32_t mrPathCrc() {                    // zlib's CRC32 of the cells as (x, y) bytes: mission exchange's pathCrc
  MrRoute& R = mrM->r;
  uint32_t c = 0xFFFFFFFFu;
  for (int i = 0; i < R.len; i++) {
    uint8_t b[2] = {(uint8_t)(R.path[i] % MR_W), (uint8_t)(R.path[i] / MR_W)};
    for (int k = 0; k < 2; k++) { c ^= b[k]; for (int s = 0; s < 8; s++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1))); }
  }
  return ~c;
}
inline void mrTakeStops(const MrReply& r) {     // a mission from the server becomes the route (worked out again here)
  MrRoute& R = mrM->r;
  R.nStops = (uint8_t)r.nStops; R.lim = (uint8_t)(r.lim < 5 ? 5 : r.lim > 40 ? 40 : r.lim); R.ok = 0; R.len = 0;
  for (int k = 0; k < r.nStops; k++) { R.stop[k] = r.stop[k]; R.custom[k] = r.custom[k]; memcpy(R.name[k], r.name[k], MR_NAME); }
}

// ---------- the mission file (/marte/mNN.txt, plain text) ----------
//   MARTE 1
//   lim 20
//   stop 10 26 0 Stornoway          (x y custom name)
inline int mrMissionText(char* out, int n) {
  MrRoute& R = mrM->r;
  int o = snprintf(out, n, "MARTE 1\nlim %d\n", R.lim);
  for (int k = 0; k < R.nStops && o < n; k++) o += snprintf(out + o, n - o, "stop %d %d %d %s\n", R.stop[k] % MR_W, R.stop[k] / MR_W, R.custom[k], R.name[k]);
  return o < n ? o : 0;
}
inline bool mrMissionRead(const char* s, MrReply* r) {   // -> the stops + limit, as if from the server
  memset(r, 0, sizeof *r);
  if (strncmp(s, "MARTE 1\n", 8)) return false;
  r->lim = 20;
  for (const char* line = s; *line; ) {
    const char* end = strchr(line, '\n');
    int len = end ? (int)(end - line) : (int)strlen(line);
    char ln[96];
    int n = len < (int)sizeof ln - 1 ? len : (int)sizeof ln - 1, x, y, c, used = 0;
    memcpy(ln, line, n); ln[n] = 0;                // (one line at a time: a name never runs into the next one)
    if (n > 0 && ln[n - 1] == '\r') ln[--n] = 0;
    if (!strncmp(ln, "lim ", 4)) r->lim = atoi(ln + 4);
    else if (!strncmp(ln, "stop ", 5) && r->nStops < MR_STOPS && sscanf(ln + 5, "%d %d %d %n", &x, &y, &c, &used) == 3 && used) {
      if (x < 0 || y < 0 || x >= MR_W || y >= MR_H) return false;
      int k = r->nStops++;
      r->stop[k] = (uint16_t)(y * MR_W + x); r->custom[k] = c != 0;
      snprintf(r->name[k], MR_NAME, "%s", ln + 5 + used);
      if (!r->name[k][0]) snprintf(r->name[k], MR_NAME, "punto %d", k + 1);
    }
    line = end ? end + 1 : line + len;
  }
  return r->nStops >= 2;
}
#if defined(ESP_PLATFORM)
#pragma GCC pop_options
#endif
