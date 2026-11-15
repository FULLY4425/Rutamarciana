# Datos científicos, fuentes y reproducción

[Índice](../README.md) · [Especificaciones](ESPECIFICACIONES.md).

## Inventario incluido

| Paquete | Contenido | Resolución derivada |
| --- | --- | --- |
| `assets/jezero/` | `dem.npy`, `slope.npy`, `rough.npy`, `routes.json`, vista previa y procedencia | 500×500, 4 m norte-sur y ~3.79356 m este-oeste |
| `assets/jezero_ortho_4m.png` y `.json` | Ortofoto real de referencia y descripción espacial | Ventana declarada emparejada con el DEM Jezero |
| `assets/glen-torridon/` | DEM, pendiente, rugosidad, manifiesto, `ortho.png`, `imagery.json`, etiqueta, `targets.json`, CSV original y hashes | 72×122; ~12.14096×12.09959 m por celda; 718 registros científicos |
| `assets/mars/` | `mola16.npy`, etiqueta PDS y `metadata.json` | 2880×5760, 16 píxeles/grado, int16; ~33 MB para el array |
| `assets/science_sources.json` | Catálogo de enlaces y papel de cada fuente | Referencias y fuentes incorporadas diferenciadas |

El ZIP de GitHub contiene los paquetes preparados. No incluye el GeoTIFF completo de Jezero de ~1.8 GB ni descarga productos NASA al iniciar. Jezero preparado ocupa ~3.7 MB y Glen ~1.3 MB; son tamaños aproximados por paquete, no el tamaño total del repositorio.

## Jezero

