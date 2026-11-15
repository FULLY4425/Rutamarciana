# API, unidades y formatos

Referencia de la implementación en `relay/`. [Índice](../README.md) · [Especificaciones](ESPECIFICACIONES.md).

## Convenciones y regiones

Mismo origen para página y API; JSON salvo imágenes, scripts y el paquete ESP. Coordenadas de ruta: **`[fila, columna]` enteras de la rejilla**, no grados ni coordenadas terrestres. Distancias/elevaciones en metros, tiempos en segundos, pendiente en grados y velocidades de escenario en km/h. Los números de entrada deben ser finitos.

Selector `region` en query:

| Valor | Terreno |
| --- | --- |
| Ausente o `jezero` | Carpeta local configurada; Jezero por defecto |
| `glen-torridon` | Paquete incluido de Glen Torridon |
| `mola:latitud:longitud` | Ventana MOLA local, por ejemplo `mola:18.65:-133.8` |

Los endpoints de terreno/rutas/manifiesto usan ese selector. El catálogo científico devuelve los registros solo para `glen-torridon`. El selector no cambia una región global compartida del servidor.

## Endpoints públicos de consulta

| Método y ruta | Respuesta/uso |
| --- | --- |
| `GET /health` | `ok` y archivos de la carpeta local por defecto; disponibilidad básica, no certificación de toda la misión |
| `GET /api/mars/atlas` | Modelo reducido del globo, metadatos y accesos regionales |
| `GET /api/routes?region=...` | Manifiesto schema v3 y rutas precalculadas; pueden estar vacías |
| `GET /api/terrain?region=...&detail=75` | Malla de elevación/pendiente, filas/columnas de origen, rejilla completa, escala física por eje, estadísticas, CRS y procedencia |
| `GET /api/terrain-image?region=...&layer=natural` | PNG; capas admitidas `natural`, `elevation`, `slope` |
| `GET /api/science-targets?region=glen-torridon` | `targets`, fuente y nota; 718 registros en esa región y lista vacía fuera |
| `GET /api/chemistry?region=glen-torridon` | Observaciones químicas cuantitativas, etiquetas, fuentes/hashes y cobertura; lista vacía fuera de Glen |
| `GET /api/science-sources` | Catálogo de fuentes y usos declarados |
| `GET /api/manifest?region=...` | Catálogo compacto embebido: sitio, waypoints, rutas precalculadas y parámetros |
| `GET /api/manifest?region=...&full=1` | Manifiesto completo de la carpeta, sin compactar |
| `GET /reference-metadata?region=...` | Metadatos de ortofoto; Glen cuando se pide Glen, referencia Jezero en los demás casos |
| `GET /reference-image?region=...` | PNG de ortofoto con el mismo criterio; el cliente debe comprobar compatibilidad antes de superponer |

`detail` debe ser entero de 25 a 150; cada eje devuelve como máximo ese número de muestras e incluye bordes. Los valores NoData son `null`. `cell_m` es escala norte-sur y `cell_x_m`, este-oeste. `rows` y `cols` son índices de la rejilla completa, no posiciones consecutivas del muestreo.

`/`, `/app.js`, `/style.css`, `/terrain.js`, `/navigation.js`, `/guidance.js`, `/streetview.js`, `/science.js`, `/planet.js`, `/preview.png` y `/mock_routes.json` sirven recursos de la aplicación. `/field/<nombre>.js` solo admite `points`, `operations`, `actors`, `texture`, `navigator`, `mission` y `chemistry`.

## Calcular una ruta

`POST /api/route?region=glen-torridon`, `Content-Type: application/json`:

```json
{
  "schema_version": 3,
  "start": [30, 30],
  "end": [30, 40],
  "science_waypoints": [],
  "max_slope_deg": 40,
  "scenario_assumptions": {
    "suit_budget_s": 14400,
    "science_stop_s": 1200
  },
  "environment": {
    "weather": "clear",
    "blocked_zones": []
  },
  "named_points": [
    {"point": [30, 30], "name": "Salida de ejemplo", "source": "user_map"}
  ]
}
```

