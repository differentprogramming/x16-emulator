// Commander X16 Emulator
// Copyright (c) 2019 Michael Steil
// All rights reserved. License: 2-clause BSD

#ifndef _GLUE_H_
#define _GLUE_H_

#include <stdint.h>
#include <stdbool.h>
#include "65c02incpp.h"

extern emulate65c02 emulator;

///*
/*extern uint8_t emulator.a, emulator.x, emulator.y, emulator.s, emulator.p;
extern uint16_t emulator.pc;
extern uint8_t *emulator.memory;
*/
//extern uint8_t emulator.rom[];
//*/

//extern struct emulate65c02 *emulator;
/*
#define a emulator->a
#define x emulator->x
#define y emulator->y
#define sp emulator->s
#define pc emulator->pc
#define status emulator->p
#define RAM emulator->memory
#define ROM emulator->rom
//*/

#endif
