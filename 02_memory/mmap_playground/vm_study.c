#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>

/**
 * Demonstrate mmap page faults
 * 
 * Virtual memory works by mapping:
 * - Virtual address space (yours) → Physical RAM
 * - Pages are loaded on demand (page faults)
 */
void demonstrate_page_faults(const char *filename) {
    printf("=== Page Fault Demonstration ===\n");
    printf("File: %s\n", filename);
    
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return;
    }
    
    struct stat sb;
    fstat(fd, &sb);
    size_t file_size = sb.st_size;
    
    // Memory map file
    // mmap parameters:
    //   arg1 (NULL):       Let OS choose virtual address (ASLR friendly)
    //   arg2 (file_size):  Map entire file into address space
    //   arg3 (PROT_READ):  Read-only access (can also be PROT_WRITE, PROT_EXEC)
    //   arg4 (MAP_SHARED): Changes visible to other processes & disk
    //                      (alternative: MAP_PRIVATE = changes local to this process)
    //   arg5 (fd):         File descriptor to map
    //   arg6 (0):          Offset in file to start mapping (0 = from beginning)
    // Returns: pointer to virtual address, or MAP_FAILED on error
    char *map = (char *)mmap(NULL, file_size, PROT_READ, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return;
    }
    
    printf("\nSequential access (simulates disk read):\n");
    double start = 0;
    int page_size = getpagesize();
    
    // First pass: sequential (will page fault)
    // PAGE FAULT MECHANISM:
    //   1. CPU accesses memory address that isn't in RAM
    //   2. CPU triggers page fault exception (trap to kernel)
    //   3. Kernel checks: is this a valid mmap region?
    //   4. If yes: loads page from disk into physical RAM
    //   5. Updates page table entry (PTE) to mark page as present
    //   6. Returns to user code - access now succeeds
    //   This is TRANSPARENT - your code doesn't see the fault!
    printf("  Pass 1 (cold, page faults): ");
    fflush(stdout);
    start = (double)clock() / CLOCKS_PER_SEC;
    
    // 'volatile' prevents compiler from optimizing away the loop
    // Without volatile: compiler sees sum is unused and removes entire loop
    // With volatile: compiler must execute as written (triggering actual page faults)
    volatile char sum = 0;
    for (size_t i = 0; i < file_size; i += page_size) {
        sum += map[i];  // Touch one byte per page (first touch triggers page fault)
    }
    
    double time_cold = (double)clock() / CLOCKS_PER_SEC - start;
    printf("%.3f seconds\n", time_cold);
    
    // Second pass: warm (pages already in memory)
    // All pages now cached in RAM (page cache) from first pass
    // No page faults this time - direct memory access only
    // Result: MUCH faster than first pass
    printf("  Pass 2 (warm, cached):     ");
    fflush(stdout);
    start = (double)clock() / CLOCKS_PER_SEC;
    
    for (size_t i = 0; i < file_size; i += page_size) {
        sum += map[i];  // These are cache hits - no disk I/O
    }
    
    double time_warm = (double)clock() / CLOCKS_PER_SEC - start;
    printf("%.3f seconds\n", time_warm);
    
    printf("\nObservation: First pass slower (page faults)\n");
    printf("             Second pass much faster (cached in RAM)\n");
    
    printf("\nMemory layout:\n");
    printf("  Page size: %d bytes\n", page_size);
    printf("  File size: %zu bytes\n", file_size);
    printf("  Pages: %zu\n", (file_size + page_size - 1) / page_size);
    
    // Cleanup
    munmap(map, file_size);
    close(fd);
}

/**
 * Compare mmap to read() for random access
 * This shows where mmap shines: irregular access patterns
 */
