# Presupuesto EVA del escenario LUNAR-ANALOG-2026 (Jezero / Marte)

Documento de referencia del **modelo de presupuesto EVA** del backend y de la
API. Describe qué mide, con qué números, qué estados emite y qué no puede
afirmarse con ellos. La UI actual consume el presupuesto del backend; las capacidades
vigentes se documentan en [ESPECIFICACIONES.md](ESPECIFICACIONES.md) y [API.md](API.md).

**Contexto histórico:** las secciones 13–15 conservan gaps, desconocidos y evidencia de
una etapa anterior del backend. Sus observaciones sobre falta de commit, UI heredada,
`data/out` y ausencia de hashes no describen el estado completo actual del repositorio.
Jezero/Glen/MOLA incluyen procedencia y la interfaz fue actualizada después. Consultar
[CAMBIOS_IMPLEMENTADOS.md](CAMBIOS_IMPLEMENTADOS.md) para el estado vigente.

## 1. Qué es y qué no es

- **Es** una comprobación de factibilidad de un plan de travesía simulado sobre
  un DEM, bajo supuestos de movilidad, tiempo y reserva **declarados por el
  proyecto**.
- **No es** capacidad de un traje (marciano ni de otro tipo), ni consumibles,
  ni telemetría, ni un criterio oficial NASA, ni GO/NO-GO.
- **No** certifica transitabilidad real del terreno ni viabilidad de rescate.
- `site` es **Jezero / Marte**. Solo los parámetros de movilidad/tiempo/reserva
  usan datos lunares como analogía (`assumption_basis: "lunar_analogue"`),
  porque no existe un estándar marciano público comparable.

## 2. Identidad del escenario

| Campo | Valor | Nota |
|---|---|---|
| `scenario.profile` / `scenario.scenario_id` | `LUNAR-ANALOG-2026` | identificador interno y versionado; **no** es una ubicación lunar |
| `budget_model` (raíz) | `LUNAR-ANALOG-2026` | validado por el relay |
| `scenario.site` | `Jezero/Mars` \| `synthetic` \| `unknown` | ubicación real del DEM |
| `scenario.assumption_basis` | `lunar_analogue` | base de los supuestos numéricos |
| `scenario.type` | `project_scenario_assumption` | naturaleza declarada |
| `scenario.label` | `Jezero / Marte — supuestos EVA basados en analogía lunar` | etiqueta visible |

El identificador interno no debe usarse como nombre del sitio. Nunca rotular la
misión o la ruta como «escenario lunar».

## 3. Supuestos numéricos por defecto

Todos son **supuestos de escenario** (`project_assumption`), no capacidad de un
traje marciano ni umbral NASA. `scenario.defaults` los expone siempre; si el
usuario los sobrescribe, el valor efectivo está en `scenario.values` y la
entrada explícita en `scenario.overrides`.

| Parámetro (JSON) | Valor por defecto | Equivalente | Sensibilidad declarada | Estado |
|---|---:|---:|---|---|
| `suit_budget_s` | 14 400 s | **240 min** (4 h) | — | presupuesto de escenario del proyecto |
| `v_nom_flat_kmh` | 2.0 km/h | — | — | assumption/TBR de planificación para llano (según informe interno, no promedio universal) |
| `v_return_flat_kmh` | 2.0 km/h | — | — | supuesto conservador del proyecto para el tramo de regreso |
| `science_stop_s` | 1 200 s | **20 min** por parada | 600–2 700 s (**10–45 min**) | evidencia Apollo de tiempos de estación; no universal |
| `egress_s` | 900 s | 15 min | incluido en overhead | supuesto optimista |
| `ingress_s` | 900 s | 15 min | incluido en overhead | supuesto optimista |
| overhead total | 1 800 s | **30 min** (15+15) | 1 800–3 600 s (**30–60 min**) | optimista; el límite superior es sensibilidad, no default |
| `other_tasks_s` | 0 s | — | — | tareas no científicas del plan |
| `reserve_fixed_s` | 1 800 s | **30 min** | 1 800–3 600 s (**30–60 min**) | análogo Apollo; reserva fija mostrada aparte |
| `max_return_s` | 3 600 s | **60 min** | — | restricción de retorno contingente del escenario |
| `max_return_distance_m` | 2 000 m | **2 km** | — | restricción geométrica separada |

