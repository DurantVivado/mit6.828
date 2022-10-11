// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/error.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/pmap.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *sname; // short name
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "h", "Display this list of commands", mon_help },
	{ "kerninfo", "ki", "Display information about the kernel", mon_kerninfo },
	{ "backtrace", "bt", "show the kernel stack backtrace", mon_backtrace },
	{ "showmappings", "sm", "show mapping of the physical address range", mon_showmappings },
};

/***** Implementations of basic kernel monitor commands *****/
void get_perm(char* perm, pte_t *pte) {
	if (!pte) {
		return;
	}
	strcat(perm, (*pte & PTE_AVAIL) ? "(AVL)":"-");
	strcat(perm, (*pte & PTE_G) ? "(G)":"-");
	strcat(perm, (*pte & PTE_PS) ? "(PS)":"-");
	strcat(perm, (*pte & PTE_D) ? "(D)":"-");
	strcat(perm, (*pte & PTE_A) ? "(A)":"-");
	strcat(perm, (*pte & PTE_PCD) ? "(PCD)":"-");
	strcat(perm, (*pte & PTE_PWT) ? "(PWT)":"-");
	strcat(perm, (*pte & PTE_U) ? "(U)":"-");
	strcat(perm, (*pte & PTE_W) ? "(W)":"-");
	strcat(perm, (*pte & PTE_P) ? "(P)":"-");
}

int
mon_showmappings(int argc, char **argv, struct Trapframe *tf)
{
	uint32_t addr, start_addr, end_addr;
	pte_t* pte;
	char* endptr; // the first invalid char
	char perm[] = "";
	start_addr = strtol(argv[1], &endptr, 16);
	// cprintf("end_ptr:%x, *end_ptr:%c\n", endptr, *endptr);
	if (*endptr) {
		cprintf("invalid start address.\n");
		return -E_INVAL;
	}
	// memory alignment according to PGSIZE
	start_addr = ROUNDDOWN(start_addr, PGSIZE);
	// look up if the page exists
	if (argc == 2) {
		// if there is one argument, we directly output the 
		// corresponding physical address
		
		pte = pgdir_walk(kern_pgdir, (void*) start_addr, 0);
		if (!pte || !(*pte & PTE_P)) {
			cprintf("VA [0x%08x] not mapped.\n", start_addr);
			return 0;
		}
		cprintf("VA [0x%08x] mapped at PA [0x%08x], permission: ",
				start_addr, PTE_ADDR(*pte));
		//PWU(PWT)(PCD)AD(PS)G
		get_perm(perm, pte);
		cprintf("%s\n", perm);
		return 0;
	}
	if (argc != 3) {
		cprintf("require one or two arguments,\n");
		cprintf("i.e., start [and end] address.\n");
		return -E_INVAL;
	}
	end_addr = strtol(argv[2], &endptr, 16);
	if (*endptr) {
		cprintf("invalid start address.\n");
		return -E_INVAL;
	}
	// 2. check corner cases
	// start_addr and end_addr must be valid hex values
	// start_addr <= end_addr
	if (start_addr > end_addr) {
		cprintf("start address higher than end address\n");
		return -E_INVAL;
	}
	cprintf("%x round up to ", end_addr);
	end_addr = ROUNDUP(end_addr, PGSIZE);
	cprintf("%x\n", end_addr);
	cprintf("start_addr:%x, end_addr:%x\n", start_addr, end_addr);
	for(addr = start_addr; addr <= end_addr; addr += PGSIZE) {
		pte = pgdir_walk(kern_pgdir, (void*) addr, 0);
		if (!pte || !(*pte & PTE_P)) {
			cprintf("VA [0x%08x] not mapped.\n", addr);
			continue;
		}
		cprintf("VA [0x%08x] mapped at PA [0x%08x], permission: ",
				addr, PTE_ADDR(*pte));
		memset(perm, 0, sizeof(perm));
		get_perm(perm, pte);
		cprintf("%s\n", perm);
	}
	return 0;
}

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(commands); i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	uint32_t  ebp, eip, args[5];
    cprintf("Stack backtrace:\n");
    ebp = read_ebp();
	struct Eipdebuginfo info;
	while (ebp) {
		eip = *((uint32_t *)ebp + 1);
		args[0] = *((uint32_t *)ebp + 2);
		args[1] = *((uint32_t *)ebp + 3);
		args[2] = *((uint32_t *)ebp + 4);
		args[3] = *((uint32_t *)ebp + 5);
		args[4] = *((uint32_t *)ebp + 6);
		debuginfo_eip(eip, &info);
		cprintf("  ebp %08x  eip %08x  args %08x %08x %08x %08x %08x\n", ebp,
				eip, args[0], args[1], args[2], args[3], args[4]);
				cprintf("%s:%d: %.*s+%d\n", info.eip_file, info.eip_line, 
								info.eip_fn_namelen, info.eip_fn_name, eip - info.eip_fn_addr);
		ebp = *((uint32_t *)ebp);
    }

	// Your code here.
	return 0;
}



/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[0], commands[i].name) == 0 ||
		 	strcmp(argv[0], commands[i].sname) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");


	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
