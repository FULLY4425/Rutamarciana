# Ruta Marciana · Micor Field

Explorador de **todo Marte** con globo MOLA, mapas 2D/3D, recortes HiRISE, planificación educativa de recorridos, objetivos científicos y guía para celular y ESP32.

**Aplicación:** [rutamarciana.onrender.com](https://rutamarciana.onrender.com). **Rama de la implementación:** `feat/ruta-marciana-servidor-local` en `FULLY4425/Rutamarciana`. Para descargarla, selecciona esa rama y usa **Code → Download ZIP**; cuando se integre en `main`, podrás descargar `main`.

## Página y servidor en Render

**Abre la aplicación aquí: [https://rutamarciana.onrender.com](https://rutamarciana.onrender.com).**

El proyecto ya tiene una página web y un servidor Python alojados en **Render**. Puedes usarlo desde una computadora o celular con un navegador, sin descargar el repositorio ni ejecutar Python en tu equipo. El mismo servidor sirve el mapa, calcula las rutas y conecta la guía del celular con el ESP32 por HTTPS.

| Acceso | Enlace |
| --- | --- |
| Aplicación web | [rutamarciana.onrender.com](https://rutamarciana.onrender.com) |
| Comprobación básica del servidor | [/health](https://rutamarciana.onrender.com/health) |
| Atlas global de Marte, API | [/api/mars/atlas](https://rutamarciana.onrender.com/api/mars/atlas) |

Al estar en el plan gratuito, el servicio puede entrar en reposo por inactividad y el primer acceso puede tardar aproximadamente un minuto. Mantén la página abierta mientras despierta. No se garantiza disponibilidad continua para el entrenamiento: si se pierde la conexión, el ESP pasa a espera.

El GPS y la voz se usan en el celular con sus permisos; el servidor no proporciona una ubicación propia. Las sesiones temporales del ESP se pierden al reiniciar o desplegar y deben volver a vincularse. Las copias de bitácora guardadas en Render tampoco tienen persistencia garantizada; el ESP conserva su archivo local.

La alternativa local sigue disponible con `python iniciar.py`. Configuración, comandos de despliegue y almacenamiento: [guía de Render](RENDER.md).

## Documentación del proyecto

| Documento | Contenido |
| --- | --- |
| [Auditoría del reto proporcionado](docs/AUDITORIA_RETO_NASA.md) | Requisito por requisito, funciones añadidas, evidencia y límites |
| [Especificaciones completas](docs/ESPECIFICACIONES.md) | Todas las funciones, parámetros, modelos, capacidades y límites |
| [Matriz de funciones](docs/FEATURE_MATRIX.md) | Resumen de capacidades y cobertura |
| [Arquitectura](docs/ARQUITECTURA.md) | Componentes, flujos, archivos, persistencia y decisiones de diseño |
| [API y formatos](docs/API.md) | Endpoints, contratos JSON, unidades, errores y protocolo ESP |
| [Datos y fuentes](docs/DATOS_Y_FUENTES.md) | NASA/PDS/USGS, resoluciones, coordenadas, preparación y procedencia |
| [Servidor local](INICIAR_SERVIDOR.md) | Descargar el ZIP de GitHub, instalar y ejecutar Python |
| [Render](RENDER.md) | Configuración del servicio, arranque, memoria y almacenamiento |
| [Módulo 3 · ROVER](docs/MODULO_ROVER_ESP32.md) | Simulación sobre ortofoto de Jezero, tarjetas, mosaicos LittleFS y contrato geométrico |
| [Celular y ESP32](docs/NAVEGADOR_ESP32.md) | GPS de entrenamiento, vinculación, TFT 320×240 y compilación |
| [Presupuesto EVA](docs/eva_budget_lunar_analog.md) | Fórmulas, supuestos y límites del modelo educativo |
| [Cambios implementados](docs/CAMBIOS_IMPLEMENTADOS.md) | Evolución de las funciones y estado de verificación |

## Qué hace

- Globo global de Marte con selección de cualquier región, incluidos polos y meridianos; ventanas regionales MOLA de 400 km.
- Recortes reales de **Jezero** (DEM de 4 m) y **Glen Torridon** (~12.14 m), con fotografía HiRISE compatible, mapas 2D/3D y capas de elevación/pendiente.
- **Química real ChemCam:** 878 observaciones de 320 objetivos, nueve óxidos en wt% y RMSEP; capa combinable con foto y relieve en Glen Torridon.
- **718 registros científicos** NASA PDS ChemCam de Glen Torridon: búsqueda, sol, coordenadas, formación, miembro, vetas y nódulos.
- Puntos personalizados por clic o coordenadas, nombres, eliminación, guardado en el navegador y combinación con objetivos científicos.
- Rutas **FAST/SAFE** de 2–20 puntos, pendiente máxima de 5°–40°, ida, regreso, ascenso/descenso, métricas y presupuesto EVA de escenario.
- Dos astronautas animados, seguimiento de cámara, avance manual y reconstrucción de superficie a nivel del terreno.
- Giros, distancias, paradas y llegada, con voces españolas del dispositivo, selección de voz, ritmo y volumen.
- Recálculo desde una posición simulada o vinculada al GPS, conservando el hogar; polvo/tormentas hipotéticos y pausa cuando no hay paso.
- **Reloj / celular**: GPS terrestre vinculado explícitamente al mapa de Marte para entrenamiento 1:1, con orientación elegida y coordenadas terrestres procesadas en el celular.
- App **ROVER** en el ESP32: simulación sobre ortofoto de Jezero, recorrido manual/animado y tarjetas de llegada; geometría demo rotulada cuando no hay proveedor.
- Receptor **ESP32-WROOM-32**, TFT **320×240 horizontal** y OLED: indicaciones por Wi-Fi, enlace temporal privado y espera ante datos antiguos. Voz en el celular.
- Objetivo e hipótesis de misión, equipo y factor de marcha, áreas de exclusión manual y cuaderno web de observaciones propias.
- Exportación JSON de misión y bitácora embebida con exportación JSON/CSV y sincronización manual.

La superficie 3D es una reconstrucción del DEM, **no panoramas fotográficos Street View**. El GPS mide desplazamientos reales en la Tierra; no existe GPS marciano ni navegación por calles terrestres en este proyecto. Las rutas no certifican seguridad EVA. Consulta los límites en las [especificaciones](docs/ESPECIFICACIONES.md#persistencia-y-límites-del-producto).

## Ejecutar desde el ZIP de GitHub

Extrae toda la carpeta, abre una terminal dentro y ejecuta:

```sh
python iniciar.py
```

Abre `http://127.0.0.1:8080/`. Requiere Python 3.10+; recomendado 3.12. El primer arranque crea `.server-venv` e instala dependencias con internet. Los datos preparados ya están incluidos; no descarga NASA al arrancar. No requiere Git, Node.js ni Make para utilizarlo.

```sh
python iniciar.py --check                 # preparar y verificar sin servir
python iniciar.py --port 8090             # cambiar puerto
python iniciar.py --host 127.0.0.1        # solo equipo local
python iniciar.py --demo                  # terreno sintético explícito
python iniciar.py --data data/mi-terreno   # build compatible existente
```

Para GPS en un celular usa la versión HTTPS de Render o un servidor HTTPS propio. Una dirección HTTP de red local permite abrir el mapa, pero normalmente no habilita geolocalización. La API de planificación requiere Python; `python -m http.server` no la sustituye.

## Primer recorrido

1. Elige una región en el atlas o abre **Glen Torridon** desde el panel científico.
2. Selecciona salida A y destino B. Añade paradas por clic o desde objetivos/puntos guardados. Son como máximo 18 paradas intermedias más A y B.
3. Ajusta presupuesto, tiempo científico y pendiente; pulsa **Calcular rutas**.
4. Compara FAST y SAFE; inicia el recorrido, cambia a superficie o activa la voz.
5. Para caminar realmente, abre **Reloj / celular**, activa GPS y pulsa **Vincular aquí** en una ruta corta HiRISE. Mantén visible la página.
6. Para el ESP, compila el [firmware integrado](firmware/psion-micor-spaceapps/README.md), conéctalo a Wi-Fi y usa el código temporal de vinculación del celular.

En celular, los botones **Salida A**, **Destino B**, **Parada** y **Zona a evitar** están junto al mapa. Un toque coloca el punto; arrastrar explora el terreno y dos dedos acercan o desplazan sin añadir puntos. **Mover mapa** permite desplazar con un dedo también en 3D. El mapa comienza en 2D en pantallas táctiles pequeñas; puedes cambiar a 3D. La barra inferior lleva a **Mapa / trazar**, **Plan**, **Guía** y **Resultados**. Las capas avanzadas se despliegan en **Capas y opciones de vista**.

El nuevo [reloj Mars OS de dos pantallas](docs/RELOJ_MARS_OS.md) está en `firmware/psion-marte-os`: TFT 320×240 y ST7789 secundaria 240×240, mapa local de Glen Torridon, guía privada del celular, misiones por código y relay opcional para sincronización e imágenes 3D. Su A* local difiere de FAST/SAFE; conserva el firmware anterior como alternativa.

## Desarrollo y comprobaciones

```sh
python -m venv .venv
# Linux/macOS: . .venv/bin/activate
# Windows PowerShell: .venv\Scripts\Activate.ps1
pip install -r requirements.txt
python -m pytest -q
node --test tests/*.cjs
```

La comprobación opcional `node tools/check_mobile_browser.cjs` usa Chromium con depuración CDP en `http://127.0.0.1:9227` y el servidor en `http://127.0.0.1:8094` (variables `RUTA_CDP_URL` y `RUTA_TEST_BASE`). Prueba toques reales emulados, puntos, exclusiones, cálculo Python, zoom con dos dedos y tamaños desde 320×568 hasta 1280×800; guarda una captura en `/tmp/ruta-mobile-map.png`.

Node.js se usa para pruebas, no para servir la aplicación. Las pruebas nativas C++ requieren `g++` con soporte de AddressSanitizer/UBSan; tampoco es necesario para utilizar el servidor. `requirements-server.txt` contiene las dependencias de ejecución; las completas añaden pytest y rasterio para pruebas e importación geoespacial. También existen `make demo` y `make serve` para el flujo de desarrollo con `data/out`.

La versión actual del firmware fue compilada para ESP32 core 3.3.12, partición `no_ota`: **1,992,078 B de flash (94%) y 94,516 B de RAM estática (28%)**. Las pruebas automáticas no sustituyen una prueba física de GPS, Wi-Fi, voz, pantallas o batería.

## Créditos y procedencia

NASA/JPL-Caltech/University of Arizona/USGS (HiRISE), NASA/GSFC/MGS MOLA Science Team y NASA PDS ChemCam. Base embebida **Psion Micor / Space Apps**, suministrada para esta integración; se preservan sus módulos de bitácora y presupuesto. Los enlaces, autores, ventanas y hashes están en [datos y fuentes](docs/DATOS_Y_FUENTES.md) y en los manifiestos de `assets/`.
