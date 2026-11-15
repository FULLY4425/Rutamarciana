# Manual completo de la página Ruta Marciana

Estado: 15 de noviembre de 2026. Describe la interfaz de la rama `feat/ruta-marciana-servidor-local`, incluida la organización lateral actual. [Índice de documentación](README.md) · [Referencia de controles](CONTROLES_WEB.md) · [Problemas frecuentes](SOLUCION_DE_PROBLEMAS.md).

## 1. Entrar y reconocer los modos

Abre **https://rutamarciana.onrender.com** desde una computadora o celular. No necesitas instalar Python para usar la versión pública. El servicio gratuito puede tardar en responder después de estar inactivo. La alternativa para tu propia PC está en [Servidor local](../INICIAR_SERVIDOR.md).

Ruta Marciana sirve para explorar datos de Marte y planificar recorridos educativos. Sus tres modos principales son:

| Modo | Qué representa |
| --- | --- |
| Exploración y planificación | Mapa orbital, relieve, objetivos y rutas calculadas por Python. |
| Recorrido simulado | Dos personajes avanzan por la ruta según la reproducción elegida. No es una ubicación real. |
| Entrenamiento con celular | Tus desplazamientos GPS terrestres se vinculan explícitamente a una ruta de Marte, a escala 1:1. |

El mapa global no tiene la resolución del recorte HiRISE. La vista de superficie reconstruye el DEM y no contiene panoramas fotográficos tomados desde el suelo. Las métricas son un modelo educativo, no una certificación de seguridad para astronautas ni navegación por calles terrestres.

## 2. Organización de la pantalla

En escritorio se mantienen tres columnas: **planificación**, **mapa** y **recorrido**. El área principal termina al pie del mapa. La información extensa se organiza en pestañas, sin continuar como una columna interminable debajo.

| Lado | Pestaña | Contenido |
| --- | --- | --- |
| Izquierdo | Plan | Presupuesto, paradas, tiempo científico, pendiente, cálculo y reinicio. |
| Izquierdo | Ciencia | Objetivo, hipótesis, equipo, carga y factor de marcha. |
| Izquierdo | Puntos | Catálogo científico, puntos propios y lista del recorrido. |
| Izquierdo | Datos | Procedencia del terreno, ortofoto y química ChemCam. |
| Izquierdo | Reloj | Intercambio de misiones mediante códigos con Mars OS. |
| Derecho | Guía | Próxima indicación, distancias y voces. |
| Derecho | Recorrido | Reproducción, cámara de seguimiento, exportación y perfil. |
| Derecho | Entorno | Polvo simulado, desvíos y recálculo. |
| Derecho | Rutas | Comparación FAST/SAFE, tramos y presupuesto EVA. |
| Derecho | Notas | Observaciones propias asociadas a paradas. |
| Derecho | Fuentes | Catálogo NASA/USGS, imagen de referencia, alcance y créditos. |

Pulsa una pestaña para seleccionarla. Con el foco en las pestañas también puedes usar **← / →**, **Inicio** y **Fin**. El botón **Ampliar sección** abre la categoría en una vista completa; **Volver al mapa** o **Escape** la cierra. Los campos son los mismos: conservan sus valores al ampliar o volver. Si una categoría es demasiado extensa para la columna de escritorio, aparece una descripción y se consulta completa mediante ese botón.

En celular la disposición es vertical. La barra inferior lleva a **Mapa / trazar**, **Plan**, **Guía** y **Resultados**. Los enlaces a Plan, Guía, Resultados y Fuentes activan su categoría, aunque estuviera oculta. En pantallas táctiles pequeñas el mapa comienza en 2D; puedes cambiar a 3D.

## 3. Primer recorrido completo

