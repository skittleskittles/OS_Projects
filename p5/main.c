/**
 * Tony Givargis
 * Copyright (C), 2023
 * University of California, Irvine
 *
 * CS 238P - Operating Systems
 * main.c
 */

#include <signal.h>
#include "system.h"

/**
 * Needs:
 *   signal()
 */

static volatile int done;

static void
_signal_(int signum)
{
	assert( SIGINT == signum );

	done = 1;
}

double
cpu_util(const char *s)
{
	static unsigned sum_, vector_[7];
	unsigned sum, vector[7];
	const char *p;
	double util;
	uint64_t i;

	/*
	  user  Time spent on processes running in user mode
	  nice  Time spent on processes running in user mode with a low priority (nice)
	  system  Time spent on processes running in kernel mode
	  idle  Time spent with the CPU idle, waiting for tasks
	  iowait  Time spent with the CPU idle, waiting for tasks
	  irq  Time spent handling hardware interrupts
	  softirq  Time spent handling software interrupts
	*/

    /* research the above things
	 find proper(stat file) */
	if (!(p = strstr(s, " ")) ||
	    (7 != sscanf(p,
			 "%u %u %u %u %u %u %u",
			 &vector[0],
			 &vector[1],
			 &vector[2],
			 &vector[3],
			 &vector[4],
			 &vector[5],
			 &vector[6]))) {
		return 0;
	}
	sum = 0.0;
	for (i=0; i<ARRAY_SIZE(vector); ++i) {
		sum += vector[i];
	}
	/* vector[3] - vector_[3] idle time */
	util = (1.0 - (vector[3] - vector_[3]) / (double)(sum - sum_)) * 100.0;
	sum_ = sum;
	for (i=0; i<ARRAY_SIZE(vector); ++i) {
		vector_[i] = vector[i];
	}
	return util;
}

void memory_util() {
	const char * const PROC_MEMINFO = "/proc/meminfo";
	char line[1024];
	FILE *file;
	double util;
	unsigned long mem_total, mem_free, buffers, cached;

	if (!(file = fopen(PROC_MEMINFO, "r"))) {
		TRACE("/proc/meminfo fopen()");
		return;
	}

	while (fgets(line, sizeof(line), file)) {
        if (strstr(line, "MemTotal")) {
            sscanf(line, "MemTotal: %lu kB\n", &mem_total);
			/*printf(" MemTotal: %lu", mem_total);*/
        } else if (strstr(line, "MemFree")) {
            sscanf(line, "MemFree: %lu kB\n", &mem_free);
			/*printf(" MemFree: %lu", mem_free);*/
        } else if (strstr(line, "Buffers")) {
			sscanf(line, "Buffers: %lu kB\n", &buffers);
			/*printf(" Buffers: %lu", buffers);*/
		} else if (strstr(line, "Cached")) {
			sscanf(line, "Cached: %lu kB\n", &cached);
			/*printf(" Cached: %lu", cached);*/
		}
    }
	util = (double) (mem_total - mem_free - buffers - cached) / (double) mem_total * 100.0; 
	printf("  MEM: %5.1f%%", util);
	fclose(file);
}

void net_util() {
	const char * const PROC_NET = "/proc/net/dev";
	char line[1024];
	FILE *file;
	unsigned long rx_bytes, tx_bytes;
	int i;

	if (!(file = fopen(PROC_NET, "r"))) {
		TRACE("/proc/net/dev fopen()");
		return;
	}

	for(i=0;i<2;i++) {
		if (!fgets(line, sizeof(line), file)) {
			TRACE("net_util() skip line failed");
			fclose(file);
			return;
		}
	}

	while (fgets(line, sizeof(line), file)) {
        if (strstr(line, "enp0s1")) {
			if (sscanf(line, "enp0s1: %lu %*u %*u %*u %*u %*u %*u %*u %lu", &rx_bytes, &tx_bytes) != 2) {
				TRACE("net_util() parse failed");
				fclose(file);
				break;
			}
			printf("  Interface: enp0s1, Receive: %lu, Transmit: %lu", rx_bytes, tx_bytes);
			break;
        }
    }
	fclose(file);
}

void io_util() {
	const char * const PROC_DISSTATS = "/proc/diskstats";
	char line[1024];
	FILE *file;
	unsigned long rd, wt;
	
	if (!(file = fopen(PROC_DISSTATS, "r"))) {
		TRACE("/proc/diskstats fopen()");
		return;
	}
	while (fgets(line, sizeof(line), file)) {
        if (strstr(line, "sda")) { 
			/* 
			major number
			minor number
			sda
			reads completed successfully
			reads merged
			sectors read
			time spent reading
			writes completed
			writes merged
			sectors written
			time spent writing
			I/Os currently in progress
			time spent doing I/Os
			*/
            sscanf(line, "%*d %*d sda %*u %*u %*u %lu %*u %*u %*u %lu", &rd, &wt);
            break;
        }
    }
	printf("  Device Name: sda, Time Spent Reading: %lu, Time Spent Writting: %lu", rd, wt);
	fclose(file);

}

int
main(int argc, char *argv[])
{
	/* /proc/stat/ cotains system statistics */
	const char * const PROC_STAT = "/proc/stat";
	char line[1024];
	FILE *file;

	UNUSED(argc);
	UNUSED(argv);

 	/* SIGINT to kill the process */
	if (SIG_ERR == signal(SIGINT, _signal_)) {
		TRACE("signal()");
		return -1;
	}
	while (!done) {
		if (!(file = fopen(PROC_STAT, "r"))) {
			TRACE("fopen()");
			return -1;
		}
		/* fgets read one line */
		if (fgets(line, sizeof (line), file)) {
			/* parse, \r move the cursor back */
			/* cpu utilization*/
			printf("\r CPU: %5.1f%%", cpu_util(line));
			/* ensure the output is immediately visible */
			fflush(stdout);
		}
		fclose(file);

		memory_util();
		net_util();
		io_util();

		us_sleep(500000);
		
	}
	printf("\rDone!   \n");
	return 0;
}

