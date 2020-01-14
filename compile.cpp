#include "glue.h"
#include <list>

void Label::here(emulate65c02 *emulate, int offset) { set_target(emulate, emulate->compile_point + offset); }
void Label::here(int offset) { set_target(&emulator, emulator.compile_point + offset); }

#define TESTRNG

static uint8_t header[13] = { //0x01,0x08,
0x0C,0x08,0x0A,0x00,0x9E,0x20,0x32,0x30,0x36,0x32,0x00,0x00,0x00 };

#ifndef TESTRNG

const int default_temp = 2;
const int default_ret = 6;
const int ret_temp = 8;
/*
 Zero page
 $00-$7F available to the user
($02-$52 are used if using BASIC graphics commands)
$80-$A3 used by KERNAL and DOS
$A4-$A8 reserved for KERNAL/DOS/BASIC
$A9-$FF used by BASIC

$0000-$9EFF	Fixed RAM (40 KB minus 256 bytes)
$9F00-$9FFF	I/O Area (256 bytes)
$A000-$BFFF	Banked RAM (8 KB window into one of 256 banks for a total of 2 MB)
$C000-$FFFF	Banked ROM (16 KB window into one of 8 banks for a total of 128 KB)
The RAM bank (0-255) defaults to 255, and the ROM bank (0-7) defaults to 7 on RESET.
The RAM bank can be configured through VIA#1 PA0-7 ($9F61), and the ROM bank through
VIA#1 PB0-2 ($9F60). See section "I/O Programming" for more information.

0	KERNAL	character sets (uploaded into VRAM), MONITOR, KERNAL
1	KEYBD	Keyboard layout tables
2	CBDOS	The computer-based CBM-DOS for FAT32 SD cards
3	GEOS	GEOS KERNAL
4	BASIC	BASIC interpreter
5-7	–	[Currently unused]

$0000-$007F	User zero page
$0080-$00FF	KERNAL and BASIC zero page variables
$0100-$01FF	CPU stack
$0200-$07FF	KERNAL and BASIC variables
$0800-$9EFF	BASIC program/variables; available to the user

0-254	Available to the user
255[^2]	DOS buffers and variables

$9F00-$9F1F	Reserved for audio controller
$9F20-$9F3F	VERA video controller
$9F40-$9F5F	Reserved
$9F60-$9F6F	VIA I/O controller #1
$9F70-$9F7F	VIA I/O controller #2
$9F80-$9F9F	Real time clock
$9FA0-$9FBF	Future Expansion
$9FC0-$9FDF	Future Expansion
$9FE0-$9FFF	Future Expansion

hmm
how about:
self
9a 9b 9c
interpreter code, source, dest temps
9d 9e 9f a0 a1 a2 a3 a4 a5
local area
a6-5e
5f-7f 33 bytes - used as return values and scratch

a6-5e temp variable area, x as base pointer - 185 locations indexed as ($a6,x)

x+a+71>255

txa
clc
adc #variables_needed1
cmp #184
bcs routine
jsr spill_temp
.routine 
	tax
...
txa
sec
sbc #variables_needed1
//tax
rts
[on return side]
//txa
sec
sbc #param_bytes
//tax
bcs cont0
jsr unspill_temp
.cont0
cmp #variables_needed0
bcs cont
.unspill
jsr unspill_temp
.cont tax
//

direct vs jsr vs interp
Since jump table is max 128 jumps, that's a max of 128 byte codes
 */

#define E emulator.
enum REGISTERS { REG_A, REG_X, REG_Y };

Label	lshift1, lshift2, lshift3, lshift4, lshift5, lshift6, lshift7,
		rshift1, rshift2, rshift3, rshift4, rshift5, rshift6, rshift7,
		ashift1, ashift2, ashift3, ashift4, ashift5, ashift6, ashift7,
		lshift_low,lshift_high,rshift_low,rshift_high,ashift_low,ashift_high,lshift_rev_low, lshift_rev_high,
		squarelow, squarehigh;


//this is a tagged, wide stack for temps, passing values to and from routines and for messages;
Label pass_type, pass_0, pass_1, pass_2, pass_3, pass_4, pass_5, pass_6;

class var;
class do_compare;

typedef void (*compair_fn)(do_compare*, Label&);

class do_compare
{
public:
	static bool force_short;
	do_compare(int a1, int a2, compair_fn f) :
		address1((uint16_t)a1), address2((uint16_t)a2),literal(a2), fn(f) {}
	uint16_t address1;
	uint16_t address2;
	bool force_short;
	int literal;
	compair_fn fn;
	void then(Label &l, bool f=true) {
		force_short = f;
		(*fn)(this, l);
	}
};

//57 bytes free
const int WORKSPACE = 0xa9;
const int TEMPSTACK = WORKSPACE; //		0-1 temp stack index, 1 byte offset into top of zp
const int LOCALSTACK = TEMPSTACK + 1;//	2-4 Local Stack, ie the 24 bit address of deep bound locals
const int ROOTSTACK = LOCALSTACK + 2;//	5-7 ROOT stack, 24 bit pointer to stack that only holds deep bound root pointers
//const int RS = ROOTSTACK + 2;//return stack, 16 bit pointer
const int T1 = ROOTSTACK + 2;//			8-10 T1,T2,T3 three 3 byte address
const int T2 = T1 + 3;//				11-13
const int T3 = T2 + 3;//				14-16
const int A1 = T3 + 3;//				17-18 A1,A2,A2 three 2 byte address
const int A2 = A1 + 2;//				19-20
const int A3 = A2 + 2;//				21-22
const int F1 = A3 + 2;//				23-28 F1,F2 two fixed point registers
const int F2 = F1 + 6;//				29-34
const int G1 = F2 + 6;//				35-37 G1,G2 two three bytes signed registers
const int G2 = G1 + 3;//				38-40
const int S1 = G1 + 3;//				41-42 S1,S2 two signed 2 byte registers
const int S2 = S1 + 2;//				43-44
const int B1 = S2 + 2;//				45 B1,B2 two unsigned 1 byte registers
const int B2 = B1 + 1;//				46
const int N1 = B2 + 1;//				47-48 Any type consists of a type and a register alias .. in memory ANY = 5 bytes tagged float or type+data
const int N2 = N1 + 2;//				49-50
const int W1 = N2 + 2;//				51-52 Whether type for when conditional expressions get reified
const int W2 = W1 + 1;//				53-54
const int NURS = W2 + 1;//				55 nursery 2 bytes
const int FREE_BMEM = NURS + 2;//		56 Free banked memory list 63 bytes

const int RegisterAddress[NUM_REGISTERS] = {
	F1,F2,G1,G2,S1,S2,B1,B2,T1,T2,T3,A1,A2,A3,N1,N2,
};
enum REGISTERS {
	F1_REG,
	F2_REG,
	G1_REG,
	G2_REG,
	S1_REG,
	S2_REG,
	B1_REG,
	B2_REG,
	NUM_NUMERIC_REGISTERS,
	T1_REG = NUM_NUMERIC_REGISTERS,
	T2_REG,
	T3_REG,
	A1_REG,
	A2_REG,
	A3_REG,
	N1_REG,
	N2_REG,
	W1_REG,
	W2_REG,
	NUM_REGISTERS,
	NOT_IN_A_REGISTER = NUM_REGISTERS,
};

enum PrimitiveTypes {
	U1_TYPE,
	S2_TYPE,
	S3_TYPE,
	F6_TYPE,
	ADDR_TYPE,
	BADDR_TYPE,
	ANY_TYPE,
	TRUTH_TYPE,
	NUM_PRIM_TYPES,
};

const PrimitiveTypes RegType[NUM_REGISTERS] = {
	F6_TYPE,F6_TYPE,S3_TYPE,S3_TYPE,S2_TYPE,S2_TYPE,U1_TYPE,U1_TYPE,BADDR_TYPE,BADDR_TYPE,BADDR_TYPE,ADDR_TYPE,ADDR_TYPE,ADDR_TYPE,ANY_TYPE,ANY_TYPE,TRUTH_TYPE,TRUTH_TYPE
};

const REGISTERS FirstRegOfType[NUM_PRIM_TYPES] =
{
	F1_REG, G1_REG, S1_REG, B1_REG, T1_REG, A1_REG, N1_REG, W1_REG
};
const REGISTERS SecondRegOfType[NUM_PRIM_TYPES] =
{
	F2_REG, G2_REG, S2_REG, B2_REG, T2_REG, A2_REG, N2_REG, W2_REG
};

const REGISTERS ThirdRegOfType[NUM_PRIM_TYPES] =
{
	NOT_IN_A_REGISTER, NOT_IN_A_REGISTER, NOT_IN_A_REGISTER, NOT_IN_A_REGISTER, T3_REG, A3_REG, NOT_IN_A_REGISTER, NOT_IN_A_REGISTER,
};

const int RegisterAddress[NUM_REGISTERS] = {
	F1,F2,G1,G2,S1,S2,B1,B2,T1,T2,T3,A1,A2,A3,N1,N2,W1,W2
};
const bool TypeIsNum[NUM_PRIM_TYPES] =
{ true,true,true,true,false,false, false,false };
const bool TypeIsSigned[NUM_PRIM_TYPES]=
{ false, true, true, true, false, false, false,false };
const int TypeSize[NUM_PRIM_TYPES] = {
	1,2,3,6,2,3,7,1
};

//note top of stack is HIGHEST number
//and TOS never goes down, this is for keeping track of relative order

int virtualTOS; 

struct RegisterTracking {
	REGISTERS me;

};

/* Register allocation and spilling turns out to have an easy algorithm
 * 1) use the shuttle parser to turn the expression into reverse polish

 * OK THIS:
 * virtually run the RPN, BUT the values are the code that generated them
 * IE you have (6 (5 8 *) +) = the stack holds code.
 * EVERY TIME a biop sees a left side that's raw and a right side that isn't it swaps them
 * and swaps itself for a reverse op.
 * NOW generate from the code, and just do all the spills
 *
 * Oops STILL needs to keep track of where things are in the stack and backdate pushes to get them
 * into the right spot in the case of multiple register sets.
 *
 * f[a+b*c]+g[d+e*h]*i[j+k*l]
 * f (a (b c *) +)
 * -> (f ((b c *) a  r+) [])
 * -> (((b c *) a  r+) f r[]) g (d (e h *) +)
 * -> (((b c *) a  r+) f r[]) (g ((e h *) d r+) [])
 * -> (((b c *) a  r+) f r[]) (((e h *) d r+) g r[]) (((k l *) j  r+) i r[]) * +
 * b1 c2 *2 a1 +1 f2 r[]2 e1 push2 h2 *2 d1 +1 g2 r[]2 k1 push2 l2 *2 j1 +1 i2 r[]2 pop1 *2 pop1 +2
 * hmm
 * lower case are bytes so G[..]+i[..] is actualy where G is the register
 * that changes because that's the type to preserve
 * f[a+b*c]+G[D+E*H]*i[j+k*l]
 * b c * a + f r[] E H * D+ G R[] k l* j + i r[] * +
 * b1b c2b *2b a1b +ab E1s H2s *2s D1s +1s G2s R[]2s    
 */
int ValueID = 0;

enum ADDRESS_CLASS {
	ZP_AC,
	ABS_AC,
	BANKED_AC,//nop ops for banked, they're only loaded from and stored to
	TEMP_AC,
	IMMEDIATE_AC,
	NUM_OF_AC
};

//what if locals are 7 pages indexed by x or 7 big enough to hold any type and be 256 elements deep and every variable could be typed
//messages could be on this typed substrate and so could return values
//also every function could have 2 entries, typed and boxed

enum OPERATIONS {
	PLUS_OP,
	SUB_OP,
	RSUB_OP,
	BAND_OP,
	BOR_OP,
	BEOR_OP,
	SHIFTR_OP,
	RSHIFTR_OP,
	SHIFTL_OP,
	RSHIFTL_OP,
	LOAD_OP,
	STORE_OP,
	INDEXED_LOAD_OP,
	RINDEXED_LOAD_OP,
	INDEXED_STORE_OP,
	RINDEXED_STORE_OP,
	MUL_OP,
	DIV_OP,
	RDIV_OP,
	EQ_OP,
	NEQ_OP,
	LT_OP,
	GE_OP,
	GT_OP,
	LE_OP,
	LEAF_OP,
	AND_OP,
	OR_OP,
	NOT_OP,
	NEGATE_OP,
	COND_OP,
};

OPERATIONS ReverseOp[] = {
	PLUS_OP,
	RSUB_OP,
	SUB_OP,
	BAND_OP,
	BOR_OP,
	BEOR_OP,
	RSHIFTR_OP,
	SHIFTR_OP,
	RSHIFTL_OP,
	SHIFTL_OP,
	LOAD_OP,
	STORE_OP,
	RINDEXED_LOAD_OP,
	INDEXED_LOAD_OP,
	RINDEXED_STORE_OP,
	INDEXED_STORE_OP,
	MUL_OP,
	RDIV_OP,
	DIV_OP,
	NEQ_OP,
	EQ_OP,
	GE_OP,
	LT_OP,
	LE_OP,
	GT_OP,
	LEAF_OP,
	AND_OP,
	OR_OP,
	NOT_OP,
	NEGATE_OP,
	COND_OP,
};

int StackTop;

struct CompilerOpNode{
	OPERATIONS op;
	CompilerOpNode *left, *right;
	ADDRESS_CLASS ac; //only applies to LEAF nodes
	int stack_order;

};

struct ExecutionNode {

};
std::list<CompilerOpNode *> CompilerStack;


/*
arith sources
+ 64
- 64
>> << 8 (right side is byte, put a conversion first) 
& | ^ 192 can it be bitop+opcode -> 64
< > = (and >= <= !) 192 can it be cmpop+opcodes -> 64
* / 128
-> 64
456 ops x 20 = 9k  with opcodes factored out 
no registers
4 local+4 global types = 8
local only
4x4
+16 -16 << >> 4 & | ^ 16+opcode < > =16xopcode * / % 48 -> 32 (includes global)
= 148 x 40 = 6k
only registers w/ symmatry also slow ops don't happen IN register so 
+32 -64 >> << 32 * | ^ 32 < > = 64 * / % 16 -> 64+64 
368x20=7k
load/store arch is looking good because ops are simple.
if loads and stores are inline it will be fast enough

Locals are dynamic bound from main memory
GCed variables will be saved on a different stack
ANY variables will be saved on GC stack with type tag
temps are locals
return values are stored in a ZP stack
parameters are passed in a ZP stack too.
the locals stack is in banked memory, because this language doesn't have variable capture

 */
enum GENERATION_MODE
{
	NORMAL_GM,
	BANK_GM,
};

GENERATION_MODE generation_mode = NORMAL_GM;
#ifdef DEFINEOUT
class var
{
public:
	uint8_t address;
	uint8_t bank;//if ZPINDEXED_AC temp for index
	int bytes; //5 for float
	bool is_signed;

	explicit var(int add, int bytes, bool is_signed) :address(add&0xffff), bytes(bytes),is_signed(is_signed),bank(add>>16)
	{
	}

	var(const var &) = default;
	var(var &&) = default;

	var& operator =(const var&) = default;

	var&operator +=(const var& o);
	var&operator +=(int o);

	var&operator -=(const var& o);
	var&operator -=(int o);

	var&operator &=(const var& o);
	var&operator &=(int o);

	var&operator |=(const var& o);
	var&operator |=(int o);

	var&operator ^=(const var& o);
	var&operator ^=(int o);

	var&operator >>=(const var& o);
	var&operator >>=(int o);

	var&operator <<=(const var& o);
	var&operator <<=(int o);

	var indexLoad(const var& o, int b);//as register
	var indexLoad(int o, int b);////as register

	var& indirectStore(const var& o);
	var& indirectStore(int o, int b);

	var&operator =( var& o);
	var&operator =( int o);


	//off can chose register on a register type
	void cmp_imm(int val, int off) const;

	void r1ToA();
	void r1ToX();
	void r1ToY();
	void r2ToA();
	void r2ToX();
	void r2ToY();
	void r3ToA();
	void r3ToX();
	void r3ToY();

	var r1ToASafe();//not for 3 byte register types
	var r2ToASafe();//not for 3 byte register types

	void storeR1(int add);
	void storeR2(int add);
	void storeR3(int add);

	do_compare operator ==(var& o) const;
	do_compare operator !=(var& o) const;
	do_compare operator <=(var& o) const;
	do_compare operator <(var& o) const;
	do_compare operator >=(var& o) const;
	do_compare operator >(var& o) const;

