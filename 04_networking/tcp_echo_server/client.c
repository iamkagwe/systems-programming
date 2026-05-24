#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <time.h>
#include <stdatomic.h>

#define PORT 8888
#define HOST "127.0.0.1"

typedef struct {
    int client_id;
    int num_messages;
    int message_size;
} client_args_t;

static atomic_int messages_sent = 0;
static atomic_int messages_received = 0;
static atomic_int errors = 0;

double get_time_seconds() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

void *client_worker(void *arg) {
    client_args_t *args = (client_args_t *)arg;
    
    // Create socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        atomic_fetch_add(&errors, 1);
        free(args);
        return NULL;
    }
    
    // Connect
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(HOST);
    addr.sin_port = htons(PORT);
    
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        atomic_fetch_add(&errors, 1);
        close(sock);
        free(args);
        return NULL;
    }
    
    // Send/receive messages
    char *send_buf = malloc(args->message_size);
    char *recv_buf = malloc(args->message_size);
    
    memset(send_buf, 'A' + (args->client_id % 26), args->message_size);
    
    for (int i = 0; i < args->num_messages; i++) {
        // Send
        ssize_t n = write(sock, send_buf, args->message_size);
        if (n != args->message_size) {
            atomic_fetch_add(&errors, 1);
            break;
        }
        atomic_fetch_add(&messages_sent, 1);
        
        // Receive (echo back)
        ssize_t total = 0;
        while (total < args->message_size) {
            n = read(sock, recv_buf + total, args->message_size - total);
            if (n <= 0) {
                atomic_fetch_add(&errors, 1);
                break;
            }
            total += n;
        }
        
        if (total == args->message_size) {
            atomic_fetch_add(&messages_received, 1);
        }
    }
    
    close(sock);
    free(send_buf);
    free(recv_buf);
    free(args);
    
    return NULL;
}

void benchmark(int num_clients, int messages_per_client, int message_size) {
    printf("\n=== Benchmark: %d clients, %d messages, %d bytes ===\n",
           num_clients, messages_per_client, message_size);
    
    atomic_store(&messages_sent, 0);
    atomic_store(&messages_received, 0);
    atomic_store(&errors, 0);
    
    pthread_t *threads = malloc(num_clients * sizeof(pthread_t));
    
    double start = get_time_seconds();
    
    // Spawn clients
    for (int i = 0; i < num_clients; i++) {
        client_args_t *args = malloc(sizeof(client_args_t));
        args->client_id = i;
        args->num_messages = messages_per_client;
        args->message_size = message_size;
        
        if (pthread_create(&threads[i], NULL, client_worker, args) != 0) {
            perror("pthread_create");
            free(args);
        }
    }
    
    // Wait for all
    for (int i = 0; i < num_clients; i++) {
        pthread_join(threads[i], NULL);
    }
    
    double elapsed = get_time_seconds() - start;
    
    int total_msgs = atomic_load(&messages_received);
    printf("Messages sent:     %d\n", atomic_load(&messages_sent));
    printf("Messages received: %d\n", total_msgs);
    printf("Errors:            %d\n", atomic_load(&errors));
    printf("Time:              %.3f seconds\n", elapsed);
    printf("Throughput:        %.0f msgs/sec\n", total_msgs / elapsed);
    printf("Latency:           %.3f ms\n", (elapsed * 1000) / (total_msgs + 1));
    
    free(threads);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Simple TCP Echo Client for Benchmarking\n");
        printf("Usage: %s <connections>\n", argv[0]);
        printf("  connections: number of concurrent clients (default: 10)\n");
        printf("\nExample benchmark scenarios:\n");
        printf("  %s 10   (small load)\n", argv[0]);
        printf("  %s 100  (medium load)\n", argv[0]);
        printf("  %s 500  (high load)\n", argv[0]);
        return 0;
    }
    
    int num_clients = atoi(argv[1]);
    if (num_clients <= 0 || num_clients > 10000) {
        printf("Invalid number of clients (must be 1-10000)\n");
        return 1;
    }
    
    printf("TCP Echo Client Benchmark\n");
    printf("Connecting to %s:%d\n", HOST, PORT);
    
    // Give server time to start
    sleep(1);
    
    // Run benchmarks with different loads
    benchmark(num_clients, 100, 64);
    benchmark(num_clients, 100, 1024);
    benchmark(num_clients, 50, 4096);
    
    printf("\nDone!\n");
    return 0;
}
