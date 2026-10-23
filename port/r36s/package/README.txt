Dragon Ball Z: Budokai Tenkaichi 3 — R36S / ArkOS (dArkOSRE) — TEST BUILD
===========================================================================

ESPAÑOL
-------
Port nativo para el R36S (y otras consolas RK3326 con ArkOS / dArkOSRE) del port de PS Vita
Tenkaichi3Decomp-Vita, que a su vez viene de Tenkaichi3Decomp (z3xox) y la decompilación BT3-Decompiled.
No es un emulador. NO incluye datos del juego: necesitas tu propio disco USA (SLUS-21678).

Esta es una VERSIÓN DE PRUEBA: se hizo sin una consola para probarla. Puede no arrancar, verse mal o ir lento.

Instalación
  1. Copia el contenido del zip a la tarjeta, en la carpeta "ports" de ArkOS:
       ports/Dragon Ball Z Budokai Tenkaichi 3.sh
       ports/bt3/   (el programa)
  2. De la imagen .iso de TU disco USA copia SLUS_216.78 y las carpetas BIN y DATA a ports/bt3/disc/:
       ports/bt3/disc/SLUS_216.78
       ports/bt3/disc/BIN/DBZP.BIN
       ports/bt3/disc/DATA/PZS3US0.AFS  (y el resto de la carpeta DATA)
     En Windows: abre la .iso con 7-Zip y extrae SLUS_216.78, BIN y DATA.
  3. Reinicia EmulationStation (o la consola) y abre "Dragon Ball Z Budokai Tenkaichi 3" desde Ports.

Controles (por posición, como en PS2)
  Botón de abajo = X (Dash) · derecha = Círculo (Guard) · izquierda = Cuadrado (Fight) · arriba = Triángulo (Ki Blast)
  L1 / R1 = L1 / R1 · L2 / R2 = L2 / R2 · Stick izquierdo = mover
  L3 / R3: presionar los sticks (si tu consola lo permite) o SELECT + L1 / SELECT + R1
  START + SELECT durante 2 segundos: salir del juego

Si algo falla, mándame estos archivos de ports/bt3/:
  log.txt, err.txt, launcher.txt y crash.txt (si existe)

Ajustes opcionales en ports/bt3/bt3.txt (una línea por ajuste):
  res=480x360     dibujar las peleas a menor resolución (más rápido)
  vsync=0         sin esperar el refresco de pantalla
  pin=1           fijar los hilos del juego a núcleos concretos (como en la Vita)
  rt=1            los dibujos en un hilo propio, como en la Vita (experimental: en las pruebas, no más rápido)

ENGLISH
-------
A native port for the R36S (and other RK3326 handhelds on ArkOS / dArkOSRE) of the PS Vita port
Tenkaichi3Decomp-Vita, itself based on Tenkaichi3Decomp (z3xox) and the BT3-Decompiled decompilation.
Not an emulator. NO game data included: you need your own USA disc (SLUS-21678).

This is a TEST BUILD made without a device to try it on. It may not start, look wrong or run slowly.

Install: copy the zip's contents into ArkOS's "ports" folder, then copy SLUS_216.78 and the BIN and DATA folders
of your own USA disc image to ports/bt3/disc/. Start it from Ports. Controls are by position, as on the PS2.
START + SELECT held for two seconds quits. On a problem, send log.txt, err.txt, launcher.txt and crash.txt.

License: the port code is under the GNU AGPL v3 (LICENSE.txt). The game's code comes from the decompilation and
belongs to the game's rights holders; this project is not affiliated with or endorsed by them.