El antiguo `reserve_fraction = 0.15` **no se usa** para el presupuesto. El
parámetro `reserve_fraction` sigue en la firma de `plan_routes`/`write_manifest`
solo por compatibilidad: se valida el rango, se ignora y no determina la reserva.
`SUIT_FACTOR = 0.4` (en `pipeline/cost.py`) es únicamente una escala de
movilidad que normaliza Tobler para que el llano coincida con la velocidad
declarada (~2.01 km/h); se cancela en el cociente de `route.py` y **no** modela
consumibles. No confundir velocidad de marcha con capacidad de consumibles.

## 4. Fórmula del presupuesto

Por cada ruta (`fast` y `safe`), con `N = len(science_waypoints)`:

```
operational_s = egress_s
              + outbound_travel_s
              + N * science_stop_s
              + other_tasks_s
              + return_travel_s
              + ingress_s
```

La reserva **no** entra en `operational_s`:

```
slack_s = suit_budget_s - operational_s - reserve_fixed_s
margin_s = suit_budget_s - operational_s        # antes de reserva
used_s   = operational_s
```

Campos emitidos en `budget`: `operational_s`, `used_s`, `margin_s`,
`outbound_travel_s`, `return_travel_s`, `science_total_s`, `overhead_s`,
`other_tasks_s`, `reserve_fixed_s`, `reserve_s`, `slack_s`, `status`,
`fits_reserve`, `limiting_terms`, `science_stops`, `return_contingency`.

`overhead_s` es exactamente `egress_s + ingress_s`; `other_tasks_s` va separado
y no se suma al overhead. `science_total_s = N * science_stop_s` y se cobra una
sola vez. `return_travel_s` es el tramo de regreso planificado y también se
cobra una sola vez: el chequeo de contingencia no añade otro retorno ni otra
reserva. `limiting_terms` enumera los términos con mayor duración (puede haber
empates); no identifica un consumible limitante real.

## 5. Estados: cabe / justo / no_cabe

| Estado | Condición (valores sin redondear) | Significado |
|---|---|---|
| `cabe` | `slack_s >= 0` | el plan entra en el presupuesto **manteniendo** la reserva completa |
| `justo` | `slack_s < 0` y `operational_s <= suit_budget_s` | el plan entra en el presupuesto pero consume parte o toda la reserva |
| `no_cabe` | `operational_s > suit_budget_s` | el plan excede el presupuesto de escenario |

`fits_reserve` es `true` solo en `cabe`.

Reglas:

- La clasificación se hace con **segundos sin redondear**. El redondeo a 0.1 s
  es solo de presentación: dos rutas con `total_s` idéntico pueden tener estados
  distintos.
- Un residuo positivo ínfimo (p. ej. ~1e-9 s) cae en `justo`, nunca en `cabe`:
  la frontera es conservadora.
- Son **estados de factibilidad LUNAR-ANALOG**, no GO/NO-GO. No deben
  presentarse como aprobación/rechazo de misión ni como criterio NASA.
- Esta regla de bandas prevalece sobre los umbrales distintos del informe de
  investigación interno (que desplazaba la banda una reserva).

## 6. N = 0 paradas científicas

- `N = 0` ⇒ `science_total_s = 0` y no se cobra tiempo de ciencia.
- `science_stops` es siempre `len(science_waypoints)` (0 si la lista va vacía u
  omitida); es **solo salida**, nunca se acepta como entrada.
- Sin paradas, la contingencia de retorno se evalúa desde el **punto más
  alejado radialmente de la ruta planificada**, no desde el destino final.
- Con paradas, se evalúa desde **cada parada científica**, en el orden enviado.

## 7. Contingencia de retorno: dos capas distintas

### 7.1 Camino modelado (`return_contingency.waypoint_returns`)

- `path_method: "fastest_directed_terrain_path"`.
- Dijkstra dirigido sobre el grafo inverso de tiempo desde el hábitat (inicio),
  con pendiente, rugosidad y la **velocidad de retorno declarada**
  (`v_return_flat_kmh`).
- Por cada parada (o el punto más alejado si `N = 0`):
  `waypoint`, `points`, `distance_m`, `time_s`, `distance_status`,
  `time_status`.
- `distance_status` / `time_status` ∈ {`within_limit`, `exceeds_limit`} contra
  `max_return_distance_m` (2 km) y `max_return_s` (60 min), **por separado**.
- Es el **coste de camino** (path cost) del modelo, no la distancia recta.
- Exceder un límite en este camino **no demuestra** que no exista otra ruta que
  los cumpla; cumplirlos no valida terreno real ni asegura rescate.

### 7.2 Diagnóstico radial (objeto exterior)

- `radial_distance_m`: máxima distancia radial en planta de todos los puntos de
  la ruta planificada (ida + regreso), con `cell_m` y `cell_x_m`.
