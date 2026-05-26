#include "shared_buffer.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define SHM_NAME "/shared_ring_buffer"
#define SEM_FULL_NAME "/sem_full"
#define SEM_EMPTY_NAME "/sem_empty"
#define SEM_MUTEX_NAME "/sem_mutex"

static int shm_fd = -1;
static SharedBuffer *shared = NULL;

/*
 * Create or open shared memory buffer
 * 
 * Producer (create=1):
 *   - Creates new shared memory object
 *   - Initializes semaphores
 *   - Zeroes buffer
 * 
 * Consumers (create=0):
 *   - Opens existing shared memory
 *   - Opens existing semaphores
 */
SharedBuffer* buffer_open(int create) {
    if (create) {
        // Producer: create new shared memory
        // O_CREAT: create if doesn't exist
        // O_EXCL: fail if already exists (prevents double-creation)
        shm_fd = shm_open(SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0666);
        if (shm_fd < 0) {
            perror("shm_open");
            return NULL;
        }
        
        // Resize to fit SharedBuffer struct
        if (ftruncate(shm_fd, sizeof(SharedBuffer)) < 0) {
            perror("ftruncate");
            shm_unlink(SHM_NAME);
            return NULL;
        }
        
        // Map into address space
        // MAP_SHARED: changes visible to other processes
        shared = (SharedBuffer *)mmap(NULL, sizeof(SharedBuffer),
                                      PROT_READ | PROT_WRITE,
                                      MAP_SHARED, shm_fd, 0);
        if (shared == MAP_FAILED) {
            perror("mmap");
            shm_unlink(SHM_NAME);
            return NULL;
        }
        
        // Zero the buffer
        memset(shared, 0, sizeof(SharedBuffer));
        
        // Initialize semaphores
        // sem_full: tracks available messages (starts at 0 = buffer empty)
        shared->sem_full = sem_open(SEM_FULL_NAME, O_CREAT | O_EXCL, 0666, 0);
        if (shared->sem_full == SEM_FAILED) {
            perror("sem_open full");
            munmap(shared, sizeof(SharedBuffer));
            shm_unlink(SHM_NAME);
            return NULL;
        }
        
        // sem_empty: tracks empty slots (starts at MAX_MESSAGES)
        shared->sem_empty = sem_open(SEM_EMPTY_NAME, O_CREAT | O_EXCL, 0666, MAX_MESSAGES);
        if (shared->sem_empty == SEM_FAILED) {
            perror("sem_open empty");
            sem_unlink(SEM_FULL_NAME);
            munmap(shared, sizeof(SharedBuffer));
            shm_unlink(SHM_NAME);
            return NULL;
        }
        
        // sem_mutex: protects write_pos (starts at 1 = unlocked)
        shared->sem_mutex = sem_open(SEM_MUTEX_NAME, O_CREAT | O_EXCL, 0666, 1);
        if (shared->sem_mutex == SEM_FAILED) {
            perror("sem_open mutex");
            sem_unlink(SEM_FULL_NAME);
            sem_unlink(SEM_EMPTY_NAME);
            munmap(shared, sizeof(SharedBuffer));
            shm_unlink(SHM_NAME);
            return NULL;
        }
        
    } else {
        // Consumer: open existing shared memory
        shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
        if (shm_fd < 0) {
            perror("shm_open consumer");
            return NULL;
        }
        
        // Map into address space
        shared = (SharedBuffer *)mmap(NULL, sizeof(SharedBuffer),
                                      PROT_READ | PROT_WRITE,
                                      MAP_SHARED, shm_fd, 0);
        if (shared == MAP_FAILED) {
            perror("mmap consumer");
            return NULL;
        }
        
        // Wait for semaphores to be initialized by producer
        // Try to open semaphores (they should already exist)
        shared->sem_full = sem_open(SEM_FULL_NAME, 0);
        shared->sem_empty = sem_open(SEM_EMPTY_NAME, 0);
        shared->sem_mutex = sem_open(SEM_MUTEX_NAME, 0);
        
        if (shared->sem_full == SEM_FAILED ||
            shared->sem_empty == SEM_FAILED ||
            shared->sem_mutex == SEM_FAILED) {
            fprintf(stderr, "Consumer: semaphores not ready (producer not started?)\n");
            munmap(shared, sizeof(SharedBuffer));
            return NULL;
        }
    }
    
    return shared;
}

