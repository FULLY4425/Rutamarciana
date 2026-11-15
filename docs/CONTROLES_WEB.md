# Referencia de controles de la página

[Manual web](MANUAL_WEB.md) · [Índice](README.md). Inventario contrastado con `web/index.html` y los módulos de la interfaz el 15 de noviembre de 2026.

Incluye los **89 controles estáticos con identificador**. Los valores son los declarados en el HTML; los catálogos, las voces, los estados habilitado/deshabilitado y ciertos textos se actualizan al cargar datos o cambiar el modo. Los identificadores sirven para mantenimiento, no es necesario conocerlos para usar la página.

## Barra superior y cámara

| Control | ID | Tipo / valores declarados | Acción |
| --- | --- | --- | --- |
| Reloj / celular | `navopen` | button | Abre la guía de bolsillo, GPS y vínculo privado. |
| Atlas de Marte | `planetopen` | button | Abre el globo y selección de regiones. |
| Vista de superficie | `surfaceopen` | button | Abre la reconstrucción si hay ruta y WebGL. |
| Vista 2D / Vista 3D | `viewmode` | button | Cambia de representación; el texto indica el destino. |
| Centrar cámara | `camerareset` | button | Restablece cámara, zoom y seguimiento. |
| Capa | `terrainpalette` | select · natural, elevation, slope | Relieve iluminado, elevación o pendiente. |
| Detalle | `terraindetail` | select · 75, 150 | Estándar 75 / Alto 150 muestras máximas por eje, sin cambiar la rejilla de cálculo. |
| Relieve | `relief` | range · 1–8 | Exageración vertical de la vista aérea, sin alterar el cálculo. |

## Edición junto al mapa

| Control | ID | Tipo / valores declarados | Acción |
| --- | --- | --- | --- |
| Salida A | `editstart` | button | Coloca o modifica la salida. |
| Destino B | `editend` | button | Coloca o modifica el destino. |
| Parada | `editstop` | button | Añade una intermedia; requiere extremos y evita duplicados. |
| Zonas a evitar | `editzone` | button | Abre las exclusiones manuales. |
| Mover mapa | `editpan` | button | Permite desplazar sin marcar puntos. |
| Calcular rutas | `mapcompute` | button | Mismo cálculo que Plan; requiere puntos completos. |
| Ver en 2D / Ver en 3D | `mapdimension` | button | Acceso al cambio de vista. |
| Acercar mapa | `mapzoomin` | button | Aumenta zoom. |
| Alejar mapa | `mapzoomout` | button | Reduce zoom. |
| Radio de exclusión | `obstacleradius` | number · 1–10000 | Hasta siete áreas manuales; radio no mayor que la diagonal del terreno. |
| Marcar exclusión en el mapa | `obstacleplace` | button | Arma/cancela el modo de colocación del centro. |
| Acercar | `zin` | button | Control superpuesto de la vista 2D. |
| Alejar | `zout` | button | Control superpuesto de la vista 2D. |
| Restablecer vista | `zreset` | button | Restablece el zoom/desplazamiento 2D. |

## Atlas global

| Control | ID | Tipo / valores declarados | Acción |
| --- | --- | --- | --- |
| Volver a la región | `planetclose` | button | Cierra sin abrir una región nueva. |
| Selección del atlas | `planetbookmark` | select · opciones dinámicas | Opciones cargadas desde el catálogo del atlas. |
| Latitud del atlas | `planetlat` | number · -90–90 | Coordenada planetaria en grados. |
| Longitud este del atlas | `planetlon` | number · -180–360 | Coordenada planetaria en grados. |
| Abrir región | `planetload` | submit | Abre la región MOLA o recorte seleccionado; el texto cambia con el marcador. |
| Jezero · detalle HiRISE de 4 m | `planetjezero` | button | Acceso directo al recorte Jezero. |

## Vista de superficie

| Control | ID | Tipo / valores declarados | Acción |
| --- | --- | --- | --- |
| Volver al mapa | `surfaceclose` | button | Cierra la reconstrucción de superficie. |
| −5 m | `surfaceback` | button | Retrocede manualmente por el recorrido. |
| +5 m | `surfaceforward` | button | Avanza manualmente por el recorrido. |
| Mirar hacia la ruta | `surfacerecenter` | button | Restablece orientación de mirada y campo de visión. |

## Plan

