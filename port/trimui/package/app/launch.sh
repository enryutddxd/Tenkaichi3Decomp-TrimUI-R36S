#!/bin/sh
# Dragon Ball Z: Budokai Tenkaichi 3 as a TrimUI App (without PortMaster): the game in Data/ports/bt3.
export BT3_NO_PM=1
S="/mnt/SDCARD/Roms/PORTS/Dragon Ball Z Budokai Tenkaichi 3.sh"
[ -f "$S" ] || S="$(cd "$(dirname "$0")/../.." && pwd)/Roms/PORTS/Dragon Ball Z Budokai Tenkaichi 3.sh"
exec sh "$S"
