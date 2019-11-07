#include "65c02incpp.h"
extern emulate65c02 emulator;
#include "debugger.h"

/*
uint8_t
read6502(uint16_t address) {
	return emulator.read6502(address);
}
void
write6502(uint16_t address, uint8_t value)
{
	emulator.write6502(address, value);
}
*/
//6502 defines
#define UNDOCUMENTED //when this is defined, undocumented opcodes are handled.
					 //otherwise, they're simply treated as NOPs.

//#define NES_CPU      //when this is defined, the binary-coded decimal (BCD)
					 //status flag is not honored by ADC and SBC. the 2A03
					 //CPU in the Nintendo Entertainment System does not
					 //support BCD operation.

#define FLAG_CARRY     0x01
#define FLAG_ZERO      0x02
#define FLAG_INTERRUPT 0x04
#define FLAG_DECIMAL   0x08
#define FLAG_BREAK     0x10
#define FLAG_CONSTANT  0x20
#define FLAG_OVERFLOW  0x40
#define FLAG_SIGN      0x80

#define BASE_STACK     0x100

//6502 CPU registers
static uint16_t pc;
static uint8_t sp, a, x, y, status;


//helper variables
static uint32_t instructions = 0; //keep track of total instructions executed
static uint32_t clockticks6502 = 0, clockgoal6502 = 0;
static uint16_t oldpc, ea, reladdr, value, result;
static uint8_t opcode, oldstatus;

static uint8_t penaltyop, penaltyaddr;

//rewrite these
/*

						Extracted from original single ke6502.c file

*/

#define saveaccum(n) a = (uint8_t)((n) & 0x00FF)


//flag modifier macros
#define setcarry() status |= FLAG_CARRY
#define clearcarry() status &= (~FLAG_CARRY)
#define setzero() status |= FLAG_ZERO
#define clearzero() status &= (~FLAG_ZERO)
#define setinterrupt() status |= FLAG_INTERRUPT
#define clearinterrupt() status &= (~FLAG_INTERRUPT)
#define setdecimal() status |= FLAG_DECIMAL
#define cleardecimal() status &= (~FLAG_DECIMAL)
#define setoverflow() status |= FLAG_OVERFLOW
#define clearoverflow() status &= (~FLAG_OVERFLOW)
#define setsign() status |= FLAG_SIGN
#define clearsign() status &= (~FLAG_SIGN)


//flag calculation macros
#define zerocalc(n) {\
    if ((n) & 0x00FF) clearzero();\
        else setzero();\
}

#define signcalc(n) {\
    if ((n) & 0x0080) setsign();\
        else clearsign();\
}

#define carrycalc(n) {\
    if ((n) & 0xFF00) setcarry();\
        else clearcarry();\
}

#define overflowcalc(n, m, o) { /* n = result, m = accumulator, o = memory */ \
    if (((n) ^ (uint16_t)(m)) & ((n) ^ (o)) & 0x0080) setoverflow();\
        else clearoverflow();\
}

//a few general functions used by various other functions
void push16(uint16_t pushval) {
	emulator.push16(pushval);
	sp = emulator.s;
//	write6502(BASE_STACK + sp, (pushval >> 8) & 0xFF);
//	write6502(BASE_STACK + ((sp - 1) & 0xFF), pushval & 0xFF);
//	sp -= 2;
}

void push8(uint8_t pushval) {
	emulator.push8(pushval);
	sp = emulator.s;
//	write6502(BASE_STACK + sp--, pushval);
}

uint16_t pull16() {
	uint16_t r = emulator.pop_word();
	sp = emulator.s;
	return r;
//	uint16_t temp16;
//	temp16 = read6502(BASE_STACK + ((sp + 1) & 0xFF)) | ((uint16_t)read6502(BASE_STACK + ((sp + 2) & 0xFF)) << 8);
//	sp += 2;
//	return(temp16);
}

uint8_t pull8() {
	uint8_t r = emulator.pop_byte();
	sp = emulator.s;
	return r;
	//	return (read6502(BASE_STACK + ++sp));
}

