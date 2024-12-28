/**
 * Tony Givargis
 * Copyright (C), 2023
 * University of California, Irvine
 *
 * CS 238P - Operating Systems
 * logfs.c
 */

#include <pthread.h>
#include "device.h"
#include "logfs.h"

#define WCACHE_BLOCKS 32
#define RCACHE_BLOCKS 256

/**
 * Needs:
 *   pthread_create()
 *   pthread_join()
 *   pthread_mutex_init()
 *   pthread_mutex_destroy()
 *   pthread_mutex_lock()
 *   pthread_mutex_unlock()
 *   pthread_cond_init()
 *   pthread_cond_destroy()
 *   pthread_cond_wait()
 *   pthread_cond_signal()
 */

/* research the above Needed API and design accordingly */

struct write_buffer
{
    void *buffer;    /* write buffer, block aligned */
    void *buffer_;   /* write buffer, actually malloc and free */
    size_t head;     /* write pointer, assert (capacity >= head) */
    size_t tail;     /* read pointer, assert (0 == (tail % Block)) */
    size_t capacity; /* buffer size, fixed */
    size_t utilized; /* size of buffer that already been used, also is the size of pending data between tail and head */
};

struct worker
{
    pthread_t worker_thread;
    pthread_mutex_t lock;
    pthread_cond_t data_avail;  /* worker thread: check data available */
    pthread_cond_t space_avail; /* append: check space available */
    int done;
};

struct read_cache_block
{
    void *block;  /* read cache, block aligned */
    void *block_; /* read cache, actually malloc and free */
    size_t tag;
    short valid;
};

struct logfs
{
    /* base info */
    struct device *device;
    size_t utilized;
    size_t capacity;

    /* write buffer*/
    struct write_buffer *write_buffer;

    /* worker */
    struct worker *worker;

    /* read */
    struct read_cache_block read_cache[RCACHE_BLOCKS];
};

/**
 * cache_block_mapping:
 * get the index of target block in read cache
 * map read_offset of device to the index of block in read cache
 */
uint64_t cache_block_mapping(struct logfs *logfs, uint64_t device_offset)
{
    uint64_t block_size = device_block(logfs->device);
    return ((device_offset - (STORE_RESTORE_FILE * block_size)) / block_size) % RCACHE_BLOCKS;
}

/**
 * check_cache_hit:
 *  if cache hit, return the stored data
 */
char *check_cache_hit(struct logfs *logfs, uint64_t device_off)
{
    uint64_t block_idx = cache_block_mapping(logfs, device_off);
    if (logfs->read_cache[block_idx].valid && logfs->read_cache[block_idx].tag == device_off)
    {
        return (char *)logfs->read_cache[block_idx].block;
    }
    return NULL; /* data not existed in read cache */
}

void save_to_cache(struct logfs *logfs, uint64_t device_off, char *data)
{
    uint64_t block_idx = cache_block_mapping(logfs, device_off);
    logfs->read_cache[block_idx].valid = 1;
    logfs->read_cache[block_idx].tag = device_off;
    memcpy(logfs->read_cache[block_idx].block, data, device_block(logfs->device));
}

/**
 * update_read_cache_after_write:
 *  buf: write buffer
 *  off: device offset
 *  len: len of write data
 */
void update_read_cache_after_write(struct logfs *logfs,
                                   char *write_buf_src,
                                   uint64_t device_off,
                                   uint64_t write_len)
{
    uint64_t write_buf_off = 0;
    uint64_t block_size = device_block(logfs->device);

    while (write_len > 0)
    {
        /* update 1 cache block a time */
        save_to_cache(logfs, device_off, write_buf_src + write_buf_off);
        write_buf_off += block_size;
        device_off += block_size;
        write_len -= block_size;
    }

    return;
}

/**
 * write_to_disk: write data from write_buffer to disk
 */
