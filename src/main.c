#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>


void *handle_client(void *arg)
{
    int client_fd = *(int *)arg;
    free(arg);

    char buffer[4096];

    ssize_t bytes_received = recv(
        client_fd,
        buffer,
        sizeof(buffer) - 1,
        0
    );

    if(bytes_received == -1){
        perror("recv");
        close(client_fd);
        return NULL;
    }

    if(bytes_received == 0) {
        printf("Client closed the connection.\n");
        close(client_fd);
        return NULL;
    }

    buffer[bytes_received] = '\0';

    printf("Received request from fd %d:\n%s\n", client_fd, buffer);

    const char *response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 13\r\n"
        "Connection: close\r\n"
        "\r\n"
        "Hello from C!";

    
    ssize_t bytes_sent = send(
        client_fd,
        response,
        strlen(response),
        0
    );

    if(bytes_sent == -1){
        perror("send");
        close(client_fd);
        return NULL;
    }

    printf(
        "Response sent to fd %d. %zd bytes sent.\n",
        client_fd,
        bytes_sent
    );

    close(client_fd);

    return NULL;
}


int main(void)
{
    int server_fd;
    int client_fd;

    struct sockaddr_in server_addr = {0};
    struct sockaddr_in client_addr = {0};
    socklen_t client_addr_len = sizeof(client_addr);

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(18080);
    server_addr.sin_addr.s_addr = INADDR_ANY;


    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }

    printf("Socket created successfully. fd = %d\n", server_fd);

    if(bind(
        server_fd,
        (struct sockaddr *)&server_addr,
        sizeof(server_addr)
        ) == -1) 
    {
        perror("bind");
        close(server_fd);
        return EXIT_FAILURE;
    }

    printf("Socket bound successfully to port 18080.\n");

    if (listen(server_fd, 10) == -1) {
        perror("listen");
        close(server_fd);
        return EXIT_FAILURE;
    }

    printf("Server listening on port 18080...\n");


    while(1) 
    {
        client_addr_len = sizeof(client_addr);

        printf("Wating for a client...\n");

        client_fd = accept(
            server_fd,
            (struct sockaddr *)& client_addr,
            &client_addr_len
        );

        if(client_fd == -1){
            perror("accept");
            close(server_fd);
            return EXIT_FAILURE;
        }

        printf("Client connected successfully. Client fd = %d\n", client_fd);

        int *client_fd_ptr = malloc(sizeof(int));

        if(client_fd_ptr == NULL) {
            perror("malloc");
            close(client_fd);
            continue;
        }

        *client_fd_ptr = client_fd;

        pthread_t thread;

        int result = pthread_create(
            &thread,
            NULL,
            handle_client,
            client_fd_ptr
        );

        if(result != 0) {
            fprintf(
                stderr,
                "pthread_create failed: %s\n",
                strerror(result)
            );

            free(client_fd_ptr);
            close(client_fd);
            continue;
        }

        pthread_detach(thread);
    }


    close(server_fd);
    return EXIT_SUCCESS;
}