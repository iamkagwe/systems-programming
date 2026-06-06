#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>

/*
 * Signal Handlers: Graceful Shutdown Demo
 *
 * Demonstrates:
 *   1. Signal handling (SIGINT, SIGTERM)
 *   2. Graceful shutdown (cleanup + resource draining)
 *   3. Signal masking (critical sections)
 *   4. Atomic state management
 *   5. Connection draining patterns
 *
 * Story:
 *   - Start TCP server accepting connections
 *   - Press Ctrl+C (SIGINT) or send SIGTERM
 *   - Server stops accepting NEW connections
 *   - Waits for existing clients to finish (graceful drain)
 *   - Cleans up all resources
 *   - Exit cleanly
 */

typedef struct {
    volatile int shutdown_requested;  // 1 = shutdown in progress
    volatile int active_connections;  // Count of active client threads
    pthread_mutex_t mutex;
    pthread_cond_t cond_drain;  // Signal when all connections drained
} ServerState;

static ServerState server_state = {
    .shutdown_requested = 0,
    .active_connections = 0,
};

static int server_fd = -1;
static volatile int server_running = 1;

/*
 * SIGNAL HANDLER: The function called when signal arrives
 *
 * IMPORTANT: Signal handlers have strict rules:
 *   - Use only async-signal-safe functions (write, exit, signal, etc.)
 *   - Don't call printf, malloc, pthread_*, etc. (NOT async-safe!)
 *   - Keep it SHORT and simple
 *   - Set flags (volatile) that main code checks
 *
 * We just set flags; main loop checks them
 */
void signal_handler(int sig) {
    if (sig == SIGINT) {
        // Ctrl+C: graceful shutdown
        server_running = 0;
    } else if (sig == SIGTERM) {
        // Terminate signal: graceful shutdown
        server_running = 0;
    }
}

/*
 * Increment active connection count
 * Called when client thread starts
 * Signal masking: protect this counter from signals
 */
void increment_connections(void) {
    sigset_t oldset, newset;
    
    // Block all signals during critical section
    sigfillset(&newset);
    pthread_sigmask(SIG_BLOCK, &newset, &oldset);
    
    // Safe to modify now
    pthread_mutex_lock(&server_state.mutex);
    server_state.active_connections++;
    pthread_mutex_unlock(&server_state.mutex);
    
    // Restore previous signal mask
    pthread_sigmask(SIG_SETMASK, &oldset, NULL);
}

/*
 * Decrement active connection count
 * Called when client thread exits
 * If shutdown requested and last connection: signal drain complete
 */
void decrement_connections(void) {
    sigset_t oldset, newset;
    
    // Block all signals during critical section
    sigfillset(&newset);
    pthread_sigmask(SIG_BLOCK, &newset, &oldset);
    
    // Safe to modify now
    pthread_mutex_lock(&server_state.mutex);
    server_state.active_connections--;
    
    // If shutdown requested and all connections drained: signal main thread
    if (server_state.shutdown_requested && server_state.active_connections == 0) {
        pthread_cond_signal(&server_state.cond_drain);
    }
    
    pthread_mutex_unlock(&server_state.mutex);
    
    // Restore previous signal mask
    pthread_sigmask(SIG_SETMASK, &oldset, NULL);
}

/*
 * Handle client connection
 * Each client connection runs in its own thread
 */
void* handle_client(void *arg) {
    int client_fd = (intptr_t)arg;
    increment_connections();
    
    printf("[%ld] Client connected\n", (long)pthread_self());
    
    // Simple echo protocol
    char buffer[1024];
    ssize_t bytes_read;
    
    while ((bytes_read = read(client_fd, buffer, sizeof(buffer))) > 0) {
        // Echo back to client
        write(client_fd, buffer, bytes_read);
        
        // Optional: print activity
        printf("[%ld] Echoed %zd bytes\n", (long)pthread_self(), bytes_read);
    }
    
    printf("[%ld] Client disconnected\n", (long)pthread_self());
    close(client_fd);
    decrement_connections();
    
    return NULL;
}

