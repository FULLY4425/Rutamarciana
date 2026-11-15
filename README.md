# Micor Field — Ruta marciana

**Lobos Espaciales** · NASA Space Apps

Planificador de caminatas EVA en **Jezero, Marte**: mapa con ortofoto HiRISE real, rutas de
ida y regreso con paradas científicas que el usuario coloca estilo Google Maps, y un presupuesto
EVA de escenario que dice si el recorrido cabe en el tiempo del traje.

Dos rutas por punto: **FAST** minimiza el tiempo de desplazamiento y **SAFE** penaliza pendiente
y rugosidad del terreno. Ambas calculan el regreso por separado y revisan el retorno de emergencia
desde cada parada.

> **No es telemetría ni un criterio oficial NASA GO/NO-GO.** Los veredictos CABE / JUSTO / NO CABE
> son factibilidad bajo supuestos declarados. Las rutas no certifican seguridad: la rugosidad es
> un proxy del DEM y no detecta rocas ni arena.

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

1. Elige **Suit budget** (240 min default), **Number of science stops** (0–5, default 0) y
   **Science per stop**. Con 0 paradas se calcula directo A→B y B→A.
2. Clic en el mapa: primero A (inicio), luego B (fin) y después S1…SN en el orden en que se
   quiere visitarlas. Cambiar budget o science recalcula solo.
3. `COMPUTE ROUTES` genera ambas rutas. Cada tarjeta muestra:
   - **Tramos de ida por separado** (A→S1, S1→S2, …, S2→B) con minutos y km de cada uno
   - **REGRESO** como tarjeta propia: es un tramo directo optimizado B→A, sin repetir la ciencia
   - **Distancias:** km recorridos vs línea recta A–B etiquetada como aproximación plana
   - **Presupuesto desglosado:** ida + ciencia + regreso + overhead = tiempo operacional,
     con la reserva fija mostrada aparte (no se cuenta dos veces)
   - **Retorno de emergencia:** la parada más lejana contra el límite de 60 min / 2 km
   - **Veredicto CABE ✓ / JUSTO / NO CABE** con holgura y el término que la limita

## Cómo funciona

El DEM se recorta y se promedia en bloques de celda (pendiente, rugosidad y celdas NoData
excluidas del tránsito). El tiempo de cada paso entre vecinos usa una velocidad de Tobler
normalizada a la marcha del escenario, y Dijkstra busca el mínimo por perfil: tiempo puro para
FAST, costo penalizado por pendiente y rugosidad para SAFE.

El presupuesto suma `egress + ida + N × tiempo por parada + regreso + ingress` y compara contra
el suit budget; `slack = budget − operacional − reserva`. Los supuestos son configurables por
build o por POST y viajan con la respuesta (`scenario.values`), junto con su procedencia.

**Supuestos del escenario** (base: analogía lunar — no existe estándar marciano público):
presupuesto 240 min, marcha 2 km/h (supuesto Artemis/TBR, no promedio), 20 min/parada
(evidencia Apollo, no universal), overhead 30 min, reserva fija 30 min aparte, contingencia
de retorno 60 min / 2 km. El `15%` de versiones anteriores ya no se usa.

## API (relay :8080)

| Endpoint | Qué hace |
|---|---|
| `GET /api/routes` | Rutas precalculadas + metadatos del manifiesto |
| `POST /api/route` | `{schema_version:3, start, end, science_waypoints, scenario_assumptions}` → rutas fast/safe con presupuesto |
| `GET /api/manifest` | Catálogo reducido para el cliente embebido (`?full=1` = JSON crudo) |
| `POST /api/bitacora` | Guarda una caminata del firmware Psion (`bitacora/<id>.json`) |
| `GET /health` | Estado |

Ejemplo de POST:

```json
{"schema_version":3, "start":[100,100], "end":[400,400], "science_waypoints":[[250,200],[300,350]]}
```

Las paradas van ordenadas y el conteo se deriva de la lista. Coordenada inválida, duplicada o
tramo imposible → HTTP 400; manifiesto de un build anterior → 503 (regenerar `data/out`).

## Pruebas

