# Despliegue de Ruta Marciana en Render

La interfaz, el mapa y la API de cálculo se sirven desde un único **Web Service Python**.

- Repositorio: `FULLY4425/Rutamarciana`.
- Rama con la implementación actual: `feat/ruta-marciana-servidor-local`.
- Python: 3.12, fijado en `.python-version`.
- Build command: `pip install -r requirements-server.txt`.
- Start command: `python server.py`.
- Puerto: variable `PORT` de Render, escuchando en `0.0.0.0`.
- Datos: `assets/jezero` y modelo global `assets/mars`, incluidos en GitHub; sin descargar el TIFF al arrancar.
- Comprobación HTTP disponible: `/health`.

`server.py` reutiliza la aplicación y la validación del terreno del lanzador local,
pero no crea otro entorno virtual ni instala paquetes al arrancar. Waitress procesa
una solicitud a la vez para limitar la memoria de los cálculos de rutas en el
plan gratuito de 512 MB. Los cálculos largos pueden retrasar otras solicitudes.
Las variables `OPENBLAS_NUM_THREADS=1` y `OMP_NUM_THREADS=1` limitan los hilos
numéricos, y `PYTHONUNBUFFERED=1` permite ver los mensajes de inicio inmediatamente.

El plan gratuito puede suspenderse tras 15 minutos de inactividad; el primer acceso
posterior puede tardar alrededor de un minuto. Las horas gratuitas se comparten
con los demás servicios gratuitos del espacio de trabajo. Los archivos creados
por la aplicación, incluidas las bitácoras recibidas, son temporales: se pierden
al reiniciar, suspender o desplegar. Los datos de Jezero incluidos en el repositorio
se recuperan con cada despliegue y no necesitan un disco persistente.

La voz del navegador se reproduce en el dispositivo del visitante. Este despliegue
no configura ElevenLabs ni requiere claves de esa plataforma.

Para probar el mismo arranque localmente con las dependencias ya instaladas:

```bash
python server.py
```

Abre `http://localhost:10000/`. La ejecución local simplificada con preparación
automática sigue disponible mediante `python iniciar.py`.


El atlas global se sirve desde `/api/mars/atlas`. Las ventanas MOLA de cualquier
coordenada se generan por solicitud, con una caché limitada a ocho regiones y
lectura del modelo global mediante memoria mapeada. Los archivos temporales de
cada ventana se eliminan tras la respuesta; no necesitan un disco persistente.
No cambies `RUTA_DATA_DIR` a `assets/mars`: esa carpeta contiene el modelo global,
no un build local del pipeline. El terreno local por defecto sigue siendo Jezero.
