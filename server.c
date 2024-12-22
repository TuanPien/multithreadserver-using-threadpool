//sử dụng winsock thay cho sys/socket vì Window

#include <winsock2.h>
#include <windows.h>    
#define socklen_t int  
#define close closesocket  

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>

#define SERVERPORT 8989
#define BUFSIZE 4096
#define SOCKETERROR (-1)
#define SERVER_BACKLOG 100
#define THREAD_POOL_SIZE 20
#define MAX_REQUEST_SIZE 8192
#define HTTP_OK "200 OK"
#define HTTP_BAD_REQUEST "400 Bad Request"
#define HTTP_NOT_FOUND "404 Not Found"
#define HTTP_METHOD_NOT_ALLOWED "405 Method Not Allowed"

pthread_t thread_pool[THREAD_POOL_SIZE];
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t condition = PTHREAD_COND_INITIALIZER;

typedef struct sockaddr_in SA_IN;
typedef struct sockaddr SA;

int client_socket_queue[THREAD_POOL_SIZE];
int queue_start = 0;
int queue_end = 0;

volatile sig_atomic_t server_running = 1;
FILE *log_file = NULL;

typedef struct {
    char method[16];
    char path[256];
    char version[16];
} http_request_t;

void log_message(const char *format, ...) {
    time_t now;
    time(&now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));

    va_list args;
    va_start(args, format);

    if (log_file) {
        fprintf(log_file, "[%s] ", timestamp);
        vfprintf(log_file, format, args);
        fprintf(log_file, "\n");
        fflush(log_file);
    }
    
    printf("[%s] ", timestamp);
    vprintf(format, args);
    printf("\n");
    
    va_end(args);
}

void signal_handler(int sig) {
    server_running = 0;
}

int parse_http_request(const char *buffer, http_request_t *request) {
    return sscanf(buffer, "%15s %255s %15s", request->method, request->path, request->version) == 3;
}

void * handle_connection(void* p_client_socket);
int check(int exp, const char *msg);
void * thread_function(void *arg);
void add_to_queue(int client_socket);
int get_from_queue();

int main(int argc, char **argv)
{
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    log_file = fopen("server.log", "a");
    if (!log_file) {
        fprintf(stderr, "Failed to open log file\n");
        return 1;
    }

    int server_socket, client_socket, addr_size;
    SA_IN server_addr, client_addr;

    // Initialize Winsock
    WSADATA wsaData;
    check(WSAStartup(MAKEWORD(2, 2), &wsaData), "WSAStartup failed");

    // Create socket
    check((server_socket = socket(AF_INET, SOCK_STREAM, 0)), "Failed to create socket");

    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt));

    // Initialize the address struct
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVERPORT);

    // Bind the socket
    check(bind(server_socket, (SA*)&server_addr, sizeof(server_addr)), "bind failed");

    // Listen on the socket
    check(listen(server_socket, SERVER_BACKLOG), "Listen failed");

    // Initialize the thread pool
    for (int i = 0; i < THREAD_POOL_SIZE; i++) {
        pthread_create(&thread_pool[i], NULL, thread_function, NULL);
    }

    printf("Server started on port %d\n", SERVERPORT);

    // Accept client connections and add them to the queue
    while (server_running) {
        addr_size = sizeof(client_addr);
        client_socket = accept(server_socket, (SA*)&client_addr, &addr_size);

        if (client_socket == SOCKETERROR) {
            printf("Client connection failed: %d\n", WSAGetLastError());
            continue;
        }

        // Add the new client socket to the queue
        add_to_queue(client_socket);
    }

    // Cleanup
    for (int i = 0; i < THREAD_POOL_SIZE; i++) {
        pthread_cancel(thread_pool[i]);
    }
    
    if (log_file) {
        fclose(log_file);
    }
    
    closesocket(server_socket);
    WSACleanup();
    return 0;
}

// Helper function to check return values and print error messages
int check(int exp, const char *msg) {
    if (exp == SOCKETERROR) {
        fprintf(stderr, "%s: %d\n", msg, WSAGetLastError());
        exit(EXIT_FAILURE);
    }
    return exp;
}

// Function to handle the communication with the client
void *handle_connection(void *p_client_socket) {
    int client_socket = *(int*)p_client_socket;
    char buffer[MAX_REQUEST_SIZE];
    int bytes_received;
    http_request_t request;
    
    // Receive data from the client
    bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received < 0) {
        log_message("Error receiving data: %d", WSAGetLastError());
        closesocket(client_socket);
        return NULL;
    }
    
    buffer[bytes_received] = '\0';
    
    // Parse HTTP request
    if (!parse_http_request(buffer, &request)) {
        const char *error_response = "HTTP/1.1 400 Bad Request\r\n"
                                   "Content-Type: text/plain\r\n"
                                   "Content-Length: 11\r\n\r\n"
                                   "Bad Request";
        send(client_socket, error_response, strlen(error_response), 0);
        closesocket(client_socket);
        return NULL;
    }

    // Log the request
    log_message("Received %s request for %s", request.method, request.path);

    // Handle different HTTP methods
    const char *content;
    const char *status;
    
    if (strcmp(request.method, "GET") == 0) {
        content = "toi la sinh vien Bach Khoa";
        status = HTTP_OK;
    } else if (strcmp(request.method, "OPTIONS") == 0) {
        content = "";
        status = HTTP_OK;
    } else {
        content = "Method Not Allowed";
        status = HTTP_METHOD_NOT_ALLOWED;
    }

    int content_length = strlen(content);
    
    // Create the full HTTP response with proper headers
    char response[BUFSIZE];
    snprintf(response, sizeof(response),
        "HTTP/1.1 %s\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n"
        "Content-Length: %d\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Connection: close\r\n"
        "Server: BachKhoa-Server/1.0\r\n"
        "\r\n"
        "%s", status, content_length, content);

    // Send the response
    int total_sent = 0;
    int response_len = strlen(response);
    
    while (total_sent < response_len) {
        int sent = send(client_socket, response + total_sent, 
                       response_len - total_sent, 0);
        if (sent < 0) {
            printf("Error sending response: %d\n", WSAGetLastError());
            break;
        }
        total_sent += sent;
    }
    
    log_message("Response sent to client");
    closesocket(client_socket);
    return NULL;
}

// Thread function to handle client connections
void *thread_function(void *arg) {
    while (true) {
        // Wait until a client socket is available in the queue
        int client_socket = get_from_queue();
        
        // Handle the client connection in a new thread
        handle_connection(&client_socket);
    }
}

// Add a client socket to the queue
void add_to_queue(int client_socket) {
    pthread_mutex_lock(&mutex);

    // Wait if the queue is full
    while ((queue_end + 1) % THREAD_POOL_SIZE == queue_start) {
        pthread_cond_wait(&condition, &mutex);
    }

    client_socket_queue[queue_end] = client_socket;
    queue_end = (queue_end + 1) % THREAD_POOL_SIZE;

    pthread_cond_signal(&condition);
    pthread_mutex_unlock(&mutex);
}

// Get a client socket from the queue
int get_from_queue() {
    pthread_mutex_lock(&mutex);

    // Wait if the queue is empty
    while (queue_start == queue_end) {
        pthread_cond_wait(&condition, &mutex);
    }

    int client_socket = client_socket_queue[queue_start];
    queue_start = (queue_start + 1) % THREAD_POOL_SIZE;

    pthread_cond_signal(&condition);
    pthread_mutex_unlock(&mutex);

    return client_socket;
}