int write_to_disk(struct logfs *logfs)
{
    uint64_t block_size = device_block(logfs->device);

    char *addr_src = (char *)logfs->write_buffer->buffer + logfs->write_buffer->tail;
    uint64_t device_off = STORE_RESTORE_FILE * block_size + logfs->utilized / block_size * block_size;
    uint64_t head_prev_aligned = logfs->write_buffer->head / block_size * block_size;
    uint64_t write_len; /* length of data that needed to be written into disk(include garbage), block aligned */
    uint64_t data_size; /* actual written data size, ignore garbage */

    /* tail < head and head-tail < 1 block */
    if (logfs->write_buffer->tail == head_prev_aligned)
    {
        write_len = block_size;

        if (device_write(logfs->device, addr_src, device_off, write_len))
        {
            TRACE("device write failed");
            return -1;
        }

        /* update read cache */
        update_read_cache_after_write(logfs, addr_src, device_off, write_len);
        logfs->utilized += logfs->write_buffer->utilized;
        logfs->write_buffer->utilized = 0;
        return 0;
    }

    /* tail < head: |---tail***head---|, write data(*) between tail and head to disk */
    if (logfs->write_buffer->tail < head_prev_aligned)
    {
        if (head_prev_aligned < logfs->write_buffer->head)
        {
            write_len = head_prev_aligned + block_size - logfs->write_buffer->tail;
        }
        else
        {
            write_len = head_prev_aligned - logfs->write_buffer->tail;
        }

        data_size = logfs->write_buffer->utilized;
        logfs->write_buffer->tail = head_prev_aligned % logfs->write_buffer->capacity;
    }

    /* tail > head: |***head---tail***|, write data(*) between tail to end to disk, data between start to head wait for next write */
    else if (logfs->write_buffer->tail > head_prev_aligned)
    {
        write_len = logfs->write_buffer->capacity - logfs->write_buffer->tail;
        data_size = logfs->write_buffer->utilized - logfs->write_buffer->head;
        logfs->write_buffer->tail = 0;
    }

    if (device_write(logfs->device, addr_src, device_off, write_len))
    {
        TRACE("device write failed");
        return -1;
    }
    /* update read cache */
    update_read_cache_after_write(logfs, addr_src, device_off, write_len);

    logfs->utilized += data_size;
    logfs->write_buffer->utilized -= data_size;

    return 0;
}

void *worker_thread(void *arg)
{
    struct logfs *logfs = (struct logfs *)arg;
    uint64_t block_size = device_block(logfs->device);

    while (1)
    {
        if (pthread_mutex_lock(&logfs->worker->lock))
        {
            TRACE("lock failed");
            return NULL;
        }

        while (logfs->write_buffer->utilized < block_size && !logfs->worker->done)
        {
            if (pthread_cond_wait(&logfs->worker->data_avail, &logfs->worker->lock))
            {
                TRACE("wait data avail cond failed");
                pthread_mutex_unlock(&logfs->worker->lock);
                return NULL;
            }
            if (logfs->worker->done)
            {
                break;
            }
        }

        if (logfs->write_buffer->utilized >= block_size && !logfs->worker->done)
        {
            if (write_to_disk(logfs))
            {
                TRACE("write to disk failed");
                pthread_mutex_unlock(&logfs->worker->lock);
                return NULL;
            }
            if (pthread_cond_signal(&logfs->worker->space_avail))
            {
                TRACE("space avail cond signal failed");
                pthread_mutex_unlock(&logfs->worker->lock);
                return NULL;
            }
        }

        if (pthread_mutex_unlock(&logfs->worker->lock))
        {
            TRACE("unlock failed");
            return NULL;
        }

        if (logfs->worker->done)
        {
            while (logfs->write_buffer->utilized > 0) /*final flush */
            {
                if (write_to_disk(logfs))
                {
                    TRACE("write to disk failed");
                    pthread_mutex_unlock(&logfs->worker->lock);
                    return NULL;
                }
            }
            break;
        }
    }

    return NULL;
}

