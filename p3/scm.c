/**
 * Tony Givargis
 * Copyright (C), 2023
 * University of California, Irvine
 *
 * CS 238P - Operating Systems
 * scm.c
 */

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include "scm.h"

/**
 * Needs:
 *   fstat()
 *   S_ISREG()
 *   open()
 *   close()
 *   sbrk()
 *   mmap()
 *   munmap()
 *   msync()
 */

/* research the above Needed API and design accordingly */

#define VM_ADDR 0x600000000000
struct scm {
    int fd;
    void *addr;
    size_t utilized;
    size_t capacity;
};

/* check if it's regular file, and set scm->capacity to file size */
int file_size(struct scm *scm) {

    struct stat st;
    size_t page = page_size();
    
    if (fstat(scm->fd, &st) == -1) {
        TRACE("fstat failed");
        return -1;
    }    
    if (!S_ISREG(st.st_mode)) {
        TRACE("not a regualr file");
        return -1;
    }

    /* scm->capacity = st.st_size;  */
    scm->capacity = (st.st_size / page) * page; 
    scm->utilized = 0;

    return (0 >= scm->capacity) ? -1 : 0;
}

/**
 * Initializes an SCM region using the file specified in pathname as the
 * backing device, opening the region for memory allocation activities.
 *
 * pathname: the file pathname of the backing device
 * truncate: if non-zero, truncates the SCM region, clearning all data
 *
 * return: an opaque handle or NULL on error
 */
/*
+----------------+------------------+-----------------+
| META Utilized  | Data block       | Data block      |
| (size_t)       |                  |                 |
+----------------+------------------+-----------------+
^
|
scm->addr
*/

struct scm *scm_open(const char *pathname, int truncate) {
 
    size_t curr;
    size_t vm_addr;
    size_t page = page_size();

    struct scm *scm = malloc(sizeof(struct scm));
    if (!scm) {
        TRACE("scm malloc failed");
        return NULL;
    }
    memset(scm, 0, sizeof(struct scm));

    assert(pathname);
    scm->fd = open(pathname, O_RDWR);
    if (scm->fd == -1) {
        TRACE("open failed");
        free(scm);
        return NULL;
    }
    
    if (-1 == file_size(scm)) {
        TRACE("file_size");
        close(scm->fd);
        free(scm);
        return NULL;
    }
    
    curr = (size_t)sbrk(0);
    vm_addr = (VM_ADDR / page) * page;
    /*
    printf("current sbrk: %lu\n", (unsigned long)curr);
    printf("current vm_addr: %lu\n", (unsigned long)vm_addr);
    */
    if (vm_addr < curr) {
        TRACE("vm_addr");
        close(scm->fd);
        free(scm);
        return NULL;
    }

    scm->addr = mmap((void *)vm_addr, scm->capacity, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_SHARED, scm->fd, 0);
    if (MAP_FAILED == scm->addr) {
        TRACE("mmap failed");
        close(scm->fd);
        free(scm);
        return NULL;
    }

    if (truncate) {
        /*  Ensures file size matches scm->capacity */
        if (ftruncate(scm->fd, scm->capacity) == -1) {
            TRACE("ftruncate failed");
            close(scm->fd);
            free(scm);
            return NULL;
        }
        scm->utilized = 0; 
        memset(scm->addr, 0, scm->capacity);
    }
    /* restore scm->utilized */
    else {
        scm->utilized = *(size_t *)scm->addr;
    }

    return scm;
}

/**
 * Closes a previously opened SCM handle.
 *
 * scm: an opaque handle previously obtained by calling scm_open()
 *
 * Note: scm may be NULL
 */

void scm_close(struct scm *scm) {
    
    if (!scm) {
        return;
    }
    
    if (scm->addr != MAP_FAILED) {
        /* ensures changes made to the region are written back to file. */
        if (msync(scm->addr, scm->capacity, MS_SYNC) == -1) {
            TRACE("mysync failed");
            return;
        }
        if (munmap(scm->addr, scm->capacity)) {
            TRACE("munmap failed");
            return;
        }
    }
    if (scm->fd) {
        close(scm->fd);
    }
    memset(scm, 0, sizeof(struct scm));
    free(scm);
}

/* 0 = free, 1 = currently using, 2 = used before, block_size != 0 */
void set_block_status(void *p, short status) {
    
    *(short *)p = status;
}

void set_block_size(void *p, size_t n) {

    void *sizeLocation = (char *)p + sizeof(short);
    *(size_t *)sizeLocation = n;
}

/* stored at the start of base address */
void set_meta_utilized(struct scm *scm) {
    
    *(size_t *)scm->addr = scm->utilized;
}