//void reset6502() {
//	pc = (uint16_t)read6502(0xFFFC) | ((uint16_t)read6502(0xFFFD) << 8);
//	a = 0;
//	x = 0;
//	y = 0;
//	sp = 0xFD;
//	status |= FLAG_CONSTANT;
//}


//**************************************

/*

						Extracted from original single fake6502.c file

*/

//
//          65C02 changes.
//
//          ind         absolute indirect
//
//                      A 6502 has a bug whereby if you jmp ($12FF) it reads the address from
//                      $12FF and $1200. This has been fixed in the 65C02. 
//                      
static void imp() { //implied
}

static void acc() { //accumulator
}

static void imm() { //immediate
	ea = pc++;
}

static void zp() { //zero-page
	ea = (uint16_t)emulator.read6502((uint16_t)pc++);
}

static void zpx() { //zero-page,X
	ea = ((uint16_t)emulator.read6502((uint16_t)pc++) + (uint16_t)x) & 0xFF; //zero-page wraparound
}

static void zpy() { //zero-page,Y
	ea = ((uint16_t)emulator.read6502((uint16_t)pc++) + (uint16_t)y) & 0xFF; //zero-page wraparound
}

static void rel() { //relative for branch ops (8-bit immediate value, sign-extended)
	reladdr = (uint16_t)emulator.read6502(pc++);
	if (reladdr & 0x80) reladdr |= 0xFF00;
}

static void abso() { //absolute
	ea = (uint16_t)emulator.read6502(pc) | ((uint16_t)emulator.read6502(pc + 1) << 8);
	pc += 2;
}

static void absx() { //absolute,X
	uint16_t startpage;
	ea = ((uint16_t)emulator.read6502(pc) | ((uint16_t)emulator.read6502(pc + 1) << 8));
	startpage = ea & 0xFF00;
	ea += (uint16_t)x;

	if (startpage != (ea & 0xFF00)) { //one cycle penlty for page-crossing on some opcodes
		penaltyaddr = 1;
	}

	pc += 2;
}

static void absy() { //absolute,Y
	uint16_t startpage;
	ea = ((uint16_t)emulator.read6502(pc) | ((uint16_t)emulator.read6502(pc + 1) << 8));
	startpage = ea & 0xFF00;
	ea += (uint16_t)y;

	if (startpage != (ea & 0xFF00)) { //one cycle penlty for page-crossing on some opcodes
		penaltyaddr = 1;
	}

	pc += 2;
}

static void ind() { //indirect
	uint16_t eahelp, eahelp2;
	eahelp = (uint16_t)emulator.read6502(pc) | (uint16_t)((uint16_t)emulator.read6502(pc + 1) << 8);
	//
	//      The 6502 page boundary wraparound bug does not occur on a 65C02.
	//
	//eahelp2 = (eahelp & 0xFF00) | ((eahelp + 1) & 0x00FF); //replicate 6502 page-boundary wraparound bug
	eahelp2 = (eahelp + 1) & 0xFFFF;
	ea = (uint16_t)emulator.read6502(eahelp) | ((uint16_t)emulator.read6502(eahelp2) << 8);
	pc += 2;
}

static void indx() { // (indirect,X)
	uint16_t eahelp;
	eahelp = (uint16_t)(((uint16_t)emulator.read6502(pc++) + (uint16_t)x) & 0xFF); //zero-page wraparound for table pointer
	ea = (uint16_t)emulator.read6502(eahelp & 0x00FF) | ((uint16_t)emulator.read6502((eahelp + 1) & 0x00FF) << 8);
}

static void indy() { // (indirect),Y
	uint16_t eahelp, eahelp2, startpage;
	eahelp = (uint16_t)emulator.read6502(pc++);
	eahelp2 = (eahelp & 0xFF00) | ((eahelp + 1) & 0x00FF); //zero-page wraparound
	ea = (uint16_t)emulator.read6502(eahelp) | ((uint16_t)emulator.read6502(eahelp2) << 8);
	startpage = ea & 0xFF00;
	ea += (uint16_t)y;

	if (startpage != (ea & 0xFF00)) { //one cycle penlty for page-crossing on some opcodes
		penaltyaddr = 1;
	}
}