void set_meta_data(struct logfs *logfs)
{
    uint64_t block_size = device_block(logfs->device);
    void *meta_;
    size_t *meta;
    if (!(meta_ = malloc(2 * block_size)))
    {
        TRACE("malloc failed");
        return;
    }
    memset(meta_, 0, 2 * block_size);
    meta = memory_align(meta_, block_size);
    *meta = logfs->utilized;
    if (device_write(logfs->device, meta, (uint64_t)0, block_size))
    {
        TRACE("device write failed");
        FREE(meta_);
        return;
    }
    FREE(meta_);
}

void *restore_meta_data(struct logfs *logfs)
{
    uint64_t block_size = device_block(logfs->device);
    void *meta_;
    size_t *meta;
    if (!(meta_ = malloc(2 * block_size)))
    {
        TRACE("malloc failed");
        return NULL;
    }
    memset(meta_, 0, 2 * block_size);
    meta = memory_align(meta_, block_size);
    if (device_read(logfs->device, meta, 0, block_size))
    {
        TRACE("device read failed");
        FREE(meta_);
        return NULL;
    }
    logfs->utilized = *meta;
    FREE(meta_);
    return NULL;
}

struct logfs *logfs_open(const char *pathname)
{
    struct logfs *logfs;
    uint64_t block_size;
    int i;

    assert(safe_strlen(pathname));
    if (!(logfs = malloc(sizeof(struct logfs))))
    {
        TRACE("malloc logfs failed");
        return NULL;
    }
    memset(logfs, 0, sizeof(struct logfs));

    /* init device */
    if (!(logfs->device = device_open(pathname)))
    {
        logfs_close(logfs);
        TRACE("device_open failed");
        return NULL;
    }
    logfs->capacity = device_size(logfs->device);
    logfs->utilized = 0;

    /* init write buffer */
    if (!(logfs->write_buffer = malloc(sizeof(struct write_buffer))))
    {
        logfs_close(logfs);
        TRACE("malloc write buffer failed");
        return NULL;
    }
    memset(logfs->write_buffer, 0, sizeof(struct write_buffer));
    block_size = device_block(logfs->device);

    logfs->write_buffer->capacity = block_size * WCACHE_BLOCKS;

    if (!(logfs->write_buffer->buffer_ = malloc(logfs->write_buffer->capacity + 1 * block_size)))
    {
        logfs_close(logfs);
        TRACE("malloc write buffer failed");
        return NULL;
    }
    memset(logfs->write_buffer->buffer_, 0, logfs->write_buffer->capacity + 1 * block_size);
    logfs->write_buffer->buffer = memory_align(logfs->write_buffer->buffer_, block_size);

    logfs->write_buffer->head = 0;
    logfs->write_buffer->tail = 0;
    logfs->write_buffer->utilized = 0;

    if (STORE_RESTORE_FILE)
    {
        restore_meta_data(logfs);
    }

    /* init read buffer */
    for (i = 0; i < RCACHE_BLOCKS; i++)
    {
        if (!(logfs->read_cache[i].block_ = malloc(block_size * 2)))
        {
            logfs_close(logfs);
            TRACE("malloc read buffer block failed");
            return NULL;
        }
        memset(logfs->read_cache[i].block_, 0, block_size * 2);
        logfs->read_cache[i].block = memory_align(logfs->read_cache[i].block_, block_size);
        logfs->read_cache[i].tag = 0;
        logfs->read_cache[i].valid = 0;
    }

