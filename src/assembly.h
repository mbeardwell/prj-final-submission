#include <keystone/keystone.h>
#include <capstone/capstone.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <stddef.h>
#include "fpuemu.h"
#include <unistd.h>
#include <stdlib.h>
#define PAGE_SIZE sysconf(_SC_PAGE_SIZE)

int8_t MIN_PRINTABLE_ASCII = (int8_t) 0x20; // 32
int8_t MAX_PRINTABLE_ASCII = (int8_t) 0x7F; // 127
int INT24_MIN = -(1 << 23);
int INT24_MAX = (1 << 23) - 1;

/*
* Template for the trampolines used to connect
* a probe site and emulation routine at run-time.
*/
int8_t template[] = {
	0xFF, 0x5F, 0x2D, 0xE9, // push {r0-r12, r14}
	0xAD, 0x5E, 0x0D, 0xE3, // movw r5, #0xdead
	0x05, 0x58, 0xA0, 0xE1, // lsl r5, r5, #16
	0xEF, 0x6E, 0x0B, 0xE3, // movw r6, #0xbeef
	0x06, 0x50, 0x85, 0xE1, // orr r5, r5, r6
	0x00, 0x00, 0xA0, 0xE3, // mov r0, #0
	0x00, 0x10, 0xA0, 0xE3, // mov r1, #0
	0x00, 0x20, 0xA0, 0xE3, // mov r2, #0
	0x00, 0x30, 0xA0, 0xE3, // mov r3, #0
	0x35, 0xFF, 0x2F, 0xE1, // blx r5
	0xFF, 0x5F, 0xBD, 0xE8, // pop {r0-r12, r14}
	0xFE, 0xFF, 0xFF, 0xEA  // b #0
};

/*
* Offsets of various instructions in the trampoline
* template and the names of registers used.
*/
int MOV_UPPER_OFFSET = 1 * 4;
int MOV_LOWER_OFFSET = 3 * 4;
int MOV_R0_OFFSET = 5 * 4;
int MOV_R1_OFFSET = 6 * 4;
int MOV_R2_OFFSET = 7 * 4;
int MOV_R3_OFFSET = 8 * 4;
int RET_OFFSET = 11 * 4;
int REG_SCRATCH = 6;
int REG_CALL = 5;

/*
* Capstone (disassembly framework) and 
* Keystone (assembly framework) engine handles.
*/
csh* cs_handle;
ks_engine* ks_handle;

/*
* Copy four bytes from one location into another.
* Used to replace four-byte ARM instructions.
*/
void clobber(void* dst, void* src) {
	printfdbg("clobbering\n");
	make_writable(dst, dst + 4, NULL);
	printfdbg(" - writing src into dst\n");
	memcpy(dst, src, 4);
}

void start_asm_engine() {
	if(ks_open(KS_ARCH_ARM, KS_MODE_ARM, &ks_handle) != KS_ERR_OK) {
		printfdbg("\tUnable start the keystone assembly engine.\n");
	}
}

void stop_asm_engine() {
	ks_close(ks_handle);
}

void start_disasm_engine() {
	if(cs_open(CS_ARCH_ARM, CS_MODE_ARM, &cs_handle) != CS_ERR_OK) {
		printfdbg("\tUnable start the capstone disassembly engine.\n");
		exit(-1);
	}
	cs_malloc(cs_handle);
	
	// enable full range of disassembly information
	cs_option(cs_handle, CS_OPT_DETAIL, CS_OPT_ON);
}

void stop_disasm_engine() {
      cs_close(cs_handle);
}

/*
* Use the Keystone assembler to convert a string in
* assembly to machine-code.
*/
int8_t* assemble_instr(char* assembly) {
	int8_t* instr;
	size_t size;
	size_t count;
	if (ks_asm(ks_handle, assembly, 0, &instr, &size, &count) != KS_ERR_OK) {
		printfdbg("Unable to assemble instruction '%s'\n", assembly);
		exit(-1);
	}
	return instr;
}

/*
* Returns a pointer to a machine-code branch 
* instruction (ARM) that branches to the given offset.
* 'offset' is a human-readable string like "0xfae".
*/
int8_t* assemble_branch(char* offset) {
	char assembly[100];
	sprintf(assembly, "b #%s", offset);
	return assemble_instr(assembly);
}

