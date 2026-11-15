# Funciones del explorador

| Función | Implementación y alcance |
| --- | --- |
| Mapa 2D y terreno 3D | Ortofoto HiRISE real en escala de grises sobre el DEM compatible. Zoom, giro, inclinación, desplazamiento con Mayús/arrastre y flechas. |
| Globo de Marte | MOLA global y selección de regiones. Acceso directo a Jezero (DEM de 4 m) y Glen Torridon (DEM muestreado a ~12.14 m). Fuera de estos recortes se usa MOLA de ~3.7 km nativos; no hay fotografía HiRISE global. |
| Objetivos científicos | 718 registros originales NASA PDS ChemCam de Glen Torridon/Gale: coordenadas, sol, formación, miembro, vetas y nódulos. Búsqueda y filtros. No se trasladan a Jezero. |
| Química y misión científica | 878 observaciones ChemCam cuantitativas, objetivo/hipótesis, equipo/factor de movilidad, áreas manuales de exclusión y cuaderno web exportable. [Revisión del reto](AUDITORIA_RETO_NASA.md). |
| Puntos personales | Clic o coordenadas, nombre, eliminación y mezcla con objetivos. Persistencia localStorage aislada por región; no se sincroniza entre dispositivos. |
| Planificación Python | 2–20 puntos totales, FAST/SAFE, pendiente máxima configurable 5°–40°, distancia, ascenso, descenso y tiempo educativo. Ida, ciencia, regreso y contingencia. |
| Astronautas | Dos figuras animadas y modelos de escala humana en superficie; pausa, avance, velocidad y cámara de seguimiento. |
| Superficie | Reconstrucción 3D del DEM con ortofoto cuando coincide el recorte. No contiene panoramas fotográficos Street View. |
| Indicaciones y voz | Giros, distancias, ciencia y regreso; selección de voces españolas instaladas, ritmo y volumen. Depende del dispositivo; no requiere API de pago. |
| Guía celular / ESP32 | Vista de indicaciones grandes, GPS real del celular vinculado explícitamente a la ruta de Marte a escala 1:1, y receptor Wi-Fi para el firmware ESP32 de la bitácora suministrada. Ubicación antigua, mala precisión o desconexión ponen la guía en espera. |
| Recálculo y polvo | Desvío simulado/manual y cambio de escenario recalculan lo restante conservando el hogar H. Polvo reduce velocidad; tormenta añade una exclusión hipotética. Sin paso, pausa y reintento. El presupuesto corresponde al nuevo tramo, sin descontar tiempo consumido. |
| Exportación | JSON con puntos nombrados, ruta, métricas, escenario, restricciones, procedencia y fuentes. |

## Datos y límites

Glen Torridon se obtiene directamente de [NASA PDS, suplemento de diagénesis MSL](https://pds-geosciences.wustl.edu/msl/urn-nasa-pds-msl_gt_diagenesis_supplement/) y del [producto HiRISE PSP_009294_1750](https://www.uahirise.org/dtm/dtm.php?ID=PSP_009294_1750). `pipeline.fetch_glen` reproduce el recorte; `assets/glen-torridon/provenance.json` incluye productos, resolución y checksums. La ortofoto procede del producto emparejado; no se ha validado independientemente su precisión absoluta. Los objetivos se ajustan a la celda más cercana y pueden compartir celda.

No se extrapolan objetivos ni fotografía orbital a todo Marte. El DEM no detecta todos los obstáculos ni determina seguridad EVA. Polvo, tiempos y restricciones son escenarios educativos, sin meteorología observada ni ubicación real en Marte. El modo de entrenamiento sí usa GPS terrestre, vinculado explícitamente al mapa.

Verificación automática: rutas, regreso independiente, pendientes, exclusiones, catálogo, coordenadas, geometría y carga conjunta de scripts. Las voces efectivas, la precisión real del GPS y la pantalla/enlace físico del ESP requieren prueba en dispositivo; no se han probado físicamente desde este entorno. Véase [guía de uso y firmware](NAVEGADOR_ESP32.md).

Especificaciones completas y parámetros: [ESPECIFICACIONES.md](ESPECIFICACIONES.md). Arquitectura: [ARQUITECTURA.md](ARQUITECTURA.md). Contratos: [API.md](API.md). Fuentes: [DATOS_Y_FUENTES.md](DATOS_Y_FUENTES.md).
