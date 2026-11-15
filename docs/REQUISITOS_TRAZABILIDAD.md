# Requisitos, trazabilidad y fuentes primarias

[Hub](HUB_RUTA_MARCIANA.md) · [Auditoría del reto](AUDITORIA_RETO_NASA.md) · [Índice](README.md).

Revisión: **15 de noviembre de 2026**. Este documento separa tres cosas que no deben mezclarse:

1. los **requisitos del reto** de NASA Space Apps (texto de la convocatoria y sugerencias opcionales);
2. las **normas formales de NASA**, que son otro tipo de documento y no se cumplen por participar en el reto;
3. las **decisiones de producto** de Ruta Marciana, que pueden añadir funciones no exigidas.

No se afirma que NASA haya evaluado, aprobado o certificado esta implementación.

## 1. Reto de origen

- **NASA Space Apps Challenge 2026**, reto *Interplanetary Survival Guide: Martian Map*.
- URL: https://www.spaceappschallenge.org/2026/challenges/interplanetary-survival-guide-martian-map/ (consulta 2026-11-15).
- Texto completo conservado en [`reference/MARSWALK_CHALLENGE_2026.txt`](../reference/MARSWALK_CHALLENGE_2026.txt), SHA-256 `71449d3c2e29be6002f06d4b5230e18d127587ec3da5e8c39c8de213629b446b`.
- Objetivo central: «create a layered, integrated view of a location or route on the Martian surface that pulls together data from multiple NASA science missions and could help a human explorer plan and carry out a successful Marswalk».

El texto usa «might», «may consider» y «may (but are not required to)» para propuestas. En la tabla se conserva esa distinción; una sugerencia no se convierte en obligación.

## 2. Trazabilidad reto → producto

| Requisito o sugerencia (síntesis fiel) | Tipo | Componente / evidencia | Estado |
| --- | --- | --- | --- |
| Vista integrada por capas de una ubicación o ruta con datos de varias misiones NASA | Central | Mapa 2D/3D, ortofoto HiRISE, DEM, química ChemCam, contexto MOLA; `web/terrain.js`, `web/science.js`, `web/chemistry.js` | Implementado y documentado |
| Ayudar a planificar y ejecutar una Marswalk con ciencia | Central | FAST/SAFE, paradas ordenadas, objetivo/hipótesis, guía, notas, presupuesto EVA | Implementado; no es ejecución certificada |
| Elegir objetivos científicos y lugar | Sugerencia | 718 registros PDS de Glen Torridon; campos de objetivo/hipótesis | Implementado |
| Partir de un lugar explorado por un rover u otro lugar | Alternativa | Glen (Curiosity), Jezero (Perseverance), ventanas MOLA | Implementado con límites de detalle |
| Combinar al menos tres clases de datos | Sugerencia adoptada | Óptica HiRISE + topografía DEM + química cuantitativa ChemCam en Glen | Implementado en Glen, no global |
| Contexto de ruta y destinos | Central | Pendiente, elevación, distancia, ascenso/descenso, perfil, química por objetivo | Implementado |
| Considerar movilidad con traje/equipo | Sugerencia | Factor de marcha 0.5–1, masa descriptiva, presupuesto `LUNAR-ANALOG-2026` | Modelo educativo; no fisiología |
| Evitar obstáculos significativos | Sugerencia | Hasta 7 exclusiones manuales + tormenta hipotética; NoData y pendiente máxima | Implementado; sin detección automática de rocas |
| Actualizaciones de condiciones | Contexto; se admite marco histórico o sin fecha | Escenarios de polvo hipotéticos; datos históricos con sol/fecha | Implementado como escenario, no meteorología |
| Meteorología espacial / radiación | Opcional | Indicador heredado `dose_msv` a tasa fija 0.02 mSv/h | No es dosimetría; no se incorporó servicio observado |
| Hábitat, tablet, HUD u otro formato | Alternativa opcional | Web escritorio/móvil + TFT ESP32 320×240 + OLED | Implementado parcialmente; hardware no probado físicamente |
| Mapa estático, interactivo o generador | Alternativa | Mapa interactivo y generador de rutas/ventanas | Implementado |
| Inspiración en otros cuerpos | Opcional | Analogía lunar declarada para el presupuesto | Declarada, no certificada |
| Datos históricos, futuros o sin fecha | Alternativa | Productos PDS con sol/fecha; relieve estable | Implementado |
| Imágenes de superficie para anticipar el destino | Opcional | Reconstrucción DEM/WebGL | No hay panoramas fotográficos reales |
| Transporte robótico o trabajo en parejas | Opcional | Dos astronautas; simulación ROVER en ESP32 | Pareja sí; rover no transporta ni opera |

