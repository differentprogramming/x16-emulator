#pragma once
#include <list>
#include <memory>
#include <iostream>

//With this defined the $02 NOP is one byte instead of 2
//and $FF is a debug break instead of being a BBR
#define ADD_OFFICIAL_EMULATOR_BUGS

extern void fake_emulator(uint8_t op);
//#define TRACE
//#define PERFSTAT
#define LOAD_HYPERCALLS

#define MHZ 8

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
	int offset; //to allow multibyte targets where some instructions take target+offset
	bool single_byte;//not used yet, for using labels for zero page immediate targets
	LabelFixup():instruction_field_address(-1), target(-1), relative(false), offset(0), single_byte(false){}
	LabelFixup(int f, bool r, int o) :instruction_field_address(f), relative(r), target(-1), offset(o), single_byte(false) {}
	LabelFixup(const LabelFixup &) = default;
	LabelFixup(LabelFixup &&) = default;
	void  update_target(emulate65c02 *emulate, int t);
};

struct Label;
struct LabelBase
{
	LabelBase() {}
	virtual int get_target() const = 0;
	virtual int get_offset() const = 0;
	virtual bool has_target() const = 0;
	virtual Label & get_label() = 0;
	virtual void set_target(emulate65c02 *emulate, int t = -1) = 0;
	virtual void here(emulate65c02 *emulate, int offset = 0) = 0;
	virtual void here(int offset = 0) = 0;
	virtual void add_fixup(int instruction_field_address, bool relative, int offset = 0) = 0;
};

struct Label : public LabelBase
{
	int target;
	Label & get_label() { return *this; };
	Label() :target(-1), fixups(new std::list<LabelFixup>){}
	Label(const Label&) = default;
	Label(Label&&) = default;
	std::shared_ptr<std::list<LabelFixup> > fixups;

	int get_offset() const { return 0; }
	int get_target() const { return target; }
	bool has_target() const { return target != -1; }
	void set_target(emulate65c02 *emulate, int t = -1);
	void here(emulate65c02 *emulate, int offset = 0);
	void here(int offset = 0);
	void add_fixup(int instruction_field_address, bool relative, int offset=0) {
		fixups->push_back(LabelFixup(instruction_field_address, relative, offset));
	}
};

struct Label_Offset : public LabelBase
{
	Label &label;
	int offset;
	Label & get_label() { return label; };
	int get_offset() const { return offset; }
	Label_Offset(int off, LabelBase& l):label(l.get_label()),offset(off+l.get_offset()){}

	Label_Offset(const Label_Offset&) = default;
	Label_Offset(Label_Offset&&) = default;
	int get_target() const {
		return label.get_target()+offset;
	}
	bool has_target() const {
		return label.has_target();
	}
	void set_target(emulate65c02 *emulate, int t = -1);
	void here(emulate65c02 *emulate, int off = 0);
	void here(int off = 0) { label.here(off-offset); }
	void add_fixup(int instruction_field_address, bool relative, int off = 0) {
		label.add_fixup(instruction_field_address, relative, off + offset);
	}
};

inline Label_Offset operator+(LabelBase &o, int i)
{
	return Label_Offset(i, o);
}

inline Label_Offset operator-(LabelBase &o, int i)
{
	return Label_Offset(-i, o);
}

