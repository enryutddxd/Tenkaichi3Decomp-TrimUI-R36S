#!/bin/bash
# Dragon Ball Z: Budokai Tenkaichi 3 (Tenkaichi3Decomp) for the R36S and other RK3326 handhelds (ArkOS, dArkOSRE).
# The game files of your own USA disc (SLUS-21678) go to ports/bt3/disc/ (see ports/bt3/README.txt).

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

GAMEDIR="/$directory/ports/bt3"
cd "$GAMEDIR" || exit 1
# (the program writes its own log.txt here; what the script and the system print goes to launcher.txt)
> "$GAMEDIR/launcher.txt" && exec > >(tee "$GAMEDIR/launcher.txt") 2>&1

export LD_LIBRARY_PATH="$GAMEDIR/libs:$LD_LIBRARY_PATH"
unset SDL_BLITTER_DISABLED

# the processor and the graphics chip at their full speed while the game runs (put back as they were afterwards)
GOVS=$(ls /sys/devices/system/cpu/cpufreq/policy*/scaling_governor /sys/class/devfreq/*/governor 2>/dev/null)
declare -A OLDGOV
for g in $GOVS; do
  OLDGOV[$g]=$(cat "$g" 2>/dev/null)
  $ESUDO sh -c "echo performance > $g" 2>/dev/null
done

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
