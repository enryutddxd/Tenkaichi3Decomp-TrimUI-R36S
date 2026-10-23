Dragon Ball Z: Budokai Tenkaichi 3 — TrimUI Smart Pro (sistema stock + PortMaster) — VERSIÓN DE PRUEBA
=====================================================================================================

ESPAÑOL
-------
Port nativo (64 bits, aarch64) para la TrimUI Smart Pro del port de PS Vita Tenkaichi3Decomp-Vita, que viene de
Tenkaichi3Decomp (z3xox) y la decompilación BT3-Decompiled. No es un emulador. NO incluye datos del juego:
necesitas tu propio disco USA (SLUS-21678).

Instalación (en la tarjeta SD)
  1. Copia el contenido del zip a la raíz de la tarjeta:
       Roms/PORTS/Dragon Ball Z Budokai Tenkaichi 3.sh
       Data/ports/bt3/   (el programa)
  2. De la imagen .iso de TU disco USA copia SLUS_216.78 y las carpetas BIN y DATA a Data/ports/bt3/disc/:
       Data/ports/bt3/disc/SLUS_216.78
       Data/ports/bt3/disc/BIN/DBZP.BIN
       Data/ports/bt3/disc/DATA/PZS3US0.AFS  (y el resto de la carpeta DATA)
     En Windows: abre la .iso con 7-Zip y extrae SLUS_216.78, BIN y DATA.
  3. Abre "Dragon Ball Z Budokai Tenkaichi 3" desde Ports.

Controles (por posición, como en PS2)
  Botón de abajo = X · derecha = Círculo · izquierda = Cuadrado · arriba = Triángulo
  L1 / R1 = L1 / R1 · L2 / R2 = L2 / R2 · sticks = sticks (L3 / R3 al presionarlos)
  START + SELECT durante 2 segundos: salir del juego

Si algo falla, mándame estos archivos de Data/ports/bt3/:
  log.txt, err.txt, launcher.txt y crash.txt (si existe)

Ajustes opcionales en Data/ports/bt3/bt3.txt (una línea por ajuste):
  res=1280x720    peleas a la resolución de la pantalla (por defecto 960x540)
  res=640x360     peleas a menor resolución (más rápido)
  menures=960x540 menús a menor resolución
  wide=0          imagen 4:3 en el centro
  vsync=0         sin esperar el refresco de pantalla

ENGLISH
-------
A native 64-bit port for the TrimUI Smart Pro (stock system, PortMaster). Not an emulator; NO game data
included: you need your own USA disc (SLUS-21678). TEST BUILD.
Install: copy the zip's contents to the root of the SD card (Roms/PORTS/ and Data/ports/bt3/), then copy
SLUS_216.78 and the BIN and DATA folders of your own disc image to Data/ports/bt3/disc/. Start it from Ports.
START + SELECT held for two seconds quits. On a problem, send log.txt, err.txt, launcher.txt and crash.txt.

License: the port code is under the GNU AGPL v3 (LICENSE.txt). The game's code comes from the decompilation and
the game's data from your own disc.
