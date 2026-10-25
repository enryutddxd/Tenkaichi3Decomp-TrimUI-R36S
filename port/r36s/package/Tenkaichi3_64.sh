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
unset SDL_BLITTER_DISABLED
# (the pictures put on the screen by the render thread itself: with a thread of their own the screen stayed black,
# through EGL (test 1) and through SDL (test 7). bt3.txt present=1 to try it)
grep -q '^present=1' "$GAMEDIR/bt3.txt" 2>/dev/null && export R36_PRESENT=1 || export R36_PRESENT=0
# (bt3.txt vsync=0: the swap not held to the screen's refresh; read here, the program reads bt3.txt after the window is made)
grep -q '^vsync=0' "$GAMEDIR/bt3.txt" 2>/dev/null && export R36_VSYNC=0

# the processor and the graphics chip at their full speed while the game runs (put back as they were afterwards)
GOVS=$(ls /sys/devices/system/cpu/cpufreq/policy*/scaling_governor /sys/class/devfreq/*/governor 2>/dev/null)
declare -A OLDGOV
for g in $GOVS; do
  OLDGOV[$g]=$(cat "$g" 2>/dev/null)
  $ESUDO sh -c "echo performance > $g" 2>/dev/null
  echo "governor $g: ${OLDGOV[$g]} -> $(cat "$g" 2>/dev/null)"
done
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
for g in $GOVS; do
  [ -n "${OLDGOV[$g]}" ] && $ESUDO sh -c "echo ${OLDGOV[$g]} > $g" 2>/dev/null
done
if type pm_finish >/dev/null 2>&1; then
  pm_finish
fi
printf "\033c" > /dev/tty1 2>/dev/null