| Control | ID | Tipo / valores declarados | Acción |
| --- | --- | --- | --- |
| Presupuesto EVA | `budget` | select · 120, 180, 240, 360 | Minutos disponibles; inicialmente 240. |
| Paradas científicas | `nstops` | select · 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18 | Intermedias, además de A y B; inicialmente 0. |
| Ciencia por parada | `science` | select · 10, 20, 30 | Minutos por parada; inicialmente 20. |
| Pendiente máxima simulada | `maxslope` | range · 5–40 | Grados; inicialmente 25. |
| Calcular rutas | `compute` | button | Solicita FAST/SAFE y presupuesto a Python. |
| Borrar puntos | `reset` | button | Reinicia trazado/hogar, no toda la personalización local. |

## Ciencia

| Control | ID | Tipo / valores declarados | Acción |
| --- | --- | --- | --- |
| Objetivo científico | `missiongoal` | textarea · hasta 600 caracteres | Intención de la misión. |
| Hipótesis / procedimiento | `missionhypothesis` | textarea · hasta 600 caracteres | Qué comparar y cómo observar. |
| Equipo transportado | `missionequipment` | input · hasta 300 caracteres | Descripción del equipo. |
| Carga informativa (kg) | `missionmass` | number · 0–100 | Dato descriptivo, sin penalización automática por kg. |
| Factor de marcha con equipo | `missionmobility` | number · 0.5–1; paso 0.05 | Ajusta tiempos de marcha del escenario; inicialmente 1. |

## Puntos

| Control | ID | Tipo / valores declarados | Acción |
| --- | --- | --- | --- |
| Abrir Glen Torridon | `openglen` | button | Cambia al recorte de los 718 registros. |
| Clasificación | `targetfilter` | select · all, veins, nodules | Todos, vetas o nódulos. |
| Buscar objetivo científico | `targetsearch` | search | Nombre, formación, miembro o sol. |
| Objetivo científico | `targetselect` | select · opciones dinámicas | Lista cargada por región y filtro. |
| Añadir objetivo al recorrido | `targetadd` | button | Añade el objetivo seleccionado a su celda, sin duplicarla. |
| Nombre del punto | `pointname` | input · hasta 80 caracteres | Se genera uno si está vacío. |
| Marcar por clic en el mapa | `pointplace` | button | Guarda un punto; después Añadir lo incorpora al recorrido. |
| Latitud del punto | `pointlat` | number | Grados de Marte dentro del recorte activo. |
| Longitud este del punto | `pointlon` | number | Grados de Marte dentro del recorte activo. |
| Guardar por coordenadas | `pointcoordinates` | button | Crea un punto guardado si la ubicación es válida. |

## Datos

| Control | ID | Tipo / valores declarados | Acción |
| --- | --- | --- | --- |
| Fotografía HiRISE real | `orthophoto` | checkbox | Se habilita tras verificar imagen/DEM compatible. |
| Superponer observaciones químicas | `chemistryvisible` | checkbox | Muestra/oculta marcadores históricos en Glen Torridon. |
| Óxido | `chemistryoxide` | select · opciones dinámicas | Selecciona el óxido de la leyenda; inicialmente CaO. |
| Combinar óptica + relieve + química | `chemistrycombine` | button | Activa foto compatible y marcadores, si están disponibles. |

## Guía

| Control | ID | Tipo / valores declarados | Acción |
| --- | --- | --- | --- |
| Activar / desactivar voz | `voice` | button | Avisos automáticos de la ruta. |
| Repetir indicación | `repeatdirection` | button | Repite la disponible. |
| Voz española del dispositivo | `voice-choice` | select · opciones dinámicas | Opciones suministradas por el navegador. |
| Ritmo | `voice-rate` | range · 0.8–1.4; paso 0.1 | Velocidad de la síntesis. |
| Volumen | `voice-volume` | range · 0–1; paso 0.1 | Nivel de la síntesis. |
| Probar voz | `voice-test` | button | Permite probar la configuración. |

## Recorrido

| Control | ID | Tipo / valores declarados | Acción |
| --- | --- | --- | --- |
| Ruta a seguir | `trackroute` | select · safe, fast | SAFE o FAST. |
| Iniciar / Pausar / Continuar / Repetir | `play` | button | Controla la reproducción por tiempo cuando no hay GPS activo. |
| Avance del recorrido | `progress` | range · 0–1000 | Posición simulada de 0 a 100%; el control interno usa 0–1000. |
| Cámara de seguimiento | `followcamera` | checkbox | Centra la cámara en la posición activa. |
| Reproducción | `playbackspeed` | select · summary, 1, 5, 20 | Resumen de un minuto o 1×/5×/20×. |
| Exportar misión JSON | `export` | button | Descarga una misión calculada, no un historial GPS. |

## Entorno

