#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <ctype.h>

/**
 * Traditional read()-based file search
 * Copies data from kernel buffer → user buffer
 */
int search_with_read(const char *filename, const char *query) {
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return -1;
    }
    
    char buffer[4096];
    int matches = 0;
    ssize_t bytes_read;
    
    while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {
        for (ssize_t i = 0; i < bytes_read; i++) {
            if (buffer[i] == query[0]) {
                // Found potential match
                if (i + strlen(query) <= (size_t)bytes_read) {
                    if (strncmp(&buffer[i], query, strlen(query)) == 0) {
                        matches++;
                    }
                }
            }
        }
    }
    
    close(fd);
    return matches;
}

/**
 * mmap-based file search
 * Memory maps file directly into address space (zero-copy)
 */
int search_with_mmap(const char *filename, const char *query) {
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return -1;
    }
    
    // Get file size
    struct stat sb;
    if (fstat(fd, &sb) < 0) {
        perror("fstat");
        close(fd);
        return -1;
    }
    
    size_t file_size = sb.st_size;
    
    // Map file into memory
    void *map = mmap(NULL, file_size, PROT_READ, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return -1;
    }
    
    // Search directly in mapped memory
    int matches = 0;
    char *data = (char *)map;
    
    for (size_t i = 0; i < file_size - strlen(query) + 1; i++) {
        if (data[i] == query[0]) {
            if (strncmp(&data[i], query, strlen(query)) == 0) {
                matches++;
            }
        }
    }
    
    // Cleanup
    munmap(map, file_size);
    close(fd);
    
    return matches;
}

/**
 * mmap with advise: hint to OS about access pattern
 * MADV_SEQUENTIAL: OS optimizes for sequential reads
 */
int search_with_mmap_advise(const char *filename, const char *query) {
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return -1;
    }
    
    struct stat sb;
    if (fstat(fd, &sb) < 0) {
        perror("fstat");
        close(fd);
        return -1;
    }
    
    size_t file_size = sb.st_size;
    
    void *map = mmap(NULL, file_size, PROT_READ, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return -1;
    }
    
    // Tell OS: we're reading sequentially
    madvise(map, file_size, MADV_SEQUENTIAL);
    
    int matches = 0;
    char *data = (char *)map;
    
    for (size_t i = 0; i < file_size - strlen(query) + 1; i++) {
        if (data[i] == query[0]) {
            if (strncmp(&data[i], query, strlen(query)) == 0) {
                matches++;
            }
        }
    }
    
    munmap(map, file_size);
    close(fd);
    
    return matches;
}

double get_time_seconds() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

void benchmark(const char *filename, const char *query, int iterations) {
    printf("\n=== Benchmark: Searching '%s' in %s ===\n", query, filename);
    
    // Get file size for info
    struct stat sb;
    stat(filename, &sb);
    printf("File size: %.2f MB\n", sb.st_size / 1e6);
    printf("Iterations: %d\n", iterations);
    
    // Test 1: Traditional read()
    printf("\n1. Traditional read():\n");
    double start = get_time_seconds();
    int matches_read = 0;
    for (int i = 0; i < iterations; i++) {
        matches_read = search_with_read(filename, query);
    }
    double elapsed_read = get_time_seconds() - start;
    printf("   Matches: %d\n", matches_read);
    printf("   Time: %.3f seconds\n", elapsed_read);
    printf("   Throughput: %.1f MB/s\n", (sb.st_size * iterations / 1e6) / elapsed_read);
    
    // Test 2: mmap (no advise)
    printf("\n2. mmap (basic):\n");
    start = get_time_seconds();
    int matches_mmap = 0;
    for (int i = 0; i < iterations; i++) {
        matches_mmap = search_with_mmap(filename, query);
    }
    double elapsed_mmap = get_time_seconds() - start;
    printf("   Matches: %d\n", matches_mmap);
    printf("   Time: %.3f seconds\n", elapsed_mmap);
    printf("   Throughput: %.1f MB/s\n", (sb.st_size * iterations / 1e6) / elapsed_mmap);
    printf("   Speedup: %.2f x\n", elapsed_read / elapsed_mmap);
    
    // Test 3: mmap with MADV_SEQUENTIAL
    printf("\n3. mmap (with MADV_SEQUENTIAL):\n");
    start = get_time_seconds();
    int matches_advise = 0;
    for (int i = 0; i < iterations; i++) {
        matches_advise = search_with_mmap_advise(filename, query);
    }
    double elapsed_advise = get_time_seconds() - start;
    printf("   Matches: %d\n", matches_advise);
    printf("   Time: %.3f seconds\n", elapsed_advise);
    printf("   Throughput: %.1f MB/s\n", (sb.st_size * iterations / 1e6) / elapsed_advise);
    printf("   Speedup vs read(): %.2f x\n", elapsed_read / elapsed_advise);
}

void create_test_file(const char *filename, size_t size_mb) {
    printf("Creating test file: %s (%.1f MB)\n", filename, size_mb / 1e6);
    
    FILE *f = fopen(filename, "wb");
    if (!f) {
        perror("fopen");
        return;
    }
    
    size_t bytes_written = 0;
    char pattern[] = "The quick brown fox jumps over the lazy dog. ";
    
    while (bytes_written < size_mb * 1e6) {
        size_t to_write = sizeof(pattern) - 1;
        if (bytes_written + to_write > size_mb * 1e6) {
            to_write = size_mb * 1e6 - bytes_written;
        }
        fwrite(pattern, 1, to_write, f);
        bytes_written += to_write;
    }
    
    fclose(f);
    printf("File created.\n");
}

int main(void) {
    printf("mmap Playground: Virtual Memory & Storage Patterns\n");
    printf("===================================================\n");
    
    // Create test files
    const char *small_file = "/tmp/test_small.txt";
    const char *medium_file = "/tmp/test_medium.txt";
    
    create_test_file(small_file, 1);      // 1 MB
    create_test_file(medium_file, 10);    // 10 MB
    
    // Benchmarks
    benchmark(small_file, "quick", 100);
    benchmark(medium_file, "lazy", 10);
    
    printf("\n=== Key Insights ===\n");
    printf("1. mmap is ~2-3x faster for large files (zero-copy)\n");
    printf("2. read() copies data twice: kernel → user buffer\n");
    printf("3. mmap maps file into virtual address space\n");
    printf("4. Page faults occur on first access (transparent)\n");
    printf("5. OS caches pages intelligently\n");
    printf("6. MADV_SEQUENTIAL hints help OS prefetch\n");
    printf("\nFor storage systems:\n");
    printf("- mmap is used for efficient large file handling\n");
    printf("- Reduces memory copies (critical for performance)\n");
    printf("- Virtual memory isolation protects data integrity\n");
    
    // Cleanup
    unlink(small_file);
    unlink(medium_file);
    
    return 0;
}
