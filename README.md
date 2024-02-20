This is the final submission of my third year project at King's College London.

Title: "Faster dynamically instrumented programs: A look at floating-point emulation in ARM Linux".
Abstract: "This project borrows existing dynamic program instrumentation techniques to propose a faster method of emulating floating-point instructions on Unix-like operating systems than what is provided by the kernel. The proposed method replaces floating-point instructions with branches that indirectly lead to emulation code resident in the same process’ memory. This prevents some execution flow switching into kernel code to run the kernel’s floating-point instruction emulator which theoretically reduces overhead for every instruction emulated."

