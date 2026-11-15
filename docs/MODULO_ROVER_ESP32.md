# Módulo 3 · ROVER para ESP32

Integración del archivo aportado `psion-micor-spaceapps_modulo rover_2026-11-15_v2.zip`, revisada el 15 de noviembre de 2026. [Índice del proyecto](../README.md) · [Guía del ESP32](NAVEGADOR_ESP32.md).

## Funciones

La app **ROVER** es una simulación sobre la ortofoto orbital real de **Jezero**, dentro de la TFT **320×240 horizontal**:

- Ventana izquierda de 200×160 píxeles: recorte desplazable de la ortofoto, polilínea, paradas y marcador orientado del rover.
- Panel derecho: distancia, ETA de la siguiente parada, velocidad nominal, escala de tiempo, pausa/fin y origen de la geometría.
- Tarjetas por parada: nombre, posición en píxeles y coordenadas proyectadas del activo, tiempo acumulado, permanencia si la entrega el presupuesto, distancia desde la anterior y créditos NASA/USGS.
- Resumen en OLED y entrada/salida mediante el shell existente.
- Animación limitada a un máximo de 10 redibujos por segundo; restaura áreas del marcador y evita volver a pintar cuando nada cambió.
- Lectura local de mosaicos, sin necesidad de red para la demostración; sin escrituras de bitácora, órdenes de motores o GPIO nuevos.

Es una **vista cenital sobre fotografía orbital**, no una cámara de un rover físico ni un panorama de superficie. No existe telemetría robótica real. Las posiciones/rutas demo son sintéticas aunque la foto sea real.

## Controles

1. Abre **ROVER** en el menú de aplicaciones. Se añadió al final, sin reordenar las anteriores.
2. Gira el encoder para avanzar/retroceder 20 metros proyectados por paso; el avance manual pausa la reproducción y puede ajustar la posición exactamente a una parada próxima.
3. Pulsa para pausar/reanudar; al final reinicia. En una parada, una pulsación abre su tarjeta.
4. En la tarjeta, gira para cambiar de parada y pulsa para alternar escala ×1/×10/×100. La escala inicial es ×100. Estas opciones están en las tarjetas, por lo que requieren una ruta con paradas.
5. Pulsación larga: tarjeta → visor; desde el visor, vuelve al menú siguiendo el comportamiento normal del shell.

La geometría sigue el contrato de la variante FAST/SAFE seleccionada en Misión. La simulación toma esa selección disponible en memoria; si todavía no se abrió Misión desde el arranque, su valor inicial es FAST.

## Origen de ruta y contrato opcional

`pov_contract.h` define `povc::setPovProvider(fn)` con `PovInput`:

- Variante 0 FAST / 1 SAFE.
- 2–10 waypoints: START, hasta 8 paradas y END.
- `u,v` enteros de 0 a 499 sobre la ortofoto Jezero, con v hacia el sur.
- `t_s` acumulado de llegada, nombres acotados a 15 caracteres más terminador y velocidad nominal positiva en m/s proyectados.

**La guía Wi-Fi existente solo transmite indicación y tiempos, no esta geometría. No se añadió una transferencia automática de las rutas de la web al rover.** Sin proveedor geométrico, ROVER muestra explícitamente **RUTA DEMO**. Puede tomar los tiempos del proveedor de presupuesto como escenario, pero los puntos y tramos siguen siendo sintéticos; esos tiempos no convierten la geometría en una ruta real. No se usa la posición GPS del celular como posición de un vehículo.

Un proveedor válido puede registrar geometría de Jezero en el contrato. Si indica datos válidos pero entrega una estructura malformada, se muestra error en vez de sustituirla silenciosamente por demo. Si no hay datos/proveedor, se usa la demostración rotulada.

El contrato representa segmentos rectos entre hasta diez waypoints. No debe usarse para afirmar que reproduce automáticamente todos los vértices de una ruta Python ni que evita sus obstáculos. Una futura transferencia deberá validar la misma ventana/CRS de Jezero y representar correctamente las restricciones; no se deben dibujar coordenadas de Glen Torridon o MOLA sobre estos mosaicos.

El núcleo usa **4 metros proyectados por píxel**, según el transform del activo. Sus longitudes son aproximaciones en la proyección, distintas de la corrección física este-oeste del planificador web. El reloj interpola los tiempos de llegada; la animación avanza por longitud de arco a velocidad nominal. No modela conducción, pendiente, energía, terreno transitable o permanencia física automática de un rover.

## Archivos integrados

| Archivo | Función |
| --- | --- |
| `firmware/psion-micor-spaceapps/pov_contract.h` | Contrato geométrico opcional; conserva el contrato EVA existente |
| `pov_core.h` | Geometría, cámara, demo, simulación y direcciones de mosaicos, sin Arduino |
| `pov_render.h` | Recorte de líneas al área de mapa y límites de coordenadas |
| `rover_app.h` | Visor, lectura de LittleFS, tarjetas y ciclo de vida ROVER |
| `data/pov/` | 64 mosaicos RGB565 little-endian y manifiesto |
| `firmware/host_tests_spaceapps/` | Pruebas de los núcleos entregados: EVA, bitácora y POV |
| `pipeline/pov_tiles.py` | Generación, recomposición, cuantizado y verificación de capacidad LittleFS |
| `rover-upstream-sha256.json` | Hashes de los archivos recibidos en el ZIP para trazabilidad |

