# Hub web Ruta Marciana: hora y módulos

La portada combina la hora UTC del reloj del dispositivo con un cálculo de hora solar media local marciana para Jezero (18.44° N, 77.45° E; longitud este positiva). El reloj marciano no consulta una fuente de telemetría ni un servicio live.

## Fórmula y límites

Se usa la relación Mars24 NASA GISS para Mars Sol Date (MSD), con días julianos derivados de UTC:

```text
JD_UTC = UnixMilliseconds / 86400000 + 2440587.5
MSD ≈ (JD_UTC − 2451549.5) / 1.0274912517 + 44796 − 0.0009626
LMST = mod24(24 × frac(MSD) + longitud_este / 15)
```

El valor de pantalla identifica la cuenta diaria como MSD y muestra LMST. NASA GISS formula MSD usando JD en tiempo terrestre dinámico; este cliente usa UTC como aproximación de TT. No aplica correcciones de segundos intercalares/ΔT, ecuación del tiempo ni efemérides completas, así que el reloj se rotula «aproximada». No es LTST ni hora de misión de un lander. La longitud y localidad están visibles junto al reloj.

Fuente primaria consultada:
- NASA GISS Mars24, *Algorithm and Worked Examples*: https://www.giss.nasa.gov/tools/mars24/help/algorithm.html
- NASA GISS Mars24, *Technical Notes on Mars Solar Time*: https://www.giss.nasa.gov/tools/mars24/help/notes.html

## Disponibilidad real

- **Mapa + EVA:** usa la web existente y el mismo mapa/planificador; el enlace conserva `?region=jezero`. La planificación es simulación, no telemetría.
- **Bitácora del hub:** notas independientes del usuario guardadas en localStorage del mismo navegador, con filtro por tag, hasta 100 entradas (1000 caracteres por nota, máximo 8 tags de 32 caracteres) y borrado. Sin cuenta, sincronización o respaldo de servidor.
- **Notas del planificador:** la aplicación anterior también conserva observaciones por región en localStorage y las incluye en la exportación de misión. No se fusionan con la bitácora nueva.
- **Endpoint `/api/bitacora`:** recepción legado de JSON del firmware para copias en disco, no un API web autenticado/listable; no se usa para las notas del hub.
- **Rover:** el repositorio contiene simulación para ESP32 sobre ortofoto orbital de Jezero. Sin proveedor geométrico ofrece una ruta demo, explícitamente rotulada; no hay visor Rover web ni telemetría física. El hub muestra la ortofoto real incluida y enlaza el mapa web de Jezero, sin presentarlos como visor Rover.

La imagen de planeta de la portada es una ilustración CSS decorativa; no es un mapa/dataset de superficie.
