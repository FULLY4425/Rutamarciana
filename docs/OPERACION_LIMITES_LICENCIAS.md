# Operación, límites, riesgos y licencias

[Hub](HUB_RUTA_MARCIANA.md) · [Tiempo marciano](TIEMPO_MARCIANO.md) · [Índice](README.md).

Estado: **15 de noviembre de 2026**. Describe el comportamiento verificable del código base y los límites que el hub debe respetar. Lo no comprobado se marca como pendiente.

## 1. Presupuesto EVA: supuestos y fórmula

Perfil `LUNAR-ANALOG-2026`, declarado en `assets/jezero/routes.json` y en la respuesta de `/api/route`. Es un **escenario educativo por analogía lunar**, no una capacidad certificada de un traje marciano.

| Supuesto | Valor | Unidad |
| --- | --- | --- |
| Presupuesto de traje | 14400 | s (240 min) |
| Velocidad plana ida/regreso | 2.0 | km/h |
| Ciencia por parada | 1200 | s (20 min) |
| Egreso / ingreso | 900 / 900 | s (15 min cada uno) |
| Otras tareas | 0 | s |
| Reserva fija | 1800 | s (30 min), separada |
| Límite de retorno | 3600 s / 2000 m | escenario |

```text
operacional = egreso + ida + ciencia + regreso + ingreso + otras_tareas
holgura     = presupuesto − operacional − reserva
CABE: holgura ≥ 0 · JUSTO: cabe operacional pero no la reserva · NO CABE: operacional > presupuesto
```

Límites:

- No modela fisiología, consumo de O2/agua/energía, temperatura, radiación real ni reservas de emergencia certificadas.
- La reserva no se suma dos veces; el antiguo 15 % no se usa.
- El retorno contingente comprueba el retorno dirigido desde paradas, no todos los puntos ni todas las alternativas.
- `dose_msv` es un indicador heredado a **0.02 mSv/h** fijos; no es dosimetría ni predicción de eventos solares.
- Cambiar escenario o pendiente recalcula el tramo restante **sin descontar** el tiempo ya consumido.
- El modelo FAST/SAFE usa Tobler escalado y penalización de rugosidad; «SAFE» no significa certificado.

