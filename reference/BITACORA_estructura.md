# BITACORA — Análisis de estructura para el firmware (Psion Micor)

**Fuente del análisis:** subagente estructural sobre `reference/psion_micor_stable.ino` (16,387 líneas, ESP8266 NodeMCU, TFT ST7735/7789 + OLED SSD1306, knob KY-040), 2026-11-14.
**Objetivo:** agregar la app BITACORA (registro de caminata EVA: hora / muestra / nota + export JSON/CSV) siguiendo exactamente el patrón de las apps existentes.

## 1) Los 12 puntos de enganche al shell

| Punto | Línea aprox. | Qué agregar |
|---|---|---|
| Enum `APP_*` | 459 | `APP_BITACORA` al final, después de `APP_BOOKS` (nunca en medio: rompe índices de juegos, dock y colores) |
| `APP_NAMES_P` | 476 | `BITACORA` (separado por `\n`, indexado por enum−1) |
| `APP_DESC_P` | 478 | `registro EVA + exportar` (≤18–21 chars, se trunca) |
| `APP_KIND_P` | 1229 | `TOOL` |
| `APP_ACCENT_BASE[22]` | 413 | extender a 23 colores RGB565 |
| `ICON16[22][16]` | 161 | nueva fila de arte 16×16 |
| `MENU_N` | 1120 | 21 → 22 |
| `enterApp()` enter | 16241 | `case APP_BITACORA: bitacoraEnter();` |
| `loop()` tick | 16349 | `case APP_BITACORA: bitacoraTick(now);` |
| `drawOled()` | 15905 | `case APP_BITACORA: oledBitacora();` |
| leave hook | 16208 | `if (app == APP_BITACORA) bitacoraLeave();` |
| Union `WK` | 3645 | (opcional) miembro nuevo + guardia `wkOwner` |

Opcionales: `ptrListLive()` (mouse de PC, 15816), re-escan en `rcFsChanged` (16382), `DOCK_APPS` (961).

## 2) El modelo a copiar: APP_NOTES (bloque 4333–4513)

- Lista de registros en flash con fila `+ nueva`, editor con teclado de knob compartido (`kbTick`), guardar en OK y al salir, tarjeta OLED de 4 líneas (`oledCard`).
- Esqueleto: constantes `MAX/LEN/VIS`, enum de página `{LISTA, EDITOR}`, estado `sel/top/count/ids[]`, buffers en la union `WK` con guardia `wkOwner`.
- Entrada consumida con la tríada `takeClicks() * ENC_DIR / takePress() / takeShort()` + `titleTimeTick()`.
- Repintado incremental: solo las filas/celdas que cambian; `oledDirty = true` en cada cambio de estado.
- `enterApp()` debe aguantar llamadas repetidas (el visor de PC repinta re-llamando enter).
- Clock (3577+) muestra el uso de tiempo: `getHMS()`, `fmtDate()`, `clkEpoch` — timestamps sin RTC.

## 3) Almacenamiento

- LittleFS en flash, global `fsOk`; sin registro central: cada app escanea su propio namespace.
- Notas: `/n<id>.txt` (slots fijos, título = primeros 26 chars). Calc demuestra escritura **atómica**: `tmp` → `remove` → `rename` (5283–5300) para sobrevivir a cortes de luz.
- Prefs de usuario van al final de `struct Prefs` con `PREFS_VER++`. Estado de sesión NO va a prefs: va a `WK` o al archivo de caminata.

## 4) Estructura propuesta para BITACORA

**Identidad:** `APP_BITACORA` al final del enum · nombre `BITACORA` · desc `registro EVA + exportar` · kind `TOOL` · color e ícono nuevos.

**Modelo de captura (decisión Ian 2026-11-14):** la bitácora NO es de escritura a mano.
El knob/botones solo **seleccionan de listas fijas**; escribir es la excepción ("Otros").
Y la app es **relay exportable**: consume catálogos del servidor y envía la sesión al relay.

