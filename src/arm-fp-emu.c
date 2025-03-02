#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <link.h>
#include "librunt.h"
#include "maps.h"		
#include "dso-meta.h"
#include "relf.h"
#include "debug-print.h"
#include "rmaps.h"
#include "assembly.h"

// Fixes undefined symbols at build stage: the --defsym compiler flag doesn't solve this.
char* __private_strdup(const char *s) { return strdup(s); }
void* __private_malloc(size_t size) { return malloc(size); }

/*
* Sets the write flag for a region of memory.
* A 'guarantee' pointer ensures that this region includes that
* address.
*/
int make_writable(void* from, void* to, void* guarantee) {
	
	from = ROUND_DOWN_PTR_TO_PAGE(from);
	if (guarantee != NULL && (guarantee < from || guarantee > to)) {
		printfdbg("ERROR: Couldn't make region writable\n");
		return -1;
	}

	size_t len = to - from;
	int perms = PROT_READ | PROT_WRITE | PROT_EXEC;
	
	printfdbg("mprotect(%p, %d, rwx)\n", from, len);
	int ret = mprotect(from, len, perms);
	
	if (ret != 0) {
		printfdbg("ERROR: Couldn't make region writable %s\n");
		perror("mprotect");
		return -1;
	}
	return 0;
}

/*
* Returns a pointer to the end of an ELF header
*/
void* end_of_header(void* sections_start) {
	Elf32_Ehdr* header = (Elf32_Ehdr*) sections_start;

	// Elf32_Off is typedef'd in elf.h as uint32_t:
	//	"typedef uint32_t Elf32_Off;"
	uint32_t ph_table_offset = header->e_phoff; 
	// Elf32_Half is also uint16_t
	uint16_t ph_table_len = ((uint16_t) header->e_phnum) * ((uint16_t) header->e_phentsize);
	
	return ((int8_t*) sections_start) + ph_table_offset + ph_table_len; 
}

/*
* Attempts to set the write flag for a region of memory.
* Returns whether change was successful (0 == success)
*/
int try_set_mem_writable(struct maps_entry *maps_ent, void* seg_start, void* seg_end) {
	if (maps_ent->w != 'w') {
		int ret = make_writable(seg_start, seg_end, NULL);
		if (ret == 0) {
			printfdbg("Write perms now set for %p-%p.\n", ROUND_DOWN_PTR_TO_PAGE(seg_start), seg_end);
			return 0;
		} else {
			printfdbg("ERROR: Failure to make writable\n");
			exit(1);
		}
	} else {
		printfdbg("Write perms already set (%c%c%c), continuing.\n", maps_ent->r, maps_ent->w, maps_ent->x);
		return 1;
	}
}

/*
* Core of the instrumentation process.
* Looks through a mapped region in memory and finds a floating-point instruction.
* If one is found, it is replaced by a branch instruction and a trampoline is made and 
* written to somewhere in memory.
*/
void replace_instrs_in_segment(struct maps_entry *maps_ent, void* from, void* to) {
    void* sections_start = from; 
    void* sections_end = to;
    void* seg_start = maps_ent->first;
    void* seg_end = maps_ent->second;
        
    printfdbg("\tWe have entered replace_instructions() \n");
    printfdbg("\tRange of executable instructions (inside segment range): \n");
	printfdbg("\t");
    if (seg_start != sections_start) {
       printfdbg("(%p-)",seg_start);
    }
	printfdbg("%p-%p", sections_start, sections_end);
    if (seg_end != sections_end) {
    	printfdbg("(-%p)",seg_end);
    }
    printfdbg("\n");

	void* instrs_start = sections_start;
	if (0 == strncmp(sections_start, "\177ELF",4)) {
		// Probably an ELF header so skip it
		instrs_start = end_of_header(sections_start);
		printfdbg("ELF headed spotted at %p, skipping to %p\n", sections_start, instrs_start);
	}
	
	assert(instrs_start >= seg_start && instrs_start <= seg_end);
	assert(sections_end >= seg_start && sections_end <= seg_end);
	
	int b_is_writable = maps_ent->w == "w";
	int b_did_perm_change = 0;
		
	printfdbg("Scanning through %p-%p for FP instructions\n", instrs_start, sections_end);
	for (int8_t* instr = instrs_start; instr < sections_end-4; instr += 2) {
		void* tramp = generate_trampoline(instr);
		if (tramp == NULL) {
			continue;
		} else {
			printfdbg("Trampoline written for FP instruction at %p in %p-%p\n", instr, instrs_start, sections_end);
			printfdbg(" - writing jump at the mentioned instr (%p)\n", instr);
			if (!b_is_writable && 0 == try_set_mem_writable(maps_ent, seg_start, seg_end)) {
				b_is_writable = 1;
				b_did_perm_change = 1;
			}
			insert_probe(instr, tramp);
		}
	}
}