Los índices del ejemplo pertenecen a Glen; la transitabilidad depende de la pendiente y restricciones. No reutilices índices de una región en otra.

| Campo | Regla |
| --- | --- |
| `start`, `end` | Requeridos, `[fila,columna]` enteras, distintas y transitables |
| `science_waypoints` | Lista ordenada, 0–18; sin duplicar A, B ni otra parada |
| `schema_version` | Si se envía, entero 3 |
| `return_to` | Hogar opcional, por defecto `start`; permite replanificar desde otra salida |
| `max_slope_deg` | Número entre 5 y 40, por defecto 25 |
| `named_points` | Metadatos opcionales; hasta 20 objetos con nombre de hasta 160 caracteres. No determinan el camino; lo determinan `start/end/science_waypoints` |
| `scenario_assumptions` | Overrides conocidos del escenario, descritos abajo |
| `environment` | Polvo y restricciones hipotéticas; no observaciones en vivo |

`science_stops` como entrada es legado y se rechaza: el conteo se deriva de `science_waypoints`.

Una exclusión tiene forma `{"center":[35,35],"radius_m":30}`. Máximo ocho; centro dentro de la rejilla y radio positivo, finito y no mayor que su diagonal física. `weather` admite `clear`, `dust`, `storm`; el servidor aplica factores de marcha 1, 0.8 y 0.6. **La zona de tormenta automática la crea la interfaz**: enviar `storm` sin `blocked_zones` solo cambia la velocidad.

Overrides: `suit_budget_s`, `science_stop_s`, `egress_s`, `ingress_s`, `other_tasks_s`, `reserve_fixed_s`, `max_return_s`, `max_return_distance_m`, `v_nom_flat_kmh`, `v_return_flat_kmh`. Se rechazan claves desconocidas, booleanos o números no finitos; rangos generales 0–1e9, y velocidades 1e-6–1e6 km/h. Estos rangos de validación no significan que los valores extremos sean físicamente razonables. El polvo escala las velocidades efectivas y queda registrado en escenario/entorno.

### Respuesta

Metadatos: `schema_version`, sitio/región, rejilla, escalas, proyección, procedencia, puntos, hogar, pendiente, entorno, presupuesto y escenario con defaults/overrides/unidades.

Objetos `fast` y `safe`:

| Campo | Significado |
| --- | --- |
| `points` | Camino de ida en la rejilla |
| `outbound.points`, `.legs` | Ida completa y tramos ordenados, con puntos, segundos, metros y métricas |
| `return` | Camino y métricas del regreso independiente |
| `return_to`, `end_is_home` | Hogar y caso en que el destino ya es el hogar |
| `distance_m`, `time_s` | Totales de marcha, ida más regreso |
| `ascent_m`, `descent_m` | Totales de cambio vertical positivo/negativo |
| `slope_max_deg`, `slope_exposure_m`, `rough_mean` | Pendiente máxima, longitud con exposición desde 15° y rugosidad ponderada |
| `science_total_s`, `total_s` | Tiempo científico y operacional |
| `dose_msv` | Indicador heredado de tasa fija; no dosímetro ni evaluación de radiación real |
| `budget` | Desglose operacional, reserva, holgura, estado y contingencia |

`budget.return_contingency` distingue el proxy radial del retorno dirigido sobre terreno. `waypoint_returns` incluye caminos, distancias, tiempos y chequeos desde las paradas. No se suma al regreso planificado ni a la reserva.

Calcular una misión **no escribe** sus rutas en `routes.json` ni actualiza `/api/manifest`; la respuesta queda en el cliente. Exporta el JSON desde la interfaz para conservarla.

## Bitácora del firmware

`POST /api/bitacora` acepta un objeto con `id` entero no negativo o cadena decimal ASCII, `fecha` cadena y `entradas` lista. El firmware genera además sus metadatos y cada entrada. Ejemplo mínimo de forma:

```json
{"id":"123456789012345678901", "fecha":"2026-11-15", "entradas":[]}
```

