# Indicaciones en celular y ESP32

**Reloj / celular** es una vista de bolsillo que muestra próxima maniobra, distancia y recorrido restante. La voz sale del celular con el motor del navegador. El ESP muestra las instrucciones recibidas; no renderiza el mapa 3D ni requiere ElevenLabs.

## Entrenar caminando

1. Abre [Ruta Marciana](https://rutamarciana.onrender.com) en el celular y calcula una ruta corta en **Jezero** o **Glen Torridon**. MOLA global no permite entrenamiento a pie por su baja resolución.
2. Abre **Reloj / celular**. Activa la voz si la necesitas.
3. Pulsa **Activar GPS del celular**, concede el permiso y espera una precisión de ±25 m o mejor. La simulación por tiempo queda detenida.
4. Elige la rotación del terreno sobre la Tierra: 0° conserva norte/este; 90° hace que el norte del mapa apunte al este terrestre. Pulsa **Vincular aquí al punto actual de la ruta**. Esta acción fija tu posición terrestre al punto actual de la misión; no cambia las coordenadas de Marte.
5. Camina: un metro real equivale a un metro sobre el terreno marciano. La proyección sobre el tramo activo actualiza giros, distancia, voz y posición relativa. La indicación también muestra el rumbo terrestre del tramo (grados y N/NE/E/etc.) según la rotación elegida. El seguimiento distingue la ida del regreso cuando comparten camino. Puedes recalcular desde la posición vinculada manteniendo el hogar H.
6. Mantén visible la página y la ubicación habilitada. Al ocultarla, se detiene el GPS. **Detener GPS** vuelve al modo de simulación, claramente rotulado.

Las coordenadas terrestres se procesan en el navegador; no se envían al servidor. La conversión es un marco local de entrenamiento de hasta 5 km, no navegación por calles, edificios o peligros reales. Usa un área conocida y despejada cuya geometría permita seguir el trazado. No hagas una ruta larga en un espacio pequeño. La ruta se ajusta a las celdas del DEM, no a obstáculos terrestres.

Si la señal tiene más de 15 s, su precisión supera ±25 m, sales del recorte o te separas más de 30 m del tramo activo, se suspenden las instrucciones de avance. Revisa el celular y recupera la señal o recalcula. El navegador solicita mantener la pantalla encendida cuando el dispositivo lo permite; no garantiza GPS en segundo plano.

Para el nuevo firmware de reloj con **dos pantallas ST7789**, consulta [Reloj Mars OS](RELOJ_MARS_OS.md). Usa el mismo vínculo privado de guía; el driver y las instrucciones de compilación de ese sistema difieren de la base con OLED descrita a continuación.

## ESP32 de la bitácora

Se integra el firmware del ZIP suministrado en `firmware/psion-micor-spaceapps/`. `forest.ino`, `bitacora_core.h`, `eva_budget.h` y `mision_map_contract.h` conservan sus bytes originales; el sketch principal añade los enganches del módulo ROVER. `upstream-sha256.json` registra los hashes. El espacio Misión → Mapa muestra el navegador y registra un proveedor del presupuesto; se añaden `navigator_core.h`, `navigator_transport.h`, `navigator_link.h`, `navigator_tft.h` y `navigator_ca.h`. El transporte HTTPS se comparte con Mars OS; la adaptación a la UI y al presupuesto de esta base permanece en `navigator_link.h`. El firmware anterior de `psion_micor_stable` permanece disponible.

El ESP32-WROOM-32 de esta base no incluye receptor GNSS. El celular aporta GPS; el ESP necesita Wi-Fi con acceso a internet (por ejemplo el hotspot del celular). No se añaden GPIO ni se cambia el cableado de las pantallas, encoder o sensores. No es una aplicación instalable en cualquier reloj comercial; es para el ESP32 y pantallas de esta base Psion Micor.

1. Compila y carga el sketch `firmware/psion-micor-spaceapps/psion-micor-spaceapps.ino` con su carpeta completa.
2. Conecta el ESP a internet desde el menú Wi-Fi existente.
3. En el celular, abre **Vincular ESP32 por Wi-Fi** y pulsa **Crear vínculo de 30 minutos**.
4. En ESP: **Misión → GUIA / Navegador → Vincular celular**. Introduce el código `16 caracteres hex . 32 caracteres hex` mostrado en el celular. Puede introducirse mediante el teclado del shell. El código es temporal y no se guarda en el firmware.
5. La TFT y el OLED muestran la guía. El encoder permite vincular, desconectar y volver al presupuesto. Desconectar en ESP desactiva el enlace en el receptor; **Desvincular** en el celular revoca la sesión del servidor.

El celular publica cada ~2 s y el ESP consulta cada ~2 s. Una tarea de red separada evita bloquear el encoder con HTTPS. La recepción acepta tanto Content-Length como transferencia fragmentada (chunked) y corta el paquete al superar 767 bytes. TLS verifica certificados mediante las raíces oficiales de Google Trust Services (`https://pki.goog/roots.pem`), con sincronización NTP; no se usa `setInsecure`. Si el proveedor cambia su cadena a otra autoridad, habrá que actualizar las raíces. En otro alojamiento se puede definir `RUTA_NAV_SERVER` antes del módulo; el receptor está preparado para HTTPS.

La sesión usa credenciales separadas para publicar y leer, sin incluirlas en la URL. El código solo permite leer instrucciones y tiempos, sin coordenadas terrestres. Las sesiones viven en RAM, caducan a los 30 minutos y se pierden al reiniciar/desplegar Render. Crea otro vínculo si caduca. Render gratuito puede tardar en despertar; durante una interrupción no se inventan indicaciones.

El ESP descarta información con edad de más de 15 s o sin respuesta reciente durante 6 s y muestra **EN ESPERA**. El contrato EVA de la bitácora admite hasta 8 paradas: para rutas mayores, el navegador sigue mostrando la guía, pero el presupuesto del ESP conserva su entrada manual y lo avisa; no se truncan las paradas. La web admite 20 puntos totales y mantiene su presupuesto completo.

## TFT de 320 × 240

El navegador usa **320 píxeles de ancho × 240 de alto, en horizontal**. La base entregada utiliza ST7789: inicializa el panel nativo con `tft.init(240, 320)` y lo gira con `tft.setRotation(1)` para obtener 320 × 240 lógicos. La opción de giro 180° del shell conserva esa resolución. No se cambian controlador, pines ni cableado.

`navigator_tft.h` reserva la barra superior (y=0–17) y el dock inferior (y=214–239). Dentro del área útil muestra flechas vectoriales grandes, distancia en fuente de 24 píxeles, maniobra, distancia restante, hasta cuatro líneas de instrucciones y controles del encoder. Convierte distancias largas a kilómetros para que quepan. En espera sustituye la flecha por pausa y oculta la indicación antigua.

El ajuste de líneas tiene un búfer propio de 51 bytes para 50 caracteres por fila; no utiliza el pequeño búfer del wrapper heredado. Los avisos demasiado largos muestran puntos suspensivos. La prueba del renderer comprueba los límites de cada texto/primitiva, todos los giros y mensajes de 180 caracteres, con AddressSanitizer y UndefinedBehaviorSanitizer.

## Compilar

Instala Arduino CLI y el índice oficial ESP32, o utiliza el gestor de tarjetas del IDE Arduino:

```sh
arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32@3.3.12
arduino-cli lib install "Adafruit GFX Library" "Adafruit SSD1306" "Adafruit ST7735 and ST7789 Library" "NimBLE-Arduino@2.5.2"
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=no_ota firmware/psion-micor-spaceapps
```

Selecciona ESP32 Dev Module, partición **No OTA (2MB APP/2MB SPIFFS)**, como la base entregada. Este cambio no flashea ni formatea tu dispositivo desde el servidor. Para conservar archivos personales, respalda LittleFS antes de cargar el firmware y no actives borrado completo de flash.

## Contrato del enlace

- `POST /api/navigator/sessions`: crea `id`, `read_token`, `write_token` y duración.
- `PUT /api/navigator/sessions/<id>`: estado, modo, tipo de giro, distancias, precisión/edad y tiempos; requiere `Authorization: Bearer <write_token>`.
- `GET /api/navigator/sessions/<id>`: última guía y edad; requiere el token de lectura.
- `GET ...?format=esp`: paquete ASCII acotado `RUTA1` con 16 líneas; parser sin asignación dinámica en `navigator_core.h`.
- `DELETE /api/navigator/sessions/<id>`: revoca con token de escritura.

No hay historial de GPS ni almacenamiento persistente de estos enlaces. El despliegue actual usa un solo proceso Waitress; varios procesos necesitarían un almacén compartido antes de escalar.

## Validación de esta integración

- Compilación real: ESP32 core 3.3.12, NimBLE-Arduino 2.5.2 y FQBN de arriba. **1,992,078 bytes de flash (94%) y 94,516 bytes de RAM estática (28%)**. Se conserva la partición original sin OTA.
- Última revisión funcional registrada del proyecto: **198 pruebas Python aprobadas, 2 omitidas y 34 comprobaciones JavaScript aprobadas**. Incluye permisos GPS explícitos, vínculo de origen, desplazamiento/rotación/rumbo, ida y regreso coincidentes, señales antiguas, pausa en segundo plano, credenciales, caducidad, aislamiento, paquete acotado y parser C++ real.
- Integración: ruta Python → publicación autenticada → paquete de 16 líneas → parser real C++ y campos del presupuesto EVA.
- Los hashes comprueban los núcleos y activos conservados; el sketch principal se compara con los enganches recibidos en el módulo ROVER.
- No se ha flasheado ni probado una placa física. Precisión GPS, enlace Wi-Fi/TLS desde la placa, consumo de batería y funcionamiento de las pantallas requieren prueba en tu hardware. Las pruebas visuales con Chromium emulan escritorio/celular; no sustituyen una prueba en dispositivo ni certifican navegación.

## Hardware y pines de la base suministrada

Estos valores describen el sketch existente ESP32 DevKit V1/WROOM-32; no son una asignación nueva para cualquier placa o pantalla. La integración de guía no los cambió.

| Periférico | Señales del sketch |
| --- | --- |
| TFT ST7789 por SPI | SCK GPIO18, MOSI GPIO23, CS GPIO5, DC GPIO4; RST a 3V3 según el cableado de la base |
| Luz TFT | GPIO32; lógica activa alta configurada; alternativa sin control en los ajustes del sketch |
| SPI TFT | 80 MHz configurados; el comentario del sketch indica 40 MHz si hay fallos de señal |
| OLED SSD1306 | I²C SDA GPIO21, SCL GPIO22, dirección 0x3C |
| Encoder | CLK GPIO27, DT GPIO14, botón GPIO13 |
| Pad opcional | Arriba GPIO19, abajo GPIO17, izquierda GPIO16, derecha GPIO15, OK GPIO33 |
| Audio heredado | GPIO25/DAC1; GPIO26/DAC2 para estéreo según configuración. La voz de navegación sigue saliendo del celular |
| Medición de batería heredada | ADC GPIO34 mediante divisor 100k/100k indicado en el sketch |
| CPU | Arranque a 240 MHz; los ajustes de batería del shell pueden modificarlo |

El nombre del sketch debe coincidir con la carpeta `psion-micor-spaceapps`. Se usa `Adafruit_ST7789`, no un controlador genérico elegido únicamente por resolución. Si la pantalla física usa otro controlador, la resolución 320×240 por sí sola no basta para decidir su driver. La compilación comprobó la base ST7789 suministrada, sin prueba eléctrica en otra placa.

Documentación relacionada: [especificaciones](ESPECIFICACIONES.md), [API/paquete](API.md) y [arquitectura](ARQUITECTURA.md).

El [módulo ROVER](MODULO_ROVER_ESP32.md) añade una simulación independiente sobre ortofoto, con sus datos LittleFS. No cambia el canal privado de guía ni hace que la ruta web se transfiera automáticamente al visor.