/**
 * Analogous to the standard C malloc function, but using SCM region.
 *
 * scm: an opaque handle previously obtained by calling scm_open()
 * n  : the size of the requested memory in bytes
 *
 * return: a pointer to the start of the allocated memory or NULL on error
 */
/*
+----------------+------------------+-----------------+
| Block Status   | Block Size = n   | Data ...        |
| (short)        | (size_t)         |                 |
+----------------+------------------+-----------------+
^                                   ^
|                                   |
curr                                return this
*/

void *scm_malloc(struct scm *scm, size_t n) {

    void *curr;
    void *end;

    short status;
    size_t block_size;    
    size_t total_size;

    if (n == 0) {
        TRACE("n is empty");
        return NULL;
    }

    total_size = n + sizeof(short) + sizeof(size_t);
    /* skip meta_utilized*/
    curr = (char *)scm->addr + sizeof(size_t);
    end = (char *)scm->addr + scm->capacity;
    
    while (curr < end) {
        status = *(short *)curr;

        /* uninitialized space */
        if (status == 0) {
            if ((char *)curr + total_size > (char *)end) {
                break;
            }
            scm->utilized += total_size;
            set_block_status(curr, 1);
            set_block_size(curr, n); 
            set_meta_utilized(scm);
            return (char *)curr + sizeof(short) + sizeof(size_t);
        }
        /* was allocated and removed, check if space is enough for total_size*/
        else if (status == 2) {
            block_size = *(size_t *)((char *)curr + sizeof(short));
            if (block_size >= n) {
                scm->utilized += block_size + sizeof(short) + sizeof(size_t);
                set_block_status(curr, 1);
                set_meta_utilized(scm);
                return (char *)curr + sizeof(short) + sizeof(size_t);
            }          
            else {
                curr = (char *)curr + sizeof(short) + sizeof(size_t) + block_size; 
            }  
        }
        /* currently using*/
        else {
            block_size = *(size_t *)((char *)curr + sizeof(short));
            curr = (char *)curr + sizeof(short) + sizeof(size_t) + block_size; 
        }
    }

    TRACE("not enough space");    
    return NULL;
}

/**
 * Analogous to the standard C strdup function, but using SCM region.
 *
 * scm: an opaque handle previously obtained by calling scm_open()
 * s  : a C string to be duplicated.
 *
 * return: the base memory address of the duplicated C string or NULL on error
 */

char *scm_strdup(struct scm *scm, const char *s) {

    size_t length;
    char *dest;

    if (!s) {
        TRACE("s is NULL");
        return NULL;
    }

    /* +1 for '\0' */
    length = strlen(s) + 1;
    dest = scm_malloc(scm, length);
    if (!dest) {
        TRACE("dest scm_malloc failed");
        return NULL;
    }
    memcpy(dest, s, length);

    return dest;    
}

/**
 * Analogous to the standard C free function, but using SCM region.
 *
 * scm: an opaque handle previously obtained by calling scm_open()
 * p  : a pointer to the start of a previously allocated memory
 */

void scm_free(struct scm *scm, void *p) {

    void *block_start = (char *)p - sizeof(size_t) - sizeof(short);

    short status = *(short *)block_start;
    size_t block_size = *(size_t *)((char *)block_start + sizeof(short));
    size_t total_size = block_size + sizeof(short) + sizeof(size_t);

    if (status == 1) {    
        set_block_status(block_start, 2);
        scm->utilized -= total_size;
        set_meta_utilized(scm);   
        memset(p, 0, block_size);
    }
    else {
        TRACE("block is empty");
    }
}

/**
 * Returns the number of SCM bytes utilized thus far.
 *
 * scm: an opaque handle previously obtained by calling scm_open()
 *
 * return: the number of bytes utilized thus far
 */

size_t scm_utilized(const struct scm *scm) {

    return scm->utilized;
}
/**
 * Returns the number of SCM bytes available in total.
 *
 * scm: an opaque handle previously obtained by calling scm_open()
 *
 * return: the number of bytes available in total
 */

size_t scm_capacity(const struct scm *scm) {
    
    return scm->capacity;
}

/**
 * Returns the base memory address withn the SCM region, i.e., the memory
 * pointer that would have been returned by the first call to scm_malloc()
 * after a truncated initialization.
 *
 * scm: an opaque handle previously obtained by calling scm_open()
 *
 * return: the base memory address within the SCM region
 */

void *scm_mbase(struct scm *scm) {

    /* scm->addr + utilized + block1 status + block1 size */
    return (char *)scm->addr + sizeof(size_t) + sizeof(short) + sizeof(size_t); 
}