```bash
.venv/bin/python -m pytest -q -p no:cacheprovider   # 115 passed
```

Incluye un smoke test contra el DEM real de Jezero (se auto-omite si el TIFF no está descargado).

## Documentación completa

Supuestos, fórmula del presupuesto, contrato v3 detallado y límites conocidos:
[`docs/eva_budget_lunar_analog.md`](docs/eva_budget_lunar_analog.md)

## Créditos

Ortomosaico y DTM: NASA/JPL-Caltech/University of Arizona/USGS — HiRISE Jezero TRN,
Fergason et al. 2020 (CC0 1.0). Terminales: plataforma Psion Micor 4.2 (desarrollo previo del equipo).

Marte no tiene GPS: la posición es relativa al mapa.

## Explorador 3D y seguimiento de misión

La interfaz incluye una malla 3D del DEM activo, sin dependencias de CDN. Arrastra
para girar, usa la rueda para acercar y ajusta la exageración vertical (×1–×8).
La vista 2D sigue disponible. Borra los puntos para elegir A, B y las paradas
científicas directamente sobre el terreno; después calcula FAST y SAFE.
El regreso se muestra en turquesa discontinuo.

En **Seguimiento de recorrido**, elige un perfil y pulsa Iniciar. Puedes pausar o
mover el control de avance para inspeccionar la posición y distancia acumulada
de ida y regreso. La reproducción dura un minuto y representa **distancia de
recorrido**, no tiempo EVA ni telemetría. No simula el tiempo dedicado a ciencia,
entrada/salida o reserva. La exportación JSON conserva rutas, escenario y origen
del terreno; se identifica como simulación.

`GET /api/terrain` expone hasta 75×75 muestras del DEM, incluyendo ambos bordes,
con coordenadas originales de rejilla y escala física independiente por eje.
NoData se conserva como `null`; la malla omite triángulos sin elevación.
El muestreo es solo visual: el cálculo de rutas conserva la resolución completa.
La visualización no añade alturas a partir de la intensidad de la ortofoto.

`make demo` usa el generador sintético de este repositorio, identificado en la
cabecera. Para relieve real se requiere generar los datos con `pipeline.build
--dem ...`. La ortofoto HiRISE incluida se puede consultar como referencia
independiente; no se superpone a otra ventana del DEM ni al terreno sintético.
Un error de cálculo se informa sin sustituir la ruta por datos precalculados.

Validación adicional de la lógica del visor (Node.js):

```bash
node tests/frontend.cjs
```

### Indicaciones paso a paso y voz

La sección **Guía paso a paso** muestra giros a izquierda/derecha, cambios de
sentido, llegadas a S1…SN, destino B y retorno a A. La distancia hasta la
siguiente maniobra y la distancia restante siguen el avance de la simulación.
Las indicaciones usan la escala física por eje del mismo recorrido FAST/SAFE;
una simplificación visual de 10 m evita anunciar cada escalón de la rejilla y
no modifica la ruta calculada. Los giros menores de 30° no se anuncian.

Pulsa **Activar voz** y después **Iniciar** para escuchar indicaciones en
español, con un aviso adicional a 30 m de la siguiente maniobra. **Repetir
indicación** reproduce la posición actual incluso con la simulación pausada.
Al pausar, cambiar de ruta, borrar puntos o mover el avance, se cancela la voz
pendiente. La voz usa `speechSynthesis` del navegador: su disponibilidad,
pronunciación y funcionamiento sin conexión dependen del navegador y las voces
instaladas en el dispositivo. No requiere claves de servicios de voz. Si no se
admite síntesis, las indicaciones escritas siguen disponibles.

Son instrucciones geométricas de una simulación: no existe ubicación en vivo,
detección de desvíos ni recálculo a partir de GPS. Las paradas se anuncian sin
simular el tiempo dedicado a ciencia.

`web/navigation.js` calcula maniobras sin dependencias de interfaz;
`web/guidance.js` adapta esas maniobras a la interfaz y a la voz del navegador.
El motor Python de planificación y presupuesto EVA permanece sin cambios.

```bash
node --test tests/navigation.cjs
```