/*
* Returns a pointer to a machine-code 'mov' 
* instruction (ARM). E.g. reg = 5 for r5. 'val' is 
* the immediate value.
*/
int8_t* assemble_mov(uint8_t reg, uint16_t val) {
	int8_t instr[] = {
		0xEF, 0x1E, 0x0B, 0xE3 // movw r1, #0xbeef
	};
		
	if (reg > 0xF) {
		printfdbg("assemble_mov() - Register not supported: %lu", reg);
		return NULL;
	}
	
	// set register
	instr[1] = (reg & 0x0F) << 4;

	// the immediate value is divided and placed in interesting positions 
	// (value's nibbles 0,1,2,3 to mov's nibbles 5,3,0,1)
	// so various logical operations are needed
	// Also it is a "byte" array not a nibble array so OR-ing is needed
	instr[2] = (instr[2] & 0xF0) | ((val & 0xF000) >> 12);
	instr[1] = (instr[1] & 0xF0) | ((val & 0x0F00) >> 8);
	instr[0] = val & 0x00FF;
	
	int8_t* outbuf = malloc(4*sizeof(int8_t));
	memcpy(outbuf, instr, sizeof(instr));
	return outbuf;
}

/*
* Returns a struct that contains information about what instruction
* exists at the provided pointer.
*/
cs_insn* disassemble_instr(void* p_instr) {
    int8_t code[4];
    memcpy(code, (int8_t*) p_instr, 4); 
	
	cs_insn* instr;
	assert(cs_handle != NULL);
    int count = cs_disasm(cs_handle, p_instr, 4, 0, 1, &instr);
    
    cs_insn* output;
    if (count == 0 || 0 == strcmp(instr[0].mnemonic, "") ) {
		output = NULL;
	} else {
		cs_insn* insn_copy = malloc(sizeof(cs_insn));
		memcpy(insn_copy, &instr[0], sizeof(cs_insn));
		output = insn_copy;
		cs_free(instr, count);
	}
	
	char* name = output == NULL ? NULL : cs_insn_name(cs_handle, output->id);
    return output;
}

/*
* Returns the name of the instruction at the
* provided pointer.
*/
char* instr_name(void* instr){
	cs_insn* disassembly = disassemble_instr(instr);
	if (disassembly == NULL) {
		return NULL;
	} 	
	char* name = cs_insn_name(cs_handle, disassembly->id);
	char* out = malloc(10);
	strcpy(out, name);
	free(disassembly); 
	return out;
}

/*
* In the instrumentation stage, this method displaces the floating-point
* instruction with a branch that points to the start of a pre-written trampoline.
* The trampoline is also written to so that when it returns, it returns to just after
* the displaced FP instruction.
*/
int insert_probe(void* instr, void* tramp) {
	printfdbg("Inserting probe at %p to connect to trampoline at %p\n", instr, tramp);

	// Calculate trampoline offset	
	ptrdiff_t offset = ((int8_t*) tramp - (int8_t*) instr);
	ptrdiff_t offset_reverse = (((int8_t*) instr + 4) - ((int8_t*) tramp + RET_OFFSET));
	// Ensure trampoline is close enough for the offset to be written
	assert(INT24_MIN <= offset && offset <= INT24_MAX);
	assert(INT24_MIN <= offset_reverse && offset_reverse <= INT24_MAX);
	
	// Convert offset into a string to pass to the assembler
	char str_offset[100];
	char str_offset_reverse[100];
	sprintf(str_offset, "%td", offset);		
	sprintf(str_offset_reverse, "%td", offset_reverse);		
	int8_t* probe_site_to_tramp = assemble_branch(str_offset); 
	int8_t* tramp_to_probe_site = assemble_branch(str_offset_reverse); 
	
	#ifdef DO_DBG_PRINT
	char* before = instr_name(instr);
	char* after = instr_name(probe_site_to_tramp);
	char* back_again = instr_name(tramp_to_probe_site);

	printfdbg(" - writing tramp branch into instr at %p\n", instr);
	printfdbg("     - instr: %02x%02x%02x%02x", 
		*((int8_t*) instr)&0xff, 
		*((int8_t*) instr + 1) & 0xff, 
		*((int8_t*) instr + 2) & 0xff, 
		*((int8_t*) instr + 3) & 0xff);
	printfdbg(" (%s)\n", before);
	printfdbg("     - assembly: %02x%02x%02x%02x", 
		*((int8_t*) probe_site_to_tramp)&0xff, 
		*((int8_t*) probe_site_to_tramp + 1) & 0xff, 
		*((int8_t*) probe_site_to_tramp + 2) & 0xff, 
		*((int8_t*) probe_site_to_tramp + 3) & 0xff);
	printfdbg(" (%s)\n", after);
	
	printfdbg(" - writing return branch into tramp at %p\n", (int8_t*) tramp + RET_OFFSET);
	printfdbg("     - instr: %02x%02x%02x%02x", 
		*((int8_t*) tramp + RET_OFFSET)&0xff, 
		*((int8_t*) tramp + RET_OFFSET + 1) & 0xff, 
		*((int8_t*) tramp + RET_OFFSET + 2) & 0xff, 
		*((int8_t*) tramp + RET_OFFSET + 3) & 0xff);
	printfdbg(" (%s)\n", before);
	printfdbg("     - assembly: %02x%02x%02x%02x", 
		*((int8_t*) probe_site_to_tramp) & 0xff, 
		*((int8_t*) probe_site_to_tramp + 1) & 0xff, 
		*((int8_t*) probe_site_to_tramp + 2) & 0xff, 
		*((int8_t*) probe_site_to_tramp + 3) & 0xff);
	printfdbg(" (%s)\n", after);
	#endif
	
	// Replace FP instruction with branch
	clobber(instr, probe_site_to_tramp);

	// Add return branch to the end of the trampoline
	clobber((int8_t*)tramp + RET_OFFSET, tramp_to_probe_site);
	
	#ifdef DO_DBG_PRINT
	printfdbg(" - branch written\n");	
	printfdbg(" - Therefore, '%s' replaced with '%s' at %p\n", before, after, instr);
	free(before);
	free(after);
	free(back_again);
	#endif 

	ks_free(probe_site_to_tramp);
	ks_free(tramp_to_probe_site);
}