| Control | ID | Tipo / valores declarados | Acción |
| --- | --- | --- | --- |
| Polvo simulado | `weather` | select · clear, dust, storm | Despejado 1.0; polvo 0.8; tormenta 0.6 y una exclusión hipotética. |
| Simular desvío de más de 30 m | `deviate` | button | Busca una posición lateral para recalcular. |
| Marcar posición simulada en el mapa | `setposition` | button | Arma/cancela selección manual desde donde recalcular. |
| Reintentar desde posición actual | `retryroute` | button | Repite el cálculo con el contexto de recálculo disponible. |

## Notas

| Control | ID | Tipo / valores declarados | Acción |
| --- | --- | --- | --- |
| Parada | `notepoint` | select · opciones dinámicas | Lista de puntos de la misión activa. |
| Observación propia | `notetext` | textarea · hasta 600 caracteres | Texto del usuario, no medición NASA; hasta 100 notas por región. |
| Guardar observación | `noteadd` | button | Guarda localmente una observación asociada a la parada. |

## Reloj Mars OS: código público

| Control | ID | Tipo / valores declarados | Acción |
| --- | --- | --- | --- |
| Enviar al reloj · crear código | `psionsend` | button | Comparte 2–10 puntos Glen Torridon; código público de 24 h. |
| Código del reloj | `psioncode` | text · hasta 4 caracteres | Cuatro caracteres; distinto del vínculo privado. |
| Abrir del reloj | `psionreceive` | button | Recupera la misión y la recalcula con Python. |

## Guía de bolsillo y vínculo privado

| Control | ID | Tipo / valores declarados | Acción |
| --- | --- | --- | --- |
| Volver al mapa | `navclose` | button | Cierra la guía de bolsillo; no sustituye a Detener GPS. |
| Activar / desactivar voz | `navvoice` | button | Mismo motor y configuración de Guía. |
| Iniciar / pausar simulación | `navsimulate` | button | Solo cuando no está activo el GPS. |
| Rotación respecto al norte terrestre | `navrotation` | number · 0–359; paso 1 | Se fija al vincular la posición; inicialmente 0°. |
| Activar GPS del celular | `navgps` | button | Solicita permiso; requiere ruta corta HiRISE y contexto compatible. |
| Vincular aquí al punto actual de la ruta | `navanchor` | button | Ancla una lectura precisa al punto actual del recorrido. |
| Detener GPS | `navstopgps` | button | Detiene el seguimiento y permite volver a simulación. |
| Recalcular desde mi posición GPS | `navreplan` | button | Recalcula desde la posición vinculada válida. |
| Crear vínculo de 30 minutos | `navpair` | button | Genera ID y credenciales para indicaciones privadas al ESP. |
| Desvincular | `navunpair` | button | Revoca la sesión del servidor. |

## Controles y contenido generados durante el uso

| Ubicación | Elementos | Comportamiento |
| --- | --- | --- |
| Pestañas laterales | `tab-plan`, `tab-science`, `tab-points`, `tab-data`, `tab-watch`; `tab-guide`, `tab-playback`, `tab-environment`, `tab-results`, `tab-notes`, `tab-sources` | Activan una categoría por lado; flechas, Inicio y Fin permiten navegar con teclado. |
| Sección activa | Ampliar sección | Abre el contenido completo reutilizando los mismos campos y valores. |
| Vista ampliada | Volver al mapa / Escape | Cierra y devuelve el contenido a su categoría. |
| Puntos personales | Añadir / Eliminar | Añadir incorpora al recorrido; Eliminar borra de la lista local. |
| Lista del recorrido | Quitar | Retira esa ubicación y reorganiza A/paradas/B. |
| Exclusiones | Quitar | Elimina una zona manual y exige recalcular. |
| Notas | Eliminar | Borra una observación local. |
| Detalle químico | Disparo químico observado | Selecciona una observación y muestra sus nueve óxidos/RMSEP. |
| Atlas | Marcadores del catálogo | Cambian selección/coordenadas y el texto del botón de apertura. |
| Guía | Próximas indicaciones | Despliega las maniobras; no cambia la ruta calculada. |
| Fuentes | Ortofoto de referencia y enlaces | Consulta la referencia y abre las fuentes originales. |
| Reloj / ESP | Código privado, código público y estados | Se generan por petición; no son credenciales intercambiables. |

## Salidas de estado y mensajes

Los estados de región, terreno, leyenda/fotografía, objetivos, química, puntos, obstáculos, recálculo, voz, GPS, vínculo, notas y misión compartida se muestran junto a cada herramienta. Los resultados FAST/SAFE se generan dentro de `cards`. No son botones ni requieren copiarse como parámetros.

Las interacciones del lienzo, atajos de teclado, significados del presupuesto y procedimientos completos están en el [manual](MANUAL_WEB.md); los errores y acciones de recuperación, en [Solución de problemas](SOLUCION_DE_PROBLEMAS.md).
