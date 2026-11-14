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
  --budget-h 4 --stops 5 --science-min 10 --reserve-fraction 0.15
```

## Endpoints del relay

- `GET /health` — estado y archivos disponibles.
- `GET /api/routes` — rutas precalculadas y metadatos.
- `POST /api/route` — `{"start":[row,col],"end":[row,col]}` → dos rutas nuevas.
- `GET /preview.png` — mapa de pendiente de la zona.

### Contrato para la UI

`GET /api/routes` entrega metadatos y `routes.safe` / `routes.fast`;
`POST /api/route` conserva `safe` / `fast` directamente en la respuesta. Cada
una contiene:

- `objective`: `fastest` o `conservative`; `safe` es solo el alias histórico,
  **no** una certificación.
- `outbound` (START→END) y `return` (END→START): `points` como pares `[row,col]`,
  `time_s`, `distance_m`, `slope_max_deg`, `slope_exposure_m`, `rough_mean`.
- `time_s` y `distance_m` superiores: **suma de ida y regreso**. `points`
  superior conserva únicamente la ida para compatibilidad con el mapa anterior;
  dibujar `return.points` explícitamente para mostrar el regreso. `dose_msv`
  es una estimación solo durante desplazamiento, no una medición.
- Métricas superiores: máximo de pendiente, suma de exposición y media de
  rugosidad ponderada por distancia de ambos tramos.
- `budget.used_s`, `margin_s`, `reserve_s`, `fits_reserve` calculados con los
  parámetros de la generación. El cliente puede recalcularlos al cambiar inputs.

Los metadatos `budget_s`, `science_s` (segundos **por parada**),
`science_stops` (número de paradas) y `reserve_fraction` son explícitos. La
fórmula es `used = time_s + science_stops * science_s`,
`margin = budget_s - used`, `reserve = budget_s * reserve_fraction` y
`fits_reserve = used + reserve <= budget_s`. Por defecto: 240 min, 5 paradas,
10 min/parada y reserva de 15% del presupuesto; la reserva no se suma dos veces.
La decisión usa precisión completa; los números JSON están redondeados, así que
recalcularla solo con los valores mostrados puede diferir cerca del límite.
Estos valores son supuestos de planificación, no telemetría ni presupuesto real.
La UI actual conserva sus propias etiquetas y cálculo; su integración y
etiquetado correctos requieren actualización por su propietario.

El POST requiere `dem.npy`, `slope.npy`, `rough.npy` y `routes.json` del mismo
build; con artefactos antiguos responde 503 y hay que regenerar. Coordenadas
inválidas o una ruta imposible responden 400. `GET /api/routes` devuelve 503
si falta el manifest de `schema_version: 2` con `cell_x_m`; `/mock_routes.json` permanece
disponible pero es antiguo y no garantiza este contrato.
Los archivos históricos `cost_fast.npy` y `cost_safe.npy` se siguen generando
por compatibilidad, pero son mapas escalares antiguos: no modelan dirección y
no se usan para calcular ninguna de las rutas de la API nueva.

## Cómo se calcula (resumen)

1. DEM → recorte → promedio en bloques; no reproyecta. `cell_m` es el paso
   físico N–S y `cell_x_m` el paso físico E–O. `spatial` registra CRS, extensión,
   latitud central y ventana de origen. En Jezero (`ESRI:103885`, esfera de
   radio 3 396 190 m, equirectangular `lat_ts=0`) se usa el coseno de la
   latitud central para convertir el paso proyectado E–O a suelo. El N–S no
   cambia. Para grados se utiliza una conversión marciana aproximada; para
   otros CRS proyectados se asume que sus unidades son metros físicos, sin
   corrección de escala de proyección. No usar esa suposición sin verificarla.
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