1. Abre **Atlas de Marte** y elige Jezero, o entra en **Puntos → Abrir Glen Torridon** para trabajar con el catálogo científico.
2. Junto al mapa pulsa **Salida A** y toca una celda; después **Destino B** y otra celda.
3. Si necesitas paradas intermedias, pulsa **＋ Parada** y marca cada una, o añádelas desde **Puntos**. La salida y el destino se conservan al añadir paradas intermedias.
4. En **Plan**, elige presupuesto, minutos de ciencia por parada y pendiente máxima.
5. Si lo necesitas, añade objetivo/equipo en **Ciencia** y restricciones en **Zonas a evitar** o **Entorno**.
6. Pulsa **Calcular rutas**. El acceso junto al mapa y el de Plan usan el mismo cálculo Python.
7. Abre **Rutas** para comparar FAST/SAFE. Si no cabe una misión bajo los supuestos, acórtala o ajusta sus parámetros y vuelve a calcular.
8. En **Recorrido**, elige la variante y pulsa **Iniciar**. Activa la voz en **Guía** si la necesitas.
9. Guarda observaciones en **Notas** y descarga **Exportar misión JSON** desde **Recorrido**.

Para una demostración corta usa puntos cercanos. Recorrer todo un recorte o una región MOLA puede requerir tiempos incompatibles con el presupuesto seleccionado.

## 4. Barra superior y mapa central

### Herramientas superiores

- **Reloj / celular:** abre la guía de bolsillo y los controles de GPS y vínculo privado con ESP32.
- **Atlas de Marte:** abre el globo MOLA y el selector de regiones.
- **Vista de superficie:** entra en la reconstrucción a nivel del terreno; requiere una ruta y WebGL disponible.
- **Capas y opciones de vista:** contiene cambio 2D/3D, centrado, capa, detalle y relieve. En celular este grupo comienza cerrado.

### Capas y cámara

| Control | Uso |
| --- | --- |
| Vista 2D / Vista 3D | El texto indica la vista a la que puedes cambiar. El acceso junto al mapa hace lo mismo. |
| Centrar cámara | Restablece orientación, acercamiento y desplazamiento; desactiva el seguimiento de cámara. |
| Capa | Relieve iluminado, Elevación o Pendiente. Son representaciones del mismo DEM. |
| Detalle | Estándar: hasta 75 muestras por eje; Alto: hasta 150, incluyendo bordes. No aumenta la resolución científica de origen. |
| Relieve | Exageración visual entre 1 y 8; no cambia alturas ni pendientes usadas por Python. |
| Acercar / Alejar | Botones disponibles junto al mapa; la vista 2D también muestra controles superpuestos y restablecimiento. |

**Con ratón:** un clic marca un punto según el modo activo. Arrastrar gira/inclina en 3D y desplaza la vista 2D ampliada. Mayús + arrastre, botón central o **Mover mapa** permiten desplazar la vista 3D. La rueda acerca/aleja. Con foco en el mapa 3D, las flechas desplazan, `+`/`-` cambian zoom y `0` centra.

**Con pantalla táctil:** un toque marca; arrastrar explora; dos dedos acercan y desplazan sin añadir puntos. **Mover mapa** permite desplazar con un dedo también en 3D. Los movimientos pequeños del dedo se toleran al seleccionar.

### Salida, destino, paradas y restricciones

- **Salida A / Destino B:** colocan o cambian ese extremo sin borrar todo el recorrido. No se aceptan puntos duplicados en la misma celda.
- **＋ Parada:** añade una parada científica intermedia cuando ya existen A y B. Se permiten hasta 18: 20 puntos contando ambos extremos.
- **Mover mapa:** los toques dejan de añadir puntos mientras desplazas la vista; elige otro modo para marcar de nuevo.
- **Zonas a evitar:** abre las exclusiones manuales. Introduce radio y pulsa **Marcar exclusión en el mapa**; luego toca el centro. **Quitar** elimina una zona de la lista.

Se admiten siete áreas manuales, más la zona hipotética de tormenta. El radio debe ser de 1 a 10,000 m y no superar la diagonal del terreno. Son anotaciones del usuario, no obstáculos detectados por NASA. Tras cambiarlas, recalcula.

