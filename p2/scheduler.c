/**
 * Tony Givargis
 * Copyright (C), 2023
 * University of California, Irvine
 *
 * CS 238P - Operating Systems
 * scheduler.c
 */

#undef _FORTIFY_SOURCE

#include <unistd.h>
#include <signal.h>
#include <setjmp.h>
#include "system.h"
#include "scheduler.h"

/**
 * Needs:
 *   setjmp()
 *   longjmp()
 */

/* research the above Needed API and design accordingly */

#define SZ_STACK 4096

struct thread{
    jmp_buf ctx;
    struct {
        /* unsigned gurentee length */
        char *memory; 
        /* actually allocated */
        char *memory_;
    } stack;
    struct {
        void *arg;
        scheduler_fnc_t fnc;
    } code;
    enum {
        INIT,
        RUNNING,
        SLEEPING, 
        TERMINATED
    } status;
    struct thread *link;
};

struct thread *head, *current; 
jmp_buf ctx;

int scheduler_create(scheduler_fnc_t fnc, void *arg) {
    size_t PAGE_SIZE = page_size();
    struct thread *t = malloc(sizeof(struct thread));
    if (!t) {
        TRACE("scheduler_create malloc()");
        return -1;
    }
    t -> status = INIT;
    t -> code.fnc = fnc;
    t -> code.arg = arg;
    t->stack.memory_ = malloc(SZ_STACK + PAGE_SIZE);
    if (t -> stack.memory_ == NULL) {
        free(t);
        return -1;
    }
    /* page-aligned memory address */
    t -> stack.memory = memory_align(t -> stack.memory_, PAGE_SIZE);
    if (head == NULL) {
        head = t;
        current = t;
        t -> link = t;
    } else {
        t -> link = current -> link;
        current -> link = t;
        current = t;
    }
    return 0;
}

/*clean up the linked list */
void destroy(void) {
    struct thread *t, *next;
    t = head;
    do {
        next = t->link;
        free(t->stack.memory_); 
        free(t);
        t = next;
    } while (t != head);
    head = NULL;
    current = NULL;
}

static struct thread *candidate(void) {
    struct thread *t = current -> link;
    while (t != current) {
        if (t -> status == INIT || t -> status == SLEEPING) {
            return t;
        }
        t = t -> link;
    }
    /* cannot find a candidate */
    return NULL;
}

void schedule(void) {
    struct thread *t = candidate();
    uint64_t rsp;
    if (t == NULL) {
        return;
    }
    current = t;
    if (current -> status == INIT) {
        rsp = (uint64_t)current -> stack.memory + SZ_STACK;
        /* move data between memory and registers*/
        /* RSP register is the CPU register that tracks the current location of the stack*/
        __asm__ volatile("mov %[rs], %%rsp \n" : [rs] "+r"(rsp)::);
        current -> status = RUNNING;
        current -> code.fnc(current -> code.arg);
        current -> status = TERMINATED;
        longjmp(ctx, 1);
    } else {
        current -> status = RUNNING;
        longjmp(current -> ctx, 1);
    }
}

void set_timer(void) {
    if (SIG_ERR == signal(SIGALRM, (__sighandler_t)scheduler_yield)) {
        TRACE("set_timer signal()");
    }
    alarm(1);
}

void clear_timer() {
    alarm(0);
    if (SIG_ERR == signal(SIGALRM, SIG_DFL)) {
        TRACE("clear_timer signal()");
    }
}

void scheduler_execute(void) {
    setjmp(ctx);
    set_timer();
    /* go find one thread and run it */
    schedule();
    clear_timer(); 
    destroy();
}

void scheduler_yield(void) {
    if(current && !setjmp(current->ctx)){
        current->status= SLEEPING;
        /* restore main process*/
        longjmp(ctx, 1);

    }
}

