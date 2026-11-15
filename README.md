# Micor Field — Ruta marciana

**Lobos Espaciales** · NASA Space Apps

Atlas global de **Marte** y explorador de regiones con un planificador de
caminatas EVA de escenario. **Jezero** conserva la cobertura HiRISE de mayor
detalle: mapa con ortofoto HiRISE real, rutas de
ida y regreso con paradas científicas que el usuario coloca estilo Google Maps, y un presupuesto
EVA de escenario que dice si el recorrido cabe en el tiempo del traje.

Dos rutas por punto: **FAST** minimiza el tiempo de desplazamiento y **SAFE** penaliza pendiente
y rugosidad del terreno. Ambas calculan el regreso por separado y revisan el retorno de emergencia
desde cada parada.

> **No es telemetría ni un criterio oficial NASA GO/NO-GO.** Los veredictos CABE / JUSTO / NO CABE
> son factibilidad bajo supuestos declarados. Las rutas no certifican seguridad: la rugosidad es
> un proxy del DEM y no detecta rocas ni arena.

## Servidor local desde el ZIP de GitHub

Extrae el ZIP de **Code → Download ZIP**, abre una terminal en la carpeta y ejecuta:

```bash
python iniciar.py
```

Requiere Python 3.10+ e internet en la primera instalación. El lanzador prepara
su entorno aislado, carga el recorte real de Jezero incluido y sirve la app
con Waitress en `http://127.0.0.1:8080/`. No requiere Git, Node.js o Make.
Guía para Windows/Linux, acceso desde la red y datos reales:
[INICIAR_SERVIDOR.md](INICIAR_SERVIDOR.md).

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

`python iniciar.py` usa el recorte real de Jezero incluido en `assets/jezero`.
`make demo` y `python iniciar.py --demo` usan el generador sintético, identificado
en la cabecera. Otros terrenos reales se preparan con `pipeline.build --dem ...`. La ortofoto HiRISE incluida se puede consultar como referencia
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

### Vista de superficie (estilo Street View)

Con terreno y una ruta calculada, pulsa **Vista de superficie**. La cámara sigue
la ruta a 1,70 m sobre la malla, siempre con relieve ×1, independientemente de
la exageración vertical del mapa general. Arrastra para mirar en cualquier
dirección, usa la rueda para ajustar el campo visual, o las flechas izquierda y
derecha para girar. Los botones ±5 m y las flechas arriba/abajo desplazan la
posición sobre la ruta y pausan la reproducción. **Mirar hacia la ruta** centra
la cámara y **Volver al mapa** / Escape recuperan el mapa general.

**Iniciar**, la barra de avance y la guía por voz del panel siguen controlando
el mismo recorrido. La vista WebGL añade iluminación del relieve y bruma
ilustrativa; no modifica el DEM, las rutas ni los supuestos EVA. Al avanzar
manualmente se mantienen sincronizadas posición, distancia e indicaciones.

No son fotografías panorámicas tomadas desde el suelo: es una reconstrucción
a partir del DEM activo. La demo sintética se identifica explícitamente.
La resolución visual está limitada a las muestras de `/api/terrain` (hasta
75×75); no se inventan rocas, detalle de elevación o geometría en zonas NoData.
La altura de cámara se interpola sobre los mismos triángulos dibujados.

La ortofoto HiRISE solo se carga sobre una malla real cuando coinciden CRS,
los cuatro límites (tolerancia de 0,1 m) y la ventana de origen declarada en
`assets/jezero_ortho_4m.json`. Esto comprueba la correspondencia declarada de
ventana; no certifica precisión de georreferenciación. Si no coincide, se usa
color ilustrativo. La iluminación y bruma tampoco representan condiciones
meteorológicas observadas. Sin WebGL, permanecen disponibles mapa y guía.

El componente está aislado en `web/streetview.js`; recibe la posición del
seguimiento existente, reutiliza sus controles y no altera la planificación.

```bash
node --test tests/streetview.cjs tests/navigation.cjs
```


## Datos NASA / USGS y capas científicas

Se incluye un recorte real HiRISE de **Jezero**, del producto USGS Mars 2020
Terrain Relative Navigation DTM (Fergason et al., 2020; DOI 10.5066/P9REJ9JN).
Rejilla derivada: 500×500, paso norte-sur 4 m y este-oeste ≈3,79356 m físicos
por la corrección de latitud. La ventana proyectada mide 2×2 km y coincide con
la declarada en `assets/jezero_ortho_4m.json`. Elevación del recorte: aproximadamente
−2494 a −2409 m en la referencia vertical del producto. No es altura sobre el suelo.

- **Relieve iluminado, elevación y pendiente**: capas derivadas del DEM activo,
  tanto en 2D como en el relieve 3D. La pendiente no clasifica rocas ni arena.
- **Ortofoto HiRISE en 2D**: se habilita únicamente cuando CRS, límites y ventana
  de origen coinciden. La vista de superficie usa la misma comprobación.