	do_compare operator >(int o) const;
	do_compare operator >=(int o) const;
	do_compare operator <(int o) const;
	do_compare operator <=(int o) const;
	do_compare operator !=(int o) const;
	do_compare operator ==(int o) const;

	var&operator *=(const var& o);
	var&operator *=(int o);

	void compile_calltable();
};

class acc
{
public:
	REGISTERS reg;
	uint8_t myTemp;
	acc(REGISTERS r, uint8_t t=2) :reg(r), myTemp(t) {}
	acc(const acc &o) :reg(o.reg), myTemp(o.myTemp) {}
	void store_zp(int o)
	{
		if (reg == REG_A) {
			E sta_zp(o);
			return;
		}
		else if (reg == REG_X) {
			E stx_zp(o);
			return;
		}
		E sty_zp(o);
	}
	void cmp_imm(int o)
	{
		if (reg == REG_A) {
			E cmp_imm(o);
			return;
		}
		else if (reg == REG_X) {
			E cpx_imm(o);
			return;
		}
		E cpy_imm(o);
	}
	void bit_imm(int o)
	{
		if (reg == REG_A) {
			E bit_imm(o);
			return;
		}
		else if (reg == REG_X) {
			E txa();
			E bit_imm(o);
			return;
		}
		E tya();
		E bit_imm(o);
	}

	acc toAandPreserve()
	{
		if (reg == REG_A) {
			E tax();
			return acc(REG_X, myTemp);
		}
		else if (reg == REG_X) {
			E txa();
			return acc(REG_X, myTemp);
		}
		E tya();
		return acc(REG_Y, myTemp);
	}
	void toA()
	{
		if (reg == REG_A) {
			return;
		}
		else if (reg == REG_X) {
			E txa();
			return;
		}
		E tya();
	}
	void toX()
	{
		if (reg == REG_A) {
			E tax();
			return;
		}
		else if (reg == REG_X) {
			return;
		}
		E sty_zp(myTemp);
		E ldx_zp(myTemp);
	}
	void toY()
	{
		if (reg == REG_A) {
			E tay();
			return;
		}
		else if (reg == REG_X) {
			return;
		}
		E stx_zp(myTemp);
		E ldy_zp(myTemp);
	}
};

class sacc {
public:
	REGISTERS reg;
	uint8_t myTemp;
	sacc(REGISTERS r, uint8_t t=2) :reg(r), myTemp(t) {}
	sacc(const sacc &o) :reg(o.reg), myTemp(o.myTemp) {}
	void store_zp(int o)
	{
		if (reg == REG_A) {
			E sta_zp(o);
			return;
		}
		else if (reg == REG_X) {
			E stx_zp(o);
			return;
		}
		E sty_zp(o);
	}
	void cmp_imm(int o)
	{
		if (reg == REG_A) {
			E cmp_imm(o);
			return;
		}
		else if (reg == REG_X) {
			E cpx_imm(o);
			return;
		}
		E cpy_imm(o);
	}
	void bit_imm(int o)
	{
		if (reg == REG_A) {
			E bit_imm(o);
			return;
		}
		else if (reg == REG_X) {
			E txa();
			E bit_imm(o);
			return;
		}
		E tya();
		E bit_imm(o);
	}
	sacc toAandPreserve()
	{
		if (reg == REG_A) {
			E tax();
			return sacc(REG_X, myTemp);
		}
		else if (reg == REG_X) {
			E txa();
			return sacc(REG_X, myTemp);
		}
		E tya();
		return sacc(REG_Y, myTemp);
	}
	void toA()
	{
		if (reg == REG_A) {
			return;
		}
		else if (reg == REG_X) {
			E txa();
			return;
		}
		E tya();
	}
	void toX()
	{
		if (reg == REG_A) {
			E tax();
			return;
		}
		else if (reg == REG_X) {
			return;
		}
		E sty_zp(myTemp);
		E ldx_zp(myTemp);
	}
	void toY()
	{
		if (reg == REG_A) {
			E tay();
			return;
		}
		else if (reg == REG_X) {
			return;
		}
		E stx_zp(myTemp);
		E ldy_zp(myTemp);
	}
};

class acc2
{
public:
	acc2(REGISTERS a1, REGISTERS a2, int m = 2) :
		r1(a1), r2(a2), myTemp(m) {}
	acc2(const acc2 &o) :
		r1(o.r1), r2(o.r2), myTemp(o.myTemp) {}

	REGISTERS r1,r2;
	uint8_t myTemp;
	
	acc2 r1toAandPreserve() {
		switch (r1) {
		case REG_A:
			return *this;
		case REG_X:
			if (r2 == REG_A) E tay();
			E txa();
			return acc2(REG_X,REG_Y);
		case REG_Y:
			if (r2 == REG_A) E tax();
			E tya();
			return acc2(REG_Y, REG_X);
		}
	}
	void r1toA() {
		switch (r1) {
		case REG_A:
			return;
		case REG_X:
			E txa();
			return;
		case REG_Y:
			E tya();
			return;
		}
	}
	void r2toA() {
		switch (r2) {
		case REG_A:
			return;
		case REG_X:
			E txa();
			return;
		case REG_Y:
			E tya();
			return;
		}
	}
	void store_r1(int o)
	{
		if (o < 256) {
			switch (r1) {
			case REG_A:
				E sta_zp(o);
				return;
			case REG_X:
				E stx_zp(o);
				return;
			case REG_Y:
				E sty_zp(o);
				return;
			}
		}
		else {
			switch (r1) {
			case REG_A:
				E sta_abs(o);
				return;
			case REG_X:
				E stx_abs(o);
				return;
			case REG_Y:
				E sty_abs(o);
				return;
			}
		}
	}
	void store_r2(int o)
	{
		if (o < 256) {
			switch (r2) {
			case REG_A:
				E sta_zp(o);
				return;
			case REG_X:
				E stx_zp(o);
				return;
			case REG_Y:
				E sty_zp(o);
				return;
			}
		}
		else {
			switch (r2) {
			case REG_A:
				E sta_abs(o);
				return;
			case REG_X:
				E stx_abs(o);
				return;
			case REG_Y:
				E sty_abs(o);
				return;
			}
		}
	}
};
;//x y
class sacc2 {
public:
	REGISTERS r1,r2;
	sacc2(REGISTERS a1, REGISTERS a2, int m = 2) :
		r1(a1), r2(a2), myTemp(m) {}
	sacc2(const sacc2 &o) :
		r1(o.r1), r2(o.r2), myTemp(o.myTemp) {}

	uint8_t myTemp;

	sacc2 r1toAandPreserve() {
		switch (r1) {
		case REG_A:
			return *this;
		case REG_X:
			if (r2 == REG_A) E tay();
			E txa();
			return sacc2(REG_X, REG_Y);
		case REG_Y:
			if (r2 == REG_A) E tax();
			E tya();
			return sacc2(REG_Y, REG_X);
		}
	}
	void r1toA() {
		switch (r1) {
		case REG_A:
			return;
		case REG_X:
			E txa();
			return;
		case REG_Y:
			E tya();
			return;
		}
	}
	void r2toA() {
		switch (r2) {
		case REG_A:
			return;
		case REG_X:
			E txa();
			return;
		case REG_Y:
			E tya();
			return;
		}
	}
	void store_r1(int o)
	{
		if (o < 256) {
			switch (r1) {
			case REG_A:
				E sta_zp(o);
				return;
			case REG_X:
				E stx_zp(o);
				return;
			case REG_Y:
				E sty_zp(o);
				return;
			}
		}
		else {
			switch (r1) {
			case REG_A:
				E sta_abs(o);
				return;
			case REG_X:
				E stx_abs(o);
				return;
			case REG_Y:
				E sty_abs(o);
				return;
			}
		}
	}
	void store_r2(int o)
	{
		if (o < 256) {
			switch (r2) {
			case REG_A:
				E sta_zp(o);
				return;
			case REG_X:
				E stx_zp(o);
				return;
			case REG_Y:
				E sty_zp(o);
				return;
			}
		}
		else {
			switch (r2) {
			case REG_A:
				E sta_abs(o);
				return;
			case REG_X:
				E stx_abs(o);
				return;
			case REG_Y:
				E sty_abs(o);
				return;
			}
		}
	}
};
#endif
//x y
const int16_t bank_reg = 0x9F61;
const int16_t rom_bank = 0x9F60;
Label zp2x2_plus_entry, zp2xb2_plus_entry;
void compile_zp2x2_plus()
{
// zp2(x) += zp2(value a y)
	//E lda_abs(o.address);
	//E ldy_abs(1 + o.address);
	//E ldx_imm(address);
	//E jsr(zp2x2_plus_entry);


	zp2x2_plus_entry.here();
	E sty_zp(WORKSPACE);//3

	E clc();			//2
	E adc_izx(0);		//6
	E sta_izx(0);		//6
	E lda_izx(1);		//6
	E adc_zp(WORKSPACE);//3
	E sta_izx(1);		//6
	E rts();			//12
	//					41+load 6 ... 47 vs 20
}
void compile_zp2xb2_plus()
{
	// zp2(x) += zp2(value a y)
		//E lda_imm(o.address>>16); //2
		//E sta_zp(WORKSPACE);		//3
		//E lda_imm(o.address);		//2
		//E ldy_imm(o.address >> 8);//2
		//E ldx_imm(address);		//2
		//E jsr(zp2xb2_plus_entry);	//6
		//13 bytes

	Label fixup,fixup2;
	zp2xb2_plus_entry.here();
	E sta_zp(WORKSPACE + 2);	//3
	E sty_zp(WORKSPACE + 3);	//3
	E lda_abs(bank_reg);		//4
	E sta_zp(WORKSPACE + 1);	//3
	E lda_zp(WORKSPACE);		//3	
	E sta_abs(bank_reg);		//4
	E stx_abs(fixup);			//4
	E stx_abs(fixup2);			//4

	E clc();					//2
	E lda_izp(2 + WORKSPACE);	//5
	E adc_izx(0);				//6
	E sta_izx(0);				//6
	E ldy_imm(1);				//2
	E lda_izy(2 + WORKSPACE);	//6
	fixup.here(1);			
	E adc_zp(0);				//3
	fixup2.here(1);
	E sta_zp(0);				//3
	E lda_zp(WORKSPACE + 1);	//2
	E sta_abs(bank_reg);		//4
	E rts();					//6
	//					67+load 17 ... 84 vs 20

	//E jsr(zp2xb2_plus_entry);	//6
	//E comp_three(o.address)
	//E comp_byte(address)
	//7 bytes
	E pla();					//4
	E clc();					//2
	E sta_zp(WORKSPACE);		//3
	E adc_imm(5);				//2 
	E pla();					//4 
	E adc_imm(0);				//2 
	E sta_zp(1 + WORKSPACE);	//3 
	E lda_abs(bank_reg);		//4
	E sta_zp(2 + WORKSPACE);	//3 
	E ldy_imm(-4);				//2 
	E lda_izy(WORKSPACE);		//3 
	E sta_zp(3 + WORKSPACE);	//3 
	E iny();					//2 
	E lda_izy(WORKSPACE);		//5 
	E sta_zp(4 + WORKSPACE);	//3 
	E iny();					//2
	E lda_izy(WORKSPACE);		//5
	E sta_abs(bank_reg);		//4 
	E iny();					//2
	E lda_izy(WORKSPACE);		//5
	E sta_abs(fixup);			//4
	E sta_abs(fixup2);			//4 72

	E clc();					//2
	E lda_izp(3 + WORKSPACE);	//5
	E adc_izx(0);				//6
	E sta_izx(0);				//6
	E ldy_imm(1);				//2
	E lda_izy(3 + WORKSPACE);	//6
	fixup.here(1);
	E adc_zp(0);				//3
	fixup2.here(1);
	E sta_zp(0);				//3
	E lda_zp(2 + WORKSPACE);	//2
	E sta_abs(bank_reg);		//4
	E jmp_ind(WORKSPACE);		//5
	//122 vm vs 84 bank vs 20
	//18 bytes inline
	//18*9+30 - about 220 to copy and run

	//E (zp2xb2_plus_entry);	
	//E comp_three(o.address)
	//E comp_byte(address)
	 

	E lda_abs(bank_reg);		//4
	E sta_zp(WORKSPACE);		//3 
	E ldy_imm(2)				//2 
	E lda_izy(pc);				//3 
	E sta_zp(1 + WORKSPACE);	//3 
	E iny();					//2 
	E lda_izy(PC);				//5 
	E sta_zp(2 + WORKSPACE);	//3 
	E iny();					//2
	E lda_izy(PC);				//5
	E sta_abs(bank_reg);		//4 
	E iny();					//2
	E lda_izy(PC);				//5
	E sta_abs(3+WORKSPACE);		//4
	E stz_abs(4+WORKSPACE);		//4 72
	E iny();					//2
	E lda_izy(PC);				//3 
	E tax();					//2
	E clc();					//2
	E lda_izp(1 + WORKSPACE);	//5
	E ldx_imm(0)				//2
	E adc_izp(3+WORKSPACE);		//5
	E sta_izp(3 + WORKSPACE);	//5
	E ldy_imm(1);				//2
	E lda_izy(1 + WORKSPACE);	//6
	E adc_izy(3 + WORKSPACE);	//5
	E sta_izy(3 + WORKSPACE);	//5
	E lda_zp(2 + WORKSPACE);	//2
	E sta_abs(bank_reg);		//4
	E jmp_ind(WORKSPACE);		//5
	E clc();					//2
	E lda_zp(PC);				//3
	E adc_imm(7);				//2 
	E sta_zp(PC);				//3
	E bcs(incp)					//2
	E jmp_ind(PC)				//5
		//123 vm
}


void compile_zp2xab2_plus()
{
	// zp2(x) += ab2(w)(in a y)

	E sta_zp(WORKSPACE);		//3
	E sty_zp(WORKSPACE+1);		//3

	E clc();					//2
	E lda_izx(0);				//6
	E adc_izp(WORKSPACE);		//5
	E sta_izx(0);				//6
	E ldy_imm(1);				//2
	E lda_izx(1);				//6
	E adc_izy(WORKSPACE);		//5
	E sta_izx(1);				//6
	E rts();					//6+6
	//							52+load 6... 58 vs 22
}//58

void compile_ab2xzp2_plus()
{
	// ab2(in a y) += zp2(w) in x
	//13 bytes down to 7
	//much slower
	E sta_zp(WORKSPACE);		//3
	E sty_zp(WORKSPACE + 1);	//3

	E clc();					//2
	E lda_izx(0);				//6
	E adc_izp(WORKSPACE);		//5
	E sta_izp(WORKSPACE);		//5
	E ldy_imm(1);				//2
	E lda_izx(1);				//6
	E adc_izy(WORKSPACE);		//5
	E sta_izy(WORKSPACE);		//5
	E rts();					//6+6
	//							54+load 6... 60 vs 22
}

void compile_2x2_plus()
{
	// zp2(w0) += zp2(w2)
	//13 bytes down to 7
	//much slower
	E clc();					//2
	E lda_izp(WORKSPACE);		//5
	E adc_izp(WORKSPACE+2);		//5
	E sta_izp(WORKSPACE);		//5
	E ldy_imm(1);				//2
	E lda_izy(WORKSPACE);		//5
	E adc_izy(WORKSPACE + 2);	//5
	E sta_izy(WORKSPACE);		//6
	E rts();					//6+6
	//							47+load 18->20
}

