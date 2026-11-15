# Validación de esta documentación

[Índice](README.md) · [Hub](HUB_RUTA_MARCIANA.md) · [Trazabilidad](REQUISITOS_TRAZABILIDAD.md).

Fecha: **15 de noviembre de 2026, 11:40 CST**. Alcance: documentación del hub y sus fuentes; no se modificó código, firmware, datos ni despliegue.

## 1. Base verificada

| Comprobación | Resultado |
| --- | --- |
| Rama y commit del clon documental | `docs/nasa-space-apps-hub`, HEAD `13ee095` (idéntico a `main` al iniciar) |
| Estado inicial | Árbol limpio, sin cambios locales |
| Clon paralelo del hub | `/home/fox/proyectos/Rutamarciana-hub`, limpio en `13ee095` al iniciar; **sin commits de hub legibles** en ese momento |
| Servidor | `server.py` sirve `/` y la API con Flask/Waitress; rutas verificadas por lectura de `relay/app.py`, `relay/navigator.py`, `relay/psion.py`, `relay/planet.py`, `relay/field.py` |
| Controles del HTML | 89 etiquetas `input/button/select/textarea` en `web/index.html` (coincide con la referencia de 89 controles) |
| Identificadores | 161 atributos `id` únicos en `web/index.html` |
| Pestañas | 11: Plan, Ciencia, Puntos, Datos, Reloj + Guía, Recorrido, Entorno, Rutas, Notas, Fuentes (`web/workspace.js`) |
| Persistencia local | Puntos ≤100, notas ≤100 × 600 caracteres, 7 exclusiones, nombre ≤80; claves por región/rejilla (`web/points.js`, `web/mission.js`) |
| Presupuesto EVA | `LUNAR-ANALOG-2026`: 14400 s, 2 km/h, 1200 s/parada, 900 s egreso/ingreso, reserva 1800 s, retorno 3600 s/2000 m (`assets/jezero/routes.json`) |
| Sin PWA | No hay `serviceWorker` en `web/`; sí `navigator.geolocation` en `web/navigator.js` |
| Licencia | No hay archivo `LICENSE` en `git ls-files` |

## 2. Reloj marciano

Se transcribieron las ecuaciones publicadas por NASA GISS (Mars24) a un cálculo de contraste ejecutado en este entorno, sin modificar el repositorio. Resultado:

- Ejemplo publicado 2000-01-06 00:00 UTC: `MST` 23:59:39.3 frente a 23:59:39; `L_s` 277.18759° frente a 277.18758°; `MSD` 44795.999037 frente a ≈44796.0.
- Ejemplo 2026-11-15 17:30 UTC: `MSD` 54344.235239, `MST` 05:39:52.0, `LMST` Jezero (+77.45°) 10:49:40.0, `LMST` Glen (+137.38°) 14:49:23.2.

Esto valida la transcripción, **no** el código del hub. La longitud de Jezero usada (+77.45°) no está declarada en los metadatos locales: queda pendiente.

## 3. Enlaces

Comprobación local con un recorrido de todos los `*.md` y resolución de rutas relativas (ignorando `http/https/mailto` y anclas):

- 174 enlaces locales comprobados; 0 destinos ausentes tras crear este archivo.
- Los enlaces externos se verificaron por consulta directa de las páginas citadas (NASA GISS, Space Apps, NASA media, NASA-STD-3001). No se comprobó disponibilidad permanente ni cambios posteriores.
- Anclas internas: revisadas por lectura; no se ejecutó un validador de anclas. **Pendiente** si se requiere garantía formal.

## 4. Lo que NO se ejecutó

- `pytest` y `node --test`: no se repitieron en esta pasada documental; las cifras 198/34 siguen siendo la última verificación registrada, no una medición nueva.
- Compilación de firmware, flasheo, GPS, TLS, pantallas, encoder, voz y batería: no se probaron.
- Arranque del hub: no se leyó su implementación; no se verificó portada, selector, reloj ni módulos.
- Cálculo del hub contra Mars24: pendiente.
- Registros/retención del servidor y de Render: no auditados.

## 5. Reproducir esta validación

```sh
cd /home/fox/proyectos/Rutamarciana-docs
git status --short --branch
git log --oneline -1
# controles e identificadores
grep -o '<\(input\|button\|select\|textarea\)' web/index.html | wc -l   # 89
grep -o 'id="[^"]*"' web/index.html | sort -u | wc -l                   # 161
# pestañas
sed -n '/side(.\+plan-panel/,/^ \]);/p' web/workspace.js
# presupuesto
python3 -c "import json;d=json.load(open('assets/jezero/routes.json'));print(d['scenario']['values'])"
```

El cálculo de contraste del reloj se ejecutó como script efímero en el directorio de trabajo del agente; no se añadió al repositorio. Para reproducirlo, implementar las ecuaciones de [TIEMPO_MARCIANO.md](TIEMPO_MARCIANO.md#3-ecuaciones-de-referencia-mars24) y comparar con el ejemplo publicado.

## 6. Límites de esta entrega

- Es documentación de contrato y procedencia; no certifica la integración del hub ni sustituye pruebas funcionales.
- Las afirmaciones sobre firmware heredado (bitácora, rover, Mars OS) provienen de la documentación y el código del árbol base; no se reflashearon placas.
- Las fuentes externas pueden cambiar; se cita fecha de consulta.
- No se hace push ni despliegue. El commit es local para permitir revisión/cherry-pick.