El servidor escribe atómicamente `<carpeta local por defecto>/bitacora/<id>.json`; un nuevo POST del mismo ID reemplaza la copia. No implementa listado, autenticación por usuario ni validación científica de las entradas. El ESP conserva su archivo LittleFS como fuente local. Render sin disco persistente pierde esas copias al reiniciar/desplegar. El enlace privado del navegador no autentica este endpoint legado.

## Sesiones de guía celular → ESP

Las sesiones son efímeras, máximo 64 por proceso, duración fija 1800 s. Consultarlas no prolonga la caducidad. Reiniciar el servidor las elimina. No contienen latitud/longitud terrestre ni historial.

| Método | Uso y autenticación |
| --- | --- |
| `POST /api/navigator/sessions` | Crea vínculo; devuelve 201 con `id` (16 hex), `read_token`, `write_token` (32 hex cada uno) y `expires_in_s` |
| `PUT /api/navigator/sessions/<id>` | Publica última guía; `Authorization: Bearer <write_token>` |
| `GET /api/navigator/sessions/<id>` | Lee guía JSON; `Authorization: Bearer <read_token>` |
| `GET /api/navigator/sessions/<id>?format=esp` | Misma lectura como paquete ASCII para ESP |
| `DELETE /api/navigator/sessions/<id>` | Revoca; token de escritura |

La interfaz muestra `id.read_token` como código para el receptor. El token de lectura no publica ni revoca; el de escritura no sirve como token de lectura. No se incluyen secretos en query strings. Las respuestas llevan `Cache-Control: no-store` y `Referrer-Policy: no-referrer`.

Publicación de ejemplo, sin coordenadas terrestres:

```json
{
  "mode": "training",
  "status": "live",
  "kind": "right",
  "instruction": "Rumbo 90 E. Gira a la derecha.",
  "distance_m": 25,
  "remaining_m": 150,
  "accuracy_m": 7,
  "fix_age_ms": 200,
  "budget": {
    "fast_out_s": 100,
    "fast_back_s": 110,
    "safe_out_s": 120,
    "safe_back_s": 125,
    "stops": 2,
    "dwell_s": 60
  }
}
```

`mode`: `training` o `simulation`. `status`: `live`, `simulation`, `waiting`, `paused`, `off_route`, `stale`, `outside`; `live` exige modo training y `simulation` exige modo simulation. `kind`: `depart`, `left`, `right`, `uturn`, `science`, `destination`, `home`, `straight`, `wait`.

Indicación de hasta 180 caracteres, saneada a ASCII imprimible. Distancias 0–1e8 m, precisión 0–1e6 m, edad 0–1e9 ms; presupuesto opcional con números 0–1e8 y `stops` entero 0–18. Cuerpo con Content-Length superior a 4096 bytes se rechaza. Se conserva solo la estructura validada. Una nueva publicación incrementa `seq`.

Al leer, el servidor suma el tiempo desde la última publicación a la edad recibida. Más de 15 s sin publicación o un fix de entrenamiento con edad mayor de 15 s produce `stale`, `wait` y distancia a maniobra cero. Precisión peor que 25 m en entrenamiento vivo produce `waiting`. Antes del primer mensaje devuelve espera.

### Paquete ESP `RUTA1`

Texto terminado en salto de línea, **16 líneas exactas**:

| Línea | Contenido |
| --- | --- |
| 1 | `RUTA1` |
| 2 | Secuencia |
| 3 | Modo |
| 4 | Estado |
| 5 | Tipo de maniobra |
| 6 | Distancia a maniobra, metros redondeados |
| 7 | Distancia restante, metros redondeados |
| 8 | Precisión, metros redondeados |
| 9 | Edad total del fix/mensaje, milisegundos |
| 10 | Indicación ASCII, una línea |
| 11–14 | Ida FAST, regreso FAST, ida SAFE, regreso SAFE, segundos |
| 15 | Número de paradas científicas |
| 16 | Tiempo por parada, segundos |