La auditoría detallada, con citas y límites, está en [AUDITORIA_RETO_NASA.md](AUDITORIA_RETO_NASA.md).

## 3. Requisitos del hub (contrato de producto)

Estos requisitos son decisiones internas, no texto del reto. Su verificación en el hub está pendiente (ver [HUB_RUTA_MARCIANA.md](HUB_RUTA_MARCIANA.md#9-pendiente-de-verificación-explícito)).

| ID | Requisito | Componente esperado | Verificación |
| --- | --- | --- | --- |
| HUB-01 | Portada con hora Tierra y hora Marte prominentes | Portada, módulo de reloj | Pendiente |
| HUB-02 | `MSD`, `MST`, `LMST` con localidad y longitud | Reloj + [TIEMPO_MARCIANO.md](TIEMPO_MARCIANO.md) | Pendiente |
| HUB-03 | Rótulo de aproximación y fuente visible | Portada | Pendiente |
| HUB-04 | Módulo Mapa + EVA conserva la app y sus IDs | `web/index.html`, `web/workspace.js` | Parcial: base verificada; integración pendiente |
| HUB-05 | Bitácora local-first por tags, con exportación | Módulo Bitácora | Pendiente |
| HUB-06 | Rover rotulado como simulación, sin telemetría | Módulo Rover | Pendiente |
| HUB-07 | Estados honestos por módulo | Portada | Pendiente |
| HUB-08 | Sin regresiones en `?region=`, API y accesibilidad | Servidor + cliente | Pendiente |

## 4. Hoja de ruta futura: hardware de bajo consumo (EDGE)

Decisión de narrativa de producto: **Micor Field / Martian OS is a simulation built on real NASA data. Future direction: adapt the NASA-data mission simulation for low-power ESP32-class hardware and LoRa-connected field nodes.** La segunda oración es **future roadmap** (hoja de ruta futura), no una adaptación en curso. Son decisiones internas, no requisitos del reto ni de NASA.

Lo que existe hoy es un firmware ESP32 como base de software/hardware (EDGE-03); la integración de **esta** simulación web en ese firmware y el enlace LoRa no están implementados. El bajo consumo no está medido y LoRa no es una capacidad actual.

| ID | Requisito | Estado verificado | Evidencia |
| --- | --- | --- | --- |
| EDGE-01 | La aplicación es una simulación, no telemetría ni operación real | Cumplido | Rover y bitácora simulados; `docs/MODULO_ROVER_ESP32.md` |
| EDGE-02 | Los datos científicos provienen de productos NASA/PDS con procedencia | Cumplido en Jezero, Glen Torridon y MOLA | `docs/DATOS_Y_FUENTES.md`, hashes en `assets/*` |
| EDGE-03 | Existe firmware para microcontrolador de bajo consumo | Parcial, como base existente: dos firmwares compilables para ESP32-WROOM-32, sin placa física flasheada; no integran esta simulación web y no hay consumo medido | `firmware/psion-micor-spaceapps/`, `firmware/psion-marte-os/` |
| EDGE-04 | El enlace del nodo funciona sin Internet | **No cumplido**: el firmware enlaza por Wi-Fi/HTTPS contra el servidor; el relay es opcional y también en PC | `firmware/*/psion-*.ino` incluyen `WiFi.h`, `HTTPClient.h` |
| EDGE-05 | Transporte LoRa (radio, banda, potencia, alcance) | **No iniciado**: no hay librería de radio ni configuración en el repositorio | `grep -ri "lora\|sx127\|sx126"` sin resultados en `firmware/` |
| EDGE-06 | Topología de nodos: múltiples nodos, enrutamiento, presupuesto de energía | **No iniciado**: no hay código, malla ni medición | — |
| EDGE-07 | Consumo medido y autonomía declarada | **No disponible**: no hay medición de corriente ni batería en el repositorio | — |

Regla de redacción: mientras EDGE-04 a EDGE-07 sigan así, la documentación usa la etiqueta «future roadmap» / «hoja de ruta futura» y la frase «Future direction: adapt the NASA-data mission simulation for low-power ESP32-class hardware and LoRa-connected field nodes.» Nunca «being adapted», «adaptación en curso», «funciona en LoRa», «red de nodos desplegada» ni cifras de consumo o alcance.

## 5. Normas formales de NASA: qué no se afirma

- **NASA-STD-3001**, *Spaceflight Human-System Standard*, Vol. 1 *Crew Health* (Rev. C, 2023-09-15) y Vol. 2 *Human Factors, Habitability, and Environmental Health* (Rev. F, 2026-07-14). Es un estándar técnico de programas de vuelo humano; establece requisitos de salud, rendimiento y factores humanos.
- Ruta Marciana es una aplicación educativa/de hackatón. **No** implementa un programa de vuelo humano, no tiene datos médicos de tripulación, no evalúa aptitud física, dosis real, trajes ni planes de contingencia certificados.
- Por tanto: **no se declara cumplimiento de NASA-STD-3001**, ni conformidad, ni certificación, ni aprobación. Tampoco se usa como fuente de los umbrales del presupuesto EVA, que son supuestos declarados (`LUNAR-ANALOG-2026`).
- Cualquier afirmación futura de cumplimiento requeriría un análisis de requisitos aplicables, evidencia verificable y revisión por la organización responsable; no se deduce de una lista de funciones.

## 6. Fuentes primarias consultadas

| Fuente | Título / organismo | URL | Fecha de consulta | Uso |
| --- | --- | --- | --- | --- |
| NASA Space Apps | *Interplanetary Survival Guide: Martian Map* (reto 2026) | https://www.spaceappschallenge.org/2026/challenges/interplanetary-survival-guide-martian-map/ | 2026-11-15 | Requisitos del reto |
| NASA GISS | *Mars24 Sunclock — Algorithm and Worked Examples* | https://www.giss.nasa.gov/tools/mars24/help/algorithm.html | 2026-11-15 | Ecuaciones y ejemplo de validación |
| NASA GISS | *Mars24 Sunclock — Technical Notes on Mars Solar Time* | https://www.giss.nasa.gov/tools/mars24/help/notes.html | 2026-11-15 | Definiciones, MSD, precisión |
| NASA GISS | *Mars24 Sunclock* | https://www.giss.nasa.gov/tools/mars24/ | 2026-11-15 | Herramienta de referencia |
| NASA | *NASA Spaceflight Human System Standard Volume 1: Crew Health* | https://www.nasa.gov/reference/nasa-std-3001v1/ | 2026-11-15 | Contexto normativo; no cumplimiento |
| NASA Standards | *NASA-STD-3001 Vol. 1 Rev. C* | https://standards.nasa.gov/standard/NASA/NASA-STD-3001_VOL_1 | 2026-11-15 | Ficha y fecha del estándar |
| NASA | *Guidelines for using NASA Images and Media* | https://www.nasa.gov/nasa-brand-center/images-and-media/ | 2026-11-15 | Licencia/atribución de medios |
| NASA PDS | Suplemento de diagénesis MSL (ChemCam) | https://pds-geosciences.wustl.edu/msl/urn-nasa-pds-msl_gt_diagenesis_supplement/ | 2026-11-15 | Objetivos y química |
| USGS | *Mars 2020 Terrain Relative Navigation HiRISE DTM Mosaic* (DOI 10.5066/P9REJ9JN) | https://doi.org/10.5066/P9REJ9JN | 2026-11-15 | DEM Jezero |
| HiRISE / UA | Producto DTM/ortofoto `PSP_009149_1750` / `PSP_009294_1750` | https://www.uahirise.org/PDS/DTM/PSP/ORB_009100_009199/PSP_009149_1750_PSP_009294_1750/ | 2026-11-15 | DEM/ortofoto Glen |
| NASA PDS | MGS MOLA MEGDR, `MGS-M-MOLA-5-MEGDR-L3-V1.0` | https://pds-geosciences.wustl.edu/missions/mgs/megdr.html | 2026-11-15 | Contexto global |

Los datasets, hashes y ventanas concretas están en [DATOS_Y_FUENTES.md](DATOS_Y_FUENTES.md). Los términos de reutilización se tratan en [OPERACION_LIMITES_LICENCIAS.md](OPERACION_LIMITES_LICENCIAS.md).

## 7. Qué falta para afirmar uso operativo

Validación independiente de precisión/registro geográfico, cartografía de peligros a escala adecuada, posicionamiento marciano real, meteorología/radiación observadas, modelo certificado de traje/consumibles y evaluación humana. La app no adquiere esas garantías por software. Ver [AUDITORIA_RETO_NASA.md](AUDITORIA_RETO_NASA.md#qué-falta-para-afirmar-uso-operativo-real).
