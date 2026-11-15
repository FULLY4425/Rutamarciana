# Especificaciones funcionales y técnicas

Estado documentado: 15 de noviembre de 2026. Describe lo implementado en `main`; no constituye una lista de funciones futuras. [Índice general](../README.md).

Guía de cada pantalla y flujo: [Manual web](MANUAL_WEB.md). Inventario completo: [Controles](CONTROLES_WEB.md). Diagnóstico: [Solución de problemas](SOLUCION_DE_PROBLEMAS.md).

## Propósito y plataformas

Ruta Marciana permite explorar relieve marciano, elegir objetivos, comparar recorridos educativos y ensayar su seguimiento. Una aplicación web sirve mapa, planificación y guía. El celular puede aportar desplazamientos GPS terrestres para entrenamiento; un ESP32 recibe la indicación actual y conserva las aplicaciones de bitácora y presupuesto de su base.

| Componente | Especificación |
| --- | --- |
| Servidor | Python 3.10+ local; Python 3.12 configurado en Render |
| HTTP | Flask, servido por Waitress; un hilo de solicitudes en `server.py` |
| Cálculo | NumPy, SciPy y scikit-image |
| Preparación geoespacial | rasterio, opcional en ejecución; Pillow para imágenes |
| Cliente | HTML, CSS y JavaScript sin compilación de frontend ni CDN requerido |
| Gráficos | Canvas 2D para atlas/mapa; malla proyectada para vista general; WebGL para superficie |
| Audio | `speechSynthesis` y voces españolas instaladas en el dispositivo |
| Ubicación | Geolocation API, con permiso explícito y contexto seguro |
| Embebido | ESP32-WROOM-32, base Psion Micor, TFT ST7789 320×240 horizontal y OLED SSD1306 |
| Transporte de guía | HTTPS por Wi-Fi, celular → servidor → ESP; no se implementó BLE de navegación |

## Cobertura de Marte

| Región | Datos y resolución | Alcance |
| --- | --- | --- |
| Todo Marte | MOLA MEGDR de 16 píxeles/grado, 2880×5760, ~3.7 km en el ecuador | Atlas y ventanas locales de 81×81 celdas a 5 km; 400 km entre centros extremos |
| Jezero | DEM HiRISE derivado 500×500, 4 m norte-sur y ~3.79356 m este-oeste | Recorte proyectado de 2×2 km; ortofoto emparejada |
| Glen Torridon / Gale | DEM HiRISE 72×122, ~12.14096 m norte-sur y ~12.09959 m este-oeste | Ventana del producto de 864×1464 píxeles nativos; ortofoto y 718 registros ChemCam |
| Demostración | Terreno generado sintéticamente | Pruebas y demostración explícitamente rotuladas; no representa una región real |

El globo se gira con arrastre o flechas. Permite elegir una posición, introducir latitud/longitud y usar accesos regionales. Las longitudes se normalizan y se contemplan cruces de meridianos y polos. Cambiar de región reinicia la misión y descarta respuestas antiguas. No se concatenan automáticamente rutas entre ventanas distintas.

## Mapa, capas y superficie

- Vista 2D y relieve 3D con zoom, giro, inclinación y desplazamiento. En 3D, Mayús/arrastre o botón central desplazan; flechas desplazan, `+`/`-` acercan/alejan y `0` centra. En 2D, arrastrar desplaza cuando hay zoom.
- Exageración vertical de ×1 a ×8 en el mapa general, cámara centrada o seguimiento del recorrido.
- Capas de relieve iluminado, elevación y pendiente. Colores de relieve/atlas ilustrativos.
- Fotografía orbital HiRISE real en escala de grises sobre la malla compatible. Se verifica coincidencia declarada de CRS, límites y ventana; no se mezcla con otro DEM ni con la demo.
- Detalle visual estándar de hasta 75×75 o alto de hasta 150×150. La API acepta 25–150 muestras por eje. El cálculo conserva la rejilla completa; el detalle visual no modifica las rutas.
- NoData viaja como `null`; las superficies no fabrican alturas ni triángulos donde faltan datos.
- Perfil de elevación muestreado a lo largo de ida/regreso, con marcador de posición; no modifica el presupuesto.
- Reconstrucción de superficie WebGL con cámara a 1.70 m sobre los triángulos, relieve ×1, iluminación y bruma ilustrativas. Arrastre y flechas laterales giran; rueda cambia campo visual; botones/flechas de avance desplazan ±5 m y pausan. Escape vuelve al mapa.
- La bruma aumenta con los escenarios de polvo. No representa observaciones meteorológicas. Sin WebGL la reconstrucción no está disponible, pero el mapa y la guía permanecen.

