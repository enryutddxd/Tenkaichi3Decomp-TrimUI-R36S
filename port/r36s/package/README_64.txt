Dragon Ball Z: Budokai Tenkaichi 3 — R36S (ArkOS), versión de 64 bits / 64-bit version
==================================================================================

ESPAÑOL
-------
Port nativo de 64 bits para la R36S (y otras consolas RK3326 con ArkOS) del decomp Tenkaichi3Decomp, basado en
el port de PS Vita de arielxflores. No es un emulador. NO incluye datos del juego: necesitas tu propio disco USA
(SLUS-21678). Peleas a ~28-30 fps a 640x480 con la consola fría; al calentarse, el sistema baja la velocidad
del chip y pueden bajar algunos fps.

Instalación
  1. Copia el contenido del zip a la carpeta "ports" de la tarjeta (en ArkOS: roms/ports):
       ports/Dragon Ball Z Budokai Tenkaichi 3 (64-bit).sh
       ports/bt3_64/   (el programa)
  2. De TU disco USA copia SLUS_216.78 y las carpetas BIN y DATA a ports/bt3_64/disc/.
     (Si ya los tienes en ports/bt3/disc/ de la versión de 32 bits, no hace falta copiarlos otra vez.)
  3. Reinicia EmulationStation y abre "Dragon Ball Z Budokai Tenkaichi 3 (64-bit)" desde Ports.

Controles (por posición, como en PS2)
  Abajo = X · derecha = Círculo · izquierda = Cuadrado · arriba = Triángulo · L1/R1, L2/R2 como en PS2
  L3 / R3: presionar los sticks o SELECT + L1 / SELECT + R1 · START + SELECT 2 segundos: salir

Si algo falla, manda log.txt, err.txt y launcher.txt de ports/bt3_64/.

Ajustes opcionales en ports/bt3_64/bt3.txt (una línea por ajuste, sin espacios):
  fxwt=1           con los efectos de distorsión de los work targets (apagados por defecto: más lento)
  fxglow=1         con el brillo (apagado por defecto)
  fxtbl=0          sin niebla ni ajustes de color
  fxol=0           sin el contorno de los personajes

ENGLISH
-------
A native 64-bit port for the R36S (and other RK3326 handhelds on ArkOS) of the Tenkaichi3Decomp decompilation,
built on arielxflores' PS Vita port. Not an emulator; NO game data included: you need your own USA disc
(SLUS-21678). Fights at ~28-30 fps at 640x480 while the handheld is cool; when it heats up the system slows the
chip down and a few fps may be lost.
Install: copy the zip's contents to the card's "ports" folder (ArkOS: roms/ports), then copy SLUS_216.78 and the
BIN and DATA folders of your own disc to ports/bt3_64/disc/ (or keep them in the 32-bit version's ports/bt3/disc/).
Start it from Ports. START + SELECT held for two seconds quits.


CRÉDITOS / CREDITS
------------------
  - Port para TrimUI Smart Pro y R36S (capa Linux/SDL2/OpenGL ES, optimización para PowerVR y Mali):
    EnryuTG (con ayuda de Claude, de Anthropic).
    TrimUI Smart Pro and R36S port (Linux/SDL2/OpenGL ES layer, PowerVR and Mali work): EnryuTG (with Claude, by Anthropic).
  - Port de PS Vita en el que se basa (renderer, sistema, opciones): arielxflores
    https://github.com/arielxflores/Tenkaichi3Decomp-Vita
    The PS Vita port this is built on (renderer, system layer, options): arielxflores.
  - Port de PC Tenkaichi3Decomp: z3xox y colaboradores — https://github.com/z3xox/Tenkaichi3Decomp
    The Tenkaichi3Decomp PC port: z3xox and contributors.
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
