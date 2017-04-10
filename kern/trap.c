#include <inc/mmu.h>
#include <inc/x86.h>
#include <inc/assert.h>
#include <inc/string.h>

#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/env.h>
#include <kern/syscall.h>
#include <kern/sched.h>
#include <kern/kclock.h>
#include <kern/picirq.h>
#include <kern/cpu.h>

#ifndef debug
# define debug 0
#endif

static struct Taskstate ts;

/* For debugging, so print_trapframe can distinguish between printing
 * a saved trapframe and printing the current trapframe and print some
 * additional information in the latter case.
 */
static struct Trapframe *last_tf;

/* Interrupt descriptor table.  (Must be built at run time because
 * shifted function addresses can't be represented in relocation records.)
 */
struct Gatedesc idt[256] = { { 0 } };
struct Pseudodesc idt_pd = {
	sizeof(idt) - 1, (uint32_t) idt
};


static const char *trapname(int trapno)
{
	static const char * const excnames[] = {
		"Divide error",
		"Debug",
		"Non-Maskable Interrupt",
		"Breakpoint",
		"Overflow",
		"BOUND Range Exceeded",
		"Invalid Opcode",
		"Device Not Available",
		"Double Fault",
		"Coprocessor Segment Overrun",
		"Invalid TSS",
		"Segment Not Present",
		"Stack Fault",
		"General Protection",
		"Page Fault",
		"(unknown trap)",
		"x87 FPU Floating-Point Error",
		"Alignment Check",
		"Machine-Check",
		"SIMD Floating-Point Exception"
	};

	if (trapno < sizeof(excnames)/sizeof(excnames[0]))
		return excnames[trapno];
	if (trapno == T_SYSCALL)
		return "System call";
	if (trapno >= IRQ_OFFSET && trapno < IRQ_OFFSET + 16)
		return "Hardware Interrupt";
	return "(unknown trap)";
}

