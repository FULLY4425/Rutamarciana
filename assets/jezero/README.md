# Jezero: datos reales preparados para el servidor

Producto: Mars 2020 Terrain Relative Navigation HiRISE DTM Mosaic, USGS;
Fergason et al., 2020. DOI: https://doi.org/10.5066/P9REJ9JN.

Distribuidor utilizado: espejo oficial USGS `asc-pds-services.s3.us-west-2.amazonaws.com`,
vinculado desde el catálogo de productos HiRISE. `provenance.json` registra el
URL exacto, ventana, momento de recuperación y hashes SHA-256 de los derivados.
No contiene el archivo TIFF original completo.

Ventana del producto de origen: fila 9744, columna 9700, 2000×2000 píxeles de
1 m. Elevaciones derivadas: promedio de bloques 4×4, 500×500 celdas float32.
La corrección física este-oeste usa la latitud central; proyección ESRI:103885.
Las coordenadas, límites y ventana declarados coinciden con la ortofoto incluida.
Esto no constituye una verificación independiente de precisión absoluta.

- `dem.npy`: elevaciones del producto de origen, en metros.
- `slope.npy`: pendientes calculadas en grados.
- `rough.npy`: proxy de rugosidad calculado a partir del DEM.
- `routes.json`: rutas y supuestos del escenario, con procedencia.
- `preview.png`: visualización de pendiente del pipeline.

Los archivos NumPy se leen con `allow_pickle=False`. Los datos no incluyen
meteorología, telemetría de rover o traje, ni clasificación de rocas o arena.
Las rutas y presupuestos son resultados del modelo del proyecto, no rutas
oficiales de NASA ni recomendaciones operativas.

Créditos: NASA/JPL-Caltech/University of Arizona/USGS; HiRISE Jezero TRN mosaic,
Fergason et al., 2020. Consultar el catálogo USGS para descripción y condiciones
del producto: https://astrogeology.usgs.gov/search/map/mars_2020_terrain_relative_navigation_hirise_dtm_mosaic
