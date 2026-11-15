# Psion Micor Space Apps + guía Ruta Marciana

Base: firmware aportado por el usuario en `psion-micor-spaceapps_entrega_2026-11-15.zip`. Crédito de la base: Psion Micor 4.3.1 / Space Apps. Se conserva la bitácora, el presupuesto EVA, el contrato de 8 paradas y el cableado original.

La pantalla Misión → Mapa pasa a ser un receptor de indicaciones por Wi-Fi para un celular con GPS, en modo de entrenamiento sobre Marte. No se añade GPS al ESP ni se modifica el sketch principal.

Uso, compilación, enlace privado, límites y pruebas: [guía completa](../../docs/NAVEGADOR_ESP32.md).

`upstream-sha256.json` permite comparar los archivos originales del ZIP. Solo `spaceapps_apps.h` cambia; se añaden los módulos `navigator_*`, incluido el renderer TFT `navigator_tft.h` para **320 × 240 horizontal**. No se han aplicado las propuestas de eliminación de juegos/apps que aparecían en los documentos del ZIP.