## Objetivos y puntos

El catálogo contiene **718 registros de Glen Torridon**, no 718 lugares distintos garantizados. Cada registro incluye identificador, nombre, coordenadas, sol, elevación, formación, miembro, secuencia y clasificación de vetas/nódulos. Admite búsqueda por nombre/formación/miembro/sol y filtros de vetas o nódulos. En otras regiones el catálogo está vacío; no se traslada a Jezero ni a MOLA.

Los objetivos se ajustan a la celda más cercana. Varios registros pueden ocupar la misma celda; el planificador rechaza puntos de ruta duplicados.

Los puntos personales se crean por clic en el mapa o coordenadas, con nombre de hasta 80 caracteres. Se pueden añadir al recorrido o eliminar. Se guardan hasta 100 por ámbito de región/sitio/rejilla/proyección en `localStorage`; no se sincronizan entre dispositivos. Bloquear o borrar almacenamiento del navegador limita esa persistencia.

La misión permite retirar puntos individuales y combinar puntos propios, clics y objetivos. Orden de visita: A → S1…Sn → B; las nuevas paradas científicas se insertan antes de B. Máximo de 20 puntos totales: A y B más 0–18 paradas.

## Planificación y métricas

Python usa una rejilla de ocho vecinos y un grafo dirigido. Excluye NoData, celdas por encima de la pendiente elegida y aristas con pendiente excesiva. No permite atravesar una esquina diagonal si las celdas laterales están bloqueadas. Subida y bajada pueden tener tiempos distintos.

| Parámetro/resultado | Comportamiento |
| --- | --- |
| FAST | Minimiza el coste de marcha del modelo |
| SAFE | Penaliza adicionalmente pendiente y rugosidad; no certifica seguridad |
| Pendiente máxima | 5°–40°, 25° inicial; seleccionable en interfaz/API |
| Tiempo | Velocidad de Tobler escalada al supuesto de marcha y penalizada por rugosidad |
| Ida | Un tramo por cada parada ordenada, hasta B |
| Regreso | Optimización independiente desde B al hogar; por defecto A |
| Replanificación | Puede cambiar la salida manteniendo `return_to` como hogar H |
| Distancia | Metros físicos por eje del DEM; total de ida/regreso y tramos |
| Ascenso/descenso | Suma de incrementos positivos/negativos de elevación de la ruta |
| Otras métricas | Pendiente máxima, exposición desde 15°, rugosidad media ponderada y tiempos |
| Contingencia | Retorno dirigido más rápido desde cada parada científica; sin paradas, desde el punto de mayor distancia radial |
| Errores | Puntos duplicados, fuera de rejilla, bloqueados o sin paso producen error; no se sustituyen por una ruta ficticia |

El coste usa `Tobler(g) = 6 × exp(-3.5 × abs(tan(g) + 0.05))`, con `g` convertido de grados a radianes para la tangente. En el tiempo final se normaliza al supuesto de velocidad plana: `v(g) = v_plana × Tobler(g) / Tobler(0)`, penalizado por el factor de rugosidad `1 + 0.2 × rugosidad`. El coste SAFE añade `1 + rugosidad_media + (pendiente_media / pendiente_máxima)^2` al coste de marcha. Son fórmulas del modelo, no medidas de movilidad de un astronauta particular.

La respuesta conserva `dose_msv`, calculada con una tasa fija de **0.02 mSv/h** multiplicada por el tiempo de marcha. Es un indicador heredado y simplificado; no es dosimetría medida, predicción de eventos solares ni criterio de protección radiológica.

## Presupuesto EVA educativo

Perfil `LUNAR-ANALOG-2026`: supuestos por analogía lunar, no capacidad certificada de un traje marciano.

| Supuesto inicial | Valor |
| --- | --- |
| Presupuesto | 240 min; interfaz: 120, 180, 240 o 360 min |
| Marcha en llano, ida/regreso | 2 km/h |
| Ciencia por parada | 20 min; interfaz: 10, 20 o 30 min |
| Egreso / ingreso | 15 min cada uno |
| Otras tareas | 0 min |
| Reserva fija | 30 min, separada del tiempo operacional |
| Límites de retorno de escenario | 60 min y 2 km |

