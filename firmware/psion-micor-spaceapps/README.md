# Psion Micor Space Apps · guía y ROVER

Base Psion Micor 4.3.1 / Space Apps suministrada por el usuario. Integra Misión (presupuesto EVA y guía por Wi-Fi), Bitácora y **ROVER**, módulo 3 de simulación sobre la ortofoto de Jezero. La TFT es ST7789 de **320×240 horizontal**, con OLED y encoder de la base.

- [Guía celular/ESP, hardware y compilación](../../docs/NAVEGADOR_ESP32.md).
- [ROVER: controles, mosaicos LittleFS, pruebas y límites](../../docs/MODULO_ROVER_ESP32.md).

El sketch principal incorpora los enganches append-only del módulo ROVER. `spaceapps_apps.h` conserva la guía actual y la bitácora, e incluye `rover_app.h`. Los núcleos EVA/bitácora, su contrato y `forest.ino` se conservan. Los manifiestos `upstream-sha256.json` y `rover-upstream-sha256.json` permiten comparar ambas entregas recibidas.

La guía Wi-Fi no transfiere automáticamente la geometría al módulo ROVER: sin un proveedor opcional de `pov_contract.h`, se muestra **RUTA DEMO**. No es un rover físico ni un panorama tomado desde el suelo.

Los archivos `data/pov` se cargan aparte en LittleFS; subir el sketch no basta para instalar la fotografía. Respaldar bitácoras/configuración antes de reemplazar una imagen de filesystem. No se eliminaron juegos/apps, no se cambiaron GPIO y no se flasheó hardware.

Compilación integrada: **1,992,078 B flash (94%), 94,516 B RAM estática (28%)**, ESP32 core 3.3.12 y partición `no_ota`.
