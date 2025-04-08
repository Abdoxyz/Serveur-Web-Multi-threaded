#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include "io_helper.h"
#include "request.h"
#include <stdarg.h>

// Global debug flag
int debug_mode = 0;

// Buffer
int *request_buffer;
int buffer_size = 5;
int buffer_count = 0; 

int buffer_start = 0;
int buffer_end = 0;

pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t buffer_not_empty = PTHREAD_COND_INITIALIZER;
pthread_cond_t buffer_not_full = PTHREAD_COND_INITIALIZER;

// Debug functions
void log_debug(const char *format, ...) {
    if (debug_mode) {
        va_list args;
        va_start(args, format);
        vprintf(format, args);  // Print the formatted string
        va_end(args);
    }
}

void print_buffer_state(const char *msg) {
    if (debug_mode) {
        printf("[DEBUG] %s\n", msg);
        printf("[DEBUG] Buffer state: [");
        for (int i = 0; i < buffer_size; i++) {
            if (request_buffer[i] == -1) {
                printf(" _ ");  // Empty slot
            } else {
                printf("%d ", request_buffer[i]);  // Active request
            }
        }
        printf("]\n");
    }
}

// Worker thread function
void *worker_thread(void *arg) {
    int request;

    while (1) {
        pthread_mutex_lock(&buffer_mutex);

        // Wait until the buffer is not empty
        while (buffer_count == 0) {
            log_debug("[DEBUG] Worker waiting: buffer is empty\n");
            pthread_cond_wait(&buffer_not_empty, &buffer_mutex);
        }

        // Find the next request in the buffer
        request = request_buffer[buffer_start % buffer_size];
        buffer_count--;
        request_buffer[buffer_start % buffer_size] = -1;  // Clear the slot
        if (request_buffer[(buffer_start + 1) % buffer_size] != -1) {
            buffer_start = (buffer_start + 1) % buffer_size;
        }

        log_debug("[DEBUG] Worker took request from buffer\n");

        // Signal that the buffer has space
        pthread_cond_signal(&buffer_not_full);
        print_buffer_state("[DEBUG] After worker took a request");

        pthread_mutex_unlock(&buffer_mutex);

        // Handle the connection
        request_handle(request);
        close(request);
    }

    return NULL;
}

void send_http_500(int conn_fd) {
     const char *response = "HTTP/1.1 500 Internal Server Error\r\n"
                           "Content-Type: text/plain\r\n"
                           "Content-Length: 21\r\n"
                           "\r\n"
                           "Server overloaded.\n";
    
    write_or_die(conn_fd, response, strlen(response));
    shutdown(conn_fd, SHUT_WR);
    return;
}

int main(int argc, char *argv[]) {
    int c;
    char *root_dir = ".";
    int port = 10000;
    int num_threads = 4;

    while ((c = getopt(argc, argv, "d:p:t:b:l")) != -1) {
        switch (c) {
        case 'd':
            root_dir = optarg;
            break;
        case 'p':
            port = atoi(optarg);
            break;
        case 't':
            num_threads = atoi(optarg);
            break;
        case 'b':
            buffer_size = atoi(optarg);
            break;
        case 'l':
            debug_mode = 1;  // Enable debug mode
            break;
        default:
            fprintf(stderr, "usage: wserver [-d basedir] [-p port] [-t threads] [-b buffer_size] [-l debug_mode]\n");
            exit(1);
        }
    }

    // Run out of this directory
    chdir_or_die(root_dir);

    // Allocate the buffer dynamically and initialize it
    request_buffer = malloc(buffer_size * sizeof(int));
    if (!request_buffer) {
        perror("malloc");
        exit(1);
    }
    for (int i = 0; i < buffer_size; i++) {
        request_buffer[i] = -1;  // Mark slots as empty
    }

    // Create worker threads
    pthread_t threads[num_threads];
    for (int i = 0; i < num_threads; i++) {
        pthread_create(&threads[i], NULL, worker_thread, NULL);
    }

    // Main server loop
    int listen_fd = open_listen_fd_or_die(port);
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int conn_fd = accept_or_die(listen_fd, (struct sockaddr *)&client_addr, &client_len);

        pthread_mutex_lock(&buffer_mutex);

        // Check to see if buffer_end end slot is empty
        if (request_buffer[buffer_end % buffer_size] == -1) {
            request_buffer[buffer_end % buffer_size] = conn_fd;
            log_debug("[DEBUG] Added request to buffer (slot %d)\n", buffer_end % buffer_size);
            pthread_cond_signal(&buffer_not_empty);
            print_buffer_state("[DEBUG] After adding a request");
            buffer_count++;
        } else if (buffer_count < buffer_size) {
            request_buffer[(buffer_end + 1) % buffer_size] = conn_fd;
            buffer_end = (buffer_end + 1) % buffer_size;
            log_debug("[DEBUG] Added request to buffer (slot %d)\n", buffer_end % buffer_size);
            pthread_cond_signal(&buffer_not_empty);
            print_buffer_state("[DEBUG] After adding a request");
            buffer_count++;
        } else {
            log_debug("[DEBUG] Buffer full: dropping connection\n");
            send_http_500(conn_fd);
            close(conn_fd);
        }

        pthread_mutex_unlock(&buffer_mutex);
    }

    // Wait for all threads to finish
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    free(request_buffer);
    return 0;
}