void
trap_init(void)
{
//	extern struct Segdesc gdt[];
	extern void (*divide_thdlr)(void);
	extern void (*debug_thdlr)(void);
	extern void (*nmi_thdlr)(void);
	extern void (*brkpt_thdlr)(void);
	extern void (*overflow_thdlr)(void);
	extern void (*bound_thdlr)(void);
	extern void (*illop_thdlr)(void);
	extern void (*device_thdlr)(void);
	extern void (*dblflt_thdlr)(void);
	extern void (*tss_thdlr)(void);
	extern void (*sefnp_thdlr)(void);
	extern void (*stack_thdlr)(void);
	extern void (*gpflt_thdlr)(void);
	extern void (*pgflt_thdlr)(void);
	extern void (*fperr_thdlr)(void);
	extern void (*align_thdlr)(void);
	extern void (*mchk_thdlr)(void);
	extern void (*simderr_thdlr)(void);

	extern void (*syscall_thdlr)(void);
	extern void (*default_thdlr)(void);	

	extern void (*timer_thdlr)(void);
	extern void (*kbd_thdlr)(void);
	extern void (*serial_thdlr)(void);
	extern void (*spurious_thdlr)(void);
	extern void (*ide_thdlr)(void);
	extern void (*error_thdlr)(void);
	extern void (*clock_thdlr)(void);

	SETGATE(idt[T_DIVIDE], 0, GD_KT, (int)(&divide_thdlr), 0);
	SETGATE(idt[T_DEBUG], 0, GD_KT, (int)(&debug_thdlr), 0);
	SETGATE(idt[T_NMI], 0, GD_KT, (int)(&nmi_thdlr), 0);
	SETGATE(idt[T_BRKPT], 0, GD_KT, (int)(&brkpt_thdlr), 3);
	SETGATE(idt[T_OFLOW], 0, GD_KT, (int)(&overflow_thdlr), 0);
	SETGATE(idt[T_BOUND], 0, GD_KT, (int)(&bound_thdlr), 0);
	SETGATE(idt[T_ILLOP], 0, GD_KT, (int)(&illop_thdlr), 0);
	SETGATE(idt[T_DEVICE], 0, GD_KT, (int)(&device_thdlr), 0);
	SETGATE(idt[T_DBLFLT], 0, GD_KT, (int)(&dblflt_thdlr), 0);
	SETGATE(idt[T_TSS], 0, GD_KT, (int)(&tss_thdlr), 0);
	SETGATE(idt[T_SEGNP], 0, GD_KT, (int)(&sefnp_thdlr), 0);
	SETGATE(idt[T_STACK], 0, GD_KT, (int)(&stack_thdlr), 0);
	SETGATE(idt[T_GPFLT], 0, GD_KT, (int)(&gpflt_thdlr), 0);
	SETGATE(idt[T_PGFLT], 0, GD_KT, (int)(&pgflt_thdlr), 0);
	SETGATE(idt[T_FPERR], 0, GD_KT, (int)(&fperr_thdlr), 0);
	SETGATE(idt[T_ALIGN], 0, GD_KT, (int)(&align_thdlr), 0);
	SETGATE(idt[T_MCHK], 0, GD_KT, (int)(&mchk_thdlr), 0);
	SETGATE(idt[T_SIMDERR], 0, GD_KT, (int)(&simderr_thdlr), 0);

	SETGATE(idt[T_SYSCALL], 0, GD_KT, (int)(&syscall_thdlr), 3);
	SETGATE(idt[T_DEFAULT], 0, GD_KT, (int)(&default_thdlr), 0);	

	SETGATE(idt[IRQ_OFFSET + IRQ_TIMER], 0, GD_KT, (int)(&timer_thdlr), 0);
	SETGATE(idt[IRQ_OFFSET + IRQ_KBD], 0, GD_KT, (int)(&kbd_thdlr),0);
	SETGATE(idt[IRQ_OFFSET + IRQ_SERIAL], 0, GD_KT, (int)(&serial_thdlr), 0);
	SETGATE(idt[IRQ_OFFSET + IRQ_SPURIOUS], 0, GD_KT, (int)(&spurious_thdlr), 0);
	SETGATE(idt[IRQ_OFFSET + IRQ_IDE], 0, GD_KT, (int)(&ide_thdlr), 0);
	SETGATE(idt[IRQ_OFFSET + IRQ_ERROR], 0, GD_KT, (int)(&error_thdlr), 0);
	SETGATE(idt[IRQ_OFFSET + IRQ_CLOCK], 0, GD_KT, (int)(&clock_thdlr), 0);

	// Per-CPU setup 
	trap_init_percpu();
}

// Initialize and load the per-CPU TSS and IDT
void
trap_init_percpu(void)
{
	// Setup a TSS so that we get the right stack
	// when we trap to the kernel.
	ts.ts_esp0 = KSTACKTOP;
	ts.ts_ss0 = GD_KD;

	// Initialize the TSS slot of the gdt.
	gdt[GD_TSS0 >> 3] = SEG16(STS_T32A, (uint32_t) (&ts),
					sizeof(struct Taskstate), 0);
	gdt[GD_TSS0 >> 3].sd_s = 0;

	// Load the TSS selector (like other segment selectors, the
	// bottom three bits are special; we leave them 0)
	ltr(GD_TSS0);

	// Load the IDT
	lidt(&idt_pd);
}


void
clock_idt_init(void)
{
	extern void (*clock_thdlr)(void);
	// init idt structure
	SETGATE(idt[IRQ_OFFSET + IRQ_CLOCK], 0, GD_KT, (int)(&clock_thdlr), 0);
	lidt(&idt_pd);
}


void
print_trapframe(struct Trapframe *tf)
{
	cprintf("TRAP frame at %p\n", tf);
	print_regs(&tf->tf_regs);
	cprintf("  es   0x----%04x\n", tf->tf_es);
	cprintf("  ds   0x----%04x\n", tf->tf_ds);
	cprintf("  trap 0x%08x %s\n", tf->tf_trapno, trapname(tf->tf_trapno));
	// If this trap was a page fault that just happened
	// (so %cr2 is meaningful), print the faulting linear address.
	if (tf == last_tf && tf->tf_trapno == T_PGFLT)
		cprintf("  cr2  0x%08x\n", rcr2());
	cprintf("  err  0x%08x", tf->tf_err);
	// For page faults, print decoded fault error code:
	// U/K=fault occurred in user/kernel mode
	// W/R=a write/read caused the fault
	// PR=a protection violation caused the fault (NP=page not present).
	if (tf->tf_trapno == T_PGFLT)
		cprintf(" [%s, %s, %s]\n",
			tf->tf_err & 4 ? "user" : "kernel",
			tf->tf_err & 2 ? "write" : "read",
			tf->tf_err & 1 ? "protection" : "not-present");
	else
		cprintf("\n");
	cprintf("  eip  0x%08x\n", tf->tf_eip);
	cprintf("  cs   0x----%04x\n", tf->tf_cs);
	cprintf("  flag 0x%08x\n", tf->tf_eflags);
	cprintf("  esp  0x%08x\n", tf->tf_esp);
	cprintf("  ss   0x----%04x\n", tf->tf_ss);
}

