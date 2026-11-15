#line 1 "/home/fox/.hermes/kanban/workspaces/t_7658b621/spaceapps/psion-micor-spaceapps/eva_budget.h"
// =====================================================================
// eva_budget.h — núcleo PURO del presupuesto EVA (Space Apps / Psion Micor)
// Sin dependencias de Arduino: compila en el ESP32 y en el host (g++) para
// pruebas. Toda la ARITMÉTICA vive aquí; la pantalla vive en el .ino.
//
// CONTRATO DE DATOS (pendiente de confirmación con el compañero del mapa)
// ---------------------------------------------------------------------
// El mapa+rutas (compañero) es la fuente de la verdad del TRAYECTO. Este
// módulo NO recalcula rutas ni duplica su algoritmo: recibe los tiempos.
// Campos de entrada (unidades explícitas, SEGUNDOS):
//   travel_out_s   ida de base a la primera/última parada según la ruta
//   travel_back_s  regreso planificado
//   n_stops        número de paradas científicas (0..EVA_MAX_STOPS)
//   dwell_s[i]     duración científica por parada
//   overhead_s     overhead fijo del traje/COM (vestirse, despresurizar,
//                  transiciones de traje); no incluye la reserva
//   reserve_s      reserva de contingencia (NO se gasta en el plan)
//   limit_s        límite del EVA (consumible, p. ej. oxígeno útil)
// Variantes de ruta FAST / SAFE: el mapa puede entregar dos EvaInput
// (uno por variante) con los mismos campos; este núcleo no distingue
// nombres — el UI etiqueta cada resultado con "FAST"/"SAFE" a la espera
// de la confirmación final del contrato.
// =====================================================================
#ifndef EVA_BUDGET_H
#define EVA_BUDGET_H

#include <cstddef>

namespace eva {

const int MAX_STOPS = 8;                 // paradas científicas soportadas

enum Status { NOGO = 0, JUST = 1, GO = 2 };          // desglose.go / estado
enum Error {
  OK = 0,                              // resultado válido
  ERR_NULL = 1,                        // puntero nulo
  ERR_NONFINITE = 2,                   // NaN/Inf en algún campo
  ERR_NEGATIVE = 3,                    // algún tiempo negativo
  ERR_LIMIT = 4,                       // limit_s <= 0 (límite indefinido)
  ERR_NSTOPS = 5                       // n_stops fuera de 0..MAX_STOPS
};

struct Input {
  float travel_out_s;
  float travel_back_s;
  int   n_stops;
  float dwell_s[MAX_STOPS];
  float overhead_s;
  float reserve_s;
  float limit_s;
};

struct Result {
  // Desglose (solo válido si valid):
  float travel_s;                      // ida + vuelta
  float science_s;                     // suma de dwell
  float overhead_s;
  float total_s;                       // travel + science + overhead
  float limit_s;
  float reserve_s;
  float usable_s;                      // limit - reserve (presupuesto del plan)
  float margin_s;                      // limit - total (holgura bruta)
  float margin_usable_s;               // usable - total (holgura sin tocar reserva)
  int   status;                        // Status: NOGO / JUST / GO
  bool  valid;
  int   error;                         // Error (0 = OK)
};

// Estado exacto en los límites (congelado por los tests):
//   total >  limit              -> NOGO
//   total == limit              -> margen 0: GO si reserve == 0, JUST si reserve > 0
//   0 <= margin < reserve       -> JUST  (cabe, pero la reserva queda expuesta)
//   margin >= reserve           -> GO    (plan íntegro sin tocar la reserva)
// Es decir: JUST existe solo cuando reserve > 0 y el plan agota el margen.
inline bool fin(float v) {                 // finito y no NaN
  return v == v && v < 1e30f && v > -1e30f;
}

inline int compute(const Input* in, Result* out) {
  if (!in || !out) return ERR_NULL;
  // Validación de campos: ausente/inválido nunca produce números.
  const float vals[3] = { in->travel_out_s, in->travel_back_s, in->overhead_s };
  const float vals2[2] = { in->reserve_s, in->limit_s };
  for (int i = 0; i < 3; i++) if (!fin(vals[i]))  { out->valid = false; out->error = ERR_NONFINITE; return out->error; }
  for (int i = 0; i < 2; i++) if (!fin(vals2[i])) { out->valid = false; out->error = ERR_NONFINITE; return out->error; }
  if (in->n_stops < 0 || in->n_stops > MAX_STOPS) { out->valid = false; out->error = ERR_NSTOPS;   return out->error; }
  for (int i = 0; i < in->n_stops; i++)
    if (!fin(in->dwell_s[i])) { out->valid = false; out->error = ERR_NONFINITE; return out->error; }
  if (in->travel_out_s < 0 || in->travel_back_s < 0 || in->overhead_s < 0 ||
      in->reserve_s < 0 || in->limit_s < 0)        { out->valid = false; out->error = ERR_NEGATIVE;  return out->error; }
  for (int i = 0; i < in->n_stops; i++)
    if (in->dwell_s[i] < 0) { out->valid = false; out->error = ERR_NEGATIVE; return out->error; }
  if (in->limit_s <= 0) { out->valid = false; out->error = ERR_LIMIT; return out->error; }

  float travel = in->travel_out_s + in->travel_back_s;
  float science = 0.0f;
  for (int i = 0; i < in->n_stops; i++) science += in->dwell_s[i];
  float total = travel + science + in->overhead_s;

  out->travel_s = travel;
  out->science_s = science;
  out->overhead_s = in->overhead_s;
  out->total_s = total;
  out->limit_s = in->limit_s;
  out->reserve_s = in->reserve_s;
  out->usable_s = in->limit_s - in->reserve_s;
  out->margin_s = in->limit_s - total;
  out->margin_usable_s = out->usable_s - total;
  if (total > in->limit_s)                out->status = NOGO;
  else if (out->margin_s >= in->reserve_s) out->status = GO;
  else                                     out->status = JUST;
  out->valid = true;
  out->error = OK;
  return OK;
}

} // namespace eva
#endif