## 5. Atlas global y elección de región

Abre **Atlas de Marte**. Arrastra el globo para girarlo; selecciona una región predefinida o pulsa el planeta para situar el selector. También puedes escribir latitud y longitud este y usar **Abrir región MOLA · 400 km**. El botón de apertura cambia de texto cuando el marcador elegido corresponde a un recorte HiRISE. **Jezero · detalle HiRISE de 4 m** es un acceso directo. **Volver a la región** cierra el atlas sin elegir un recorte nuevo.

| Región | Datos disponibles | Uso adecuado |
| --- | --- | --- |
| Jezero | Recorte DEM de 500×500 celdas, 4 m por celda, y ortofoto compatible. | Rutas locales y entrenamiento vinculado al celular. |
| Glen Torridon / Gale | DEM de 72×122 celdas, ~12.14 m norte-sur y ~12.10 m este-oeste, ortofoto, 718 objetivos y química integrada. | Trabajo científico del catálogo y rutas locales. |
| Otras regiones de Marte | MOLA global de ~3.7 km nativos, con ventana regional de 400 km. | Contexto regional y planificación educativa a baja resolución. |

Las latitudes se expresan en grados y las longitudes hacia el este; el formulario admite latitud −90 a 90 y longitud −180 a 360. La cobertura MOLA incluye polos y cruce de meridianos. Seleccionar otra región reinicia el recorrido activo y separa los puntos/notas guardados de cada ámbito. El atlas usa elevaciones reales con colores ilustrativos; no es una fotografía global HiRISE.

## 6. Pestaña Plan

| Parámetro | Valores de la interfaz | Efecto |
| --- | --- | --- |
| Presupuesto EVA | 120, 180, 240 o 360 min; inicialmente 240. | Tiempo disponible para evaluar la misión. |
| Paradas científicas | 0–18. | Número de ubicaciones intermedias que debes marcar. A y B son adicionales. |
| Ciencia por parada | 10, 20 o 30 min; inicialmente 20. | Tiempo de trabajo en cada parada científica intermedia. |
| Pendiente máxima simulada | 5°–40°; inicialmente 25°. | Límite para celdas y pasos transitables del cálculo. |

**Calcular rutas** requiere A, B y el número de paradas previsto. El cálculo considera ida y regreso al hogar. **Borrar puntos** reinicia el trazado activo; no elimina el catálogo NASA ni los puntos personales almacenados.

El aviso de recomendación puede indicar **AMBAS CABEN**, **SOLO CABE LA RUTA…** o **NINGUNA RUTA CABE bajo los supuestos del escenario**. Que una ruta se pueda calcular no significa que quepa en el presupuesto. Reducir el número de paradas también puede quitar paradas ya seleccionadas; revisa la lista del recorrido antes de recalcular.

## 7. Pestaña Ciencia

Escribe el **objetivo científico** y la **hipótesis / procedimiento**: hasta 600 caracteres cada uno. Describe el **equipo transportado**: hasta 300 caracteres.

La **carga informativa** admite 0–100 kg, pero se guarda como descripción: no crea automáticamente una penalización fisiológica. El **factor de marcha** admite 0.5–1 en pasos de 0.05; un valor menor modifica los tiempos de ida y regreso. No representa consumibles reales del traje.

La misión considera dos exploradores. El texto de contexto aclara que las observaciones de Glen Torridon pertenecen a una campaña histórica y que las condiciones orbitales no equivalen a meteorología actual. Cambiar parámetros científicos puede invalidar el cálculo anterior; vuelve a calcular.

## 8. Pestaña Puntos

### Objetivos científicos

**Abrir Glen Torridon · HiRISE / 718 registros** cambia al recorte del catálogo. Filtra por **Todos**, **Vetas** o **Nódulos** y busca por nombre, formación, miembro o sol. Al seleccionar un registro aparecen coordenadas, sol, formación, miembro y clasificación. **Añadir objetivo al recorrido** lo incorpora como salida, destino o parada según lo que ya hayas definido.

