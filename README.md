# Micor Field — MVP de mapa y presupuesto EVA

Generador local de rutas EVA simuladas: convierte un DEM en dos rutas de ida y
regreso (conservadora y rápida), las sirve por LAN y genera tiles de pendiente.
Ninguna ruta constituye una garantía o certificación de seguridad.

**Apps incluidas:** 1) mapa por capas + rutas · 2) presupuesto EVA de escenario (no GO/NO-GO).
Las apps 3–5 (reloj dual, tarjeta de llegada, bitácora) no están en este MVP.

## Arranque rápido

```bash
python3 -m venv .venv && . .venv/bin/activate
pip install -r requirements.txt

make demo          # genera rutas con terreno sintético (no necesita internet)
make serve         # http://localhost:8080
```

Abrir `http://localhost:8080/?demo=1` para la pantalla ya armada.

## Con datos reales

```bash
curl -L -C - -o data/raw/jez_dtm_1m.tif \
  "https://planetarymaps.usgs.gov/mosaic/mars2020_trn/HiRISE/JEZ_hirise_soc_006_DTM_MOLAtopography_DeltaGeoid_1m_Eqc_latTs0_lon0_blend40.tif"

python -m pipeline.build --dem data/raw/jez_dtm_1m.tif --rows 500 --cols 500 --out data/out \
  --budget-h 4 --science-waypoint 100 200 --science-min 20
```

## Endpoints del relay

- `GET /health` — estado y archivos disponibles.
- `GET /api/routes` — rutas precalculadas y metadatos.
- `GET /api/manifest` — catálogo reducido para el cliente embebido (site,
  waypoints, routes.safe/fast, budget_s, science_s, science_stops); `?full=1`
  devuelve el JSON crudo completo de `routes.json` (404 si falta;
  503 si está malformado o no es un objeto). No valida el contrato v3: usar
  `/api/routes` para consumir rutas validadas.
- `POST /api/route` — `{"start":[row,col],"end":[row,col]}` → dos rutas nuevas.
- `POST /api/bitacora` — `{id, fecha, entradas}` → escribe `bitacora/<id>.json`
  de forma atómica; 400 si `id`, `fecha` o `entradas` son inválidos.
- `GET /preview.png` — mapa de pendiente de la zona.

Especificación completa de supuestos, fórmula y estados:
[`docs/eva_budget_lunar_analog.md`](docs/eva_budget_lunar_analog.md).

### Contrato v3 para la UI

`POST /api/route` acepta por ejemplo:

```json
{"schema_version":3,"start":[2,2],"end":[497,497],"science_waypoints":[[100,200],[250,300]]}
```

`schema_version` es opcional en la petición, pero si se incluye debe ser 3.
Las coordenadas son pares enteros `[row,col]` de la grilla, distintos entre sí,
en rango y transitables. La lista es ordenada; si se omite es `[]`, sin ciencia.
No se generan posiciones automáticamente. El POST usa la lista enviada, no las
paradas del manifiesto. Una coordenada inválida, duplicada o un tramo imposible
produce HTTP 400; el error identifica el tramo imposible (1-indexado).

En la respuesta, `science_waypoints` conserva el orden y `science_stops` es
**solo salida**, calculado como `len(science_waypoints)`. `science_s` son
segundos por parada. Cada `fast` / `safe` contiene:

- `objective`: `fastest` o `conservative` (`safe` no certifica seguridad).
- `outbound`: ida A → paradas en orden → B; `outbound.legs` contiene los
  tramos consecutivos, cada uno con `points`, `time_s`, `distance_m`, pendiente,
  exposición y rugosidad. `outbound.points` concatena los tramos sin duplicar
  sus uniones. Mostrar también número y orden de las paradas.
- `return`: recorrido dirigido y optimizado B → A, sin actividades de ciencia;
  los mismos campos de métrica y `points`. La ruta puede cruzar una parada
  física sin realizar otra actividad allí.
- `time_s` es el tiempo total de desplazamiento ida + regreso, `distance_m`
  la suma de longitudes de aristas de ambos recorridos, no la distancia recta.
  Cada tramo y cada dirección tiene su propia distancia de aristas.
  `science_total_s = len(science_waypoints) * science_s`, una sola vez;
  `total_s` es tiempo operacional incluyendo overhead y otras tareas. Mostrar ida, regreso y total en cada
  tarjeta. `points` superior es únicamente la ida, por compatibilidad.
- `slope_max_deg`, `slope_exposure_m`, `rough_mean` agregan ambos recorridos;
  `dose_msv` es solo una estimación durante desplazamiento, no telemetría.