/*
 * Close shared memory
 */
void buffer_close(SharedBuffer *buf) {
    if (buf && shm_fd >= 0) {
        munmap(buf, sizeof(SharedBuffer));
        close(shm_fd);
        shm_fd = -1;
    }
}

/*
 * Unlink shared memory (producer cleanup)
 */
void buffer_unlink(void) {
    shm_unlink(SHM_NAME);
    sem_unlink(SEM_FULL_NAME);
    sem_unlink(SEM_EMPTY_NAME);
    sem_unlink(SEM_MUTEX_NAME);
}

/*
 * Producer: write message to shared buffer
 * 
 * Algorithm:
 *   1. sem_wait(sem_empty): block if no empty slots
 *   2. sem_wait(sem_mutex): acquire exclusive write access
 *   3. Copy message to buffer[write_pos % SHARED_BUF_SIZE]
 *   4. Advance write_pos
 *   5. sem_post(sem_mutex): release exclusive access
 *   6. sem_post(sem_full): signal that new message available
 */
int buffer_write(SharedBuffer *buf, const char *msg, int len) {
    if (len > MSG_SIZE - 1) {
        fprintf(stderr, "Message too long: %d > %d\n", len, MSG_SIZE - 1);
        return -1;
    }
    
    // Wait for empty slot
    int ret = sem_wait(buf->sem_empty);
    if (ret < 0) {
        perror("sem_wait empty");
        return -1;
    }
    
    // Acquire write lock
    ret = sem_wait(buf->sem_mutex);
    if (ret < 0) {
        perror("sem_wait mutex");
        sem_post(buf->sem_empty);
        return -1;
    }
    
    // Calculate write offset in ring buffer
    uint64_t write_idx = buf->write_pos % MAX_MESSAGES;
    int offset = write_idx * MSG_SIZE;
    
    // Copy message to buffer
    memcpy(&buf->data[offset], msg, len);
    buf->data[offset + len] = '\0';
    
    // Advance write position
    buf->write_pos++;
    
    // Release write lock
    sem_post(buf->sem_mutex);
    
    // Signal that message is available
    sem_post(buf->sem_full);
    
    return len;
}

/*
 * Consumer: read next message from shared buffer
 * 
 * Each consumer maintains their own read_pos (not stored in shared memory)
 * This allows multiple readers to independently consume the same messages
 * 
 * Algorithm:
 *   1. Check if my_read_pos < write_pos (new message available?)
 *   2. If not: block on sem_full until producer writes
 *   3. Copy message from buffer[my_read_pos % SHARED_BUF_SIZE]
 *   4. Advance my_read_pos
 *   5. Signal empty slot
 */
int buffer_read(SharedBuffer *buf, char *msg, int maxlen, uint64_t *my_read_pos) {
    // Check if we've fallen behind producer
    // This is racy but informational only (actual sync via semaphore)
    if (*my_read_pos >= buf->write_pos) {
        // No new messages yet, wait for producer
        int ret = sem_wait(buf->sem_full);
        if (ret < 0) {
            perror("sem_wait full");
            return -1;
        }
    }
    
    // Calculate read offset in ring buffer
    uint64_t read_idx = *my_read_pos % MAX_MESSAGES;
    int offset = read_idx * MSG_SIZE;
    
    // Copy message from buffer
    int len = strlen(&buf->data[offset]);
    if (len > maxlen - 1) {
        len = maxlen - 1;
    }
    memcpy(msg, &buf->data[offset], len);
    msg[len] = '\0';
    
    // Advance consumer's local read position
    (*my_read_pos)++;
    
    // Signal that we freed a slot
    sem_post(buf->sem_empty);
    
    return len;
}