/*
* Callback for librunt. 
* Librunt passes information about a mapped region of memory and 
* this callback method does some setup then instruments it.
*
* In full transparency, this method resembles code belonging to my supervisor Dr. Stephen Kell.
* His project "libsystrap" uses his other project "librunt" which is also a dependency for this project.
* Not only does some of the code look similar because they are using librunt in mostly the same way because
* they both are rewriting code in memory, but libsystrap contributed to what I used to learn about instrumentation 
* and how to use librunt.
* The relevant code is spread throughout the file "src/trap.c" which can be found at the link below. This includes the methods
* "trap_one_executable_region_given_shdrs", "trap_one_executable_region", and "trap_one_instruction_range".
* https://github.com/stephenrkell/libsystrap/blob/21c5b00eb256f5489ee0d163efecc3398dfef2c9/src/trap.c
*/
static int handle_maps_entry(struct maps_entry *maps_ent, char *linebuf, void *arg) {
	// Tests for memory regions that don't need instrumenting.
	// "[" is here to ensure stability by exercising overcaution, but in practice you'd want
	// to be more specific like the rest of the search strings.
	char* to_skip[] = {"[", "[stack]", "[vvar]", "[sigpage]", "[vdso]", "[vectors]", "libm-2.31.so", "libkeystone.so.0", "libcapstone.so.4"};
	for (int i = 0; i < sizeof(to_skip) / sizeof(to_skip[0]); i++) {
		if (NULL != strstr(maps_ent->rest, to_skip[i])) {
			printfdbg("\tSkipping %s\n", maps_ent->rest);	
			return 0;
		}
	}
	printfdbg("Handling maps entry for \"%s\"\n", maps_ent->rest);
	printfdbg("\tGetting file metadata.\n");
	struct file_metadata* meta = __runt_files_metadata_by_addr(maps_ent->first);
	if (meta == NULL) {
		printfdbg("File metadata not found for: %s\n", maps_ent->rest);
		return 1;
	} else if (maps_ent->x != 'x') {
		printfdbg("\tNot executable, skipping.\n");
		return 0;
	}

	// "Base address shared object is loaded at" - definition
	ElfW(Addr) l_addr = meta->l->l_addr;

	if (meta->shdrs == NULL) {
		printfdbg("\tNo section headers: file_metadata->shdrs is null.\n");
		return 1;
	} else {
		printfdbg("\tFound section headers.\n");
	}

	// Range within segment containing exactly only sections
	void* sections_from = l_addr + find_section_boundary((uintptr_t) (maps_ent->first - l_addr), \
			SHF_EXECINSTR, \
			0, \
			meta->shdrs, \
			meta->ehdr->e_shnum, \
			NULL);
	void* sections_to = l_addr + find_section_boundary((uintptr_t) (maps_ent->second - l_addr), \
			SHF_EXECINSTR, \
			1, \
			meta->shdrs, \
			meta->ehdr->e_shnum, \
			NULL);

	printfdbg("%s\n", maps_ent->rest);
	assert(maps_ent->first <= sections_from && sections_from <= sections_to && sections_to <= maps_ent->second);
	printfdbg("Maps entry goes from (%p-)%p-%p(-%p)\n", maps_ent->first, sections_from, sections_to, maps_ent->second);
	
	replace_instrs_in_segment(maps_ent, sections_from, sections_to);
	return 0;
}

/*
* Called by the dynamic linker/loader before the base program. 
* This is where the instrumentation happens.
*/
static int entrypoint(void) __attribute__((constructor(101)));
static int entrypoint(void) {
	int fd = open_maps();
	start_disasm_engine();
	start_asm_engine();
	emulator_init();
	
	// Replace instructions
	process_all_lines(fd, handle_maps_entry);

    close_maps();
    stop_disasm_engine();
    stop_asm_engine();
    return 0;
}
