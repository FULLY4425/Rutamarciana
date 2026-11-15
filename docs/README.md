# Documentación de Ruta Marciana

**Aplicación:** https://rutamarciana.onrender.com. **Repositorio:** [FULLY4425/Rutamarciana](https://github.com/FULLY4425/Rutamarciana). La implementación documentada está en la rama `main`; selecciónala para leer o descargar la versión actual.

Actualizado el **15 de noviembre de 2026**, con la interfaz de pestañas laterales y los dos firmwares ESP32. Los manuales describen funciones implementadas; las observaciones científicas, los escenarios educativos y las funciones pendientes de prueba física se distinguen en cada documento.

## Marco del producto

**Micor Field / Martian OS is a simulation built on real NASA data. Future direction: adapt the NASA-data mission simulation for low-power ESP32-class hardware and LoRa-connected field nodes.**

La segunda oración es **future roadmap** (hoja de ruta futura): no describe una adaptación en curso ni implementada.

| Parte de la frase | Estado | Documento |
| --- | --- | --- |
| Simulación | Verificado: relieve reconstruido del DEM, rutas y EVA educativos, rover simulado | [Especificaciones](ESPECIFICACIONES.md) |
| Datos NASA reales | Verificado: Jezero, Glen Torridon, MOLA, con DOI y hashes | [Datos y fuentes](DATOS_Y_FUENTES.md) |
| Firmware ESP32 existente (base) | Compilado para ESP32-WROOM-32 con enlace Wi-Fi; no integra esta simulación web, sin placa flasheada y sin consumo medido | [Mars OS](RELOJ_MARS_OS.md), [Celular y ESP32](NAVEGADOR_ESP32.md) |
| LoRa / nodos | No implementado; **future roadmap** | [Requisitos y trazabilidad](REQUISITOS_TRAZABILIDAD.md#4-hoja-de-ruta-futura-hardware-de-bajo-consumo-edge) |

## Hub, reloj Tierra/Marte y módulos

La portada/hub se está implementando en un clon paralelo; este clon documental permanece en el commit base `13ee095`. Los documentos siguientes fijan el contrato y verifican lo que existe en el código base, sin afirmar una integración que no se haya leído.

| Documento | Para qué sirve |
| --- | --- |
| [Hub Ruta Marciana](HUB_RUTA_MARCIANA.md) | Portada, reloj, selector de módulos (Mapa+EVA, Bitácora por tags, Rover), estados y criterios de aceptación. |
| [Tiempo marciano](TIEMPO_MARCIANO.md) | Definiciones, ecuaciones Mars24, ejemplo validado, supuestos y límites del reloj. |
| [Requisitos y trazabilidad](REQUISITOS_TRAZABILIDAD.md) | Reto NASA Space Apps frente a normas formales NASA; fuentes primarias con URL y fecha. |
| [Operación, límites y licencias](OPERACION_LIMITES_LICENCIAS.md) | Supuestos EVA, bitácora/rover, riesgos, modo offline, permisos, datos personales y créditos. |
| [Validación documental](VALIDACION_DOCUMENTACION.md) | Qué se comprobó, con qué comandos, y qué queda pendiente. |

Los módulos 2 y 3 no están verificados en el hub. La bitácora web existente es local (`localStorage`) y el endpoint legado no autentica ni lista; el rover es una simulación en ESP32, no telemetría.


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