`operacional = egreso + ida + ciencia + regreso + ingreso + otras_tareas`.
`holgura = presupuesto − operacional − reserva`.

- **CABE:** la holgura es no negativa.
- **JUSTO:** cabe el tiempo operacional, pero no toda la reserva.
- **NO CABE:** el operacional supera el presupuesto.

La reserva no se añade dos veces ni se usa el antiguo 15%. La respuesta detalla supuestos y overrides. La distancia radial y el tiempo a velocidad plana son aproximaciones separadas del retorno sobre terreno; no prueban transitabilidad. El chequeo del retorno más rápido desde paradas tampoco comprueba todos los puntos del recorrido ni demuestra que no exista otra alternativa con límites distintos. [Modelo detallado](eva_budget_lunar_analog.md).

## Reproducción, astronautas e indicaciones

Dos astronautas virtuales recorren el camino, con iconos aéreos y geometría de escala humana en superficie. El segundo sigue nominalmente 8 m detrás; al inicio pueden coincidir en posición física. Incluye iniciar, pausar, repetir, cambiar perfil, arrastrar el avance, mover ±5 m en superficie y seguir con la cámara.

La reproducción permite resumen de ~60 s o avance basado en el tiempo de marcha modelado, con factores ×1, ×5 y ×20. Representa avance de distancia: no simula permanencia científica, ingreso, egreso o consumo real de suministros.

La guía simplifica la geometría con tolerancia de 10 m sin alterar el camino; anuncia giros desde 30° y cambios de sentido para ángulos mayores de 150°. Incluye ciencia, destino y regreso, distancia a la maniobra y restante. Voz española elegible, ritmo 0.8–1.4 y volumen 0–1, prueba de voz y repetición; aviso adicional cerca de 30 m. Requiere activación y depende del navegador/voces instaladas. No integra ElevenLabs ni otro servicio de pago.

## Polvo, desvíos y exportación

Escenarios **hipotéticos**, no tiempo meteorológico en vivo:

| Escenario | Efecto |
| --- | --- |
| Despejado | Factor de velocidad 1 |
| Polvo | Factor 0.8 |
| Tormenta | Factor 0.6 y una zona circular hipotética creada por la interfaz |

La API admite hasta ocho zonas circulares de exclusión, en coordenadas de rejilla y radio en metros. No modifica los archivos del terreno. Se puede simular un desvío de más de 30 m, marcar otra posición y reintentar. El cambio de escenario/pendiente recalcula lo restante cuando hay recorrido. Conserva ciencia pendiente y hogar; si no encuentra paso, pausa. El nuevo presupuesto evalúa el nuevo tramo **sin descontar automáticamente tiempo ya consumido**.

Exportación JSON: rutas, puntos nombrados, métricas, región, proyección, escenario, restricciones, procedencia y fuentes disponibles. El archivo se identifica como simulación; no es un historial GPS ni un registrador de desplazamientos terrestres.

## Celular: GPS de entrenamiento

La vista **Reloj / celular** concentra flecha, distancia, rumbo e indicación. El usuario activa Geolocation API y vincula explícitamente la ubicación terrestre al punto actual de una ruta corta HiRISE. Selecciona rotación de 0°–359°: 0° conserva el norte; 90° orienta el norte del mapa hacia el este terrestre.

Cada metro real corresponde a un metro del modelo. Se procesa un marco local de hasta 5 km; se proyecta sobre el tramo activo con una ventana de avance limitada para evitar saltar a la vuelta cuando comparte camino con la ida. El rumbo se muestra en grados y cardinales.

- Precisión requerida: ±25 m o mejor; edad máxima del GPS: 15 s.
- Al salir del recorte o alejarse más de 30 m del tramo activo, se suspende la guía de avance.
- Se permite recalcular desde la posición GPS vinculada, manteniendo el hogar.
- La reproducción por tiempo y el cambio de perfil quedan desactivados durante GPS.
- Ocultar la página detiene el GPS. Se solicita mantener pantalla despierta cuando está soportado; no hay garantía de segundo plano.
- MOLA global queda excluido del entrenamiento a pie por su resolución.
- Las coordenadas terrestres permanecen en memoria del navegador. El enlace transmite instrucciones, distancias, precisión/edad y tiempos; no latitud/longitud terrestre.

