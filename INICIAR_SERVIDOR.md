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

La primera ejecución crea `.server-venv`, instala las dependencias y genera datos de demostración. Necesita internet para instalar los paquetes, espacio libre y permisos de escritura en la carpeta extraída. Los siguientes arranques reutilizan el entorno y los datos. No necesitas Git, Node.js, Make ni activar manualmente un entorno virtual. La aplicación funciona con Waitress; no uses `python -m http.server`, porque el cálculo de rutas necesita la API de Python.

## Abrir desde otra computadora o teléfono de tu red

El servidor escucha por defecto en todas las interfaces. Obtén su dirección IP con `ipconfig` en Windows o `hostname -I` en Linux y abre, por ejemplo:

```text
http://192.168.1.50:8080/
```

Ambos equipos deben tener acceso a la misma red. Permite conexiones TCP al puerto 8080 en el firewall del servidor si hace falta. No expongas directamente el puerto a internet: esta aplicación local no incluye autenticación. Para escuchar solo en el propio equipo:

```bash
python iniciar.py --host 127.0.0.1
```

Para otro puerto:

```bash
python iniciar.py --port 8090
```

## Qué terreno incluye el ZIP de GitHub

Los datos generados (`data/out`) y el DEM real no se guardan en Git. Por eso, si no encuentra un terreno, el lanzador genera la **demo sintética**, identificada en la interfaz. No presenta ese relieve como Marte real. La ortofoto de referencia de Jezero sí está incluida en el repositorio.

Para usar un build real ya preparado, copia su carpeta completa al servidor y ejecuta:

```bash
python iniciar.py --data data/jezero
```

Se requieren `routes.json`, `dem.npy`, `slope.npy` y `rough.npy`, generados con el pipeline actual. El lanzador no reemplaza una carpeta con datos parciales, antiguos o incompatibles. La descarga y preparación del GeoTIFF real siguen las instrucciones del README y requieren las dependencias completas de `requirements.txt`.

## Comprobar sin dejar el servidor encendido

```bash
python iniciar.py --check
```

Prepara el entorno, genera la demo si falta y comprueba rutas y malla. Los errores se muestran en la terminal. Si el puerto está ocupado, usa `--port` con otro número.

## Navegador

Usa un navegador moderno con WebGL para la vista de superficie. Pulsa **Activar voz** para habilitar las indicaciones habladas; las voces dependen del navegador y del dispositivo. En red local los datos salen del servidor, pero la voz se reproduce en el equipo que abre la página.

Para actualizar, descarga otro ZIP y extráelo en otra carpeta. Si tienes un build real o bitácoras que quieres conservar, cópialos antes de retirar la instalación anterior; las bitácoras se guardan dentro de la carpeta de datos seleccionada.
