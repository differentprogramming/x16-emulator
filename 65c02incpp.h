#pragma once
#include <list>
#include <memory>
#include <iostream>

//#define TRACE
//#define PERFSTAT
#define LOAD_HYPERCALLS

#define RAM_SIZE (0xa000 + num_ram_banks * 8192) /* $0000-$9FFF + banks at $A000-$BFFF */
#define ROM_SIZE (NUM_ROM_BANKS * 16384)   /* banks at $C000-$FFFF */

typedef enum {
	ECHO_MODE_NONE,
	ECHO_MODE_RAW,
	ECHO_MODE_COOKED,
	ECHO_MODE_ISO,
} echo_mode_t;

// GIF recorder commands
typedef enum {
	RECORD_GIF_PAUSE,
	RECORD_GIF_SNAP,
	RECORD_GIF_RESUME
} gif_recorder_command_t;

// GIF recorder states
typedef enum {
	RECORD_GIF_DISABLED,
	RECORD_GIF_PAUSED,
	RECORD_GIF_SINGLE,
	RECORD_GIF_ACTIVE
} gif_recorder_state_t;


extern uint16_t num_ram_banks;

extern bool debugger_enabled;
extern bool log_video;
extern bool log_keyboard;
extern echo_mode_t echo_mode;
extern bool save_on_exit;
extern gif_recorder_state_t record_gif;
extern char *gif_path;
extern uint8_t keymap;

extern void machine_dump();
extern void machine_reset();
extern void machine_paste(char *);
extern void init_audio();


struct emulate65c02;
typedef void INSTRUCTION(emulate65c02 *);
enum FLAGS {
	FLAG_C = 1,
	FLAG_Z = 2,
	FLAG_I = 4,
	FLAG_D = 8,
	FLAG_B = 16,
	FLAG_V = 64,
	FLAG_N = 128
};

const int DEVICE_EMULATOR = 0x9fb0;
#define NUM_MAX_RAM_BANKS 256
#define NUM_ROM_BANKS 8


enum WRITE_MODES { READ_MODE,WRITE_MODE,MODIFY_MODE, NUM_WRITE_MODES };
enum ADDRESSING_MODES { IMM, IZX, IZY, ZP, ABS, IZP, ABY, ABX, ZPX, ZPY, NUM_ADDRESSING_MODES};
enum JMP_MODES { JABS, JIND, JIAX };

extern int ADDR_DELAY[NUM_WRITE_MODES*NUM_ADDRESSING_MODES];

struct emulate65c02;
struct LabelFixup
{
	bool relative;
	int  instruction_field_address;
	int target;
	LabelFixup():instruction_field_address(-1), target(-1), relative(false){}
	LabelFixup(int f, bool r) :instruction_field_address(f), relative(r), target(-1) {}
	LabelFixup(const LabelFixup &) = default;
	LabelFixup(LabelFixup &&) = default;
	void  update_target(emulate65c02 *emulate, int t);
};

struct Label
{
	int target;
	Label() :target(-1), fixups(new std::list<LabelFixup>){}
	Label(const Label&) = default;
	Label(Label&&) = default;
	std::shared_ptr<std::list<LabelFixup> > fixups;

	bool has_target() { return target != -1; }
	void set_target(emulate65c02 *emulate, int t = -1);
	void add_fixup(int instruction_field_address, bool relative) {
		fixups->push_back(LabelFixup(instruction_field_address, relative));
	}
};

extern int RMB_BY_BIT[8];
extern int BBR_BY_BIT[8];

struct emulate65c02 {
	enum dissassembly_modes
	{
		imp, imm, zp, zpx, zpy, abs, abx, aby, izx, izy, izp, rel, zpr, ind, iax
	};
	int a, x, y, p, s;//empty stack decending
	int pc;

	//current bank
	uint8_t ram_bank;
	uint8_t rom_bank;

	int num_banks;
	uint8_t *memory; //RAM in emulator
	uint8_t *rom;	 //ROM in emulator


	int compile_point;
	int data_point;
	long long time;
	long long clockgoal6502;
	bool waiting;
	bool stop;
	
	int last_address;
	bool trace;

	bool callexternal;
	void(*loopexternal)();


	int	effective_ram_bank()
	{
		return ram_bank % num_banks;
	}
	//
// saves the memory content into a file
// - not in use

	void
		memory_save(FILE *f, bool dump_ram, bool dump_bank)
	{
		if (dump_ram) {
			fwrite(&memory[0], sizeof(uint8_t), 0xa000, f);
		}
		if (dump_bank) {
			fwrite(&memory[0xa000], sizeof(uint8_t), (num_banks * 8192), f);
		}
	}

	///
	///
	///

	void
		memory_set_ram_bank(uint8_t bank)//global
	{
		ram_bank = bank & (NUM_MAX_RAM_BANKS - 1);
	}

	uint8_t
		memory_get_ram_bank()//global
	{
		return ram_bank;
	}

	void
		memory_set_rom_bank(uint8_t bank)//global
	{
		rom_bank = bank & (NUM_ROM_BANKS - 1);;
	}