Producto **Mars 2020 Terrain Relative Navigation HiRISE DTM Mosaic**, Fergason et al., 2020, USGS. [DOI 10.5066/P9REJ9JN](https://doi.org/10.5066/P9REJ9JN).

Origen de 1 m; ventana fila 9744, columna 9700, 2000×2000 píxeles. Promedio de bloques 4×4 para el DEM de 500×500. Proyección `ESRI:103885`; escala física este-oeste corregida por latitud. Las elevaciones conservan la referencia vertical del producto, no son altura de un astronauta sobre el suelo.

La procedencia registra URL de distribución oficial, ventana y hashes. La ortofoto solo se habilita para un terreno que declare el mismo CRS, límites y ventana. Esto no sustituye una validación independiente de error horizontal/vertical. [Detalles del paquete](../assets/jezero/README.md).

## Glen Torridon / Gale

Catálogo de diagénesis MSL/ChemCam, NASA PDS Geosciences Node: [suplemento original](https://pds-geosciences.wustl.edu/msl/urn-nasa-pds-msl_gt_diagenesis_supplement/). Se conserva `target_classification.csv`; de él proceden los 718 registros con coordenadas, sol, formación/miembro y vetas/nódulos. La clasificación no es una predicción generada por el proyecto.

DTM HiRISE `DTEEC_009149_1750_009294_1750_U01.IMG` y ortofoto `PSP_009149_1750_RED_C_01_ORTHO.JP2`, [directorio oficial](https://www.uahirise.org/PDS/DTM/PSP/ORB_009100_009199/PSP_009149_1750_PSP_009294_1750/). Producto original de ~1.0117468 m. El DEM se muestrea cada 12 píxeles, conservando NoData. La ortofoto se recorta en la ventana declarada fila 2799, columna 1399, 864×1464 píxeles.

`MARS:GLEN_EQC` identifica el marco local declarado por esta integración, no un código EPSG estándar. `imagery.json` describe el emparejamiento; `provenance.json` incluye checksums del CSV, ventana DTM, JP2 original y PNG derivado. Los 718 registros están dentro del recorte; eso no garantiza que correspondan a 718 celdas diferentes ni que cada celda admita una ruta al límite de pendiente elegido.

## Marte global MOLA

NASA MGS MOLA MEGDR, conjunto `MGS-M-MOLA-5-MEGDR-L3-V1.0`, producto `MEGT90N000EB.IMG`. [Catálogo PDS](https://pds-geosciences.wustl.edu/missions/mgs/megdr.html).

Cobertura 90° N a 90° S, 0°–360° E, bins de 0.0625°. Coordenadas planetocéntricas IAU2000 y referencia vertical areoide MOLA GMM3. El producto ya interpola zonas sin observación; documenta huecos entre perfiles de hasta 12 km.

El servidor genera ventanas esféricas azimutales equidistantes de 81×81 celdas separadas 5 km. Muestreo bilineal, normalización de longitud y manejo de polos. La distancia de una ruta se aproxima sobre esa rejilla plana local. No es una superficie HiRISE global ni un cálculo geodésico exacto entre cualquier par de puntos planetarios. [Metadatos ampliados](../assets/mars/README.md).

## Fuentes externas propuestas y uso real

| Fuente | Uso en el proyecto |
| --- | --- |
| [NASA Mars Trek](https://trek.nasa.gov/mars/) | Referencia contextual y servicios de cartografía; no se copia una escena 3D completa ni se requiere su visor para ejecutar |
| [USGS Interactive Maps Mars](https://astrogeology-usgs.hub.arcgis.com/pages/interactive-maps-mars) | Catálogo y procedencia de cartografía marciana |
| [Curiosity y Mount Sharp](https://science.nasa.gov/resource/curiositys-proposed-path-up-mount-sharp/) | Contexto de Gale/Curiosity; no se aplica como ruta de Jezero |
| [NASA Worldview](https://worldview.earthdata.nasa.gov/) | Referencia terrestre; excluida como DEM o textura de Marte |
| [Imágenes de Perseverance](https://mars.nasa.gov/mars2020/multimedia/raw-images/) | Archivo real de misión; no se convirtió en panoramas Street View ni en telemetría en vivo |
| [NASA GISS Mars24](https://www.giss.nasa.gov/tools/mars24/) | Referencia del cálculo de tiempo marciano (`MSD`, `MST`, `LMST`); el hub no consulta la herramienta en vivo. [Detalle](TIEMPO_MARCIANO.md) |
| [NASA Space Apps 2026 · Martian Map](https://www.spaceappschallenge.org/2026/challenges/interplanetary-survival-guide-martian-map/) | Texto del reto; no es una norma NASA ni una aprobación. [Trazabilidad](REQUISITOS_TRAZABILIDAD.md) |
| [NASA Images and Media Guidelines](https://www.nasa.gov/nasa-brand-center/images-and-media/) | Condiciones de uso y atribución de contenido NASA. [Licencias](OPERACION_LIMITES_LICENCIAS.md#8-licencias-y-créditos) |
| [NASA-STD-3001 Vol. 1](https://standards.nasa.gov/standard/NASA/NASA-STD-3001_VOL_1) | Norma de salud de tripulación citada solo como contexto; **no se declara cumplimiento**. |

Las fuentes de la tabla se consultaron el **15 de noviembre de 2026**; los productos PDS/USGS/HiRISE conservan sus propias fechas, hashes y condiciones. Enlazar un portal no implica sincronización permanente.

## Reproducir los paquetes

Con las dependencias completas instaladas y conexión a internet, usa carpetas de salida nuevas:

```sh
python -m pipeline.fetch_jezero --out data/jezero-nuevo
python -m pipeline.fetch_mola --out data/mola-nuevo
python -m pipeline.fetch_glen --out data/glen-nuevo
```

El importador Glen usa rasterio y el JP2 oficial; `--out` permite elegir otra carpeta. Para el conjunto publicado se ejecutó `python -m pipeline.fetch_glen`; conserva sus salidas en `assets/glen-torridon`. Los importadores registran procedencia; no hace falta ejecutarlos para usar el ZIP.

El importador de Jezero usa rangos HTTP y recibe filas completas del distribuidor: transfiere aproximadamente 171 MB para preparar el recorte, aunque su resultado es pequeño. Reproducir Glen requiere el producto de ortofoto de decenas de MB; MOLA incluye el modelo global. Tamaños/URLs de origen pueden cambiar según el distribuidor.

Un build propio de DEM se prepara mediante:

```sh
python -m pipeline.build --help
python -m pipeline.build --dem data/raw/mi-dem.tif --rows 500 --cols 500 --out data/mi-terreno
python iniciar.py --data data/mi-terreno
```

`pipeline.build` produce arrays, manifiesto v3, vista previa y BMP paletizados de 320×240 en `tiles/`. Los BMP no convierten automáticamente el nuevo receptor ESP en un mapa 3D; el navegador embebido actual usa instrucciones.

Para demo: `python iniciar.py --demo`, o `python -m pipeline.build --synth --rows 500 --cols 500 --out data/out`. No etiquetar una demo como NASA.

## Convenciones y trazabilidad

- Geografía marciana: latitud planetocéntrica y longitud este, según metadatos de cada producto.
- Rejilla de ruta: filas hacia el sur, columnas hacia el este; cada eje tiene su propia escala física.
- GPS terrestre: coordenadas del celular; se convierten a desplazamiento local y no se confunden con coordenadas marcianas.
- NoData: preservado como NaN en arrays o `null` en JSON; no se rellena a partir del brillo de una foto.
- La elevación se deriva de DEM/DTM; el color de una imagen no es un altímetro.
- Hashes: permiten comparar archivos y origen declarado; no demuestran seguridad del terreno ni exactitud absoluta.

Créditos: NASA/JPL-Caltech/University of Arizona/USGS, NASA/GSFC/MGS MOLA Science Team y NASA PDS / equipo ChemCam y autores del suplemento. Para términos de reutilización consultar el producto concreto; no se asigna una licencia universal nueva a todos los datos o al firmware heredado.

## Química cuantitativa incorporada

`assets/glen-torridon/chemistry.json` contiene 878 observaciones ChemCam LIBS de las tablas PDS de vetas y nódulos, nueve óxidos, wt% y RMSEP. Se conservan originales `chemistry_veins.csv/.xml` y `chemistry_nodules.csv/.xml`; el JSON incluye URLs, hashes y conteos de observaciones emparejadas/excluidas. Coincidencia exacta por nombre y secuencia con los objetivos, sin inventar coordenadas para los 16 registros no emparejados. [Interpretación y límites](AUDITORIA_RETO_NASA.md#datos-y-límites-de-la-química).

Reproducción, después de preparar `targets.json` en la carpeta elegida:

```sh
python -m pipeline.fetch_chemistry --out assets/glen-torridon
```

No vuelve a descargar el DEM ni modifica las elevaciones. Requiere internet al reproducir; la aplicación distribuida sirve el JSON incluido sin descargas NASA de arranque.
