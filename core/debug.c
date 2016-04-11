#include <assert.h>
#include <execinfo.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>
#include <stdint.h>

#include <rte_config.h>
#include <rte_version.h>

#include "tc.h"
#include "task.h"
#include "module.h"
#include "snbuf.h"

#define STACK_DEPTH 64

void oom_crash(void)
{
	log_crit("Fatal: out of memory for critical operations\n");
	*((int *)NULL) = 0;
}

static const char *si_code_to_str(int sig_num, int si_code)
{
	/* See the manpage of sigaction() */
	switch (si_code) {
	case SI_USER:
		return "SI_USER: kill";
	case SI_KERNEL:
		return "SI_KERNEL: sent by the kernel";
	case SI_QUEUE:
		return "SI_QUEUE: sigqueue";
	case SI_TIMER:
		return "SI_TIMER: POSIX timer expired";
	case SI_MESGQ:
		return "SI_MESGQ: POSIX message queue state changed";
	case SI_ASYNCIO:
		return "SI_ASYNCIO: AIO completed";
	case SI_SIGIO:
		return "SI_SIGIO: Queued SIGIO";
	case SI_TKILL:
		return "SI_TKILL: tkill or tgkill";
	}

	switch (sig_num) {
	case SIGILL:
		switch (si_code) {
		case ILL_ILLOPC:
			return "ILL_ILLOPC: illegal opcode";
		case ILL_ILLOPN:
			return "ILL_ILLOPN: illegal operand";
		case ILL_ILLADR:
			return "ILL_ILLADR: illegal addressing mode";
		case ILL_ILLTRP:
			return "ILL_ILLTRP: illegal trap";
		case ILL_PRVOPC:
			return "ILL_PRVOPC: privileged opcode";
		case ILL_PRVREG:
			return "ILL_PRVREG: privileged register";
		case ILL_COPROC:
			return "ILL_COPROC: coprocessor error";
		case ILL_BADSTK:
			return "ILL_PRVREG: internal stack error";
		default:
			return "unknown";
		}

	case SIGFPE:
		switch (si_code) {
		case FPE_INTDIV:
			return "FPE_INTDIV: integer divide by zero";
		case FPE_INTOVF:
			return "FPE_INTOVF: integer overflow";
		case FPE_FLTDIV:
			return "FPE_FLTDIV: floating-point divide by zero";
		case FPE_FLTOVF:
			return "FPE_FLTOVF: floating-point overflow";
		case FPE_FLTUND:
			return "FPE_FLTOVF: floating-point underflow";
		case FPE_FLTRES:
			return "FPE_FLTOVF: floating-point inexact result";
		case FPE_FLTINV:
			return "FPE_FLTOVF: floating-point invalid operation";
		case FPE_FLTSUB:
			return "FPE_FLTOVF: subscript out of range";
		default:
			return "unknown";
		}

	case SIGSEGV:
		switch (si_code) {
		case SEGV_MAPERR:
			return "SEGV_MAPERR: address not mapped to object";
		case SEGV_ACCERR:
			return "SEGV_ACCERR: invalid permissions for mapped object";
		default:
			return "unknown";
		}

	case SIGBUS:
		switch (si_code) {
		case BUS_ADRALN:
			return "BUS_ADRALN: invalid address alignment";
		case BUS_ADRERR:
			return "BUS_ADRERR: nonexistent physical address";
		case BUS_OBJERR:
			return "BUS_OBJERR: object-specific hardware error";
#if 0
		case BUS_MCEERR_AR:
			return "BUS_MCEERR_AR: Hardware memory error consumed on a machine check";
		case BUS_MCEERR_AO:
			return "BUS_MCEERR_AO: Hardware memory error detected in process but not consumed";
#endif
		default:
			return "unknown";
		}
	};

	return "si_code unavailable for unknown signal";
}

/* addr2line must be available.
 * prints out the code lines [lineno - context, lineno + context] */
static void print_code(char *symbol, int context)
{
	char executable[1024];
	char addr[1024];

	char cmd[1024];

	FILE *proc;

	/* Symbol examples:
	 * ./bessd(run_worker+0x8e) [0x419d0e]" */
	/* ./bessd() [0x4149d8] */
	sscanf(symbol, "%[^(](%*s [%[^]]]", executable, addr);

	sprintf(cmd, "addr2line -i -f -p -e %s %s 2> /dev/null", executable, addr);

	proc = popen(cmd, "r");
	if (!proc)
		return;

	for (;;) {
		char line[1024];

		char *ret;

		char *filename;
		int lineno;
		int curr;

		FILE *fp;

		ret = fgets(line, sizeof(line), proc);

		if (!ret)
			break;

		if (line[strlen(line) - 1] != '\n') /* line is too long */
			continue;

		/* addr2line examples:
		 * sched_free at /home/sangjin/.../tc.c:277 (discriminator 2)
		 * run_worker at /home/sangjin/bess/core/module.c:653 */

		line[strlen(line) - 1] = '\0';

		log_crit("    %s\n", line);

		if (line[strlen(line) - 1] == ')')
			*(strstr(line, " (discriminator")) = '\0';

		ret = strrchr(line, ' ');
		if (!ret)
			continue;

		ret++;		/* now it points to the last token */

		filename = strtok(ret, ":");

		if (strcmp(filename, "??") == 0)
			continue;

		ret = strtok(NULL, "");
		if (!ret)
			continue;

		lineno = atoi(ret);

		fp = fopen(filename, "r");
		if (!fp) {
			log_crit("        (file/line not available)\n");
			continue;
		}

		for (curr = 1; !feof(fp); curr++) {
			int discard;

			if (abs(curr - lineno) <= context) {
				log_crit("      %s %d: ",
						(curr == lineno) ? "->" : "  ",
						curr);
				discard = 0;
			} else {
				if (curr > lineno + context)
					break;
				discard = 1;
			}

			for (;;) {
				ret = fgets(line, sizeof(line), fp);
				if (!ret)
					break;

				if (!discard)
					log_crit("%s", line);

				if (line[strlen(line) - 1] != '\n') {
					if (feof(fp))
						log_crit("\n");
				} else
					break;
			}
		}

		fclose(fp);
	}

	pclose(proc);
}