class var& var::operator +=(const var& o)
{
	int two_add_class = ACxAC(address_class,o.address_class);
	int two_sizes = BytesxBytes(bytes,o.bytes);
	int two_signs = SignxSign(is_signed, o.is_signed);
	switch (two_sizes) {
	case BytesxBytes(2, 2)://2 from 2
	case BytesxBytes(2, 3)://2 from 3
		if ( o.address_class == BANKED_AC || address_class == BANKED_AC) {
			//banked versions
		}
		else {
			//not worth calling this one, so only call on tight
			if (generation_mode > TIGHT_GM) {

			}
			else {

			}
		}
		break;
	case BytesxBytes(1, 1):
	case BytesxBytes(1, 2):
	case BytesxBytes(1, 3):
		break;

	}
	E clc();				//2
	E lda_zp(address);		//3
	E adc_zp(o.address);	//3
	E sta_zp(address);		//3
	E lda_zp(address + 1);	//3
	E adc_zp(o.address + 1);//3
	E sta_zp(address + 1);	//3 - 20
	return *this;
}
class zp2& zp2::operator +=(const zp1& o)
{
	E lda_zp(o.address);
	return *this+=acc(REG_A);
}
class zp2& zp2::operator +=(const szp2& o)
{
	E clc();
	E lda_zp(address);
	E adc_zp(o.address);
	E sta_zp(address);
	E lda_zp(address + 1);
	E adc_zp(o.address + 1);
	E sta_zp(address + 1);
	return *this;
}
class zp2& zp2::operator +=(const szp1& o)
{
	E lda_zp(o.address);
	return *this += sacc(REG_A);
}
class zp2& zp2::operator +=(const add1& o)
{
	E lda_abs(o.address);
	return *this += acc(REG_A);
}
class zp2& zp2::operator +=(const add2& o)
{
	E clc();
	E lda_zp(address);
	E adc_abs(o.address);
	E sta_zp(address);
	E lda_zp(address + 1);
	E adc_abs(o.address + 1);
	E sta_zp(address + 1);
	return *this;
}
class zp2& zp2::operator +=(const sadd1& o)
{
	E lda_abs(o.address);
	return *this += sacc(REG_A, 0);
}
class zp2& zp2::operator +=(const sadd2& o)
{
	E clc();
	E lda_zp(address);
	E adc_abs(o.address);
	E sta_zp(address);
	E lda_zp(address + 1);
	E adc_abs(o.address + 1);
	E sta_zp(address + 1);
	return *this;
}
class zp2& zp2::operator +=(acc o)
{
	o.toA();
	E clc();
	E adc_zp(address);
	E sta_zp(address);
	Label done;
	E bcc(done);
	E inc_zp(1 + address);
	done.here(&emulator);
	return *this;
}
class zp2& zp2::operator +=(sacc o)
{
	sacc t = o.toAandPreserve();
	E clc();
	E adc_zp(address);
	E sta_zp(address);
	t.toA();
	E ora_imm(0);
	Label do_pos, done;
	E bpl(do_pos);
	E bcs(done);
	E dec_zp(1 + address);
	E bra(done);
	do_pos.here(&emulator);
	E bcc(done);
	E inc_zp(1 + address);
	done.here(&emulator);
	return *this;
}
class zp2& zp2::operator +=(acc2 o)
{
	if (o.r2 == REG_A) E tax();
	o.r1toA();
	E clc();
	E adc_zp(address);
	E sta_zp(address);
	if (o.r2 == REG_Y) E tya();
	else E txa();
	E adc_zp(address+1);
	E sta_zp(address+1);
	return *this;
}
class zp2& zp2::operator +=(sacc2 o)
{
	return *this+=acc(o.r1,o.r2);
}
class zp2& zp2::operator +=(int o)
{
	if (o < 0) {
		E lda_imm(o & 0xff);
		E clc();
		E adc_zp(address);
		E sta_zp(address);
		if ((o >> 8) == -1) {
			Label done;
			E bcs(done);
			E dec_zp(1 + address);
			done.here(&emulator);
		}
		else {
			E lda_imm(o>>8);
			E adc_zp(address + 1);
			E sta_zp(address + 1);
		}
	}
	else if (o > 0) {
		E lda_imm(o & 0xff);
		E clc();
		E adc_zp(address);
		E sta_zp(address);
		if ((o >> 8) == 0) {
			Label done;
			E bcc(done);
			E inc_zp(1 + address);
			done.here(&emulator);
		}
		else {
			E lda_imm(o >> 8);
			E adc_zp(address + 1);
			E sta_zp(address + 1);
		}
	}
	return *this;
}

class zp2& zp2::operator -=(const zp2& o)
{
	E sec();
	E lda_zp(address);
	E sbc_zp(o.address);
	E sta_zp(address);
	E lda_zp(address + 1);
	E sbc_zp(o.address + 1);
	E sta_zp(address + 1);
	return *this;
}
class zp2& zp2::operator -=(const zp1& o)
{
	E sec();
	E lda_zp(address);
	E sbc_zp(o.address);
	E sta_zp(address);
	Label done;
	E bcs(done);
	E dec_zp(address + 1);
	done.here(&emulator);
	return *this;
}
class zp2& zp2::operator -=(const szp2& o)
{
	E sec();
	E lda_zp(address);
	E sbc_zp(o.address);
	E sta_zp(address);
	E lda_zp(address + 1);
	E sbc_zp(o.address + 1);
	E sta_zp(address + 1);
	return *this;
}
class zp2& zp2::operator -=(const szp1& o)
{
	Label do_pos;
	E ldx_imm(0);
	E lda_zp(o.address);
	E bpl(do_pos);
	E dex();
	do_pos.here(&emulator);
	E stx_zp(myTemp);
	E lda_zp(address);
	E sec();
	E sbc_zp(o.address);
	E sta_zp(address);
	E lda_zp(address + 1);
	E sbc_zp(myTemp);
	E sta_zp(address + 1);
	return *this;
}
class zp2& zp2::operator -=(const add1& o)
{
	E sec();
	E lda_zp(address);
	E sbc_abs(o.address);
	E sta_zp(address);
	Label done;
	E bcs(done);
	E dec_zp(address + 1);
	done.here(&emulator);
	return *this;
}
class zp2& zp2::operator -=(const add2& o)
{
	E sec();
	E lda_zp(address);
	E sbc_abs(o.address);
	E sta_zp(address);
	E lda_zp(address + 1);
	E sbc_abs(o.address + 1);
	E sta_zp(address + 1);
	return *this;
}
class zp2& zp2::operator -=(const sadd1& o)
{
	Label do_pos;
	E ldx_imm(0);
	E lda_abs(o.address);
	E bpl(do_pos);
	E dex();
	do_pos.here(&emulator);
	E stx_zp(myTemp);
	E lda_zp(address);
	E sec();
	E sbc_abs(o.address);
	E sta_zp(address);
	E lda_zp(address + 1);
	E sbc_zp(myTemp);
	E sta_zp(address + 1);
	return *this;
}
class zp2& zp2::operator -=(const sadd2& o)
{
	E sec();
	E lda_zp(address);
	E sbc_abs(o.address);
	E sta_zp(address);
	E lda_zp(address + 1);
	E sbc_abs(o.address + 1);
	E sta_zp(address + 1);
	return *this;
}
class zp2& zp2::operator -=(acc o)
{
	o.store_zp(myTemp);
	E sec();
	E lda_zp(address);
	E sbc_zp(myTemp);
	E sta_zp(address);
	Label done;
	E bcs(done);
	E dec_zp(address + 1);
	done.here(&emulator);
	return *this;
}
class zp2& zp2::operator -=(sacc o)
{
	Label do_pos;
	o.toA();
	E ldx_imm(0);
	E tay();
	E bpl(do_pos);
	E dex();
	do_pos.here(&emulator);
	E stx_zp(myTemp);
	E sta_zp(myTemp + 1);
	E lda_zp(address);
	E sec();
	E sbc_abs(myTemp + 1);
	E sta_zp(address);
	E lda_zp(address + 1);
	E sbc_zp(myTemp);
	E sta_zp(address + 1);
	return *this;
}
class zp2& zp2::operator -=(acc2 o)
{
	o.store_r1(myTemp);
	o.store_r2(myTemp + 1);
	E sec();
	E lda_zp(address);
	E sbc_zp(myTemp);
	E sta_zp(address);
	E lda_zp(address + 1);
	E sbc_zp(myTemp + 1);
	E sta_zp(address + 1);
	return *this;
}
class zp2& zp2::operator -=(sacc2 o)
{
	o.store_r1(myTemp);
	o.store_r2(myTemp + 1);
	E sec();
	E lda_zp(address);
	E sbc_zp(myTemp);
	E sta_zp(address);
	E lda_zp(address + 1);
	E sbc_zp(myTemp + 1);
	E sta_zp(address + 1);
	return *this;
}
class zp2& zp2::operator -=(int o)
{
	E sec();
	E lda_zp(address);
	E sbc_imm(o&0xff);
	E sta_zp(address);
	E lda_zp(address + 1);
	E sbc_imm(o>>8);
	E sta_zp(address + 1);
	return *this;
}

class zp2& zp2::operator &=(const zp2& o)
{
	E lda_zp(address);
	E and_zp(o.address);
	E sta_zp(address);
	E lda_zp(address + 1);
	E and_zp(o.address + 1);
	E sta_zp(address + 1);
	return *this;
}
class zp2& zp2::operator &=(const zp1& o)
{
	E lda_zp(address);
	E and_zp(o.address);
	E sta_zp(address);
	E stz_zp(address + 1);
	return *this;
}
class zp2& zp2::operator &=(const szp2& o)
{
	E lda_zp(address);
	E and_zp(o.address);
	E sta_zp(address);
	E lda_zp(address + 1);
	E and_zp(o.address + 1);
	E sta_zp(address + 1);
	return *this;
}
class zp2& zp2::operator &=(const szp1& o)
{
	E lda_zp(address);
	E and_zp(o.address);
	E sta_zp(address);
	E bit_zp(o.address);
	Label done;
	E bmi(done);
	E stz_zp(address + 1);
	done.here(&emulator);
	return *this;
}
class zp2& zp2::operator &=(const add1& o)
{
	E lda_zp(address);
	E and_abs(o.address);
	E sta_zp(address);
	E stz_zp(address + 1);
	return *this;
}
class zp2& zp2::operator &=(const add2& o)
{
	E lda_zp(address);
	E and_abs(o.address);
	E sta_zp(address);
	E lda_zp(address + 1);
	E and_abs(o.address + 1);
	E sta_zp(address + 1);
	return *this;
}
class zp2& zp2::operator &=(const sadd1& o)
{
	E lda_zp(address);
	E and_abs(o.address);
	E sta_zp(address);
	E bit_abs(o.address);
	Label done;
	E bmi(done);
	E stz_zp(address + 1);
	done.here(&emulator);
	return *this;
}
class zp2& zp2::operator &=(const sadd2& o)
{
	E lda_zp(address);
	E and_abs(o.address);
	E sta_zp(address);
	E lda_zp(address + 1);
	E and_abs(o.address + 1);
	E sta_zp(address + 1);
	return *this;
}
class zp2& zp2::operator &=(acc o)
{
	o.toA();
	E and_zp(address);
	E sta_zp(address);
	E stz_zp(address + 1);
	return *this;
}
class zp2& zp2::operator &=(sacc o)
{
	o.store_zp(myTemp);
	E lda_zp(address);
	E and_abs(myTemp);
	E sta_zp(address);
	E bit_abs(myTemp);
	Label done;
	E bmi(done);
	E stz_zp(address + 1);
	done.here(&emulator);
	return *this;
}
class zp2& zp2::operator &=(acc2 o)
{
	o.store_r2(myTemp);
	o.r1toA();
	E and_abs(address);
	E sta_zp(address);
	E lda_zp(address + 1);
	E and_abs(myTemp);
	E sta_zp(address + 1);
	return *this;
}
class zp2& zp2::operator &=(sacc2 o)
{
	o.store_r2(myTemp);
	o.r1toA();
	E and_abs(address);
	E sta_zp(address);
	E lda_zp(address + 1);
	E and_abs(myTemp);
	E sta_zp(address + 1);
	return *this;
}
class zp2& zp2::operator &=(int o)
{
	o &= 0xffff;
	if (o != 0xffff) {
		if (o == 0) {
			E stz_zp(address);
			E stz_zp(address+1);
		}
		else if ((o & 0xff00)==0){
			E lda_imm(o);
			E and_zp(address);
			E sta_zp(address);
			E stz_zp(address + 1);
		}if ((o & 0xff) == 0) {
			E stz_zp(address);
			E lda_imm(o>>8);
			E and_zp(address + 1);
			E sta_zp(address + 1);
		}
		else {
			E lda_imm(o & 0xff);
			E and_zp(address);
			E sta_zp(address);
			E lda_imm(o >> 8);
			E and_zp(address + 1);
			E sta_zp(address + 1);
		}
	}
	return *this;
}

class zp2& zp2::operator |=(const zp2& o)
{
	E lda_zp(address);
	E ora_zp(o.address);
	E sta_zp(address);
	E lda_zp(address + 1);
	E ora_zp(o.address + 1);
	E sta_zp(address + 1);
	return *this;
}
class zp2& zp2::operator |=(const zp1& o)
{
	E lda_zp(address);
	E ora_zp(o.address);
	E sta_zp(address);
	return *this;
}
class zp2& zp2::operator |=(const szp2& o)
{
	E lda_zp(address);
	E ora_zp(o.address);
	E sta_zp(address);
	E lda_zp(address + 1);
	E ora_zp(o.address + 1);
	E sta_zp(address + 1);
	return *this;
}
class zp2& zp2::operator |=(const szp1& o)
{
	E lda_zp(address);
	E ora_zp(o.address);
	E sta_zp(address);
	Label done;
	E bit_zp(o.address);
	E bpl(done);
	E lda_imm(0xff);
	E sta_zp(address + 1);
	done.here(&emulator);
	return *this;
}
class zp2& zp2::operator |=(const add1& o)
{
	E lda_zp(address);
	E ora_abs(o.address);
	E sta_zp(address);
	return *this;
}
class zp2& zp2::operator |=(const add2& o)
{
	E lda_zp(address);
	E ora_abs(o.address);
	E sta_zp(address);
	E lda_zp(address + 1);
	E ora_abs(o.address + 1);
	E sta_zp(address + 1);
	return *this;
}
class zp2& zp2::operator |=(const sadd1& o)
{
	E lda_zp(address);
	E ora_abs(o.address);
	E sta_zp(address);
	Label done;
	E bit_abs(o.address);
	E bpl(done);
	E lda_imm(0xff);
	E sta_zp(address + 1);
	done.here(&emulator);
	return *this;
}
class zp2& zp2::operator |=(const sadd2& o)
{
	E lda_zp(address);
	E ora_abs(o.address);
	E sta_zp(address);
	E lda_zp(address + 1);
	E ora_abs(o.address + 1);
	E sta_zp(address + 1);
	return *this;
}
class zp2& zp2::operator |=(acc o)
{
	o.toA();
	E ora_zp(address);
	E sta_zp(address);
	return *this;
}
class zp2& zp2::operator |=(sacc o)
{
	switch (o.reg) {
	case REG_A:
		E ora_imm(0);
		break;
	case REG_X:
		E txa();
		break;
	case REG_Y:
		E tya();
		break;
	}
	Label done;
	E bpl(done);
	E ldx_imm(0xff);
	E stx_zp(address + 1);
	done.here(&emulator);
	E ora_zp(address);
	E sta_zp(address);
	return *this;
}
class zp2& zp2::operator |=(acc2 o)
{
	o.store_r2(myTemp);
	o.r1toA();
	E ora_zp(address);
	E sta_zp(address);
	E lda_zp(address + 1);
	E ora_zp(myTemp);
	E sta_zp(address + 1);
	return *this;
}
class zp2& zp2::operator |=(sacc2 o)
{
	o.store_r2(myTemp);
	o.r1toA();
	E ora_zp(address);
	E sta_zp(address);
	E lda_zp(address + 1);
	E ora_zp(myTemp);
	E sta_zp(address + 1);
	return *this;
}
class zp2& zp2::operator |=(int o)
{
	o &= 0xffff;
	if (o != 0) {
		if ((o & 0xff00) != 0) {
			if ((o & 0xff00) == 0xff00) {
				E lda_imm(0xff);
				E sta_zp(address + 1);
			}
			else {
				E lda_zp(address + 1);
				E ora_imm(o >> 8);
				E sta_zp(address + 1);
			}
		}
		if ((o & 0xff) != 0) {
			if ((o & 0xff) == 0xff) {
				E lda_imm(0xff);
				E sta_zp(address);
			}
			else {
				E lda_zp(address);
				E ora_imm(o & 0xff);
				E sta_zp(address);
			}
		}
	}
	return *this;
}