No se planifican calles terrestres ni se reconocen obstáculos reales. Se necesita un área conocida y apropiada para ensayar la geometría elegida. [Uso completo](NAVEGADOR_ESP32.md).

## ESP32, TFT y bitácora

Firmware integrado: `firmware/psion-micor-spaceapps/`, basado en el ZIP suministrado. El celular aporta GPS; el ESP no incorpora GNSS. Funciona como receptor Wi-Fi de indicaciones, no como visor 3D autónomo ni aplicación universal para relojes comerciales.

TFT **320×240 horizontal**, ST7789: flechas vectoriales, distancia en fuente de 24 píxeles, metros/km, maniobra, restante, cuatro líneas de texto, estado y encoder. Reserva barra superior de 18 píxeles y dock desde y=214. Búfer de 51 bytes por línea y avisos largos con puntos suspensivos. OLED SSD1306 complementario con resumen. No se exige touch ni cámara.

El celular publica y el ESP consulta cada ~2 s mediante sesión privada de 30 min. Credenciales independientes de escritura y lectura; código de vinculación de 49 caracteres (`16 hex.32 hex`). Sesiones en RAM, máximo 64 simultáneas por proceso. El ESP verifica TLS con raíces GTS y sincroniza hora por NTP; tarea de red separada, receptor acotado de 767 bytes y soporte Content-Length/chunked. Datos de más de 15 s o ausencia de actualización válida durante más de 6 s ponen la guía en espera.

El proveedor EVA embebido mantiene el contrato original de **8 paradas**, FAST=0 y SAFE=1, todo en segundos. Con más paradas muestra la guía, pero usa presupuesto manual y lo avisa. La web conserva las 18 paradas intermedias; no se truncan para aparentar compatibilidad.

La bitácora conserva hasta 12 caminatas, hasta 3000 bytes por archivo. Guarda hora, parada, tipo, acción, estado y nota opcional de hasta 100 caracteres; usa selección por listas y texto libre para «Otros». Tipos: roca, suelo, veta, nódulo, polvo, imagen u otro; acciones: muestra, foto, medición o sin hallazgo; estados: normal, contratiempo, peligro o replanificación. Hay identidad de caminata, persistencia LittleFS, exportación JSON/CSV, estado pendiente/sincronizado, sincronización manual y catálogo con fallback. No adjunta fotografías ni resultados físicos de sensores por elegir esos tipos.

La bitácora consume `/api/manifest` y `/api/bitacora` con la configuración de relay heredada; este canal es distinto del enlace privado de guía. El manifest refleja datos precalculados, **no cada misión POST del navegador**. No se implementó sincronización automática del catálogo de una misión interactiva con la bitácora.

La base conserva aplicaciones heredadas: documentos, hoja de cálculo, calculadora, notas, código, archivos, herramientas, música, web, juegos, reloj, estadísticas, Wi-Fi, ajustes, NetLab y libros, además de Misión y Bitácora. Se distribuyen como parte de la base suministrada; no implica que el servidor Ruta Marciana implemente todos sus servicios externos ni que todas se hayan probado en hardware.

## Persistencia y límites del producto

| Información | Dónde vive | Qué la elimina |
| --- | --- | --- |
| Terrenos y catálogos incluidos | Archivos de `assets/` versionados | Retirar/cambiar el paquete |
| Puntos personalizados | `localStorage` del navegador | Borrar datos/cambiar navegador o ámbito |
| Misión calculada y anclaje GPS | Memoria del navegador | Recarga, reinicio o edición según el caso |
| Sesión de guía ESP | RAM del servidor y cliente | Caducidad, revocación o reinicio/despliegue |
| Bitácora del ESP | LittleFS | Borrado explícito o borrado/formateo de flash |
| Copia POST de bitácora | Carpeta de datos del servidor | Borrado; en Render efímero, reinicio/despliegue |

No hay cuentas de usuario, base de datos persistente gestionada, historial GPS, mapas de calles, telemetría real de traje/rover, meteorología en vivo, HiRISE global ni panoramas Street View. No se implementaron alertas físicas, vibración de navegación o síntesis de voz en el ESP; la voz es del celular. La posición vinculada es un entrenamiento, no localización en Marte.

