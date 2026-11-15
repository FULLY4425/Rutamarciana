// =====================================================================
// pov_contract.h — CONTRATO OPCIONAL de GEOMETRÍA entre el mapa+rutas
// (compañero) y el POV del rover (Space Apps / Psion Micor).
//
// Por qué existe: mision_map_contract.h entrega TIEMPOS (RouteInput)
// pero no GEOMETRÍA. Para el POV hacen falta las coordenadas (u,v) de
// los waypoints sobre la ortofoto jezero_ortho_4m.png (500x500, 4 m/px).
// Este contrato es NUEVO y OPCIONAL: mision_map_contract.h NO se toca.
// El módulo del mapa registra su proveedor cuando esté listo:
//     povc::setPovProvider(miFuncion);
//     bool miFuncion(povc::PovInput* out, int variant);  // 0 FAST / 1 SAFE
// y debe llenar `out` y devolver true. Sin registro (o valid=false) el
// POV usa la ruta DEMO determinista de pov_core.h — NUNCA se presentan
// datos demo como si fueran del mapa.
//
// CONVENCIONES del contenido cuando valid=true:
//   - Orden: wpt[0] = START, wpt[1..n-2] = paradas S1..Sn, wpt[n-1] = END.
//   - u,v en px sobre la ortofoto (0..499; v crece hacia el SUR).
//   - t_s = tiempo ACUMULADO de llegada a ese waypoint (segundos).
//   - name sin acentos: "START", "S1".."S8", "END".
//   - speed_ms = velocidad nominal del recorrido (m/s, > 0).
// =====================================================================
#ifndef POV_CONTRACT_H
#define POV_CONTRACT_H

#include "mision_map_contract.h"          // MAX_STOPS (capacidad compartida)

#include <cstdint>

namespace povc {

const int MAX_WPT = mision::MAX_STOPS + 2;   // START + paradas + END (= 10)

struct Waypoint {
  int16_t u, v;                           // px sobre la ortofoto (0..499, v hacia el sur)
  float   t_s;                            // tiempo acumulado de llegada (s)
  char    name[16];                       // "START","S1".."S8","END" (sin acentos)
};

struct PovInput {
  bool  valid;                            // false = el mapa no tiene geometría todavía
  int   variant;                          // 0 FAST / 1 SAFE (provisional, como mision::)
  int   n;                                // waypoints usados (2..MAX_WPT)
  Waypoint wpt[MAX_WPT];
  float speed_ms;                         // velocidad nominal (m/s)
};

typedef bool (*PovProvideFn)(PovInput* out, int variant);

// Punto de conexión único (mismo patrón que mision::setMapProvider).
inline PovProvideFn povProvider = nullptr;

inline void setPovProvider(PovProvideFn fn) { povProvider = fn; }

inline bool provide(PovInput* out, int variant) {
  if (!povProvider || !out) return false;
  out->valid = false;
  return povProvider(out, variant) && out->valid;
}

} // namespace povc
#endif
