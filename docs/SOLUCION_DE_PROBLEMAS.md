# Solución de problemas de Ruta Marciana

[Manual web](MANUAL_WEB.md) · [Índice](README.md). Comprueba primero la región activa, el modo simulado/GPS y el mensaje de estado de la herramienta.

## Acceso, pantalla y categorías

| Síntoma | Qué comprobar y cómo actuar |
| --- | --- |
| La web tarda en abrir | El servicio Render gratuito puede estar despertando. Mantén la pestaña abierta y vuelve a probar cuando responda. [/health](https://rutamarciana.onrender.com/health) permite comprobar la respuesta básica, pero no garantiza que una ruta sea válida. |
| Sigo viendo una interfaz anterior | Guarda/exporta la misión antes de recargar. En computadora usa Ctrl+F5; en el celular vuelve a cargar la página. Comprueba que sea el dominio y rama de la versión actual. |
| No encuentro una opción que antes estaba abajo | Usa las pestañas laterales: Plan/Ciencia/Puntos/Datos/Reloj y Guía/Recorrido/Entorno/Rutas/Notas/Fuentes. En celular también están los accesos inferiores. |
| Solo aparece una descripción de la categoría | Pulsa Ampliar sección: el contenido es demasiado largo para la columna de escritorio y se consulta completo en esa vista. |
| Estoy dentro de una sección ampliada | Volver al mapa o Escape la cierran. Se reutilizan los mismos campos, no una copia vacía. |
| El mapa está en blanco mientras carga | Revisa el estado del terreno y espera la respuesta del servidor. Si falla una región, vuelve a elegirla y comprueba conexión. Una capa inexistente no se reemplaza con mediciones inventadas. |
| Aparece una demo sintética | Es otro modo explícito de datos, como `python iniciar.py --demo`; no es HiRISE. Comprueba el terreno seleccionado y el comando de arranque. |

## Puntos y cálculo

| Síntoma o mensaje | Qué hacer |
| --- | --- |
| Toco el mapa y no marca | Revisa el modo: Mover mapa no coloca puntos. Elige Salida A, Destino B o Parada. Un arrastre se interpreta como navegación, no como toque. |
| No deja añadir una parada | Marca primero A y B. Revisa el límite de 18 intermedias/20 puntos totales y que la celda no esté ya en el recorrido. |
| Guardé un punto pero no aparece en la ruta | En Puntos, pulsa Añadir junto al punto guardado. Guardarlo y añadirlo son operaciones diferentes. |
| Las coordenadas quedan fuera del recorte | Usa grados de latitud y longitud este de Marte, en la región activa. Cambia de región si el lugar no pertenece a ese recorte. No introduzcas allí GPS terrestre. |
| Dos objetivos no se pueden añadir a la vez | Pueden corresponder a la misma celda del DEM. La planificación rechaza ubicaciones duplicadas, aunque los registros científicos sean distintos. |
| Calcular junto al mapa está deshabilitado | Deben existir salida, destino y todas las paradas previstas en Plan. Espera si hay un cálculo en curso. |
| No existe ruta transitable / waypoint intransitable | Comprueba pendientes, exclusiones, escenario y celdas sin elevación válida. Elige otros puntos o modifica conscientemente el escenario y vuelve a calcular. |
| NINGUNA RUTA CABE | Es un problema del presupuesto del escenario, no necesariamente de conectividad. Acorta la ruta, reduce ciencia/paradas o cambia presupuesto y vuelve a comparar. |
| La ruta desaparece al editar | Las ediciones invalidan el cálculo anterior para evitar mezclar resultados antiguos. Calcula de nuevo. |
| No puedo exportar | Primero calcula una misión válida. Si hubo una edición o error posterior, vuelve a calcular antes de exportar. |

No conviertas una ruta bloqueada en una instrucción de avance real. Los cambios de pendiente o escenario alteran los supuestos, no eliminan peligros físicos de la Tierra o Marte.

## Fotografía, química y regiones

| Síntoma | Qué comprobar |
| --- | --- |
| La casilla de ortofoto está deshabilitada | Espera la verificación y usa un recorte con imagen compatible. La imagen de referencia Jezero no sirve para cualquier DEM. |
| En el globo solo veo colores de elevación | Es el atlas MOLA. No contiene una ortofoto HiRISE de todo Marte. |
| No aparecen los 718 registros en Jezero | Son de Glen Torridon/Gale. Abre Puntos → Abrir Glen Torridon. |
| No se muestran datos químicos | La química cuantitativa integrada corresponde a Glen Torridon. Selecciona un objetivo de esa región y consulta Datos. |
| Un objetivo no tiene valores químicos | No todos los registros tienen disparos cuantitativos en las tablas integradas. La página lo indica y no infiere valores. |
| Combinar óptica + relieve + química no se activa | Abre Glen Torridon y espera catálogo, química y verificación de ortofoto. Lee el estado de Datos. |
| Al cambiar región faltan mis puntos o notas | Se separan por región y rejilla. Vuelve al mismo ámbito; no se copian automáticamente a otro recorte. |

## Superficie y reproducción

Si **Vista de superficie** está deshabilitada, calcula una ruta y comprueba que el navegador tenga WebGL. El mapa 2D y la planificación no requieren entrar a esa vista. Si se pierde el contexto gráfico, cierra la superficie y recarga después de guardar lo necesario.

La superficie puede mostrar color ilustrativo en lugar de foto si la ortofoto no coincide o todavía carga. El relieve aéreo exagerado no se aplica a la cámara de 1.70 m. Las zonas sin DEM válido no generan un suelo ficticio.

Si **Iniciar** no avanza, revisa que exista ruta, que no esté completada/pausada y que no esté activo el GPS. Durante GPS el avance depende del desplazamiento vinculado; no se puede combinar con la reproducción por tiempo. Si la cámara se mueve al explorar, desmarca Cámara de seguimiento o centra la vista.

El modo Resumen dura aproximadamente un minuto de reproducción; no significa que la caminata modelada dure un minuto.

## Voz

1. Calcula una ruta y pulsa Probar voz o Activar voz con una acción directa del usuario.
2. Comprueba volumen del dispositivo, pestaña no silenciada y volumen de la página mayor que cero.
3. Elige una voz española que realmente aparezca en el selector. Puede ser necesario habilitar voces en el sistema operativo o esperar a que el navegador las anuncie.
4. Si una voz falla, prueba otra o la selección automática.

La aplicación no instala voces en tu equipo ni configura una cuenta de síntesis externa. La calidad y disponibilidad dependen del sistema. Repetir indicación puede estar deshabilitado si no hay maniobra disponible.

## GPS del celular

| Síntoma | Acción |
| --- | --- |
| El navegador no ofrece GPS | Abre HTTPS en el teléfono, por ejemplo Render. HTTP de la IP de otra PC normalmente no habilita geolocalización. |
| Permiso denegado | Revisa permisos del sitio y ubicación del sistema en el navegador/celular. Vuelve a activar GPS desde la página. |
| Esperando señal o precisión peor que ±25 m | Espera una lectura válida en un lugar despejado. No se habilita avance con una ubicación imprecisa o de más de 15 s. |
| Hay señal pero sigo en espera | Pulsa Vincular aquí después de elegir la rotación del recorrido y situarte en el origen de entrenamiento. |
| Mi posición apunta hacia otro lado | Comprueba la rotación 0°–359° y vuelve a vincular. Es una referencia Tierra–Marte fijada explícitamente, no un mapa de calles. |
| Me salí del tramo / del recorte | Vuelve al área vinculada o recalcula desde una posición válida. La separación admisible del tramo activo es de 30 m; el marco local se limita a 5 km. |
| Se detiene al bloquear el celular o cambiar de pestaña | El GPS se detiene cuando la página deja de estar visible. Reabre, activa y vuelve a vincular cuando sea necesario. No hay garantía de segundo plano. |
| No permite entrenar en MOLA | Usa Jezero o Glen Torridon: la resolución MOLA es insuficiente para entrenamiento a pie. |

## ESP32, códigos y misiones

**Guía privada sin respuesta:** conecta la placa a una red con internet; espera sincronización de hora para TLS; comprueba el código completo `ID.TOKEN` en minúsculas. Si caducó, se revocó o reinició el servidor, crea un vínculo nuevo de 30 minutos. La placa debe pasar a espera ante datos antiguos, no seguir mostrando una maniobra como si fuera vigente.

**Código de cuatro caracteres que no abre:** es del buzón público Mars OS, no del enlace privado de navegación. Comprueba caracteres, las 24 h de duración y si el servidor se reinició/desplegó. Un 404 puede significar código inexistente o caducado. Un 429 significa buzón lleno: no se borran misiones ajenas para aceptar otra.

**No puedo enviar una ruta al reloj:** el buzón admite Glen Torridon y 2–10 puntos. No envía una ruta general de 20 puntos ni otras regiones al mapa local de ese firmware.

**El reloj indica DIFERENTE:** su A* local y su estimación de tiempo no son FAST/SAFE Python. Comparar métricas puede mostrar diferencias esperadas. Para seguir las indicaciones calculadas por la web usa Guía GPS del celular.

**El reloj no muestra la fotografía de ROVER:** ese módulo de la base Space Apps requiere sus mosaicos LittleFS aparte del sketch. No los confundas con el mapa comprimido incluido en Mars OS. Respaldar archivos antes de cambiar firmware/filesystem; consulta las guías específicas, no formatees como primer diagnóstico.

Hardware y compilación: [Mars OS](RELOJ_MARS_OS.md), [ESP32 anterior](NAVEGADOR_ESP32.md), [ROVER](MODULO_ROVER_ESP32.md).

## Servidor local y pérdida de datos

Si Python no se reconoce, instala Python 3.10+ y revisa PATH; en Windows también puedes probar `py iniciar.py`. Para puerto ocupado usa `python iniciar.py --port 8090`. Para fallos al preparar dependencias comprueba internet y permisos de escritura; ejecuta `python iniciar.py --check` para obtener el diagnóstico.

Si la web abre pero no calcula al usar `python -m http.server`, arráncala con `python iniciar.py`: necesita la API Python. Si falta un paquete o el manifiesto es incompatible, no mezcles archivos de distintos builds. [Guía local](../INICIAR_SERVIDOR.md).

Puntos/notas guardados en un dominio no aparecen automáticamente en otro navegador, modo privado, otra PC o localhost. El trazado y el anclaje GPS son memoria de la página. No se recuperan como una sesión de cuenta. El JSON descargado es una copia para conservar, pero no hay un importador general de ese archivo en la interfaz.

## Qué incluir al reportar un fallo

Indica región, pestaña/herramienta, modo 2D/3D o superficie, navegador, computadora/celular, secuencia de pasos y mensaje exacto. Una captura o video del límite visual ayuda a distinguir altura de mapa, formularios y desplazamiento. Para un fallo de ruta, adjunta el JSON exportado si existe y puedes compartir sus puntos/notas. No incluyas el vínculo privado `ID.TOKEN` ni contraseñas de Wi-Fi.