No se expone distancia directa A–B. Si la UI la calcula, debe diferenciarla de
la distancia por la ruta y etiquetarla como distancia plana aproximada, no como
geodésica exacta sin modelo y datum. Todos los totales se calculan a precisión
completa y luego se redondean: sumar los valores visibles puede diferir.

### Jezero / Marte — supuestos EVA basados en analogía lunar

El sitio y la ruta son **Jezero, Marte** (`site: "Jezero/Mars"`); únicamente
los supuestos de movilidad, tiempos y reserva usan datos lunares como analogía
(`assumption_basis: "lunar_analogue"`), por falta de un estándar marciano público.
No describen capacidad real de un traje marciano. `LUNAR-ANALOG-2026` es
únicamente el identificador interno y versionado del perfil, no la ubicación.
El DEM de demostración se etiqueta `synthetic` y otros nombres de DEM se
etiquetan `unknown`; el nombre de archivo conocido `jez_dtm_1m` se etiqueta
Jezero/Mars por convención, sin verificar la procedencia del contenido.

`scenario.values` contiene todos los valores efectivos: Suit budget `suit_budget_s`
= 14 400 s (240 min), velocidad nominal llana `v_nom_flat_kmh` = 2, velocidad
llana de retorno `v_return_flat_kmh` = 2 km/h, `science_stop_s` = 1 200 s,
`egress_s` = `ingress_s` = 900 s, `other_tasks_s` = 0,
`reserve_fixed_s` = 1 800 s, `max_return_s` = 3 600 s y
`max_return_distance_m` = 2 000 m. Son supuestos del proyecto, no capacidad
de un traje marciano ni criterio oficial NASA. Artemis ConOps declara 2 km/h
como assumption/TBR para llano, no promedio universal. 20 min/parada se
inspira en evidencia Apollo, no es universal. Los 15+15 min son optimistas.
Sensibilidad sugerida: parada 10–45 min, overhead 30–60 min y reserva 30–60 min.
El 15% anterior no se usa. Apollo 14 observó 2,47 km/h solo caminando;
progreso efectivo EVA caminada 0,10–0,37 km/h. El walkback de 6,34 km/h fue
un ensayo optimista, no el default.

Se sobrescriben parámetros con `--scenario-json '{"reserve_fixed_s":3600}'`
en el build, o con `"scenario_assumptions":{"science_stop_s":600}` en POST;
POST parte de los valores del manifiesto, sin modificarlo. `--budget-h` y
`--science-min` también sobrescriben el perfil. El relay rechaza claves
extrañas, no finitos, negativos y extremos numéricos: cada valor debe estar entre
0 y 10⁹; las velocidades entre 10⁻⁶ y 10⁶ km/h. Son límites técnicos, no físicos.
El manifiesto y la respuesta
exponen `scenario.defaults` (perfil versionado), `scenario.values` (valores
efectivos), `scenario.units` (unidad por parámetro), `scenario.provenance`,
`scenario.note` y `scenario.sensitivity`. `scenario.overrides.build` registra
las entradas explícitas del build, incluso las iguales al default;
`scenario.overrides.request` registra solo el POST actual y prevalece sobre el
build. Para llamadas directas a `write_manifest` que omiten `scenario_overrides`,
se infieren solo las diferencias respecto al default, no la intención original.
La procedencia describe los **defaults**, no atribuye overrides a las fuentes.
El relay rechaza metadatos de escenario contradictorios o incompletos con 503;
esto detecta inconsistencias, no autentica los archivos. `science_stops`
siempre es `len(science_waypoints)`.

Por ruta, `budget.operational_s = egress + outbound_travel + N*science_stop +
other_tasks + return_travel + ingress`; `reserve_fixed_s` aparece aparte y
`slack_s = suit_budget_s - operational_s - reserve_fixed_s`. `cabe`: slack ≥ 0;
`justo`: slack < 0 pero operacional ≤ presupuesto; `no_cabe`: operacional >
presupuesto. La clasificación usa valores sin redondear. `limiting_terms` enumera
los términos de mayor duración, no implica identificación de consumible limitante.
Con `N = 0` paradas el término de ciencia es exactamente 0 y no se añade tiempo
de parada. `cabe/justo/no_cabe` son factibilidad de escenario LUNAR-ANALOG, no
GO/NO-GO ni criterio NASA; no deben mostrarse como aprobación o rechazo de
misión. Esta regla prevalece sobre los umbrales diferentes del informe de
investigación.