Los objetivos se ajustan a la celda más cercana para planificar. Distintos registros pueden compartir celda y no se añaden como paradas duplicadas. Estos 718 registros no se trasladan a Jezero ni a otras regiones.

### Puntos personales

1. Escribe un nombre, hasta 80 caracteres; si está vacío se genera uno.
2. Pulsa **Marcar por clic en el mapa** y toca el lugar, o introduce latitud/longitud y pulsa **Guardar por coordenadas**.
3. El punto queda guardado en la lista local. Pulsa **Añadir** para incorporarlo al recorrido; guardarlo no lo añade automáticamente.
4. **Eliminar** quita el punto guardado. **Quitar**, en la lista del recorrido, quita esa parada de la misión; no es la misma operación.

Se guardan hasta 100 puntos personales por región en este navegador. La lista del recorrido muestra el orden A → paradas → B. Al quitar extremos se reorganiza la lista resultante; comprueba cuál pasa a ser la nueva salida o destino.

## 9. Pestaña Datos

### Terreno y ortofoto

**Base científica** muestra origen, tamaño de la rejilla, resolución, rango de alturas y leyenda de la capa. La casilla **Fotografía HiRISE real en 2D y sobre el relieve 3D** solo se habilita tras comprobar que la imagen corresponde al DEM y al recorte activo.

La imagen orbital incluida es en escala de grises. El color de relieve/elevación es ilustrativo. El recorte visual y el detalle de la malla no cambian la rejilla completa usada para calcular rutas.

### Química ChemCam

Disponible en Glen Torridon: **878 observaciones cuantitativas de 320 objetivos**. El total difiere de los 718 registros del catálogo, porque no todos tienen datos químicos integrados.

Activa **Superponer observaciones químicas** y elige **CaO, SiO2, FeOT, MgO, Al2O3, Na2O, K2O, TiO2 o MnO**. Los círculos cian → violeta representan la mediana observada por objetivo para el óxido seleccionado, dentro del rango de la tabla. No interpolan composición entre puntos ni describen todo Marte.

Selecciona un objetivo desde **Puntos** para consultar su detalle químico en **Datos**. Si hay varios disparos observados, el selector permite elegir uno. Se muestran wt%, RMSEP, secuencia, sol y enlace PDS. RMSEP es error de predicción, no un intervalo de confianza. Las coordenadas son las del objetivo, no del disparo individual.

**Combinar óptica + relieve + química** activa la ortofoto compatible y los marcadores químicos. Si la región no tiene esos datos o la foto no está verificada, el estado explica qué falta; no se generan valores de relleno.

## 10. Pestaña Guía

Muestra símbolo de maniobra, distancia hasta la indicación y distancia restante. Las maniobras incluyen salida, giros, paradas de ciencia, destino y regreso. **Próximas indicaciones** despliega la lista del recorrido.

- **Activar voz:** inicia/desactiva los avisos automáticos.
- **Repetir indicación:** repite la disponible; puede estar deshabilitado si no hay ruta.
- **Voz española del dispositivo:** elige entre las voces que ofrece tu navegador; la selección automática es el valor inicial.
- **Ritmo:** 0.8–1.4. **Volumen:** 0–1. **Probar voz** permite escucharlo antes de empezar.

La síntesis ocurre en el dispositivo que abre la página. No requiere suscripción ni una API de voz externa, pero la disponibilidad y calidad dependen del sistema/navegador; algunas voces del sistema pueden necesitar conexión. En modo simulado la posición procede de la reproducción, no del GPS.

## 11. Pestaña Recorrido

Elige **SAFE · conservadora** o **FAST · rápida** y pulsa **Iniciar**. El botón pasa a **Pausar**, **Continuar** o **Repetir** según el estado. La barra de avance permite situar manualmente a los personajes; son dos figuras virtuales, ampliadas en vista aérea y de escala humana en superficie.

