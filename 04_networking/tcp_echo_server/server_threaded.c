#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <stdint.h>

#define PORT 8888
#define BACKLOG 128
#define BUFFER_SIZE 4096

volatile int running = 1;
static pthread_mutex_t client_count_lock = PTHREAD_MUTEX_INITIALIZER;
static int active_clients = 0;

void signal_handler(int sig) {
    (void)sig;  // unused
    printf("\nShutting down...\n");
    running = 0;
}

void *handle_client(void *arg) {
    int client_fd = (intptr_t)arg;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    
    while (running && (bytes_read = read(client_fd, buffer, BUFFER_SIZE)) > 0) {
        // Echo back
        ssize_t bytes_written = 0;
        while (bytes_written < bytes_read) {
            ssize_t n = write(client_fd, buffer + bytes_written, 
                             bytes_read - bytes_written);
            if (n <= 0) {
                break;
            }
            bytes_written += n;
        }
    }
    
    close(client_fd);
    
    pthread_mutex_lock(&client_count_lock);
    active_clients--;
    pthread_mutex_unlock(&client_count_lock);
    
    return NULL;
}

int main(void) {
    signal(SIGINT, signal_handler);
    
    // Create listening socket
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }
    
    // Allow reuse of address
    int reuse = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        perror("setsockopt");
        close(listen_fd);
        return 1;
    }
    
    // Bind
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(PORT);
    
    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(listen_fd);
        return 1;
    }
    
    // Listen
    if (listen(listen_fd, BACKLOG) < 0) {
        perror("listen");
        close(listen_fd);
        return 1;
    }
    
    printf("Threaded Echo Server listening on port %d\n", PORT);
    printf("Press Ctrl+C to stop\n");
    
    // Accept loop
    while (running) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        
        int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, 
                              &client_addr_len);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;  // Interrupted by signal
            }
            perror("accept");
            continue;
        }
        
        // Create thread for this client
        pthread_t thread;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        
        if (pthread_create(&thread, &attr, handle_client, 
                          (void *)(intptr_t)client_fd) != 0) {
            perror("pthread_create");
            close(client_fd);
            continue;
        }
        
        pthread_attr_destroy(&attr);
        
        pthread_mutex_lock(&client_count_lock);
        active_clients++;
        if (active_clients % 10 == 0) {
            printf("Active clients: %d\n", active_clients);
        }
        pthread_mutex_unlock(&client_count_lock);
    }
    
    // Cleanup
    close(listen_fd);
    
    // Wait a bit for clients to disconnect
    sleep(1);
    
    pthread_mutex_lock(&client_count_lock);
    printf("Final client count: %d\n", active_clients);
    pthread_mutex_unlock(&client_count_lock);
    
    return 0;
}