- `flat_radial_time_s = radial_distance_m * 3.6 / v_return_flat_kmh`: **proxy**
  a velocidad llana, no una ruta.
- `distance_status`: `exceeds_radial_limit` si `radial_distance_m >
  max_return_distance_m`; si no, `not_verified`. La distancia recta sí es cota
  inferior geométrica de la distancia a recorrer, por eso puede marcar exceso de
  radio.
- `time_proxy_status`: `exceeds_proxy_limit` / `within_proxy_limit`.
- `time_status`: siempre `not_verified` en esta capa. El proxy **no** es cota
  inferior del tiempo real: Tobler permite bajar más rápido que la velocidad
  llana. Caso reproducido en pruebas: retorno real 3 173.1 s frente a 3 780 s
  del proxy radial llano; el proxy marcaba exceso y el camino modelado no.
  `not_verified` no significa «cumple».

### 7.3 Prohibiciones de presentación

- **Prohibido presentar una distancia recta (radial o A–B) como ruta
  transitable.** Es una cota geométrica, no un camino.
- **Prohibido presentar `flat_radial_time_s` como tiempo de retorno** o como
  prueba de que el retorno no cabe.
- **Prohibido sumar la contingencia** al tiempo operacional o a la reserva fija:
  ya están contabilizados el retorno planificado y la reserva.
- **Prohibido convertir `cabe/justo/no_cabe` en GO/NO-GO** o atribuirlos a NASA.
- Si la UI muestra distancia plana, debe etiquetarla como aproximada y
  diferenciarla de la distancia por ruta.

## 8. API y contrato

### Endpoints

| Método | Ruta | Comportamiento |
|---|---|---|
| GET | `/health` | `{ok, files}` con los archivos del directorio de datos |
| GET | `/api/routes` | manifiesto v3 validado; **503** si falta, es v2 o el escenario es inconsistente |
| GET | `/api/manifest` | Catálogo reducido (`site`, `waypoints`, `routes.safe/fast`, `budget_s`, `science_s`, `science_stops`) para el cliente embebido; `?full=1` devuelve el JSON crudo. 404 si falta, 503 si está malformado o no es objeto; no valida el contrato |
| POST | `/api/route` | recalcula ambas rutas; ver abajo |
| POST | `/api/bitacora` | escribe `bitacora/<id>.json` de forma atómica; 400 si `id`, `fecha` o `entradas` son inválidos |
| GET | `/preview.png` | vista previa de pendiente; 404 si no existe |

### POST /api/route

```json
{"schema_version":3,"start":[2,2],"end":[497,497],
 "science_waypoints":[[100,200],[250,300]],
 "scenario_assumptions":{"science_stop_s":600}}
```

- `schema_version` es opcional; si se envía debe ser el entero `3`.
- `start` y `end` son `[row, col]` enteros, distintos, en rango y transitables.
- `science_waypoints` es una lista **ordenada**; omitida equivale a `[]`.
- `science_stops` en la petición se rechaza con **400**: no define ubicaciones.
- `scenario_assumptions` parte de `scenario.values` del manifiesto, aplica los
  overrides de la petición y **no modifica** `routes.json`.
- La respuesta lleva en la raíz: `schema_version`, `start`, `end`,
  `science_waypoints`, `science_stops`, `budget_model`, `scenario`, `cell_m`,
  `cell_x_m`, `grid`, `budget_s`, `science_s`, y `fast`/`safe` con su `budget`.
  En `GET /api/routes` las rutas están bajo `routes.fast`/`routes.safe`.
- Errores: **400** por cuerpo no-objeto, `schema_version` distinto de 3,
  coordenadas inválidas/duplicadas/intransitables, tramo imposible,
  `science_waypoints` no-lista y overrides inválidos. **503** por artefactos
  ausentes, v2 o incompletos.

### Validación de overrides

- Solo claves conocidas del perfil; claves extrañas ⇒ 400/`ValueError`.
- Booleanos rechazados (no se aceptan como 0/1).
- Valores no finitos, negativos o fuera de `[0, 1e9]` ⇒ rechazo; velocidades
  acotadas a `[1e-6, 1e6]` km/h. Son límites técnicos, no físicos.
- Enteros JSON gigantes se rechazan con **400** (no 500) y no producen
  `Infinity`/`NaN` en el presupuesto.
- Constantes JSON no estándar en `routes.json` se rechazan con 503.

### Procedencia y overrides

