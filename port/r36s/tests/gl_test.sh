#!/bin/sh
# The R36S's vitaGL layer tested on the build machine (Mesa's OpenGL ES in Xvfb): see gl_test.c.
# Needs: gcc, libsdl2-dev, xvfb, Mesa (libgl1-mesa-dri), glslang-tools (for shader_check.py).
set -e
cd "$(dirname "$0")/../../.."
python3 port/r36s/tests/shader_check.py
S=port/r36s/shim
T=port/r36s/third_party
mkdir -p port/build/gl_test
gcc -O1 -g -Wall -Wno-unused-function -D_GNU_SOURCE -I$S -I$T -I$T/khronos $(sdl2-config --cflags | sed 's|-I/usr/include/SDL2||') -I/usr/include \
    -o port/build/gl_test/gl_test port/r36s/tests/gl_test.c $S/r36s_gl.c $S/r36s_cg.c $S/r36s_text.c $S/r36s_psp2.c $(sdl2-config --libs) -lpthread
cd port/build/gl_test && ln -sfn ../shader_check shader_check_dummy && cd ../../..
SDL_VIDEODRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s "-screen 0 640x480x24" port/build/gl_test/gl_test