#ifdef _MSC_VER
#define UNUSED 
#else
#define UNUSED __attribute__((unused))
#endif

extern void(*addrtable[256])();
extern void(*optable[256])();

static uint16_t getvalue() {
	if (addrtable[opcode] == acc) return((uint16_t)a);
	else return((uint16_t)emulator.read6502(ea));
}

UNUSED static uint16_t getvalue16() {
	return((uint16_t)emulator.read6502(ea) | ((uint16_t)emulator.read6502(ea + 1) << 8));
}

static void putvalue(uint16_t saveval) {
	if (addrtable[opcode] == acc) a = (uint8_t)(saveval & 0x00FF);
	else emulator.write6502(ea, (saveval & 0x00FF));
}

//externally supplied functions


/*

						Extracted from original single fake6502.c file

*/
//
//          65C02 changes.
//
//          BRK                 now clears D
//          ADC/SBC             set N and Z in decimal mode. They also set V, but this is
//                              essentially meaningless so this has not been implemented.
//
//
//
//          instruction handler functions
//

static void adc() {
	penaltyop = 1;
#ifndef NES_CPU
	if (status & FLAG_DECIMAL) {
		uint16_t tmp, tmp2;
		value = getvalue();
		tmp = ((uint16_t)a & 0x0F) + (value & 0x0F) + (uint16_t)(status & FLAG_CARRY);
		tmp2 = ((uint16_t)a & 0xF0) + (value & 0xF0);
		if (tmp > 0x09) {
			tmp2 += 0x10;
			tmp += 0x06;
		}
		if (tmp2 > 0x90) {
			tmp2 += 0x60;
		}
		if (tmp2 & 0xFF00) {
			setcarry();
		}
		else {
			clearcarry();
		}
		result = (tmp & 0x0F) | (tmp2 & 0xF0);

		zerocalc(result);                /* 65C02 change, Decimal Arithmetic sets NZV */
		signcalc(result);

		clockticks6502++;
	}
	else {
#endif
		value = getvalue();
		result = (uint16_t)a + value + (uint16_t)(status & FLAG_CARRY);

		carrycalc(result);
		zerocalc(result);
		overflowcalc(result, a, value);
		signcalc(result);
#ifndef NES_CPU
	}
#endif

	saveaccum(result);
}

//and is a keyword in Visual Studio
static void _and_() {
	penaltyop = 1;
	value = getvalue();
	result = (uint16_t)a & value;

	zerocalc(result);
	signcalc(result);

	saveaccum(result);
}

static void asl() {
	value = getvalue();
	result = value << 1;

	carrycalc(result);
	zerocalc(result);
	signcalc(result);

	putvalue(result);
}

static void bcc() {
	if ((status & FLAG_CARRY) == 0) {
		oldpc = pc;
		pc += reladdr;
		if ((oldpc & 0xFF00) != (pc & 0xFF00)) clockticks6502 += 2; //check if jump crossed a page boundary
		else clockticks6502++;
	}
}

static void bcs() {
	if ((status & FLAG_CARRY) == FLAG_CARRY) {
		oldpc = pc;
		pc += reladdr;
		if ((oldpc & 0xFF00) != (pc & 0xFF00)) clockticks6502 += 2; //check if jump crossed a page boundary
		else clockticks6502++;
	}
}

static void beq() {
	if ((status & FLAG_ZERO) == FLAG_ZERO) {
		oldpc = pc;
		pc += reladdr;
		if ((oldpc & 0xFF00) != (pc & 0xFF00)) clockticks6502 += 2; //check if jump crossed a page boundary
		else clockticks6502++;
	}
}

static void bit() {
	value = getvalue();
	result = (uint16_t)a & value;

	zerocalc(result);
	status = (status & 0x3F) | (uint8_t)(value & 0xC0);
}

static void bmi() {
	if ((status & FLAG_SIGN) == FLAG_SIGN) {
		oldpc = pc;
		pc += reladdr;
		if ((oldpc & 0xFF00) != (pc & 0xFF00)) clockticks6502 += 2; //check if jump crossed a page boundary
		else clockticks6502++;
	}
}

