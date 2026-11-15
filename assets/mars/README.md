# Modelo global de Marte: MGS MOLA MEGDR

Fuente NASA PDS Geosciences Node:
https://pds-geosciences.wustl.edu/missions/mgs/megdr.html

Producto `MEGT90N000EB.IMG`, conjunto `MGS-M-MOLA-5-MEGDR-L3-V1.0`.
El modelo contiene 2880×5760 bins de 0,0625° (16 píxeles/grado), aproximadamente
3,705 km norte-sur en el ecuador. Cobertura: 90° N a 90° S y 0° a 360° E.
Coordenadas planetocéntricas IAU2000; referencia vertical: areoide MOLA GMM3.
La etiqueta PDS original se conserva en `mola16.lbl`.

`mola16.npy` contiene los mismos enteros de 16 bits, con orden de bytes adaptado
para NumPy y sin pickle. `metadata.json` registra URLs, créditos, fecha de
recuperación y hashes SHA-256 del original y del archivo derivado. No es
fotografía ni un producto HiRISE. Los colores del atlas son ilustrativos.

El productor interpoló bins sin observaciones. La etiqueta documenta huecos
entre perfiles de hasta 12 km. Este modelo sirve para contexto regional, no
para identificar pasos caminables, rocas, arena, grietas o riesgos pequeños.

La aplicación crea ventanas locales de 81×81 celdas separadas 5 km (400 km
entre centros extremos), con proyección azimutal equidistante esférica centrada
en la selección. El muestreo bilineal no añade detalle. La distancia de las
rutas es una aproximación plana dentro de esta ventana, no un cálculo global
exacto sobre todo el planeta. Las coordenadas de longitud se normalizan y el
muestreo respeta el cruce de meridianos y los polos.

Créditos: NASA/GSFC/MGS MOLA Science Team; NASA PDS Geosciences Node.
Reproducción: `python -m pipeline.fetch_mola --out data/mola-nuevo`.
