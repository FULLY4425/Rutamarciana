# Auditoría de requisitos: exploración de la superficie de Marte

Revisión del **15 de noviembre de 2026** contra el [texto proporcionado por el usuario](../reference/MARSWALK_CHALLENGE_2026.txt). Se conserva para trazabilidad; no se afirma que NASA haya evaluado o aprobado esta implementación. SHA-256 del texto: `71449d3c2e29be6002f06d4b5230e18d127587ec3da5e8c39c8de213629b446b`.

La tabla requisito→componente, la distinción frente a normas formales de NASA (incluido NASA-STD-3001, sin declaración de cumplimiento) y las fuentes primarias con URL y fecha están en [REQUISITOS_TRAZABILIDAD.md](REQUISITOS_TRAZABILIDAD.md). El hub se documenta en [HUB_RUTA_MARCIANA.md](HUB_RUTA_MARCIANA.md); su integración no se verificó en este árbol.

El texto distingue un objetivo central de propuestas introducidas por «might», «may consider» y «may (but are not required to)». La revisión conserva esa distinción. No convierte una sugerencia de radiación, rover, boxwork o HUD en un requisito obligatorio, ni presenta una simulación como validación de seguridad.

## Resultado de la revisión

El caso científico completo es **Glen Torridon / Gale**: óptica y topografía HiRISE de MRO, química y clasificación de MSL/Curiosity, con MGS/MOLA como contexto global. Fuera del recorte no se extrapolan las observaciones. La cobertura global ofrece exploración regional, no el mismo detalle científico en todo Marte.

| Requisito o propuesta del texto | Tipo | Estado y evidencia |
| --- | --- | --- |
| Vista integrada por capas de una ubicación/ruta marciana usando misiones NASA | Central | Implementado: malla/ortofoto HiRISE, química ChemCam y contexto MOLA. Botón «Combinar óptica + relieve + química» en Glen. Ver `web/chemistry.js`, `web/terrain.js`, `assets/` |
| Ayudar a planificar y ejecutar una Marswalk con ciencia | Central | Planificación FAST/SAFE, puntos científicos, objetivo e hipótesis, ciencia por parada, guía, registro de observaciones y bitácora ESP. Es asistencia educativa, no ejecución certificada de una misión |
| Elegir objetivos científicos y lugar | Sugerencia | Implementado: campo de objetivo, hipótesis/procedimiento, selección de región y 718 registros con sol/formación/clasificación. Glen propone comparar vetas/nódulos; no se inventa una misión boxwork de 2025 |
| Partir de un lugar explorado por rover o de otro lugar | Alternativas permitidas | Glen corresponde a Curiosity; Jezero a Perseverance. MOLA permite otras regiones con límites de detalle explícitos |
| Al menos tres clases de datos combinadas | Sugerencia, adoptada | **Óptica, topografía y química cuantitativa** en Glen. 878 observaciones ChemCam geolocalizadas al objetivo, 9 óxidos y RMSEP. Las clases geológicas aportan contexto adicional |
| Contexto útil de ruta y destinos | Central | Pendientes, elevación, fotografía orbital, distancias, ascenso/descenso, secuencia de paradas y química individual/por objetivo. Los círculos químicos usan mediana por objetivo, no una interpolación de todo el terreno |
| Considerar seguridad y movilidad con traje/equipo | Sugerencia | Límites de pendiente, NoData, regreso, contingencia y presupuesto; lista de equipo, kg descriptivos y factor de marcha 0.5–1 elegido por usuario. No es fisiología ni cálculo de consumibles |
| Evitar obstáculos significativos | Sugerencia | Exclusiones circulares manuales por clic, visibles y transmitidas al motor. Demostración de desvíos y falta de paso. No existe detección automática validada de rocas a partir de fotografías |
| Actualizaciones de condiciones | Contexto introductorio; se admite marco histórico o sin fecha | Escenarios de polvo hipotéticos y recálculo. Datos científicos históricos con sol/fechas y procedencia. No se afirma disponer de meteorología marciana en vivo |
| Incluir meteorología espacial/radiación | Opcional | No se incorporó un servicio observado de meteorología espacial. Existe un indicador heredado de tasa fija, explícitamente insuficiente como dosimetría. No es condición para el objetivo central |
| Usarlo en hábitat, tablet, HUD u otro formato | Alternativas opcionales | Web de escritorio/celular y receptor ESP32 con TFT 320×240 y OLED |
| Mapa estático, interactivo o generador | Alternativas permitidas | Interactivo y generador de rutas/ventanas regionales. No necesita implementar todos los formatos a la vez |
| Inspiración en otras misiones/cuerpos | Opcional | Analogía lunar declarada para presupuesto; no se copia cartografía terrestre sobre Marte ni se confunden coordenadas |
| Datos históricos, futuros o sin marco temporal específico | Alternativas permitidas | Caso histórico: productos PDS con sol y etiquetas de fechas, relieve orbital estable y escenarios hipotéticos separados |
| Imágenes de superficie de misiones para anticipar el destino | Opcional | Referencias a archivos NASA disponibles; no se incorporaron panoramas fotográficos por destino. La vista de superficie sigue siendo una reconstrucción DEM, marcada como tal |
| Considerar transporte robótico o trabajo en parejas | Opcional | Dos astronautas y planificación para pareja. También existe una app de simulación ROVER sobre ortofoto de Jezero; no hay transporte físico, modelo energético o rutas certificadas con vehículo |