static void bne() {
	if ((status & FLAG_ZERO) == 0) {
		oldpc = pc;
		pc += reladdr;
		if ((oldpc & 0xFF00) != (pc & 0xFF00)) clockticks6502 += 2; //check if jump crossed a page boundary
		else clockticks6502++;
	}
}

static void bpl() {
	if ((status & FLAG_SIGN) == 0) {
		oldpc = pc;
		pc += reladdr;
		if ((oldpc & 0xFF00) != (pc & 0xFF00)) clockticks6502 += 2; //check if jump crossed a page boundary
		else clockticks6502++;
	}
}

static void brk() {
	pc++;


	push16(pc); //push next instruction address onto stack
	push8(status | FLAG_BREAK); //push CPU status to stack
	setinterrupt(); //set interrupt flag
	cleardecimal();       // clear decimal flag (65C02 change)
	pc = (uint16_t)emulator.read6502(0xFFFE) | ((uint16_t)emulator.read6502(0xFFFF) << 8);
}

static void bvc() {
	if ((status & FLAG_OVERFLOW) == 0) {
		oldpc = pc;
		pc += reladdr;
		if ((oldpc & 0xFF00) != (pc & 0xFF00)) clockticks6502 += 2; //check if jump crossed a page boundary
		else clockticks6502++;
	}
}

static void bvs() {
	if ((status & FLAG_OVERFLOW) == FLAG_OVERFLOW) {
		oldpc = pc;
		pc += reladdr;
		if ((oldpc & 0xFF00) != (pc & 0xFF00)) clockticks6502 += 2; //check if jump crossed a page boundary
		else clockticks6502++;
	}
}

static void clc() {
	clearcarry();
}

static void cld() {
	cleardecimal();
}

static void cli() {
	clearinterrupt();
}

static void clv() {
	clearoverflow();
}

static void cmp() {
	penaltyop = 1;
	value = getvalue();
	result = (uint16_t)a - value;

	if (a >= (uint8_t)(value & 0x00FF)) setcarry();
	else clearcarry();
	if (a == (uint8_t)(value & 0x00FF)) setzero();
	else clearzero();
	signcalc(result);
}

static void cpx() {
	value = getvalue();
	result = (uint16_t)x - value;

	if (x >= (uint8_t)(value & 0x00FF)) setcarry();
	else clearcarry();
	if (x == (uint8_t)(value & 0x00FF)) setzero();
	else clearzero();
	signcalc(result);
}

static void cpy() {
	value = getvalue();
	result = (uint16_t)y - value;

	if (y >= (uint8_t)(value & 0x00FF)) setcarry();
	else clearcarry();
	if (y == (uint8_t)(value & 0x00FF)) setzero();
	else clearzero();
	signcalc(result);
}

static void dec() {
	value = getvalue();
	result = value - 1;

	zerocalc(result);
	signcalc(result);

	putvalue(result);
}

static void dex() {
	x--;

	zerocalc(x);
	signcalc(x);
}

static void dey() {
	y--;

	zerocalc(y);
	signcalc(y);
}

static void eor() {
	penaltyop = 1;
	value = getvalue();
	result = (uint16_t)a ^ value;

	zerocalc(result);
	signcalc(result);

	saveaccum(result);
}

static void inc() {
	value = getvalue();
	result = value + 1;

	zerocalc(result);
	signcalc(result);

	putvalue(result);
}

static void inx() {
	x++;

	zerocalc(x);
	signcalc(x);
}

static void iny() {
	y++;

	zerocalc(y);
	signcalc(y);
}

static void jmp() {
	pc = ea;
}

static void jsr() {
	push16(pc - 1);
	pc = ea;
}

static void lda() {
	penaltyop = 1;
	value = getvalue();
	a = (uint8_t)(value & 0x00FF);

	zerocalc(a);
	signcalc(a);
}

static void ldx() {
	penaltyop = 1;
	value = getvalue();
	x = (uint8_t)(value & 0x00FF);

	zerocalc(x);
	signcalc(x);
}

static void ldy() {
	penaltyop = 1;
	value = getvalue();
	y = (uint8_t)(value & 0x00FF);

	zerocalc(y);
	signcalc(y);
}