    /* init worker thread */
    if (!(logfs->worker = malloc(sizeof(struct worker))))
    {
        logfs_close(logfs);
        TRACE("malloc worker failed");
        return NULL;
    }
    memset(logfs->worker, 0, sizeof(struct worker));
    if (pthread_mutex_init(&logfs->worker->lock, NULL))
    {
        logfs_close(logfs);
        TRACE("create worker mutex lock failed");
        return NULL;
    }
    if (pthread_cond_init(&logfs->worker->data_avail, NULL) || pthread_cond_init(&logfs->worker->space_avail, NULL))
    {
        logfs_close(logfs);
        TRACE("create worker thread failed");
        return NULL;
    }
    if (pthread_create(&logfs->worker->worker_thread, NULL, &worker_thread, (void *)logfs))
    {
        logfs_close(logfs);
        TRACE("create worker thread failed");
        return NULL;
    }

    return logfs;
}

int flush_data(struct logfs *logfs)
{
    if (pthread_mutex_lock(&logfs->worker->lock))
    {
        TRACE("lock failed");
        return -1;
    }
    while (logfs->write_buffer->utilized >= device_block(logfs->device))
    {
        if (pthread_cond_signal(&logfs->worker->data_avail))
        {
            TRACE("signal failed\n");
            pthread_mutex_unlock(&logfs->worker->lock);
            return -1;
        }
        if (pthread_cond_wait(&logfs->worker->space_avail, &logfs->worker->lock))
        {
            TRACE("wait space avail cond failed");
            pthread_mutex_unlock(&logfs->worker->lock);
            return -1;
        }
    }
    while (logfs->write_buffer->utilized > 0 && logfs->write_buffer->utilized < device_block(logfs->device))
    {
        if (write_to_disk(logfs))
        {
            TRACE("write to disk failed");
            pthread_mutex_unlock(&logfs->worker->lock);
            return -1;
        }
        if (pthread_cond_signal(&logfs->worker->space_avail))
        {
            TRACE("signal failed");
            pthread_mutex_unlock(&logfs->worker->lock);
            return -1;
        }
    }
    if (pthread_mutex_unlock(&logfs->worker->lock))
    {
        TRACE("unlock failed");
        return -1;
    }
    return 0;
}

void logfs_close(struct logfs *logfs)
{
    int i;
    assert(logfs);

    if (pthread_mutex_lock(&logfs->worker->lock))
    {
        TRACE("lock failed");
    }
    logfs->worker->done = 1;
    if (pthread_cond_signal(&logfs->worker->data_avail))
    {
        TRACE("data avail signal failed");
    }
    if (pthread_mutex_unlock(&logfs->worker->lock))
    {
        TRACE("unlock failed");
    }

    /* destroy worker */
    if (pthread_join(logfs->worker->worker_thread, NULL))
    {
        TRACE("wait thread end failed");
    }
    if (STORE_RESTORE_FILE)
    {
        set_meta_data(logfs);
    }

    if (pthread_mutex_destroy(&logfs->worker->lock))
    {
        TRACE("destroy lock failed");
    }
    if (pthread_cond_destroy(&logfs->worker->data_avail) || pthread_cond_destroy(&logfs->worker->space_avail))
    {
        TRACE("destroy conditions failed");
    }

    FREE(logfs->worker);

    /* destroy write buffer */
    if (!logfs->write_buffer)
    {
        TRACE("write buffer is null");
        return;
    }
    FREE(logfs->write_buffer->buffer_);
    FREE(logfs->write_buffer);

    /* destroy read buffer*/
    for (i = 0; i < RCACHE_BLOCKS; i++)
    {
        FREE(logfs->read_cache[i].block_);
    }

    device_close(logfs->device);
    memset(logfs, 0, sizeof(struct logfs));
    FREE(logfs);
}

