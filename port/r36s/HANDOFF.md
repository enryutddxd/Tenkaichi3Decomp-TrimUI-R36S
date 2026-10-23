# R36S port — estado del trabajo (para continuarlo)

Port del port de PS Vita (Tenkaichi3Decomp-Vita) a la R36S (RK3326: 4× Cortex-A35 a 1.5 GHz, Mali-G31, pantalla
640×480) con dArkOSRE / ArkOS. Programa Linux ARM de 32 bits (armhf) sobre SDL2 + OpenGL ES 3. Todo está en la rama
`r36s`; los cambios a las fuentes de la Vita y del juego van dentro de `#ifdef PORT_R36S`. Ver `README.md` para la
estructura y la compilación.

## Estado

- **Funciona:** arranque, videos (FMV a velocidad completa), menús a 60 fps, selección, peleas completas, sonido,
  controles (mapeo PortMaster), guardado.
- **Rendimiento en peleas:** 15–20 fps (el juego corre al 100 % de velocidad y salta imágenes). Caídas con poderes
  (los efectos multiplican los primitivos GS por 10×). Objetivo de 30 fps **no alcanzado**.
- **Probado en hardware real** por el usuario en cada versión (v1…v16). No hay acceso a la consola desde aquí: cada
  prueba es compilar → el usuario copia `ports/bt3/bt3`, `bt3.sym` y el `.sh` → juega → manda `log.txt`, `err.txt`,
  `launcher.txt`, `prof.txt`, `crash.txt` / `hang.txt` si hay.

## Compilar y probar

```sh
pip install ziglang libclang
sudo apt install binutils-arm-linux-gnueabihf gcc-arm-linux-gnueabihf zip glslang-tools xvfb libsdl2-dev libgl1-mesa-dri
python3 port/r36s/build.py            # → port/build/r36s/Tenkaichi3Decomp-R36S.zip (+ bt3.unstripped para crash.txt)
BT3_INCREMENTAL=1 python3 port/r36s/build.py   # reusa objetos (borrar los .o si cambian flags)
bash port/r36s/tests/gl_test.sh       # shaders (glslang) + capa GL en Mesa/Xvfb (debe decir "all passed")
```

Prueba rápida de arranque sin disco (qemu-user): requiere un SDL2 armhf en `libs/`; con
`SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy qemu-arm-static -L /usr/arm-linux-gnueabihf ./bt3` debe llegar al
mensaje "FATAL: the game's program was not found" sin crash.txt. El disco del juego (SLUS-21678, USA) no está aquí.

Compilador: zig cc (`-target arm-linux-gnueabihf.2.28`, glibc 2.28) para casi todo; GCC cruzado para los tipos
"gs" y "hard" (renderizador y front end GS) con `-U_TIME_BITS -U_FILE_OFFSET_BITS` (time_t de 32 bits, igual que el
resto) y sin fortify. Enlace: zig/lld con `-rtlib=none` + `libgcc.a` (si no, zig mete su memcpy/bcmp lentos).
**Sin NEON en el código del juego** (`-mcpu=cortex_a35-neon`): clang ponía vld1/vst1 con alineación :128 en datos de
8 bytes → SIGBUS en la R36S. NEON solo en vita_vgl.c (clang) y en lo compilado con GCC.

## Opciones de ejecución

`ports/bt3/bt3.txt` (una por línea): `res=WxH`, `menures=WxH`, `vsync=0/1`, `pin=1` (fijar hilos a núcleos),
`rt=1` (hilo de render de la Vita), además de las de la Vita (`report=`, `shadows=`, `safe=`, `safe2=`...).
Variables de entorno (en el .sh, antes de `./bt3`): `R36_RT=1`, `R36_BATCH=0` (piezas una por una), `R36_COARSE=0`
(reloj por clock_gettime), `R36_MALLOC=0`, `R36_PROF=0` (sin prof.txt), `R36_GLTIME=1` (tiempo por llamada GL en
prof.txt; cuesta: cada lectura del reloj es una llamada al sistema), `R36_NOUNSYNC=1`, `R36_VSYNC`, `R36_PIN`.

## Herramientas de diagnóstico incluidas

- `log.txt`: informe de la Vita cada 2 s (fps, zonas del hilo de dibujo "picture thread, ms": list, GS packets,
  GS primitives, to the chip, swap...), más líneas `r36`.
- `err.txt`: `r36 gl: per picture (120)` cada 120 imágenes.
- `prof.txt` (shim/r36s_prof.c): perfilador por muestreo de tiempo de CPU por hilo cada 10 s, por función con
  `bt3.sym`; tiempo usuario/kernel, fallos de página y cambios de contexto por hilo; uso del contexto GL; dibujos
  por imagen. Ojo: las muestras dentro de librerías ([libgbm] = driver Mali) tienen el "caller" adivinado del stack
  y a veces es basura.
- `crash.txt`: registros, mapa de memoria, direcciones; simbolizar con `bt3.unstripped` de esa versión
  (`port/build/r36s/vN/`).
- `hang.txt`: watchdog de la Vita (sin imagen 3 s).