	uint8_t
		memory_get_rom_bank()//global
	{
		return rom_bank;
	}

	// Control the GIF recorder
	void
		emu_recorder_set(gif_recorder_command_t command)
	{
		// turning off while recording is enabled
		if (command == RECORD_GIF_PAUSE && record_gif != RECORD_GIF_DISABLED) {
			record_gif = RECORD_GIF_PAUSED; // need to save
		}
		// turning on continuous recording
		if (command == RECORD_GIF_RESUME && record_gif != RECORD_GIF_DISABLED) {
			record_gif = RECORD_GIF_ACTIVE;		// activate recording
		}
		// capture one frame
		if (command == RECORD_GIF_SNAP && record_gif != RECORD_GIF_DISABLED) {
			record_gif = RECORD_GIF_SINGLE;		// single-shot
		}
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
	void emu_write(uint8_t reg, uint8_t value)
	{
		bool v = value != 0;
		switch (reg) {
		case 0: debugger_enabled = v; break;
		case 1: log_video = v; break;
		case 2: log_keyboard = v; break;
		case 3: echo_mode = (echo_mode_t)value; break;
		case 4: save_on_exit = v; break;
		case 5: emu_recorder_set((gif_recorder_command_t)value); break;
		default: printf("WARN: Write Invalid register %x\n", DEVICE_EMULATOR + reg);
		}
	}

	uint8_t
		emu_read(uint8_t reg)
	{
		if (reg == 0) {
			return debugger_enabled ? 1 : 0;
		}
		else if (reg == 1) {
			return log_video ? 1 : 0;
		}
		else if (reg == 2) {
			return log_keyboard ? 1 : 0;
		}
		else if (reg == 3) {
			return echo_mode;
		}
		else if (reg == 4) {
			return save_on_exit ? 1 : 0;
		}
		else if (reg == 5) {
			return record_gif;
		}
		else if (reg == 13) {
			return keymap;
		}
		else if (reg == 14) {
			return '1'; // emulator detection
		}
		else if (reg == 15) {
			return '6'; // emulator detection
		}
		printf("WARN: Read Invalid register %x\n", DEVICE_EMULATOR + reg);
		return 0xff;
	}

	
	void exec6502(uint32_t tickcount) {
		clockgoal6502 += tickcount;
		stop = waiting = false;

		while (time < clockgoal6502) {
			instructions[read6502(pc++)](this);

			if (callexternal) (*loopexternal)();
			if (stop || waiting) time = clockgoal6502;
		}
	}

	void step6502() {

		if (trace) {
			disassembly_point = pc;
			std::cout << disassemble();
		}
		instructions[read6502(pc)](this);
		if (trace) {
			std::cout << std::hex << "\ta=" << a << " x=" << x << " y=" << y << " pc=" << pc << " (C" <<
				((p&(FLAG_C)) == 0 ? 0 : 1)
				<< 'Z' << ((p&(FLAG_Z)) == 0 ? 0 : 1)
				<< 'I' << ((p&(FLAG_I)) == 0 ? 0 : 1)
				<< 'D' << ((p&(FLAG_D)) == 0 ? 0 : 1)
				<< 'B' << ((p&(FLAG_B)) == 0 ? 0 : 1)
				<< 'V' << ((p&(FLAG_V)) == 0 ? 0 : 1)
				<< 'N' << ((p&(FLAG_N)) == 0 ? 0 : 1)
				<< ") s= " << s << '\n';
		}
		++pc;
		clockgoal6502 = time;

		if (callexternal) (*loopexternal)();
	}

	typedef void fntype(void);
	void hookexternal(void *funcptr) {
		if (funcptr != (void *)NULL) {
			loopexternal = (fntype*)funcptr;
			callexternal = true;
		}
		else callexternal = false;
	}

	long long test_execute(int addr)
	{
		long long t = time;
		stop = waiting = false;
		pc = addr;
		for (;;) {
			if (trace) {
				disassembly_point = pc;
				std::cout << disassemble();
			}
			instructions[read6502(pc)](this);
			if (trace) {
				std::cout << std::hex<<"\ta="<<a<<" x="<<x<<" y="<<y<<" pc="<<pc<<" (C"<<
					((p&(FLAG_C))==0?0:1)
					<< 'Z'<< ((p&(FLAG_Z)) == 0 ? 0 : 1)
					<< 'I' << ((p&(FLAG_I)) == 0 ? 0 : 1)
					<< 'D' << ((p&(FLAG_D)) == 0 ? 0 : 1)
					<< 'B' << ((p&(FLAG_B)) == 0 ? 0 : 1)
					<< 'V' << ((p&(FLAG_V)) == 0 ? 0 : 1)
					<< 'N' << ((p&(FLAG_N)) == 0 ? 0 : 1)
					<<") s= "<<s<<'\n';
			}
			++pc;
			if (waiting || stop) break;
		}
		return time - t;
	}

	//for JMP and JSR
	void decode_jmp(JMP_MODES jmode)
	{
		time += 3;
		int add;
		add = deref_abs(pc+1);
		switch (jmode)
		{
		case JABS:
			pc = (add - 1) & 0xffff;
			break;
		case JIND:
			pc = (deref_abs(add) - 1) & 0xffff;
			time += 2;
			break;
		case JIAX:
			pc = (deref_abs(add+x) - 1) & 0xffff;
			time += 3;
		}
	}
	int decode_branch(int *if_taken)
	{
		time += 2;
		int add;
		pc = ((pc + 1) & 0xffff);
		add = read6502(pc);
		if (0 != (add & 0x80)) add |= 0xffffff00;
		time += 2;
		if (0 != (((pc - 1) ^ (pc + add + 1)) & 0x100)) *if_taken = 2;
		else *if_taken = 1;
		return (pc + add ) & 0xffff;
	}
	
	uint8_t decode_addr_readIMM();
	uint8_t decode_addr_readIZX();
	uint8_t decode_addr_readIZY();
	uint8_t decode_addr_readIZP();
	uint8_t decode_addr_readABS();
	uint8_t decode_addr_readZP();
	uint8_t decode_addr_readABY();
	uint8_t decode_addr_readABX();
	uint8_t decode_addr_readZPY();
	uint8_t decode_addr_readZPX();
	uint8_t decode_addr_read(ADDRESSING_MODES am);
	void decode_addr_write(ADDRESSING_MODES am, uint8_t v);
	void decode_addr_writeIZX(uint8_t v);
	void decode_addr_writeIZY(uint8_t v);
	void decode_addr_writeIZP(uint8_t v);
	void decode_addr_writeABS(uint8_t v);
	void decode_addr_writeZP(uint8_t v);
	void decode_addr_writeABY(uint8_t v);
	void decode_addr_writeABX(uint8_t v);
	void decode_addr_writeZPY(uint8_t v);
	void decode_addr_writeZPX(uint8_t v);
	int decode_addr_modify(ADDRESSING_MODES am);
	int decode_addr_modifyZP();
	int decode_addr_modifyABX();
	int decode_addr_modifyZPX();
	int decode_addr_modifyABS();


	static int sign_extend(int n)
	{
		if (0 != (n & 0x80))return n | 0xffffff00;
		return n&0xff;
	}
	void do_adc(int u)
	{
		int s = sign_extend(u);
		int sa = sign_extend(a);
		u &= 0xff;
		a &= 0xff;
		const int c=(p&(int)FLAG_C);

		int r =a + c + u;
		sa += c + s;

		if (0 != (p&FLAG_D)) {
			++time;
			if ((u&0xf)+(a&0xf)+c>=0xa) {
				r += 0x6;
			}
			if (r >= 0xa0) {
				r += 0x60;
			}
		}

		if (0 != (r & 0x100)) p |= (int)FLAG_C;
		else p &= ~(int)FLAG_C;

		if ((0 != (sa & 0x80000000)) != (0 != (sa & 0x00000080)))p |= (int)FLAG_V;
		else p &= ~(int)FLAG_V;
		a = r & 0xff;
		test_for_N(test_for_Z(a));
	}

	void carry_from_shift_bit(int v)
	{
		if (0 != v) p |= (int)FLAG_C;
		else p &= ~(int)FLAG_C;
	}

	void do_sbc(int u)
	{
		if (0 != (p&FLAG_D)) return do_adc(0x99 - u); return do_adc(u ^ 0xff);
	}

	void do_cmp(int o, int u)
	{
		u &= 0xff;
		o &= 0xff;
	
		o -= u;

		if (0 == (o & 0x100)) p |= (int)FLAG_C;
		else p &= ~(int)FLAG_C;

		o &= 0xff;
		test_for_N(test_for_Z(o));
	}

	uint8_t real_read6502(uint16_t address, bool debugOn, uint8_t bank);
	void write6502(uint16_t address, uint8_t value);

	uint8_t read6502(uint16_t address) {
		return real_read6502(address, false, 0);
	}

	//uint8_t* map_addr(int addr)
	//{
	//	last_address = addr;
		//add bank switching later
	//	return &memory[addr & 0xffff];
	//}
	int deref_abs(int addr)
	{
		return read6502(addr) + (read6502(addr + 1) << 8);
	}
	int deref_zp(int addr)
	{
		addr &= 0xff;
		int ret = read6502(addr) + (read6502((addr + 1)&0xff) << 8);
		return ret;
	}
	static int stack_mask(int stack)
	{
		return (stack&0xff)+0x100;
	}
	int deref_stack(int addr)
	{
		if (0 == (addr&0x1) || 0 != (addr & ~0x1ff)) throw "derefed non-stack address as stack";
		return read6502(addr) + (read6502(stack_mask(addr + 1)) << 8);
	}
	void set_mem(int addr, int v)
	{
		write6502(addr,(uint8_t)v);
	}
	uint8_t get_mem(int addr)
	{
		return read6502(addr);
	}

	void push16(uint16_t pushval) {
		write6502(0x100 + s, (pushval >> 8) & 0xFF);
		write6502(0x100 + ((s - 1) & 0xFF), pushval & 0xFF);
		s -= 2;
	}

	void push8(uint8_t pushval) {
		write6502(0x100 + s--, pushval);
	}

	void push_byte(int v)
	{
		set_mem(s-- + 0x100, v);
		s &= 0xff;
	}
	void push_word(int v) {
		push_byte(v>>8);
		push_byte(v);
	}
	uint8_t pop_byte()
	{
		s= (s+1 & 0xff);
		return get_mem(s + 0x100);
	}
	uint16_t pop_word()
	{
		uint8_t t = pop_byte();
		return (pop_byte() << 8) + t;
	}
	uint16_t zp_add(int i) {
		return get_mem(i) + (get_mem((i + 1) & 0xff) << 8);
	}

	void init(int kb)
	{
		a = 0;
		x = 0;
		y = 0;
		p = (int)FLAG_I;
		s = 0xff;
		pc = 0x100;
		compile_point = 0x100;
		data_point = 0x8000;
		waiting = false;
		stop = false;
		last_address = -1;
		disassembly_point = 0;
		external_disassembly_point = nullptr;
		trace = false;
		num_banks = kb / 8192;
		memory = (uint8_t*)malloc(0xa000 + num_banks * 8192);
		rom = (uint8_t*)malloc(8 * 16384);
		callexternal = false;
	}

	emulate65c02() :a(0), x(0), y(0), p((int)FLAG_I), s(0xff), pc(0x100),compile_point(0x100), data_point(0x8000),
		waiting(false), stop(false),last_address(-1),
		disassembly_point(0), external_disassembly_point(nullptr), trace(false),rom_bank(0), ram_bank(0),num_banks(0),memory(nullptr), rom(nullptr), callexternal(false)
	{
	}

	~emulate65c02() {
		free(memory);
		free(rom);
	}
	uint8_t dis_deref()
	{
		if (external_disassembly_point == nullptr) return read6502(disassembly_point++);
		else return external_disassembly_point[disassembly_point++];
	}
	int word_dis_deref()
	{
		uint8_t low = dis_deref();
		return low + (dis_deref() << 8);
	}
	void comp_ab_label(Label &label, uint8_t code)
	{
		comp_byte(code);
		if (label.has_target()) {
			comp_word(label.target);
		}
		else {
			label.add_fixup(compile_point, false);
			comp_word((compile_point + 2) & 0xffff);
		}
	}
	char *disassemble();
	void comp_byte(int v) { memory[compile_point++] = (uint8_t)v; }
	void comp_word(int v) { comp_byte(v & 0x0ff); comp_byte(v>>8); }

	void brk() { comp_byte(0); }
	void ora_izx(int v) { comp_byte(1); comp_byte(v); }
	//won't be an entry for every nop
	void nop_imm(int v) { comp_byte(2); comp_byte(v); }
	void nop() { comp_byte(0xea); }//chosen because it's a 6502 nop too
	void tsb_zp(int v) { comp_byte(4); comp_byte(v); }
	void ora_zp(int v) { comp_byte(5); comp_byte(v); }
	void asl_zp(int v) { comp_byte(6); comp_byte(v); }
	void rmb(int bit, int zp) 
	{
		comp_byte(RMB_BY_BIT[bit]);
		comp_byte(zp);
	}
	void php() { comp_byte(8); }
	void ora_imm(int v) { comp_byte(9); comp_byte(v); }
	void asl() { comp_byte(0x0a); }
	void tsb_abs(int v) { comp_byte(0x0c); comp_word(v); }
	void ora_abs(int v) { comp_byte(0x0d); comp_word(v); }
	void asl_abs(int v) { comp_byte(0x0e); comp_word(v); }
	void bbr(int bit, int zp)//needs address too
	{
		comp_byte(BBR_BY_BIT[bit]);
		comp_byte(zp);
	}
	inline void setcarry() {
		p |= (int)FLAG_C;
	}
	inline void clearcarry() {
		p &= (~(int)FLAG_C);
	}
	inline void setzero() {
		p |= (int)FLAG_Z;
	}
	inline void clearzero() {
		p &= (~(int)FLAG_Z);
	}
	inline void setinterrupt() {
		p |= (int)FLAG_I;
	}
	inline void clearinterrupt() {
		p &= (~(int)FLAG_I);
	}
	inline void setdecimal() {
		p |= (int)FLAG_D;
	}
	inline void cleardecimal() {
		p &= (~(int)FLAG_D);
	}
	inline void setoverflow() {
		p |= (int)FLAG_V;
	}
	inline void clearoverflow() {
		p &= (~(int)FLAG_V);
	}
	inline void setsign() {
		p |= (int)FLAG_N;
	}
	inline void clearsign() {
		p &= (~(int)FLAG_N);
	}

	//flag calculation funcitons
	inline void zerocalc(int n) {
		if ((n) & 0x00FF) clearzero();
		else setzero();
	}

	inline void signcalc(int n) {
		if ((n) & 0x0080) setsign();
		else clearsign();
	}

	inline void carrycalc(int n) {
		if ((n) & 0xFF00) setcarry();
		else clearcarry();
	}

	inline void overflowcalc(int n, int m, int o) { /* n = result, m = accumulator, o = memory */
		if (((n) ^ (uint16_t)(m)) & ((n) ^ (o)) & 0x0080) setoverflow();
		else clearoverflow();
	}

	//if branch address is known then it either branches short (if it can reach) otherwise
	//if it can't reach it synthesizes a long branch
	//if the target isn't known it reserves space for a short if force_short is true
	//otherwise it reserves space for a long branch and adds a fixup
	void compile_branch(int branch, int inverted_branch, Label& label, bool force_short) {
		int cur_add = (compile_point + 2) & 0xffff;
		if (label.has_target() && label.target - cur_add <= 128 && cur_add - label.target <= 127) {
			comp_byte(branch);
			comp_byte(label.target - cur_add);
		}
		else if (force_short) {
			label.add_fixup((cur_add - 1) & 0xffff, true);
			comp_byte(branch);
			comp_byte(0);
		}
		else {
			label.add_fixup((cur_add + 1) & 0xffff, false);
			comp_byte(inverted_branch); //the code is designed so that if you try to run it when the fixup hasn't happened, the branch falls through
			comp_byte(3);//over jump
			comp_byte(0x4c);//jmp
			cur_add = (compile_point + 2) & 0xffff;
			comp_byte(cur_add & 0xff);
			comp_byte((cur_add >> 8) & 0xff);
		}
	}

	void bpl(Label& label, bool force_short=false) {
		compile_branch(0x10, 0x30, label, force_short);
	}
	void ora_izy(int v) { comp_byte(0x11); comp_byte(v); }
	void ora_izp(int v) { comp_byte(0x12); comp_byte(v); }
	void trb_zp(int v) { comp_byte(0x14); comp_byte(v); }
	void ora_zpx(int v){ comp_byte(0x15); comp_byte(v); }
	void asl_zpx(int v) { comp_byte(0x16); comp_byte(v); }
	void clc() { comp_byte(0x18); }
	void ora_aby(int v) { comp_byte(0x19); comp_word(v); }
	void ora_aby(Label &label) { comp_ab_label(label, 0x19); }
	void inc() { comp_byte(0x1a); }
	void trb_abs(int v) { comp_byte(0x1c); comp_word(v); }
	void trb_abs(Label &label) { comp_ab_label(label, 0x1c); }
	void ora_abx(int v) { comp_byte(0x1d); comp_word(v); }
	void ora_abx(Label &label) { comp_ab_label(label, 0x1d); }
	void asl_abx(int v) { comp_byte(0x1e); comp_word(v); }
	void asl_abx(Label &label) { comp_ab_label(label, 0x1e); }
	void jsr(Label& label) { comp_ab_label(label, 0x20); }
	void and_izx(int v) { comp_byte(0x21); comp_byte(v); }
	void bit_zp(int v) { comp_byte(0x24); comp_byte(v); }
	void and_zp(int v) { comp_byte(0x25); comp_byte(v); }
	void rol_zp(int v) { comp_byte(0x26); comp_byte(v); }
	void plp() { comp_byte(0x28); }
	void and_imm(int v) { comp_byte(0x29); comp_byte(v); }
	void rol() { comp_byte(0x2a); }
	void bit_abs(int v) { comp_byte(0x2c); comp_word(v); }
	void bit_abs(Label &label) { comp_ab_label(label, 0x2c); }
	void and_abs(int v) { comp_byte(0x2d); comp_word(v); }
	void and_abs(Label &label) { comp_ab_label(label, 0x2d); }
	void rol_abs(int v) { comp_byte(0x2e); comp_word(v); }
	void rol_abs(Label &label) { comp_ab_label(label, 0x2e); }
	void bmi(Label& label, bool force_short = false) {
		compile_branch(0x30, 0x10, label, force_short);
	}
	void and_izy(int v) { comp_byte(0x31); comp_byte(v); }
	void and_izp(int v) { comp_byte(0x32); comp_byte(v); }
	void bit_zpx(int v) { comp_byte(0x34); comp_byte(v); }
	void and_zpx(int v){ comp_byte(0x35); comp_byte(v); }
	void rol_zpx(int v){ comp_byte(0x36); comp_byte(v); }
	void sec(){ comp_byte(0x38); }
	void and_aby(int v){ comp_byte(0x39); comp_word(v); }
	void and_aby(Label &label) { comp_ab_label(label, 0x39); }
	void dec(){ comp_byte(0x3a); }
	void bit_abx(int v){ comp_byte(0x3c); comp_word(v); }
	void bit_abx(Label &label) { comp_ab_label(label, 0x3c); }
	void and_abx(int v){ comp_byte(0x3d); comp_word(v); }
	void and_abx(Label &label) { comp_ab_label(label, 0x3d); }
	void rol_abx(int v){ comp_byte(0x3e); comp_word(v); }
	void rol_abx(Label &label) { comp_ab_label(label, 0x3e); }
	void rti(){ comp_byte(0x40); }
	void eor_izx(int v){ comp_byte(0x41); comp_byte(v); }
	void eor_zp(int v){ comp_byte(0x45); comp_byte(v); }
	void lsr_zp(int v){ comp_byte(0x46); comp_byte(v); }
	void pha(){ comp_byte(0x48); }
	void eor_imm(int v){ comp_byte(0x49); comp_byte(v); }
	void lsr(){ comp_byte(0x4a); }
	void jmp_abs(int v){ comp_byte(0x4c); comp_word(v); }
	void jmp(Label &label)  { comp_ab_label(label, 0x4c); }
	void eor_abs(int v){ comp_byte(0x4d); comp_word(v); }
	void eor_abs(Label &label) { comp_ab_label(label, 0x4d); }
	void lsr_abs(int v){ comp_byte(0x4e); comp_word(v); }
	void lsr_abs(Label &label) { comp_ab_label(label, 0x4e); }
	void bvc(Label &label, bool force_short=false){ compile_branch(0x50, 0x70, label, force_short); }
	void eor_izy(int v){ comp_byte(0x51); comp_byte(v); }
	void eor_izp(int v){ comp_byte(0x52); comp_byte(v); }
	void eor_zpx(int v){ comp_byte(0x55); comp_byte(v); }
	void lsr_zpx(int v){ comp_byte(0x56); comp_byte(v); }
	void cli(){ comp_byte(0x58); }
	void eor_aby(int v){ comp_byte(0x59); comp_word(v); }
	void eor_aby(Label &label) { comp_ab_label(label, 0x59); }
	void phy(){ comp_byte(0x5a); }
	void eor_abx(int v){ comp_byte(0x5d); comp_word(v); }
	void eor_abx(Label &label) { comp_ab_label(label, 0x5d); }
	void lsr_abx(int v){ comp_byte(0x5e); comp_word(v); }
	void lsr_abx(Label &label) { comp_ab_label(label, 0x5e); }
	void rts(){ comp_byte(0x60); }
	void adc_izx(int v){ comp_byte(0x61); comp_byte(v); }
	void stz_zp(int v){ comp_byte(0x64); comp_byte(v); }
	void adc_zp(int v){ comp_byte(0x65); comp_byte(v); }
	void ror_zp(int v){ comp_byte(0x66); comp_byte(v); }
	void pla(){ comp_byte(0x68); }
	void adc_imm(int v){ comp_byte(0x69); comp_byte(v); }
	void ror(){ comp_byte(0x6a); }
	void jmp_ind(int v){ comp_byte(0x6c); comp_word(v); }
	void adc_abs(int v){ comp_byte(0x6d); comp_word(v); }
	void adc_abs(Label &label) { comp_ab_label(label, 0x6d); }
	void ror_abs(int v){ comp_byte(0x6e); comp_word(v); }
	void ror_abs(Label &label) { comp_ab_label(label, 0x6e); }
	void bvs(Label &label, bool force_short = false) { compile_branch(0x70, 0x50, label, force_short); }
	void adc_izy(int v){ comp_byte(0x71); comp_byte(v); }
	void adc_izp(int v){ comp_byte(0x72); comp_byte(v); }
	void stz_zpx(int v){ comp_byte(0x74); comp_byte(v); }
	void adc_zpx(int v){ comp_byte(0x75); comp_byte(v); }
	void ror_zpx(int v){ comp_byte(0x76); comp_byte(v); }
	void sei(){ comp_byte(0x78); }
	void adc_aby(int v){ comp_byte(0x79); comp_word(v); }
	void adc_aby(Label &label) { comp_ab_label(label, 0x79); }
	void ply(){ comp_byte(0x7a); }
	void jmp_iax(int v){ comp_byte(0x7c); comp_word(v); }
	void adc_abx(int v){ comp_byte(0x7d); comp_word(v); }
	void adc_abx(Label &label) { comp_ab_label(label, 0x7d); }
	void ror_abx(int v){ comp_byte(0x7e); comp_word(v); }
	void ror_abx(Label &label) { comp_ab_label(label, 0x7e); }
	void bra(Label& label, bool force_short=false) {
		int cur_add = (compile_point + 2) & 0xffff;
		if (label.has_target() && label.target - cur_add <= 128 && cur_add - label.target <= 127) {
			comp_byte(0x80);
			comp_byte(label.target - cur_add);
		}
		else if (force_short) {
			label.add_fixup((cur_add - 1) & 0xffff, true);
			comp_byte(0x80);
			comp_byte(0);
		}
		else {
			comp_byte(0x4c);//jmp
			label.add_fixup(compile_point, false);
			comp_word((compile_point + 2) & 0xffff);
		}
	}
	void sta_izx(int v){ comp_byte(0x81); comp_byte(v); }
	void sty_zp(int v){ comp_byte(0x84); comp_byte(v); }
	void sta_zp(int v){ comp_byte(0x85); comp_byte(v); }
	void stx_zp(int v){ comp_byte(0x86); comp_byte(v); }
	void dey(){ comp_byte(0x88); }
	void bit_imm(int v){ comp_byte(0x89); comp_byte(v); }
	void txa(){ comp_byte(0x8a); }
	void sty_abs(int v){ comp_byte(0x8c); comp_word(v); }
	void sty_abs(Label &label) { comp_ab_label(label, 0x8c); }
	void sta_abs(int v){ comp_byte(0x8d); comp_word(v); }
	void sta_abs(Label &label) { comp_ab_label(label, 0x8d); }
	void stx_abs(int v){ comp_byte(0x8e); comp_word(v); }
	void stx_abs(Label &label) { comp_ab_label(label, 0x8e); }
	void bcc(Label &label, bool force_short = false) { compile_branch(0x90, 0xb0, label, force_short); }
	void sta_izy(int v){ comp_byte(0x91); comp_byte(v); }
	void sta_izp(int v){ comp_byte(0x92); comp_byte(v); }
	void sty_zpx(int v){ comp_byte(0x94); comp_byte(v); }
	void sta_zpx(int v){ comp_byte(0x95); comp_byte(v); }
	void stx_zpy(int v){ comp_byte(0x96); comp_byte(v); }
	void tya(){ comp_byte(0x98); }
	void sta_aby(int v){ comp_byte(0x99); comp_word(v); }
	void sta_aby(Label& label) { comp_ab_label(label, 0x99); }

	void txs(){ comp_byte(0x9a); }
	void stz_abs(int v){ comp_byte(0x9c); comp_word(v); }
	void stz_abs(Label &label) { comp_ab_label(label, 0x9c); }
	void sta_abx(int v){ comp_byte(0x9d); comp_word(v); }
	void sta_abx(Label& label) { comp_ab_label(label, 0x9d); }

	void stz_abx(int v){ comp_byte(0x9e); comp_word(v); }
	void stz_abx(Label &label) { comp_ab_label(label, 0x9e); }
	void ldy_imm(int v){ comp_byte(0xa0); comp_byte(v); }
	void lda_izx(int v){ comp_byte(0xa1); comp_byte(v); }
	void ldx_imm(int v){ comp_byte(0xa2); comp_byte(v); }
	void ldy_zp(int v){ comp_byte(0xa4); comp_byte(v); }
	void lda_zp(int v){ comp_byte(0xa5); comp_byte(v); }
	void ldx_zp(int v){ comp_byte(0xa6); comp_byte(v); }
	void tay(){ comp_byte(0xa8); }
	void lda_imm(int v){ comp_byte(0xa9); comp_byte(v); }
	void tax(){ comp_byte(0xaa); }
	void ldy_abs(int v){ comp_byte(0xac); comp_word(v); }
	void ldy_abs(Label &label) { comp_ab_label(label, 0xac); }
	void lda_abs(int v){ comp_byte(0xad); comp_word(v); }
	void lda_abs(Label &label) { comp_ab_label(label, 0xad); }
	void ldx_abs(int v){ comp_byte(0xae); comp_word(v); }
	void ldx_abs(Label &label) { comp_ab_label(label, 0xae); }
	void bcs(Label &label, bool force_short = false) { compile_branch(0xb0, 0x90, label, force_short); }
	void lda_izy(int v){ comp_byte(0xb1); comp_byte(v); }
	void lda_izp(int v){ comp_byte(0xb2); comp_byte(v); }
	void ldy_zpx(int v){ comp_byte(0xb4); comp_byte(v); }
	void lda_zpx(int v){ comp_byte(0xb5); comp_byte(v); }
	void ldx_zpy(int v){ comp_byte(0xb6); comp_byte(v); }
	void clv(){ comp_byte(0xb8); }
	void lda_aby(int v){ comp_byte(0xb9); comp_word(v); }
	void lda_aby(Label& label) { comp_ab_label(label, 0xb9); }

	void tsx(){ comp_byte(0xba); }
	void ldy_abx(int v){ comp_byte(0xbc); comp_word(v); }
	void ldy_abx(Label &label) { comp_ab_label(label, 0xbc); }
	void lda_abx(int v){ comp_byte(0xbd); comp_word(v); }
	void lda_abx(Label& label) { comp_ab_label(label, 0xbd); }

	void ldx_aby(int v){ comp_byte(0xbe); comp_word(v); }
	void ldx_aby(Label &label) { comp_ab_label(label, 0xbe); }
	void cpy_imm(int v){ comp_byte(0xc0); comp_byte(v); }
	void cmp_izx(int v){ comp_byte(0xc1); comp_byte(v); }
	void cpy_zp(int v){ comp_byte(0xc4); comp_byte(v); }
	void cmp_zp(int v){ comp_byte(0xc5); comp_byte(v); }
	void dec_zp(int v){ comp_byte(0xc6); comp_byte(v); }
	void iny(){ comp_byte(0xc8); }
	void cmp_imm(int v){ comp_byte(0xc9); comp_byte(v); }
	void dex(){ comp_byte(0xca); }
	void wai(){ comp_byte(0xcb); }
	void cpy_abs(int v){ comp_byte(0xcc); comp_word(v); }
	void cpy_abs(Label &label) { comp_ab_label(label, 0xcc); }
	void cmp_abs(int v){ comp_byte(0xcd); comp_word(v); }
	void cmp_abs(Label &label) { comp_ab_label(label, 0xcd); }
	void dec_abs(int v){ comp_byte(0xce); comp_word(v); }
	void dec_abs(Label &label) { comp_ab_label(label, 0xce); }
	void bne(Label &label, bool force_short = false) { compile_branch(0xd0, 0xf0, label, force_short); }
	void cmp_izy(int v){ comp_byte(0xd1); comp_byte(v); }
	void cmp_izp(int v){ comp_byte(0xd2); comp_byte(v); }
	void cmp_zpx(int v){ comp_byte(0xd5); comp_byte(v); }
	void dec_zpx(int v){ comp_byte(0xd6); comp_byte(v); }
	void cld(){ comp_byte(0xd8); }
	void cmp_aby(int v){ comp_byte(0xd9); comp_word(v); }
	void cmp_aby(Label &label) { comp_ab_label(label, 0xd9); }
	void phx(){ comp_byte(0xda); }
	void stp(){ comp_byte(0xdb); }
	void cmp_abx(int v){ comp_byte(0xdd); comp_word(v); }
	void cmp_abx(Label &label) { comp_ab_label(label, 0xdd); }
	void dec_abx(int v){ comp_byte(0xde); comp_word(v); }
	void dec_abx(Label &label) { comp_ab_label(label, 0xde); }
	void cpx_imm(int v){ comp_byte(0xe0); comp_byte(v); }
	void sbc_izx(int v){ comp_byte(0xe1); comp_byte(v); }
	void cpx_zp(int v){ comp_byte(0xe4); comp_byte(v); }
	void sbc_zp(int v){ comp_byte(0xe5); comp_byte(v); }
	void inc_zp(int v){ comp_byte(0xe6); comp_byte(v); }
	void inx(){ comp_byte(0xe8); }
	void sbc_imm(int v){ comp_byte(0xe9); comp_byte(v); }
	void cpx_abs(int v){ comp_byte(0xec); comp_word(v); }
	void cpx_abs(Label &label) { comp_ab_label(label, 0xec); }
	void sbc_abs(int v){ comp_byte(0xed); comp_word(v); }
	void sbc_abs(Label &label) { comp_ab_label(label, 0xed); }
	void inc_abs(int v){ comp_byte(0xee); comp_word(v); }
	void inc_abs(Label &label) { comp_ab_label(label, 0xee); }
	void beq(Label &label, bool force_short = false) { compile_branch(0xf0, 0xd0, label, force_short); }
	void sbc_izy(int v){ comp_byte(0xf1); comp_byte(v); }
	void sbc_izp(int v){ comp_byte(0xf2); comp_byte(v); }
	void sbc_zpx(int v){ comp_byte(0xf5); comp_byte(v); }
	void inc_zpx(int v){ comp_byte(0xf6); comp_byte(v); }
	void sed(){ comp_byte(0xf8); }
	void sbc_aby(int v){ comp_byte(0xf9); comp_word(v); }
	void sbc_aby(Label &label) { comp_ab_label(label, 0xf9); }
	void plx(){ comp_byte(0xfa); }
	void sbc_abx(int v){ comp_byte(0xfd); comp_word(v); }
	void sbc_abx(Label &label) { comp_ab_label(label, 0xfd); }
	void inc_abx(int v){ comp_byte(0xfe); comp_word(v); }
	void inc_abx(Label &label){ comp_ab_label(label, 0xfe); }

	bool test_assembler();

	void reset() {
		time += 7;
		//p &= ~(int)FLAG_B;
		p = (p& ~(int)FLAG_D) | (int)FLAG_I;
		s = 0xfd;
		pc = ((int)get_mem(0xfffd) << 8) + get_mem(0xfffc);
	}
	void irq() {
		waiting = false;
		if ((p&(int)FLAG_I) == 0) {
			push_word(pc);
			push_byte((p&~(int)FLAG_B) | 0x20);
			time += 7;
			p =(p& ~(int)FLAG_D)|(int)FLAG_I;
			pc = (get_mem(0xffff) << 8) + get_mem(0xfffe);
		}
	}
	void nmi() {
		waiting = false;
		push_word(pc);
		push_byte((p&~(int)FLAG_B) | 0x20);
		time += 7;
		p = (p& ~(int)FLAG_D) | (int)FLAG_I;
		pc = (get_mem(0xfffb) << 8) + get_mem(0xfffa);
	}
	int test_for_Z(int v) {
		if ((v&0xff) == 0) p |= (int)FLAG_Z; else p &= ~(int)FLAG_Z;
		return v;
	}
	int test_for_N(int v) {
		if ((v & 0x80) != 0) p |= (int)FLAG_N; else p &= ~(int)FLAG_N;
		return v;
	}

	//emulation table
	static INSTRUCTION * instructions[256];

	//diassembly tables
	static const char * names[256];
	static dissassembly_modes modes[256];
	int disassembly_point;
	uint8_t * external_disassembly_point;
};

