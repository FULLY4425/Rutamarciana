# Mars OS · reloj Ruta Marciana

Firmware independiente basado en el RAR `psion-marte-os.rar` aportado por el usuario. Está adaptado a **este repositorio y a https://rutamarciana.onrender.com**. El firmware anterior se conserva en `../psion-micor-spaceapps/`.

- TFT principal ST7789: **320×240 horizontal**.
- Segunda pantalla GMT130-V1.0/ST7789: **240×240**, SPI; no es el OLED I²C anterior.
- Mapa sin conexión de Glen Torridon, 136 sitios y 718 objetivos, regenerado desde `assets/glen-torridon`.
- Guía privada del celular por HTTPS, con caducidad, espera ante posición antigua y sonidos de giro.
- Misiones por código temporal y sincronización local mediante el Mars relay opcional.

Abre [la guía completa](../../docs/RELOJ_MARS_OS.md) para compilar, conectar, comparar los modos y conservar archivos. Mantén **ambas carpetas de firmware**: el reloj reutiliza el parser, las raíces TLS y el transporte de navegación de su carpeta hermana.

El reloj no incorpora GPS. Los recorridos locales son una simulación calculada con A* y no equivalen a FAST/SAFE de Python. El mapa sin conexión no cubre todo Marte; la web conserva su cobertura global y sus otros recortes.

`upstream-sha256.json` registra los archivos recibidos antes de adaptarlos; `map-source-sha256.json` registra los datos actuales usados para generar el mapa. No se incluyen ejecutables Windows, preferencias personales ni logs recibidos. No se ha flasheado una placa física.
