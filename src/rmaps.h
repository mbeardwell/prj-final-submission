#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <link.h>
#include "maps.h"	
#include "dso-meta.h" 
#include "librunt.h"
#include "relf.h"

int MAPS_MAX_NUM_LINEBUFS = 100;
int MAPS_BUF_SIZE = 1000;
FILE* maps_file;

/*
* In full transparency, this method resembles that of one of my supervisor's Dr. Stephen Kell.
* His project "libsystrap" uses his other project "librunt" which is also a dependency for this project
* Due to the rigid intended use of the librunt functions "get_a_line_from_maps_fd"
* and "process_one_maps_entry", it was difficult to find a less similar way to implement this.
* Here is a link to that method "trap_all_mappings":
* https://github.com/stephenrkell/libsystrap/blob/790cf958157520ce44afab0bcc2b0fcda9d168fe/example/trace-syscalls.c#L76
*/
static void process_all_lines(int fd, void* callback) {
	typedef char* linebuf_t;
	linebuf_t linebufs[MAPS_MAX_NUM_LINEBUFS];
		
	/* Allocate line buffers and
	*  Read lines from "/proc/<self>/maps" into the line buffers
	*/
	int line_counter = 0;
	int newline_pos;
	while (1) {
		assert(line_counter < MAPS_MAX_NUM_LINEBUFS);
		
		linebufs[line_counter] = malloc(MAPS_BUF_SIZE);
		newline_pos = get_a_line_from_maps_fd(linebufs[line_counter], MAPS_BUF_SIZE, fd);
		if (newline_pos == -1) break;
		linebufs[line_counter][newline_pos] = '\0';

		line_counter++;
	}

	int num_lines_read = line_counter;  // to be explicit. 

	/* Process line buffers (including instrumenting relevant regions) */
	struct maps_entry mline;
	int num_entries_skipped = 0;
	int was_skipped;
	for (int i = 0; i < num_lines_read; i++) {	
		if (0 == strncmp(linebufs[i], "00 ", 3)) { 	// if the region is not memory mapped
			was_skipped = 1;
			printfdbg("Maps entry began with \"00 \" meaning it was unmapped. Skipping.\n");
		} else {
			was_skipped = process_one_maps_entry(linebufs[i], &mline, callback, NULL);
		}
		if (was_skipped) num_entries_skipped++;
	}
	
	/* Clean up */
	for (int i = 0; i < num_lines_read; i++) {
		free(linebufs[i]);
	}
}

static int open_maps() {
	maps_file = fopen("/proc/self/maps", "r");
    int fd;
    if ( maps_file == 0 || (fd = fileno(maps_file)) <= 2 ) {
        printfdbg("Unable to open /proc/self/maps: Invalid file descriptor.\n");
        exit(1);
    }
    printfdbg("/proc/self/maps file descriptor: %d\n", fd);
    return fd;
}

static void close_maps() {
	fclose(maps_file);
}