**Cámara de seguimiento** centra el recorrido en la posición activa. La reproducción admite:

- **Resumen · 1 minuto:** comprime el recorrido para demostración.
- **1× · desplazamiento modelado**, **5×** y **20×**: avance basado en el tiempo del modelo y su aceleración seleccionada.

La animación incluye ida y regreso. El **perfil del recorrido** muestra elevación muestreada y posición sobre el trazado; es una representación de la malla gráfica y no reemplaza los cálculos del presupuesto. Durante GPS activo se deshabilitan la reproducción por tiempo y el cambio de perfil.

**Exportar misión JSON** descarga `ruta-marciana-mision.json`. Requiere una misión calculada y contiene región, puntos nombrados, FAST/SAFE, regreso, métricas, supuestos, restricciones, capas/procedencia y observaciones propias. Se identifica como simulación; no es un historial GPS. La página no ofrece un importador general de ese archivo: los códigos Mars OS son otro mecanismo de intercambio.

## 12. Vista de superficie

Calcula una ruta y abre **Vista de superficie** en la barra superior. La cámara se coloca aproximadamente a 1.70 m sobre el terreno reconstruido, sin exageración vertical del mapa aéreo. La fotografía se aplica únicamente si coincide con ese DEM.

Arrastra para mirar alrededor; usa la rueda para cambiar el campo de visión. Las flechas izquierda/derecha giran y arriba/abajo avanzan o retroceden. Los botones **↓ −5 m** y **↑ +5 m** permiten pasos manuales; **Mirar hacia la ruta** centra la mirada. **Volver al mapa** o **Escape** salen de la vista. La reproducción iniciada desde Recorrido también puede mover la posición.

El astronauta de la cámara y los personajes superpuestos a ella se omiten para evitar que el casco tape la imagen. El terreno no incorpora panoramas fotográficos de superficie ni una detección completa de rocas/arena. Si no hay WebGL, elevación válida o ruta, el estado explica por qué no se puede utilizar.

## 13. Pestaña Entorno

| Escenario | Comportamiento |
| --- | --- |
| Despejado | Factor de marcha 1.0. |
| Polvo | Factor 0.8. |
| Tormenta | Factor 0.6 y una zona hipotética de exclusión en el recorte. |

Son escenarios educativos, no meteorología actual de Marte. Las exclusiones manuales de **Zonas a evitar** se combinan con ellos.

**Simular desvío de más de 30 m** busca una posición lateral fuera del tramo. **Marcar posición simulada en el mapa** permite elegir una manualmente. **Reintentar desde posición actual** vuelve a intentar el cálculo cuando ya existe contexto de recálculo.

El sistema recalcula las paradas pendientes y conserva el hogar **H**. Si no hay paso, la simulación se pausa y muestra un estado de bloqueo: cambia la pendiente, el escenario, las exclusiones o la posición y reintenta. El presupuesto del nuevo tramo no descuenta automáticamente tiempo ya consumido.

El recálculo por GPS se activa desde **Reloj / celular**, no mediante estos controles de posición simulada.

## 14. Pestaña Rutas y presupuesto

Compara las tarjetas **RUTA SAFE** y **RUTA FAST**. FAST usa el costo temporal; SAFE añade penalizaciones de pendiente y rugosidad, pero ninguna garantiza un tránsito real seguro.

Las tarjetas muestran minutos totales frente al presupuesto, distancia del recorrido, referencia plana A–B, rugosidad, dosis modelada, ascenso/descenso acumulados, paradas, tramos de ida y regreso y desglose EVA. El detalle puede requerir **Ampliar sección** para ver las dos tarjetas completas.

| Estado | Lectura |
| --- | --- |
| Cabe | Cumple los límites del escenario actual. |
| Justo | Margen reducido bajo los criterios del modelo. |
| No cabe | Supera al menos un límite; no significa necesariamente que no exista un camino geométrico. |

