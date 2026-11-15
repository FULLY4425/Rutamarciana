// Pruebas de host para eva_budget.h (núcleo puro del presupuesto EVA)
#include "../psion-micor-spaceapps/eva_budget.h"
#include <cmath>
#include <cstdio>

static int fails = 0, total = 0;
#define CHECK(cond, name) do { total++; if (!(cond)) { fails++; printf("FALLO: %s\n", name); } } while (0)

using namespace eva;

static Input mk(float out, float back, int ns, const float* dwell, float ovh, float res, float lim) {
  Input i{};
  i.travel_out_s = out; i.travel_back_s = back; i.n_stops = ns; i.overhead_s = ovh;
  i.reserve_s = res; i.limit_s = lim;
  for (int k = 0; k < ns && dwell && k < MAX_STOPS; k++) i.dwell_s[k] = dwell[k];
  return i;
}

int main() {
  Result r{};

  // 1) GO básico: ida 600 s + 3 paradas de 600 s + vuelta 600 s + overhead 300 s = 3300;
  //    límite 5400, reserva 900 -> usable 4500; margin 2100 >= 900 -> GO.
  { float d[3] = {600, 600, 600};
    Input i = mk(600, 600, 3, d, 300, 900, 5400);
    CHECK(compute(&i, &r) == OK, "GO: sin error");
    CHECK(r.valid && r.status == GO, "GO: estado");
    CHECK(fabsf(r.total_s - 3300) < 0.01f, "GO: total = ida+vuelta+dwell+overhead");
    CHECK(fabsf(r.science_s - 1800) < 0.01f, "GO: suma de dwell");
    CHECK(fabsf(r.travel_s - 1200) < 0.01f, "GO: travel = ida + vuelta");
    CHECK(fabsf(r.margin_s - 2100) < 0.01f, "GO: margen bruto");
    CHECK(fabsf(r.margin_usable_s - 1200) < 0.01f, "GO: margen sin reserva"); }

  // 2) JUST exacto: total > usable pero <= límite.
  //    ida 600 + vuelta 600 + 1 parada 600 + overhead 0 = 1800; límite 2000, reserva 300:
  //    margin 200 < 300 -> JUST.
  { float d[1] = {600};
    Input i = mk(600, 600, 1, d, 0, 300, 2000);
    compute(&i, &r);
    CHECK(r.status == JUST, "JUST: total>usable, total<=limite"); }

  // 3) NO-GO: total > límite.
  { float d[1] = {600};
    Input i = mk(600, 600, 1, d, 0, 0, 1799.9f);
    compute(&i, &r);
    CHECK(r.status == NOGO, "NOGO: total > limite"); }

  // 4) Límites exactos en la frontera (con reserva 0): total == límite -> GO (margen 0 >= 0).
  { float d[1] = {600};
    Input i = mk(600, 600, 1, d, 0, 0, 1800);
    compute(&i, &r);
    CHECK(r.status == GO, "borde: total==limite, reserva 0 -> GO"); }
  //    Con reserva > 0 y total == límite: margen 0 < reserva -> JUST.
  { float d[1] = {600};
    Input i = mk(600, 600, 1, d, 0, 300, 1800);
    compute(&i, &r);
    CHECK(r.status == JUST, "borde: total==limite, reserva>0 -> JUST"); }
  //    Frontera GO/JUST: margin == reserve exacto -> GO.
  { float d[1] = {600};
    Input i = mk(600, 600, 1, d, 0, 200, 1800);   // total 1800, margin 0 < 200 -> JUST
    compute(&i, &r);
    CHECK(r.status == JUST, "borde: margin 0 < 200 -> JUST");
    Input j = mk(500, 500, 1, d, 0, 200, 1800);   // total 1600, margin 200 == reserve -> GO
    compute(&j, &r);
    CHECK(r.status == GO, "borde: margin==reserve -> GO"); }

  // 5) Unidades: los campos son segundos; 90 min de EVA = 5400 s. Los minutos
  //    se convierten FUERA del núcleo (así lo hace la UI). Verificación de
  //    identidad: lo que entra en s sale en s.
  { float d[2] = {1200, 900};
    Input i = mk(900, 900, 2, d, 600, 1800, 7200);
    compute(&i, &r);
    CHECK(fabsf(r.total_s - 4500) < 0.01f, "unidades: total en segundos");
    CHECK(fabsf(r.usable_s - 5400) < 0.01f, "unidades: usable = limite - reserva"); }

  // 6) Campos ausentes/inválidos.
  { float d0[1] = {0};
    Input i = mk(600, 600, 1, d0, 0, 0, 0);                // límite ausente (0)
    int e = compute(&i, &r);
    CHECK(e == ERR_LIMIT && !r.valid, "invalido: limite <= 0 -> ERR_LIMIT"); }
  { float d0[1] = {0};
    Input i = mk(-1, 600, 1, d0, 0, 0, 3600);              // negativo
    compute(&i, &r);
    CHECK(!r.valid && r.error == ERR_NEGATIVE, "invalido: tiempo negativo"); }
  { float d8[MAX_STOPS] = {0};
    Input i = mk(600, 600, MAX_STOPS + 1, d8, 0, 0, 3600);
    compute(&i, &r);
    CHECK(!r.valid && r.error == ERR_NSTOPS, "invalido: n_stops fuera de rango"); }
  { float d[1] = {NAN};
    Input i = mk(600, 600, 1, d, 0, 0, 3600);
    compute(&i, &r);
    CHECK(!r.valid && r.error == ERR_NONFINITE, "invalido: NaN en dwell"); }
  { Input i = mk(INFINITY, 0, 0, nullptr, 0, 0, 3600);
    compute(&i, &r);
    CHECK(!r.valid && r.error == ERR_NONFINITE, "invalido: Inf en ida"); }
  { CHECK(compute(nullptr, &r) == ERR_NULL, "invalido: input nulo"); }

  // 7) Cero paradas y cero reserva son válidos (caminata de reconocimiento).
  { Input i = mk(1200, 1200, 0, nullptr, 300, 0, 3000);
    compute(&i, &r);
    CHECK(r.valid && r.status == GO && fabsf(r.science_s) < 0.01f, "0 paradas: valido"); }

  // 8) Ruta FAST / SAFE (dos variantes del mapa, mismas unidades):
  //    FAST ataja (ida+vuelta 800) pero sobrevuelo más caro de overhead;
  //    SAFE rodea (1600) con overhead menor. Ambas evaluables juntas.
  { float d[2] = {900, 900};
    Input fast = mk(400, 400, 2, d, 900, 600, 5400);
    Input safe = mk(800, 800, 2, d, 300, 600, 5400);
    Result rf{}, rs{};
    compute(&fast, &rf); compute(&safe, &rs);
    CHECK(rf.valid && rs.valid, "FAST/SAFE: ambas válidas");
    CHECK(rf.status == GO && rs.status == GO, "FAST/SAFE: ambas GO en este caso");
    CHECK(rf.total_s < rs.total_s, "FAST/SAFE: FAST total menor");
    CHECK(fabsf(rf.science_s - rs.science_s) < 0.01f, "FAST/SAFE: ciencia igual"); }

  // 9) Precisión float: 8 paradas sumadas sin deriva grosera.
  { float d[MAX_STOPS] = {123, 456, 789, 321, 654, 987, 147, 258};
    Input i = mk(0, 0, MAX_STOPS, d, 0, 0, 100000);
    compute(&i, &r);
    float sum = 0; for (int k = 0; k < MAX_STOPS; k++) sum += d[k];
    CHECK(fabsf(r.science_s - sum) < 0.01f, "8 paradas: suma exacta"); }

  printf("%d/%d pruebas OK\n", total - fails, total);
  return fails ? 1 : 0;
}