void
print_regs(struct PushRegs *regs)
{
	cprintf("  edi  0x%08x\n", regs->reg_edi);
	cprintf("  esi  0x%08x\n", regs->reg_esi);
	cprintf("  ebp  0x%08x\n", regs->reg_ebp);
	cprintf("  oesp 0x%08x\n", regs->reg_oesp);
	cprintf("  ebx  0x%08x\n", regs->reg_ebx);
	cprintf("  edx  0x%08x\n", regs->reg_edx);
	cprintf("  ecx  0x%08x\n", regs->reg_ecx);
	cprintf("  eax  0x%08x\n", regs->reg_eax);
}


static void
trap_dispatch(struct Trapframe *tf)
{
	// Handle processor exceptions.

	// Handle page faults
	if (tf->tf_trapno == T_PGFLT) {
		page_fault_handler(tf);
		sched_yield();
		return;
	}

	// Handle system calls
	if (tf->tf_trapno == T_SYSCALL) {
		tf->tf_regs.reg_eax = syscall(  tf->tf_regs.reg_eax, 
										tf->tf_regs.reg_edx, 
										tf->tf_regs.reg_ecx, 
										tf->tf_regs.reg_ebx,
										tf->tf_regs.reg_edi,
										tf->tf_regs.reg_esi  );
		sched_yield();
		return;
	}

	// Handle system calls
	if (tf->tf_trapno == T_BRKPT) {
		monitor(tf);
		return;
	}

	// Handle spurious interrupts
	// The hardware sometimes raises these because of noise on the
	// IRQ line or other reasons. We don't care.
	//
	if (tf->tf_trapno == IRQ_OFFSET + IRQ_SPURIOUS) {
		cprintf("Spurious interrupt on irq 7\n");
		print_trapframe(tf);
		return;
	}

	// Handle RTC interrupts
	// For now, we just throw away the information
	if (tf->tf_trapno == IRQ_OFFSET + IRQ_CLOCK) {
		rtc_check_status();
		pic_send_eoi(IRQ_CLOCK);
		sched_yield();
		return;
	}

	// Handle keyboard and serial interrupts.
	// LAB 11: Your code here.

	print_trapframe(tf);
	if (tf->tf_cs == GD_KT) {
		panic("unhandled trap in kernel");
	} else {
		env_destroy(curenv);
	}
}

void
trap(struct Trapframe *tf)
{
	// The environment may have set DF and some versions
	// of GCC rely on DF being clear
	asm volatile("cld" ::: "cc");

	// Halt the CPU if some other CPU has called panic()
	extern char *panicstr;
	if (panicstr)
		asm volatile("hlt");

	// Check that interrupts are disabled.  If this assertion
	// fails, DO NOT be tempted to fix it by inserting a "cli" in
	// the interrupt path.
	assert(!(read_eflags() & FL_IF));

	assert(curenv);

	// Garbage collect if current enviroment is a zombie
	if (curenv->env_status == ENV_DYING) {
		env_free(curenv);
		curenv = NULL;
		sched_yield();
	}

	// Copy trap frame (which is currently on the stack)
	// into 'curenv->env_tf', so that running the environment
	// will restart at the trap point.
	curenv->env_tf = *tf;
	// The trapframe on the stack should be ignored from here on.
	tf = &curenv->env_tf;

	// Record that tf is the last real trapframe so
	// print_trapframe can print some additional information.
	last_tf = tf;

	// Dispatch based on what type of trap occurred
	trap_dispatch(tf);

	// If we made it to this point, then no other environment was
	// scheduled, so we should return to the current environment
	// if doing so makes sense.
	if (curenv && curenv->env_status == ENV_RUNNING)
		env_run(curenv);
	else
		sched_yield();
}


