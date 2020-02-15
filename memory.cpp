// Commander X16 Emulator
// Copyright (c) 2019 Michael Steil
// All rights reserved. License: 2-clause BSD

#include <sys/types.h>
#include <string.h>
#include <unistd.h>
#include "glue.h"
#include "via.h"
#include "memory.h"
#include "video.h"
#ifdef WITH_YM2151
#include "ym2151.h"
#endif
#include "ps2.h"

//#define DEVICE_EMULATOR (0x9fb0)

//void
//memory_init()//global
//{
//	emulator.memory =(uint8_t *) malloc(RAM_SIZE);
//}

static uint8_t
effective_ram_bank()
{
	return emulator.effective_ram_bank();
}

//
// interface for fake6502
//
// if debugOn then reads memory only for debugger; no I/O, no side effects whatsoever

uint8_t
read6502(uint16_t address) {
	return emulator.read6502(address);
}

//if "debugOn" then it uses "bank" instead of the current bank.
uint8_t
real_read6502(uint16_t address, bool debugOn, uint8_t bank)
{
	return emulator.real_read6502(address, debugOn, bank);
}

void
write6502(uint16_t address, uint8_t value)
{
	emulator.write6502(address, value);
}

//
// saves the memory content into a file
// - not in use

void
memory_save(FILE *f, bool dump_ram, bool dump_bank)
{
	emulator.memory_save(f, dump_ram, dump_bank);
}


///
///
///

void
memory_set_ram_bank(uint8_t bank)//global
{
	emulator.memory_set_ram_bank(bank);
}

uint8_t
memory_get_ram_bank()//global
{
	return emulator.ram_bank;
}

void
memory_set_rom_bank(uint8_t bank)//global
{
	emulator.memory_set_rom_bank(bank);
}

uint8_t
memory_get_rom_bank()//global
{
	return emulator.rom_bank;
}

// Control the GIF recorder
void
emu_recorder_set(gif_recorder_command_t command)
{
	emulator.emu_recorder_set(command);
}

//
// read/write emulator state (feature flags)
//
// 0: debugger_enabled
// 1: log_video
// 2: log_keyboard
// 3: echo_mode
// 4: save_on_exit
// 5: record_gif
// POKE $9FB3,1:PRINT"ECHO MODE IS ON":POKE $9FB3,0
void
emu_write(uint8_t reg, uint8_t value)
{
	emulator.emu_write(reg, value);
}

uint8_t
emu_read(uint8_t reg)
{
	return emulator.emu_read(reg);
}