class zp2& zp2::operator ^=(const zp2& o)
{
	E lda_zp(address);
	E eor_zp(o.address);
	E sta_zp(address);
	E lda_zp(address + 1);
	E eor_zp(o.address + 1);
	E sta_zp(address + 1);
	return *this;
}
class zp2& zp2::operator ^=(const zp1& o)
{
	E lda_zp(address);
	E eor_zp(o.address);
	E sta_zp(address);
	return *this;
}
class zp2& zp2::operator ^=(const szp2& o)
{
	E lda_zp(address);
	E eor_zp(o.address);
	E sta_zp(address);
	E lda_zp(address + 1);
	E eor_zp(o.address + 1);
	E sta_zp(address + 1);
	return *this;
}
class zp2& zp2::operator ^=(const szp1& o)
{
	E lda_zp(address);
	E eor_zp(o.address);
	E sta_zp(address);
	Label done;
	E bit_zp(o.address);
	E bpl(done);
	E lda_imm(0xff);
	E eor_zp(address + 1);
	E sta_zp(address + 1);
	done.here(&emulator);
	return *this;
}
class zp2& zp2::operator ^=(const add1& o)
{
	E lda_zp(address);
	E eor_abs(o.address);
	E sta_zp(address);
	return *this;
}
class zp2& zp2::operator ^=(const add2& o)
{
	E lda_zp(address);
	E eor_abs(o.address);
	E sta_zp(address);
	E lda_abs(address + 1);
	E eor_zp(o.address + 1);
	E sta_zp(address + 1);
	return *this;
}
class zp2& zp2::operator ^=(const sadd1& o)
{
	E lda_zp(address);
	E eor_abs(o.address);
	E sta_zp(address);
	Label done;
	E bit_abs(o.address);
	E bpl(done);
	E lda_imm(0xff);
	E eor_zp(address + 1);
	E sta_zp(address + 1);
	done.here(&emulator);
	return *this;
}
class zp2& zp2::operator ^=(const sadd2& o)
{
	E lda_zp(address);
	E eor_abs(o.address);
	E sta_zp(address);
	E lda_abs(address + 1);
	E eor_zp(o.address + 1);
	E sta_zp(address + 1);
	return *this;
}
class zp2& zp2::operator ^=(acc o)
{
	o.toA();
	E eor_abs(address);
	E sta_zp(address);
	return *this;
}
class zp2& zp2::operator ^=(sacc o)
{
	switch (o.reg) {
	case REG_A:
		E tax();
		break;
	case REG_X:
		E txa();
		break;
	case REG_Y:
		E tya();
		break;
	}
	E eor_zp(address);
	E sta_zp(address);
	Label done;
	if (o.reg== REG_Y) E tya(); else E txa();
	E bpl(done);
	E lda_imm(0xff);
	E eor_zp(address + 1);
	E sta_zp(address + 1);
	done.here(&emulator);
	return *this;
}
class zp2& zp2::operator ^=(acc2 o)
{
	o.store_r2(myTemp);
	o.r1toA();
	E eor_zp(address);
	E sta_zp(address);
	E lda_zp(address + 1);
	E eor_zp(myTemp);
	E sta_zp(address + 1);
	return *this;
}
class zp2& zp2::operator ^=(sacc2 o)
{
	o.store_r2(myTemp);
	o.r1toA();
	E eor_zp(address);
	E sta_zp(address);
	E lda_zp(address + 1);
	E eor_zp(myTemp);
	E sta_zp(address + 1);
	return *this;
}
class zp2& zp2::operator ^=(int o)
{
	o &= 0xffff;
	if (o != 0) {
		if ((o & 0xff00) != 0) {
			E lda_zp(address + 1);
			E ora_imm(o >> 8);
			E sta_zp(address + 1);
		}
		if ((o & 0xff) != 0) {
			E lda_zp(address);
			E ora_imm(o & 0xff);
			E sta_zp(address);
		}
	}
	return *this;
}

const int shift_temp = 4;

class zp2& zp2::operator >>=(const zp2& o)
{//too long shouldn't be inline
	Label done, do_shift, simple_shift;
	(o < 1).then(done);
	(o < 16).then(do_shift);
	E stz_ab(address);
	E stz_ab(address+1);
	E bra(done);
	do_shift.here(&emulator);
	E ldx_ab(o.address);
	E cpx_imm(8);
	E bcc(simple_shift);
	E lda_ab(address + 1);
	E stz_ab(address + 1);
	E sta_ab(address);
	E tay();
	E sec();
	E txa();
	E sbc_imm(8);
	E beq(done);
	E tax();
	//1 byte shift time, shift is in x
	E lda_ax(rshift_low);
	E sta_ab(myTemp);
	E lda_ax(rshift_high);
	E sta_ab(myTemp+1);
	E lda_izy(myTemp);
	E sta_ab(address);
	E bra(done);
	simple_shift.here(&emulator);
	E lda_ax(rshift_low);
	E sta_ab(myTemp);
	E lda_ax(rshift_high);
	E sta_ab(myTemp + 1);
	E lda_ax(lshift_rev_low);
	E sta_ab(shift_temp);
	E lda_ax(lshift_rev_high);
	E sta_ab(shift_temp+1);
	E ldy_ab(address);
	E lda_izy(myTemp);
	E ldy_ab(address+1);
	E ora_izy(shift_temp);
	E sta_ab(address);
	E lda_izy(myTemp);
	E sta_ab(address + 1);
	//shift is in x
	done.here(&emulator);
	return *this;
}
class zp2& zp2::operator >>=(const zp1& o)
{
	return *this;
}
class zp2& zp2::operator >>=(const szp2& o)
{
	return *this;
}
class zp2& zp2::operator >>=(const szp1& o)
{
	return *this;
}
class zp2& zp2::operator >>=(const add1& o)
{
	return *this;
}
class zp2& zp2::operator >>=(const add2& o)
{
	return *this;
}
class zp2& zp2::operator >>=(const sadd1& o)
{
	return *this;
}
class zp2& zp2::operator >>=(const sadd2& o)
{
	return *this;
}
class zp2& zp2::operator >>=(acc o)
{
	return *this;
}
class zp2& zp2::operator >>=(sacc o)
{
	return *this;
}
class zp2& zp2::operator >>=(acc2 o)
{
	return *this;
}
class zp2& zp2::operator >>=(sacc2 o)
{
	return *this;
}
class zp2& zp2::operator >>=(int o)
{
	return *this;
}

class zp2& zp2::operator <<=(const zp2& o)
{
	return *this;
}
class zp2& zp2::operator <<=(const zp1& o)
{
	return *this;
}
class zp2& zp2::operator <<=(const szp2& o)
{
	return *this;
}
class zp2& zp2::operator <<=(const szp1& o)
{
	return *this;
}
class zp2& zp2::operator <<=(const add1& o)
{
	return *this;
}
class zp2& zp2::operator <<=(const add2& o)
{
	return *this;
}
class zp2& zp2::operator <<=(const sadd1& o)
{
	return *this;
}
class zp2& zp2::operator <<=(const sadd2& o)
{
	return *this;
}
class zp2& zp2::operator <<=(acc o)
{
	return *this;
}
class zp2& zp2::operator <<=(sacc o)
{
	return *this;
}
class zp2& zp2::operator <<=(acc2 o)
{
	return *this;
}
class zp2& zp2::operator <<=(sacc2 o)
{
	return *this;
}
class zp2& zp2::operator <<=(int o)
{
	return *this;
}

class zp2& zp2::operator *=(const zp2& o)
{
	return *this;
}
class zp2& zp2::operator *=(const zp1& o)
{
	return *this;
}
class zp2& zp2::operator *=(const szp2& o)
{
	return *this;
}
class zp2& zp2::operator *=(const szp1& o)
{
	return *this;
}
class zp2& zp2::operator *=(const add1& o)
{
	return *this;
}
class zp2& zp2::operator *=(const add2& o)
{
	return *this;
}
class zp2& zp2::operator *=(const sadd1& o)
{
	return *this;
}
class zp2& zp2::operator *=(const sadd2& o)
{
	return *this;
}
class zp2& zp2::operator *=(acc o)
{
	return *this;
}
class zp2& zp2::operator *=(sacc o)
{
	return *this;
}
class zp2& zp2::operator *=(acc2 o)
{
	return *this;
}
class zp2& zp2::operator *=(sacc2 o)
{
	return *this;
}
class zp2& zp2::operator *=(int o)
{
	return *this;
}


acc zp2::indexLoad1(const zp2& o)
{
	E clc();
	E lda_ab(address);
	E adc_ab(o.address);
	E sta_zp(myTemp);
	E lda_ab(address+1);
	E adc_ab(o.address+1);
	E sta_zp(myTemp+1);
	E lda_izp(myTemp);
	return acc(REG_A, default_temp);
}
acc zp2::indexLoad1(const zp1& o)
{
	E ldy_ab(o.address);
	E lda_izy(address);
	return acc(REG_A,default_temp);
}
acc zp2::indexLoad1(const szp2& o)
{
	E clc();
	E lda_ab(address);
	E adc_ab(o.address);
	E sta_zp(myTemp);
	E lda_ab(address + 1);
	E adc_ab(o.address + 1);
	E sta_zp(myTemp + 1);
	E lda_izp(myTemp);
	return acc(REG_A, default_temp);
}
acc zp2::indexLoad1(const szp1& o)
{
	Label pos_load, done;
	E ldy_ab(o.address);
	E bpl(pos_load);
	E dec_zp(address + 1);
	E lda_izy(address);
	E inc_zp(address + 1);
	E bra(done);
	pos_load.here(&emulator);
	E lda_izy(address);
	done.here(&emulator);
	return acc(REG_A, default_temp);
}
acc zp2::indexLoad1(const add1& o)
{
	E ldy_ab(o.address);
	E lda_izy(address);
	return acc(REG_A, default_temp);
}
acc zp2::indexLoad1(const add2& o)
{
	E clc();
	E lda_ab(address);
	E adc_ab(o.address);
	E sta_zp(myTemp);
	E lda_ab(address + 1);
	E adc_ab(o.address + 1);
	E sta_zp(myTemp + 1);
	E lda_izp(myTemp);
	return acc(REG_A, default_temp);
}
acc zp2::indexLoad1(const sadd1& o)
{
	Label pos_load, done;
	E ldy_ab(o.address);
	E bpl(pos_load);
	E dec_zp(address + 1);
	E lda_izy(address);
	E inc_zp(address + 1);
	E bra(done);
	pos_load.here(&emulator);
	E lda_izy(address);
	done.here(&emulator);
	return acc(REG_A, default_temp);
}
acc zp2::indexLoad1(const sadd2& o)
{
	E clc();
	E lda_zp(address);
	E adc_ab(o.address);
	E sta_zp(myTemp);
	E lda_zp(address + 1);
	E adc_ab(o.address + 1);
	E sta_zp(myTemp + 1);
	E lda_izp(myTemp);
	return acc(REG_A, default_temp);
}
acc zp2::indexLoad1(acc o)
{
	o.toY();
	E lda_izy(address);
	return acc(REG_A, default_temp);
}
acc zp2::indexLoad1(sacc o)
{
	if (o.reg == REG_Y) E cpy_imm(0);
	else o.toY();
	Label pos_load, done;
	E bpl(pos_load);
	E dec_zp(address + 1);
	E lda_izy(address);
	E inc_zp(address + 1);
	E bra(done);
	pos_load.here(&emulator);
	E lda_izy(address);
	done.here(&emulator);
	return acc(REG_A, default_temp);
}
acc zp2::indexLoad1(acc2 o)
{
	acc2 o2=o.r1toAandPreserve();
	E clc();
	E adc_ab(address);
	E sta_zp(myTemp);
	o2.r2toA();
	E adc_ab(address + 1);
	E sta_zp(myTemp + 1);
	E lda_izp(myTemp);
	return acc(REG_A, default_temp);
}
acc zp2::indexLoad1(sacc2 o)
{
	sacc2 o2 = o.r1toAandPreserve();
	E clc();
	E adc_ab(address);
	E sta_zp(myTemp);
	o2.r2toA();
	E adc_ab(address + 1);
	E sta_zp(myTemp + 1);
	E lda_izp(myTemp);
	return acc(REG_A, default_temp);
}
acc zp2::indexLoad1(int o)
{
	if (o == 0) {
		E lda_izp(address);
	}
	else if (o > 0 && o <= 255) {
		E ldy_imm(o);
		E lda_izy(address);
	}
	else {
		E lda_zp(address);
		E sta_zp(myTemp);
		E clc();
		E lda_zp(address+1);
		E adc_imm((o >> 8)&0xff);
		E sta_zp(myTemp + 1);
		E ldy_imm(o & 0xff);
		E lda_izy(myTemp);
	}
	return acc(REG_A, default_temp);
}

zp2 zp2::indexLoad2(const zp2& o)
{
	zp2 ret(default_ret,ret_temp);
	E clc();
	E lda_ab(address);
	E adc_ab(o.address);
	E sta_zp(myTemp);
	E lda_ab(address + 1);
	E adc_ab(o.address + 1);
	E sta_zp(myTemp + 1);
	E lda_izp(myTemp);
	E sta_zp(ret.address);
	E ldy_imm(1);
	E lda_izy(myTemp);
	E sta_zp(ret.address+1);
	return ret;
}
zp2 zp2::indexLoad2(const zp1& o)
{
	zp2 ret(default_ret, ret_temp);
	E ldy_ab(o.address);
	E lda_izy(address);
	E sta_zp(ret.address);
	E iny();
	E lda_izy(address);
	E sta_zp(ret.address + 1);
	return ret;
}
zp2 zp2::indexLoad2(const szp2& o)
{
	zp2 ret(default_ret, ret_temp);
	E clc();
	E lda_ab(address);
	E adc_ab(o.address);
	E sta_zp(myTemp);
	E lda_ab(address + 1);
	E adc_ab(o.address + 1);
	E sta_zp(myTemp + 1);
	E lda_izp(myTemp);
	E sta_zp(ret.address);
	E ldy_imm(1);
	E lda_izy(myTemp);
	E sta_zp(ret.address + 1);
	return ret;
}
zp2 zp2::indexLoad2(const szp1& o)
{
	zp2 ret(default_ret, ret_temp);
	E ldx_imm(0);
	E lda_ab(o.address);
	Label past;
	E bpl(past);
	E dex();
	past.here(&emulator);
	E clc();
	E adc_zp(address);
	E sta_zp(myTemp);
	E txa();
	E adc_zp(address+1);
	E sta_zp(myTemp+1);
	E lda_izp(myTemp);
	E sta_zp(ret.address);
	E ldy_imm(1);
	E lda_izy(myTemp);
	E sta_zp(ret.address + 1);
	return ret;
}
zp2 zp2::indexLoad2(const add1& o)
{
	zp2 ret(default_ret, ret_temp);
	E ldy_ab(o.address);
	E lda_izy(address);
	E sta_zp(ret.address);
	E iny();
	E lda_izy(address);
	E sta_zp(ret.address + 1);
	return ret;
}
zp2 zp2::indexLoad2(const add2& o)
{
	zp2 ret(default_ret, ret_temp);
	E clc();
	E lda_ab(address);
	E adc_ab(o.address);
	E sta_zp(myTemp);
	E lda_ab(address + 1);
	E adc_ab(o.address + 1);
	E sta_zp(myTemp + 1);
	E lda_izp(myTemp);
	E sta_zp(ret.address);
	E ldy_imm(1);
	E lda_izy(myTemp);
	E sta_zp(ret.address + 1);
	return ret;
}
zp2 zp2::indexLoad2(const sadd1& o)
{
	zp2 ret(default_ret, ret_temp);
	E ldx_imm(0);
	E lda_ab(o.address);
	Label past;
	E bpl(past);
	E dex();
	past.here(&emulator);
	E clc();
	E adc_zp(address);
	E sta_zp(myTemp);
	E txa();
	E adc_zp(address + 1);
	E sta_zp(myTemp + 1);
	E lda_izp(myTemp);
	E sta_zp(ret.address);
	E ldy_imm(1);
	E lda_izy(myTemp);
	E sta_zp(ret.address + 1);
	return ret;
}
zp2 zp2::indexLoad2(const sadd2& o)
{
	zp2 ret(default_ret, ret_temp);
	E clc();
	E lda_ab(address);
	E adc_ab(o.address);
	E sta_zp(myTemp);
	E lda_ab(address + 1);
	E adc_ab(o.address + 1);
	E sta_zp(myTemp + 1);
	E lda_izp(myTemp);
	E sta_zp(ret.address);
	E ldy_imm(1);
	E lda_izy(myTemp);
	E sta_zp(ret.address + 1);
	return ret;
}
zp2 zp2::indexLoad2(acc o)
{
	zp2 ret(default_ret, ret_temp);
	o.toY();
	E lda_izy(address);
	E sta_zp(ret.address);
	E iny();
	E lda_izy(address);
	E sta_zp(ret.address + 1);
	return ret;
}
zp2 zp2::indexLoad2(sacc o)
{
	zp2 ret(default_ret, ret_temp);
	o.toY();
	E ldx_imm(0);
	E tya();
	Label past;
	E bpl(past);
	E dex();
	past.here(&emulator);
	E clc();
	E adc_zp(address);
	E sta_zp(myTemp);
	E txa();
	E adc_zp(address + 1);
	E sta_zp(myTemp + 1);
	E lda_izp(myTemp);
	E sta_zp(ret.address);
	E ldy_imm(1);
	E lda_izy(myTemp);
	E sta_zp(ret.address + 1);
	return ret;
}
zp2 zp2::indexLoad2(acc2 o)
{
	zp2 ret(default_ret, ret_temp);
	E clc();
	acc2 temp = o.r1toAandPreserve();
	E adc_ab(address);
	E sta_zp(myTemp);
	temp.r2toA();
	E adc_ab(address + 1);
	E sta_zp(myTemp + 1);
	E lda_izp(myTemp);
	E sta_zp(ret.address);
	E ldy_imm(1);
	E lda_izy(myTemp);
	E sta_zp(ret.address + 1);
	return ret;
}
zp2 zp2::indexLoad2(sacc2 o)
{
	zp2 ret(default_ret, ret_temp);
	E clc();
	sacc2 temp = o.r1toAandPreserve();
	E adc_ab(address);
	E sta_zp(myTemp);
	temp.r2toA();
	E adc_ab(address + 1);
	E sta_zp(myTemp + 1);
	E lda_izp(myTemp);
	E sta_zp(ret.address);
	E ldy_imm(1);
	E lda_izy(myTemp);
	E sta_zp(ret.address + 1);
	return ret;
}
zp2 zp2::indexLoad2(int o)
{
	zp2 ret(default_ret, ret_temp);
	if (o == 0) {
		E lda_izp(address);
		E sta_ab(ret.address);
		E ldy_imm(1);
		E lda_izy(address);
		E sta_ab(ret.address+1);
	}
	else if (o > 0 && o <= 254) {
		E ldy_imm(o);
		E lda_izy(address);
		E iny;
		E lda_izy(address);
		E sta_ab(ret.address + 1);
	}
	else {
		E lda_zp(address);
		E clc();
		E adc_imm(o & 0xff);
		E sta_zp(myTemp);
		E lda_zp(address + 1);
		E adc_imm((o >> 8) & 0xff);
		E lda_izp(address);
		E sta_ab(ret.address);
		E ldy_imm(1);
		E lda_izy(address);
		E sta_ab(ret.address + 1);
	}
	return ret;
}

