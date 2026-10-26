#!/bin/bash
# Dragon Ball Z: Budokai Tenkaichi 3 (Tenkaichi3Decomp) for the R36S and other RK3326 handhelds (ArkOS, dArkOSRE):
# the 64-bit program (ports/bt3_64). The game files of your own USA disc (SLUS-21678) go to ports/bt3_64/disc/, or stay
# in the 32-bit version's ports/bt3/disc/ (used when this one's folder has none).

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

if [ -f "$controlfolder/control.txt" ]; then
  source $controlfolder/control.txt
  [ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
  get_controls
  export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
else
  # without PortMaster: the folder next to this script
  directory="$(cd "$(dirname "$0")/.." && pwd)"
  directory="${directory#/}"
  ESUDO="sudo"
  GPTOKEYB=":"
fi

GAMEDIR="/$directory/ports/bt3_64"
cd "$GAMEDIR" || exit 1
if [ ! -f "$GAMEDIR/disc/SLUS_216.78" ] && [ -f "/$directory/ports/bt3/disc/SLUS_216.78" ]; then
  export BT3_DISC="/$directory/ports/bt3/disc"   # (the 32-bit version's copy of the disc)
fi
# (the program writes its own log.txt here; what the script and the system print goes to launcher.txt)
> "$GAMEDIR/launcher.txt" && exec > >(tee "$GAMEDIR/launcher.txt") 2>&1

export LD_LIBRARY_PATH="$GAMEDIR/libs:$LD_LIBRARY_PATH"
echo "disc folder: ${BT3_DISC:-$GAMEDIR/disc}"; uname -m
export R36_PROF=0   # (no prof.txt: the test builds' measurements off)
unset SDL_BLITTER_DISABLED
# (the pictures put on the screen by the render thread itself: with a thread of their own the screen stayed black,
# through EGL (test 1) and through SDL (test 7). bt3.txt present=1 to try it)
# (test 21: made current through SDL on its thread, as the other R36S port of the game does: on; bt3.txt present=0 off)
grep -q '^present=0' "$GAMEDIR/bt3.txt" 2>/dev/null && export R36_PRESENT=0 || export R36_PRESENT=1
# (bt3.txt vsync=0: the swap not held to the screen's refresh; read here, the program reads bt3.txt after the window is made)
grep -q '^vsync=0' "$GAMEDIR/bt3.txt" 2>/dev/null && export R36_VSYNC=0
# (bt3.txt fragmp=1: the pixel shaders in medium precision; test 9: no faster)
grep -q '^fragmp=1' "$GAMEDIR/bt3.txt" 2>/dev/null && export R36_FRAG_MP=1 || export R36_FRAG_MP=0
# (bt3.txt gpufin=1: the GPU's own time per picture measured, glFinish before the swap; test 10: ~13 ms after the last draw)
grep -q '^gpufin=1' "$GAMEDIR/bt3.txt" 2>/dev/null && export R36_GPUFIN=1 || export R36_GPUFIN=0
# (bt3.txt ubgrid=4096: the uniform blocks in the stream on a grid (test 25-26; did not stop the heap growing), off by default)
UBGRID=$(sed -n 's/^ubgrid=\([0-9]*\).*/\1/p' "$GAMEDIR/bt3.txt" 2>/dev/null | tail -1); export R36_UBGRID=${UBGRID:-0}
# (bt3.txt ubo=0: the shaders' uniforms as plain uniforms, not in blocks; test 18: slower, 22-28 fps against 28-30)
grep -q '^ubo=0' "$GAMEDIR/bt3.txt" 2>/dev/null && export R36_UBO=0 || export R36_UBO=1

# the processor and the graphics chip at their full speed while the game runs (put back as they were afterwards)
GOVS=$(ls /sys/devices/system/cpu/cpufreq/policy*/scaling_governor /sys/class/devfreq/*/governor 2>/dev/null)
declare -A OLDGOV
for g in $GOVS; do
  OLDGOV[$g]=$(cat "$g" 2>/dev/null)
  $ESUDO sh -c "echo performance > $g" 2>/dev/null
  echo "governor $g: ${OLDGOV[$g]} -> $(cat "$g" 2>/dev/null)"
done
# Less heat (test 24): the processor and the graphics chip held at a lower top speed, at a lower voltage. A fight
# brings them there anyway after a few minutes (test 22: 1008 / 400 MHz from minute 12, 22-28 fps, the chip still
# climbing to 88 C); held lower from the start, the chip runs cooler and the speed steadier. Only the top is
# lowered: the system's own heat limits stay as they are. bt3.txt cpumax=<MHz> (1008, 1200, 1248, 1296, 1512),
# gpumax=<MHz> (400, 480, 520); 0 = no cap (the default since test 28: at 520 MHz 29.1 fps against 26.5, as hot).
CPUMAX=$(sed -n 's/^cpumax=\([0-9]*\).*/\1/p' "$GAMEDIR/bt3.txt" 2>/dev/null | tail -1); CPUMAX=${CPUMAX:-1200}
GPUMAX=$(sed -n 's/^gpumax=\([0-9]*\).*/\1/p' "$GAMEDIR/bt3.txt" 2>/dev/null | tail -1); GPUMAX=${GPUMAX:-0}
declare -A OLDMAX
if [ "$CPUMAX" -gt 0 ]; then
  for f in /sys/devices/system/cpu/cpufreq/policy*/scaling_max_freq; do
    [ -f "$f" ] || continue
    OLDMAX[$f]=$(cat "$f")
    $ESUDO sh -c "echo $((CPUMAX * 1000)) > $f" 2>/dev/null
    echo "processor top: ${OLDMAX[$f]} -> $(cat "$f") kHz"
  done
fi
if [ "$GPUMAX" -gt 0 ]; then
  for d in /sys/class/devfreq/*gpu*; do
    [ -f "$d/max_freq" ] || continue
    OLDMAX[$d/max_freq]=$(cat "$d/max_freq")
    $ESUDO sh -c "echo $((GPUMAX * 1000000)) > $d/max_freq" 2>/dev/null
    echo "graphics chip top: ${OLDMAX[$d/max_freq]} -> $(cat "$d/max_freq") Hz"
  done
fi
cat /sys/devices/system/cpu/cpufreq/policy*/scaling_max_freq /sys/devices/system/cpu/cpufreq/policy*/scaling_cur_freq /sys/class/devfreq/*/cur_freq 2>/dev/null | tr '\n' ' '; echo "(processor max, now; devfreq now)"

chmod +x "$GAMEDIR/bt3"
$GPTOKEYB "bt3" &
if type pm_platform_helper >/dev/null 2>&1; then
  pm_platform_helper "$GAMEDIR/bt3"
fi
free -m 2>/dev/null | head -2
./bt3
STATUS=$?
echo "bt3 ended: status $STATUS (137: stopped by the system, out of memory?; 139: crash; 135: bus error)"
free -m 2>/dev/null | head -2
if [ $STATUS -ge 128 ]; then
  $ESUDO dmesg 2>/dev/null | tail -40
fi

$ESUDO kill -9 $(pidof gptokeyb) 2>/dev/null
for f in "${!OLDMAX[@]}"; do
  $ESUDO sh -c "echo ${OLDMAX[$f]} > $f" 2>/dev/null
done
for g in $GOVS; do
  [ -n "${OLDGOV[$g]}" ] && $ESUDO sh -c "echo ${OLDGOV[$g]} > $g" 2>/dev/null
done
if type pm_finish >/dev/null 2>&1; then
  pm_finish
fi
printf "\033c" > /dev/tty1 2>/dev/null
