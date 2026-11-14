# Micor Field — MVP de mapa y presupuesto EVA

Generador local de rutas para un paseo marciano: convierte un DEM en dos rutas
(segura y rápida), las sirve por LAN y deja los tiles listos para el terminal.

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

python -m pipeline.build --dem data/raw/jez_dtm_1m.tif --rows 500 --cols 500 --out data/out
```

## Endpoints del relay

- `GET /health` — estado y archivos disponibles.
- `GET /api/routes` — rutas precalculadas y metadatos.
- `POST /api/route` — `{"start":[row,col],"end":[row,col]}` → dos rutas nuevas.
- `GET /preview.png` — mapa de pendiente de la zona.
- `GET /api/tiles/<name>` — tiles BMP para el terminal.

## Cómo se calcula (resumen)

1. DEM → recorte → reproyección a metros → bloques de 4 m.
2. Pendiente (gradiente; equivalente a Horn para este uso).
3. Rugosidad aproximada por percentiles (sin ortofoto: residuo del DEM).
4. Costo en **segundos por metro**: Tobler `6·e^(−3.5|s+0.05|)` × factor de traje
   (`0.4`, calibrado para reproducir el ~1 km/h de las caminatas Apolo) ×
   `(1 + k·rugosidad)`. Celdas > 25° quedan intransitables.
5. Ruta de costo mínimo con 8 vecinos y distancia geométrica (diagonal √2).
6. Presupuesto: `margen = presupuesto − ciencia − tiempo`.

Perfiles: `k = 1.0` (segura) y `k = 0.2` (rápida).

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

- La rugosidad es un **proxy** (residuo del DEM), no un mapa de rocas.
- Sin posicionamiento: Marte no tiene GPS; la posición es relativa al mapa.
- La radiación no entra en la búsqueda: es tiempo × tasa, mostrada aparte.
- Un solo corredor de ~2 km; no hay mosaico multi-hoja.
