# Tiempo marciano: cálculo, procedencia y límites

[Hub](HUB_RUTA_MARCIANA.md) · [Índice](README.md) · [Fuentes](REQUISITOS_TRAZABILIDAD.md#fuentes-primarias).

Este documento define cómo debe calcularse y rotularse la hora marciana del hub. **No** afirma que el hub ya implemente exactamente este cálculo: la verificación del código del hub está pendiente. La referencia primaria es el algoritmo publicado por NASA GISS para Mars24 Sunclock.

## 1. Definiciones

| Término | Significado |
| --- | --- |
| **Sol** | Día solar medio marciano: 24 h 39 min 35.244 s. El reloj marciano usa convención de 24 «horas marcianas» con 60 minutos y 60 segundos. |
| **MSD** | *Mars Sol Date*: cuenta secuencial de soles desde 1873-12-29 (JD 2405522.0). No es un calendario civil ni un «sol de misión». |
| **MST / AMT** | *Mean Solar Time* en el meridiano 0 de Marte, también llamado *Airy Mean Time*. |
| **LMST** | *Local Mean Solar Time*: MST desplazada por la longitud este del lugar. |
| **LTST** | *Local True Solar Time*: LMST más la ecuación de tiempo (EOT). |
| **L_s** | Longitud solar areocéntrica; mide la estación marciana. |

Una hora «marciana» no es una hora oficial ni una lectura de telemetría: es una función determinista de UTC y de la longitud elegida.

## 2. Entradas y salidas del contrato

| Campo | Tipo/unidad | Regla |
| --- | --- | --- |
| `utc` | ISO 8601 con `Z` | Reloj del dispositivo; debe ser convertible a *Julian Date* |
| `longitude_east_deg` | grados, −180…360 | Longitud **este** planetográfica del punto; por ejemplo Glen Torridon ≈ +137.38° |
| `MSD` | soles (número real) | `(JD_UT − 2405522.0028779) / 1.0274912517` |
| `MST` | h 0–24 | Ecuación C-2 de Mars24 |
| `LMST` | h 0–24 | `mod 24 { MST + λ_este / 15 }` |
| `LTST` | h 0–24 | `mod 24 { LMST + EOT / 15 }` |
| `L_s` | grados 0–360 | Ecuación B-5; contexto estacional |

Si falta la longitud, el hub puede mostrar `MSD` y `MST`, pero **no** debe etiquetar `MST` como hora local del lugar.

## 3. Ecuaciones de referencia (Mars24)

Con `JD_UT` = fecha juliana UT y `TT − UTC` de la tabla de segundos intercalares (69.184 s desde 2017; 64.184 s en 2000):

```text
JD_TT      = JD_UT + (TT − UTC)/86400
Δt_J2000   = JD_TT − 2451545.0

M          = 19.3871° + 0.52402073° · Δt_J2000
α_FMS      = 270.3871° + 0.524038496° · Δt_J2000
PBS        = Σ A_i · cos(0.985626° · Δt_J2000 / τ_i + φ_i)      (7 términos)
ν − M      = (10.691° + 3.0e−7 · Δt_J2000)·sin M + 0.623°·sin 2M
             + 0.050°·sin 3M + 0.005°·sin 4M + 0.0005°·sin 5M + PBS
L_s        = α_FMS + (ν − M)
EOT        = 2.861°·sin 2L_s − 0.071°·sin 4L_s + 0.002°·sin 6L_s − (ν − M)
MST        = mod 24 { 24 · [ (JD_TT − 2451549.5)/1.0274912517 + 44796.0 − 0.0009626 ] }
LMST       = mod 24 { MST + λ_este/15 }
LTST       = mod 24 { LMST + EOT/15 }
MSD        = (JD_UT − 2405522.0028779) / 1.0274912517
```

Constantes de los siete términos PBS (A_i, τ_i, φ_i): (0.0071, 2.2353, 49.409), (0.0057, 2.7543, 168.173), (0.0039, 1.1177, 191.837), (0.0037, 15.7866, 21.736), (0.0021, 2.1354, 15.704), (0.0020, 2.4694, 95.528), (0.0018, 32.8493, 49.095).

Fuente: NASA GISS, *Mars24 Sunclock — Algorithm and Worked Examples* (actualizado 2025-01-07), basado en Allison & McEwen (2000). El documento de NASA GISS usa longitud **oeste** en su ecuación C-3; aquí se convierte a longitud este para no cambiar el signo dos veces.

## 4. Validación reproducible

Implementación independiente de las ecuaciones anteriores, ejecutada el 2026-11-15 en este entorno. Se contrastó con el ejemplo publicado por NASA GISS: **2000-01-06 00:00:00 UTC, longitud 0°, latitud 0°**.

| Parámetro | NASA GISS (publicado) | Cálculo de contraste | Diferencia |
| --- | --- | --- | --- |
| `L_s` | 277.18758° | 277.18759° | < 0.00002° |
| `EOT` | −5.18774° (−00:20:45) | −5.18775° | < 0.00002° |
| `MST` | 23:59:39 | 23:59:39.3 | < 0.4 s |
| `MSD` | ≈ 44796.0 | 44795.999037 | < 0.001 sol |

La coincidencia valida la transcripción de las ecuaciones, no la implementación del hub. Cualquier desviación mayor que unos segundos en `MST` debe revisarse antes de publicar el reloj.

### Ejemplo para el hub

Entrada: **2026-11-15 17:30:00 UTC**. Resultado del mismo cálculo de contraste:

| Salida | Valor |
| --- | --- |
| `MSD` | 54344.235239 |
| `L_s` | 22.3838° |
| `EOT` | −6.4931° (−00:25:58) |
| `MST` (meridiano 0) | 05:39:52.0 |
| `LMST` Jezero, λ = +77.45° | 10:49:40.0 |
| `LTST` Jezero | 10:23:41.7 |
| `LMST` Glen Torridon, λ = +137.38° | 14:49:23.2 |
| `LTST` Glen Torridon | 14:23:24.9 |

Este ejemplo es una **referencia de contraste** calculada con las ecuaciones publicadas; no es una salida de Mars24 ni una observación. La longitud de Jezero (+77.45°) es un valor de referencia: los metadatos locales del recorte no declaran longitud central, por lo que está **pendiente de verificación**.

## 5. Supuestos y límites

- **Reloj del dispositivo:** si la hora del sistema es incorrecta, el resultado también lo será. El hub no sincroniza con un servidor de tiempo.
- **Segundos intercalares:** desde 2017 no se han añadido; una tabla futura puede cambiar `TT − UTC`. Mars24 documenta este límite.
- **Longitud:** la precisión de la hora local no supera la precisión de la longitud usada. Glen Torridon se toma de los objetivos PDS (≈137.38°E); Jezero requiere confirmar la longitud del recorte.
- **Topografía y atmósfera:** no se modelan; afectan salidas y puestas de sol, no la hora media.
- **`MSD` no es un calendario:** no hay meses ni años marcianos. Tampoco es el «sol» de una misión: Perseverance, Curiosity o InSight definen su Sol 0 de forma distinta.
- **No telemetría:** no hay enlace a Mars24, JPL o una misión en vivo. El rótulo «cálculo aproximado» es obligatorio.
- **`LTST` opcional:** si se muestra, debe explicarse que incluye EOT y no coincide con la hora de un reloj de pared.
- **Precisión declarada por NASA GISS:** el algoritmo tiene error máximo de 0.008° en `L_s` en ±100 años de J2000 y hasta ~3 s en tiempo solar verdadero por la conversión TT–UTC; la hora local depende además de la longitud.

## 6. Criterios de aceptación para el hub

1. La portada muestra `MSD`, `MST` y `LMST` con localidad y longitud, o explica por qué falta.
2. El rótulo dice «cálculo aproximado a partir de UTC» y enlaza a este documento.
3. El ejemplo de la sección 4 se reproduce dentro de ±2 s en `MST` (o se documenta la diferencia).
4. Cambiar de región actualiza la longitud y la hora local, sin fingir que cambió la hora del meridiano.
5. Sin red, el reloj sigue funcionando; sin hora del dispositivo, muestra un estado de error.
6. Ninguna etiqueta sugiere «hora oficial de Marte», «telemetría» o «Mars24 en vivo».

## 7. Fuentes

| Fuente | Título / organismo | URL | Consulta |
| --- | --- | --- | --- |
| NASA GISS | *Mars24 Sunclock — Algorithm and Worked Examples* | https://www.giss.nasa.gov/tools/mars24/help/algorithm.html | 2026-11-15 |
| NASA GISS | *Mars24 Sunclock — Technical Notes on Mars Solar Time* (Allison & Schmunk) | https://www.giss.nasa.gov/tools/mars24/help/notes.html | 2026-11-15 |
| NASA GISS | *Mars24 Sunclock* (herramienta) | https://www.giss.nasa.gov/tools/mars24/ | 2026-11-15 |

Referencias citadas por NASA GISS: Allison, M. (1997), *Geophys. Res. Lett.* 24, 1967–1970; Allison, M. & McEwen, M. (2000), *Planet. Space Sci.* 48, 215–235.
