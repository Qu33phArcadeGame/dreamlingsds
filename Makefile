# Deepdreamlings DS - build with BlocksDS (https://blocksds.skylyrac.net)
#   make            -> dreamlings.nds
#   make pc         -> build/dreamlings_pc (headless test build, see tools/pc)
NAME          := dreamlings
GAME_TITLE    := Deepdreamlings DS
GAME_SUBTITLE := Real neural dreaming
GAME_AUTHOR   := Coby
GAME_ICON     := icon.bmp
SOURCEDIRS    := source

ifeq ($(MAKECMDGOALS),pc)
pc:
	mkdir -p build
	cc -O2 -DPC_BUILD -Isource source/*.c -o build/dreamlings_pc
else
BLOCKSDS      ?= /opt/blocksds/core
include $(BLOCKSDS)/sys/default_makefiles/rom_arm9/Makefile
endif