static void lsr() {
	value = getvalue();
	result = value >> 1;

	if (value & 1) setcarry();
	else clearcarry();
	zerocalc(result);
	signcalc(result);

	putvalue(result);
}

static void nop() {
	switch (opcode) {
	case 0x1C:
	case 0x3C:
	case 0x5C:
	case 0x7C:
	case 0xDC:
	case 0xFC:
		penaltyop = 1;
		break;
	}
}

static void ora() {
	penaltyop = 1;
	value = getvalue();
	result = (uint16_t)a | value;

	zerocalc(result);
	signcalc(result);

	saveaccum(result);
}

static void pha() {
	push8(a);
}

static void php() {
	push8(status | FLAG_BREAK);
}

static void pla() {
	a = pull8();

	zerocalc(a);
	signcalc(a);
}

static void plp() {
	status = pull8() | FLAG_CONSTANT;
}

static void rol() {
	value = getvalue();
	result = (value << 1) | (status & FLAG_CARRY);

	carrycalc(result);
	zerocalc(result);
	signcalc(result);

	putvalue(result);
}

static void ror() {
	value = getvalue();
	result = (value >> 1) | ((status & FLAG_CARRY) << 7);

	if (value & 1) setcarry();
	else clearcarry();
	zerocalc(result);
	signcalc(result);

	putvalue(result);
}

static void rti() {
	status = pull8();
	value = pull16();
	pc = value;
}

static void rts() {
	value = pull16();
	pc = value + 1;
}

static void sbc() {
	penaltyop = 1;

#ifndef NES_CPU
	if (status & FLAG_DECIMAL) {
		value = getvalue();
		result = (uint16_t)a - (value & 0x0f) + (status & FLAG_CARRY) - 1;
		if ((result & 0x0f) > (a & 0x0f)) {
			result -= 6;
		}
		result -= (value & 0xf0);
		if ((result & 0xfff0) > ((uint16_t)a & 0xf0)) {
			result -= 0x60;
		}
		if (result <= (uint16_t)a) {
			setcarry();
		}
		else {
			clearcarry();
		}

		zerocalc(result);                /* 65C02 change, Decimal Arithmetic sets NZV */
		signcalc(result);

		clockticks6502++;
	}
	else {
#endif
		value = getvalue() ^ 0x00FF;
		result = (uint16_t)a + value + (uint16_t)(status & FLAG_CARRY);

		carrycalc(result);
		zerocalc(result);
		overflowcalc(result, a, value);
		signcalc(result);
#ifndef NES_CPU
	}
#endif

	saveaccum(result);
}

static void sec() {
	setcarry();
}

static void sed() {
	setdecimal();
}

static void sei() {
	setinterrupt();
}

static void sta() {
	putvalue(a);
}

static void stx() {
	putvalue(x);
}

static void sty() {
	putvalue(y);
}

static void tax() {
	x = a;

	zerocalc(x);
	signcalc(x);
}

static void tay() {
	y = a;

	zerocalc(y);
	signcalc(y);
}

static void tsx() {
	x = sp;

	zerocalc(x);
	signcalc(x);
}

static void txa() {
	a = x;

	zerocalc(a);
	signcalc(a);
}

static void txs() {
	sp = x;
}

static void tya() {
	a = y;

	zerocalc(a);
	signcalc(a);
}
// *******************************************************************************************
// *******************************************************************************************
//
//		File:		65C02.H
//		Date:		3rd September 2019
//		Purpose:	Additional functions for new 65C02 Opcodes.
//		Author:		Paul Robson (paul@robson.org.uk)
//
// *******************************************************************************************
// *******************************************************************************************

// *******************************************************************************************
//
//					Indirect without indexation.  (copied from indy)
//
// *******************************************************************************************

static void ind0() {
	uint16_t eahelp, eahelp2;
	eahelp = (uint16_t)emulator.read6502(pc++);
	eahelp2 = (eahelp & 0xFF00) | ((eahelp + 1) & 0x00FF); //zero-page wraparound
	ea = (uint16_t)emulator.read6502(eahelp) | ((uint16_t)emulator.read6502(eahelp2) << 8);
}


