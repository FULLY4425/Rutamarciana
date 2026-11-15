#line 1 "/home/fox/.hermes/kanban/workspaces/t_7658b621/spaceapps/psion-micor-spaceapps/mision_map_contract.h"
// =====================================================================
// mision_map_contract.h — CONTRATO entre el mapa+rutas (compañero) y el
// presupuesto EVA (Space Apps / Psion Micor). UNIDAD: SEGUNDOS (float).
//
// CÓMO SE CONECTA (sin tocar este archivo):
//   El módulo del mapa llama, una vez tras calcular sus rutas:
//       misionMapSetProvider(miFuncion);
//   donde
//       bool miFuncion(mision::RouteInput* out, int variant);
//   debe llenar `out` con los datos de la variante `variant`
//   (0 = FAST, 1 = SAFE — nombres provisionales, a confirmar por el
//   equipo) y devolver true. Si devuelve false o no hay proveedor,
//   la pantalla de presupuesto usa los valores manuales editables
//   (persistidos en /eva.cfg). NADA del presupuesto asume endpoints
//   ni nombres definitivos del mapa.
// =====================================================================
#ifndef MISION_MAP_CONTRACT_H
#define MISION_MAP_CONTRACT_H

namespace mision {

const int MAX_STOPS = 8;                 // misma capacidad que eva::Input
enum Variant { FAST = 0, SAFE = 1 };

struct RouteInput {
  bool  valid;                           // false = el mapa aún no tiene esta variante
  int   variant;                         // 0 FAST / 1 SAFE (provisional)
  float travel_out_s;                    // ida (segundos)
  float travel_back_s;                   // vuelta planificada (segundos)
  int   n_stops;                         // paradas científicas de la ruta
  float dwell_s[MAX_STOPS];              // ciencia por parada (segundos)
};

typedef bool (*MapProvideFn)(RouteInput* out, int variant);

// Punto de conexión único: el mapa registra su proveedor; el presupuesto
// lo consulta. Sin registro, la UI marca "mapa: pendiente" y usa /eva.cfg.
inline MapProvideFn mapProvider = nullptr;

inline void setMapProvider(MapProvideFn fn) { mapProvider = fn; }

inline bool provide(RouteInput* out, int variant) {
  if (!mapProvider || !out) return false;
  out->valid = false;
  return mapProvider(out, variant) && out->valid;
}

} // namespace mision
#endif
