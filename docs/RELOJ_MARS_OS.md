# Reloj Mars OS para Ruta Marciana

## Base y alcance

Se adapta el firmware `psion-marte-os.rar` suministrado por el usuario como un **segundo sistema independiente**, no como una sustitución automática del firmware previo. Se encuentra en [firmware/psion-marte-os](../firmware/psion-marte-os/README.md). Conserva mapa, misiones, caminata simulada, sonidos, sitios, datos de Marte, Wi-Fi, idioma español/inglés, configuración, teclado con encoder y reposo.

Sus datos se generan exclusivamente con `assets/glen-torridon/dem.npy`, `routes.json` y `targets.json` de Ruta Marciana. El DEM de 72×122 celdas se cuantiza al centímetro; el generador comprueba el error y registra hashes de entrada. Agrupa los 718 registros científicos en 136 sitios. Conserva el espaciado distinto de los ejes: ~12.141 m norte-sur y ~12.100 m este-oeste. No carga datos de otro repositorio.

La web y el servidor están en **https://rutamarciana.onrender.com**. El firmware usa esa dirección por defecto para misiones y guía privada. El relay opcional abre ese mismo proyecto en Glen Torridon.

## Dos modos que debes distinguir

| Modo | Cálculo y posición | Conexión |
| --- | --- | --- |
| MAPA → Caminar | A* local, hasta 10 puntos, pendiente 5–40°, restricciones y velocidad del escenario. Personaje simulado, con aceleración seleccionable. | Mapa disponible sin internet. |
| MAPA → Guía GPS del celular | Indicaciones calculadas por la web con FAST/SAFE; GPS terrestre del celular vinculado 1:1 a Marte para entrenamiento. | Celular con página abierta y GPS; reloj con internet por Wi-Fi. |

El A* local minimiza un costo de distancia y pendiente y estima tiempo a velocidad fija, más 10 minutos por tramo. Python usa otro costo, rugosidad, pendientes dirigidas, ciencia y presupuesto EVA de ida/regreso. **Compartir paradas no transfiere la geometría FAST/SAFE al A* local ni garantiza una ruta idéntica.** La opción de comprobación compara celdas y métricas y puede mostrar diferencias esperadas. Para seguir las indicaciones de la ruta web, usa **Guía GPS del celular**.

El reloj no incluye GNSS. La guía no es navegación por calles terrestres. La voz se reproduce en el celular; la placa usa los sonidos existentes para avisar giros próximos. El mapa sin conexión solo cubre Glen Torridon: no contiene DEM de todo Marte ni de Jezero. Para los demás recortes utiliza la guía del celular y la web.

## Pantallas y GPIO

Se mantienen los pines recibidos; la segunda pantalla cambia respecto de la versión anterior con OLED I²C.

| Periférico | Señales |
| --- | --- |
| ST7789 principal 320×240 | SCK 18, MOSI 23, CS 5, DC 4; reset según la base recibida |
| GMT130-V1.0 240×240 ST7789 sin CS | SCK 22, MOSI 21, DC 2; RES a EN o 3V3 según la base; resistencia de 10 kΩ de SCK a 3V3 recomendada por la entrega |
| Luz de ambas pantallas | GPIO32 |
| Encoder | CLK 27, DT 14, botón 13 |
| Pad | Arriba 19, abajo 17, izquierda 16, derecha 15, OK 33 |
| Audio DAC | Izquierdo 25, derecho 26 |
| Batería | ADC34 y divisor de la base |

La segunda pantalla usa el driver `oled240.h` recibido, a pesar de que el nombre conserva «oled». Sus bytes y los de la fuente `font5x7.h` se comprueban contra la entrega original. Resolución, orientación, colores y señales eléctricas requieren comprobación en tu placa.

## Compilar y cargar

En GitHub selecciona la rama `feat/ruta-marciana-servidor-local`, pulsa **Code → Download ZIP** y extrae el repositorio completo. Abre `firmware/psion-marte-os/psion-marte-os.ino` en Arduino IDE. No copies solamente esa carpeta: incluye cabeceras compartidas de `firmware/psion-micor-spaceapps`.

