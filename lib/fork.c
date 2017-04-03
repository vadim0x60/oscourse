// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

static void
copy_page(void *srcva, envid_t dstenv, void *dstva, int perm) 
{
	if(sys_page_alloc(0, (void*)PFTEMP, perm)) {
		panic("Failed to allocate a page for a copy!");
	}

	memcpy((void*)PFTEMP, srcva, PGSIZE);

	if (sys_page_map(0, (void*)PFTEMP, dstenv, dstva, perm)) {
		panic("Failed to map a copied page!");
	}
}

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;addr=addr;
	uint32_t err = utf->utf_err;err=err;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 9: My code here:
	if (!(err & FEC_WR)) panic("This page fault isn't caused by a write!");

	// A kiosk version of pgdir_walk cafe:
	pte_t pte = uvpt[utf->utf_fault_va / PGSIZE];
	if (!(pte & PTE_COW)) panic("This page is not copy-on-write!");

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.
	//   No need to explicitly delete the old page's mapping.

	// LAB 9: My code here:
	addr = ROUNDDOWN(addr, PGSIZE);
	copy_page(addr, 0, addr, PTE_W | PTE_U | PTE_P);
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	// LAB 9: My code here:
	int error;
	pte_t pte = uvpt[pn];

	if (!(pte & PTE_P)) return -1;

	int perm = PTE_U | PTE_P;
	if (pte & (PTE_W | PTE_COW)) {
		perm |= PTE_COW;
	}

	error = sys_page_map(0, (void*)(pn * PGSIZE), 0, (void*)(pn * PGSIZE), perm);
	if (error) return error;

	return sys_page_map(0, (void*)(pn * PGSIZE), envid, (void*)(pn * PGSIZE), perm);
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 9: My code here:
	int ret;
	int i;
	int tab_i;

	set_pgfault_handler(pgfault);
	ret = sys_exofork();

	if (ret > 0) {
		// We're the parent

		for (tab_i = 0; tab_i < NPDENTRIES; tab_i++) {
			if (!(uvpd[tab_i] & PTE_P)) continue;

			for (i = tab_i * NPTENTRIES; i < (tab_i + 1) * NPTENTRIES; i++) {
				if ((i + 1) * PGSIZE == UXSTACKTOP) continue;

				duppage(ret, i);			
			}	

			if (i * PGSIZE >= ULIM) break;		
		}

		copy_page((void*)(UXSTACKTOP - PGSIZE), ret, (void*)(UXSTACKTOP - PGSIZE), PTE_U | PTE_P | PTE_W);
		sys_env_set_pgfault_upcall(ret, thisenv->env_pgfault_upcall);

		// Start the child
		if (sys_env_set_status(ret, ENV_RUNNABLE))
			panic("fork() failed to start a new environment!");
	}

	if (ret == 0) {
		// We're the child
		thisenv = &envs[ENVX(sys_getenvid())];
	}

	if (ret < 0) {
		panic("exofork() failed!");
	}
	
	return ret;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
