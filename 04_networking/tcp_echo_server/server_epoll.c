#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <poll.h>
#include <signal.h>
#include <errno.h>
#include <stdint.h>

#define PORT 8889
#define BACKLOG 128
#define BUFFER_SIZE 4096
#define MAX_CLIENTS 10000

typedef struct {
    int fd;
    char *buffer;
    size_t buffer_len;
    size_t buffer_capacity;
} client_t;

volatile int running = 1;

void signal_handler(int sig) {
    (void)sig;
    printf("\nShutting down...\n");
    running = 0;
}

int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main(void) {
    signal(SIGINT, signal_handler);
    
    // Create listening socket
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }
    
    // Allow reuse
    int reuse = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    
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
    
    // Set nonblocking
    if (set_nonblocking(listen_fd) < 0) {
        perror("fcntl");
        close(listen_fd);
        return 1;
    }
    
    printf("Event-Driven Echo Server (poll) listening on port %d\n", PORT);
    printf("Press Ctrl+C to stop\n");
    printf("(Note: Using poll() for macOS compatibility, epoll() on Linux)\n");
    
    // Poll file descriptors
    struct pollfd *fds = malloc(MAX_CLIENTS * sizeof(struct pollfd));
    client_t **clients = malloc(MAX_CLIENTS * sizeof(client_t *));
    memset(clients, 0, MAX_CLIENTS * sizeof(client_t *));
    
    int nfds = 1;  // Start with listen_fd
    fds[0].fd = listen_fd;
    fds[0].events = POLLIN;
    
    int active_clients = 0;
    
    // Event loop
    while (running) {
        int poll_ret = poll(fds, nfds, 1000);  // 1s timeout
        
        if (poll_ret < 0) {
            if (errno == EINTR) continue;
            perror("poll");
            break;
        }
        
        // Check listen socket first
        if (fds[0].revents & POLLIN) {
            // Accept new connections
            while (1) {
                struct sockaddr_in client_addr;
                socklen_t addr_len = sizeof(client_addr);
                
                int client_fd = accept(listen_fd, 
                                      (struct sockaddr *)&client_addr, &addr_len);
                if (client_fd < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    }
                    perror("accept");
                    break;
                }
                
                // Set nonblocking
                if (set_nonblocking(client_fd) < 0) {
                    close(client_fd);
                    continue;
                }
                
                // Add to polling list
                if (nfds < MAX_CLIENTS) {
                    fds[nfds].fd = client_fd;
                    fds[nfds].events = POLLIN;
                    
                    client_t *client = malloc(sizeof(client_t));
                    client->fd = client_fd;
                    client->buffer = malloc(BUFFER_SIZE);
                    client->buffer_len = 0;
                    client->buffer_capacity = BUFFER_SIZE;
                    
                    clients[nfds] = client;
                    nfds++;
                    
                    active_clients++;
                    if (active_clients % 100 == 0) {
                        printf("Active clients: %d\n", active_clients);
                    }
                } else {
                    // Server full
                    close(client_fd);
                }
            }
        }
        
        // Check client sockets
        for (int i = 1; i < nfds; i++) {
            if (fds[i].fd < 0) continue;
            
            client_t *client = clients[i];
            if (!client) continue;
            
            if (fds[i].revents & POLLIN) {
                // Read available
                ssize_t n = read(fds[i].fd, 
                                client->buffer + client->buffer_len,
                                client->buffer_capacity - client->buffer_len);
                
                if (n > 0) {
                    client->buffer_len += n;
                } else {
                    // Connection closed or error
                    close(fds[i].fd);
                    free(client->buffer);
                    free(client);
                    clients[i] = NULL;
                    fds[i].fd = -1;
                    active_clients--;
                    continue;
                }
            }
            
            if (fds[i].revents & (POLLHUP | POLLERR)) {
                // Connection error
                close(fds[i].fd);
                free(client->buffer);
                free(client);
                clients[i] = NULL;
                fds[i].fd = -1;
                active_clients--;
            } else if (client->buffer_len > 0) {
                // Echo back
                ssize_t written = write(fds[i].fd, client->buffer, 
                                       client->buffer_len);
                
                if (written > 0) {
                    if (written == (ssize_t)client->buffer_len) {
                        client->buffer_len = 0;
                    } else {
                        memmove(client->buffer, client->buffer + written,
                               client->buffer_len - written);
                        client->buffer_len -= written;
                    }
                }
            }
        }
    }
    
    // Cleanup
    for (int i = 1; i < nfds; i++) {
        if (fds[i].fd >= 0) {
            close(fds[i].fd);
            if (clients[i]) {
                free(clients[i]->buffer);
                free(clients[i]);
            }
        }
    }
    
    free(fds);
    free(clients);
    close(listen_fd);
    
    printf("Final active clients: %d\n", active_clients);
    return 0;
}