Configuración: **ESP32 Dev Module**, ESP32 core **3.3.12**, CPU 240 MHz, flash 4 MB, partición **No OTA (2MB APP/2MB SPIFFS)**. Bibliotecas: Adafruit GFX Library y Adafruit ST7735 and ST7789 Library, con Adafruit BusIO como dependencia. No requiere NimBLE ni Adafruit SSD1306 para este sistema.

```sh
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=no_ota firmware/psion-marte-os
```

Mantén el esquema de particiones existente y respalda LittleFS antes de cambiar firmware o particiones. El arranque adaptado usa `LittleFS.begin(false)`: si no monta el área de archivos, no la formatea ni borra automáticamente; puede funcionar sin guardar misiones hasta solucionar el montaje. No se incluye ni se carga una imagen de filesystem para este OS: su mapa comprimido va dentro del programa. No se ha flasheado hardware desde esta integración.

## Guía GPS privada del celular

1. En el celular abre Ruta Marciana, calcula una ruta corta HiRISE y abre **Reloj / celular**. Para caminar realmente, activa GPS y vincula tu posición como explica [NAVEGADOR_ESP32.md](NAVEGADOR_ESP32.md).
2. En **Vincular ESP32 por Wi-Fi**, crea el vínculo temporal de 30 minutos.
3. Conecta el reloj al hotspot o una red con internet desde **WI-FI**.
4. En el reloj abre **MAPA**, pulsa OK y elige **Guía GPS del celular**. Elige **Vincular celular** e introduce `ID.TOKEN`: 16 caracteres hexadecimales, punto, 32 caracteres hexadecimales, todos en minúsculas.
5. La TFT muestra maniobra, distancia, texto y distancia restante; la pantalla chica resume el giro. El encoder elige vincular, desvincular o volver al mapa; mantener pulsado vuelve al inicio.

La tarea de red consulta cada ~2 s sin bloquear el control de la guía, limita el paquete a 767 bytes y valida TLS con las mismas raíces de la integración anterior. Espera la sincronización de hora antes de verificar TLS. El código privado queda en RAM, no en Preferences ni LittleFS. **Desvincular** en el reloj detiene el receptor; para revocar en el servidor usa también el botón del celular.

La guía deja de autorizar avance ante señal antigua, baja precisión, pausa, desvío o falta de paquetes recientes. No equivale a posicionamiento autónomo del reloj ni a seguimiento garantizado con el celular bloqueado. Las indicaciones recibidas conservan el idioma que publica el celular; el idioma del menú no traduce los mensajes remotos.

## Misiones por código, sin PC adicional

En el reloj selecciona **AJUSTES → Conexión: Web directa** y dirección `https://rutamarciana.onrender.com` para intercambiar códigos sin relay. La web incorpora **Reloj Mars OS · compartir misión** al final de la página:

- **Enviar al reloj** publica puntos, pendiente, exclusiones y escenario. Devuelve un código de cuatro caracteres. En reloj: **MISIONES → Recibir de la web**.
- Desde el reloj, **Comprobar con el servidor** publica sus puntos y recibe un código. En la web: **Abrir del reloj** y recalcular con Python.
- El intercambio admite **2–10 puntos de Glen Torridon**. La web mantiene su límite general de 20 puntos; no se truncan los restantes para enviarlos al reloj.

Los códigos son **públicos**, no contraseñas: no envíes información privada mediante nombres de puntos. Solo se comparten puntos, restricciones y métricas, sin coordenadas GPS terrestres ni notas científicas. Caducan a las 24 horas o cuando se reinicia/despliega el servidor. El buzón guarda hasta 128 misiones en memoria y responde 429 al llenarse. Son distintos del vínculo privado de navegación de 30 minutos.

