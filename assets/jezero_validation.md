# Jezero: comparación de escala física y ortoimagen

Ejecutado con el DTM local `data/raw/jez_dtm_1m.tif`, ventana central, 500×500,
4 m proyectados, START (2,2), END (497,497), presupuesto 4 h, 5 paradas de
10 min y reserva 15%. Antes: commit `0e2bf4d`; después: este cambio. Los
valores de ruta son de ida **más** vuelta; la optimización puede elegir un
trazado distinto.

| Métrica | Conservadora antes | Conservadora después | Rápida antes | Rápida después |
|---|---:|---:|---:|---:|
| Distancia (m) | 6181.4 | 6034.7 (−2.373%) | 5757.3 | 5658.4 (−1.718%) |
| Tiempo (s) | 12017.2 | 11753.5 (−2.194%) | 11573.6 | 11310.3 (−2.275%) |
| Pendiente máxima (°) | 20.12 | 20.41 | 20.12 | 20.41 |
| Exposición ≥15° (m) | 46.6 | 55.8 | 46.6 | 38.4 |
| Rugosidad media | 0.214 | 0.213 | 0.334 | 0.332 |
| Dosis estimada (mSv) | 0.0668 | 0.0653 | 0.0643 | 0.0628 |
| Margen EVA (s), sin sumar reserva | −617.2 | −353.5 | −173.6 | 89.7 |
| Cumple reserva | no | no | no | no |

Pendiente de todas las celdas del DEM agregado: p50 3.5017° → 3.5961°;
p95 12.1870° → 12.5082°; máximo 50.4841° → 51.2631°. La pendiente de
ruta excluye celdas/pasos bloqueados, por ello no coincide con ese máximo.

El recorte físico usa latitud central 18.488057228° y `cell_x_m` =
3.7935590952 m, `cell_m` = 4 m. El coseno central difiere del coseno de los
extremos del recorte menos de 0.01% relativo. El algoritmo usa Dijkstra
sobre pasos dirigidos, con longitud `hypot(dr*cell_m, dc*cell_x_m)`.

## Ortofoto

`jezero_ortho_4m.png` se obtuvo con promedio BOX 8×8 del JPEG adjunto de
4000×4000; `jezero_ortho_4m.json` registra su hash, transform y extensión.
El PNG tiene 500×500, rango 13–254, cero píxeles negros y cero filas negras;
el JPEG tiene cero filas negras y 210 píxeles de valor cero (posibles efectos
de compresión). No se usó el PNG previo que contenía la banda negra. El JPEG
carece de georreferencia propia: su alineación es la declarada para la ventana
por el informe y `crop_metadata.json` de t_d9eac6cb, no una medición de
precisión absoluta. Comparación exploratoria paso-alto con el hillshade adjunto,
búsqueda ±3 celdas: correlación 0.51266 en (0,0), máximo 0.51327 en (0,−1)
según la convención de desplazamiento de esta prueba; no demuestra alineación
subpíxel ni excluye desplazamiento de una celda. No extrapolar a otra ventana.

Uso exclusivamente visual; no es una clasificación de peligros. Imagen:
NASA/JPL-Caltech/University of Arizona/USGS, HiRISE Jezero TRN, Fergason et
al. 2020. CC0 1.0 Universal; atribución recomendada conforme al informe fuente.
No se descargó el TIFF completo.
