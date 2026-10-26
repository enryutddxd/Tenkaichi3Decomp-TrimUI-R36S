Dragon Ball Z: Budokai Tenkaichi 3 — TrimUI Smart Pro (CrossMix + PortMaster) — v1.2
==========================================================================

ESPAÑOL
-------
Port nativo (64 bits, aarch64) para la TrimUI Smart Pro del port de PS Vita Tenkaichi3Decomp-Vita, que viene de
Tenkaichi3Decomp (z3xox) y la decompilación BT3-Decompiled. No es un emulador. NO incluye datos del juego:
necesitas tu propio disco USA (SLUS-21678).

Novedades de la v1.2
  - L1 + R1 a la vez = L3, y el botón MENU = R3 (los sticks de la TrimUI no se presionan).
  - Menos retraso entre los botones y la pantalla (máximo una imagen en espera).

Novedades de la v1.1
  - Gran Saiyaman (Gohan y Videl) y Goku GT SSJ3 ya no cierran el juego.
  - Ya no se pone lento tras varios minutos de pelea (el espacio de los personajes en la GPU se llenaba).
  - La explosión del planeta ya no se pone lenta.
  - Todas las imágenes dibujadas se muestran (antes se descartaban algunas): más fluido y menos calor.

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
  L1 / R1 = L1 / R1 · L2 / R2 = L2 / R2 · sticks = sticks
  L1 + R1 a la vez = L3 · MENU = R3
  START + SELECT durante 2 segundos: salir del juego

Si algo falla, mándame estos archivos de Data/ports/bt3/:
  log.txt, err.txt, launcher.txt y crash.txt (si existe)

Ajustes opcionales en Data/ports/bt3/bt3.txt (una línea por ajuste):
  res=1280x720     peleas a la resolución de la pantalla (por defecto 854x480)
  menures=1280x720 menús a la resolución de la pantalla (por defecto 854x480)
  fxwt=1           con los efectos de distorsión de los work targets (apagados por defecto: ~4 fps menos)
  fxglow=1         con el brillo (apagado por defecto)
  fxtbl=0          sin niebla ni ajustes de color
  fxol=0           sin el contorno de los personajes
  glowhalf=0       el brillo calculado en cada imagen (más lento)
  wide=0          imagen 4:3 en el centro
  vsync=0         sin esperar el refresco de pantalla
  nodrop=0        como en la v1.0: una imagen nueva reemplaza a la que no se mostró aún (menos retraso, menos fluido)
  a0skip=0        sin omitir las piezas invisibles de algunos efectos (la explosión del planeta, más lenta)

ENGLISH
-------
A native 64-bit port for the TrimUI Smart Pro (stock system, PortMaster). Not an emulator; NO game data
included: you need your own USA disc (SLUS-21678). v1.2: L1 + R1 together = L3, MENU = R3; less delay between
the buttons and the screen. v1.1: Great Saiyaman (Gohan, Videl) and Goku GT SSJ3 no longer
quit the game; no more slowdown after minutes of a fight, nor in the planet's explosion; every picture drawn is shown.
Install: copy the zip's contents to the root of the SD card (Roms/PORTS/ and Data/ports/bt3/), then copy
SLUS_216.78 and the BIN and DATA folders of your own disc image to Data/ports/bt3/disc/. Start it from Ports.
START + SELECT held for two seconds quits. On a problem, send log.txt, err.txt, launcher.txt and crash.txt.


CRÉDITOS / CREDITS
------------------
  - Port para TrimUI Smart Pro y R36S (capa Linux/SDL2/OpenGL ES, optimización para PowerVR):
    EnryuTG (con ayuda de Claude, de Anthropic).
    TrimUI Smart Pro and R36S port (Linux/SDL2/OpenGL ES layer, PowerVR work): EnryuTG (with Claude, by Anthropic).
  - Port de PS Vita en el que se basa (renderer, sistema, opciones): arielxflores
    https://github.com/arielxflores/Tenkaichi3Decomp-Vita
    The PS Vita port this is built on (renderer, system layer, options): arielxflores.
  - Port de PC Tenkaichi3Decomp: z3xox y colaboradores — https://github.com/z3xox/Tenkaichi3Decomp
    The Tenkaichi3Decomp PC port: z3xox and contributors.
    (De ahí viene también la traducción a C de ObjSeam_TransformVtx, la "costura" de algunos personajes.
     Upstream's C translation of ObjSeam_TransformVtx comes from there too.)
  - La decompilación BT3-Decompiled y sus autores. / The BT3-Decompiled decompilation and its authors.
  - Spike / Bandai Namco: Dragon Ball Z: Budokai Tenkaichi 3, el juego original (sus derechos son de sus dueños).
    Spike / Bandai Namco: the original game (its rights belong to its owners).
  - Rinnegatamante (vitaGL, cuya interfaz imita la capa de este port), el equipo de SDL, Khronos, PortMaster.
  - Código de terceros: THIRD_PARTY.txt. / Third-party code: THIRD_PARTY.txt.

LICENCIA / LICENSE
------------------
El código del port está bajo la GNU Affero General Public License v3 (LICENSE.txt), igual que el port de Vita en
que se basa; la parte anterior a ese cambio de licencia venía bajo MIT, cuyo aviso se conserva en LICENSE-MIT.txt.
Esta versión está modificada respecto al port de Vita de arielxflores (ver créditos). Por la AGPL, el código
fuente completo de esta versión está disponible para quien lo reciba:
https://github.com/enryutddxd/Tenkaichi3Decomp-TrimUI
El código del juego viene de la decompilación; los datos del juego, de tu propio disco. Este proyecto no está
afiliado ni respaldado por los dueños del juego.

The port code is under the GNU AGPL v3 (LICENSE.txt), as the Vita port it is built on; the earlier part came under
MIT, whose notice is kept in LICENSE-MIT.txt. This version is modified from arielxflores' Vita port. Under the
AGPL, the complete source code of this version is available to whoever receives it:
https://github.com/enryutddxd/Tenkaichi3Decomp-TrimUI. The game's code comes from the decompilation; the game's data from your own disc. Not affiliated
with or endorsed by the game's rights holders.
