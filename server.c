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
#include <pthread.h>  // Include pthread library for multithreading

#define SERVERPORT 8989
#define BUFSIZE 4096
#define SOCKETERROR (-1)
#define SERVER_BACKLOG 100
#define THREAD_POOL_SIZE 20

pthread_t thread_pool[THREAD_POOL_SIZE];
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t condition = PTHREAD_COND_INITIALIZER;

typedef struct sockaddr_in SA_IN;
typedef struct sockaddr SA;

int client_socket_queue[THREAD_POOL_SIZE];
int queue_start = 0;
int queue_end = 0;

void * handle_connection(void* p_client_socket);
int check(int exp, const char *msg);
void * thread_function(void *arg);
void add_to_queue(int client_socket);
int get_from_queue();

int main(int argc, char **argv)
{
    int server_socket, client_socket, addr_size;
    SA_IN server_addr, client_addr;

    // Initialize Winsock
    WSADATA wsaData;
    check(WSAStartup(MAKEWORD(2, 2), &wsaData), "WSAStartup failed");

    // Create socket
    check((server_socket = socket(AF_INET, SOCK_STREAM, 0)), "Failed to create socket");

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
    while (true) {
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
    char buffer[BUFSIZE];
    int bytes_received;

     // Receive data from the client
    while ((bytes_received = recv(client_socket, buffer, sizeof(buffer), 0)) > 0) {
        buffer[bytes_received] = '\0';  // Null-terminate the received data

        // Send a valid HTTP response to the client
        const char *response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 30\r\n\r\ntoi la sinh vien Bach Khoa";
        send(client_socket, response, strlen(response), 0);
        break;  // Just handle one request and then close the connection
    }

    // Close the client socket after sending the message
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