Las rutas son aproximaciones sobre el DEM, con distancias planas regionales y rugosidad proxy. No detectan todos los obstáculos. Los hashes y coincidencias de ventana acreditan procedencia/coherencia declarada, no precisión absoluta independiente. Se han verificado software y compilación; pantallas, radio, GPS real, consumo de batería y audio necesitan prueba física. No se ha flasheado una placa desde este entorno.

## Ampliación científica tras revisar el reto

En Glen se integran **878 observaciones cuantitativas de ChemCam**, asociadas a 320 objetivos por nombre y secuencia, con nueve óxidos, wt% y RMSEP. La capa muestra mediana de cada óxido por objetivo; el detalle permite consultar cada observación individual. No hay interpolación entre objetivos ni química fuera del recorte. El botón de combinación activa fotografía compatible y química sobre el relieve. [Auditoría y fuentes](AUDITORIA_RETO_NASA.md).

El plan incluye objetivo/hipótesis de hasta 600 caracteres, equipo de hasta 300, masa descriptiva de 0–100 kg y factor de marcha de 0.5–1 seleccionado por usuario. Dos exploradores; el factor escala velocidades con el polvo, sin cambiar el modelo fisiológico ni añadir consumibles inexistentes.

La interfaz permite marcar hasta 7 exclusiones circulares manuales (radio de 1–10000 m, acotado a la diagonal del terreno), más la zona de tormenta. Se guardan por región/rejilla y se representan con el resto de restricciones. Cambiarlas invalida la ruta anterior. No hay detección automática de grandes rocas.

El cuaderno web guarda hasta 100 observaciones propias de hasta 600 caracteres, por región/rejilla, con punto, nombre, fecha y origen no verificado. No adjunta fotos binarias ni produce mediciones de sensores. Puede borrar entradas y exportarlas con el plan. Objetivo, equipo, zonas y notas se guardan localmente; la exportación añade metadatos de las capas. Estas notas no sincronizan automáticamente con la bitácora del ESP.

## Módulo 3: app ROVER

El ESP32 incluye un visor independiente de simulación sobre ortofoto cenital de Jezero. Ventana de 200×160 en la TFT 320×240, panel lateral, marcador con rumbo, avance por encoder, pausa/play, escalas ×1/×10/×100 accesibles en tarjetas y tarjetas de llegada por parada. Foto RGB565 en 64 mosaicos LittleFS; buffer de píxeles de 128 bytes, sin incorporar la foto en PROGMEM.

El proveedor geométrico opcional admite START, hasta 8 paradas y END (10 puntos), en píxeles de la ortofoto de 500×500. Sin él hay RUTA DEMO rotulada, con velocidad nominal de 0.04 m/s proyectados y geometría determinista; puede consumir los tiempos del presupuesto como escenario. No transfiere automáticamente la ruta del navegador ni usa el GPS como telemetría de un rover. Los metros son proyectados a 4 m/px; no equivalen exactamente a la escala física corregida del planificador web. No implementa conducción, energía o peligros robóticos. [Especificación completa del módulo](MODULO_ROVER_ESP32.md).

## Reloj Mars OS de dos pantallas

Se incorpora el [firmware Mars OS](RELOJ_MARS_OS.md) recibido: ST7789 principal 320×240 y secundaria 240×240, mapa local de Glen Torridon generado desde el repositorio (136 sitios, 718 objetivos), guía privada HTTPS del celular y misiones públicas temporales de 2–10 puntos. Un relay opcional en la PC sincroniza paradas/restricciones y envía imágenes 3D. Su A* local y tiempos difieren del planificador Python; no amplía el DEM sin conexión a todo Marte ni aporta GPS autónomo. El firmware con OLED y módulo ROVER se conserva como alternativa.

## Organización del espacio de trabajo

En escritorio se conserva la distribución de tres columnas. La altura de los paneles sigue al mapa y no añade información al pie del área de trabajo. Los controles existentes se agrupan en pestañas: Plan, Ciencia, Puntos, Datos y Reloj; Guía, Recorrido, Entorno, Rutas, Notas y Fuentes. La categoría activa presenta el contenido que cabe completo; las secciones largas se consultan con **Ampliar sección**, en una vista completa con cierre por botón o Escape. Se reutilizan los mismos campos y eventos, conservando sus valores. Los enlaces a Plan, Guía, Resultados y Fuentes activan la categoría correspondiente. En pantallas pequeñas las categorías siguen el flujo de la página.
