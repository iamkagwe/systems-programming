#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

/*
 * Simple client to test the signal server
 * Connects and sends/receives messages
 */

void* client_thread(void *arg) {
    int client_id = (intptr_t)arg;
    
    // Connect to server
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return NULL;
    }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(9999);
    
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(fd);
        return NULL;
    }
    
    printf("Client %d: Connected\n", client_id);
    
    // Send and receive messages
    for (int i = 0; i < 5; i++) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Client %d message %d", client_id, i);
        
        if (write(fd, msg, strlen(msg)) < 0) {
            perror("write");
            break;
        }
        
        char buffer[256];
        ssize_t n = read(fd, buffer, sizeof(buffer));
        if (n > 0) {
            buffer[n] = '\0';
            printf("Client %d: Received: %s\n", client_id, buffer);
        }
        
        sleep(1);
    }
    
    close(fd);
    printf("Client %d: Disconnected\n", client_id);
    
    return NULL;
}

int main(int argc, char *argv[]) {
    int num_clients = 2;
    if (argc > 1) {
        num_clients = atoi(argv[1]);
    }
    
    printf("Signal Server Test Client\n");
    printf("==========================\n");
    printf("Spawning %d client(s)...\n\n", num_clients);
    
    pthread_t *threads = (pthread_t *)malloc(sizeof(pthread_t) * num_clients);
    
    for (int i = 0; i < num_clients; i++) {
        if (pthread_create(&threads[i], NULL, client_thread, (void *)(intptr_t)i) != 0) {
            perror("pthread_create");
            continue;
        }
    }
    
    // Wait for all clients to finish
    for (int i = 0; i < num_clients; i++) {
        pthread_join(threads[i], NULL);
    }
    
    printf("\nAll clients finished\n");
    free(threads);
    
    return 0;
}