zp2& zp2::indirectStore(const zp2& o)
{
	E ldy_imm(1);
	E lda_ab(o.address);
	E sta_izp(address);
	E lda_ab(o.address + 1);
	E sta_izy(address);
	return *this;
}
zp2& zp2::indirectStore(const zp1& o)
{
	E lda_ab(o.address);
	E sta_izp(address);
	return *this;
}
zp2& zp2::indirectStore(const szp2& o)
{
	E ldy_imm(1);
	E lda_ab(o.address);
	E sta_izp(address);
	E lda_ab(o.address + 1);
	E sta_izy(address);
	return *this;
}
zp2& zp2::indirectStore(const szp1& o)
{
	E lda_ab(o.address);
	E sta_izp(address);
	return *this;
}
zp2& zp2::indirectStore(const add1& o)
{
	E lda_ab(o.address);
	E sta_izp(address);
	return *this;
}
zp2& zp2::indirectStore(const add2& o)
{
	E ldy_imm(1);
	E lda_ab(o.address);
	E sta_izp(address);
	E lda_ab(o.address + 1);
	E sta_izy(address);
	return *this;
}
zp2& zp2::indirectStore(const sadd1& o)
{
	E lda_ab(o.address);
	E sta_izp(address);
	return *this;
}
zp2& zp2::indirectStore(const sadd2& o)
{
	E ldy_imm(1);
	E lda_ab(o.address);
	E sta_izp(address);
	E lda_ab(o.address + 1);
	E sta_izy(address);
	return *this;
}
zp2& zp2::indirectStore(acc o)
{
	o.toA();
	E sta_izp(address);
	return *this;
}
zp2& zp2::indirectStore(sacc o)
{
	o.toA();
	E sta_izp(address);
	return *this;
}
zp2& zp2::indirectStore(acc2 o)
{
	acc2 temp = o.r1toAandPreserve();
	E sta_izp(address);
	temp.r2toA();
	E ldy_imm(1);
	E sta_izy(address);
	return *this;
}
zp2& zp2::indirectStore(sacc2 o)
{
	sacc2 temp = o.r1toAandPreserve();
	E sta_izp(address);
	temp.r2toA();
	E ldy_imm(1);
	E sta_izy(address);
	return *this;
}
zp2& zp2::indirectStore(int o)
{
	if (o>=-128 && o<=255){
		E lda_imm(o&0xff);
		E sta_izp(address);
	}
	else {
		E lda_imm(o & 0xff);
		E sta_izp(address);
		E ldy_imm(1);
		E lda_imm(o >> 8);
		E sta_izy(address);
	}
	return *this;
}

zp2& zp2::operator =(zp2& o)
{
	E lda_ab(o.address);
	E sta_ab(address);
	E lda_ab(o.address+1);
	E sta_ab(address+1);
	return *this;
}
zp2& zp2::operator =(zp1& o)
{
	E lda_ab(o.address);
	E sta_ab(address);
	E stz_ab(address + 1);
	return *this;
}
zp2& zp2::operator =(szp2& o)
{

	E lda_ab(o.address);
	E sta_ab(address);
	E lda_ab(o.address + 1);
	E sta_ab(address + 1);
	return *this;
}
zp2& zp2::operator =(szp1& o)
{
	E ldx_imm(0);
	E lda_ab(o.address);
	E sta_ab(address);
	Label past;
	E bpl(past);
	E dex();
	past.here(&emulator);
	E stx_ab(address + 1);
	return *this;
}
zp2& zp2::operator =(add1& o)
{
	E lda_ab(o.address);
	E sta_ab(address);
	E stz_ab(address + 1);
	return *this;
}
zp2& zp2::operator =(add2& o)
{
	E lda_ab(o.address);
	E sta_ab(address);
	E lda_ab(o.address + 1);
	E sta_ab(address + 1);
	return *this;
}
zp2& zp2::operator =(sadd1& o)
{
	E ldx_imm(0);
	E lda_ab(o.address);
	E sta_ab(address);
	Label past;
	E bpl(past);
	E dex();
	past.here(&emulator);
	E stx_ab(address + 1);
	return *this;
}
zp2& zp2::operator =(sadd2& o)
{
	E lda_ab(o.address);
	E sta_ab(address);
	E lda_ab(o.address + 1);
	E sta_ab(address + 1);
}
zp2& zp2::operator =(acc o)
{
	o.toA();
	E sta_ab(address);
	E stz_ab(address + 1);
	return *this;
}
zp2& zp2::operator =(sacc o)
{
	switch (o.reg) {
	case REG_A:
		E ldx_imm(0);
		E sta_ab(address);
		E cmp_imm(0);
		break;
	case REG_X:
		E txa();
		E ldx_imm(0);
		E sta_ab(address);
		E cmp_imm(0);
		break;
	case REG_Y:
		E ldx_imm(0);
		E sty_ab(address);
		E cpy_imm(0);
		break;
	}
	Label past;
	E bpl(past);
	E dex();
	past.here(&emulator);
	E stx_ab(address + 1);
	return *this;
}
zp2& zp2::operator =(acc2 o)
{
	switch (o.r1) {
	case REG_A:
		E sta_ab(address);
		break;
	case REG_X:
		E stx_ab(address);
		break;
	case REG_Y:
		E sty_ab(address);
		break;
	}
	switch (o.r2) {
	case REG_A:
		E sta_ab(address+1);
		break;
	case REG_X:
		E stx_ab(address+1);
		break;
	case REG_Y:
		E sty_ab(address+1);
		break;
	}
	return *this;
}
zp2& zp2::operator =(sacc2 o)
{
	switch (o.r1) {
	case REG_A:
		E sta_ab(address);
		break;
	case REG_X:
		E stx_ab(address);
		break;
	case REG_Y:
		E sty_ab(address);
		break;
	}
	switch (o.r2) {
	case REG_A:
		E sta_ab(address + 1);
		break;
	case REG_X:
		E stx_ab(address + 1);
		break;
	case REG_Y:
		E sty_ab(address + 1);
		break;
	}
	return *this;
}
zp2& zp2::operator =(int o)
{
	if ((o&0xff) == 0) 
		E stz_ab(address);
	else { E lda_imm(o & 0xff); E sta_ab(address); }
	if (((o >> 8)&0xff) == 0)E stz_ab(address + 1);
	else { E lda_imm((o >> 8) & 0xff); E sta_ab(address+1); }
}

static void compare2bytesEq(do_compare*c, Label&l)
{
	Label failed;
	E lda_ab(c->address1);
	E cmp_ab(c->address2);
	E bne(failed);
	E lda_ab(c->address1+1);
	E cmp_ab(c->address2+1);
	E beq(l, c->force_short);
	failed.here(&emulator);
}

static void compare12bytesUUEq(do_compare*c, Label&l)
{
	Label failed;
	E lda_ab(c->address1+1);
	E bne(failed);
	E lda_ab(c->address1);
	E cmp_ab(c->address2);
	E beq(l, c->force_short);
	failed.here(&emulator);
}

static void compare12bytesSUEq(do_compare*c, Label&l)
{
	Label failed;
	E lda_ab(c->address1 + 1);
	E bne(failed);
	E lda_ab(c->address1);
	E bmi(failed);
	E cmp_ab(c->address2);
	E beq(l, c->force_short);
	failed.here(&emulator);
}


static void compare2bytesSUEq(do_compare*c, Label&l)
{
	Label failed;
	E lda_ab(c->address1+1);
	E bmi(failed);
	E cmp_ab(c->address2+1);
	E bne(failed);
	E lda_ab(c->address1);
	E cmp_ab(c->address2);
	E beq(l, c->force_short);
	failed.here(&emulator);
}
/***/
static void compare2bytesNeq(do_compare*c, Label&l)
{
	E lda_ab(c->address1);
	E cmp_ab(c->address2);
	E bne(l, c->force_short);
	E lda_ab(c->address1 + 1);
	E cmp_ab(c->address2 + 1);
	E bne(l, c->force_short);
}

static void compare12bytesUUNeq(do_compare*c, Label&l)
{
	E lda_ab(c->address1 + 1);
	E bne(l, c->force_short);
	E lda_ab(c->address1);
	E cmp_ab(c->address2);
	E bne(l, c->force_short);
}

static void compare12bytesSUNeq(do_compare*c, Label&l)
{
	E lda_ab(c->address1 + 1);
	E bne(l, c->force_short);
	E lda_ab(c->address1);
	E bmi(l, c->force_short);
	E cmp_ab(c->address2);
	E bne(l, c->force_short);
}


static void compare2bytesSUNeq(do_compare*c, Label&l)
{
	E lda_ab(c->address1 + 1);
	E bmi(l, c->force_short);
	E cmp_ab(c->address2 + 1);
	E bne(l, c->force_short);
	E lda_ab(c->address1);
	E cmp_ab(c->address2);
	E bne(l, c->force_short);
}

static void compare2bytesConstGT(do_compare*c, Label&l)
{
	if (c->literal < 0) {
		E bra(l, c->force_short);
	}
	else if (c->literal <= 0xffff) {
		{//bcc is LT
		 //bcs is GE 
			E lda_imm(c->literal & 0xff);
			E cmp_ab(c->address1);
			E lda_imm(c->literal >> 8);
			E sbc_ab(c->address1 + 1);
			E bcc(l, c->force_short);
		}
	}
}

static void compare2bytesConstLE(do_compare*c, Label&l)
{
	if (c->literal < 0) {
	}
	else if (c->literal <= 0xffff) {
		{//bcc is LT
		 //bcs is GE 
			E lda_imm(c->literal & 0xff);
			E cmp_ab(c->address1);
			E lda_imm(c->literal >> 8);
			E sbc_ab(c->address1 + 1);
			E bcs(l, c->force_short);
		}
	}
	else {
		E bra(l, c->force_short);
	}
}

static void compare2bytesConstGE(do_compare*c, Label&l)
{
	if (c->literal < 0) {
		E bra(l, c->force_short);
	}
	else if (c->literal <= 0xffff) {
		{//bcc is LT
		 //bcs is GE 
			E lda_ab(c->address1);
			E cmp_imm(c->literal & 0xff);
			E lda_ab(c->address1 + 1);
			E sbc_imm(c->literal >> 8);
			E bcs(l, c->force_short);
		}
	}
	else {
	}
}

static void compare2bytesConstLT(do_compare*c, Label&l)
{
	if (c->literal < 0) {
	}
	else if (c->literal <= 0xffff) {
		{//bcc is LT
		 //bcs is GE 
			E lda_ab(c->address1);
			E cmp_imm(c->literal & 0xff);
			E lda_ab(c->address1 + 1);
			E sbc_imm(c->literal >> 8);
			E bcc(l, c->force_short);
		}
	}
	else {
		E bra(l, c->force_short);
	}
}

static void compare2bytesConstEQ(do_compare*c, Label&l)
{
	if (c->literal < 0) {
	}
	else if (c->literal <= 0xffff) {
		{//bcc is LT
		 //bcs is GE
			Label fail;
			E lda_imm(c->literal & 0xff);
			E cmp_ab(c->address1);
			E bne(fail);
			E lda_imm(c->literal >> 8);
			E cmp_ab(c->address1 + 1);
			E beq(l, c->force_short);
			fail.here(&emulator);
		}
	}
	else {
	}
}

static void compare2bytesConstNE(do_compare*c, Label&l)
{
	if (c->literal < 0) {
		E bra(l, c->force_short);
	}
	else if (c->literal <= 0xffff) {
		{//bcc is LT
		 //bcs is GE
			E lda_imm(c->literal & 0xff);
			E cmp_ab(c->address1);
			E bne(l, c->force_short);
			E lda_imm(c->literal >> 8);
			E cmp_ab(c->address1 + 1);
			E bne(l, c->force_short);
		}
	}
	else {
		E bra(l, c->force_short);
	}
}

do_compare zp2::operator >(int o) const
{
	return do_compare(address, o, &compare2bytesConstGT);
}
do_compare zp2::operator >=(int o) const
{
	return do_compare(address, o, &compare2bytesConstGE);
}
do_compare zp2::operator <(int o) const
{
	return do_compare(address, o, &compare2bytesConstLT);
}
do_compare zp2::operator <=(int o) const
{
	return do_compare(address, o, &compare2bytesConstLE);
}
do_compare zp2::operator !=(int o) const
{
	return do_compare(address, o, &compare2bytesConstNE);
}
do_compare zp2::operator ==(int o) const
{
	return do_compare(address, o, &compare2bytesConstEQ);
}



do_compare zp2::operator ==(zp2& o) const
{
	return do_compare(address,o.address, &compare2bytesEq);
}

do_compare zp2::operator ==(zp1& o) const
{
	return do_compare(o.address, address, &compare12bytesUUEq);
}
do_compare zp2::operator ==(szp2& o) const
{
	return do_compare(o.address, address, &compare2bytesSUEq);
}
do_compare zp2::operator ==(szp1& o) const
{
	return do_compare(o.address, address, &compare12bytesSUEq);
}
do_compare zp2::operator ==(add1& o) const
{
	return do_compare(o.address, address, &compare12bytesUUEq);
}
do_compare zp2::operator ==(add2& o) const
{
	return do_compare(address, o.address, &compare2bytesEq);
}
do_compare zp2::operator ==(sadd1& o) const
{
	return do_compare(o.address, address, &compare12bytesSUEq);
}
do_compare zp2::operator ==(sadd2& o) const
{
	return do_compare(o.address, address, &compare2bytesSUEq);
}
do_compare zp2::operator ==(acc o) const
{
	o.store_zp(o.myTemp);
	return do_compare(o.myTemp, address, &compare12bytesUUEq);
}
do_compare zp2::operator ==(sacc o) const
{
	o.store_zp(o.myTemp);
	return do_compare(o.myTemp, address, &compare12bytesSUEq);
}
do_compare zp2::operator ==(acc2 o) const
{
	o.store_r1(o.myTemp);
	o.store_r2(o.myTemp+1);

	return do_compare(address, o.myTemp, &compare2bytesEq);
}

