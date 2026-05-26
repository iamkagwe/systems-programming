#ifndef SHARED_BUFFER_H
#define SHARED_BUFFER_H

#include <stdint.h>
#include <semaphore.h>

/*
 * Shared ring buffer for IPC
 * 
 * Multiple processes can map this structure into their address space
 * via POSIX shared memory (shm_open + mmap)
 * 
 * One producer writes, N consumers read from same buffer
 * Zero-copy: data stays in shared memory, no syscalls to copy
 */

#define SHARED_BUF_SIZE (1024 * 1024)  // 1 MB ring buffer
#define MSG_SIZE 256                    // Max message size
#define MAX_MESSAGES (SHARED_BUF_SIZE / MSG_SIZE)

typedef struct {
    // Ring buffer data
    char data[SHARED_BUF_SIZE];
    
    // Write and read positions (in messages, not bytes)
    // Multiple readers can advance independently
    uint64_t write_pos;      // Producer advances this
    uint64_t read_pos;       // Each consumer maintains their own copy
    
    // Synchronization
    sem_t *sem_full;         // Counts available messages (producer waits if full)
    sem_t *sem_empty;        // Counts empty slots (consumers wait if empty)
    sem_t *sem_mutex;        // Protects write_pos
    
} SharedBuffer;

/*
 * Create or open shared memory buffer
 * Returns: pointer to SharedBuffer in shared memory
 * If create=1: initializes new buffer (producer)
 * If create=0: opens existing buffer (consumers)
 */
SharedBuffer* buffer_open(int create);

/*
 * Close and unmap shared memory
 */
void buffer_close(SharedBuffer *buf);

/*
 * Unlink shared memory (cleanup, call from producer)
 */
void buffer_unlink(void);

/*
 * Producer: write message to buffer
 * Blocks if buffer is full
 */
int buffer_write(SharedBuffer *buf, const char *msg, int len);

/*
 * Consumer: read next message from buffer
 * Each consumer maintains their own read position (not stored in shared memory)
 * 
 * Blocks if no new messages since last read
 */
int buffer_read(SharedBuffer *buf, char *msg, int maxlen, uint64_t *my_read_pos);

#endif