Si falta presupuesto, sus campos valen `-1`. El receptor limita el cuerpo a 767 bytes, acepta Content-Length o chunked mediante HTTPClient y verifica exactamente las 16 líneas. No interpreta comandos de shell. Considera también el tiempo transcurrido local desde recepción: más de 6 s sin respuesta válida o más de 15 s de edad total suspende el avance. El proveedor EVA usa datos recientes y admite como máximo ocho paradas, sin truncar.

## Errores y límites de servicio

| HTTP | Casos relevantes |
| --- | --- |
| 400 | Entrada inválida, región/parámetro incompatible, ruta bloqueada, puntos duplicados o falta de paso |
| 403 | Credencial de sesión ausente/incorrecta o rol equivocado |
| 404 | Recurso no servido, manifest ausente o sesión inexistente/caducada |
| 413 | Publicación de guía con Content-Length mayor de 4096 |
| 429 | Capacidad de 64 sesiones sin caducar alcanzada |
| 503 | Terreno o manifiesto de planificación ausente, antiguo o incompatible |

La planificación y la bitácora legada no tienen autenticación general ni cuotas de producción. Waitress atiende un cálculo a la vez en Render; una ruta costosa puede retrasar el enlace de guía. Si la respuesta llega tarde, el ESP debe pasar a espera. No se garantiza navegación durante una caída, suspensión o reinicio del servicio.

## Contexto científico y movilidad

`POST /api/route` admite `mission` opcional: `science_goal` y `hypothesis` (hasta 600 caracteres cada uno), `equipment` (hasta 300), `equipment_mass_kg` (0–100) y `mobility_factor` (0.5–1). La respuesta incluye `mission` normalizado, pareja de 2 exploradores y procedencia del supuesto. Solo el factor multiplica velocidades, junto con el polvo; los kg son descriptivos. Valor por defecto 1 conserva el cálculo previo. No se altera la fórmula EVA ni se asume fisiología certificada.

Las exclusiones pueden incluir `kind: manual_obstacle` o `kind: hypothetical_dust`, conservado en la respuesta; tipos desconocidos se rechazan. La interfaz reserva 7 áreas manuales y una de tormenta. La API mantiene el máximo total de 8.

La exportación del navegador añade `mission_observations` (notas propias, no enviadas por la API de rutas) y `data_layers` (disponibilidad/procedencia de topografía, óptica y química). Ni esas notas ni la misión interactiva actualizan el manifiesto precalculado de la bitácora del ESP.

## Misiones del reloj Mars OS

`POST /api/psion/missions` recibe `stops` (2–10 nombres científicos o puntos `{name,lat,lon}`), `maxSlopeDegrees` (5–40) y `environment` (mismo formato de restricciones del planificador). Sin `region` se usa Glen Torridon; las demás regiones se rechazan para este intercambio. Devuelve un código público de cuatro caracteres, puntos normalizados, entorno y métricas de ida FAST del servicio existente, `pathCrc` CRC32 de bytes `(col,row)`, `planner` y advertencia de diferencias con el A* local.

`GET /api/psion/missions/<code>` recupera esa misión. Los códigos caducan a las 24 h o al reiniciar, con capacidad de 128 misiones y respuesta 429 al llenarse. Errores de entrada: 400; cuerpo mayor de 12 KB: 413; inexistente/caducado: 404. No guarda GPS terrestre, notas ni tokens privados. `GET /api/health` identifica compatibilidad con este reloj. Consulta [Reloj Mars OS](RELOJ_MARS_OS.md) para el relay local y el adaptador web.

## Hub y reloj: sin endpoints nuevos

El hub no añade rutas de servidor. La portada se sirve desde `/` y el reloj marciano se calcula en el cliente a partir de UTC y de la longitud de la localidad; no hay API de hora marciana, telemetría ni consulta a Mars24. Los módulos reutilizan los recursos y endpoints existentes; la bitácora por tags es local (`localStorage`) y no crea un endpoint autenticado. Cualquier envío al ESP debe seguir usando los canales ya descritos y no presentarse como almacenamiento duradero. [Contrato del hub](HUB_RUTA_MARCIANA.md), [cálculo del reloj](TIEMPO_MARCIANO.md).