Detalle: [Especificaciones](ESPECIFICACIONES.md#presupuesto-eva-educativo) y [presupuesto lunar](eva_budget_lunar_analog.md).

## 2. Bitácora: canales y límites

| Canal | Persistencia | Límite / riesgo |
| --- | --- | --- |
| Notas de misión web | `localStorage` | Máx. 100 por región, 600 caracteres; se pierde al borrar datos del navegador |
| Bitácora del ESP32 | LittleFS | Máx. 12 caminatas, 3000 bytes/archivo; requiere placa y no se probó físicamente aquí |
| `POST /api/bitacora` | Carpeta del servidor | Sin autenticación ni listado; reemplaza por ID; efímero en Render |

El módulo de bitácora por tags del hub debe ser local-first. **No** debe prometer sincronización automática, respaldo en la nube, adjuntar fotos ni validar científicamente las entradas. Las etiquetas son categorías operativas (roca, suelo, veta, nódulo, polvo, imagen, otro), no una clasificación geológica validada.

## 3. Rover: límites

- La app ROVER es una **simulación en el ESP32** sobre ortofoto de Jezero (64 mosaicos, visor 200×160); no hay rover físico.
- Sin proveedor geométrico se muestra `RUTA DEMO` rotulada, con velocidad nominal proyectada; no es telemetría.
- El canal de guía transmite indicaciones/tiempos, no geometría automática del navegador al rover.
- No implementa conducción, energía, obstáculos ni autonomía.
- Cualquier demo web del hub debe repetir estas etiquetas.

## 4. Riesgos operativos

| Riesgo | Impacto | Mitigación documentada |
| --- | --- | --- |
| Reloj marciano mal rotulado o con longitud inventada | Confusión con hora oficial/telemetría | Rótulo «cálculo aproximado», localidad y longitud visibles, enlace a [TIEMPO_MARCIANO.md](TIEMPO_MARCIANO.md) |
| Hora del dispositivo incorrecta | `MSD/LMST` erróneos | Mostrar UTC usado; no sincronizar silenciosamente |
| Pérdida de `localStorage` | Se pierden puntos, notas y tags | Exportar JSON/CSV; avisar de que no hay copia de servidor |
| Endpoint legado `/api/bitacora` sin auth | Cualquiera con la URL puede escribir una copia | No presentarlo como almacenamiento seguro; no usarlo para datos personales |
| Reinicio/despliegue de Render | Se pierden sesiones y copias de bitácora | El ESP conserva LittleFS; reintentar vinculación |
| Servicio en frío (plan gratuito) | Primer acceso lento; ESP en espera | Mantener página visible; no prometer disponibilidad |
| Sin conexión | No hay PWA ni *service worker* | Servidor local o página ya cargada; no afirmar offline completo |
| Firmware no flasheado aquí | GPS, TLS, pantallas y batería sin prueba física | Declararlo como pendiente |
| Rutas sobre DEM | No detectan rocas, arena ni peligros reales | Exclusiones manuales, pendiente máxima, NoData; no certificar seguridad |

## 5. Modo offline

- El código base **no** incluye *service worker*, manifiesto PWA ni caché de aplicación. «Offline» solo es posible con el servidor local (`python iniciar.py`) o con los recursos ya cargados.
- El reloj del hub no requiere red: depende de la hora del dispositivo y de las ecuaciones.
- El mapa, los catálogos y la química se sirven desde `assets/`; no se descargan de NASA al arrancar.
- La guía ESP y las sesiones requieren red; sin ella el receptor pasa a espera.
- No confundir «datos incluidos» con «funcionamiento offline completo»: la planificación requiere el servidor Python.

## 6. Permisos

| Permiso | Uso | Condición |
| --- | --- | --- |
| Geolocalización | GPS terrestre de entrenamiento en el módulo de guía | Acción explícita del usuario; contexto seguro (HTTPS o localhost); se detiene al ocultar la página |
| Síntesis de voz | Indicaciones en el celular | Voces instaladas del dispositivo; no hay servicio externo |
| Almacenamiento del navegador | Puntos, notas, tags | Puede bloquearse; la app avisa y continúa en sesión |

No se usan cámara, micrófono, contactos ni notificaciones push. El ESP32 no aporta GNSS.

## 7. Datos personales

- Las coordenadas terrestres del GPS se procesan en el navegador y **no** se envían al canal de guía; el servidor recibe indicaciones, distancias, precisión y edad.
- Puntos, notas y tags pueden contener texto libre; se guardan localmente. No hay cuentas ni perfiles.
- El `POST /api/bitacora` legado puede recibir notas del firmware; no tiene autenticación ni cifrado adicional más allá de HTTPS. No introducir datos personales sensibles.
- Los registros del servidor y de Render no se auditaron en esta revisión; **pendiente de verificación**.
- No se envían datos a NASA ni a terceros por usar la aplicación.

## 8. Licencias y créditos

- **Contenido NASA:** según las *Guidelines for using NASA Images and Media*, el contenido generalmente no está sujeto a derechos de autor en EE. UU. y puede usarse con fines educativos/informativos citando a NASA como fuente. La insignia, el logotipo y los identificadores NASA **no** son de dominio público y no se usan aquí. Materiales de terceros conservan sus derechos.
- **USGS:** el DEM Jezero se distribuye vía USGS (DOI 10.5066/P9REJ9JN); consultar los términos del producto. No se asigna una licencia nueva.
- **HiRISE / University of Arizona:** crédito NASA/JPL-Caltech/University of Arizona; los productos PDS pueden tener condiciones propias.
- **NASA PDS ChemCam:** crédito al equipo MSL/ChemCam y a los autores del suplemento; citar el producto.
- **MOLA:** crédito NASA/GSFC/MGS MOLA Science Team.
- **Firmware base Psion Micor / Space Apps:** procedencia suministrada; se conservan módulos y avisos. No se relicencia automáticamente.
- **Código de este repositorio:** no hay archivo `LICENSE` en el árbol verificado (`git ls-files` no devuelve licencia). La licencia del código propio **no está declarada**; pendiente de decisión del autor. No afirmar MIT/GPL ni uso comercial libre.
- **Fuentes y enlaces:** enlazar un portal no implica sincronización ni endoso.

Créditos completos y hashes: [DATOS_Y_FUENTES.md](DATOS_Y_FUENTES.md).

## 9. Pendiente de verificación

- Fórmula y rótulo del reloj en el hub.
- Modelo de tags, exportación y claves de la bitácora del hub.
- Estado real del módulo Rover en el hub.
- Registros del servidor y política de retención.
- Prueba física de GPS, TLS, pantallas, encoder y batería.
- Licencia del código del repositorio.