// *******************************************************************************************
//
//						(Absolute,Indexed) address mode for JMP
//
// *******************************************************************************************

static void ainx() { 		// absolute indexed branch
	uint16_t eahelp, eahelp2;
	eahelp = (uint16_t)emulator.read6502(pc) | (uint16_t)((uint16_t)emulator.read6502(pc + 1) << 8);
	eahelp = (eahelp + (uint16_t)x) & 0xFFFF;
	eahelp2 = (eahelp & 0xFF00) | ((eahelp + 1) & 0x00FF); //replicate 6502 page-boundary wraparound bug
	ea = (uint16_t)emulator.read6502(eahelp) | ((uint16_t)emulator.read6502(eahelp2) << 8);
	pc += 2;
}

// *******************************************************************************************
//
//								Store zero to memory.
//
// *******************************************************************************************

static void stz() {
	putvalue(0);
}

// *******************************************************************************************
//
//								Unconditional Branch
//
// *******************************************************************************************

static void bra() {
	oldpc = pc;
	pc += reladdr;
	if ((oldpc & 0xFF00) != (pc & 0xFF00)) clockticks6502 += 2; //check if jump crossed a page boundary
	else clockticks6502++;
}

// *******************************************************************************************
//
//									Push/Pull X and Y
//
// *******************************************************************************************

static void phx() {
	push8(x);
}

static void plx() {
	x = pull8();

	zerocalc(x);
	signcalc(x);
}

static void phy() {
	push8(y);
}

static void ply() {
	y = pull8();

	zerocalc(y);
	signcalc(y);
}

// *******************************************************************************************
//
//								TRB & TSB - Test and Change bits 
//
// *******************************************************************************************

static void tsb() {
	value = getvalue(); 							// Read memory
	result = (uint16_t)a & value;  					// calculate A & memory
	zerocalc(result); 								// Set Z flag from this.
	result = value | a; 							// Write back value read, A bits are set.
	putvalue(result);
}

static void trb() {
	value = getvalue(); 							// Read memory
	result = (uint16_t)a & value;  					// calculate A & memory
	zerocalc(result); 								// Set Z flag from this.
	result = value & (a ^ 0xFF); 					// Write back value read, A bits are clear.
	putvalue(result);
}

// *******************************************************************************************
//
//                                     Invoke Debugger
//
// *******************************************************************************************

static void dbg() {
	DEBUGBreakToDebugger();                          // Invoke debugger.
}

void (*addrtable[256])() = {
/*        |  0  |  1  |  2  |  3  |  4  |  5  |  6  |  7  |  8  |  9  |  A  |  B  |  C  |  D  |  E  |  F  |     */
/* 0 */     imp, indx,  imp,  imp,   zp,   zp,   zp,  imp,  imp,  imm,  acc,  imp, abso, abso, abso,  imp, /* 0 */
/* 1 */     rel, indy, ind0,  imp,   zp,  zpx,  zpx,  imp,  imp, absy,  acc,  imp, abso, absx, absx,  imp, /* 1 */
/* 2 */    abso, indx,  imp,  imp,   zp,   zp,   zp,  imp,  imp,  imm,  acc,  imp, abso, abso, abso,  imp, /* 2 */
/* 3 */     rel, indy, ind0,  imp,  zpx,  zpx,  zpx,  imp,  imp, absy,  acc,  imp, absx, absx, absx,  imp, /* 3 */
/* 4 */     imp, indx,  imp,  imp,  imp,   zp,   zp,  imp,  imp,  imm,  acc,  imp, abso, abso, abso,  imp, /* 4 */
/* 5 */     rel, indy, ind0,  imp,  imp,  zpx,  zpx,  imp,  imp, absy,  imp,  imp,  imp, absx, absx,  imp, /* 5 */
/* 6 */     imp, indx,  imp,  imp,   zp,   zp,   zp,  imp,  imp,  imm,  acc,  imp,  ind, abso, abso,  imp, /* 6 */
/* 7 */     rel, indy, ind0,  imp,  zpx,  zpx,  zpx,  imp,  imp, absy,  imp,  imp, ainx, absx, absx,  imp, /* 7 */
/* 8 */     rel, indx,  imp,  imp,   zp,   zp,   zp,  imp,  imp,  imm,  imp,  imp, abso, abso, abso,  imp, /* 8 */
/* 9 */     rel, indy, ind0,  imp,  zpx,  zpx,  zpy,  imp,  imp, absy,  imp,  imp, abso, absx, absx,  imp, /* 9 */
/* A */     imm, indx,  imm,  imp,   zp,   zp,   zp,  imp,  imp,  imm,  imp,  imp, abso, abso, abso,  imp, /* A */
/* B */     rel, indy, ind0,  imp,  zpx,  zpx,  zpy,  imp,  imp, absy,  imp,  imp, absx, absx, absy,  imp, /* B */
/* C */     imm, indx,  imp,  imp,   zp,   zp,   zp,  imp,  imp,  imm,  imp,  imp, abso, abso, abso,  imp, /* C */
/* D */     rel, indy, ind0,  imp,  imp,  zpx,  zpx,  imp,  imp, absy,  imp,  imp,  imp, absx, absx,  imp, /* D */
/* E */     imm, indx,  imp,  imp,   zp,   zp,   zp,  imp,  imp,  imm,  imp,  imp, abso, abso, abso,  imp, /* E */
/* F */     rel, indy, ind0,  imp,  imp,  zpx,  zpx,  imp,  imp, absy,  imp,  imp,  imp, absx, absx,  imp  /* F */
};