do_compare zp2::operator ==(sacc2 o) const
{
	o.store_r1(o.myTemp);
	o.store_r2(o.myTemp + 1);

	return do_compare(o.myTemp, address, &compare2bytesSUEq);
}

do_compare zp2::operator !=(zp2& o) const
{
	return do_compare(address, o.address, &compare2bytesNeq);
}
do_compare zp2::operator !=(zp1& o) const
{
	return do_compare(o.address, address, &compare12bytesUUNeq);
}
do_compare zp2::operator !=(szp2& o) const
{
	return do_compare(o.address, address, &compare2bytesSUNeq);
}
do_compare zp2::operator !=(szp1& o) const
{
	return do_compare(o.address, address, &compare12bytesSUNeq);
}
do_compare zp2::operator !=(add1& o) const
{
	return do_compare(o.address, address, &compare12bytesUUNeq);
}
do_compare zp2::operator !=(add2& o) const
{
	return do_compare(address, o.address, &compare2bytesNeq);
}
do_compare zp2::operator !=(sadd1& o) const
{
	return do_compare(o.address, address, &compare12bytesSUNeq);
}
do_compare zp2::operator !=(sadd2& o) const
{
	return do_compare(o.address, address, &compare2bytesSUNeq);
}
do_compare zp2::operator !=(acc o) const
{
	o.store_zp(o.myTemp);
	return do_compare(o.myTemp, address, &compare12bytesUUNeq);
}
do_compare zp2::operator !=(sacc o) const
{
	o.store_zp(o.myTemp);
	return do_compare(o.myTemp, address, &compare12bytesSUNeq);
}
do_compare zp2::operator !=(acc2 o) const
{
	o.store_r1(o.myTemp);
	o.store_r2(o.myTemp + 1);

	return do_compare(address, o.myTemp, &compare2bytesNeq);
}
do_compare zp2::operator !=(sacc2 o) const
{
	o.store_r1(o.myTemp);
	o.store_r2(o.myTemp + 1);

	return do_compare(o.myTemp, address, &compare2bytesSUNeq);
}

/*
a<n signed

sec
sbc n
bvc .1
eor #$80
.1 bmi lessthan

a>=n signed

sec
sbc n
bvc .1
eor #$80
.1 pl ge


a<=n
clc
sbc n
bvc .1
.1 bmi le

a>n
clc
sbc n
bvc .1
.1 bpl greaterthan

zero flag is trash

16 bit

num1<num2
		 LDA NUM1L ; NUM1-NUM2
		  CMP NUM2L
		  LDA NUM1H
		  SBC NUM2H
		  BVC LABEL ; N eor V
		  EOR #$80
	LABEL bmi lessthan

	num1>=num2
			 LDA NUM1L ; NUM1-NUM2
		  CMP NUM2L
		  LDA NUM1H
		  SBC NUM2H
		  BVC LABEL ; N eor V
		  EOR #$80
	LABEL bpl ge

24 bit num1<num2
		  LDA NUM1L ; NUM1-NUM2
		  CMP NUM2L
		  LDA NUM1M
		  SBC NUM2M
		  LDA NUM1H
		  SBC NUM2H
		  BVC LABEL ; N eor V
		  EOR #$80
	LABEL bmi lessthan


	*/

static void compare2bytesUULE(do_compare*c, Label&l)
{//bcc is LT
 //bcs is GE 
	E lda_ab(c->address2);
	E cmp_ab(c->address1);
	E lda_ab(c->address2+1);
	E sbc_ab(c->address1+1);
	E bcs(l, c->force_short);
}

static void compare21bytesUULE(do_compare*c, Label&l)
{//bcc is LT
 //bcs is GE 
	E lda_ab(c->address2);
	E cmp_ab(c->address1);
	E lda_imm(0);
	E sbc_ab(c->address1 + 1);
	E bcs(l, c->force_short);
}

static void compare2bytesUSLE(do_compare*c, Label&l)
{//bcc is LT
 //bcs is GE 
	E lda_ab(c->address2);
	E cmp_ab(c->address1);
	E lda_ab(c->address2 + 1);
	Label fail;
	E bmi(fail);
	E sbc_ab(c->address1 + 1);
	E bcs(l, c->force_short);
	fail.here(&emulator);
}
static void compare21bytesUSLE(do_compare*c, Label&l)
{//bcc is LT
 //bcs is GE 
	Label fail;
	E lda_ab(c->address1 + 1);
	E bne(fail);
	E lda_ab(c->address2);
	E bmi(fail);
	E cmp_ab(c->address1);
	E bcs(l, c->force_short);
	fail.here(&emulator);
}

do_compare zp2::operator <=(zp2& o) const
{
	return do_compare(address, o.address, &compare2bytesUULE);
}
do_compare zp2::operator <=(zp1& o) const
{
	return do_compare(address, o.address, &compare21bytesUULE);
}
do_compare zp2::operator <=(szp2& o) const
{
	return do_compare(address, o.address, &compare2bytesUSLE);
}
do_compare zp2::operator <=(szp1& o) const
{
	return do_compare(address, o.address, &compare21bytesUSLE);
}
do_compare zp2::operator <=(add1& o) const
{
	return do_compare(address, o.address, &compare21bytesUULE);
}
do_compare zp2::operator <=(add2& o) const
{
	return do_compare(address, o.address, &compare2bytesUULE);
}
do_compare zp2::operator <=(sadd1& o) const
{
	return do_compare(address, o.address, &compare21bytesUSLE);
}
do_compare zp2::operator <=(sadd2& o) const
{
	return do_compare(address, o.address, &compare2bytesUSLE);
}
do_compare zp2::operator <=(acc o) const
{
	o.store_zp(o.myTemp);
	return do_compare(address, o.myTemp, &compare21bytesUULE);
}
do_compare zp2::operator <=(sacc o) const
{
	o.store_zp(o.myTemp);
	return do_compare(address, o.myTemp, &compare21bytesUSLE);
}
do_compare zp2::operator <=(acc2 o) const
{
	o.store_r1(o.myTemp);
	o.store_r2(o.myTemp + 1);
	return do_compare(address, o.myTemp, &compare2bytesUULE);
}
do_compare zp2::operator <=(sacc2 o) const
{
	o.store_r1(o.myTemp);
	o.store_r2(o.myTemp + 1);
	return do_compare(address, o.myTemp, &compare2bytesUSLE);
}

static void compare2bytesUULT(do_compare*c, Label&l)
{//bcc is LT
 //bcs is GE 
	E lda_ab(c->address1);
	E cmp_ab(c->address2);
	E lda_ab(c->address1 + 1);
	E sbc_ab(c->address2 + 1);
	E bcc(l, c->force_short);
}

static void compare21bytesUULT(do_compare*c, Label&l)
{//bcc is LT
 //bcs is GE 
	E lda_ab(c->address1);
	E cmp_ab(c->address2);
	E lda_ab(c->address1 + 1);
	E sbc_imm(0);
	E bcc(l, c->force_short);
}

static void compare2bytesUSLT(do_compare*c, Label&l)
{//bcc is LT
 //bcs is GE 
	E lda_ab(c->address2 + 1);
	Label fail;
	E bmi(fail);
	E lda_ab(c->address1);
	E cmp_ab(c->address2);
	E lda_ab(c->address1 + 1);
	E sbc_ab(c->address2 + 1);
	E bcc(l, c->force_short);
	fail.here(&emulator);
}
static void compare21bytesUSLT(do_compare*c, Label&l)
{//bcc is LT
 //bcs is GE 
	Label fail;
	E lda_ab(c->address1 + 1);
	E bne(fail);
	E lda_ab(c->address2);
	E bmi(fail);
	E lda_ab(c->address1);
	E cmp_ab(c->address2);
	E bcc(l, c->force_short);
	fail.here(&emulator);
}


do_compare zp2::operator <(zp2& o) const
{
	return do_compare(address, o.address, &compare2bytesUULT);
}
do_compare zp2::operator <(zp1& o) const
{
	return do_compare(address, o.address, &compare21bytesUULT);
}
do_compare zp2::operator <(szp2& o) const
{
	return do_compare(address, o.address, &compare2bytesUSLT);
}
do_compare zp2::operator <(szp1& o) const
{
	return do_compare(address, o.address, &compare21bytesUSLT);
}
do_compare zp2::operator <(add1& o) const
{
	return do_compare(address, o.address, &compare21bytesUULT);
}
do_compare zp2::operator <(add2& o) const
{
	return do_compare(address, o.address, &compare2bytesUULT);
}
do_compare zp2::operator <(sadd1& o) const
{
	return do_compare(address, o.address, &compare21bytesUSLT);
}
do_compare zp2::operator <(sadd2& o) const
{
	return do_compare(address, o.address, &compare2bytesUSLT);
}
do_compare zp2::operator <(acc o) const
{
	o.store_zp(o.myTemp);
	return do_compare(address, o.myTemp, &compare21bytesUULT);
}
do_compare zp2::operator <(sacc o) const
{
	o.store_zp(o.myTemp);
	return do_compare(address, o.myTemp, &compare21bytesUSLT);
}
do_compare zp2::operator <(acc2 o) const
{
	o.store_r1(o.myTemp);
	o.store_r2(o.myTemp + 1);
	return do_compare(address, o.myTemp, &compare2bytesUULT);
}
do_compare zp2::operator <(sacc2 o) const
{
	o.store_r1(o.myTemp);
	o.store_r2(o.myTemp + 1);
	return do_compare(address, o.myTemp, &compare2bytesUSLT);
}
static void compare21bytesUUGE(do_compare*c, Label&l)
{//bcc is LT
 //bcs is GE 
	E lda_ab(c->address1);
	E cmp_ab(c->address2);
	E lda_ab(c->address1 + 1);
	E sbc_imm(0);
	E bcs(l, c->force_short);
}

static void compare2bytesUSGE(do_compare*c, Label&l)
{//bcc is LT
 //bcs is GE 
	E lda_ab(c->address2+1);
	E bmi(l, c->force_short);

	E lda_ab(c->address1);
	E cmp_ab(c->address2);
	E lda_ab(c->address1 + 1);
	E sbc_ab(c->address2 + 1);
	E bcs(l, c->force_short);
}

static void compare21bytesUSGE(do_compare*c, Label&l)
{//bcc is LT
 //bcs is GE 
	E lda_ab(c->address2);
	E bmi(l, c->force_short);
	E lda_ab(c->address1 + 1);
	E bne(l, c->force_short);

	E lda_ab(c->address1);
	E cmp_ab(c->address2);
	E bcs(l, c->force_short);
}

do_compare zp2::operator >=(zp2& o) const
{
	return do_compare(o.address, address, &compare2bytesUULE);
}
do_compare zp2::operator >=(zp1& o) const
{
	return do_compare(address, o.address, &compare21bytesUUGE);
}
do_compare zp2::operator >=(szp2& o) const
{
	return do_compare(address, o.address, &compare2bytesUSGE);
}
do_compare zp2::operator >=(szp1& o) const
{
	return do_compare(address, o.address, &compare21bytesUSGE);
}
do_compare zp2::operator >=(add1& o) const
{
	return do_compare(address, o.address, &compare21bytesUUGE);
}
do_compare zp2::operator >=(add2& o) const
{
	return do_compare(o.address, address, &compare2bytesUULE);
}
do_compare zp2::operator >=(sadd1& o) const
{
	return do_compare(address, o.address, &compare21bytesUSGE);
}
do_compare zp2::operator >=(sadd2& o) const
{
	return do_compare(address, o.address, &compare2bytesUSGE);
}
do_compare zp2::operator >=(acc o) const
{
	o.store_zp(o.myTemp);
	return do_compare(address, o.myTemp, &compare21bytesUUGE);
}
do_compare zp2::operator >=(sacc o) const
{
	o.store_zp(o.myTemp);
	return do_compare(address, o.myTemp, &compare21bytesUSGE);
}
do_compare zp2::operator >=(acc2 o) const
{
	o.store_r1(o.myTemp);
	o.store_r2(o.myTemp + 1);
	return do_compare(o.myTemp, address, &compare2bytesUULE);
}
do_compare zp2::operator >=(sacc2 o) const
{
	o.store_r1(o.myTemp);
	o.store_r2(o.myTemp + 1);
	return do_compare(address, o.myTemp, &compare2bytesUSGE);
}

static void compare21bytesUUGT(do_compare*c, Label&l)
{//bcc is LT
 //bcs is GE 
	E lda_ab(c->address2);
	E cmp_ab(c->address1);
	E lda_imm(0);
	E sbc_ab(c->address1 + 1);
	E bcc(l, c->force_short);
}

static void compare2bytesUSGT(do_compare*c, Label&l)
{//bcc is LT
 //bcs is GE 
	E lda_ab(c->address2);
	E cmp_ab(c->address1);
	E lda_ab(c->address2 + 1);
	E bmi(l, c->force_short);
	E sbc_ab(c->address1 + 1);
	E bcc(l, c->force_short);
}
static void compare21bytesUSGT(do_compare*c, Label&l)
{//bcc is LT
 //bcs is GE 
	E lda_ab(c->address1 + 1);
	E bne(l, c->force_short);
	E lda_ab(c->address2);
	E bmi(l, c->force_short);
	E cmp_ab(c->address1);
	E bcc(l, c->force_short);
}


do_compare zp2::operator >(zp2& o) const
{
	return do_compare(o.address, address, &compare2bytesUULT);
}
do_compare zp2::operator >(zp1& o) const
{
	return do_compare(address, o.address, &compare21bytesUUGT);
}
do_compare zp2::operator >(szp2& o) const
{
	return do_compare(address, o.address, &compare2bytesUSGT);
}
do_compare zp2::operator >(szp1& o) const
{
	return do_compare(address, o.address, &compare21bytesUSGT);
}
do_compare zp2::operator >(add1& o) const
{
	return do_compare(address, o.address, &compare21bytesUUGT);
}
do_compare zp2::operator >(add2& o) const
{
	return do_compare(o.address, address, &compare2bytesUULT);
}
do_compare zp2::operator >(sadd1& o) const
{
	return do_compare(address, o.address, &compare21bytesUSGT);
}
do_compare zp2::operator >(sadd2& o) const
{
	return do_compare(address, o.address, &compare2bytesUSGT);
}
do_compare zp2::operator >(acc o) const
{
	o.store_zp(o.myTemp);
	return do_compare(address, o.myTemp, &compare21bytesUUGT);
}
do_compare zp2::operator >(sacc o) const
{
	o.store_zp(o.myTemp);
	return do_compare(address, o.myTemp, &compare21bytesUSGT);
}
do_compare zp2::operator >(acc2 o) const
{
	o.store_r1(o.myTemp);
	o.store_r2(o.myTemp + 1);
	return do_compare(o.myTemp, address, &compare2bytesUULT);
}
do_compare zp2::operator >(sacc2 o) const
{
	o.store_r1(o.myTemp);
	o.store_r2(o.myTemp + 1);
	return do_compare(address, o.myTemp, &compare2bytesUSGT);
}


static uint8_t K2Ap[] = { 176,191,188,172,177,187,165,180,162,181,161,182,167,170,185,175,171,178,174,
163,184,190,179,189,183,173,164,168,223,153,220};

static uint8_t HC[] = {145,157,133,137,134,138,135,139,136,140,129};
static uint8_t HR[] = { 130,128,133,134,135,136,137,138,139,140,146 };
static uint8_t SC[] = { 222,186,192,169, };
static uint8_t SR[] = { 119,115,107, 93, };
//shift left by
//left 1: right 7, left 1
//right 1:
// logical right 1, left 7
// arith   arith right 1, left 7
//left 2:  rigtht 6, left 2
//logical right 2 
static uint8_t LShift1[256];
static uint8_t LShift2[256];
static uint8_t LShift3[256];
static uint8_t LShift4[256];
static uint8_t LShift5[256];
static uint8_t LShift6[256];
static uint8_t LShift7[256];

static uint8_t RShift1[256];
static uint8_t RShift2[256];
static uint8_t RShift3[256];
static uint8_t RShift4[256];
static uint8_t RShift5[256];
static uint8_t RShift6[256];
static uint8_t RShift7[256];

static uint8_t AShift1[256];
static uint8_t AShift2[256];
static uint8_t AShift3[256];
static uint8_t AShift4[256];
static uint8_t AShift5[256];
static uint8_t AShift6[256];
static uint8_t AShift7[256];

