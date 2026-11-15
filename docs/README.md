# Documentación de Ruta Marciana

**Aplicación:** https://rutamarciana.onrender.com. **Repositorio:** [FULLY4425/Rutamarciana](https://github.com/FULLY4425/Rutamarciana). La implementación documentada está en la rama `main`; selecciónala para leer o descargar la versión actual.

Actualizado el **15 de noviembre de 2026**, con la interfaz de pestañas laterales y los dos firmwares ESP32. Los manuales describen funciones implementadas; las observaciones científicas, los escenarios educativos y las funciones pendientes de prueba física se distinguen en cada documento.

## Usar la página

| Documento | Para qué sirve |
| --- | --- |
| [Manual completo de la web](MANUAL_WEB.md) | Recorrido por cada pantalla, las once pestañas, controles, planificación, voz, GPS, exportación y reloj. |
| [Referencia de todos los controles](CONTROLES_WEB.md) | Ubicación, etiqueta, identificador, acción y valores de los 89 controles estáticos; además, pestañas y controles generados. |
| [Solución de problemas](SOLUCION_DE_PROBLEMAS.md) | Diagnóstico de interfaz, rutas, fotografía/química, voz, ubicación, códigos, conexión y datos guardados. |
| [Funciones y cobertura](FEATURE_MATRIX.md) | Resumen de lo disponible y sus límites. |

Empieza por el manual si solo quieres usar la página. Para calcular tu primer recorrido consulta su [flujo inicial](MANUAL_WEB.md#3-primer-recorrido-completo).

## Instalar, operar y conectar hardware

| Documento | Para qué sirve |
| --- | --- |
| [Servidor local desde el ZIP de GitHub](../INICIAR_SERVIDOR.md) | Python, arranque, red local, datos incluidos y comprobación del entorno. |
| [Servidor de Render](../RENDER.md) | URL pública, configuración aplicada, arranque y persistencia. |
| [Celular y ESP32 con OLED](NAVEGADOR_ESP32.md) | GPS terrestre vinculado a Marte, enlace privado, TFT 320×240 y firmware Space Apps. |
| [Reloj Mars OS de dos pantallas](RELOJ_MARS_OS.md) | TFT 320×240 + ST7789 240×240, mapa local, misiones por código, guía y relay opcional. |
| [Módulo 3 ROVER](MODULO_ROVER_ESP32.md) | Simulación Jezero, contrato geométrico, mosaicos LittleFS, controles y límites. |

El enlace privado de guía de 30 minutos y los códigos públicos de misión de 24 horas son canales diferentes. La descarga del ZIP no instala ni flashea una placa automáticamente.

## Entender y mantener el proyecto

| Documento | Para qué sirve |
| --- | --- |
| [Especificaciones](ESPECIFICACIONES.md) | Capacidades, parámetros, plataformas y persistencia. |
| [Arquitectura](ARQUITECTURA.md) | Componentes, flujos, responsabilidades, interfaz agrupada y almacenamiento. |
| [API y formatos](API.md) | Endpoints, JSON, unidades, errores y paquetes del ESP. |
| [Datos y fuentes](DATOS_Y_FUENTES.md) | Productos NASA/PDS/USGS, cobertura, resolución y trazabilidad. |
| [Presupuesto EVA](eva_budget_lunar_analog.md) | Fórmulas y criterios del escenario; distingue los registros históricos. |
| [Auditoría del reto](AUDITORIA_RETO_NASA.md) | Correspondencia de requisitos, evidencia y límites. |
| [Cambios y verificación](CAMBIOS_IMPLEMENTADOS.md) | Estado actual, pruebas realizadas y etapas anteriores. |

## Alcance de las comprobaciones

La última revisión funcional registrada aprobó **198 pruebas Python, con 2 omitidas, y 34 JavaScript**. Chromium comprobó gestos móviles, cálculo real Python, las once pestañas, límite inferior del mapa, formularios ampliados, FAST/SAFE y cierre con Escape. La guía Mars OS documenta su compilación y la del firmware anterior. Esto no sustituye pruebas físicas de GPS, voz, pantallas, encoder, Wi-Fi o batería.

Esta actualización de documentación contrasta los controles con el HTML y los módulos actuales; no cambia la aplicación ni obliga a desplegar de nuevo Render. Para instrucciones de desarrollo y ejecución de pruebas consulta el [README principal](../README.md#desarrollo-y-comprobaciones).
