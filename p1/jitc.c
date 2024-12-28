/**
 * Tony Givargis
 * Copyright (C), 2023
 * University of California, Irvine
 *
 * CS 238P - Operating Systems
 * jitc.c
 */

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dlfcn.h>
#include "system.h"
#include "jitc.h"

/**
 * Needs:
 *   fork()
 *   execv()
 *   waitpid()
 *   WIFEXITED()
 *   WEXITSTATUS()
 *   dlopen()
 *   dlclose()
 *   dlsym()
 */

/* research the above Needed API and design accordingly */

struct jitc
{
    void *handle;
};

int jitc_compile(const char *input, const char *output)
{
    pid_t pid = fork();

    if (pid < 0)
    {
        TRACE("fork failed");
        return -1;
    }

    if (pid == 0)
    { /* child */
        /* gcc -shared -o output -fPIC input */
        char *args[] = {"gcc", "-shared", "-o", "", "-fPIC", "", NULL};
        args[3] = (char *)output;
        args[5] = (char *)input;

        execv("/usr/bin/gcc", args);
        TRACE("execv failed");
        return -1;
    }
    else
    { /* parent */
        int status;
        pid_t waited_pid = waitpid(pid, &status, 0);
        if (waited_pid == -1)
        {
            TRACE("waitpid failed");
            return -1;
        }

        if (WIFEXITED(status) == 0)
        {
            char error_msg[256];
            sprintf(error_msg, "child process exited with status %d", WEXITSTATUS(status));
            TRACE(error_msg);
            return -1;
        }
    }

    return 0;
}

struct jitc *jitc_open(const char *pathname)
{
    struct jitc *jitc;

    if (!(jitc = malloc(sizeof(struct jitc))))
    {
        TRACE("out of memory");
        return NULL;
    }

    jitc->handle = dlopen(pathname, RTLD_LAZY);
    if (!jitc->handle)
    {
        char error_msg[256];
        sprintf(error_msg, "load library failed: %s", dlerror());
        TRACE(error_msg);
        FREE(jitc);
        return NULL;
    }

    return jitc;
}

void jitc_close(struct jitc *jitc)
{
    if (!jitc)
    {
        TRACE("NULL jitc");
        return;
    }
    if (dlclose(jitc->handle) != 0)
    {
        char error_msg[256];
        sprintf(error_msg, "close library failed: %s", dlerror());
        TRACE(error_msg);
    }
    FREE(jitc);
}

long jitc_lookup(struct jitc *jitc, const char *symbol)
{
    void *address;
    char *error;

    if (jitc == NULL || jitc->handle == NULL)
    {
        TRACE("NULL jitc");
        return -1;
    }
    address = dlsym(jitc->handle, symbol);
    error = dlerror();
    if (error != NULL)
    {
        char error_msg[256];
        sprintf(error_msg, "locate symbol failed: %s", error);
        TRACE(error_msg);
        return -1;
    }
    return (long)address;
}