static void trap_handler(int sig_num, siginfo_t *info, void *ucontext)
{
	void *addrs[STACK_DEPTH];
	void *ip;
	char **symbols;

	int cnt;
	int i;

	struct ucontext *uc;

	int skips;

	uc = (struct ucontext *)ucontext;

#if defined(RTE_ARCH_I686)
	ip = (void *) uc->uc_mcontext.gregs[REG_EIP];
#elif defined(RTE_ARCH_X86_64)
	ip = (void *) uc->uc_mcontext.gregs[REG_RIP];
#else
	#error neither x86 or x86-64
#endif

	log_crit("A critical error has occured. Aborting...\n");
	log_crit("Signal: %d (%s), si_code: %d (%s), address: %p, IP: %p\n",
			sig_num, strsignal(sig_num),
			info->si_code, si_code_to_str(sig_num, info->si_code),
			info->si_addr, ip);

	/* the linker requires -rdynamic for non-exported symbols */
	cnt = backtrace(addrs, STACK_DEPTH);
	if (cnt < 3) {
		log_crit("ERROR: backtrace() failed\n");
		exit(EXIT_FAILURE);
	}

	/* addrs[0]: this function
	 * addrs[1]: sigaction in glibc
	 * addrs[2]: the trigerring instruction pointer *or* its caller
	 *           (depending on the kernel behavior?) */
	if (addrs[2] == ip) {
		skips = 2;
	} else {
		addrs[1] = ip;
		skips = 1;
	}

	/* The return addresses point to the next instruction
	 * after its call, so adjust. */
	for (i = skips + 1; i < cnt; i++)
		addrs[i] = (void *)((uint64_t)addrs[i] - 1);

	symbols = backtrace_symbols(addrs, cnt);

	if (symbols) {
		log_crit("Backtrace (recent calls first) ---\n");

		for (i = skips; i < cnt; ++i) {
			log_crit("(%d): %s\n", i - skips, symbols[i]);
			print_code(symbols[i], (i == skips) ? 3 : 0);
		}

		free(symbols);	/* required by backtrace_symbols() */
	} else
		log_crit("ERROR: backtrace_symbols() failed\n");

	exit(EXIT_FAILURE);
}

__attribute__((constructor(101))) static void set_trap_handler()
{
	const int signals[] = {
			SIGSEGV,
			SIGBUS,
			SIGILL,
			SIGFPE,
			SIGABRT,
			SIGUSR1, /* for users to trigger */
		};

	const int ignored_signals[] = {
		SIGPIPE,
	};
	struct sigaction sigact;
	int i;

	sigact.sa_sigaction = trap_handler;
	sigact.sa_flags = SA_RESTART | SA_SIGINFO;

	for (i = 0; i < sizeof(signals) / sizeof(int); i++) {
		int ret = sigaction(signals[i], &sigact, NULL);
		assert(ret != 1);
	}

	for (i = 0; i < sizeof(ignored_signals) / sizeof(int); i++) {
		signal(ignored_signals[i], SIG_IGN);
	}
}

void dump_types(void)
{
	printf("DPDK %d.%d.%d.%d\n",
			RTE_VER_MAJOR, RTE_VER_MINOR,
			RTE_VER_PATCH_LEVEL, RTE_VER_PATCH_RELEASE);

	printf("sizeof(char)=%zu\n", sizeof(char));
	printf("sizeof(short)=%zu\n", sizeof(short));
	printf("sizeof(int)=%zu\n", sizeof(int));
	printf("sizeof(long)=%zu\n", sizeof(long));
	printf("sizeof(long long)=%zu\n", sizeof(long long));
	printf("sizeof(void *)=%zu\n", sizeof(void *));

	printf("sizeof(heap)=%lu\n", sizeof(struct heap));
	printf("sizeof(clist_head)=%lu sizeof(cdlist_item)=%lu\n",
			sizeof(struct cdlist_head),
			sizeof(struct cdlist_item));

	printf("sizeof(rte_mbuf)=%lu\n", sizeof(struct rte_mbuf));
	printf("sizeof(snbuf)=%lu\n", sizeof(struct snbuf));

	printf("sizeof(pkt_batch)=%lu\n",
			sizeof(struct pkt_batch));
	printf("sizeof(sn_rx_metadata)=%lu sizeof(sn_tx_metadata)=%lu\n",
			sizeof(struct sn_rx_metadata),
			sizeof(struct sn_tx_metadata));
	printf("sizeof(sn_rx_desc)=%lu sizeof(sn_tx_desc)=%lu\n",
			sizeof(struct sn_rx_desc), sizeof(struct sn_tx_desc));
	printf("sizeof(sched)=%lu sizeof(sched_stats)=%lu\n",
			sizeof(struct sched),
			sizeof(struct sched_stats));
	printf("sizeof(tc)=%lu sizeof(tc_stats)=%lu\n",
			sizeof(struct tc),
			sizeof(struct tc_stats));
	printf("sizeof(task)=%lu\n", sizeof(struct task));

	printf("sizeof(module)=%lu\n", sizeof(struct module));
	printf("sizeof(gate)=%lu\n", sizeof(struct gate));

	printf("sizeof(worker_context)=%lu\n", sizeof(struct worker_context));

	printf("sizeof(snobj)=%lu\n", sizeof(struct snobj));
}
