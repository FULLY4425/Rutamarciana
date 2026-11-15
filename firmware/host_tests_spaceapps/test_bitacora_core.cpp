// Pruebas de host para bitacora_core.h (núcleo puro de BITACORA)
#include "../psion-micor-spaceapps/bitacora_core.h"
#include <cstdio>
#include <cstring>

static int fails = 0, total = 0;
#define CHECK(cond, name) do { total++; if (!(cond)) { fails++; printf("FALLO: %s\n", name); } } while (0)

using namespace btx;

int main() {
  char buf[MAX_FILE + 1];

  // ---- saneo de texto libre ("otros") ----
  { sanitize("hola|mundo", buf, sizeof buf);
    CHECK(strcmp(buf, "hola mundo") == 0, "sanitize: '|' fuera");
    sanitize("a=b\n\rc", buf, sizeof buf);
    CHECK(strcmp(buf, "a b  c") == 0, "sanitize: '=' y saltos fuera");
    char sm[8]; sanitize("1234567890", sm, sizeof sm);
    CHECK(strcmp(sm, "1234567") == 0, "sanitize: trunca a cap-1"); }

  // ---- construcción de línea de entrada ----
  { Entry e{"12:34:56", "S1", "ROCA", "MUESTRA", "NORMAL", "nota libre"};
    int n = lineBuild(&e, buf, sizeof buf);
    CHECK(n > 0 && strcmp(buf, "12:34:56|parada=S1|tipo=ROCA|accion=MUESTRA|estado=NORMAL|otros=nota libre\n") == 0,
          "lineBuild: formato exacto");
    Entry bad{"12:34:56", "S|1", "ROCA", "MUESTRA", "NORMAL", "x=y"};
    lineBuild(&bad, buf, sizeof buf);
    CHECK(strcmp(buf, "12:34:56|parada=S 1|tipo=ROCA|accion=MUESTRA|estado=NORMAL|otros=x y\n") == 0,
          "lineBuild: campos saneados"); }

  // ---- encabezado + identidad ----
  { int n = headerBuild("2026-11-14", "Caminata 0", "100000000012345678901", buf, sizeof buf);
    CHECK(n > 0, "headerBuild: escribe");
    CHECK(strcmp(buf, "fecha=2026-11-14\nsync=0\ntitulo=Caminata 0\nid=100000000012345678901\n") == 0,
          "headerBuild: formato exacto");
    CHECK(headerBuild("2026-11-14", "x", "corto", buf, sizeof buf) == -1, "headerBuild: rechaza identidad inválida");
    CHECK(validIdentity("100000000012345678901"), "identity: válida de 21 dígitos");
    CHECK(!validIdentity("200000000012345678901"), "identity: debe empezar en '1'");
    CHECK(!validIdentity("10000000001234567890X"), "identity: rechaza no dígito"); }

  // ---- parse del cuerpo ----
  { char w[MAX_FILE + 1];
    headerBuild("2026-11-14", "Caminata 0", "100000000012345678901", w, sizeof w);
    CHECK(bodyStart(w) == (int)strlen(w), "bodyStart: sin entradas -> fin");
    Entry e{"09:00:00", "START", "SUELO", "FOTO", "NORMAL", ""};
    CHECK(appendEntry(w, sizeof w, &e, nullptr), "append: entrada 1");
    Entry e2{"09:05:30", "S2", "VETA", "MEDICION", "CONTRATIEMPO", "replan"};
    CHECK(appendEntry(w, sizeof w, &e2, nullptr), "append: entrada 2");
    CHECK(entryCount(w) == 2, "entryCount: 2 entradas");
    char line[MAX_LINE], v[64];
    entryAt(w, 1, line, sizeof line);
    CHECK(fieldOf(line, "estado", v, sizeof v) && strcmp(v, "CONTRATIEMPO") == 0, "fieldOf: estado");
    CHECK(fieldOf(line, "otros", v, sizeof v) && strcmp(v, "replan") == 0, "fieldOf: otros");
    CHECK(!fieldOf(line, "noexiste", v, sizeof v), "fieldOf: clave ausente");
    CHECK(validWalk(w), "validWalk: caminata bien formada");
    char id[32], f[32];
    CHECK(identityOf(w, id, sizeof id) == 21 && strcmp(id, "100000000012345678901") == 0, "identityOf");
    CHECK(dateOf(w, f, sizeof f) && strcmp(f, "2026-11-14") == 0, "dateOf");
    CHECK(!syncFlag(w), "sync: nueva entrada la marca pendiente");
    setSyncFlag(w, true);
    CHECK(syncFlag(w), "sync: bandera a 1");
    setSyncFlag(w, false);
    CHECK(!syncFlag(w), "sync: bandera de vuelta a 0"); }

  // ---- límite de tamaño: no corromper, no perder lo anterior ----
  { char w[MAX_FILE + 1];
    headerBuild("2026-11-14", "Llena", "100000000012345678901", w, sizeof w);
    Entry e{"10:00:00", "S1", "ROCA", "MUESTRA", "NORMAL", ""};
    int err = 0, added = 0;
    while (appendEntry(w, sizeof w, &e, &err)) added++;
    CHECK(err == ERR_LIMIT, "límite: rechaza por ERR_LIMIT al llenar");
    CHECK(entryCount(w) == added, "límite: count coincide con lo aceptado");
    CHECK(strlen(w) <= MAX_FILE, "límite: nunca excede MAX_FILE bytes");
    CHECK(validWalk(w), "límite: el archivo sigue bien formado");
    // una entrada que por sí sola no cabe: se rechaza entera, sin escritura parcial
    char w2[MAX_FILE + 1];
    headerBuild("2026-11-14", "Llena2", "100000000012345678901", w2, sizeof w2);
    while (appendEntry(w2, sizeof w2, &e, nullptr)) {}
    size_t before = strlen(w2);
    int cnt = entryCount(w2), err2 = 0;
    char z[MAX_OTROS + 8]; memset(z, 'z', sizeof z - 1); z[sizeof z - 1] = 0;
    Entry huge{"11:01:00", "S1", "ROCA", "MUESTRA", "NORMAL", z};
    CHECK(!appendEntry(w2, sizeof w2, &huge, &err2) && err2 == ERR_LIMIT,
          "límite: entrada que no cabe se rechaza entera");
    CHECK(strlen(w2) == before && entryCount(w2) == cnt, "límite: sin escritura parcial"); }

  // ---- export JSON ----
  { char w[MAX_FILE + 1], out[MAX_FILE * 2];
    headerBuild("2026-11-15", "J", "100000000012345678901", w, sizeof w);
    Entry e{"07:15:00", "END", "POLVO", "SIN HALLAZGO", "PELIGRO", "com\"illas\\"};
    appendEntry(w, sizeof w, &e, nullptr);
    int n = jsonExport(w, out, sizeof out);
    CHECK(n > 0, "json: escribe");
    CHECK(strcmp(out, "{\"id\":\"100000000012345678901\",\"fecha\":\"2026-11-15\",\"entradas\":["
                      "{\"hora\":\"07:15:00\",\"parada\":\"END\",\"tipo\":\"POLVO\",\"accion\":\"SIN HALLAZGO\","
                      "\"estado\":\"PELIGRO\",\"otros\":\"com\\\"illas\\\\\"}]}") == 0,
          "json: contrato del relay exacto (id, fecha, entradas, escape)");
    char wbad[MAX_FILE + 1] = "no es una caminata";
    CHECK(jsonExport(wbad, out, sizeof out) == -1, "json: rechaza caminata inválida"); }

  // ---- export CSV ----
  { char w[MAX_FILE + 1], out[MAX_FILE * 2];
    headerBuild("2026-11-15", "C", "100000000012345678901", w, sizeof w);
    Entry e{"08:00:00", "S3", "IMAGEN", "FOTO", "REPLANIFICACION", "di, \"coma\""};
    appendEntry(w, sizeof w, &e, nullptr);
    int n = csvExport(w, out, sizeof out);
    CHECK(n > 0 && strstr(out, "hora,parada,tipo,accion,estado,otros"), "csv: encabezado");
    CHECK(strstr(out, "\"08:00:00\",\"S3\",\"IMAGEN\",\"FOTO\",\"REPLANIFICACION\",\"di, \"\"coma\"\"\"\r\n"),
          "csv: celdas escapadas y CRLF"); }

  // ---- reinicio y relectura (persistencia simulada en el buffer) ----
  { char w[MAX_FILE + 1];
    headerBuild("2026-11-16", "R", "100000000012345678901", w, sizeof w);
    Entry e{"06:00:00", "S1", "ROCA", "MUESTRA", "NORMAL", ""};
    appendEntry(w, sizeof w, &e, nullptr);
    char copy[MAX_FILE + 1];
    memcpy(copy, w, strlen(w) + 1);                    // simulación de flash persistente
    CHECK(validWalk(copy) && entryCount(copy) == 1, "reinicio: relectura intacta");
    char line[MAX_LINE], v[32];
    entryAt(copy, 0, line, sizeof line);
    CHECK(fieldOf(line, "parada", v, sizeof v) && strcmp(v, "S1") == 0, "reinicio: campo releyó"); }

  // ---- datos inválidos no entran ----
  { char w[MAX_FILE + 1];
    headerBuild("2026-11-16", "I", "100000000012345678901", w, sizeof w);
    Entry bad{"", "S1", "ROCA", "MUESTRA", "NORMAL", ""};   // hora vacía -> se normaliza
    CHECK(appendEntry(w, sizeof w, &bad, nullptr), "invalido: hora vacía se normaliza a 00:00:00");
    char line[MAX_LINE];
    entryAt(w, 0, line, sizeof line);
    CHECK(strncmp(line, "00:00:00|", 9) == 0, "invalido: hora por defecto");
    Entry nulls{"06:00:00", nullptr, nullptr, nullptr, nullptr, nullptr};
    int err = 0;
    CHECK(!appendEntry(w, sizeof w, &nulls, &err) && err == ERR_LINE, "invalido: campos nulos rechazados"); }

  printf("%d/%d pruebas OK\n", total - fails, total);
  return fails ? 1 : 0;
}
