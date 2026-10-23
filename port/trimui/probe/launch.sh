#!/bin/sh
# BT3 probe (TrimUI Smart Pro, stock firmware): writes probe.txt next to this file.
cd "$(dirname "$0")"
export LD_LIBRARY_PATH="/usr/trimui/lib:/usr/lib:$LD_LIBRARY_PATH"
chmod +x ./probe ./armhf_test 2>/dev/null
./probe > probe_out.txt 2>&1
echo "exit $?" >> probe_out.txt
ls -la /usr/trimui/lib /usr/lib 2>/dev/null | grep -iE "sdl|gles|egl|pvr|mali|gbm" >> probe_out.txt