void (*optable[256])() = {
/*        |  0  |  1  |  2  |  3  |  4  |  5  |  6  |  7  |  8  |  9  |  A  |  B  |  C  |  D  |  E  |  F  |     */
/* 0 */      brk,  ora,  nop,  nop,  tsb,  ora,  asl,  nop,  php,  ora,  asl,  nop,  tsb,  ora,  asl,  nop, /* 0 */
/* 1 */      bpl,  ora,  ora,  nop,  trb,  ora,  asl,  nop,  clc,  ora,  inc,  nop,  trb,  ora,  asl,  nop, /* 1 */
/* 2 */      jsr,  _and_,  nop,  nop,  bit,  _and_,  rol,  nop,  plp,  _and_,  rol,  nop,  bit,  _and_,  rol,  nop, /* 2 */
/* 3 */      bmi,  _and_,  _and_,  nop,  bit,  _and_,  rol,  nop,  sec,  _and_,  dec,  nop,  bit,  _and_,  rol,  nop, /* 3 */
/* 4 */      rti,  eor,  nop,  nop,  nop,  eor,  lsr,  nop,  pha,  eor,  lsr,  nop,  jmp,  eor,  lsr,  nop, /* 4 */
/* 5 */      bvc,  eor,  eor,  nop,  nop,  eor,  lsr,  nop,  cli,  eor,  phy,  nop,  nop,  eor,  lsr,  nop, /* 5 */
/* 6 */      rts,  adc,  nop,  nop,  stz,  adc,  ror,  nop,  pla,  adc,  ror,  nop,  jmp,  adc,  ror,  nop, /* 6 */
/* 7 */      bvs,  adc,  adc,  nop,  stz,  adc,  ror,  nop,  sei,  adc,  ply,  nop,  jmp,  adc,  ror,  nop, /* 7 */
/* 8 */      bra,  sta,  nop,  nop,  sty,  sta,  stx,  nop,  dey,  bit,  txa,  nop,  sty,  sta,  stx,  nop, /* 8 */
/* 9 */      bcc,  sta,  sta,  nop,  sty,  sta,  stx,  nop,  tya,  sta,  txs,  nop,  stz,  sta,  stz,  nop, /* 9 */
/* A */      ldy,  lda,  ldx,  nop,  ldy,  lda,  ldx,  nop,  tay,  lda,  tax,  nop,  ldy,  lda,  ldx,  nop, /* A */
/* B */      bcs,  lda,  lda,  nop,  ldy,  lda,  ldx,  nop,  clv,  lda,  tsx,  nop,  ldy,  lda,  ldx,  nop, /* B */
/* C */      cpy,  cmp,  nop,  nop,  cpy,  cmp,  dec,  nop,  iny,  cmp,  dex,  nop,  cpy,  cmp,  dec,  nop, /* C */
/* D */      bne,  cmp,  cmp,  nop,  nop,  cmp,  dec,  nop,  cld,  cmp,  phx,  nop,  nop,  cmp,  dec,  nop, /* D */
/* E */      cpx,  sbc,  nop,  nop,  cpx,  sbc,  inc,  nop,  inx,  sbc,  nop,  nop,  cpx,  sbc,  inc,  nop, /* E */
/* F */      beq,  sbc,  sbc,  nop,  nop,  sbc,  inc,  nop,  sed,  sbc,  plx,  nop,  nop,  sbc,  inc,  dbg  /* F */
};

