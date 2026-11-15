# Micor Field — MVP de mapa y presupuesto EVA

Generador local de rutas EVA simuladas: convierte un DEM en dos rutas de ida y
regreso (conservadora y rápida), las sirve por LAN y genera tiles de pendiente.
Ninguna ruta constituye una garantía o certificación de seguridad.

**Apps incluidas:** 1) mapa por capas + rutas · 2) presupuesto EVA / GO-NO-GO.
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
  --budget-h 4 --science-waypoint 100 200 --science-min 10 --reserve-fraction 0.15
```

## Endpoints del relay

- `GET /health` — estado y archivos disponibles.
- `GET /api/routes` — rutas precalculadas y metadatos.
- `POST /api/route` — `{"start":[row,col],"end":[row,col]}` → dos rutas nuevas.
- `GET /preview.png` — mapa de pendiente de la zona.

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
  `total_s = time_s + science_total_s`. Mostrar ida, regreso y total en cada
  tarjeta. `points` superior es únicamente la ida, por compatibilidad.
- `slope_max_deg`, `slope_exposure_m`, `rough_mean` agregan ambos recorridos;
  `dose_msv` es solo una estimación durante desplazamiento, no telemetría.

No se expone distancia directa A–B. Si la UI la calcula, debe diferenciarla de
la distancia por la ruta y etiquetarla como distancia plana aproximada, no como
geodésica exacta sin modelo y datum. Todos los totales se calculan a precisión
completa y luego se redondean: sumar los valores visibles puede diferir.

`budget_model: "legacy_provisional"` identifica los parámetros `budget_s`,
`science_s`, `reserve_fraction` y los campos `budget` como simulación heredada,
no un modelo EVA real. Se preservan la fórmula y reglas anteriores:
`used = time_s + science_total_s`, `margin = budget_s - used`,
`reserve = budget_s * reserve_fraction`, `fits_reserve = used + reserve <= budget_s`.
La decisión usa precisión completa. La UI mantiene sus etiquetas y cálculos
hasta que su propietario los integre; no se cambian reglas GO/NO-GO aquí.

**Migración explícita:** `GET /api/routes` y POST requieren artefactos del mismo
build con manifiesto `schema_version: 3`, `cell_m`, `cell_x_m`,
`dem_validity: "nodata-preserved-v1"` y los tres NPY. Versiones v3 anteriores
sin ese marcador también devuelven 503: hay que regenerar todos los artefactos
juntos desde el DEM original, no añadir el marcador a mano. No comprueba hashes.
Los manifiestos v2 devuelven 503: regenerar el pipeline y elegir cada parada
con `--science-waypoint ROW COL` repetido en orden (o ninguna). El antiguo
`--stops` ya no se acepta: su contador no contiene ubicaciones. La petición
con `science_stops` también se rechaza con 400, nunca se convierte en waypoints.
`GET /api/routes` usa `routes.fast` y `routes.safe`; el POST los devuelve en la
raíz. `/mock_routes.json` sigue siendo legado y no garantiza el contrato v3.
Los mapas históricos `cost_fast.npy` y `cost_safe.npy` siguen por compatibilidad;
no modelan dirección ni se usan para calcular estas rutas.

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
   km/h × `0.4`. Tiempo = distancia / velocidad × `(1 + 0.2·rugosidad media)`.
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
- El lector rellena nodata con la mediana del recorte: puede ocultar huecos;
  inspeccionar la cobertura antes de interpretar una ruta.
- Sin posicionamiento: Marte no tiene GPS; la posición es relativa al mapa.
- La radiación no entra en la búsqueda: es tiempo × tasa, mostrada aparte.
- Un solo corredor de ~2 km; no hay mosaico multi-hoja.