La comprobación devuelve métricas de **ida FAST** calculadas por el mismo servicio Python de la web y CRC32 de las celdas `(col,row)`. La duración devuelta es marcha de ida, no el tiempo local ni todo el presupuesto EVA. No se debe interpretar «DIFERENTE» como fallo de transmisión.

## Mars relay opcional en la PC

Sirve para sincronizar el plan y los escenarios y para enviar imágenes de los visores web al reloj. La guía GPS por HTTPS no lo necesita.

```sh
pip install playwright pillow
python -m playwright install chromium
python firmware/psion-marte-os/extras/mars_relay/mars_relay.py
```

En Windows también puedes abrir `extras/mars_relay/start_mars_relay.bat`, con Python y esas dependencias instaladas. El panel permite poner la dirección de Render o tu servidor local. El relay intenta Edge, Chrome o Chromium; `RUTA_CHROMIUM` permite especificar un ejecutable para pruebas. En la placa elige **AJUSTES → Conexión: Relay** y su dirección `IP_DE_PC:8790`, y activa **En vivo con la web** si quieres sincronizar paradas. Se conserva el funcionamiento de ventanas separadas para plan y captura.

La PC y el reloj deben compartir red accesible; el puerto 8790 es HTTP local sin autenticación y no debe exponerse a internet. El relay es un programa opcional para tu PC, no se ejecuta en el servicio Render. La sincronización de este OS se restringe a Glen Torridon; rechaza planes de más de 10 puntos. Cambios de exclusiones también cambian la versión del plan, aunque no cambie el nombre del escenario.

El adaptador `window.ojoDeMarte` en `web/psion.js` expone `ready`, `mars`, `terrain`, `plan`, `setPlan`, `weather`, `setWeather` y `view3d`. Reutiliza los estados y cálculos actuales. Las vistas terrain/follow/surface/globe producen PNG del renderizador existente; el relay las convierte a RGB565 little-endian, por ejemplo 320×190 píxeles. Son imágenes calculadas en la PC, no un motor 3D nuevo en el reloj ni fotos panorámicas desde el suelo. La vista surface requiere WebGL disponible.

## Datos, pruebas y límites

```sh
python firmware/psion-marte-os/extras/make_marte_data.py
python -m pytest tests/test_psion.py -q
# Con el relay iniciado en puerto 8790:
python tools/check_watch_relay.py
RUTA_TEST_BASE=http://127.0.0.1:8095 RUTA_TEST_WATCH=1 node tools/check_mobile_browser.cjs
```

El último comando usa el servidor y Chromium CDP ya iniciados, como se describe en el README del proyecto. Prueba intercambio de misión, importación y capturas de los cuatro modos, además de los gestos móviles existentes.

Las pruebas nativas ejecutan el núcleo recibido con AddressSanitizer/UBSan, comparan todas las elevaciones con el DEM del repositorio cuantizado al centímetro, comprueban pasos del recorrido, JSON acotado y archivos preservados. Las pruebas HTTP comprueban que el cálculo se haga en Glen Torridon aunque el servidor arranque con Jezero, validación de puntos, límites y caducidad. Los ejecutables Windows y el simulador de PC recibidos no se distribuyen como si validaran esta adaptación: la validación corresponde al nuevo código y a las compilaciones ESP32. Falta verificar en hardware las dos pantallas, giro del encoder, DAC, Wi-Fi/TLS y batería.

Validación de esta revisión: **198 pruebas Python aprobadas, 2 omitidas, 34 JavaScript aprobadas**. Compilación real del nuevo OS con core 3.3.12: **1,301,595 B de flash (62%) y 71,540 B de RAM estática (21%)**. El firmware anterior también recompila: 1,992,078 B de flash y 94,516 B de RAM estática. Chromium comprobó el flujo táctil móvil, intercambio/importación con nombres y exclusiones y cuatro capturas; el relay real entregó cuatro imágenes RGB565 de 121,600 B (320×190), con contenido visible. La vista de superficie oculta al astronauta de la cámara y personajes superpuestos a ella para que sus cascos no tapen la imagen. No se ha probado una placa física.