`return_contingency.waypoint_returns` contiene el camino de retorno más rápido
modelado desde cada parada científica, o desde el punto más alejado radialmente
si no hay paradas. Dijkstra dirigido usa pendiente, rugosidad y velocidad de
retorno declarada; sus distancias y tiempos se comparan separadamente con los
límites de 2 km y 60 min. Exceder un límite en ese camino no demuestra que no
exista otra ruta que cumpla ambos; cumplirlos no valida el terreno real ni
asegura rescate. Es una comprobación de modelo, no una garantía operacional.

Adicionalmente se inspecciona la máxima **distancia radial en planta** de ambas
rutas. `flat_radial_time_s` es un proxy calculado con velocidad llana, no una
ruta ni cota inferior del tiempo. Los estados `distance_status`, `time_status`
y `time_proxy_status` del objeto exterior se refieren solamente a ese diagnóstico
radial; `time_status: not_verified` no contradice las comparaciones de rutas
modeladas dentro de `waypoint_returns`. El retorno planeado ya se contabiliza
una vez; esta prueba no añade otro retorno ni reserva. La UI existente no se
ha actualizado y puede mostrar cálculos/etiquetas heredadas contradictorias;
consumir el contrato backend, no interpretar la UI como validación.

**Prohibido presentar una distancia recta (radial o A–B) como ruta transitable**:
es solo una cota geométrica. Tampoco debe presentarse `flat_radial_time_s` como
tiempo de retorno, ni sumar la contingencia al tiempo operacional o a la reserva.

**Migración explícita:** `GET /api/routes` y POST requieren artefactos del mismo
build con manifiesto `schema_version: 3`, `cell_m`, `cell_x_m`,
`dem_validity: "nodata-preserved-v1"`, perfil Jezero con supuestos de analogía lunar y los tres NPY. Versiones v3 anteriores
sin ese marcador o sin todos los valores del perfil también devuelven 503: hay que regenerar todos los artefactos
juntos desde el DEM original, no añadir el marcador a mano. No comprueba hashes.
Los manifiestos v2 devuelven 503: regenerar el pipeline y elegir cada parada
con `--science-waypoint ROW COL` repetido en orden (o ninguna). El antiguo
`--stops` ya no se acepta: su contador no contiene ubicaciones. La petición
con `science_stops` también se rechaza con 400, nunca se convierte en waypoints.
`GET /api/routes` usa `routes.fast` y `routes.safe`; el POST los devuelve en la
raíz. `/mock_routes.json` sigue siendo legado y no garantiza el contrato v3.
Los mapas históricos `cost_fast.npy` y `cost_safe.npy` siguen por compatibilidad;
no modelan dirección ni se usan para calcular estas rutas.

## Pruebas

```bash
.venv/bin/python -m pytest -q -p no:cacheprovider   # 114 passed
.venv/bin/python -m compileall -q pipeline relay tests
git diff --check
git status --short -- web/                          # debe quedar vacío
```

`tests/test_jezero_smoke.py` construye el DEM real (`data/raw/jez_dtm_1m.tif`,
~1.8 GB, 500×500 a 4 m), valida el manifiesto v3 y ejercita `GET /api/routes` y
`POST /api/route` con paradas ordenadas; se auto-omite si faltan el TIFF o `git`.
Cobertura de presupuesto, fronteras y contingencia, con comandos y gaps:
[`docs/eva_budget_lunar_analog.md`](docs/eva_budget_lunar_analog.md).
Nota de estado: en este árbol `data/out` sigue en `schema_version: 2`, así que
`GET /api/routes` y POST devuelven **503** hasta regenerarlo.

## Cómo se calcula (resumen)

1. DEM → recorte → promedio en bloques; no reproyecta. `cell_m` es el paso
   físico N–S y `cell_x_m` el paso físico E–O. `spatial` registra CRS, extensión,
   latitud central y ventana de origen. En Jezero (`ESRI:103885`, esfera de
   radio 3 396 190 m, equirectangular `lat_ts=0`) se usa el coseno de la
   latitud central para convertir el paso proyectado E–O a suelo. El N–S no
   cambia. Para grados se utiliza una conversión marciana aproximada; para
   otros CRS proyectados se asume que sus unidades son metros físicos, sin
   corrección de escala de proyección. No usar esa suposición sin verificarla.
   Si cualquier píxel de un bloque es NoData o no finito, todo el bloque se
   marca inválido. Se excluyen también los vecinos afectados por el cálculo
   de pendiente y rugosidad (ventana 3×3): no se aceptan como paradas ni se
   atraviesan. La vista previa no distingue explícitamente esas celdas.