/*
* Finds and reserves a page of memory near 'instr_addr'. 
* Returns a pointer to the start of this page.
*/
void* mmap_nearby(void* instr_addr) {
	unsigned int perms = PROT_EXEC | PROT_READ | PROT_WRITE;
	unsigned int flags = MAP_FIXED_NOREPLACE | MAP_PRIVATE | MAP_ANONYMOUS;

	void* range_low = 0x0;
	void* range_high = 0xFFFFFFFF;
	if (instr_addr >= -INT24_MIN) {
		range_low = instr_addr + INT24_MIN;
	}
	if (instr_addr <= range_high - INT24_MAX) {
		range_high = instr_addr + INT24_MAX;
	}	
	assert(range_low <= range_high);
	void* search_from = ROUND_UP_PTR_TO_PAGE(range_low);
	void* search_to = ROUND_DOWN_PTR_TO_PAGE(range_high);
	printfdbg("mmap_nearby: We have %p-%p range but will look at pages %p-%p\n", range_low, range_high, search_from, search_to);
	
	assert(search_from <= instr_addr && instr_addr <= search_to);
	
	void* map_region = NULL;
	printfdbg("Searching from %p to %p\n", search_from, search_to);
	for (int page_start = search_from; page_start < search_to; page_start += PAGE_SIZE) {
		printfdbg("mmap(%p, %d, ...) = ", page_start, sizeof(template));
		map_region = mmap(page_start, sizeof(template), perms, flags, -1, 0);
		printfdbg("%p (should be %p)\n", map_region, page_start);
		if (map_region == page_start) {  // request accepted
			break;
		}
		#ifdef DO_DBG_PRINT
  		perror("mmap");
		#endif
	}
	if (map_region == MAP_FAILED || map_region == NULL) {
		printfdbg("ERROR: no space for trampoline near instruction %p (see mmap error below)\n", instr_addr);
		#ifdef DO_DBG_PRINT
  		perror("mmap");
		#endif
		return NULL;
	}
	return map_region;
}

/*
* Reserves a page of memory and writes the trampoline template to it.
*/
void* gen_template_tramp(void* instr_addr) {
	void* p_template = mmap_nearby(instr_addr);
	if (p_template == NULL) {
		printfdbg("ERROR: no space for trampoline near instruction %p (see mmap error below)\n", instr_addr);
		#ifdef DO_DBG_PRINT
  		perror("mmap");
		#endif
		return NULL;
	}
	memcpy(p_template, &template, sizeof(template));
	return p_template;
}

