# Hub Ruta Marciana: portada, reloj y módulos

Estado del documento: **15 de noviembre de 2026, 11:30 CST**. Describe el contrato de producto del hub y el estado **verificado** del código base. La implementación del hub se realiza en un clon paralelo (`/home/fox/proyectos/Rutamarciana-hub`); en el momento de esta revisión ese clon estaba limpio en el commit base `13ee095` y **no había ningún commit de hub que este documento pudiera leer**. Por tanto, todo lo marcado como *pendiente de verificación* no debe presentarse como integración terminada.

[Índice](README.md) · [Tiempo marciano](TIEMPO_MARCIANO.md) · [Trazabilidad](REQUISITOS_TRAZABILIDAD.md) · [Límites y licencias](OPERACION_LIMITES_LICENCIAS.md).

## 1. Qué es el hub

Una portada única que reúne tres módulos de Ruta Marciana y muestra, de forma prominente, la hora de la Tierra y una referencia de tiempo marciano calculada. No sustituye a la aplicación existente: la conserva como módulo 1.

| Elemento | Contrato |
| --- | --- |
| Portada | Hora Tierra (UTC y hora local del dispositivo), hora marciana con localidad y método, y selector de módulos |
| Módulo 1 · Mapa + EVA | La aplicación actual: atlas, terreno, planificación FAST/SAFE, presupuesto, guía y exportación |
| Módulo 2 · Bitácora por tags | Registro local-first de observaciones etiquetadas; nunca telemetría ni sincronización automática con el ESP |
| Módulo 3 · Rover | Acceso a la simulación ROVER del ESP32 y a su documentación; no es un rover real ni un canal de telemetría |

La portada **no** crea una segunda copia del mapa ni del estado de misión. El módulo 1 debe seguir siendo la misma página y los mismos identificadores; el hub solo añade una capa de entrada y navegación. Criterio verificable: con el hub abierto, los controles del mapa conservan sus IDs, sus listeners y la query `?region=jezero|glen-torridon|mola:lat:lon`.

### 1.1 Narrativa del producto en la portada

La portada describe el producto con una frase fija: *Micor Field / Martian OS is a simulation built on real NASA data. Future direction: adapt the NASA-data mission simulation for low-power ESP32-class hardware and LoRa-connected field nodes.* La segunda oración es **future roadmap** (hoja de ruta futura), no una adaptación en curso. Los elementos no son intercambiables y la portada debe mantenerlos separados:

| Elemento | Lo que la portada puede afirmar | Lo que no puede afirmar |
| --- | --- | --- |
| Simulación con datos NASA reales | Los mapas vienen de productos NASA/PDS con procedencia (Jezero, Glen Torridon, MOLA) y el relieve es una reconstrucción del DEM | Que la app opera en Marte, recibe telemetría o predice condiciones reales |
| Firmware ESP32 existente | Que existe un firmware ESP32 compilable (base de software/hardware) y que adaptar **esta** simulación web a hardware de bajo consumo es **future roadmap** | Que hay una adaptación en curso, integración de esta simulación web en el ESP32, autonomía medida, consumo verificado o placa probada en campo |
| Nodos LoRa | Que LoRa es **future roadmap** declarado, no una capacidad | Que hay radio LoRa, malla, alcance o nodos conectados |