La dosis es una estimación del modelo, no una medición de radiación en vivo. El presupuesto no es solamente caminar: incluye ciencia, entrada/salida, otras tareas y reserva. Los valores base incluyen marcha nominal de 2 km/h, ciencia de 20 min por parada, entrada/salida de 30 min en conjunto y reserva fija de 30 min; los controles de la página y el factor de movilidad pueden modificar supuestos aplicables.

La contingencia evalúa el regreso al hogar a partir del modelo. No equivale a rescate, oxígeno medido o capacidad certificada del traje. Fórmulas y estados exactos: [Presupuesto EVA](eva_budget_lunar_analog.md). Unidades y respuesta JSON: [API](API.md).

## 15. Pestañas Notas y Fuentes

En **Notas**, elige una parada, escribe hasta 600 caracteres y pulsa **Guardar observación**. Se permiten hasta 100 por región. La lista permite eliminarlas. Se guardan localmente y se incluyen al exportar una misión calculada. Se identifican como observaciones del usuario, no como nuevas mediciones NASA.

En **Fuentes** están el catálogo de fuentes NASA/USGS, sus enlaces, la **Ortofoto de referencia · Jezero**, el alcance del modelo y los créditos. Esa imagen de referencia no autoriza superponerla a un terreno incompatible. Las fuentes no convierten las capas simuladas en observaciones actuales. Consulta [Datos y fuentes](DATOS_Y_FUENTES.md) para productos, ventanas y hashes.

## 16. Reloj / celular: entrenamiento real vinculado a Marte

1. Calcula una ruta corta en Jezero o Glen Torridon.
2. Abre **Reloj / celular**. Muestra indicación grande, distancia, estado y modo.
3. Pulsa **Activar GPS del celular**, acepta el permiso y espera precisión de ±25 m o mejor.
4. Elige una rotación de 0° a 359°: 0° conserva el norte; 90° orienta el norte del mapa al este terrestre.
5. Pulsa **Vincular aquí al punto actual de la ruta**. Cada metro terrestre se aplica como un metro en el mapa, tomando ese origen explícito.
6. Mantén la página visible. **Detener GPS** termina el seguimiento; **Recalcular desde mi posición GPS** usa la posición vinculada cuando es válida.

La guía muestra rumbo terrestre en grados y cardinales cuando corresponde. Se suspende el avance con señal de más de 15 s, precisión peor que ±25 m, posición fuera del recorte o más de 30 m separada del tramo activo. El marco local se limita a 5 km. MOLA no habilita este entrenamiento a pie por su baja resolución.

Las coordenadas terrestres quedan en la memoria del celular; no se publican como latitud/longitud al ESP. La página pide mantener la pantalla despierta si el navegador lo soporta; ocultarla detiene el GPS. Cerrar solamente la guía con **Volver al mapa** no es el botón **Detener GPS**. No se garantiza seguimiento con la pantalla bloqueada.

La guía de bolsillo también permite activar/desactivar voz e iniciar/pausar la simulación cuando el GPS no está activo. Necesitas HTTPS para GPS en el teléfono, como el enlace público de Render; HTTP en otra máquina de tu red normalmente no basta. Procedimiento ampliado: [Celular y ESP32](NAVEGADOR_ESP32.md).

## 17. ESP32 y pestaña Reloj

Hay tres funciones distintas:

| Función | Cómo se usa | Límite |
| --- | --- | --- |
| Guía privada del celular | En la guía de bolsillo crea el vínculo; el ESP consulta instrucciones por HTTPS/Wi-Fi. | Sesión de 30 min; el celular mantiene GPS y voz. |
| Misiones Mars OS por código | En Reloj usa Enviar al reloj / Abrir del reloj. | Código público de 4 caracteres, 24 h, 2–10 puntos y Glen Torridon. |
| ROVER de la base anterior | Aplicación independiente en el firmware Space Apps. | Simulación Jezero con mosaicos LittleFS; sin proveedor geométrico usa ruta demo. |