**Tres páginas, estilo NOTES anidado:**
1. **Lista de caminatas** — cada caminata = `/b<id>.txt` en LittleFS (slots estilo `notesScan`), fila 0 = `+ Nueva caminata`.
2. **Lista de entradas** — filas de la caminata abierta; cada entrada = hora auto (`getHMS`) + parada + tipo + acción + estado (+ nota "Otros" si hay).
3. **Editor de entrada por selección** — cada campo es un **picker** (lista vertical que se recorre con el knob, OK selecciona), no el teclado. El teclado de knob (`kbTick`) SOLO se abre en el campo "Otros".

**Esquema de una entrada (todos los campos por selección, hora automática):**
- `parada` (picker): catálogo de waypoints del manifest (`/api/manifest` → `waypoints[]`); fallback estático en PROGMEM: `START / S1..S5 / END / OTRO`.
- `tipo` (picker): `ROCA / SUELO / VETA / NODULO / POLVO / IMAGEN / OTRO`.
- `accion` (picker): `MUESTRA / FOTO / MEDICION / SIN HALLAZGO`.
- `estado` (picker): `NORMAL / CONTRATIEMPO / PELIGRO / REPLANIFICACION`.
- `otros` (opcional, texto libre ≤ ~100 chars): "escribe lo que quieras, pero enfocado" — siempre tras los pickers, nunca obligatorio.

**Formato del archivo:** encabezado con título y fecha, luego una línea por entrada:
`HH:MM:SS|parada=START|tipo=ROCA|accion=MUESTRA|estado=NORMAL|otros=...`
Delimitador `|` y `=` para parsear sin CSV ambiguo. Del lado del servidor se convierte a JSON.

**Sincronía con el relay (dos direcciones):**
- *Descarga* (al entrar a la app, si hay red): `GET /api/manifest` → catálogo de waypoints para el picker de paradas. Si falla: catálogo estático de PROGMEM. El demo nunca depende de la red.
- *Carga* (botón "Exportar" en la lista de caminatas): `POST /api/bitacora` con la caminata como JSON `{id, fecha, entradas[]}`. Escritura local tmp+rename **antes** de intentar enviar; si el relay no responde queda pendiente y se reintenta a mano. La fuente de verdad siempre es el flash.

**Export a archivo:** además del POST, popup (`pmTick`): Exportar JSON / Exportar CSV / Borrar caminata — escribe tmp+rename en flash y sale por FILES/visor de PC.

**Alambre al shell:** los 12 puntos del análisis, sin bibliotecas nuevas ni casos en el switch de juegos.

## 5) Riesgos (no romper)

- `appAccent()` silencia ids > 21 → color de APP_MENU sin error de compilación; igual con `ICON16` (glifo "Extras").
- Mantener `APP_BITACORA` fuera del rango de juegos `APP_PONG..APP_RACE` (enterApp activa `gameSetupActive`).
- Heap ESP8266 escaso: el miembro de `WK` no debe crecer más que el mayor actual (~3.7 KB); verificar heap en STATS tras flashear.
- Toda E/S de flash detrás de `if (fsOk)`; degradar con gracia si LittleFS no monta.
- OLED: buffers de 24 chars — truncado silencioso; mantener textos ≤ ~23 chars.
- No olvidar `oledDirty = true` o el espejo OLED/PC queda rancio.
- Escrituras anti corte de luz (tmp+rename), nunca mezclar append con rewrite en el mismo archivo.
- Lista debe manejar 0 y lleno como NOTES ("(lleno — borra una)").

## 6) Decisión (resuelta por Ian 2026-11-14)

NO va de captura manual de texto. Va de **selección por categorías** (pickers de knob, escribir solo en "Otros") y de **relay exportable** — la bitácora se sincroniza con el servidor de micor-field (GET manifest para catálogos, POST para exportar la sesión). Ver §4.