void demonstrate_random_access(const char *filename) {
    printf("\n=== Random Access Pattern ===\n");
    
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return;
    }
    
    struct stat sb;
    fstat(fd, &sb);
    size_t file_size = sb.st_size;
    
    printf("File size: %zu bytes\n", file_size);
    printf("Access pattern: random 1000 byte blocks\n");
    
    // Test 1: read() with seeks
    // SYSCALL COST: Each iteration = 2 syscalls + data copy
    //   - lseek():  context switch to kernel, update file offset
    //   - read():   context switch to kernel, disk I/O, copy data kernel→user
    // For random access, syscall overhead dominates the actual I/O time
    printf("\nMethod 1: read() with lseek (random access):\n");
    double start = (double)clock() / CLOCKS_PER_SEC;
    
    char buffer[1024];
    for (int i = 0; i < 100; i++) {
        off_t offset = (random() % (file_size - 1024));
        lseek(fd, offset, SEEK_SET);     // Syscall 1: seek
        read(fd, buffer, 1024);           // Syscall 2: read + copy
    }
    
    double elapsed_read = (double)clock() / CLOCKS_PER_SEC - start;
    printf("  Time: %.3f seconds\n", elapsed_read);
    
    // Test 2: mmap (random access is cheap)
    // NO SYSCALLS: Direct memory access for pages already in page cache
    // COST: If page not in RAM → page fault (transparent disk I/O)
    //       But once cached, access is just CPU memory load
    char *map = (char *)mmap(NULL, file_size, PROT_READ, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return;
    }
    
    printf("\nMethod 2: mmap (random access):\n");
    start = (double)clock() / CLOCKS_PER_SEC;
    
    volatile char sum = 0;
    for (int i = 0; i < 100; i++) {
        off_t offset = (random() % (file_size - 1024));
        for (int j = 0; j < 1024; j++) {
            sum += map[offset + j];  // Direct memory pointer arithmetic - no syscalls!
        }
    }
    
    double elapsed_mmap = (double)clock() / CLOCKS_PER_SEC - start;
    printf("  Time: %.3f seconds\n", elapsed_mmap);
    
    printf("\nObservation: mmap much faster for random access\n");
    printf("             (direct memory access vs syscalls)\n");
    
    munmap(map, file_size);
    close(fd);
}

/**
 * Show shared memory via mmap
 * Multiple processes can share the same file via mmap
 */
void demonstrate_shared_memory(const char *filename) {
    printf("\n=== Shared Memory via mmap ===\n");
    printf("(In production: multiple processes can mmap same file)\n");
    printf("(Enables zero-copy IPC - used in real databases)\n");
    
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return;
    }
    
    struct stat sb;
    fstat(fd, &sb);
    
    // SHARED MEMORY CONCEPT:
    // MAP_SHARED means: changes visible to other processes + disk
    // Multiple mmap calls to same file create multiple virtual mappings
    // that all point to the SAME physical pages via page tables
    // 
    // Virtual Space          Physical Pages      Disk
    // Process A: 0x1000 ──┐                    ┌─ file.bin
    // Process B: 0x5000 ──┼──→ [phys page]  ──┤
    // Process C: 0x9000 ──┘                    └─ (same)
    // 
    // KEY: All processes see identical, coherent data
    // This is ZERO-COPY IPC - the OS shares physical RAM
    
    // Map 1
    char *map1 = (char *)mmap(NULL, sb.st_size, PROT_READ, MAP_SHARED, fd, 0);
    
    // Map 2 (same file, different virtual address)
    char *map2 = (char *)mmap(NULL, sb.st_size, PROT_READ, MAP_SHARED, fd, 0);
    
    printf("File size: %lld\n", (long long)sb.st_size);
    printf("Map 1 address: %p\n", (void *)map1);
    printf("Map 2 address: %p\n", (void *)map2);
    printf("\nBoth map the SAME physical pages\n");
    printf("OS handles cache coherence automatically\n");
    printf("This is how databases do zero-copy sharing!\n");
    
    munmap(map1, sb.st_size);
    munmap(map2, sb.st_size);
    close(fd);
}

void create_demo_file(const char *filename, size_t size) {
    FILE *f = fopen(filename, "wb");
    char data[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    
    for (size_t i = 0; i < size; i++) {
        fputc(data[i % strlen(data)], f);
    }
    
    fclose(f);
}

int main(void) {
    printf("mmap Playground: Understanding Virtual Memory\n");
    printf("==============================================\n\n");
    
    const char *demo_file = "/tmp/mmap_demo.bin";
    create_demo_file(demo_file, 1024 * 1024);  // 1 MB
    
    demonstrate_page_faults(demo_file);
    demonstrate_random_access(demo_file);
    demonstrate_shared_memory(demo_file);
    
    printf("\n=== Key Concepts ===\n");
    printf("1. Virtual Memory: OS maps "
        "virtual addresses → physical RAM\n");
    printf("2. Page Faults: Happen transparently when accessing new pages\n");
    printf("3. First access slower (fault), subsequent accesses fast (cached)\n");
    printf("4. Random access: mmap beats read() (direct memory vs syscalls)\n");
    printf("5. Shared memory: Multiple processes can share pages\n");
    printf("6. Zero-copy: No data is copied between kernel ↔ user\n");
    printf("\nFor Storage Systems:\n");
    printf("- Databases use mmap for efficient file access\n");
    printf("- Cache management relies on page faults\n");
    printf("- High-performance I/O avoids copies\n");
    
    unlink(demo_file);
    return 0;
}