int logfs_read(struct logfs *logfs, void *buf, uint64_t off, size_t len)
{

    int num_blocks, i;
    uint64_t device_start_block_off, device_end_block_off;
    uint64_t block_size;
    uint64_t bytes_read = 0;

    assert(!len || buf);

    block_size = device_block(logfs->device);
    off += STORE_RESTORE_FILE * block_size;

    device_start_block_off = off / block_size * block_size;
    device_end_block_off = (off + len) / block_size * block_size;
    num_blocks = (device_end_block_off - device_start_block_off) / block_size;
    if (off + len > device_end_block_off)
    {
        num_blocks += 1;
    }

    /* flush data from write buffer to disk */
    if (flush_data(logfs))
    {
        TRACE("flush data failed");
        return -1;
    };

    /* read data by blocks in device */
    for (i = 0; i < num_blocks; i++)
    {
        uint64_t data_off, read_len;
        uint64_t device_off = device_start_block_off + i * block_size;
        char *data;
        char *data_;

        if (i == 0)
        {                                            /* first block*/
            data_off = off - device_start_block_off; /* off may start from the middle of the first block */
            read_len = MIN(block_size - data_off, len);
        }
        else if (i == num_blocks - 1)
        { /* last block */
            data_off = 0;
            read_len = off + len - device_end_block_off;
        }
        else
        {
            data_off = 0;
            read_len = block_size;
        }

        /* first, try to read directly from cache */
        if ((data = check_cache_hit(logfs, device_off)))
        {
            /* cache hit */
            memcpy((char *)buf + bytes_read, data + data_off, read_len); /* read data from cache */
            bytes_read += read_len;
            continue;
        }

        /* cach miss, read from device */
        if (!(data_ = malloc(2 * block_size)))
        {
            TRACE("malloc failed");
            return -1;
        }
        memset(data_, 0, 2 * block_size);
        data = memory_align(data_, block_size);
        if (device_read(logfs->device, data, device_off, block_size))
        {
            TRACE("device read failed");
            FREE(data_);
            return -1;
        }
        memcpy((char *)buf + bytes_read, data + data_off, read_len);
        save_to_cache(logfs, device_off, data);
        bytes_read += read_len;
        FREE(data_);
    }

    return 0;
}

int logfs_append(struct logfs *logfs, const void *buf, uint64_t len)
{
    if (!logfs)
    {
        TRACE("logfs is null");
        return -1;
    }
    assert(buf || !len);

    if (logfs->utilized + len > logfs->capacity)
    {
        TRACE("no space");
        return -1;
    }

    if (pthread_mutex_lock(&logfs->worker->lock))
    {
        TRACE("lock failed");
        return -1;
    }

    while (logfs->write_buffer->utilized + len > logfs->write_buffer->capacity)
    {

        if (pthread_cond_wait(&logfs->worker->space_avail, &logfs->worker->lock))
        {
            TRACE("wait space avail cond failed");
            pthread_mutex_unlock(&logfs->worker->lock);
            return -1;
        }
    }

    if (logfs->write_buffer->head + len <= logfs->write_buffer->capacity)
    {
        memcpy((char *)logfs->write_buffer->buffer + logfs->write_buffer->head, buf, len);
        logfs->write_buffer->head = (logfs->write_buffer->head + len) % logfs->write_buffer->capacity;
    }
    else
    {
        /* split data to 2 parts */
        /* part1: head to buffer end */
        /* part2: buffer start to the remaining len */
        uint64_t part1_len = logfs->write_buffer->capacity - logfs->write_buffer->head;
        uint64_t part2_len = len - part1_len;
        memcpy((char *)logfs->write_buffer->buffer + logfs->write_buffer->head, buf, part1_len);
        memcpy((char *)logfs->write_buffer->buffer, (char *)buf + part1_len, part2_len);
        logfs->write_buffer->head = part2_len % logfs->write_buffer->capacity;
    }

    logfs->write_buffer->utilized += len;

    if (pthread_cond_signal(&logfs->worker->data_avail))
    {
        TRACE("data avail signal failed");
        pthread_mutex_unlock(&logfs->worker->lock);
        return -1;
    }

    if (pthread_mutex_unlock(&logfs->worker->lock))
    {
        TRACE("unlock failed");
        return -1;
    }

    return 0;
}

uint64_t logfs_size(struct logfs *logfs)
{
    assert(logfs);
    return logfs->utilized;
}