extern int RMB_BY_BIT[8];
extern int SMB_BY_BIT[8];
extern int BBR_BY_BIT[8];
extern int BBS_BY_BIT[8];

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
	int protected_start, protected_end;
	
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
		//printf("WARN: Read Invalid register %x\n", DEVICE_EMULATOR + reg);
		return 0xff;
	}

	
	void exec6502(uint32_t tickcount) {
		clockgoal6502 += tickcount;
		stop = waiting = false;

		while (time < clockgoal6502) {
			instructions[read6502(pc)](this);

			if (callexternal) (*loopexternal)();
			if (stop || waiting) time = clockgoal6502;
			++pc;
		}
	}

	void exec(int address=-1) {
		stop = waiting = false;
		if (address != -1) pc = address;
		while (!stop && !waiting) {
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
			//if (callexternal) (*loopexternal)();
		}
	}


	void step6502() {

		if (trace) {
			disassembly_point = pc;
			std::cout << disassemble();
		}

		uint8_t opcode = read6502(pc);
		if (true) instructions[opcode](this);
		else {
			fake_emulator(opcode);
			--pc;
		}
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

	uint8_t *map_addr(int address) {
		if (address < 0xa000) return &memory[address];
		else if (address < 0xc000) { // banked RAM
			return	&memory[((effective_ram_bank()) << 13) + address];
		}
		else { // banked ROM
			return &rom[((rom_bank) << 14) + address - 0xc000];
		}
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
		disassembly_point(0), external_disassembly_point(nullptr), trace(false),rom_bank(0), ram_bank(0),num_banks(0),
		memory(nullptr), rom(nullptr), callexternal(false),protected_start(-1), protected_end(-1)
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
	void comp_ab_label(LabelBase &label, uint8_t code)
	{
		comp_byte(code);
		if (label.has_target()) {
			comp_word(label.get_target());
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
	void smb(int bit, int zp)
	{
		comp_byte(SMB_BY_BIT[bit]);
		comp_byte(zp);
	}
	void php() { comp_byte(8); }
	void ora_imm(int v) { comp_byte(9); comp_byte(v); }
	void asl() { comp_byte(0x0a); }
	void tsb_abs(int v) { comp_byte(0x0c); comp_word(v); }
	void tsb_abs(LabelBase &label) { comp_ab_label(label, 0x0c); }
	void tsb_ab(int v) { if (v < 256) tsb_zp(v); else tsb_abs(v); }
	void tsb_ab(LabelBase &label) { if (label.has_target() && label.get_target()<256) tsb_zp(label.get_target());  tsb_abs(label); }

	void ora_abs(int v) { comp_byte(0x0d); comp_word(v); }
	void ora_abs(LabelBase &label) { comp_ab_label(label, 0x0d); }
	void ora_ab(int v) { if (v < 256) ora_zp(v); else ora_abs(v); }
	void ora_ab(LabelBase &label) { if (label.has_target() && label.get_target() < 256) ora_zp(label.get_target());  ora_abs(label); }

	void asl_abs(int v) { comp_byte(0x0e); comp_word(v); }
	void asl_abs(LabelBase &label) { comp_ab_label(label, 0x0e); }
	void asl_ab(int v) { if (v < 256) asl_zp(v); else asl_abs(v); }
	void asl_ab(LabelBase &label) { if (label.has_target() && label.get_target() < 256) asl_zp(label.get_target());  asl_abs(label); }

	void bbr(int bit, int zp, LabelBase& label, bool force_short = true)
	{
		int cur_add = (compile_point + 3) & 0xffff;
		if (label.has_target() && label.get_target() - cur_add <= 128 && cur_add - label.get_target() <= 127){
			comp_byte(BBR_BY_BIT[bit]);
			comp_byte(zp);
			comp_byte(label.get_target() - cur_add);
		} else if (force_short) {
			comp_byte(BBR_BY_BIT[bit]);
			comp_byte(zp);
			label.add_fixup((cur_add - 1) & 0xffff, true);
			comp_byte(0);
		}
		else {
			comp_byte(BBS_BY_BIT[bit]);
			label.add_fixup((cur_add + 1) & 0xffff, false);
			comp_byte(zp); //the code is designed so that if you try to run it when the fixup hasn't happened, the branch falls through
			comp_byte(3);//over jump
			comp_byte(0x4c);//jmp
			cur_add = (compile_point + 2) & 0xffff;
			comp_byte(cur_add & 0xff);
			comp_byte((cur_add >> 8) & 0xff);
		}
	}
	void bbs(int bit, int zp, LabelBase& label, bool force_short = true)
	{
		int cur_add = (compile_point + 3) & 0xffff;
		if (label.has_target() && label.get_target() - cur_add <= 128 && cur_add - label.get_target() <= 127) {
			comp_byte(BBS_BY_BIT[bit]);
			comp_byte(zp);
			comp_byte(label.get_target() - cur_add);
		}
		else if (force_short) {
			comp_byte(BBS_BY_BIT[bit]);
			comp_byte(zp);
			label.add_fixup((cur_add - 1) & 0xffff, true);
			comp_byte(0);
		}
		else {
			comp_byte(BBR_BY_BIT[bit]);
			label.add_fixup((cur_add + 1) & 0xffff, false);
			comp_byte(zp); //the code is designed so that if you try to run it when the fixup hasn't happened, the branch falls through
			comp_byte(3);//over jump
			comp_byte(0x4c);//jmp
			cur_add = (compile_point + 2) & 0xffff;
			comp_byte(cur_add & 0xff);
			comp_byte((cur_add >> 8) & 0xff);
		}
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
	void compile_branch(int branch, int inverted_branch, LabelBase& label, bool force_short) {
		int cur_add = (compile_point + 2) & 0xffff;
		if (label.has_target() && label.get_target() - cur_add <= 128 && cur_add - label.get_target() <= 127) {
			comp_byte(branch);
			comp_byte(label.get_target() - cur_add);
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

	void bpl(LabelBase& label, bool force_short=true) {
		compile_branch(0x10, 0x30, label, force_short);
	}
	void bpl(LabelBase&& label, bool force_short = true) {
		compile_branch(0x10, 0x30, label, force_short);
	}
	void ora_izy(int v) { comp_byte(0x11); comp_byte(v); }
	void ora_izp(int v) { comp_byte(0x12); comp_byte(v); }
	void trb_zp(int v) { comp_byte(0x14); comp_byte(v); }
	void ora_zpx(int v){ comp_byte(0x15); comp_byte(v); }
	void asl_zpx(int v) { comp_byte(0x16); comp_byte(v); }
	void clc() { comp_byte(0x18); }

	void ora_aby(int v) { comp_byte(0x19); comp_word(v); }
	void ora_aby(LabelBase &label) { comp_ab_label(label, 0x19); }
	void ora_aby(LabelBase &&label) { comp_ab_label(label, 0x19); }

	void inc() { comp_byte(0x1a); }
	void trb_abs(int v) { comp_byte(0x1c); comp_word(v); }
	void trb_abs(LabelBase &label) { comp_ab_label(label, 0x1c); }
	void trb_abs(LabelBase &&label) { comp_ab_label(label, 0x1c); }
	void trb_ab(int v) { if (v < 256) trb_zp(v); else trb_abs(v); }
	void trb_ab(LabelBase &label) { if (label.has_target() && label.get_target() < 256) trb_zp(label.get_target());  trb_abs(label); }
	void trb_ab(LabelBase &&label) { if (label.has_target() && label.get_target() < 256) trb_zp(label.get_target());  trb_abs(label); }


	void ora_abx(int v) { comp_byte(0x1d); comp_word(v); }
	void ora_abx(LabelBase &label) { comp_ab_label(label, 0x1d); }
	void ora_abx(LabelBase &&label) { comp_ab_label(label, 0x1d); }
	void ora_ax(int v) { if (v < 256) ora_zpx(v); else ora_abx(v); }
	void ora_ax(LabelBase &label) { if (label.has_target() && label.get_target() < 256) ora_zpx(label.get_target());  ora_abx(label); }
	void ora_ax(LabelBase &&label) { if (label.has_target() && label.get_target() < 256) ora_zpx(label.get_target());  ora_abx(label); }

	void asl_abx(int v) { comp_byte(0x1e); comp_word(v); }
	void asl_abx(LabelBase &label) { comp_ab_label(label, 0x1e); }
	void asl_abx(LabelBase &&label) { comp_ab_label(label, 0x1e); }
	void asl_ax(int v) { if (v < 256) asl_zpx(v); else asl_abx(v); }
	void asl_ax(LabelBase &&label) { if (label.has_target() && label.get_target() < 256) asl_zpx(label.get_target());  asl_abx(label); }
	void asl_ax(LabelBase &label) { if (label.has_target() && label.get_target() < 256) asl_zpx(label.get_target());  asl_abx(label); }

	void jsr(LabelBase& label) { comp_ab_label(label, 0x20); }
	void jsr(LabelBase&& label) { comp_ab_label(label, 0x20); }
	void and_izx(int v) { comp_byte(0x21); comp_byte(v); }
	void bit_zp(int v) { comp_byte(0x24); comp_byte(v); }
	void and_zp(int v) { comp_byte(0x25); comp_byte(v); }
	void rol_zp(int v) { comp_byte(0x26); comp_byte(v); }
	void plp() { comp_byte(0x28); }
	void and_imm(int v) { comp_byte(0x29); comp_byte(v); }
	void rol() { comp_byte(0x2a); }
	void bit_abs(int v) { comp_byte(0x2c); comp_word(v); }
	void bit_ab(int v) { if (v < 256) bit_zp(v); else bit_abs(v); }
	void bit_ab(LabelBase &label) { if (label.has_target() && label.get_target() < 256) bit_zp(label.get_target()); else bit_abs(label); }
	void bit_ab(LabelBase &&label) { if (label.has_target() && label.get_target() < 256) bit_zp(label.get_target()); else bit_abs(label); }
	void bit_abs(LabelBase &label) { comp_ab_label(label, 0x2c); }
	void bit_abs(LabelBase &&label) { comp_ab_label(label, 0x2c); }
	void and_abs(int v) { comp_byte(0x2d); comp_word(v); }
	void and_ab(int v) { if (v < 256) and_zp(v); else and_abs(v); }
	void and_abs(LabelBase &label) { comp_ab_label(label, 0x2d); }
	void and_ab(LabelBase &&label) { if (label.has_target() && label.get_target() < 256) and_zp(label.get_target()); else and_abs(label); }
	void and_ab(LabelBase &label) { if (label.has_target() && label.get_target() < 256) and_zp(label.get_target()); else and_abs(label); }
	void rol_abs(int v) { comp_byte(0x2e); comp_word(v); }
	void rol_abs(LabelBase &label) { comp_ab_label(label, 0x2e); }
	void rol_abs(LabelBase &&label) { comp_ab_label(label, 0x2e); }
	void rol_ab(int v) { if (v < 256) rol_zp(v); else rol_abs(v); }
	void rol_ab(LabelBase &label) { if (label.has_target() && label.get_target() < 256) rol_zp(label.get_target());  rol_abs(label); }
	void rol_ab(LabelBase &&label) { if (label.has_target() && label.get_target() < 256) rol_zp(label.get_target());  rol_abs(label); }

	void bmi(LabelBase& label, bool force_short = true) {
		compile_branch(0x30, 0x10, label, force_short);
	}
	void bmi(LabelBase&& label, bool force_short = true) {
		compile_branch(0x30, 0x10, label, force_short);
	}
	void and_izy(int v) { comp_byte(0x31); comp_byte(v); }
	void and_izp(int v) { comp_byte(0x32); comp_byte(v); }
	void bit_zpx(int v) { comp_byte(0x34); comp_byte(v); }
	void and_zpx(int v){ comp_byte(0x35); comp_byte(v); }
	void rol_zpx(int v){ comp_byte(0x36); comp_byte(v); }
	void sec(){ comp_byte(0x38); }
	void and_aby(int v){ comp_byte(0x39); comp_word(v); }
	void and_aby(LabelBase &label) { comp_ab_label(label, 0x39); }
	void and_aby(LabelBase &&label) { comp_ab_label(label, 0x39); }
	void dec(){ comp_byte(0x3a); }
	void bit_abx(int v){ comp_byte(0x3c); comp_word(v); }
	void bit_abx(LabelBase &label) { comp_ab_label(label, 0x3c); }
	void bit_abx(LabelBase &&label) { comp_ab_label(label, 0x3c); }
	void bit_ax(int v) { if (v < 256) bit_zpx(v); else bit_abx(v); }
	void bit_ax(LabelBase &label) { if (label.has_target() && label.get_target() < 256) bit_zpx(label.get_target());  bit_abx(label); }
	void bit_ax(LabelBase &&label) { if (label.has_target() && label.get_target() < 256) bit_zpx(label.get_target());  bit_abx(label); }

	void and_abx(int v){ comp_byte(0x3d); comp_word(v); }
	void and_abx(LabelBase &label) { comp_ab_label(label, 0x3d); }
	void and_abx(LabelBase &&label) { comp_ab_label(label, 0x3d); }
	void and_ax(int v) { if (v < 256) and_zpx(v); else and_abx(v); }
	void and_ax(LabelBase &label) { if (label.has_target() && label.get_target() < 256) and_zpx(label.get_target());  and_abx(label); }
	void and_ax(LabelBase &&label) { if (label.has_target() && label.get_target() < 256) and_zpx(label.get_target());  and_abx(label); }

	void rol_abx(int v){ comp_byte(0x3e); comp_word(v); }
	void rol_abx(LabelBase &&label) { comp_ab_label(label, 0x3e); }
	void rol_abx(LabelBase &label) { comp_ab_label(label, 0x3e); }
	void rol_ax(int v) { if (v < 256) rol_zpx(v); else rol_abx(v); }
	void rol_ax(LabelBase &&label) { if (label.has_target() && label.get_target() < 256) rol_zpx(label.get_target());  rol_abx(label); }
	void rol_ax(LabelBase &label) { if (label.has_target() && label.get_target() < 256) rol_zpx(label.get_target());  rol_abx(label); }

	void rti(){ comp_byte(0x40); }
	void eor_izx(int v){ comp_byte(0x41); comp_byte(v); }
	void eor_zp(int v){ comp_byte(0x45); comp_byte(v); }
	void lsr_zp(int v){ comp_byte(0x46); comp_byte(v); }
	void pha(){ comp_byte(0x48); }
	void eor_imm(int v){ comp_byte(0x49); comp_byte(v); }
	void lsr(){ comp_byte(0x4a); }
	void jmp_abs(int v){ comp_byte(0x4c); comp_word(v); }
	void jmp(LabelBase &&label) { comp_ab_label(label, 0x4c); }
	void jmp(LabelBase &label)  { comp_ab_label(label, 0x4c); }
	void eor_abs(int v){ comp_byte(0x4d); comp_word(v); }
	void eor_abs(LabelBase &&label) { comp_ab_label(label, 0x4d); }
	void eor_abs(LabelBase &label) { comp_ab_label(label, 0x4d); }
	void eor_ab(int v) { if (v < 256) eor_zp(v); else eor_abs(v); }
	void eor_ab(LabelBase &&label) { if (label.has_target() && label.get_target() < 256) eor_zp(label.get_target());  eor_abs(label); }
	void eor_ab(LabelBase &label) { if (label.has_target() && label.get_target() < 256) eor_zp(label.get_target());  eor_abs(label); }

	void lsr_abs(int v){ comp_byte(0x4e); comp_word(v); }
	void lsr_abs(LabelBase &&label) { comp_ab_label(label, 0x4e); }
	void lsr_abs(LabelBase &label) { comp_ab_label(label, 0x4e); }
	void lsr_ab(int v) { if (v < 256) lsr_zp(v); else lsr_abs(v); }
	void lsr_ab(LabelBase &&label) { if (label.has_target() && label.get_target() < 256) lsr_zp(label.get_target());  lsr_abs(label); }
	void lsr_ab(LabelBase &label) { if (label.has_target() && label.get_target() < 256) lsr_zp(label.get_target());  lsr_abs(label); }

	void bvc(LabelBase &&label, bool force_short = true) { compile_branch(0x50, 0x70, label, force_short); }
	void bvc(LabelBase &label, bool force_short=true){ compile_branch(0x50, 0x70, label, force_short); }
	void eor_izy(int v){ comp_byte(0x51); comp_byte(v); }
	void eor_izp(int v){ comp_byte(0x52); comp_byte(v); }
	void eor_zpx(int v){ comp_byte(0x55); comp_byte(v); }
	void lsr_zpx(int v){ comp_byte(0x56); comp_byte(v); }
	void cli(){ comp_byte(0x58); }
	void eor_aby(int v){ comp_byte(0x59); comp_word(v); }
	void eor_aby(LabelBase &label) { comp_ab_label(label, 0x59); }
	void eor_aby(LabelBase &&label) { comp_ab_label(label, 0x59); }
	void phy(){ comp_byte(0x5a); }
	void eor_abx(int v){ comp_byte(0x5d); comp_word(v); }
	void eor_abx(LabelBase &&label) { comp_ab_label(label, 0x5d); }
	void eor_abx(LabelBase &label) { comp_ab_label(label, 0x5d); }
	void eor_ax(int v) { if (v < 256) eor_zpx(v); else eor_abx(v); }
	void eor_ax(LabelBase &&label) { if (label.has_target() && label.get_target() < 256) eor_zpx(label.get_target());  eor_abx(label); }
	void eor_ax(LabelBase &label) { if (label.has_target() && label.get_target() < 256) eor_zpx(label.get_target());  eor_abx(label); }

	void lsr_abx(int v){ comp_byte(0x5e); comp_word(v); }
	void lsr_abx(LabelBase &&label) { comp_ab_label(label, 0x5e); }
	void lsr_abx(LabelBase &label) { comp_ab_label(label, 0x5e); }
	void lsr_ax(int v) { if (v < 256) lsr_zpx(v); else lsr_abx(v); }
	void lsr_ax(LabelBase &label) { if (label.has_target() && label.get_target() < 256) lsr_zpx(label.get_target());  lsr_abx(label); }
	void lsr_ax(LabelBase &&label) { if (label.has_target() && label.get_target() < 256) lsr_zpx(label.get_target());  lsr_abx(label); }

	void rts(){ comp_byte(0x60); }
	void adc_izx(int v){ comp_byte(0x61); comp_byte(v); }
	void stz_zp(int v){ comp_byte(0x64); comp_byte(v); }
	void adc_zp(int v){ comp_byte(0x65); comp_byte(v); }
	void ror_zp(int v){ comp_byte(0x66); comp_byte(v); }
	void pla(){ comp_byte(0x68); }
	void adc_imm(int v){ comp_byte(0x69); comp_byte(v); }
	void ror(){ comp_byte(0x6a); }
	void jmp_ind(int v){ comp_byte(0x6c); comp_word(v); }
	void jmp_ind(LabelBase &label) { comp_ab_label(label, 0x6c); }
	void jmp_ind(LabelBase &&label) { comp_ab_label(label, 0x6c); }
	void adc_abs(int v){ comp_byte(0x6d); comp_word(v); }
	void adc_abs(LabelBase &label) { comp_ab_label(label, 0x6d); }
	void adc_abs(LabelBase &&label) { comp_ab_label(label, 0x6d); }
	void adc_ab(int v) { if (v < 256) adc_zp(v); else adc_abs(v); }
	void adc_ab(LabelBase &label) { if (label.has_target() && label.get_target() < 256) adc_zp(label.get_target());  adc_abs(label); }
	void adc_ab(LabelBase &&label) { if (label.has_target() && label.get_target() < 256) adc_zp(label.get_target());  adc_abs(label); }

	void ror_abs(int v){ comp_byte(0x6e); comp_word(v); }
	void ror_abs(LabelBase &&label) { comp_ab_label(label, 0x6e); }
	void ror_abs(LabelBase &label) { comp_ab_label(label, 0x6e); }
	void ror_ab(int v) { if (v < 256) ror_zp(v); else ror_abs(v); }
	void ror_ab(LabelBase &&label) { if (label.has_target() && label.get_target() < 256) ror_zp(label.get_target());  ror_abs(label); }
	void ror_ab(LabelBase &label) { if (label.has_target() && label.get_target() < 256) ror_zp(label.get_target());  ror_abs(label); }


	void bvs(LabelBase &&label, bool force_short = true) { compile_branch(0x70, 0x50, label, force_short); }
	void bvs(LabelBase &label, bool force_short = true) { compile_branch(0x70, 0x50, label, force_short); }
	void adc_izy(int v){ comp_byte(0x71); comp_byte(v); }
	void adc_izp(int v){ comp_byte(0x72); comp_byte(v); }
	void stz_zpx(int v){ comp_byte(0x74); comp_byte(v); }
	void adc_zpx(int v){ comp_byte(0x75); comp_byte(v); }
	void ror_zpx(int v){ comp_byte(0x76); comp_byte(v); }
	void sei(){ comp_byte(0x78); }
	void adc_aby(int v){ comp_byte(0x79); comp_word(v); }
	void adc_aby(LabelBase &label) { comp_ab_label(label, 0x79); }
	void adc_aby(LabelBase &&label) { comp_ab_label(label, 0x79); }
	void ply(){ comp_byte(0x7a); }
	void jmp_iax(int v){ comp_byte(0x7c); comp_word(v); }
	void jmp_iax(LabelBase &label) { comp_ab_label(label, 0x7c); }
	void jmp_iax(LabelBase &&label) { comp_ab_label(label, 0x7c); }

	void adc_abx(int v){ comp_byte(0x7d); comp_word(v); }
	void adc_abx(LabelBase &label) { comp_ab_label(label, 0x7d); }
	void adc_abx(LabelBase &&label) { comp_ab_label(label, 0x7d); }
	void adc_ax(int v) { if (v < 256) adc_zpx(v); else adc_abx(v); }
	void adc_ax(LabelBase &label) { if (label.has_target() && label.get_target() < 256) adc_zpx(label.get_target());  adc_abx(label); }
	void adc_ax(LabelBase &&label) { if (label.has_target() && label.get_target() < 256) adc_zpx(label.get_target());  adc_abx(label); }

	void ror_abx(int v){ comp_byte(0x7e); comp_word(v); }
	void ror_abx(LabelBase &&label) { comp_ab_label(label, 0x7e); }
	void ror_abx(LabelBase &label) { comp_ab_label(label, 0x7e); }
	void ror_ax(int v) { if (v < 256) ror_zpx(v); else ror_abx(v); }
	void ror_ax(LabelBase &&label) { if (label.has_target() && label.get_target() < 256) ror_zpx(label.get_target());  ror_abx(label); }
	void ror_ax(LabelBase &label) { if (label.has_target() && label.get_target() < 256) ror_zpx(label.get_target());  ror_abx(label); }

	void bra(LabelBase& label, bool force_short=true) {
		int cur_add = (compile_point + 2) & 0xffff;
		if (label.has_target() && label.get_target() - cur_add <= 128 && cur_add - label.get_target() <= 127) {
			comp_byte(0x80);
			comp_byte(label.get_target() - cur_add);
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
	void bra(LabelBase&& label, bool force_short = true) {
		int cur_add = (compile_point + 2) & 0xffff;
		if (label.has_target() && label.get_target() - cur_add <= 128 && cur_add - label.get_target() <= 127) {
			comp_byte(0x80);
			comp_byte(label.get_target() - cur_add);
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
	void sty_abs(LabelBase &&label) { comp_ab_label(label, 0x8c); }
	void sty_abs(LabelBase &label) { comp_ab_label(label, 0x8c); }
	void sty_ab(int v) { if (v < 256) sty_zp(v); else sty_abs(v); }
	void sty_ab(LabelBase &&label) { if (label.has_target() && label.get_target() < 256) sty_zp(label.get_target());  sty_abs(label); }
	void sty_ab(LabelBase &label) { if (label.has_target() && label.get_target() < 256) sty_zp(label.get_target());  sty_abs(label); }
	void sta_abs(int v){ comp_byte(0x8d); comp_word(v); }
	void sta_abs(LabelBase &&label) { comp_ab_label(label, 0x8d); }
	void sta_abs(LabelBase &label) { comp_ab_label(label, 0x8d); }
	void sta_ab(int v) { if (v < 256) sta_zp(v); else sta_abs(v); }
	void sta_ab(LabelBase &&label) { if (label.has_target() && label.get_target() < 256) sta_zp(label.get_target());  sta_abs(label); }
	void sta_ab(LabelBase &label) { if (label.has_target() && label.get_target() < 256) sta_zp(label.get_target());  sta_abs(label); }
	void stx_abs(int v){ comp_byte(0x8e); comp_word(v); }
	void stx_abs(LabelBase &label) { comp_ab_label(label, 0x8e); }
	void stx_abs(LabelBase &&label) { comp_ab_label(label, 0x8e); }
	void stx_ab(int v) { if (v < 256) stx_zp(v); else stx_abs(v); }
	void stx_ab(LabelBase &&label) { if (label.has_target() && label.get_target() < 256) stx_zp(label.get_target());  stx_abs(label); }
	void stx_ab(LabelBase &label) { if (label.has_target() && label.get_target() < 256) stx_zp(label.get_target());  stx_abs(label); }
	void bcc(LabelBase &&label, bool force_short = true) { compile_branch(0x90, 0xb0, label, force_short); }
	void bcc(LabelBase &label, bool force_short = true) { compile_branch(0x90, 0xb0, label, force_short); }
	void blt(LabelBase &&label, bool force_short = true) { compile_branch(0x90, 0xb0, label, force_short); }
	void blt(LabelBase &label, bool force_short = true) { compile_branch(0x90, 0xb0, label, force_short); }
	void sta_izy(int v){ comp_byte(0x91); comp_byte(v); }
	void sta_izp(int v){ comp_byte(0x92); comp_byte(v); }
	void sty_zpx(int v){ comp_byte(0x94); comp_byte(v); }
	void sta_zpx(int v){ comp_byte(0x95); comp_byte(v); }
	void stx_zpy(int v){ comp_byte(0x96); comp_byte(v); }
	void tya(){ comp_byte(0x98); }
	void sta_aby(int v){ comp_byte(0x99); comp_word(v); }
	void sta_aby(LabelBase&& label) { comp_ab_label(label, 0x99); }
	void sta_aby(LabelBase& label) { comp_ab_label(label, 0x99); }

	void txs(){ comp_byte(0x9a); }
	void stz_abs(int v){ comp_byte(0x9c); comp_word(v); }
	void stz_abs(LabelBase &&label) { comp_ab_label(label, 0x9c); }
	void stz_abs(LabelBase &label) { comp_ab_label(label, 0x9c); }
	void stz_ab(int v) { if (v < 256) stz_zp(v); else stz_abs(v); }
	void stz_ab(LabelBase &&label) { if (label.has_target() && label.get_target() < 256) stz_zp(label.get_target());  stz_abs(label); }
	void stz_ab(LabelBase &label) { if (label.has_target() && label.get_target() < 256) stz_zp(label.get_target());  stz_abs(label); }
	void sta_abx(int v){ comp_byte(0x9d); comp_word(v); }
	void sta_abx(LabelBase&& label) { comp_ab_label(label, 0x9d); }
	void sta_abx(LabelBase& label) { comp_ab_label(label, 0x9d); }
	void sta_ax(int v) { if (v < 256) sta_zpx(v); else sta_abx(v); }
	void sta_ax(LabelBase &label) { if (label.has_target() && label.get_target() < 256) sta_zpx(label.get_target());  sta_abx(label); }
	void sta_ax(LabelBase &&label) { if (label.has_target() && label.get_target() < 256) sta_zpx(label.get_target());  sta_abx(label); }


	void stz_abx(int v){ comp_byte(0x9e); comp_word(v); }
	void stz_abx(LabelBase &&label) { comp_ab_label(label, 0x9e); }
	void stz_abx(LabelBase &label) { comp_ab_label(label, 0x9e); }
	void stz_ax(int v) { if (v < 256) stz_zpx(v); else stz_abx(v); }
	void stz_ax(LabelBase &&label) { if (label.has_target() && label.get_target() < 256) stz_zpx(label.get_target());  stz_abx(label); }
	void stz_ax(LabelBase &label) { if (label.has_target() && label.get_target() < 256) stz_zpx(label.get_target());  stz_abx(label); }

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
	void ldy_abs(LabelBase &&label) { comp_ab_label(label, 0xac); }
	void ldy_abs(LabelBase &label) { comp_ab_label(label, 0xac); }
	void ldy_ab(int v) { if (v < 256) ldy_zp(v); else ldy_abs(v); }
	void ldy_ab(LabelBase &&label) { if (label.has_target() && label.get_target() < 256) ldy_zp(label.get_target());  ldy_abs(label); }
	void ldy_ab(LabelBase &label) { if (label.has_target() && label.get_target() < 256) ldy_zp(label.get_target());  ldy_abs(label); }

	void lda_abs(int v){ comp_byte(0xad); comp_word(v); }
	void lda_abs(LabelBase &&label) { comp_ab_label(label, 0xad); }
	void lda_abs(LabelBase &label) { comp_ab_label(label, 0xad); }
	void lda_ab(int v) { if (v < 256) lda_zp(v); else lda_abs(v); }
	void lda_ab(LabelBase &&label) { if (label.has_target() && label.get_target() < 256) lda_zp(label.get_target());  lda_abs(label); }
	void lda_ab(LabelBase &label) { if (label.has_target() && label.get_target() < 256) lda_zp(label.get_target());  lda_abs(label); }

	void ldx_abs(int v){ comp_byte(0xae); comp_word(v); }
	void ldx_abs(LabelBase &&label) { comp_ab_label(label, 0xae); }
	void ldx_abs(LabelBase &label) { comp_ab_label(label, 0xae); }
	void ldx_ab(int v) { if (v < 256) ldx_zp(v); else ldx_abs(v); }
	void ldx_ab(LabelBase &&label) { if (label.has_target() && label.get_target() < 256) ldx_zp(label.get_target());  ldx_abs(label); }
	void ldx_ab(LabelBase &label) { if (label.has_target() && label.get_target() < 256) ldx_zp(label.get_target());  ldx_abs(label); }

	void bcs(LabelBase &&label, bool force_short = true) { compile_branch(0xb0, 0x90, label, force_short); }
	void bcs(LabelBase &label, bool force_short = true) { compile_branch(0xb0, 0x90, label, force_short); }
	void bge(LabelBase &label, bool force_short = true) { compile_branch(0xb0, 0x90, label, force_short); }
	void bge(LabelBase &&label, bool force_short = true) { compile_branch(0xb0, 0x90, label, force_short); }
	void lda_izy(int v){ comp_byte(0xb1); comp_byte(v); }
	void lda_izp(int v){ comp_byte(0xb2); comp_byte(v); }
	void ldy_zpx(int v){ comp_byte(0xb4); comp_byte(v); }
	void lda_zpx(int v){ comp_byte(0xb5); comp_byte(v); }
	void ldx_zpy(int v){ comp_byte(0xb6); comp_byte(v); }
	void clv(){ comp_byte(0xb8); }
	void lda_aby(int v){ comp_byte(0xb9); comp_word(v); }
	void lda_aby(LabelBase&& label) { comp_ab_label(label, 0xb9); }
	void lda_aby(LabelBase& label) { comp_ab_label(label, 0xb9); }

	void tsx(){ comp_byte(0xba); }
	void ldy_abx(int v){ comp_byte(0xbc); comp_word(v); }
	void ldy_abx(LabelBase &&label) { comp_ab_label(label, 0xbc); }
	void ldy_abx(LabelBase &label) { comp_ab_label(label, 0xbc); }
	void ldy_ax(int v) { if (v < 256) ldy_zpx(v); else ldy_abx(v); }
	void ldy_ax(LabelBase &&label) { if (label.has_target() && label.get_target() < 256) ldy_zpx(label.get_target());  ldy_abx(label); }
	void ldy_ax(LabelBase &label) { if (label.has_target() && label.get_target() < 256) ldy_zpx(label.get_target());  ldy_abx(label); }

	void lda_abx(int v){ comp_byte(0xbd); comp_word(v); }
	void lda_abx(LabelBase&& label) { comp_ab_label(label, 0xbd); }
	void lda_abx(LabelBase& label) { comp_ab_label(label, 0xbd); }
	void lda_ax(int v) { if (v < 256) lda_zpx(v); else lda_abx(v); }
	void lda_ax(LabelBase &label) { if (label.has_target() && label.get_target() < 256) lda_zpx(label.get_target());  lda_abx(label); }
	void lda_ax(LabelBase &&label) { if (label.has_target() && label.get_target() < 256) lda_zpx(label.get_target());  lda_abx(label); }

	void ldx_aby(int v){ comp_byte(0xbe); comp_word(v); }
	void ldx_aby(LabelBase &&label) { comp_ab_label(label, 0xbe); }
	void ldx_aby(LabelBase &label) { comp_ab_label(label, 0xbe); }
	void ldx_ay(int v) { if (v < 256) ldx_zpy(v); else ldx_aby(v); }
	void ldx_ay(LabelBase &&label) { if (label.has_target() && label.get_target() < 256) ldx_zpy(label.get_target());  ldx_aby(label); }
	void ldx_ay(LabelBase &label) { if (label.has_target() && label.get_target() < 256) ldx_zpy(label.get_target());  ldx_aby(label); }


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
	void cpy_abs(LabelBase &&label) { comp_ab_label(label, 0xcc); }
	void cpy_abs(LabelBase &label) { comp_ab_label(label, 0xcc); }
	void cpy_ab(int v) { if (v < 256) cpy_zp(v); else cpy_abs(v); }
	void cpy_ab(LabelBase &&label) { if (label.has_target() && label.get_target() < 256) cpy_zp(label.get_target());  cpy_abs(label); }
	void cpy_ab(LabelBase &label) { if (label.has_target() && label.get_target() < 256) cpy_zp(label.get_target());  cpy_abs(label); }

	void cmp_abs(int v){ comp_byte(0xcd); comp_word(v); }
	void cmp_abs(LabelBase &&label) { comp_ab_label(label, 0xcd); }
	void cmp_abs(LabelBase &label) { comp_ab_label(label, 0xcd); }
	void cmp_ab(int v) { if (v < 256) cmp_zp(v); else cmp_abs(v); }
	void cmp_ab(LabelBase &&label) { if (label.has_target() && label.get_target() < 256) cmp_zp(label.get_target());  cmp_abs(label); }
	void cmp_ab(LabelBase &label) { if (label.has_target() && label.get_target() < 256) cmp_zp(label.get_target());  cmp_abs(label); }

	void dec_abs(int v){ comp_byte(0xce); comp_word(v); }
	void dec_abs(LabelBase &&label) { comp_ab_label(label, 0xce); }
	void dec_abs(LabelBase &label) { comp_ab_label(label, 0xce); }
	void dec_ab(int v) { if (v < 256) dec_zp(v); else dec_abs(v); }
	void dec_ab(LabelBase &&label) { if (label.has_target() && label.get_target() < 256) dec_zp(label.get_target());  dec_abs(label); }
	void dec_ab(LabelBase &label) { if (label.has_target() && label.get_target() < 256) dec_zp(label.get_target());  dec_abs(label); }

	void bne(LabelBase &&label, bool force_short = true) { compile_branch(0xd0, 0xf0, label, force_short); }
	void bne(LabelBase &label, bool force_short = true) { compile_branch(0xd0, 0xf0, label, force_short); }
	void cmp_izy(int v){ comp_byte(0xd1); comp_byte(v); }
	void cmp_izp(int v){ comp_byte(0xd2); comp_byte(v); }
	void cmp_zpx(int v){ comp_byte(0xd5); comp_byte(v); }
	void dec_zpx(int v){ comp_byte(0xd6); comp_byte(v); }
	void cld(){ comp_byte(0xd8); }
	void cmp_aby(int v){ comp_byte(0xd9); comp_word(v); }
	void cmp_aby(LabelBase &&label) { comp_ab_label(label, 0xd9); }
	void cmp_aby(LabelBase &label) { comp_ab_label(label, 0xd9); }
	void phx(){ comp_byte(0xda); }
	void stp(){ comp_byte(0xdb); }
	void cmp_abx(int v){ comp_byte(0xdd); comp_word(v); }
	void cmp_abx(LabelBase &&label) { comp_ab_label(label, 0xdd); }
	void cmp_abx(LabelBase &label) { comp_ab_label(label, 0xdd); }
	void cmp_ax(int v) { if (v < 256) cmp_zpx(v); else cmp_abx(v); }
	void cmp_ax(LabelBase &&label) { if (label.has_target() && label.get_target() < 256) cmp_zpx(label.get_target());  cmp_abx(label); }
	void cmp_ax(LabelBase &label) { if (label.has_target() && label.get_target() < 256) cmp_zpx(label.get_target());  cmp_abx(label); }

	void dec_abx(int v){ comp_byte(0xde); comp_word(v); }
	void dec_abx(LabelBase &&label) { comp_ab_label(label, 0xde); }
	void dec_abx(LabelBase &label) { comp_ab_label(label, 0xde); }
	void dec_ax(int v) { if (v < 256) dec_zpx(v); else dec_abx(v); }
	void dec_ax(LabelBase &&label) { if (label.has_target() && label.get_target() < 256) dec_zpx(label.get_target());  dec_abx(label); }
	void dec_ax(LabelBase &label) { if (label.has_target() && label.get_target() < 256) dec_zpx(label.get_target());  dec_abx(label); }

	void cpx_imm(int v){ comp_byte(0xe0); comp_byte(v); }
	void sbc_izx(int v){ comp_byte(0xe1); comp_byte(v); }
	void cpx_zp(int v){ comp_byte(0xe4); comp_byte(v); }
	void sbc_zp(int v){ comp_byte(0xe5); comp_byte(v); }
	void inc_zp(int v){ comp_byte(0xe6); comp_byte(v); }
	void inx(){ comp_byte(0xe8); }
	void sbc_imm(int v){ comp_byte(0xe9); comp_byte(v); }
	void cpx_abs(int v){ comp_byte(0xec); comp_word(v); }
	void cpx_abs(LabelBase &&label) { comp_ab_label(label, 0xec); }
	void cpx_abs(LabelBase &label) { comp_ab_label(label, 0xec); }
	void cpx_ab(int v) { if (v < 256) cpx_zp(v); else cpx_abs(v); }
	void cpx_ab(LabelBase &&label) { if (label.has_target() && label.get_target() < 256) cpx_zp(label.get_target());  cpx_abs(label); }
	void cpx_ab(LabelBase &label) { if (label.has_target() && label.get_target() < 256) cpx_zp(label.get_target());  cpx_abs(label); }

	void sbc_abs(int v){ comp_byte(0xed); comp_word(v); }
	void sbc_abs(LabelBase &&label) { comp_ab_label(label, 0xed); }
	void sbc_abs(LabelBase &label) { comp_ab_label(label, 0xed); }
	void sbc_ab(int v) { if (v < 256) sbc_zp(v); else sbc_abs(v); }
	void sbc_ab(LabelBase &&label) { if (label.has_target() && label.get_target() < 256) sbc_zp(label.get_target());  sbc_abs(label); }
	void sbc_ab(LabelBase &label) { if (label.has_target() && label.get_target() < 256) sbc_zp(label.get_target());  sbc_abs(label); }

	void inc_abs(int v){ comp_byte(0xee); comp_word(v); }
	void inc_abs(LabelBase &&label) { comp_ab_label(label, 0xee); }
	void inc_abs(LabelBase &label) { comp_ab_label(label, 0xee); }
	void inc_ab(int v) { if (v < 256) inc_zp(v); else inc_abs(v); }
	void inc_ab(LabelBase &&label) { if (label.has_target() && label.get_target() < 256) inc_zp(label.get_target());  inc_abs(label); }
	void inc_ab(LabelBase &label) { if (label.has_target() && label.get_target() < 256) inc_zp(label.get_target());  inc_abs(label); }

	void beq(LabelBase &&label, bool force_short = true) { compile_branch(0xf0, 0xd0, label, force_short); }
	void beq(LabelBase &label, bool force_short = true) { compile_branch(0xf0, 0xd0, label, force_short); }
	void sbc_izy(int v){ comp_byte(0xf1); comp_byte(v); }
	void sbc_izp(int v){ comp_byte(0xf2); comp_byte(v); }
	void sbc_zpx(int v){ comp_byte(0xf5); comp_byte(v); }
	void inc_zpx(int v){ comp_byte(0xf6); comp_byte(v); }
	void sed(){ comp_byte(0xf8); }
	void sbc_aby(int v){ comp_byte(0xf9); comp_word(v); }
	void sbc_aby(LabelBase &&label) { comp_ab_label(label, 0xf9); }
	void sbc_aby(LabelBase &label) { comp_ab_label(label, 0xf9); }
	void plx(){ comp_byte(0xfa); }
	void sbc_abx(int v){ comp_byte(0xfd); comp_word(v); }
	void sbc_abx(LabelBase &&label) { comp_ab_label(label, 0xfd); }
	void sbc_abx(LabelBase &label) { comp_ab_label(label, 0xfd); }
	void sbc_ax(int v) { if (v < 256) sbc_zpx(v); else sbc_abx(v); }
	void sbc_ax(LabelBase &&label) { if (label.has_target() && label.get_target() < 256) sbc_zpx(label.get_target());  sbc_abx(label); }
	void sbc_ax(LabelBase &label) { if (label.has_target() && label.get_target() < 256) sbc_zpx(label.get_target());  sbc_abx(label); }
	void inc_abx(int v){ comp_byte(0xfe); comp_word(v); }
	void inc_abx(LabelBase &&label) { comp_ab_label(label, 0xfe); }
	void inc_abx(LabelBase &label){ comp_ab_label(label, 0xfe); }
	void inc_ax(int v) { if (v < 256) inc_zpx(v); else inc_abx(v); }
	void inc_ax(LabelBase &&label) { if (label.has_target() && label.get_target() < 256) inc_zpx(label.get_target());  inc_abx(label); }
	void inc_ax(LabelBase &label) { if (label.has_target() && label.get_target() < 256) inc_zpx(label.get_target());  inc_abx(label); }

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
	uint16_t build_solid();
	template <typename T, int size> void comp_table(T (&table) [size])
	{
		for (int i = 0; i < size; ++i) {
			for (int j = 0; j < sizeof(T); ++j) {
				comp_byte(*(j + (unsigned char *)&table[i]));
			}
		}
	}

#define TWO_ADDRESS(name,first,second,code) void name(int first, int second) code \
void name(LabelBase & first, int second) code \
void name(int first, LabelBase & second) code \
void name(LabelBase && first, int second) code \
void name(int first, LabelBase && second) code \
void name(LabelBase & first, LabelBase& second) code \
void name(LabelBase && first, LabelBase& second) code \
void name(LabelBase & first, LabelBase&& second) code \
void name(LabelBase && first, LabelBase&& second) code

#define TWO_ADD_CMP(name,first,second,third,code) void name(int first, int second, LabelBase &third, bool force_short=true) code \
void name(LabelBase & first, int second, LabelBase &third, bool force_short=true) code \
void name(int first, LabelBase & second, LabelBase &third, bool force_short=true) code \
void name(LabelBase && first, int second, LabelBase &third, bool force_short=true) code \
void name(int first, LabelBase && second, LabelBase &third, bool force_short=true) code \
void name(LabelBase & first, LabelBase& second, LabelBase &third, bool force_short=true) code \
void name(LabelBase && first, LabelBase& second, LabelBase &third, bool force_short=true) code \
void name(LabelBase & first, LabelBase&& second, LabelBase &third, bool force_short=true) code \
void name(LabelBase && first, LabelBase&& second, LabelBase &third, bool force_short=true) code


#define ONE_ADDRESS(name,first,second,code) void name(int first, int second) code \
void name(LabelBase & first, int second) code \
void name(LabelBase && first, int second) code

#define ONE_ADD_CMP(name,first,second,third,code) void name(int first, int second, LabelBase &third, bool force_short=true) code \
void name(LabelBase & first, int second, LabelBase &third, bool force_short=true) code \
void name(LabelBase && first, int second, LabelBase &third, bool force_short=true) code

	ONE_ADDRESS(ld16_imm,dest, v, {
		lda_imm(v & 0xff);
		sta_ab(dest);
		lda_imm((v >> 8) & 0xff);
		sta_ab(dest + 1);
	})

	TWO_ADDRESS(mv_16, dest, src, {
		lda_ab(src);
		sta_ab(dest);
		lda_ab(src + 1);
		sta_ab(dest + 1);
	})


	ONE_ADDRESS( add16_imm,dest,v,
	{
		if (v != 0) {
			clc();
			if ((v & 0xff) != 0) {
				lda_imm(v & 0xff);
				adc_ab(dest);
				sta_ab(dest);
				if (0 != ((v >> 8) & 0xff)) {
					lda_imm((v >> 8) & 0xff);
					adc_ab(dest + 1);
					sta_ab(dest + 1);
				}
				else {
					Label temp;
					bcc(temp);
					inc_ab(dest + 1);
					temp.here();
				}
			}
			else {
				lda_imm((v >> 8) & 0xff);
				adc_ab(dest + 1);
				sta_ab(dest + 1);
			}
		}
	})

	ONE_ADDRESS(sub16_imm, dest,  v, {
		if (v != 0) {
			sec();
			if ((v & 0xff) != 0) {
				lda_ab(dest);
				sbc_imm(v & 0xff);
				sta_ab(dest);
				if (0 != ((v >> 8) & 0xff)) {
					lda_ab(dest + 1);
					sbc_imm((v >> 8) & 0xff);
					sta_ab(dest + 1);
				}
				else {
					Label temp;
					bcs(temp);
					dec_ab(dest + 1);
					temp.here();
				}
			}
			else {
				lda_ab(dest + 1);
				sbc_imm((v >> 8) & 0xff);
				sta_ab(dest + 1);
			}
		}
	})
	TWO_ADDRESS(add_16,dest, src, {
		clc();
		lda_ab(dest);
		adc_ab(src);
		sta_ab(dest);
		lda_ab(dest + 1);
		adc_ab(src + 1);
		sta_ab(dest + 1);
	})

	TWO_ADDRESS(sub_16, dest,  src, {
		sec();
		lda_ab(dest);
		sbc_ab(src);
		sta_ab(dest);
		lda_ab(dest + 1);
		sbc_ab(src + 1);
		sta_ab(dest + 1);
	})
	TWO_ADDRESS(rsub_16, dest, src, {
		sec();
		lda_ab(src);
		sbc_ab(dest);
		sta_ab(dest);
		lda_ab(src + 1);
		sbc_ab(dest + 1);
		sta_ab(dest + 1);
		})

	TWO_ADD_CMP(s_lt_16, dest, src, to, {
		lda_ab(dest);
		cmp_ab(src);
		lda_ab(dest + 1);
		sbc_ab(src + 1);
		Label temp;
		bvc(temp);
		eor_imm(0x80);
		temp.here();
		bmi(to,force_short);
		})
	TWO_ADD_CMP(lt_16, dest, src, to, {
		lda_ab(dest);
		cmp_ab(src);
		lda_ab(dest + 1);
		sbc_ab(src + 1);
		bcc(to,force_short);
		})

	TWO_ADD_CMP(s_ge_16, dest, src, to, {
		lda_ab(dest);
		cmp_ab(src);
		lda_ab(dest + 1);
		sbc_ab(src + 1);
		Label temp;
		bvc(temp);
		eor_imm(0x80);
		temp.here();
		bpl(to,force_short);
		})
	TWO_ADD_CMP(ge_16, dest, src, to, {
		lda_ab(dest);
		cmp_ab(src);
		lda_ab(dest + 1);
		sbc_ab(src + 1);
		bcs(to,force_short);
		})

	TWO_ADD_CMP(s_gt_16, src, dest, to, {
		lda_ab(dest);
		cmp_ab(src);
		lda_ab(dest + 1);
		sbc_ab(src + 1);
		Label temp;
		bvc(temp);
		eor_imm(0x80);
		temp.here();
		bmi(to,force_short);
		})
	TWO_ADD_CMP(gt_16, src, dest, to, {
		lda_ab(dest);
		cmp_ab(src);
		lda_ab(dest + 1);
		sbc_ab(src + 1);
		bcc(to,force_short);
		})

	TWO_ADD_CMP(s_le_16, src, dest, to, {
		lda_ab(dest);
		cmp_ab(src);
		lda_ab(dest + 1);
		sbc_ab(src + 1);
		Label temp;
		bvc(temp);
		eor_imm(0x80);
		temp.here();
		bpl(to,force_short);
		})
	TWO_ADD_CMP(le_16, src, dest, to, {
		lda_ab(dest);
		cmp_ab(src);
		lda_ab(dest + 1);
		sbc_ab(src + 1);
		bcs(to,force_short);
		})

	TWO_ADD_CMP(ne_16, src, dest, to, {
		lda_ab(dest);
		cmp_ab(src);
		bne(to,force_short);
		lda_ab(dest+1);
		cmp_ab(src+1);
		bne(to, force_short);
		})
	TWO_ADD_CMP(eq_16, src, dest, to, {
		lda_ab(dest);
		cmp_ab(src);
		Label temp;
		bne(temp);
		lda_ab(dest + 1);
		cmp_ab(src + 1);
		beq(to, force_short);
		temp.here();
		})


	ONE_ADD_CMP(ne_imm_16, dest, src, to, {
		lda_ab(dest);
		cmp_imm(src&0xff);
		bne(to,force_short);
		lda_ab(dest + 1);
		cmp_imm((src>>8)&0xff);
		bne(to, force_short);
		})

	ONE_ADD_CMP(eq_imm_16, dest, src, to, {
		lda_ab(dest);
		cmp_imm(src&0xff);
		Label temp;
		bne(temp);
		lda_ab(dest + 1);
		cmp_imm((src >>8)&0xff);
		beq(to, force_short);
		temp.here();
		})

	ONE_ADD_CMP(s_lt_imm_16, dest, src, to, {
		lda_ab(dest);
		cmp_imm(src&0xff);
		lda_ab(dest + 1);
		sbc_imm((src>>8)&0xff);
		Label temp;
		bvc(temp);
		eor_imm(0x80);
		temp.here();
		bmi(to,force_short);
		})
	ONE_ADD_CMP(lt_imm_16, dest, src, to, {
		lda_ab(dest);
		cmp_imm(src & 0xff);
		lda_ab(dest + 1);
		sbc_imm((src >> 8) & 0xff);
		bcc(to,force_short);
		})
	ONE_ADD_CMP(s_ge_imm_16, dest, src, to, {
		lda_ab(dest);
		cmp_imm(src&0xff);
		lda_ab(dest + 1);
		sbc_imm((src>>8)&0xff);
		Label temp;
		bvc(temp);
		eor_imm(0x80);
		temp.here();
		bpl(to,force_short);
		})
	ONE_ADD_CMP(ge_imm_16, dest, src, to, {
		lda_ab(dest);
		cmp_imm(src & 0xff);
		lda_ab(dest + 1);
		sbc_imm((src >> 8) & 0xff);
		bcs(to,force_short);
		})

	ONE_ADD_CMP(s_gt_imm_16, src, dest, to, {
		lda_imm(dest&0xff);
		cmp_ab(src);
		lda_imm((dest>>8)&0xff);
		sbc_ab(src + 1);
		Label temp;
		bvc(temp);
		eor_imm(0x80);
		temp.here();
		bmi(to,force_short);
		})
	ONE_ADD_CMP(gt_imm_16, src, dest, to, {
		lda_imm(dest & 0xff);
		cmp_ab(src);
		lda_imm((dest >> 8) & 0xff);
		sbc_ab(src + 1);
		bcc(to,force_short);
		})

	ONE_ADD_CMP(s_le_imm_16, src, dest, to, {
		lda_imm(dest&0xff);
		cmp_ab(src);
		lda_imm((dest>>8)&0xff);
		sbc_ab(src + 1);
		Label temp;
		bvc(temp);
		eor_imm(0x80);
		temp.here();
		bpl(to,force_short);
		})

	ONE_ADD_CMP(le_imm_16, src, dest, to, {
		lda_imm(dest & 0xff);
		cmp_ab(src);
		lda_imm((dest >> 8) & 0xff);
		sbc_ab(src + 1);
		bcs(to,force_short);
		})

	TWO_ADD_CMP(s_lt, dest, src, to, {
		sec();
		lda_ab(dest);
		sbc_ab(src);
		Label temp;
		bvc(temp);
		eor_imm(0x80);
		temp.here();
		bmi(to,force_short);
		})
	TWO_ADD_CMP(lt, dest, src, to, {
		lda_ab(dest);
		cmp_ab(src);
		bcc(to,force_short);
		})

	TWO_ADD_CMP(s_ge, dest, src, to, {
		sec();
		lda_ab(dest);
		sbc_ab(src);
		Label temp;
		bvc(temp);
		eor_imm(0x80);
		temp.here();
		bpl(to,force_short);
		})
	TWO_ADD_CMP(ge, dest, src, to, {
		lda_ab(dest);
		cmp_ab(src);
		bcs(to,force_short);
		})

	TWO_ADD_CMP(s_gt, src, dest, to, {
		sec();
		lda_ab(dest);
		sbc_ab(src);
		Label temp;
		bvc(temp);
		eor_imm(0x80);
		temp.here();
		bmi(to,force_short);
		})

	TWO_ADD_CMP(gt, src, dest, to, {
		lda_ab(dest);
		cmp_ab(src);
		bcc(to,force_short);
		})

	TWO_ADD_CMP(s_le, src, dest, to, {
		sec();
		lda_ab(dest);
		sbc_ab(src);
		Label temp;
		bvc(temp);
		eor_imm(0x80);
		temp.here();
		bpl(to,force_short);
		})

	TWO_ADD_CMP(le, src, dest, to, {
		lda_ab(dest);
		cmp_ab(src);
		bcs(to,force_short);
		})

	TWO_ADD_CMP(ne, src, dest, to, {
		lda_ab(dest);
		cmp_ab(src);
		bne(to,force_short);
		})
	TWO_ADD_CMP(eq, src, dest, to, {
		lda_ab(dest);
		cmp_ab(src);
		beq(to, force_short);
		})


	ONE_ADD_CMP(s_lt_imm, dest, src, to, {
		lda_ab(dest);
		if (src!=0){
			sec();
			sbc_imm(src & 0xff);
			Label temp;
			bvc(temp);
			eor_imm(0x80);
			temp.here();
		}
		bmi(to, force_short);
		})
	ONE_ADD_CMP(lt_imm, dest, src, to, {
		if (src != 0) {
			lda_ab(dest);
			cmp_imm(src & 0xff);
			bcc(to, force_short);
		}
		})
	ONE_ADD_CMP(s_ge_imm, dest, src, to, {
		lda_ab(dest);
		if (src != 0) {
			sec();
			sbc_imm(src & 0xff);
			Label temp;
			bvc(temp);
			eor_imm(0x80);
			temp.here();
		}
		bpl(to,force_short);
		})
		ONE_ADD_CMP(ge_imm, dest, src, to, {
			if (src != 0) {
				lda_ab(dest);
				cmp_imm(src & 0xff);
				bcs(to, force_short);
			} else bra(to, force_short);
			})

	ONE_ADD_CMP(s_gt_imm, src, dest, to, {
		if (dest==0){
			lda_ab(src);
			Label temp;
			beq(temp);
			bpl(to,force_short);
			temp.here();
		}
		else if (dest == -1) {
			lda_ab(src);
			bpl(to, force_short);
		}
		else {
			sec();
			lda_imm(dest & 0xff);
			sbc_ab(src);
			Label temp;
			bvc(temp);
			eor_imm(0x80);
			temp.here();
			bmi(to,force_short);
		}
	})

	ONE_ADD_CMP(s_le_imm, src, dest, to, {
		if (dest == 0) {
			lda_ab(src);
			bmi(to,force_short);
			beq(to,force_short);
		}
		else if (dest == -1) {
			lda_ab(src);
			bmi(to, force_short);
		}
		else {
			sec();
			lda_imm(dest & 0xff);
			sbc_ab(src);
			Label temp;
			bvc(temp);
			eor_imm(0x80);
			temp.here();
			bpl(to,force_short);
		}
		})

		void set_vera_imm(int port, int addr, int inc)
		{
			lda_imm(port);
			sta_abs(0x9f25);//select
			lda_imm(addr & 0xff);
			sta_abs(0x9f20);
			lda_imm((addr>>8) & 0xff);
			sta_abs(0x9f21);
			lda_imm((addr >> 16) + (inc<<4));
			sta_abs(0x9f22);
		}
		void set_vera(int port, int addr, int inc)
		{
			lda_imm(port);
			sta_abs(0x9f25);//select
			lda_ab(addr);
			sta_abs(0x9f20);
			lda_ab(addr+1);
			sta_abs(0x9f21);
			lda_ab(addr+2);
			clc();
			adc_imm(inc << 4);
			sta_abs(0x9f22);
		}
		void set_vera(int port, LabelBase& addr, int inc)
		{
			lda_imm(port);
			sta_abs(0x9f25);//select
			lda_ab(addr);
			sta_abs(0x9f20);
			lda_ab(addr + 1);
			sta_abs(0x9f21);
			lda_ab(addr + 2);
			clc();
			adc_imm(inc << 4);
			sta_abs(0x9f22);
		}
		void set_vera(int port, LabelBase&& addr, int inc)
		{
			lda_imm(port);
			sta_abs(0x9f25);//select
			lda_ab(addr);
			sta_abs(0x9f20);
			lda_ab(addr + 1);
			sta_abs(0x9f21);
			lda_ab(addr + 2);
			clc();
			adc_imm(inc << 4);
			sta_abs(0x9f22);
		}
		void store_vera(int port)
		{
			sta_abs(0x9f23+port);
		}
		void load_vera(int port)
		{
			lda_abs(0x9f23 + port);
		}
		void st_vera_imm(int p, int v)
		{
			lda_imm(v);
			store_vera(p);
		}
		void st_vera0_16_imm(int p, int v)
		{
			lda_imm(v&0xff);
			store_vera(p);
			lda_imm(v >>8);
			store_vera(p);
		}
		void st_vera(int p, int v)
		{
			lda_ab(v);
			store_vera(p);
		}
		void ld_vera(int p, LabelBase& v)
		{
			load_vera(p);
			sta_ab(v);
		}
		void ld_vera(int p, LabelBase&& v)
		{
			load_vera(p);
			sta_ab(v);
		}
		void st_vera(int p, LabelBase& v)
		{
			lda_ab(v);
			store_vera(p);
		}
		void st_vera(int p, LabelBase&& v)
		{
			lda_ab(v);
			store_vera(p);
		}
		void st_vera_16(int p, int v)
		{
			lda_ab(v);
			store_vera(p);
			lda_ab(v+1);
			store_vera(p);
		}
		void st_vera_16(int p, LabelBase& v)
		{
			lda_ab(v);
			store_vera(p);
			lda_ab(v + 1);
			store_vera(p);
		}
		void st_vera_16(int p, LabelBase&& v)
		{
			lda_ab(v);
			store_vera(p);
			lda_ab(v + 1);
			store_vera(p);
		}
		void ld_vera_16(int p, int v)
		{
			load_vera(p);
			sta_ab(v);
			load_vera(p);
			sta_ab(v + 1);
		}
		void ld_vera_16(int p, LabelBase& v)
		{
			load_vera(p);
			sta_ab(v);
			load_vera(p);
			sta_ab(v + 1);
		}
		void ld_vera_16(int p, LabelBase&& v)
		{
			load_vera(p);
			sta_ab(v);
			load_vera(p);
			sta_ab(v + 1);
		}

		void st_vera_x(int p, int v)
		{
			lda_ax(v);
			store_vera(p);
		}
		void st_vera_x(int p, LabelBase& v)
		{
			lda_ax(v);
			store_vera(p);
		}
		void st_vera_x(int p, LabelBase&& v)
		{
			lda_ax(v);
			store_vera(p);
		}
		void ld_vera_x(int p, int v)
		{
			load_vera(p);
			sta_ax(v);
		}
		void ld_vera_x(int p, LabelBase& v)
		{
			load_vera(p);
			sta_ax(v);
		}
		void ld_vera_x(int p, LabelBase&& v)
		{
			load_vera(p);
			sta_ax(v);
		}

		void vera_to_vera(int p1, int p2)
		{
			load_vera(p1);
			store_vera(p2);
		}


		void st_vera_16_x(int p, int v)
		{
			lda_ax(v);
			store_vera(p);
			inx();
			lda_ax(v);
			store_vera(p);
			inx();
		}
		void st_vera_16_x(int p, LabelBase& v)
		{
			lda_ax(v);
			store_vera(p);
			inx();
			lda_ax(v);
			store_vera(p);
			inx();
		}
		void st_vera_16_x(int p, LabelBase&& v)
		{
			lda_ax(v);
			store_vera(p);
			inx();
			lda_ax(v);
			store_vera(p);
			inx();
		}
		void ld_vera_16_x(int p, int v)
		{
			load_vera(p);
			sta_ax(v);
			inx();
			load_vera(p);
			sta_ax(v);
			inx();
		}
		void ld_vera_16_x(int p, LabelBase& v)
		{
			load_vera(p);
			sta_ax(v);
			inx();
			load_vera(p);
			sta_ax(v);
			inx();
		}
		void ld_vera_16_x(int p, LabelBase&& v)
		{
			load_vera(p);
			sta_ax(v);
			inx();
			load_vera(p);
			sta_ax(v);
			inx();
		}

		void st_vera_izy(int p, int v)
		{
			lda_izy(v);
			store_vera(p);
		}
		void st_vera_izx(int p, int v)
		{
			lda_izx(v);
			store_vera(p);
		}
		void st_vera_16_izy(int p, int v)
		{
			lda_izy(v);
			store_vera(p);
			iny();
			lda_izy(v);
			store_vera(p);
			iny();
		}

		void ld_vera_izy(int p, int v)
		{
			load_vera(p);
			sta_izy(v);
		}
		void ld_vera_izx(int p, int v)
		{
			load_vera(p);
			sta_izx(v);
		}
		void ld_vera_16_izy(int p, int v)
		{
			load_vera(p);
			sta_izy(v);
			iny();
			load_vera(p);
			sta_izy(v);
			iny();
		}


};