El sketch principal solo incorpora los enganches de ROVER recibidos: includes, icono/acento, registros al final de los arrays y entrada/salida/tick/OLED. `spaceapps_apps.h` conserva la guía `navigator_*`, el proveedor EVA y la bitácora actuales, e incluye el nuevo módulo por separado. **No se reemplazó Misión por el placeholder del ZIP.** `forest.ino`, `eva_budget.h`, `bitacora_core.h` y `mision_map_contract.h` mantienen sus bytes.

## Correcciones de la revisión

- Cada fila de una intersección parcial de mosaico se busca con su stride completo; evita leer como si los píxeles recortados fueran filas completas.
- Líneas, círculos, etiquetas y marcador se limitan a la ventana; no pintan sobre panel, barra superior o dock.
- Texto lateral limitado al ancho del panel y mensajes largos con búfer propio, sin pasar 50 caracteres al wrapper heredado de 24 bytes.
- Lecturas fuera del activo se rechazan y archivos ausentes/incompletos se representan en gris con aviso.
- El informe del generador usa bytes de una compilación actual si se proporcionan; no reutiliza la cifra de flash libre de una versión anterior. El búfer real de píxeles del módulo es 64×2 = 128 bytes, aparte de estado, pila y otras apps.
- El firmware puede usar FAST o SAFE según la selección de Misión, en lugar de fijar siempre FAST.

## Mosaicos y LittleFS

Los assets originales `assets/jezero_ortho_4m.png` y `.json` son idénticos a los que ya tenía el repositorio. No se sustituyeron por otra imagen. 500×500 píxeles, 64 mosaicos en una rejilla 8×8 de 64×64; la última fila/columna tiene **52 píxeles**, sin padding.

La fotografía ocupa la **partición de archivos**, no PROGMEM. No necesita otro hardware ni memoria PSRAM por esta integración.

```sh
# Verificar los datos incluidos; no es necesario regenerarlos para compilar.
python -m pipeline.pov_tiles --verify-only

# Regenerar desde los mismos PNG/JSON de origen.
python -m pipeline.pov_tiles
```

`--mklittlefs /ruta/a/mklittlefs` permite comprobar una imagen de LittleFS con la herramienta instalada; `--sketch-bytes N` registra la flash usada por tu compilación actual. Sin herramienta se informa que el fit binario no fue comprobado. Sin cifra actual no se inventa la memoria libre de app0. Los artefactos de comprobación se generan en `data/out`, fuera de los archivos versionados.

### Cargar datos en la placa

Compilar/subir el sketch **no instala automáticamente** los mosaicos. El dispositivo debe contener `/pov/manifest.json` y los 64 archivos `/pov/rXXcYY.bin` de `firmware/psion-micor-spaceapps/data/`.

**Respaldar primero LittleFS:** una imagen completa de archivos puede reemplazar la bitácora, preferencias y documentos existentes. Conserva una copia y combina esos archivos con `data/pov` antes de crear una imagen destinada a una placa ya utilizada. No se ha flasheado ni formateado ningún dispositivo en esta integración.

Para una placa nueva o un contenido ya respaldado y preparado, la partición `no_ota` comprobada tiene offset **0x210000** y tamaño **0x1E0000**. Confirma que tu tabla de particiones sea la misma antes de usar esos valores. Ejemplo de preparación local:

```sh
mklittlefs -c firmware/psion-micor-spaceapps/data -s 0x1E0000 pov_littlefs.img
```

Carga con la herramienta de filesystem del IDE compatible con tu core o esptool, seleccionando el puerto y esa partición. No grabes esa imagen mínima sobre archivos personales sin haberlos respaldado/integrado. Si no se cargan los mosaicos, la app sigue identificando la demo pero avisa de ortofoto ausente.

## Verificación realizada

- Núcleos recibidos: **27/27 EVA + 40/40 bitácora + 109/109 POV**, ejecutados sobre el árbol integrado.
- Pruebas adicionales con ASan/UBSan: lectura real de filas/tiles parciales y bordes, fallback sin archivos, coordenadas inválidas, clipping, texto largo y preservación del resto de módulos.
- Recomposición RGB565 bit-exacta frente a la cuantización del PNG; error máximo de canal R/G/B **4/2/4**, dentro del límite esperado. Hashes del PNG/JSON sin cambios.
- LittleFS: **511,128 bytes** de payload; estimación conservadora con bloques/metadatos **602,112 bytes**. Imagen real construida con mklittlefs y capacidad de **1,966,080 bytes**, sin flashear hardware.
- Firmware completo con guía anterior y ROVER: **1,992,078 bytes de flash (94%) y 94,516 bytes de RAM estática (28%)**, ESP32 core 3.3.12, NimBLE 2.5.2, partición `no_ota`. Delta frente al firmware anterior: +11,560 bytes de flash y +824 bytes de RAM estática. No se eliminaron apps para hacerlo caber.

La RAM estática no incluye toda la memoria de ejecución de TLS, tareas o filesystem. Rendimiento real de SPI/LittleFS, color/rotación de la TFT, controles y OLED necesitan prueba física. El visor no valida peligros o seguridad de conducción.