`scenario.provenance` describe **solo los defaults** y no atribuye overrides a
las fuentes. `scenario.overrides.build` registra las entradas explícitas del
build (incluidas las iguales al default) y `scenario.overrides.request` solo el
POST actual, que prevalece. El relay rechaza metadatos contradictorios o
incompletos con 503: detecta inconsistencias, no autentica archivos ni hashes.

## 9. Migración de artefactos

Un artefacto sirve si es del mismo build y cumple: `schema_version: 3`,
`cell_m`, `cell_x_m`, `dem_validity: "nodata-preserved-v1"`, `budget_model` y
`scenario` coherentes, y los tres NPY (`dem.npy`, `slope.npy`, `rough.npy`).
Versiones v2 o v3 parciales ⇒ 503: regenerar todo junto desde el DEM original;
no añadir marcadores a mano. Elegir paradas con `--science-waypoint ROW COL`
repetido y en orden, o ninguna.

**Estado verificado en el árbol de trabajo:** `data/out/routes.json` sigue en
`schema_version: 2` (build de 17:33, anterior al perfil). Con ese directorio:

- `GET /api/routes` → **503** `routes.json antiguo; regenera el pipeline`
- `POST /api/route` → **503** `routes.json legado o incompleto; ...`
- `GET /api/manifest` → **200** (catálogo reducido; `?full=1` para el JSON crudo, sin validar semántica)

Por tanto, para usar la app hay que regenerar `data/out` con el pipeline actual.
Manifiestos con overrides también requieren rebuild: el POST no reescribe el
archivo.

## 10. Pruebas (carril de pruebas, tarea 3)

Estado **verificado en esta sesión** sobre el árbol de trabajo:

```
.venv/bin/python -m pytest -q -p no:cacheprovider
→ 114 passed in 8.76s
```

Desglose por archivo (114 recolectados):

| Archivo | Tests |
|---|---:|
| `relay/tests/test_app.py` | 19 |
| `relay/tests/test_scenario_contract.py` | 15 |
| `tests/test_eva_budget_contract.py` | 21 |
| `tests/test_lunar_budget.py` | 13 |
| `relay/tests/test_roundtrip.py` | 12 |
| `tests/test_eva.py` | 9 |
| `tests/test_nodata.py` | 4 |
| `tests/test_cost.py` | 4 |
| `tests/test_terrain.py` | 3 |
| `tests/test_route.py` | 3 |
| `tests/test_physical_grid.py` | 3 |
| `tests/test_waypoints.py` | 2 |
| `tests/test_jezero_smoke.py` | 2 |
| `tests/test_contingency_path.py` | 2 |
| `tests/test_tiles.py` | 1 |
| `tests/test_bitacora_host.py` | 1 |

Cobertura relevante de las 23 pruebas añadidas (21 + 2):

- `N = 0/1/3` paradas, con delta exacto de duración por parada y `science_stops`
  como `len(waypoints)`.
- Overhead = egreso + ingreso, con `other_tasks_s` separado.
- Reserva exacta y seis casos de frontera `cabe/justo/no_cabe` con `slack_s`
  exacto, incluido 60.04 s de slack frente a 40 s de reserva sobre 100 s.
- Contingencia por camino vs recta: muro intransitable con camino de 82.43 m
  frente a 60 m en recta; `N = 0` usa el punto más alejado, no el final.
- Contrato v3: `schema_version`, `dem_validity`, `cell_m`/`cell_x_m`,
  `budget_model`, `defaults == values` sin overrides, `units` por parámetro.
- Overrides de build y de request separados; el archivo del manifiesto no cambia
  (comprobación por `sha256`); overrides inválidos → 400.
- Mensaje obligatorio: `note` contiene «not a Mars suit capacity» y
  «not NASA GO/NO-GO»; `path_note` incluye «not field validation».

Mutaciones del modelo probadas por el carril de pruebas (8 de 8 detectadas):
redondeo del slack, eliminación del ingreso, contingencia en línea recta,
ignorar el número de paradas, descartar overrides de request o de build,
eliminar el descargo de responsabilidad y cobrar la contingencia dentro del
tiempo operacional.

Smoke con DEM real: `tests/test_jezero_smoke.py` construye 500×500 a 4 m desde
`data/raw/jez_dtm_1m.tif`, valida el manifiesto v3 y ejercita `GET /api/routes`
y `POST /api/route` con paradas ordenadas. **Se auto-omite** (`skipif`) si falta
el TIFF de ~1.8 GB o `git`. En esta sesión no hubo omisiones: 114 passed.

## 11. Archivos cambiados en el árbol de trabajo