/*
 * Main server loop
 * Accepts incoming connections
 * Checks signal flags for graceful shutdown
 */
int main(int argc, char *argv[]) {
    int port = 9999;
    if (argc > 1) {
        port = atoi(argv[1]);
    }
    
    printf("Signal Handlers: Graceful Shutdown Demo\n");
    printf("========================================\n");
    printf("Listening on port %d\n", port);
    printf("Press Ctrl+C (SIGINT) or send SIGTERM for graceful shutdown\n\n");
    
    // Initialize server state
    pthread_mutex_init(&server_state.mutex, NULL);
    pthread_cond_init(&server_state.cond_drain, NULL);
    
    /*
     * SIGNAL SETUP
     *
     * Important: signals should be handled in main thread, not worker threads
     * Use pthread_sigmask to block signals in worker threads
     * Use sigaction or signal() to install handler
     */
    
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    
    // Install handlers for both SIGINT and SIGTERM
    if (sigaction(SIGINT, &sa, NULL) < 0 || sigaction(SIGTERM, &sa, NULL) < 0) {
        perror("sigaction");
        return 1;
    }
    
    // Prevent broken pipe errors when writing to closed sockets
    signal(SIGPIPE, SIG_IGN);
    
    /*
     * CREATE SERVER SOCKET
     */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }
    
    // Allow reuse of address (important for rapid restart)
    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        perror("setsockopt");
        return 1;
    }
    
    // Bind to port
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }
    
    // Listen for connections
    if (listen(server_fd, 10) < 0) {
        perror("listen");
        return 1;
    }
    
    printf("Waiting for connections...\n\n");
    
    /*
     * MAIN SERVER LOOP
     *
     * Pattern:
     *   1. Accept connection (non-blocking check for signals)
     *   2. Check if shutdown requested
     *   3. If shutdown: stop accepting, drain existing
     *   4. Otherwise: spawn thread for client
     */
    
    while (server_running) {
        // Set read timeout to allow signal checking every second
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        setsockopt(server_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        
        if (client_fd < 0) {
            // accept() timeout (expected, for signal checking)
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            perror("accept");
            break;
        }
        
        // Got a connection - create thread for it
        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, (void *)(intptr_t)client_fd) != 0) {
            perror("pthread_create");
            close(client_fd);
            continue;
        }
        
        // Detach thread (don't need to join)
        pthread_detach(tid);
    }
    
    /*
     * SHUTDOWN PHASE
     *
     * 1. Set shutdown flag
     * 2. Stop accepting new connections
     * 3. Wait for existing connections to drain
     * 4. Clean up resources
     */
    
    printf("\n--- Shutdown initiated ---\n");
    printf("Stopping new connections...\n");
    
    pthread_mutex_lock(&server_state.mutex);
    server_state.shutdown_requested = 1;
    pthread_mutex_unlock(&server_state.mutex);
    
    // Close server socket (stop accepting)
    close(server_fd);
    server_fd = -1;
    
    // Wait for all active connections to drain
    printf("Waiting for %d active connection(s) to finish...\n", 
           server_state.active_connections);
    
    pthread_mutex_lock(&server_state.mutex);
    while (server_state.active_connections > 0) {
        printf("  Still %d connection(s) active, waiting...\n", 
               server_state.active_connections);
        pthread_cond_wait(&server_state.cond_drain, &server_state.mutex);
    }
    pthread_mutex_unlock(&server_state.mutex);
    
    printf("All connections drained.\n");
    printf("Cleaning up resources...\n");
    
    // Cleanup
    pthread_mutex_destroy(&server_state.mutex);
    pthread_cond_destroy(&server_state.cond_drain);
    
    printf("✓ Graceful shutdown complete\n");
    
    return 0;
}
