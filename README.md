# Micor Field — Ruta marciana

**Lobos Espaciales** · NASA Space Apps

Planificador de caminatas EVA en **Jezero, Marte**: mapa con ortofoto HiRISE, rutas ida/regreso
(rápida y conservadora) con paradas científicas ordenadas estilo Google Maps, y presupuesto EVA
de escenario. **No es telemetría ni criterio NASA GO/NO-GO** — los veredictos CABE/JUSTO/NO CABE
son factibilidad bajo supuestos declarados; las rutas no certifican seguridad.

## Arranque

```bash
python3 -m venv .venv && . .venv/bin/activate
pip install -r requirements.txt

make demo    # rutas con terreno sintético, sin internet
make serve   # http://localhost:8080
```

Demo lista: `http://localhost:8080/?demo=1`

Con el DEM real de Jezero (1.8 GB, [USGS DOI 10.5066/P9REJ9JN](https://astrogeology.usgs.gov/search/map/mars_2020_terrain_relative_navigation_hirise_dtm_mosaic)):

```bash
curl -L -C - -o data/raw/jez_dtm_1m.tif "https://planetarymaps.usgs.gov/mosaic/mars2020_trn/HiRISE/JEZ_hirise_soc_006_DTM_MOLAtopography_DeltaGeoid_1m_Eqc_latTs0_lon0_blend40.tif"
python -m pipeline.build --dem data/raw/jez_dtm_1m.tif --rows 500 --cols 500 --out data/out --science-waypoint 100 200 --science-waypoint 200 300
```

## Cómo usar la página

1. Elige **Suit budget** (240 min default), **Number of science stops** (0–5, default 0) y **Science per stop**.
2. Clic en el mapa: A → B → S1…SN (en orden de visita).
3. `COMPUTE ROUTES`: cada tarjeta muestra tramos de ida por separado, regreso aparte, distancias
   (ruta vs línea recta "flat"), desglose del presupuesto y retorno de emergencia por parada (60 min / 2 km).

## API (relay :8080)

| Endpoint | Qué hace |
|---|---|
| `GET /api/routes` | Rutas precalculadas + metadatos del manifiesto |
| `POST /api/route` | `{schema_version:3, start, end, science_waypoints, scenario_assumptions}` → rutas fast/safe con presupuesto |
| `GET /api/manifest` | Catálogo reducido para el cliente embebido (`?full=1` = JSON crudo) |
| `POST /api/bitacora` | Guarda una caminata del firmware Psion (`bitacora/<id>.json`) |
| `GET /health` | Estado |

**Supuestos del escenario** (sobrescribibles en build o POST): presupuesto 240 min, marcha 2 km/h
(supuesto Artemis/TBR, no promedio), 20 min/parada (evidencia Apollo, no universal), overhead 30 min,
reserva fija 30 min aparte, contingencia de retorno 60 min / 2 km (analogía lunar — no hay estándar
marciano público). El `15%` antiguo no se usa.

## Pruebas

```bash
.venv/bin/python -m pytest -q -p no:cacheprovider   # 115 passed
```

## Documentación completa

Supuestos, fórmula del presupuesto, contrato v3 detallado y límites conocidos:
[`docs/eva_budget_lunar_analog.md`](docs/eva_budget_lunar_analog.md)

## Créditos

Ortomosaico y DTM: NASA/JPL-Caltech/University of Arizona/USGS — HiRISE Jezero TRN,
Fergason et al. 2020 (CC0 1.0). Terminales: plataforma Psion Micor 4.2 (desarrollo previo del equipo).

**Limitación principal:** la rugosidad es un proxy del DEM — no detecta rocas ni arena y no
valida seguridad EVA. Marte no tiene GPS: la posición es relativa al mapa.