static const uint32_t ticktable[256] = {
/*        |  0  |  1  |  2  |  3  |  4  |  5  |  6  |  7  |  8  |  9  |  A  |  B  |  C  |  D  |  E  |  F  |     */
/* 0 */       7,    6,    2,    2,    5,    3,    5,    2,    3,    2,    2,    2,    6,    4,    6,    2, /* 0 */
/* 1 */       2,    5,    5,    2,    5,    4,    6,    2,    2,    4,    2,    2,    6,    4,    7,    2, /* 1 */
/* 2 */       6,    6,    2,    2,    3,    3,    5,    2,    4,    2,    2,    2,    4,    4,    6,    2, /* 2 */
/* 3 */       2,    5,    5,    2,    4,    4,    6,    2,    2,    4,    2,    2,    4,    4,    7,    2, /* 3 */
/* 4 */       6,    6,    2,    2,    2,    3,    5,    2,    3,    2,    2,    2,    3,    4,    6,    2, /* 4 */
/* 5 */       2,    5,    5,    2,    2,    4,    6,    2,    2,    4,    3,    2,    2,    4,    7,    2, /* 5 */
/* 6 */       6,    6,    2,    2,    3,    3,    5,    2,    4,    2,    2,    2,    5,    4,    6,    2, /* 6 */
/* 7 */       2,    5,    5,    2,    4,    4,    6,    2,    2,    4,    4,    2,    6,    4,    7,    2, /* 7 */
/* 8 */       3,    6,    2,    2,    3,    3,    3,    2,    2,    2,    2,    2,    4,    4,    4,    2, /* 8 */
/* 9 */       2,    6,    5,    2,    4,    4,    4,    2,    2,    5,    2,    2,    4,    5,    5,    2, /* 9 */
/* A */       2,    6,    2,    2,    3,    3,    3,    2,    2,    2,    2,    2,    4,    4,    4,    2, /* A */
/* B */       2,    5,    5,    2,    4,    4,    4,    2,    2,    4,    2,    2,    4,    4,    4,    2, /* B */
/* C */       2,    6,    2,    2,    3,    3,    5,    2,    2,    2,    2,    2,    4,    4,    6,    2, /* C */
/* D */       2,    5,    5,    2,    2,    4,    6,    2,    2,    4,    3,    2,    2,    4,    7,    2, /* D */
/* E */       2,    6,    2,    2,    3,    3,    5,    2,    2,    2,    2,    2,    4,    4,    6,    2, /* E */
/* F */       2,    5,    5,    2,    2,    4,    6,    2,    2,    4,    4,    2,    2,    4,    7,    1  /* F */
};

void fake_emulator(uint8_t op)
{
	pc = emulator.pc;
	sp = emulator.s;
	a = emulator.a;
	x = emulator.x;
	y = emulator.y;
	status = emulator.p;
	clockticks6502 = 0;
	opcode = op;
	++pc;
	status |= FLAG_CONSTANT;

	penaltyop = 0;
	penaltyaddr = 0;

	(*addrtable[opcode])();
	(*optable[opcode])();
	clockticks6502 += ticktable[opcode];
	if (penaltyop && penaltyaddr) clockticks6502++;
	emulator.time += clockticks6502;
	emulator.pc = pc;
	emulator.s = sp;
	emulator.a = a;
	emulator.x = x;
	emulator.y = y;
	emulator.p = status;
}