## Hallazgos (medidos en la consola)

1. **El cuello de botella es el hilo de dibujo de la Vita (gr_thread), ~80–90 % de un núcleo, todo tiempo de
   usuario** (v16: 7.76 s usuario / 0.25 s kernel por cada 10 s, casi sin fallos de página). El juego (≈20 %) y el
   lector de listas (≈20 %) van sobrados.
2. Zonas por imagen en pelea (≈50 ms/imagen): GS packets ~14 ms, GS primitives ~10, swap ~7 (present + eglSwap),
   to the chip ~7, el resto repartido. Según comentarios del código, en la Vita ese hilo toma ~19 ms/imagen.
3. El driver Mali (userspace, aparece como `libgbm.so.1`) es ~12–15 % del hilo de dibujo.
4. `clock_gettime` es una llamada al sistema en esta consola (kernel de 64 bits sin vDSO de 32 bits) y el contador
   ARM (CNTVCT) da SIGILL desde userspace. Solución actual: hilo `r36_clock` que actualiza la hora cada 0.5 ms.
5. CPU a 1512 MHz, GPU 520 MHz, governors en performance (lo pone el .sh); la GPU termina cada imagen en ~8 ms.

## Lo que se intentó y su efecto (fps de pelea ≈ 16 antes de todo esto)

| Versión | Cambio | Resultado |
|---|---|---|
| v4 | Sin NEON en el juego (SIGBUS) | Las peleas cargan |
| v5 | Escrituras a buffers mapeados con glMapBufferRange UNSYNCHRONIZED (antes glBufferSubData copiaba 16 MB) | Videos y menús a velocidad completa; peleas de ~5 a ~16 fps |
| v8 | memcpy/bcmp de glibc en vez de los de zig (-rtlib=none + libgcc.a) | Sin cambio visible |
| v9 | Filtro de estado GL redundante (bind buffer/attrib/program/FBO) | Sin cambio |
| v10–v12 | Hilo de render de la Vita (contexto GL que pasa entre hilos) | 16–19 fps, más irregular (el hilo principal espera al de render al final de cada imagen); hubo deadlocks corregidos; queda opcional con `rt=1` |
| v13 | Vuelta a un solo hilo | Estable |
| v14 | Piezas de modelo (direct path) agrupadas: hasta 16/32 por dibujo, constantes en slots de `U`, slot por `gl_VertexID` (vita_vgl.c `dp_draw_batched`, `dp_batch_src`) | ~900 → ~80 dibujos por imagen, **mismos fps** (los dibujos no eran el freno) |
| v15 | GCC -O3 + NEON para el renderizador (como el build de Vita); reloj por hilo | Sin cambio |
| v16 | mallopt (sin mmap por bloque) + métricas de sistema | Sin cambio; las métricas descartan sistema/memoria |

## Ideas que quedan (no probadas)

- Ritmo fijo (p. ej. 15 fps constantes en peleas) para que se sienta parejo: el código de ritmo de la Vita está en
  vita_vgl.c (`pace_*`, "pictures in step with the game's clock").
- Reducir el trabajo de "GS packets"/"GS primitives" (gs_core.c, vita_vgl.c GsGpu_Draw_impl, Gs_RectId, tex_sig,
  vram_rw): perfilar más fino (contadores de ciclos no disponibles: `scePerf*` son stubs), o mover parte al hilo
  lector de listas (que tiene CPU libre).
- Bajar efectos caros (glow, outline, sombras: bits de `gVglSafe`/`gVglSafe2`, `shadows=2` ya por defecto).
- Thumb-2 (`-mthumb`) para menos presión de i-cache en el A35 (el perfilador asume código ARM para los callers).
- Present/swap (~7 ms): triple buffer, o evitar el quad de presentación dibujando directo al framebuffer 0.

## Archivos clave

- `port/r36s/build.py` — compilación (tipos game/ee/vu0/gs/hard/shim, flags, stubs automáticos de símbolos que
  faltan: revisar la lista "N stubs" si aparece un símbolo nuevo de la libc → significa incompatibilidad).
- `port/r36s/shim/r36s_gl.c` — vitaGL sobre GLES3 (FBOs al revés, texturas virtuales, buffers mapeados con
  sombra en CPU + `vglR36Dirty`, contexto GL entre hilos, filtro de estado, informe de dibujos).
- `port/r36s/shim/r36s_psp2.c` — sistema Vita sobre Linux (hilos, semáforos con liberación del contexto GL al
  bloquear, archivos, control, vblank 59.94 Hz, audio, reloj, crash handler, mallopt).
- `port/r36s/shim/r36s_cg.c` — traductor Cg → GLSL ES 3.00.
- `port/r36s/shim/r36s_prof.c` — perfilador.
- `port/vita/vita_vgl.c` — renderizador de la Vita (cambios R36S marcados): batching `dp_draw_batched`, hilo de
  render opcional, tamaños 640×480, conversión de paletas.
- `port/r36s/package/Tenkaichi3.sh` — lanzador PortMaster (governors, logs a launcher.txt, dmesg al fallar).