2. Pendiente absoluta de celda por gradiente central.
3. Rugosidad aproximada por percentiles (sin ortofoto: residuo del DEM).
4. Para cada paso dirigido entre 8 vecinos, gradiente firmado = diferencia de
   elevación / distancia horizontal; velocidad Tobler `6·e^(−3.5|gradiente+0.05|)`
   km/h normalizada para que en llano coincida con `v_nom_flat_kmh` o `v_return_flat_kmh`. Tiempo = distancia / velocidad × `(1 + 0.2·rugosidad media)`.
   El factor es un supuesto de aproximación, no calibración de un traje marciano.
5. Dijkstra minimiza tiempo para `fast`; `safe` minimiza la suma de tiempo ×
   `[1 + rugosidad media + (pendiente media/25°)²]`. El tiempo mostrado para
   ambos usa el mismo modelo físico estimado, no el costo penalizado. La ruta
   conservadora no siempre será distinta ni tendrá menor exposición en cada métrica.
6. Ningún extremo de paso puede superar 25° de pendiente de celda ni 25° de
   pendiente direccional; una diagonal no cruza esquinas bloqueadas. Sin camino,
   se informa error. El máximo incluye la pendiente de celda y la pendiente
   absoluta de cada paso. Exposición = metros de pasos cuya pendiente absoluta
   o pendiente media de sus celdas extremas sea ≥15°;
   rugosidad media está ponderada por longitud y es relativa a la ventana DEM.

## Procedencia (importante)

- **Código de este repositorio:** escrito para este proyecto. La capa de misión
  (pipeline, relay, página, apps MAP/BUDGET) es trabajo nuevo.
- **Plataforma Psion Micor 4.2 (ESP32):** desarrollo previo del equipo. Aquí solo
  se usa como terminal de destino; no se incluye su firmware. Acreditada en
  referencias.
- **Nada de esto mide telemetría del traje:** el presupuesto EVA es una
  simulación sobre la ruta planeada.

## Referencias

- MOLA/MEGDR — PDS Geosciences: https://pds-geosciences.wustl.edu/missions/mgs/megdr.html
- Mars 2020 TRN HiRISE DTM Mosaic — USGS, DOI 10.5066/P9REJ9JN
- Mars Human Exploration Zone DEM Archive 2023 — DOI 10.17189/ervc-mr85
- THEMIS thermal inertia — https://astrogeology.usgs.gov/search/map/mars-themis-derived-global-thermal-inertia-mosaic
- MSL RAD RDR — PDS, dataset `MSL-M-RAD-3-RDR-V1.0`
- Tobler, W. (1993) *Three presentations on geographical analysis and modeling*.
- SEXTANT (EVA traverse planner, NASA/AIAA 2011) — antecedente del enfoque de costo.
- Mars Trek WMTS — https://trek.nasa.gov/tiles/apidoc/trekAPI.html?body=mars

## Límites conocidos

- La rugosidad es un **proxy** (residuo del DEM): no detecta rocas ni arena,
  ni peligros por debajo de la resolución; no valida seguridad EVA.
- `assets/jezero_ortho_4m.png` es contexto visual, **no mapa de peligros** y
  no se utiliza para calcular rutas. Solo corresponde a la ventana Jezero
  central de 500×500, no a un `--at` arbitrario. Véase el sidecar JSON para
  extensión, afín, CRS, hashes y atribución. El PNG no lleva georreferencia
  interna; el JPEG de origen tampoco: se asignó la extensión documentada en el
  informe/metadata del recorte adjunto t_d9eac6cb. Su registro depende de esa
  procedencia y de que el JPEG no haya sido recortado o deformado. Reproducir:
  `python -c "from PIL import Image; Image.open('jezero_ortho_2km_05m.jpg').resize((500,500), Image.Resampling.BOX).save('jezero_ortho_4m.png')"`.
  El JPEG tiene compresión con pérdida; no sirve para análisis de textura fino.
- Crédito: NASA/JPL-Caltech/University of Arizona/USGS, ortomosaico HiRISE
  Jezero TRN, Fergason et al. 2020. Licencia CC0 1.0 Universal, atribución
  recomendada según el informe; no es el mapa de peligros TRN.
- La escala E–O es constante en la latitud central del recorte: es una
  aproximación local; para regiones grandes debe variar por fila. El recorte
  predeterminado cubre 2 000 m proyectados por eje pero ~1 896.8 m físicos E–O
  por 2 000 m N–S. La rejilla registra las distancias sin reproyectar la imagen.
- NoData y sus vecinos afectados se excluyen de la búsqueda; inspeccionar la cobertura antes de interpretar una ruta.
- Sin posicionamiento: Marte no tiene GPS; la posición es relativa al mapa.
- La radiación no entra en la búsqueda: es tiempo × tasa, mostrada aparte.
- Un solo corredor de ~2 km; no hay mosaico multi-hoja.
