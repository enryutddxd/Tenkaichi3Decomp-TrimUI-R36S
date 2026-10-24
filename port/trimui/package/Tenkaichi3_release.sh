#!/bin/sh
# Dragon Ball Z: Budokai Tenkaichi 3 (Tenkaichi3Decomp) for the TrimUI Smart Pro, stock system, through PortMaster.
# This script goes to Roms/PORTS/; the game to Data/ports/bt3/, with the files of your own USA disc (SLUS-21678)
# in Data/ports/bt3/disc/ (see Data/ports/bt3/README.txt).
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
exec > "$GAMEDIR/launcher.txt" 2>&1
echo "game folder: $GAMEDIR; PortMaster: $controlfolder (${CFW_NAME:-?})"

export LD_LIBRARY_PATH="$GAMEDIR/libs:/usr/trimui/lib:/usr/lib:$LD_LIBRARY_PATH"
export MALLOC_ARENA_MAX=1
# (bt3.txt bench=1: the driver's costs measured at start, before bt3.txt is read by the program)
grep -q '^bench=1' "$GAMEDIR/bt3.txt" 2>/dev/null && export R36_BENCH=1
export R36_PROF=0   # (no prof.txt: the test builds' measurements off)
unset SDL_BLITTER_DISABLED

# the processor at a fixed speed while the game runs (put back afterwards): the governor, and the lowest and
# highest speed it may pick both at 1608 MHz (the system's menu leaves the highest at 1.2 GHz; the chip goes to 2.0).
# BT3_CPU_KHZ=<kHz> for another (one of scaling_available_frequencies)
CF=/sys/devices/system/cpu/cpu0/cpufreq
OLDGOV=$(cat $CF/scaling_governor 2>/dev/null)
OLDMIN=$(cat $CF/scaling_min_freq 2>/dev/null)
OLDMAX=$(cat $CF/scaling_max_freq 2>/dev/null)
TOP=${BT3_CPU_KHZ:-1608000}
for c in /sys/devices/system/cpu/cpu*/cpufreq; do
  echo performance > "$c/scaling_governor" 2>/dev/null || $ESUDO sh -c "echo performance > $c/scaling_governor" 2>/dev/null
  if [ -n "$TOP" ]; then
    echo "$TOP" > "$c/scaling_max_freq" 2>/dev/null
    echo "$TOP" > "$c/scaling_min_freq" 2>/dev/null
  fi
done
echo "processor: top $TOP kHz; min $(cat $CF/scaling_min_freq 2>/dev/null) max $(cat $CF/scaling_max_freq 2>/dev/null) (were $OLDMIN $OLDMAX)"
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
echo "temperatures: $(cat /sys/class/thermal/thermal_zone*/temp 2>/dev/null | tr '\n' ' '); processor now $(cat $CF/scaling_cur_freq 2>/dev/null) kHz"
echo "bt3 ended: status $STATUS (137: stopped by the system, out of memory?; 139: crash; 135: bus error)"
free -m 2>/dev/null | head -2
if [ $STATUS -ge 128 ]; then
  dmesg 2>/dev/null | tail -40
fi

kill -9 $(pidof gptokeyb) 2>/dev/null
for c in /sys/devices/system/cpu/cpu*/cpufreq; do
  [ -n "$OLDMIN" ] && echo "$OLDMIN" > "$c/scaling_min_freq" 2>/dev/null
  [ -n "$OLDMAX" ] && echo "$OLDMAX" > "$c/scaling_max_freq" 2>/dev/null
  [ -n "$OLDGOV" ] && echo "$OLDGOV" > "$c/scaling_governor" 2>/dev/null
done
if type pm_finish >/dev/null 2>&1; then
  pm_finish
fi
exit 0
