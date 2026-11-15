#line 1 "/home/fox/.hermes/kanban/workspaces/t_7658b621/spaceapps/psion-micor-spaceapps/bitacora_core.h"
// =====================================================================
// bitacora_core.h — núcleo PURO de BITACORA (Psion Micor / Space Apps)
// Sin dependencias de Arduino: mismo código en el ESP32 y en pruebas de
// host (g++). La UI y el LittleFS viven en el .ino; aquí solo el formato.
//
// FORMATO DEL ARCHIVO DE CAMINATA (LittleFS /b<slot>.txt) — legado v1
// conservado a propósito: el relay ya lo convierte a JSON.
//   fecha=2026-11-14
//   sync=0                  (0 = pendiente de enviar, 1 = sincronizada)
//   titulo=Caminata 3
//   id=1<chipid 10d><seq 10d>   (21 dígitos, identidad global única)
//   HH:MM:SS|parada=START|tipo=ROCA|accion=MUESTRA|estado=NORMAL|otros=...
//   ...una línea por entrada...
// Delimitador '|' y 'k=v': parseable sin ambigüedad de CSV. Los campos
// seleccionados por picker nunca llevan '|' ni '='; el texto libre
// ("otros") se sanea aquí, no en la UI.
// =====================================================================
#ifndef BITACORA_CORE_H
#define BITACORA_CORE_H

#include <cstddef>
#include <cstdio>
#include <cstring>