## Funciones añadidas tras la auditoría

1. **Química real:** tablas PDS `veins.csv` y `nodules.csv`, etiquetas XML y hashes; 878 observaciones en 320 objetivos. Se muestran óxidos en wt%, RMSEP, sol, secuencia y punto de observación.
2. **Capa combinable:** círculos químicos sobre la malla/foto, selector de óxido, escala relativa de medianas y lectura individual al seleccionar el objetivo científico. No se asignan valores a lugares sin observación.
3. **Plan científico:** objetivo, hipótesis/procedimiento, equipo y factor de movilidad. El factor se multiplica con el factor de polvo y cambia los tiempos de ida/regreso; los kg no generan por sí solos una penalización inventada.
4. **Exclusiones manuales:** hasta 7 áreas, más la tormenta hipotética. Se dibujan, pueden retirarse, se incluyen en el POST y el motor las excluye. Cambiarlas invalida el cálculo anterior y exige recalcular.
5. **Cuaderno web:** hasta 100 observaciones propias por región/rejilla, asociadas a puntos nombrados, con fecha y marca `user_observation_unverified`. Guardado local, borrado e inclusión en exportación; no se confunde con la bitácora LittleFS.
6. **Exportación trazable:** objetivo, equipo/factor efectivo, zonas tipadas, notas propias y metadatos de las tres clases de datos, además de la misión existente.

## Demostración reproducible

1. Abrir [la aplicación](https://rutamarciana.onrender.com), seleccionar **Glen Torridon** y esperar la carga del terreno y la química.
2. Pulsar **Combinar óptica + relieve + química**. Girar/inclinar el relieve para identificar contexto topográfico con fotografía y círculos de composición.
3. Seleccionar un objetivo científico; consultar formación/sol y sus mediciones individuales. Cambiar CaO por SiO2 o FeOT para comparar los valores observados. No interpretar RMSEP como intervalo de confianza.
4. Escribir un objetivo e hipótesis, elegir dos puntos transitables y paradas intermedias. Calcular, inspeccionar FAST/SAFE y el presupuesto.
5. Registrar equipo y probar factor 1 frente a 0.5: el tiempo de marcha aumenta, con el camino geométrico normalmente igual si no cambian costes relativos ni restricciones. El presupuesto muestra las velocidades efectivas.
6. Marcar una exclusión manual fuera de los puntos obligatorios, recalcular y revisar si la ruta la rodea; si bloquea el paso, debe informar error/espera.
7. Iniciar seguimiento, usar superficie/voz o GPS terrestre de entrenamiento; registrar una observación propia por parada y exportar JSON.
8. Abrir el JSON y comprobar `mission`, `environment.blocked_zones`, `mission_observations`, `data_layers`, rutas y presupuesto. Las notas web permanecen locales; exportar no publica resultados científicos en NASA.

## Datos y límites de la química

Fuente: [NASA PDS, suplemento de diagénesis MSL](https://pds-geosciences.wustl.edu/msl/urn-nasa-pds-msl_gt_diagenesis_supplement/). Óxidos: SiO2, TiO2, Al2O3, FeOT, MgO, CaO, Na2O, K2O y MnO. Las etiquetas describen wt% y RMSEP de predicción.

Emparejamiento exacto por **Target + SeqID** con el catálogo geográfico. Se incorporaron 586 observaciones de vetas y 292 de nódulos; 5 y 11 observaciones, respectivamente, no se emparejaron y fueron excluidas. No se adivinaron sus coordenadas. Un disparo hereda la coordenada del objetivo: no es la posición independiente de cada punto láser. La cobertura temporal declarada de la tabla de vetas es 2019-01-24 a 2021-01-16; cada medición conserva su sol y cada producto su etiqueta original.

La mediana por objetivo es solo un resumen visual del óxido elegido, no una nueva medición, modelo mineralógico continuo o promedio de errores. RMSEP es error de predicción publicado, no certeza sobre una muestra futura. No hay química en vivo, interpolación a Jezero/MOLA ni sensores químicos conectados.

## Qué falta para afirmar uso operativo real

Validación de precisión/registro geográfico, cartografía de peligros a escala adecuada, posicionamiento marciano real, meteorología/radiación observada, modelo de traje/carga/consumibles y evaluación humana sobre terreno. La aplicación no puede añadirse automáticamente esas garantías con controles de software. GPS terrestre, radio, TFT/OLED, audio y batería aún necesitan pruebas físicas. La revisión verifica cobertura funcional del reto educativo, no seguridad de una Marswalk real.