- **Detalle estándar / alto**: 75×75 o 150×150 muestras visuales. El cálculo de
  rutas siempre usa la rejilla completa de 500×500. La resolución de origen
  no debe confundirse con la resolución reducida de visualización.
- **Perfil de elevación**: muestra alturas muestreadas a lo largo de ida y regreso
  y la posición actual de reproducción. Es una vista de la malla reducida, no
  una nueva métrica del presupuesto EVA.
- **Catálogo de fuentes**: enlaces, cobertura y uso de las cinco fuentes propuestas,
  distinguiendo datos incorporados de referencias externas. Mars Trek ofrece
  contexto global y servicios WMTS; el archivo de Perseverance ofrece imágenes
  reales de Jezero, pero no se usa como una textura arbitraria o posición en vivo.
  Curiosity/Mount Sharp pertenece a Gale. Worldview muestra la Tierra y queda
  excluido como fuente de terreno marciano.

La descarga ZIP ya incluye los datos preparados (≈3,7 MB), sin descargas NASA
al arrancar. `python iniciar.py --demo` mantiene disponible la demo sintética.
El importador reproducible descarga por rangos HTTP las filas necesarias,
valida la ventana y guarda hashes de los productos derivados:

```bash
# Requiere las dependencias completas, incluido rasterio.
python -m pipeline.fetch_jezero --out data/jezero-nuevo
python iniciar.py --data data/jezero-nuevo
```

El distribuidor sirve filas completas: preparar el recorte transfiere unos
171 MB, aunque el resultado es pequeño. El importador no descarga el TIFF
completo de 1,8 GB ni reemplaza una carpeta existente. Es necesario internet
para regenerar esos datos. Ver `assets/jezero/provenance.json` y
`assets/science_sources.json` para trazabilidad y alcance por fuente.


## Atlas de todo Marte y selección de regiones

La página abre un globo 3D con el modelo real **NASA MGS MOLA MEGDR** incluido
en `assets/mars` (16 píxeles/grado; aproximadamente 3,7 km en el ecuador).
Arrastra el planeta o usa las flechas para girar. Pulsa una zona, escribe sus
coordenadas planetocéntricas o elige un acceso del catálogo. **Abrir región**
carga una ventana local de 400 km alrededor de cualquier latitud/longitud,
incluidos polos y cruces de meridianos. **Jezero · HiRISE** conserva el recorte
de 4 m; no se sustituye por MOLA al pulsar ese acceso específico.

Dentro de cada región están disponibles mapa 2D/3D, capas de elevación y
pendiente, salida/destino/paradas, cálculo FAST/SAFE con regreso, presupuesto
simulado, perfil visual, seguimiento, guía de voz, reconstrucción de superficie
y exportación de misión. **Las funciones trabajan dentro de la ventana cargada**;
no concatenan automáticamente un recorrido entre ventanas opuestas del planeta.

MOLA sirve para explorar relieve regional. No ofrece detalle suficiente para
planificar una caminata EVA operativa. La interfaz lo indica permanentemente:
las celdas locales son de 5 km, el producto original ya contiene interpolaciones
y tiene huecos entre perfiles de hasta 12 km. El motor mantiene sus supuestos
de marcha y presupuesto, por lo que recorridos regionales extensos normalmente
superan el presupuesto EVA. Un resultado numérico no valida transitabilidad.

Se usan ventanas azimutales equidistantes esféricas y muestreo bilineal, sin
fabricar detalle de elevación. El atlas usa colores ilustrativos, no imágenes
del suelo. La ortofoto de Jezero nunca se superpone a una región MOLA.

Cada solicitud resuelve su propia región: `/api/routes`, `/api/terrain`,
`/api/terrain-image`, `/api/route` y `/api/manifest` aceptan
`?region=mola:latitud:longitud`. Sin parámetro, conservan el terreno configurado
(originalmente Jezero). No hay un mapa global mutable compartido entre usuarios.
Las pequeñas ventanas se materializan en carpetas temporales por solicitud y
se eliminan al finalizar; el muestreo mantiene una caché limitada en memoria.
Las bitácoras del firmware conservan su ubicación de almacenamiento original.

Los enlaces de regiones se pueden compartir: la selección queda en la URL,
por ejemplo `/?region=mola:18.65:-133.8`. La exportación incluye región,
procedencia, alcance regional y parámetros de proyección. Cambiar de región
reinicia los puntos y cancela los cálculos/respuestas anteriores para evitar
mezclar una ruta con el terreno de otra zona.

El ZIP de GitHub incluye MOLA preparado (≈33 MB) y no descarga el modelo al
arrancar. Para reproducir el modelo desde NASA PDS en otra carpeta:

```bash
python -m pipeline.fetch_mola --out data/mola-nuevo
node --test tests/planet.cjs tests/region-ui.cjs
```