static uint8_t SquareLow[256];
static uint8_t SquareHigh[256];

uint16_t emulate65c02::build_solid()
{
	Label key2ascii, k2atable, hc,hr,sc,sr, ascii2screen, screen2ascii,teststart,ps;
	int zp_user_start = 0;
	int zp_basic_start = 0xa9;


	compile_point = 0x801;
	for (int i = 0; i < sizeof(header); ++i) comp_byte(header[i]);

	for (int i = 0; i < 256; ++i) {
		LShift1[i] = i << 1;
		LShift2[i] = i << 2;
		LShift3[i] = i << 3;
		LShift4[i] = i << 4;
		LShift5[i] = i << 5;
		LShift6[i] = i << 6;
		LShift7[i] = i << 7;

		RShift1[i] = i >> 1;
		RShift2[i] = i >> 2;
		RShift3[i] = i >> 3;
		RShift4[i] = i >> 4;
		RShift5[i] = i >> 5;
		RShift6[i] = i >> 6;
		RShift7[i] = i >> 7;

		AShift1[i] = (int8_t)i >> 1;
		AShift2[i] = (int8_t)i >> 2;
		AShift3[i] = (int8_t)i >> 3;
		AShift4[i] = (int8_t)i >> 4;
		AShift5[i] = (int8_t)i >> 5;
		AShift6[i] = (int8_t)i >> 6;
		AShift7[i] = (int8_t)i >> 7;

		SquareLow[i] = i * i & 0xff;
		SquareHigh[i] = i * i >> 8;
	}

	lshift1.here(this);
	for (int i = 0; i < 256; ++i)comp_byte(LShift1[i]);
	lshift2.here(this);
	for (int i = 0; i < 256; ++i)comp_byte(LShift2[i]);
	lshift3.here(this);
	for (int i = 0; i < 256; ++i)comp_byte(LShift3[i]);
	lshift4.here(this);
	for (int i = 0; i < 256; ++i)comp_byte(LShift4[i]);
	lshift5.here(this);
	for (int i = 0; i < 256; ++i)comp_byte(LShift5[i]);
	lshift6.here(this);
	for (int i = 0; i < 256; ++i)comp_byte(LShift6[i]);
	lshift7.here(this);
	for (int i = 0; i < 256; ++i)comp_byte(LShift7[i]);

	rshift1.here(this);
	for (int i = 0; i < 256; ++i)comp_byte(RShift1[i]);
	rshift2.here(this);
	for (int i = 0; i < 256; ++i)comp_byte(RShift2[i]);
	rshift3.here(this);
	for (int i = 0; i < 256; ++i)comp_byte(RShift3[i]);
	rshift4.here(this);
	for (int i = 0; i < 256; ++i)comp_byte(RShift4[i]);
	rshift5.here(this);
	for (int i = 0; i < 256; ++i)comp_byte(RShift5[i]);
	rshift6.here(this);
	for (int i = 0; i < 256; ++i)comp_byte(RShift6[i]);
	rshift7.here(this);
	for (int i = 0; i < 256; ++i)comp_byte(RShift7[i]);

	ashift1.here(this);
	for (int i = 0; i < 256; ++i)comp_byte(AShift1[i]);
	ashift2.here(this);
	for (int i = 0; i < 256; ++i)comp_byte(AShift2[i]);
	ashift3.here(this);
	for (int i = 0; i < 256; ++i)comp_byte(AShift3[i]);
	ashift4.here(this);
	for (int i = 0; i < 256; ++i)comp_byte(AShift4[i]);
	ashift5.here(this);
	for (int i = 0; i < 256; ++i)comp_byte(AShift5[i]);
	ashift6.here(this);
	for (int i = 0; i < 256; ++i)comp_byte(AShift6[i]);
	ashift7.here(this);
	for (int i = 0; i < 256; ++i)comp_byte(AShift7[i]);

	ashift_low.here(this);
	comp_byte(0);
	comp_byte(ashift1.target & 0xff);
	comp_byte(ashift2.target & 0xff);
	comp_byte(ashift3.target & 0xff);
	comp_byte(ashift4.target & 0xff);
	comp_byte(ashift5.target & 0xff);
	comp_byte(ashift6.target & 0xff);
	ashift_high.here(this);
	comp_byte(ashift7.target & 0xff);
	comp_byte(ashift1.target >> 8);
	comp_byte(ashift2.target >> 8);
	comp_byte(ashift3.target >> 8);
	comp_byte(ashift4.target >> 8);
	comp_byte(ashift5.target >> 8);
	comp_byte(ashift6.target >> 8);
	lshift_low.here(this);
	comp_byte(ashift7.target >> 8);
	comp_byte(lshift1.target & 0xff);
	comp_byte(lshift2.target & 0xff);
	comp_byte(lshift3.target & 0xff);
	comp_byte(lshift4.target & 0xff);
	comp_byte(lshift5.target & 0xff);
	comp_byte(lshift6.target & 0xff);
	lshift_high.here(this);
	comp_byte(lshift7.target & 0xff);
	comp_byte(lshift1.target >> 8);
	comp_byte(lshift2.target >> 8);
	comp_byte(lshift3.target >> 8);
	comp_byte(lshift4.target >> 8);
	comp_byte(lshift5.target >> 8);
	comp_byte(lshift6.target >> 8);
	rshift_low.here(this);
	comp_byte(lshift7.target >> 8);
	comp_byte(rshift1.target & 0xff);
	comp_byte(rshift2.target & 0xff);
	comp_byte(rshift3.target & 0xff);
	comp_byte(rshift4.target & 0xff);
	comp_byte(rshift5.target & 0xff);
	comp_byte(rshift6.target & 0xff);
	rshift_high.here(this);
	comp_byte(rshift7.target & 0xff);
	comp_byte(rshift1.target >> 8);
	comp_byte(rshift2.target >> 8);
	comp_byte(rshift3.target >> 8);
	comp_byte(rshift4.target >> 8);
	comp_byte(rshift5.target >> 8);
	comp_byte(rshift6.target >> 8);
	lshift_rev_high.here(this);
	comp_byte(rshift7.target >> 8);
	comp_byte(lshift7.target >> 8);
	comp_byte(lshift6.target >> 8);
	comp_byte(lshift5.target >> 8);
	comp_byte(lshift4.target >> 8);
	comp_byte(lshift3.target >> 8);
	comp_byte(lshift2.target >> 8);
	lshift_rev_low.here(this);
	comp_byte(lshift1.target >> 8);
	comp_byte(lshift7.target & 0xff);
	comp_byte(lshift6.target & 0xff);
	comp_byte(lshift5.target & 0xff);
	comp_byte(lshift4.target & 0xff);
	comp_byte(lshift3.target & 0xff);
	comp_byte(lshift2.target & 0xff);
	comp_byte(lshift1.target & 0xff);

	squarelow.here(this);
	for (int i = 0; i < 256; ++i)comp_byte(SquareLow[i]);
	squarehigh.here(this);
	for (int i = 0; i < 256; ++i)comp_byte(SquareHigh[i]);

	//a*b =
	/* works for signed too, just put an abs on the first square too
	   even combinations work if you handle the signs correctly
	   combinations would be done with 2 byte signed addition
	square((a+b)>>1)-square(abs(a-b)>>1)
	+(((a^b)&1)?b:0)
	*/

	k2atable.here(this);
	for (int i = 0; i < sizeof(K2Ap); ++i) comp_byte(K2Ap[i]);
	hc.here(this);
	for (int i = 0; i < sizeof(HC); ++i) comp_byte(HC[i]);
	hr.here(this);
	for (int i = 0; i < sizeof(HR); ++i) comp_byte(HR[i]);
	sc.here(this);
	for (int i = 0; i < sizeof(SC); ++i) comp_byte(SC[i]);
	sr.here(this);
	for (int i = 0; i < sizeof(SR); ++i) comp_byte(SR[i]);
	compile_point = (compile_point + 0xff) & 0xff00;
	pass_type.here();
	compile_point += 0x100;
	pass_0.here();
	compile_point += 0x100;
	pass_1.here();
	compile_point += 0x100;
	pass_2.here();
	compile_point += 0x100;
	pass_3.here();
	compile_point += 0x100;
	pass_4.here();
	compile_point += 0x100;
	pass_5.here();
	compile_point += 0x100;
	pass_6.here();


	//input a, output a
	key2ascii.here(this);
	cmp_imm(141);
	beq(ps);
	cmp_imm(65);
	Label notlowpassthrough,notlc,pt,o95,nomansland,under;
	bge(notlowpassthrough);
	cmp_imm(32);
	bge(ps);
	cmp_imm(46);//del
	Label __a, __b, __c, __d, __e,__f,__g;
	bne(__a);
	lda_imm(127);
	rts();
	__a.here(this);
	cmp_imm(17);//down
	bne(__b);
	lda_imm(131);
	rts();
	__b.here(this);
	cmp_imm(29);//right
	bne(__c);
	lda_imm(129);
	rts();
	__c.here(this);
	cmp_imm(21);//f10
	bne(__d);
	lda_imm(143);
	rts();
	__d.here(this);
	cmp_imm(22);//f11
	bne(__e);
	lda_imm(144);
	rts();
	__e.here(this);
	cmp_imm(23);//f12
	bne(__f);
	lda_imm(145);
	rts();
	__f.here(this);
	cmp_imm(19);//f12
	bne(__g);
	lda_imm(146);
	rts();
	__g.here(this);
	cmp_imm(13);
	beq(ps);
	cmp_imm(9);
	beq(ps);
	cmp_imm(20);
	beq(ps);
	cmp_imm(9);
	beq(ps);
	lda_imm(0);
ps.here(this);
	rts();//0-64, passthrough
	notlowpassthrough.here(this);
	cmp_imm(91);
	bge(notlc);//bcs
	adc_imm(32);//65-90 (a-z) becomes 97-122
	rts();
	notlc.here(this);
	cmp_imm(95);//95 ` becomes 96
	blt(ps);//91-94 passthrough
	bne(o95);
	inc();
	rts();
o95.here(this);
	cmp_imm(193);
	blt(nomansland);
	cmp_imm(219);
	bge(nomansland);
	eor_imm(128);
	rts();//A-Z 193-218 -> 65-90
nomansland.here(this);
	cmp_imm(221);
	Label __1, __2, __3, __4, __5, __6,__7,__8,__9,__A,__B;
	bne(__1);
	lda_imm(95);
	rts();
	__1.here(this);
	cmp_imm(186);
	bne(__2);
	lda_imm(123);
	rts();
	__2.here(this);
	cmp_imm(169);
	bne(__3);
	lda_imm(124);
	rts();
	__3.here(this);
	cmp_imm(192);
	bne(__4);
	lda_imm(125);
	rts();
	__4.here(this);
	cmp_imm(222);
	bne(__5);
	lda_imm(126);
	rts();
	__5.here(this);
	cmp_imm(148);
	bne(__6);
	lda_imm(127);
	rts();
	__6.here(this);
	ldx_imm(0);
	__7.here(this);
	cmp_abx(k2atable);
	bne(__8);
	inx();
	txa();
	rts();
	__8.here(this);
	inx();
	cpx_imm(sizeof(K2Ap));
	bne(__7);
	ldx_imm(0);
	__9.here(this);
	cmp_abx(hc);
	bne(__A);
	lda_abx(hr);
	rts();
	__A.here(this);
	inx();
	cpx_imm(sizeof(HC));
	bne(__9);
	rts();
	__B.here(this);
	rts();

	Label zret,_1,_2,_3,_9,_A;
	zret.here(this);
	lda_imm(0);
	rts();
ascii2screen.here(this);
	cmp_imm(32);
	blt(zret);
	cmp_imm(127);
	bge(zret);
	cmp_imm(96);
	bge(_1);
	cmp_imm(65);
	blt(__B);
	//carry is set
	sbc_imm(64);
	rts();
	_1.here(this);
	cmp_imm(219);
	bge(_3);
	cmp_imm(193);
	blt(__B);
	//carry is set
	sbc_imm(128);
	rts();
	_3.here(this);

	ldx_imm(0);
	_9.here(this);
	cmp_abx(sc);
	bne(_A);
	lda_abx(sr);
	rts();
	_A.here(this);
	inx();
	cpx_imm(sizeof(SC));
	bne(_9);
	lda_imm(0);
	rts();

//96-192 186{(123) 192}(125)
//169 |(124)
//145 up, 157 left, c-v 115, f1-f8, 133-140, c1 144, c4 159,c5 156, c8 158,  c0 146
//ac188, av190, ax189, aa180, ae 177, au 172, ad 172, ai 162, abs/sbs 148, 
	return compile_point;
}

#else
#define TEST_VLOAD

#ifdef TEST_VLOAD

uint16_t emulate65c02::build_solid()
{
	Label starttest, spriteFileName, setlfs, setname, load, andmask;

	setlfs.set_target(this, 0xffba);
	setname.set_target(this, 0xffbd);
	load.set_target(this, 0xffd5);
	andmask.set_target(this, 0xc5);
	const char filename[] = "testfile.txt";

	compile_point = 0x801;
	for (int i = 0; i < sizeof(header); ++i) comp_byte(header[i]);
	jmp(starttest);
	spriteFileName.here();
	int i = 0;
	do {
		comp_byte(filename[i]);
	} while (0 != filename[i++]);
	starttest.here();
	lda_imm(0);
	sta_ab(0x9F60);// enable the KERNAL to be sure sure
	lda_imm(1);
	ldx_imm(1);
	ldy_imm(0);// custom address
	jsr(setlfs);
	lda_imm(sizeof(spriteFileName) - 1);//not counting trailing 0 in this test
	ldx_imm(spriteFileName.target & 0xff);// these values match what basic makes
	ldy_imm(spriteFileName.target >> 8);
	jsr(setname);
	lda_imm(1);
	sta_ab(andmask);// BANK 1
	lda_imm(2);// VLOAD
	ldx_imm(0);
	ldy_imm(0);// $0000 VRAM
	jsr(load);
	stp();
	trace = true;
	exec(starttest.target);
	return 0;
}

#else