/*
* Shorthand to replace an instruction in a trampoline.
*/
void insert_tramp_instr(void* trampoline, void* instr, int offset) {
	memcpy(((int8_t*)trampoline) + offset, (int8_t*) instr, 4*sizeof(int8_t));
}

/*
* Inserts the emulation routine arguments into a trampoline.
*/
void tramp_insert_emu_args(void* trampoline, int arg1, int arg2, int arg3, int arg4) {
	int8_t* mov_r0 = assemble_mov(0, arg1);
	int8_t* mov_r1 = assemble_mov(1, arg2);
	int8_t* mov_r2 = assemble_mov(2, arg3);
	int8_t* mov_r3 = assemble_mov(3, arg4);
	insert_tramp_instr(trampoline, mov_r0, MOV_R0_OFFSET);	
	insert_tramp_instr(trampoline, mov_r1, MOV_R1_OFFSET);	
	insert_tramp_instr(trampoline, mov_r2, MOV_R2_OFFSET);	
	insert_tramp_instr(trampoline, mov_r3, MOV_R3_OFFSET);	
	free(mov_r0);
	free(mov_r1);
	free(mov_r2);
	free(mov_r3);
}

/*
* Writes the address of the emulation routine into the trampoline for 
* branching later. As described in the report, the address is divided and 
* loaded in two parts into a register by instructions in the trampoline.
*/
void link_tramp_to_emu(void* trampoline, void* func_address) {
	// Split address into two two-byte pieces.
	int16_t word_lower = ((int32_t) func_address) & 0x0000FFFF;
	int16_t word_upper = (((int32_t) func_address) & 0xFFFF0000) >> 16;
		
	int8_t* mov_instr0 = assemble_mov(REG_CALL, word_upper);
	int8_t* mov_instr1 = assemble_mov(REG_SCRATCH, word_lower);
	
	if (mov_instr0 == NULL || mov_instr1 == NULL) {
		printfdbg("ERROR: couldn't assemble func address into mov");
		exit(1);
	}
	
	insert_tramp_instr(trampoline, mov_instr0, MOV_UPPER_OFFSET);
	insert_tramp_instr(trampoline, mov_instr1, MOV_LOWER_OFFSET);

	free(mov_instr0);
	free(mov_instr1);
}

/*
* Returns a pointer to the beginning of the trampoline or NULL if failed.
* In three places there is a hard-coded check for the 'vadd.f32' instruction as
* it is the only instruction emulated so far in the emulator. In a full solution,
* these checks would be removed.
*/
void* generate_trampoline(void* instr_addr) {
	cs_insn* disassembly = disassemble_instr(instr_addr);

	if (disassembly == NULL) {
		return NULL;
	} else if (disassembly->id != ARM_INS_VADD) {
		free(disassembly);
		return NULL;
	}
		
	// Get which S registers are used
	cs_arm* arm = &(disassembly->detail->arm);
	int Sd = arm->operands[0].reg;
	int Sn = arm->operands[1].reg;
	int Sm = arm->operands[2].reg;
	
	if (Sd != ARM_REG_S0 || Sn != ARM_REG_S0 || Sm != ARM_REG_S1 || arm->cc != ARM_CC_AL) return NULL;
	printfdbg("%s %s\n", disassembly->mnemonic, disassembly->op_str);
	printfdbg("This vadd (CC=%d) instruction uses the registers %d, %d, %d %d %d\n", arm->cc, Sd, Sn, Sm, arm->operands[3].reg, arm->operands[4].reg);
	assert(ARM_REG_S0 <= Sd  && Sd <= ARM_REG_S31);
	assert(ARM_REG_S0 <= Sn  && Sn <= ARM_REG_S31);
	assert(ARM_REG_S0 <= Sm  && Sm <= ARM_REG_S31);

	// Make trampoline
	int8_t* tramp = gen_template_tramp(instr_addr);
	if (tramp == NULL) {
		printfdbg("ERROR: failed to generate template trampoline\n");
		exit(1);
	}	
	
	// 'vadd_f32' is connected here as it is the only emulation routine present
	// but in practice you'd want to check the dissassembly above - the variable 'disassembly'.
	link_tramp_to_emu(tramp, &vadd_f32);
	
	// Put the args (names of S registers) into r0-r2
	tramp_insert_emu_args(tramp, Sd, Sn, Sm, 0); 

	printfdbg("Trampoline made for  instruction.");
	return tramp;
}