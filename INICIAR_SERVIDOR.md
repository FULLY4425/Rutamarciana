# Ruta Marciana en tu servidor local

## Descargar y arrancar

1. En GitHub, selecciona la rama que contiene estos cambios y pulsa **Code → Download ZIP**. Cuando esté integrada en `main`, descarga esa rama.
2. Extrae **toda** la carpeta en un lugar con permisos de escritura.
3. Instala Python **3.10 o posterior** (recomendado: 3.11 o 3.12). En Windows marca **Add Python to PATH**. En Linux puede ser necesario instalar `python3-venv`.
4. Abre una terminal dentro de la carpeta extraída y ejecuta:

   ```bash
   python iniciar.py
   ```

   En Linux/macOS, si el comando es `python3`, usa `python3 iniciar.py`. En Windows también puedes usar `py iniciar.py`.

5. Abre **http://127.0.0.1:8080/** en el navegador del servidor. Deja la terminal abierta. Para detenerlo, pulsa **Ctrl+C**.

La primera ejecución crea `.server-venv`, instala las dependencias y usa el recorte real de Jezero incluido en `assets/jezero`. Necesita internet para instalar los paquetes, espacio libre y permisos de escritura en la carpeta extraída. Los siguientes arranques reutilizan el entorno y los datos. No necesitas Git, Node.js, Make ni activar manualmente un entorno virtual. La aplicación funciona con Waitress; no uses `python -m http.server`, porque el cálculo de rutas necesita la API de Python.

## Abrir desde otra computadora o teléfono de tu red

El servidor escucha por defecto en todas las interfaces. Obtén su dirección IP con `ipconfig` en Windows o `hostname -I` en Linux y abre, por ejemplo:

```text
http://192.168.1.50:8080/
```

Ambos equipos deben tener acceso a la misma red. Permite conexiones TCP al puerto 8080 en el firewall del servidor si hace falta. La aplicación no tiene autenticación general para planificación ni para el endpoint legado de bitácora. El enlace de guía ESP sí usa credenciales temporales; eso no protege el resto del servidor. Para publicar el servicio usa una configuración HTTPS apropiada. Para escuchar solo en el propio equipo:

```bash
python iniciar.py --host 127.0.0.1
```

Para otro puerto:

```bash
python iniciar.py --port 8090
```

## Qué terreno incluye el ZIP de GitHub

El ZIP incluye **un recorte real de Jezero**: DEM HiRISE de USGS, rejilla 500 × 500 con celdas de 4 m, y su procedencia con hashes SHA-256. El producto original tiene resolución de 1 m; las alturas se promedian en bloques de 4 × 4. No se incluye el TIFF original de 1,8 GB.

`python iniciar.py` selecciona ese paquete por defecto. No reemplaza datos anteriores de `data/out`. Para seguir usando una carpeta existente, indícala con `--data data/out`.

Para ensayar con terreno sintético explícitamente:

```bash
python iniciar.py --demo
```

La demo se guarda por separado en `data/demo` y se identifica en pantalla. Si eliges una carpeta vacía con `--data`, el lanzador conserva el comportamiento de generar una demo; una carpeta no vacía e incompatible se rechaza.

Para usar un build real ya preparado, copia su carpeta completa al servidor y ejecuta:

```bash
python iniciar.py --data data/jezero
```

Se requieren `routes.json`, `dem.npy`, `slope.npy` y `rough.npy`, generados con el pipeline actual. El lanzador no reemplaza una carpeta con datos parciales, antiguos o incompatibles. La descarga y preparación del GeoTIFF real siguen las instrucciones del README y requieren las dependencias completas de `requirements.txt`.

## Comprobar sin dejar el servidor encendido

```bash
python iniciar.py --check
```

Prepara el entorno y comprueba las rutas y la malla del terreno seleccionado. Los errores se muestran en la terminal. Si el puerto está ocupado, usa `--port` con otro número.

## Navegador

Usa un navegador moderno con WebGL para la vista de superficie. Pulsa **Activar voz** para habilitar las indicaciones habladas; las voces dependen del navegador y del dispositivo. En red local los datos salen del servidor, pero la voz se reproduce en el equipo que abre la página.

Para actualizar, descarga otro ZIP y extráelo en otra carpeta. Si tienes un build real o bitácoras que quieres conservar, cópialos antes de retirar la instalación anterior; las bitácoras se guardan dentro de la carpeta de datos seleccionada.

## Cobertura completa incluida

Además del Jezero por defecto, el ZIP incluye Glen Torridon con DEM/ortofoto y 718 registros científicos, y el modelo global MOLA. Puedes elegirlos desde la interfaz sin volver a descargar NASA. [Inventario y procedencia](docs/DATOS_Y_FUENTES.md).

## GPS en un celular y receptor ESP32

Abrir el mapa por HTTP en la red local no garantiza geolocalización: los navegadores normalmente requieren HTTPS, salvo contextos de desarrollo como localhost en el mismo dispositivo. Para entrenar con GPS en un teléfono utiliza [la versión Render con HTTPS](https://rutamarciana.onrender.com) o configura HTTPS propio. La guía vincula desplazamientos terrestres a Marte; no navega por calles reales.

El receptor ESP32 está configurado para HTTPS de Render y TFT de 320×240. El servidor local no instala ni flashea la placa automáticamente. [Compilación, hardware y vinculación](docs/NAVEGADOR_ESP32.md).