Para la guía privada, pulsa **Crear vínculo de 30 minutos** en **Vincular ESP32 por Wi-Fi**. Introduce en la placa el código de 16 caracteres hexadecimales, punto y 32 caracteres hexadecimales. El ESP necesita internet, por ejemplo mediante el hotspot del teléfono. **Desvincular** en el celular revoca el enlace; desconectar el receptor solo lo detiene en ese dispositivo. Al reiniciar el servidor debes volver a crear el vínculo.

En **Reloj → Reloj Mars OS · compartir misión**, **Enviar al reloj · crear código** publica la misión de Glen Torridon y devuelve un código. En el reloj se recibe desde Misiones. **Abrir del reloj** recupera el código escrito y recalcula con Python. Los códigos son públicos: comparten puntos, restricciones y escenario, sin notas, GPS terrestre ni credenciales privadas. No admiten las 20 posiciones de la web: el límite del reloj es 10 y no se trunca silenciosamente una ruta mayor.

El firmware Space Apps usa TFT ST7789 320×240 y OLED; Mars OS usa TFT 320×240 y una segunda ST7789 240×240. Son alternativas de firmware. El A* sin conexión de Mars OS no equivale a FAST/SAFE: compartir paradas puede producir una ruta local distinta. La guía privada del celular sí sigue las indicaciones de la web. Ninguna de estas placas aporta GNSS autónomo en la integración actual.

Compilación, cableado y operación: [Mars OS](RELOJ_MARS_OS.md), [base celular/ESP](NAVEGADOR_ESP32.md) y [ROVER](MODULO_ROVER_ESP32.md). La página no instala ni flashea firmware en tu PC o reloj.

## 18. Qué se guarda y qué se pierde

| Información | Dónde queda | Qué conviene saber |
| --- | --- | --- |
| Puntos personales, objetivo/equipo, exclusiones y notas | Almacenamiento de este navegador, por región/rejilla. | No se sincroniza entre celulares/PC ni entre dominios. Se pierde si borras esos datos del navegador. |
| Trazado activo, reproducción y anclaje GPS | Memoria de la página. | No se garantiza recuperación al recargar o cerrar. Exporta antes de hacerlo. |
| JSON exportado | Archivo descargado en tu dispositivo. | Es una copia para conservar/revisar, no una carga automática en el servidor. |
| Guía privada ESP | Último estado por sesión en RAM del servidor. | Caduca a los 30 min y se pierde al reiniciar/desplegar. |
| Misiones por código Mars OS | Buzón en RAM, hasta 128 entradas. | Caducan a las 24 h o al reiniciar; no son almacenamiento privado. |
| Bitácora del firmware | LittleFS y, si sincronizas, copia del servidor. | Es otro canal; Render no garantiza persistencia de esa copia sin almacenamiento adecuado. |

Los datos precargados NASA/PDS están incluidos en el repositorio; no se descargan de nuevo al abrir cada mapa. El navegador sigue necesitando acceso al servidor para cargar datos y calcular rutas. Esta página no se presenta como una aplicación web sin conexión instalable.

## 19. Reiniciar, actualizar y resolver errores

**Borrar puntos** reinicia el trazado, no todos los datos guardados. Cambiar región separa sus datos. Para eliminar un punto personal o una nota utiliza sus botones propios. Recargar pierde el estado en memoria y puede detener vínculos activos.

Si actualizas la web, guarda primero el JSON de la misión. En computadora puedes usar **Ctrl+F5** para renovar recursos; esto no corrige por sí solo una ruta imposible, un permiso GPS denegado o un dato incompatible.

Diagnósticos por síntoma y procedimientos concretos: [Solución de problemas](SOLUCION_DE_PROBLEMAS.md). Para todas las etiquetas, límites y sus identificadores de mantenimiento: [Referencia de controles](CONTROLES_WEB.md). Para montar tu propio servidor: [Servidor local](../INICIAR_SERVIDOR.md) y [Render](../RENDER.md).