uint16_t emulate65c02::build_solid()
{

	const int MLEN1 = 44;
	const int MLAG1 = 14;
	const int MLEN2 = 24;
	const int MLAG2 = 23;

	Label rngstate1, rngstate2, rngcarry2, rngi2, rngs2, rngseed1, rngseed2;
//	Label rngi1, rngs1, rngcarry1, rngcache;
	Label conditionshiftseedgen, shft3left, shft3right, rot3right, eor3shftright, eor3shftleft, seedshiftreggen;
	Label seedrandom, rndbyte, rndbytevalue, rnd_mask_table, starttest, dotest, dorndbytevalue, dorndwordvalue,  rndwordvalue;
	Label t1, t2, t3, t4, t5, t6, t7, a1, a2, a3, a4, a5, b1, b2, b3, b4, mff, m7f, m3f;
	Label c1, c2, c3, c4, c5, c6, cmff, cm7f, cm3f;

	/* ******************************************************************
	The only variables that HAVE to be on zero page are rngseed1tmp-rngseed5tmp and those are only used in initialization.
	The rest that are on zero page are only there for speed, feel free to take them off of zero page.

	Note that on entry to seedrandom(), rngseed1tmp and rngseed3tmp each hold 3 bytes each of a seed.

	 ******************************************************************** */
	const int rngseed1tmp = 0x2, rngseed2tmp = 0x5, rngseed3tmp = 0x8, rngseed4tmp = 0xb, rngseed5tmp = 0xe,
		rngtmp = 0x10, rngtmp2 = 0x11, rngtmp3 = 0x12, rngtmp4 = 0x14;
	const int rngi1 = 0x15, rngs1 = 0x16, rngcarry1 = 0x17, rngcache = 0x18;


	compile_point = 0x801;
	for (int i = 0; i < sizeof(header); ++i) comp_byte(header[i]);
	jmp(starttest);
	rngseed1.here();
	comp_byte(0x0b);
	comp_byte(0xb0);
	comp_byte(0xef);
	rngseed2.here();
	comp_byte(0xbe);
	comp_byte(0xad);
	comp_byte(0xde);
	rngstate1.here();
	compile_point += MLEN1;
	rngstate2.here();
	compile_point += MLEN2;
//	rngcache.here();
//	compile_point += 1;
//	rngcarry1.here();
//	compile_point += 1;
	rngcarry2.here();
	compile_point += 1;
//	rngi1.here();
//	compile_point += 1;
//	rngs1.here();
//	compile_point += 1;
	rngi2.here();
	compile_point += 1;
	rngs2.here();
	compile_point += 1;
	rngi2.here();
	compile_point += 1;

	//randomize the seed input bits a little so that
//inputs like 1 or 2 won't be kind of poor
conditionshiftseedgen.here();
	lda_imm(0xb5);
	eor_zp(rngseed1tmp);
	sta_zp(rngseed1tmp);
	lda_imm(0xb0);
	eor_zp(rngseed1tmp + 1);
	sta_zp(rngseed1tmp + 1);
	lda_imm(0xcc);
	eor_zp(rngseed1tmp + 2);
	sta_zp(rngseed1tmp + 2);
	ora_zp(rngseed1tmp + 1);
	ora_zp(rngseed1tmp);	//if seed is 0 then load something else
	bne(t1);
	lda_imm(0xf3);
	sta_zp(rngseed1tmp);
	sta_zp(rngseed1tmp + 1);
	sta_zp(rngseed1tmp + 2);
t1.here();
	lda_imm(0xc7);
	eor_zp(rngseed2tmp);
	sta_zp(rngseed2tmp);
	lda_imm(0x49);
	eor_zp(rngseed2tmp + 1);
	sta_zp(rngseed2tmp + 1);
	lda_imm(0xd2);
	eor_zp(rngseed2tmp + 2);
	sta_zp(rngseed2tmp + 2);
	ora_zp(rngseed2tmp + 1);
	ora_zp(rngseed2tmp);
	bne(t2);
	lda_imm(0x5a);
	sta_zp(rngseed2tmp);
	sta_zp(rngseed2tmp + 1);
	sta_zp(rngseed2tmp + 2);
t2.here();
	rts();

	//input address in x()
	//number of bits to shift in y
shft3left.here();
t3.here();
	asl_zpx(0);
	rol_zpx(1);
	rol_zpx(2);
	dey();
	bne(t3);
	rts();
	//input address in (x)
	//number of bits to shift in y
shft3right.here();
t4.here();
	lsr_zpx(2);
	ror_zpx(1);
	ror_zpx(0);
	dey();
	bne(t4);
	rts();

	//input address in (x)
	//number of bits to shift in y
rot3right.here();
	lda_zpx(0);
	lsr();
	ror_zpx(2);
	ror_zpx(1);
	ror_zpx(0);
	dey();
	bne(rot3right);
	rts();


	//do one tap
	//num bits to shift in y
	//input/output (x)
	//uses_zp(rngseed3tmp
eor3shftright.here();
	lda_zpx(0);
	sta_zp(rngseed3tmp);
	lda_zpx(1);
	sta_zp(rngseed3tmp + 1);
	lda_zpx(2);
	sta_zp(rngseed3tmp + 2);
	jsr(shft3right);
	lda_zpx(0);
	eor_zp(rngseed3tmp);
	sta_zpx(0);
	lda_zpx(1);
	eor_zp(rngseed3tmp + 1);
	sta_zpx(1);
	lda_zpx(2);
	eor_zp(rngseed3tmp + 2);
	sta_zpx(2);
	rts();

	//do one tap
	//num bits to shift in x
	//input/output (shiftparam)
	//uses_zp(rngseed3tmp
eor3shftleft.here();
	lda_zpx(0);
	sta_zp(rngseed3tmp);
	lda_zpx(1);
	sta_zp(rngseed3tmp + 1);
	lda_zpx(2);
	sta_zp(rngseed3tmp + 2);
	jsr(shft3left);
	lda_zpx(0);
	eor_zp(rngseed3tmp);
	sta_zpx(0);
	lda_zpx(1);
	eor_zp(rngseed3tmp + 1);
	sta_zpx(1);
	lda_zpx(2);
	eor_zp(rngseed3tmp + 2);
	sta_zpx(2);
	rts();

	//generate next initialization byte
	//state in_zp(rngseed1tmp,rngseed2tmp
	//output in a
	//uses_zp(rngseed1tmp-rngseed5tmp
	//does not touch x,y
seedshiftreggen.here();
	phx();
	phy();

	ldx_imm(rngseed1tmp);

	ldy_imm(3);
	jsr(eor3shftleft);
	ldy_imm(15);
	jsr(eor3shftright);
	ldy_imm(11);
	jsr(eor3shftleft);

	lda_zp(rngseed1tmp);
	sta_zp(rngseed4tmp);
	lda_zp(rngseed1tmp + 1);
	sta_zp(rngseed4tmp + 1);
	lda_zp(rngseed1tmp + 2);
	sta_zp(rngseed4tmp + 2);

	ldx_imm(rngseed4tmp);
	ldy_imm(6);
	jsr(eor3shftleft);
	ldy_imm(1);
	jsr(eor3shftright);
	ldy_imm(9);
	jsr(eor3shftleft);

	ldx_imm(rngseed2tmp);

	ldy_imm(13);
	jsr(eor3shftleft);
	ldy_imm(5);
	jsr(eor3shftright);
	ldy_imm(8);
	jsr(eor3shftleft);

	lda_zp(rngseed2tmp);
	sta_zp(rngseed5tmp);
	lda_zp(rngseed2tmp + 1);
	sta_zp(rngseed5tmp + 1);
	lda_zp(rngseed2tmp + 2);
	sta_zp(rngseed5tmp + 2);

	ldx_imm(rngseed5tmp);
	ldy_imm(11);
	jsr(eor3shftleft);
	ldy_imm(1);
	jsr(eor3shftright);
	ldy_imm(8);
	jsr(eor3shftleft);

	clc();
	lda_zp(rngseed5tmp);
	adc_zp(rngseed4tmp);
	sta_zp(rngseed5tmp);
	lda_zp(rngseed5tmp + 1);
	adc_zp(rngseed4tmp + 1);
	sta_zp(rngseed5tmp + 1);
	lda_zp(rngseed5tmp + 2);
	adc_zp(rngseed4tmp + 2);
	sta_zp(rngseed5tmp + 2);

	ldy_imm(4);
	jsr(shft3right);
	lda_zp(rngseed5tmp + 1);

	ply();
	plx();
	rts();


	// initialize seed 
	// input: rngseed1,rngseed2
	// output state in:
	// 	rngstate1-rngstate3, rngcarry1-rngcarry3,rngi1-rngi3, rngs1-rngs3, rngcache
	// uses all registers
seedrandom.here();
	jsr(conditionshiftseedgen);
	ldx_imm(MLEN1);
	ldy_imm(0);
t5.here();
	jsr(seedshiftreggen);
	sta_aby(rngstate1);
	iny();
	dex();
	bne(t5);
	ldx_imm(MLEN2);
	ldy_imm(0);
t6.here();
	jsr(seedshiftreggen);
	sta_aby(rngstate2);
	iny();
	dex();
	bne(t6);
	stz_ab(rngi1);
	stz_ab(rngi2);
	stz_ab(rngcache);
	//initializing the carries isn't necessary but except for testing
	stz_ab(rngcarry2);
	stz_ab(rngcarry1);

	lda_imm(MLAG1);
	sta_ab(rngs1);
	lda_imm(MLAG2);
	sta_ab(rngs2);

	//x is still 0, do 256 warm up
	ldx_imm(0);
t7.here();
	jsr(rndbyte);
	dex();
	bne(t7);
rnd_mask_table.here();//one byte early because 0th element is don't care
	rts();	//0
			//1
	comp_byte(1);
			//2,3
	comp_byte(3); comp_byte(3);
			//4,5,6,7
	comp_byte(7); comp_byte(7); comp_byte(7); comp_byte(7);
			//8,9,10,11,12,13,14,15,
	comp_byte(15); comp_byte(15); comp_byte(15); comp_byte(15);
	comp_byte(15); comp_byte(15); comp_byte(15); comp_byte(15);
			//16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
	comp_byte(31); comp_byte(31); comp_byte(31); comp_byte(31);
	comp_byte(31); comp_byte(31); comp_byte(31); comp_byte(31);
	comp_byte(31); comp_byte(31); comp_byte(31); comp_byte(31);
	comp_byte(31); comp_byte(31); comp_byte(31); comp_byte(31);
	//the important random function
	//generate one random byte 
	//output in a
	//changes state in 
	// 	rngstate1-rngstate2, rngcarry1-rngcarry2,rngi1-rngi2, rngs1-rngs2, rngcache
	//  does not effect x and y
rndbyte.here();

	phx();
	dec_ab(rngi1);
	bmi(a2);
	dec_ab(rngs1);
	bmi(a3);
a1.here();
	lsr_ab(rngcarry1);
	ldx_ab(rngs1);
	lda_abx(rngstate1);
	ldx_ab(rngi1);
	adc_abx(rngstate1);
	sta_abx(rngstate1);
	rol_ab(rngcarry1); //save the carry
	eor_ab(rngcache);

	plx();
	rts();
a2.here();
	lda_imm(MLEN1 - 1);
	sta_ab(rngi1);
	dec_ab(rngs1);
	bpl(a1);
a3.here();
	lda_imm(MLEN1 - 1);
	sta_ab(rngs1);
	dec_ab(rngi2);
	bpl(a4);
	lda_imm(MLEN2 - 1);
	sta_ab(rngi2);
a4.here();
	dec_ab(rngs2);
	bpl(a5);
	lda_imm(MLEN2 - 1);
	sta_ab(rngs2);
a5.here();
	lsr_ab(rngcarry2);
	ldx_ab(rngs2);
	lda_abx(rngstate2);
	ldx_ab(rngi2);
	adc_abx(rngstate2);
	sta_abx(rngstate2);
	rol_ab(rngcarry2); //save the carry
	sta_ab(rngcache);
	bra(a1);

	//return a uniform deviate from 0 to a in a
	//does not preserve y
	//uses zero page rngtmp and rngtmp as well as the addresses used by rndbyte
	//worst case number averages about 158 cycles per call.
rndbytevalue.here();
	tay();
	beq(b1); //if the range is 0 to 0 then just return 0
	inc(); 
	beq(rndbyte,false); //if the range is up to 255, no need for testing, just call rndbyte
	sta_zp(rngtmp2); //number to compare against after rndbyte is incremented because there's no branch less than
	sty_zp(rngtmp);
	lda_imm(0x20);
	bit_zp(rngtmp); //bit is clever because it tests three things, top bit, second bit and third bit at once
	bmi(mff);//high bit, mask is ff
	bvs(m7f);//second bit mask is 7f
	bne(m3f);//third bit mask is 3f
b2.here();
	lda_aby(rnd_mask_table);
b4.here();
	sta_zp(rngtmp);
b3.here();
	jsr(rndbyte);
	and_zp(rngtmp);
	cmp_zp(rngtmp2);
	bcs(b3);
b1.here();
	rts();
mff.here();
	lda_imm(0xff);
	bra(b4);
m7f.here();
	lda_imm(0x7f);
	bra(b4);
m3f.here();
	lda_imm(0x3f);
	bra(b4);

	//return a uniform deviate from 0 to a,x in a,x (where a is the high byte)
	//does not preserve y
	//uses zero page rngtmp and rngtmp as well as the addresses used by rndbyte
rndwordvalue.here();
	tay();
	beq(c1);
	sta_zp(rngtmp);//high for mask making
	txa();
	clc();
	adc_imm(1);
	sta_zp(rngtmp3);//low for compare, incremented
	tya();
	adc_imm(0);
	bcs(c6);//special case, compare # overflowed, but that's ok because 65535 can just be two calls with no compare
	sta_zp(rngtmp2);//high for compare (part of incremented value)

	lda_imm(0x20);
	bit_zp(rngtmp); //bit is clever because it tests three things, top bit, second bit and third bit at once
	bmi(cmff);//high bit, mask is ff
	bvs(cm7f);//second bit mask is 7f
	bne(cm3f);//third bit mask is 3f
c2.here();
	lda_aby(rnd_mask_table);
c4.here();
	sta_zp(rngtmp);
c3.here();
	jsr(rndbyte);
	sta_zp(rngtmp4);
	jsr(rndbyte);
	and_zp(rngtmp);
	tax();
	lda_zp(rngtmp4);
	cmp_zp(rngtmp3);
	txa();
	sbc_zp(rngtmp2);
	bcs(c3);
	txa();
	ldx_zp(rngtmp4);
	rts();
c1.here();
	txa();
	jsr(rndbytevalue);
	tax();
	lda_imm(0);
	rts();
cmff.here();
	lda_imm(0xff);
	bra(c4);
cm7f.here();
	lda_imm(0x7f);
	bra(c4);
cm3f.here();
	lda_imm(0x3f);
	bra(c4);

c5.here();
	txa();
	jsr(rndwordvalue);
	tax();
	lda_imm(0);
	rts();
c6.here();//special case 65535
	jsr(rndbyte);
	tax();
	jmp(rndbyte);

starttest.here();
	lda_ab(rngseed1);
	sta_zp(rngseed1tmp);
	lda_ab(rngseed1.target+1);
	sta_zp(rngseed1tmp+1);
	lda_ab(rngseed1.target+2);
	sta_zp(rngseed1tmp+2);
	lda_ab(rngseed2);
	sta_zp(rngseed2tmp);
	lda_ab(rngseed2.target + 1);
	sta_zp(rngseed2tmp + 1);
	lda_ab(rngseed2.target + 2);
	sta_zp(rngseed2tmp + 2);
	jsr(seedrandom);
	stp();
dotest.here();
	jsr(rndbyte);
	stp();
dorndbytevalue.here();
	jsr(rndbytevalue);
	stp();
dorndwordvalue.here();
	jsr(rndwordvalue);
	stp();

	protected_start = conditionshiftseedgen.target;
	protected_end = compile_point;

	exec(starttest.target);
	for (int i = 0; i < 100; ++i) {
		exec(dotest.target);

		int v = a;
		std::cout << std::hex << (v < 16 ? "0" : "") << v << ' ';
		if ((i + 1) % 20 == 0) std::cout << '\n';
	}
	for (;;) {
		int tabulate[65536];
		for (int i = 0; i < 65536; ++i) tabulate[i] = 0;
		std::cout << "menu\n enter 1 to test random byte\n enter 2 to test random byte up to n\n enter 3 to test random word up to n\n: " << std::dec;
		int what;
		std::cin >> what;
		if (what < 1 || what>3) continue;
		int limit;
		long long timestart = time;
		switch (what) {
		case 1://byte
			limit = 255;
			for (long long i = (limit + 1) * 1000ll; i > 0; --i) {
				exec(dotest.target);
				timestart += 3;//don't count the stop
				int v = a;
				if (v > limit || v < 0) {//<0 is out of range of the emulator but I test
					std::cout << "value over limit.  Expected up to " << limit << " got " << v << "\n";
					break;
				}
				++tabulate[v];
			}
			break;
		case 2://byte up to value
			std::cout << "enter deviate limit from 0 to 255: " << std::dec;
			std::cin >> limit;
			if (limit < 0) break;
			if (limit > 255) continue;
			for (long long i = (limit + 1) * 1000ll; i > 0; --i) {
				a = limit;
				exec(dorndbytevalue.target);
				timestart += 3;//don't count the stop
				int v = a;
				if (v > limit || v < 0) {//<0 is out of range of the emulator but I test
					std::cout << "value over limit.  Expected up to " << limit << " got " << v << "\n";
					break;
				}
				++tabulate[v];
			}
			break;
		case 3://word up to value
			std::cout << "enter deviate limit from 0 to 65535 : " << std::dec;
			std::cin >> limit;
			if (limit < 0) break;
			if (limit > 65535) continue;
			for (long long i = (limit + 1) * 1000ll; i > 0; --i) {
				a = limit >> 8;
				x = limit & 0xff;
				exec(dorndwordvalue.target);
				timestart += 3;//don't count the stop
				int v = x + (a << 8);
				if (v > limit || v < 0) {//<0 is out of range of the emulator but I test
					std::cout << "value over limit.  Expected up to " << limit << " got " << v << "\n";
					break;
				}
				++tabulate[v];
			}
		}


		for (int i = 0; i <= limit; ++i) {
			std::cout << "# @ " << i << " = " << tabulate[i] << "\n";
		}
		std::cout << "done! Cycles per call " << (time - timestart) / (1000.0*(limit + 1)) << "\n";
	}
//	FILE* file = fopen("testrnd.prg", "wb");
//	fwrite(map_addr(0x7ff), 1, compile_point - 0x7ff, file);
//	fclose(file);

	return compile_point;
}
#endif
#endif
