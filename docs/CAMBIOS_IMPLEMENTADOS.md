# Cambios implementados y verificación

Registro funcional hasta el **15 de noviembre de 2026**. No es un roadmap ni una afirmación de que se hayan probado físicamente todas las funciones del firmware heredado. [Índice](../README.md).

| Etapa | Resultado actual |
| --- | --- |
| Servidor local | Lanzador `python iniciar.py`, entorno aislado y datos preparados en el ZIP descargado de GitHub |
| Jezero real | Recorte DEM/ortofoto HiRISE, procedencia y comprobaciones de compatibilidad espacial |
| Planificación | FAST/SAFE dirigidos, ciencia ordenada, regreso independiente, presupuesto por analogía lunar y retorno contingente |
| Interfaz y visor | Mapa 2D/3D, capas, detalle visual, perfil, navegación de cámara y reconstrucción de superficie |
| Seguimiento | Dos astronautas, reproducción, avance manual, cámara de seguimiento, instrucciones y voces españolas configurables |
| Marte global | MOLA incluido, atlas, selección de cualquier coordenada y ventanas regionales aisladas |
| Glen Torridon | Importación oficial HiRISE/PDS y 718 registros ChemCam con clasificación, sol y formación |
| Edición de misión | Puntos propios guardados por región, combinación con ciencia, 2–20 puntos totales y pendiente 5°–40° |
| Entorno hipotético | Polvo, restricciones, desvío, recálculo desde posición actual y conservación del hogar |
| Exportación | JSON de rutas, puntos, métricas, proyección, supuestos y fuentes |
| Hosting | Servicio Python en Render con HTTPS, datos incluidos y arranque Waitress |
| Guía de bolsillo | Vista Reloj / celular con GPS terrestre explícitamente vinculado a Marte, entrenamiento 1:1 |
| Enlace embebido | Sesiones privadas temporales, guía ASCII y receptor HTTPS ESP32, conservando la bitácora suministrada |
| TFT 320×240 | Renderer horizontal dedicado, flechas grandes, fuente de distancia de 24 píxeles y texto acotado |
| Documentación integral | README reorganizado, especificaciones, arquitectura, API, datos, hardware y límites contrastados con código |

## Última verificación disponible

- Suite completa de software previa a la adaptación TFT: **166 pruebas Python aprobadas, 2 omitidas y 27 comprobaciones JavaScript aprobadas**.
- Adaptación TFT: nueva prueba de límites/texto con ASan/UBSan y comprobaciones focalizadas de parser/API/preservación de firmware: **15 pruebas Python aprobadas**. No sumar 15 a 166 como si fueran pruebas independientes; hay pruebas repetidas entre ejecuciones.
- Firmware completo, ESP32 core 3.3.12, partición `no_ota`: **1,980,518 bytes de flash (94%)**, **93,692 bytes de RAM estática (28%)**. La memoria dinámica de TLS, tareas y aplicaciones se añade en ejecución.
- Verificación HTTP pública: vista de bolsillo y script de navegación disponibles, credenciales separadas, publicación/lectura y revocación de sesión, paquete de 16 líneas. Se observó transferencia chunked y se adaptó el receptor acotado, con recompilación correcta.
- Integración en host: resultado de ruta Python → mensaje autenticado → paquete real → parser C++ y campos EVA.
- Los archivos del sketch principal, forest y núcleos de bitácora/EVA/contrato preservan sus hashes del ZIP aportado.

Estos resultados son evidencia de desarrollo, no una garantía permanente para cualquier entorno. Comandos reproducibles en el [README](../README.md#desarrollo-y-comprobaciones) y en la [guía ESP](NAVEGADOR_ESP32.md).

## Pendiente de prueba física

Precisión GPS y orientación escogida en un celular real, permisos/voz de cada navegador, conexión TLS desde la placa, TFT/OLED, manejo del encoder, consumo/temperatura/batería y comportamiento de todas las aplicaciones heredadas. No se ha flasheado una placa en estas verificaciones. Tampoco se ha certificado seguridad EVA, rescate o tránsito terrestre.

## Documentos históricos

`reference/` conserva propuestas de diseño anteriores. Las secciones históricas de `eva_budget_lunar_analog.md` registran una etapa previa de backend y no describen todos los cambios posteriores de UI/datos/hosting. Para capacidades actuales usar [especificaciones](ESPECIFICACIONES.md), [API](API.md) y [guía ESP](NAVEGADOR_ESP32.md). La documentación no convierte propuestas de los anexos en instrucciones para eliminar aplicaciones o cambiar hardware.

## Revisión del reto científico

La [auditoría completa](AUDITORIA_RETO_NASA.md) distingue objetivos centrales y propuestas opcionales del texto proporcionado. Se añadieron química PDS cuantitativa, combinación de tres clases de datos en Glen, objetivo/hipótesis, equipo/factor de movilidad, exclusiones manuales y cuaderno web. **177 pruebas Python aprobadas (2 omitidas) y 29 comprobaciones JavaScript aprobadas**, incluidos emparejamiento/hashes, factor de marcha sin penalización inventada por kg, ausencia de interpolación química y persistencia por región. No se cambió el firmware del ESP ni se añadió meteorología/radiación observada o panoramas fotográficos reales.
