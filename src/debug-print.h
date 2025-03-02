/* 
* Uncommenting/commenting this 'define' statement will 
* enable or disable detailed console output useful for debugging.
*/
//#define DO_DBG_PRINT

#ifdef DO_DBG_PRINT
  #define printfdbg(...) printf (__VA_ARGS__)
#else
  #define printfdbg(...)
#endif

extern long int etext;
extern long int edata;

// Returns 0 or 1 depending on whether an address is in the range [from, to]
static int is_in_range(long unsigned int from, long unsigned int to, long unsigned int addr) {
	return ((from <= addr) && (addr <= to));
}

// Prints if a memory range contains the .text or .data sections
static int print_if_interesting_addr(long unsigned int from, long unsigned int to) {
	if (is_in_range(from, to, &etext)) {
		printfdbg("Range contains .text\n");
	} else if (is_in_range(from, to, &edata)) {
		printfdbg("Range contains .data\n");
	}
	return 0;
}

// Print human-readable information about a programs section headers
static void print_section_headers(struct file_metadata* meta, ElfW(Shdr)* shdrs, int count) {
	printfdbg("Section headers: \n");
	for (int i = 0; i<count; i++) {
		ElfW(Shdr)* shdr = shdrs + i;
		if (shdr == NULL) {
			printfdbg("\tshdrs[%d] does not exist. %d shdrs reported.\n", i, count);
			break;
		}
		int shstrtab_index = shdr->sh_name;
		char* section_name = meta->shstrtab + shstrtab_index;

		char short_name[100];
		if (strlen(section_name) == 0) strcpy(short_name, "(empty header name)");
		else short_name[0] = '\0';

		if (shdr->sh_addr == 0) {
			printfdbg("\t%s%s @ not loaded or base+0\n", short_name, section_name);
		} else {
			printfdbg("\t%s%s \t@ base+%lx\n", short_name, section_name, shdr->sh_addr);
		}
	}
	return;
}

// Print human-readable information about a programs program headers
static void print_program_headers(struct file_metadata* meta, ElfW(Phdr)* phdrs, int count) {
	printfdbg("Program headers: \n");
	for (int i = 0; i<count; i++) {
		ElfW(Phdr)* phdr = phdrs + i;
		if (phdr == NULL) {
			printfdbg("phdrs[%d] does not exist. %d phdrs reported.\n", i, count);
			break;
		}
		printfdbg("\t");
		switch (phdr->p_type) {
			case PT_NULL:
				printfdbg("PT_NULL - ignore the segment");
				break;
			case PT_LOAD:
				printfdbg("PT_LOAD - loadable segment");
				break;
			case PT_DYNAMIC:
				printfdbg("PT_DYNAMIC - dynamic linking info");
				break;
			case PT_INTERP:
				printfdbg("PT_INTERP - location interpreter");
				break;
			case PT_NOTE:
				printfdbg("PT_NOTE - location of Nhdr's (note headers)");
				break;
			case PT_SHLIB:
				printfdbg("PT_SHLIB - reserved");
				break;
			case PT_PHDR:
				printfdbg("PT_PHDR - location of program header table");
				break;
			case PT_LOPROC:
				printfdbg("PT_LOPROC - reserved");
				break;
			case PT_HIPROC:
				printfdbg("PT_HIPROC - reserved");
				break;
			case PT_GNU_STACK:
				printfdbg("PT_GNU_STACK - used by kernel");
				break;
			default: 
				printfdbg("Unknown (%ld)", phdr->p_type);
				break;
		}
		printfdbg("\n");
		if (phdr->p_type == PT_LOAD) { // or any other type but we only care about PT_LOAD
			printfdbg("\t\tp_offset := %x\n", phdr->p_offset);
			printfdbg("\t\tp_vaddr := %x\n", phdr->p_vaddr);
			printfdbg("\t\tp_filesz := %x\n", phdr->p_filesz);
			printfdbg("\t\tp_memsz := %x\n", phdr->p_memsz);
			printfdbg("\t\tp_align := %x\n", phdr->p_align);
		}
	}
	return;
}

// Prints more information about an entry in the '/proc/<pid>/maps' file
static int print_maps_entry(struct maps_entry *ent, char *linebuf, void *arg) {
	struct file_metadata* meta = __runt_files_metadata_by_addr(ent->first);
	if (meta == NULL) {
		printfdbg("Error: could not retrieve metadata for file %s\n",ent->rest);
		return 1;
	}
	ElfW(Ehdr)* ehdr = meta->ehdr;

	printfdbg("%lx to %lx\n", ent->first, ent->second);
	print_if_interesting_addr(ent->first, ent->second);
	printfdbg("Privileges: %c%c%c%c\n", ent->r, ent->w, ent->x, ent->p);
	printfdbg("Inode: %d\n", ent->inode);
	printfdbg("Filename: %s\n", meta->filename);		

	printfdbg("Magic: %s\n", ehdr->e_ident);
	printfdbg("Arch: ");
	switch(ehdr->e_machine) {
		case EM_X86_64: 
			printfdbg("EM_X86_64"); 
			break;
		case EM_IA_64:
			printfdbg("EM_IA_64");
			break;
		case EM_ARM:
			printfdbg("EM_ARM");
			break;
		case EM_NONE:
			printfdbg("EM_NONE");
			break;
		case EM_386:
			printfdbg("EM_386");
			break;
		default: printfdbg("Unknown - %d", ehdr->e_machine);
	}
	printfdbg("\n");
	printfdbg("Section header table file offset: %ld\n", ehdr->e_shoff);
	printfdbg("\tentry size: %d\n", ehdr->e_shentsize);
	printfdbg("\tentry count: %d\n", ehdr->e_shnum);
	printfdbg("Section header string table index: %d\n", ehdr->e_shstrndx);
	printfdbg("Segment header table file offset: %ld\n", ehdr->e_phoff);
	printfdbg("\tentry size: %d\n", ehdr->e_phentsize);
	printfdbg("\tentry count: %d\n", ehdr->e_phnum);

	print_section_headers(meta, meta->shdrs, ehdr->e_shnum);
	print_program_headers(meta, meta->phdrs, ehdr->e_phnum);

	printfdbg("\n");
	return 0;
}
