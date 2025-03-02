#include <math.h>

/* 
* 64 single precision registers 	= s0 to s63 
* 									= d0 to d31 
* This is twice that needed by my test hardware (VFPv3-D16).
*/
#define NUM_SINGLE_PREC_REGS 64
int32_t fpu_registers[NUM_SINGLE_PREC_REGS]; 

/*
* Converts a single precision register number, 
* such as '5' from the register 'r5', to a pointer into memory
* of where the value for that register is stored.
*/
int32_t* sreg_to_bank_ptr(arm_reg reg) {
	assert(ARM_REG_S0 <= reg && reg <= ARM_REG_S31);
	return (int32_t*)fpu_registers + (reg - ARM_REG_S0);
}

// Sets a emulated floating-point register to the given 32-bit value
void set_sreg(arm_reg reg, int32_t val) {
	*sreg_to_bank_ptr(reg) = val;
}

// Returns a value stored in an emulated floating-point register
int32_t get_sreg(arm_reg reg) {	
	return *sreg_to_bank_ptr(reg);
}

// Initialise emulator by setting the emulated registers to zero.
void emulator_init() {
	memset(fpu_registers, 0, NUM_SINGLE_PREC_REGS * sizeof(int32_t));
}

/*
* Example of an emulation routine called by a trampoline.
* This is the emulation routine for the 'vadd.f32' instruction and will
* be called from a 'vadd.f32' trampoline. No C code calls this method as 
* the machine code in the trampolines are generated at run-time.
*/
void vadd_f32(int32_t Sd, int32_t Sn, int32_t Sm) {
	float a, b;

	/* 
	* Copy register values into local variables
	* which are either on the stack or in scratch registers
	* that are restored by the trampoline after this method returns.
	*/ 
	int32_t Sn_val = get_sreg(Sn);
	int32_t Sm_val = get_sreg(Sm);
	memcpy(&a, &Sn_val, sizeof(int32_t));
	memcpy(&b, &Sm_val, sizeof(int32_t));

	// Perform the addition
	float c = a + b;  
	int32_t result;

	// Store the result back in a register
	memcpy(&result, &c, sizeof(int32_t));
	set_sreg(Sd, result);

	printfdbg("vadd.f32 Sd:%d Sn:%d Sm:%d: %f + %f = %f\n", Sd, Sn, Sm, a, b, c);
	return;
}