void
page_fault_handler(struct Trapframe *tf)
{
	uint32_t fault_va;

	// Read processor's CR2 register to find the faulting address
	fault_va = rcr2();

	// Handle kernel-mode page faults.

	// LAB 8: Your code here.
	if (!(tf->tf_cs & 3)) {
		panic("Kernel-mode page fault!"); // Handled.
	}

	// We've already handled kernel-mode exceptions, so if we get here,
	// the page fault happened in user mode.

	// Call the environment's page fault upcall, if one exists.  Set up a
	// page fault stack frame on the user exception stack (below
	// UXSTACKTOP), then branch to curenv->env_pgfault_upcall.
	//
	// The page fault upcall might cause another page fault, in which case
	// we branch to the page fault upcall recursively, pushing another
	// page fault stack frame on top of the user exception stack.
	//
	// The trap handler needs one word of scratch space at the top of the
	// trap-time stack in order to return.  In the non-recursive case, we
	// don't have to worry about this because the top of the regular user
	// stack is free.  In the recursive case, this means we have to leave
	// an extra word between the current top of the exception stack and
	// the new stack frame because the exception stack _is_ the trap-time
	// stack.
	//
	// If there's no page fault upcall, the environment didn't allocate a
	// page for its exception stack or can't write to it, or the exception
	// stack overflows, then destroy the environment that caused the fault.
	// Note that the grade script assumes you will first check for the page
	// fault upcall and print the "user fault va" message below if there is
	// none.  The remaining three checks can be combined into a single test.
	//
	// Hints:
	//   user_mem_assert() and env_run() are useful here.
	//   To change what the user environment runs, modify 'curenv->env_tf'
	//   (the 'tf' variable points at 'curenv->env_tf').

	// LAB 9: My code here:

	if (!curenv->env_pgfault_upcall) {
		cprintf("[%08x] user fault va %08x ip %08x\n",
			curenv->env_id, fault_va, tf->tf_eip);
		print_trapframe(tf);
		env_destroy(curenv);
	}

	// Map the user exception stack for ourselves
	struct PageInfo* ex_page;


	// So I guess that's why we mapped the entire physical memory for the kernel
	ex_page = page_lookup(curenv->env_pgdir, (void*)(UXSTACKTOP - PGSIZE), NULL);
	uintptr_t MAPUXSTACKTOP = KERNBASE + page2pa(ex_page) + PGSIZE;
	
	// If we did this:
	// curenv->env_pgfault_upcall(utrapframe);
	// the upcall would operate in kernel mode. 
	// So we do this:

	int sp_offset;
	if (tf->tf_esp < UXSTACKTOP && tf->tf_esp > (UXSTACKTOP - PGSIZE))
		sp_offset = tf->tf_esp - UXSTACKTOP; // We're already in the exception stack
	else 
		sp_offset = 0; // This is the first frame in the exception stack

	sp_offset -= 4;
	sp_offset -= sizeof(struct UTrapframe);

	user_mem_assert(curenv, (void*)(UXSTACKTOP + sp_offset), -sp_offset, PTE_U | PTE_W);

	if (sp_offset < -PGSIZE) {
		// The error stack is over
		// So is this environment's life
		env_destroy(curenv); 
	}

	struct UTrapframe *utrap = (struct UTrapframe*)(MAPUXSTACKTOP + sp_offset);
	utrap->utf_fault_va = fault_va;
	utrap->utf_err = tf->tf_err;

	utrap->utf_eip = tf->tf_eip;
	utrap->utf_esp = tf->tf_esp;
	utrap->utf_eflags = tf->tf_eflags;
	memcpy(&utrap->utf_regs, &tf->tf_regs, sizeof(struct PushRegs));

	tf->tf_eip = (uintptr_t)curenv->env_pgfault_upcall;
	tf->tf_esp = UXSTACKTOP + sp_offset;
	env_run(curenv);

	// TODO: Why don't we use %ebp in the exception stack?
}