namespace btx {

const int MAX_FILE     = 3000;      // bytes máx por caminata (igual que el legado)
const int MAX_OTROS = 100;       // texto libre de una entrada
const int MAX_LINE  = 200;       // línea de entrada bien formada, con margen

enum { ERR_NONE = 0, ERR_LIMIT = 1, ERR_LINE = 2 };

struct Entry {
  const char* hora;      // "HH:MM:SS" (8 chars, la UI la toma del reloj)
  const char* parada;
  const char* tipo;
  const char* accion;
  const char* estado;
  const char* otros;     // "" si no hay
};

// ---- saneo: '|' '=' \n \r y control -> ' '; siempre termina en '\0' ----
inline void sanitize(const char* src, char* out, size_t cap) {
  size_t n = 0;
  if (cap) out[0] = 0;
  if (!src) return;
  for (size_t i = 0; src[i]; i++) {
    unsigned char c = (unsigned char)src[i];
    if (c == '|' || c == '=' || c == '\n' || c == '\r' || c < 32) c = ' ';
    if (n + 1 < cap) out[n++] = (char)c;      // trunca silencioso (cabe en la pantalla)
  }
  out[n] = 0;
}

inline bool validIdentity(const char* id);                // definida abajo (headerBuild la usa)

// ---- una entrada del cuerpo; devuelve la longitud escrita o -1 ----
// Reglas: hora vacía se normaliza a 00:00:00; los 4 campos de picker son
// OBLIGATORIOS (no nulos ni vacíos); "otros" puede faltar.
inline int lineBuild(const Entry* e, char* out, size_t cap) {
  if (!e || !out || cap < 2) return -1;
  if (!e->parada || !e->parada[0] || !e->tipo || !e->tipo[0] ||
      !e->accion || !e->accion[0] || !e->estado || !e->estado[0]) return -1;
  char p[48], t[48], a[48], s[48], o[MAX_OTROS + 8];
  sanitize(e->parada, p, sizeof p);
  sanitize(e->tipo,   t, sizeof t);
  sanitize(e->accion, a, sizeof a);
  sanitize(e->estado, s, sizeof s);
  sanitize(e->otros,  o, sizeof o);
  int n = snprintf(out, cap, "%.8s|parada=%s|tipo=%s|accion=%s|estado=%s|otros=%s\n",
                   e->hora && e->hora[0] ? e->hora : "00:00:00", p, t, a, s, o);
  if (n < 0 || (size_t)n >= cap) { if (cap) out[0] = 0; return -1; }
  return n;
}

// ---- encabezado de una caminata nueva ----
inline int headerBuild(const char* fecha, const char* titulo, const char* identity,
                       char* out, size_t cap) {
  if (!out || cap < 2) return -1;
  char f[32], t[64], id[32];
  sanitize(fecha,   f, sizeof f);
  sanitize(titulo,  t, sizeof t);
  sanitize(identity, id, sizeof id);
  if (!validIdentity(id)) return -1;                      // identidad obligatoria y válida
  int n = snprintf(out, cap, "fecha=%s\nsync=0\ntitulo=%s\nid=%s\n", f, t, id);
  if (n < 0 || (size_t)n >= cap) { out[0] = 0; return -1; }
  return n;
}

// ---- identidad: 21 dígitos empezando en '1' (chip + contador) ----
inline bool validIdentity(const char* id) {
  if (!id || strlen(id) != 21 || id[0] != '1') return false;
  for (int i = 1; id[i]; i++) if (id[i] < '0' || id[i] > '9') return false;
  return true;
}

// ---- índice donde empieza el cuerpo de entradas, o -1 ----
inline int bodyStart(const char* data) {
  if (!data) return -1;
  int p = -1;
  for (int i = 0; i < 3; i++) {                           // fecha, sync, titulo
    const char* q = strchr(data + p + 1, '\n');
    if (!q) return -1;
    p = (int)(q - data);
  }
  p++;
  if (strncmp(data + p, "id=", 3) == 0) {                 // línea de identidad (opcional en legado)
    const char* q = strchr(data + p, '\n');
    if (!q) return -1;
    p = (int)(q - data) + 1;
  }
  return p;                                               // p == strlen(w) si el cuerpo está vacío
}

// ---- identidad guardada en la caminata ("" si el archivo no la trae) ----
inline int identityOf(const char* data, char* out, size_t cap) {
  out[0] = 0;
  int p = -1;
  for (int i = 0; i < 3; i++) {
    const char* q = strchr(data + p + 1, '\n');
    if (!q) return 0;
    p = (int)(q - data);
  }
  p++;
  if (strncmp(data + p, "id=", 3) != 0) return 0;
  const char* e = strchr(data + p, '\n');
  size_t n = e ? (size_t)(e - (data + p) - 3) : strlen(data + p) - 3;
  if (n >= cap) n = cap - 1;
  memcpy(out, data + p + 3, n);
  out[n] = 0;
  return validIdentity(out) ? (int)n : 0;
}

// ---- fecha (línea 1) ----
inline int dateOf(const char* data, char* out, size_t cap) {
  out[0] = 0;
  if (!data || strncmp(data, "fecha=", 6) != 0) return 0;
  const char* e = strchr(data + 6, '\n');
  size_t n = e ? (size_t)(e - (data + 6)) : strlen(data + 6);
  if (n >= cap) n = cap - 1;
  memcpy(out, data + 6, n);
  out[n] = 0;
  return (int)n;
}

inline int entryCount(const char* data) {
  int p = bodyStart(data);
  if (p < 0) return 0;
  int c = 0;
  for (int i = p; data[i]; i++) if (data[i] == '\n') c++;
  return c;
}

// ---- k-ésima línea del cuerpo (0-based) en out ----
inline bool entryAt(const char* data, int k, char* out, size_t cap) {
  out[0] = 0;
  int p = bodyStart(data);
  if (p < 0 || k < 0) return false;
  for (int i = 0; i < k; i++) {
    const char* q = strchr(data + p, '\n');
    if (!q) return false;
    p = (int)(q - data) + 1;
  }
  const char* e = strchr(data + p, '\n');
  size_t n = e ? (size_t)(e - (data + p)) : strlen(data + p);
  if (n >= cap) n = cap - 1;
  memcpy(out, data + p, n);
  out[n] = 0;
  return true;
}

// ---- valor de "clave=" en una línea del cuerpo ----
inline bool fieldOf(const char* line, const char* key, char* out, size_t cap) {
  out[0] = 0;
  char mark[40];
  snprintf(mark, sizeof mark, "|%s=", key);
  const char* p = strstr(line, mark);
  if (!p) return false;
  p += strlen(mark);
  const char* e = strchr(p, '|');
  size_t n = e ? (size_t)(e - p) : strlen(p);
  if (n >= cap) n = cap - 1;
  memcpy(out, p, n);
  out[n] = 0;
  return true;
}

// ---- archivo bien formado (lo que btLoad exige antes de editar) ----
inline bool validWalk(const char* data) {
  if (!data || strncmp(data, "fecha=", 6) != 0) return false;
  if (!strstr(data, "\nsync=")) return false;
  if (bodyStart(data) < 0) return false;
  char id[32];
  return identityOf(data, id, sizeof id) == 21;
}

// ---- bandera sync in-place (posicional: la línea 2 SIEMPRE es sync=x) ----
inline void setSyncFlag(char* data, bool synced) {
  char* p = strstr(data, "\nsync=");
  if (p && p[6]) p[6] = synced ? '1' : '0';               // p[6] = la bandera tras "sync="
}

inline bool syncFlag(const char* data) {
  const char* p = strstr(data, "\nsync=");
  return p && p[6] == '1';
}

// ---- agregar una entrada respetando MAX_FILE; err = ERR_* ----
inline bool appendEntry(char* data, size_t cap, const Entry* e, int* err) {
  if (err) *err = ERR_NONE;
  size_t len = strlen(data);
  if (len + 1 > (size_t)MAX_FILE || cap < MAX_FILE + 1) { if (err) *err = ERR_LIMIT; return false; }
  char line[MAX_LINE];
  int n = lineBuild(e, line, sizeof line);
  if (n < 0) { if (err) *err = ERR_LINE; return false; }
  if (len + (size_t)n > (size_t)MAX_FILE) { if (err) *err = ERR_LIMIT; return false; }
  memcpy(data + len, line, (size_t)n + 1);
  setSyncFlag(data, false);                               // una entrada nueva = pendiente
  return true;
}

// ---- export JSON (mismo contrato que el legado /api/bitacora) ----
inline void jsonQuote(const char* s, char* out, size_t cap) {
  size_t n = 0;
  out[0] = 0;
  if (!s) return;
  for (size_t i = 0; s[i] && n + 2 < cap; i++) {
    unsigned char c = (unsigned char)s[i];
    if (c == '"' || c == '\\') { if (n + 3 < cap) { out[n++] = '\\'; out[n++] = (char)c; } }
    else if (c < 32) { if (n + 2 < cap) out[n++] = ' '; }
    else out[n++] = (char)c;
  }
  out[n] = 0;
}

inline int jsonExport(const char* data, char* out, size_t cap) {
  if (!out || cap < 8) return -1;
  char fecha[32], id[32];
  if (!validWalk(data)) return -1;
  dateOf(data, fecha, sizeof fecha);
  identityOf(data, id, sizeof id);
  char qi[64], qf[64];
  jsonQuote(id, qi, sizeof qi);
  jsonQuote(fecha, qf, sizeof qf);
  int n = snprintf(out, cap, "{\"id\":\"%s\",\"fecha\":\"%s\",\"entradas\":[", qi, qf);
  if (n < 0 || (size_t)n >= cap) { out[0] = 0; return -1; }
  int count = entryCount(data), used = n;
  for (int k = 0; k < count; k++) {
    char line[MAX_LINE], hora[12], val[6][64];
    entryAt(data, k, line, sizeof line);
    fieldOf(line, "parada", val[0], sizeof val[0]);
    fieldOf(line, "tipo",   val[1], sizeof val[1]);
    fieldOf(line, "accion", val[2], sizeof val[2]);
    fieldOf(line, "estado", val[3], sizeof val[3]);
    fieldOf(line, "otros",  val[4], sizeof val[4]);
    char q[5][130];
    jsonQuote(val[0], q[0], sizeof q[0]); jsonQuote(val[1], q[1], sizeof q[1]);
    jsonQuote(val[2], q[2], sizeof q[2]); jsonQuote(val[3], q[3], sizeof q[3]);
    jsonQuote(val[4], q[4], sizeof q[4]);
    memcpy(hora, line, 8); hora[8] = 0;
    char qh[24]; jsonQuote(hora, qh, sizeof qh);
    int m = snprintf(out + used, cap - used,
                     "%s{\"hora\":\"%s\",\"parada\":\"%s\",\"tipo\":\"%s\",\"accion\":\"%s\",\"estado\":\"%s\",\"otros\":\"%s\"}",
                     k ? "," : "", qh, q[0], q[1], q[2], q[3], q[4]);
    if (m < 0 || (size_t)m >= cap - used) { out[0] = 0; return -1; }
    used += m;
  }
  int m = snprintf(out + used, cap - used, "]}");
  if (m < 0 || (size_t)m >= cap - used) { out[0] = 0; return -1; }
  return used + m;
}

// ---- export CSV (CRLF, celdas con "" escapado) ----
inline void csvCell(const char* s, char* out, size_t cap) {
  size_t n = 0;
  out[0] = 0;
  if (!s) return;
  for (size_t i = 0; s[i] && n + 2 < cap; i++) {
    if (s[i] == '"') { if (n + 3 < cap) { out[n++] = '"'; out[n++] = '"'; } }
    else out[n++] = s[i];
  }
  out[n] = 0;
}

inline int csvExport(const char* data, char* out, size_t cap) {
  if (!out || cap < 40) return -1;
  if (bodyStart(data) < 0) return -1;
  int used = snprintf(out, cap, "hora,parada,tipo,accion,estado,otros\r\n");
  if (used < 0 || (size_t)used >= cap) { out[0] = 0; return -1; }
  int count = entryCount(data);
  for (int k = 0; k < count; k++) {
    char line[MAX_LINE], hora[12], v[6][64], c[6][130];
    entryAt(data, k, line, sizeof line);
    fieldOf(line, "parada", v[0], sizeof v[0]);
    fieldOf(line, "tipo",   v[1], sizeof v[1]);
    fieldOf(line, "accion", v[2], sizeof v[2]);
    fieldOf(line, "estado", v[3], sizeof v[3]);
    fieldOf(line, "otros",  v[4], sizeof v[4]);
    for (int j = 0; j < 5; j++) csvCell(v[j], c[j], sizeof c[j]);
    memcpy(hora, line, 8); hora[8] = 0;
    int m = snprintf(out + used, cap - used, "\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"\r\n",
                     hora, c[0], c[1], c[2], c[3], c[4]);
    if (m < 0 || (size_t)m >= cap - used) { out[0] = 0; return -1; }
    used += m;
  }
  return used;
}

} // namespace btx
#endif
