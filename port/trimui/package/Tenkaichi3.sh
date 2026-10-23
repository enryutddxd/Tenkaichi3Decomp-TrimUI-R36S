#!/bin/sh
# Dragon Ball Z: Budokai Tenkaichi 3 (Tenkaichi3Decomp) for the TrimUI Smart Pro, stock system, through PortMaster.
# This script goes to Roms/PORTS/; the game to Data/ports/bt3/, with the files of your own USA disc (SLUS-21678)
# in Data/ports/bt3/disc/ (see Data/ports/bt3/README.txt).
# every step of this script into bt3_trace.txt at the card's root (to see where it stops)
TRACE=/mnt/SDCARD/bt3_trace.txt
[ -d /mnt/SDCARD ] || TRACE="$(dirname "$0")/bt3_trace.txt"
echo "=== start: $0 (shell: ${BASH_VERSION:-sh}, user $(id -u 2>/dev/null))" >> "$TRACE"
exec 2>>"$TRACE"
set -x

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}
if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
elif [ -d "/mnt/SDCARD/Apps/PortMaster/PortMaster/" ]; then
  controlfolder="/mnt/SDCARD/Apps/PortMaster/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

GPTOKEYB=":"
ESUDO=""
# BT3_NO_PM=1 (the App launcher): without PortMaster's scripts
if [ -z "$BT3_NO_PM" ] && [ -f "$controlfolder/control.txt" ]; then
  . "$controlfolder/control.txt"
  [ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && . "${controlfolder}/mod_${CFW_NAME}.txt"
  type get_controls >/dev/null 2>&1 && get_controls
  [ -n "$sdl_controllerconfig" ] && export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
fi

# the game's folder: PortMaster's ($directory/ports/bt3), else the TrimUI's Data/ports/bt3
GAMEDIR=""
for d in "/$directory/ports/bt3" "/mnt/SDCARD/Data/ports/bt3" "$(dirname "$0")/../../Data/ports/bt3" "$(dirname "$0")/bt3"; do
  if [ -f "$d/bt3" ]; then
    GAMEDIR="$d"
    break
  fi
done
if [ -z "$GAMEDIR" ]; then
  echo "bt3: the game's folder (Data/ports/bt3) was not found" > /tmp/bt3_launcher.txt
  exit 1
fi
cd "$GAMEDIR" || exit 1
GAMEDIR="$(pwd)"
# (the program writes its own log.txt here; what the script and the system print goes to launcher.txt)
set +x
exec > "$GAMEDIR/launcher.txt" 2>&1
echo "game folder: $GAMEDIR; PortMaster: $controlfolder (${CFW_NAME:-?})"

export LD_LIBRARY_PATH="$GAMEDIR/libs:/usr/trimui/lib:/usr/lib:$LD_LIBRARY_PATH"
export MALLOC_ARENA_MAX=1
unset SDL_BLITTER_DISABLED

# the processor at its full speed while the game runs (put back afterwards)
OLDGOV=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null)
for g in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
  echo performance > "$g" 2>/dev/null || $ESUDO sh -c "echo performance > $g" 2>/dev/null
done
for g in /sys/class/devfreq/*/governor; do
  [ -f "$g" ] && echo performance > "$g" 2>/dev/null
done
echo "governor: $OLDGOV -> $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null), now $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq 2>/dev/null) kHz"

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
  dmesg 2>/dev/null | tail -40
fi

kill -9 $(pidof gptokeyb) 2>/dev/null
for g in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
  [ -n "$OLDGOV" ] && echo "$OLDGOV" > "$g" 2>/dev/null
done
if type pm_finish >/dev/null 2>&1; then
  pm_finish
fi
exit 0