Sin commit, compartidos entre carriles; `web/` intacto
(`git status --short -- web/` vacío).

Modificados: `README.md`, `pipeline/build.py`, `pipeline/cost.py`,
`pipeline/manifest.py`, `pipeline/route.py`, `relay/app.py`,
`relay/tests/test_app.py`, `relay/tests/test_roundtrip.py`,
`tests/test_eva.py`, `tests/test_waypoints.py`.

Nuevos: `pipeline/scenario.py`, `relay/tests/test_scenario_contract.py`,
`tests/test_eva_budget_contract.py`, `tests/test_jezero_smoke.py`,
`tests/test_lunar_budget.py`, `tests/test_contingency_path.py`,
`tests/test_bitacora_host.py` y este documento
(`docs/eva_budget_lunar_analog.md`, que también añade `/api/bitacora` y la
sección de pruebas al README).

Untracked preexistentes, no tocados: `firmware/`, `reference/`.

Base: HEAD `b72d4a39c4f8cf67091a4d4610e2f00094638c10`.

## 12. Comandos

```bash
# suite y sanidad
.venv/bin/python -m pytest -q -p no:cacheprovider
.venv/bin/python -m compileall -q pipeline relay tests
git diff --check
git status --short -- web/          # debe salir vacío

# regenerar artefactos reales (necesario: data/out está en v2)
python -m pipeline.build --dem data/raw/jez_dtm_1m.tif --cell 4 --rows 500 --cols 500 \
  --out data/out --science-waypoint 100 200 --science-waypoint 200 300

# demo sintética para no tocar data/out
python -m pipeline.build --synth --rows 40 --cols 40 --out <scratch> \
  --science-waypoint 10 10 --science-waypoint 20 30
```

## 13. Registro histórico: gaps, límites y bloqueos

1. **`data/out` está obsoleto (v2).** `GET /api/routes` y POST devuelven 503
   hasta regenerar; verificado. No es un fallo del contrato, es un artefacto no
   regenerado.
2. **Sin capacidad de traje marciano pública.** `suit_budget_s = 240 min` es
   presupuesto de escenario; ningún número aquí mide consumibles reales.
3. **El retorno contingente no valida rescate.** Es el camino más rápido
   modelado; exceder un límite no prueba imposibilidad y cumplirlo no prueba
   viabilidad.
4. **El proxy radial no es cota inferior de tiempo** ni ruta; ver caso de
   bajada (3 173.1 s reales vs 3 780 s del proxy).
5. **La UI heredada contradice al backend** (recalcula con 15%, omite overhead y
   añade paradas no enviadas). Fuera de alcance por instrucción; consumir la API.
6. **No se validan hashes ni la procedencia del DEM**: el nombre
   `jez_dtm_1m` se etiqueta Jezero/Mars por convención.
7. **`reserve_fraction` es vestigial** en `plan_routes`/`write_manifest`: se
   ignora, pero sigue en la firma por compatibilidad. Riesgo de lectura errónea.
8. **Sin commit**: los cambios viven en el árbol de trabajo compartido.
9. **El modelo no modela radiación ni consumibles en la búsqueda**; la dosis es
   tiempo × tasa, mostrada aparte.
10. **Sin validación de terreno real** (ni ortofoto ni mapa de peligros): la
    rugosidad es un proxy del residuo del DEM.

## 14. Registro histórico: desconocidos / no verificado

- Comportamiento de la UI con el nuevo contrato (no se ejecutó ni modificó).
- Correspondencia del DEM con el terreno actual de Jezero (procedencia por
  convención de nombre, sin hash contra la fuente).
- Viabilidad de una ruta alternativa de retorno cuando el camino más rápido
  excede 2 km/60 min: no se busca ni se certifica.
- Sensibilidad combinada de todos los parámetros a la vez; solo se declaran
  rangos individuales.
- Umbrales de `justo` frente al informe: la regla del código prevalece y no se
  revalidó contra literatura.

## 15. Evidencia de la etapa de backend

- `pytest -q -p no:cacheprovider` → `114 passed in 8.76s`; recolección: 114.
- Build sintético 40×40 en scratch + `GET /api/routes` → 200; `budget` con
  `status: cabe`, `slack_s: 7304.18`, `operational_s: 5295.82`; POST con dos
  paradas ordenadas → 200.
- Con `data/out` actual: `GET /api/routes` → 503; `POST /api/route` → 503;
  `GET /api/manifest` → 200 (reducido) y `?full=1` → 200 (crudo).
- `git status --short -- web/` → vacío.