Regla de estado por módulo: el hub no puede mostrar un módulo como «en línea» si su verificación es parcial. La tabla completa de estado por capa está en [Requisitos y trazabilidad](REQUISITOS_TRAZABILIDAD.md#4-hoja-de-ruta-futura-hardware-de-bajo-consumo-edge) y el reparto de destinos en [Arquitectura](ARQUITECTURA.md), sección «Destinos de ejecución».

## 2. Portada y reloj

Contenido mínimo y rotulación obligatoria:

1. **Hora Tierra:** UTC explícito y, si se muestra, hora local del dispositivo con su zona IANA. No confundir UTC con la hora local.
2. **Hora Marte:** `MSD` (Mars Sol Date), `MST`/AMT (hora solar media en el meridiano 0) y `LMST` de la localidad elegida. `LTST` es opcional; si se muestra, debe indicar que incluye la ecuación de tiempo.
3. **Localidad:** nombre y longitud este usada (por ejemplo, Glen Torridon ≈ +137.38°). Sin longitud no se puede calcular `LMST`: mostrar solo `MST`/`MSD` o pedir el dato; no inventar una longitud.
4. **Método:** etiqueta visible «cálculo aproximado a partir de UTC», enlace a [Tiempo marciano](TIEMPO_MARCIANO.md) y fecha de la fuente. Si el hub usa una fórmula simplificada distinta de Mars24, debe decirlo y no presentar el resultado como salida de Mars24.
5. **No telemetría:** el reloj no es una lectura de una misión NASA, no es hora oficial de Marte y no se actualiza desde un servicio en vivo. Es una estimación reproducible con reloj del dispositivo.
6. **Degradación:** sin `Date`/zona horaria utilizable, mostrar «hora no disponible»; no congelar un valor antiguo como si fuera actual.

La fórmula, unidades, validación y límites están en [TIEMPO_MARCIANO.md](TIEMPO_MARCIANO.md). El hub en implementación usa el rótulo «cálculo aproximado»; la verificación contra el ejemplo publicado de NASA GISS queda **pendiente de verificación en el código del hub**.

## 3. Módulo 1 · Mapa + EVA (implementado en el código base)

Capacidades verificadas en este clon (commit `13ee095`):

- Servidor Flask/Waitress (`server.py`, `relay/app.py`); `/` sirve `web/index.html`.
- Once categorías laterales definidas en `web/workspace.js`: Plan, Ciencia, Puntos, Datos y Reloj; Guía, Recorrido, Entorno, Rutas, Notas y Fuentes.
- Planificación Python FAST/SAFE, 2–20 puntos, pendiente 5°–40°, presupuesto `LUNAR-ANALOG-2026` y exportación JSON.
- Región Jezero (DEM 4 m), Glen Torridon (~12.14 m, 718 objetivos, 878 observaciones ChemCam) y ventanas MOLA.
- Guía de celular con GPS terrestre anclado explícitamente; sesiones privadas temporales para ESP32.
- Persistencia local: puntos (máx. 100) y notas de misión (máx. 100, 600 caracteres) en `localStorage` por región/rejilla; no hay cuentas ni sincronización.

El hub debe enlazar a esta experiencia sin duplicarla. Si el módulo 1 se abre en una vista propia, esa vista sigue siendo la página existente; no se reimplementa el planificador en JavaScript.

**Pendiente de verificación:** que la portada no rompa `?region=`, el estado de misión, los formularios agrupados ni la accesibilidad (roles de pestaña, Escape, foco). Debe comprobarse en el clon del hub.

## 4. Módulo 2 · Bitácora por tags

### Lo que existe realmente en el código base

| Canal | Estado verificado | Límite |
| --- | --- | --- |
| Notas de misión web | `web/mission.js`: observaciones propias por parada, `localStorage`, máx. 100, texto ≤600 caracteres, marca `user_observation_unverified` | Solo en el navegador; no se envían al servidor |
| Bitácora del firmware | LittleFS del ESP32; hasta 12 caminatas, 3000 bytes por archivo; tipos roca/suelo/veta/nódulo/polvo/imagen/otro; acciones muestra/foto/medición/sin hallazgo; estados normal/contratiempo/peligro/replanificación | Requiere placa; no verificada físicamente en esta revisión |
| `POST /api/bitacora` | Copia atómica `<carpeta>/bitacora/<id>.json`; reemplaza por ID | Sin autenticación, sin listado, sin validación científica; en Render efímero se pierde |

No existe una API de bitácora web autenticada ni un catálogo de etiquetas servido por el backend. Cualquier módulo de bitácora del hub debe ser **local-first** y declararlo.

### Contrato mínimo del módulo

- Crear, listar, filtrar y borrar entradas en el navegador (`localStorage`), con `id` local, `created_at` ISO 8601, texto acotado y etiquetas normalizadas.
- Etiquetas sugeridas, no impuestas: `roca`, `suelo`, `veta`, `nódulo`, `polvo`, `imagen`, `otro`; estado `normal|contratiempo|peligro|replanificación`; acción `muestra|foto|medición|sin_hallazgo`. Son las categorías del firmware, no una taxonomía científica validada.
- Exportación JSON y CSV con versión de esquema, unidad/región y advertencia de origen no verificado.
- Sin fotos binarias, sin sensores, sin sincronización automática, sin cuentas y sin envío a NASA.
- Si se ofrece «enviar al ESP», debe ser una acción explícita y usar el canal que exista; hoy el endpoint legado no autentica ni lista. No presentar el POST como respaldo duradero.

**Pendiente de verificación:** el modelo final de tags, claves de almacenamiento y exportación del hub no se pudieron leer. Hasta verificarlo, este documento fija el contrato, no la implementación.

## 5. Módulo 3 · Rover

Lo existente es una **simulación en el ESP32**: visor de 200×160 sobre ortofoto de Jezero, 64 mosaicos RGB565 en LittleFS, proveedor geométrico opcional (START + hasta 8 paradas + END) y `RUTA DEMO` rotulada cuando no hay proveedor. No hay app web de rover, telemetría, conducción, energía ni peligros robóticos.

El módulo del hub debe:

- enlazar a la documentación y al estado real del firmware ([Módulo ROVER](MODULO_ROVER_ESP32.md));
- rotular cualquier animación o demo como simulación, nunca como telemetría del rover;
- no transferir automáticamente la ruta del navegador al ESP si el canal no existe;
- distinguir «disponible en hardware» de «documentado» y «probado físicamente».

**Pendiente de verificación:** qué experiencia concreta ofrece el hub en este módulo (enlace, visor o estado). Cualquier captura o dato debe proceder de la placa o del código, no de una maqueta.

## 6. Navegación, estados y accesibilidad

- Cada módulo muestra un estado honesto: `disponible`, `requiere hardware`, `solo documentación`, `en implementación`.
- Un módulo no disponible no debe abrir una pantalla vacía: explica qué falta y enlaza a la guía.
- Teclado: el selector de módulos debe ser operable con Tab/Enter/flechas y conservar el foco; el retorno al hub no debe perder la posición.
- En móvil, la portada no debe impedir el uso del mapa ni el zoom táctil existente.
- El reloj no debe anunciarse cada segundo a lectores de pantalla; usar `aria-live="off"` o actualización contenida.

## 7. Modo offline, permisos y datos

- El hub se sirve desde el mismo origen; no hay *service worker* ni caché PWA en el código base. Sin conexión, solo funciona lo ya cargado o un servidor local. No afirmar «offline completo».
- El reloj funciona sin red si el dispositivo tiene hora; no requiere API.
- El módulo 1 usa Geolocation solo tras acción del usuario, en contexto seguro; las coordenadas terrestres permanecen en el navegador y no se envían al canal de guía.
- La bitácora y las notas viven en `localStorage`: borrar datos del navegador las elimina. No hay copia de servidor.
- No se añaden cámara, micrófono ni cuentas. La voz es `speechSynthesis` del dispositivo.

## 8. Criterios de aceptación del hub

1. `/` muestra portada con hora Tierra, hora Marte, localidad y método; sin valores inventados.
2. `?region=jezero` y `?region=glen-torridon` siguen abriendo el mapa correcto.
3. Los 3 módulos tienen estado y acción coherentes; ninguno afirma telemetría o sincronización inexistente.
4. Bitácora: crear, filtrar, borrar y exportar; recargar conserva lo guardado; borrar almacenamiento lo pierde de forma explicada.
5. Rover: demo rotulada; sin proveedor no se inventan posiciones.
6. Pruebas existentes del proyecto siguen pasando; se añaden pruebas del reloj y del selector.
7. Documentación enlazada desde la portada y desde el índice.

## 9. Pendiente de verificación (explícito)

- Existencia y comportamiento del hub en el clon paralelo (no leído al redactar).
- Fórmula exacta implementada para `MSD/LMST` y su contraste con Mars24.
- Modelo de datos, claves y exportación de la bitácora por tags.
- Contenido real del módulo Rover y cualquier captura.
- Pruebas nuevas del hub y regresiones del módulo 1.
- Longitud central del recorte Jezero usada por el reloj (los metadatos locales no la declaran).
