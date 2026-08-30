#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/epoll.h>

#define BUFFER_SIZE 8192

struct client_state {
    int fd;
    
    char request[BUFFER_SIZE];
    size_t request_len;

    const char *response;
    size_t response_len;
    size_t response_sent;
};


int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);

    if(flags == -1) {
        return -1;
    }

    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
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

    if(set_nonblocking(server_fd) == -1) {
        perror("set_nonblocking");
        close(server_fd);
        return EXIT_FAILURE;
    }

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

    int epoll_fd = epoll_create1(0);

    if(epoll_fd == -1) {
        perror("epoll_create1");
        close(server_fd);
        return EXIT_FAILURE;
    }

    struct epoll_event event = {0};

    event.events = EPOLLIN | EPOLLET;
    event.data.ptr = NULL;

    if(epoll_ctl(
        epoll_fd,
        EPOLL_CTL_ADD,
        server_fd,
        &event
                ) == -1) {

        perror("epoll_ctl");
        close(epoll_fd);
        close(server_fd);
        return EXIT_FAILURE;
    }


    struct epoll_event events[64];

    while(1) 
    {
        int event_count = epoll_wait(
            epoll_fd,
            events,
            64,
            -1
        );

        if(event_count == -1){
            perror("epoll_wait");
            break;
        }

        printf("%d event(s) ready.\n", event_count);

        for(int i = 0; i < event_count; i++)
        {
            struct client_state *client = events[i].data.ptr;

            if(client == NULL)
            {
                printf("Incoming connection ready.\n");

                while(1) 
                {
                    client_addr_len = sizeof(client_addr);

                    client_fd = accept(
                        server_fd,
                        (struct sockaddr *)&client_addr,
                        &client_addr_len
                    );

                    if(client_fd == -1)
                    {
                        if(errno == EAGAIN || errno == EWOULDBLOCK) break;

                        perror("accept");
                        break;
                    }

                    printf("Client accepted. fd = %d\n", client_fd);

                    if(set_nonblocking(client_fd) == -1)
                    {
                        perror("set_nonblocking client");
                        close(client_fd);
                        continue;
                    }

                    struct client_state *new_client = calloc(
                        1,
                        sizeof(struct client_state)
                    );

                    if(new_client == NULL)
                    {
                        perror("calloc");
                        close(client_fd);
                        continue;
                    }

                    new_client->fd = client_fd;

                    struct epoll_event client_event = {0};

                    client_event.events = EPOLLIN | EPOLLET;
                    client_event.data.ptr = new_client;

                    if(epoll_ctl(
                            epoll_fd,
                            EPOLL_CTL_ADD,
                            client_fd,
                            &client_event
                            ) == -1)
                    {
                        perror("epoll_ctl client");
                        free(new_client);
                        close(client_fd);
                        continue;
                    }

                    printf("Client fd %d added to epoll.\n", client_fd);
                }
                
            }
            else{
                
                //char buffer[4096];

                int current_fd = client->fd;

                if(events[i].events & EPOLLIN)
                {
                    while(1)
                    {
                        ssize_t bytes_received = recv(
                            current_fd,
                            client->request + client->request_len,
                            BUFFER_SIZE - client->request_len - 1,
                            0
                        );

                        if(bytes_received > 0) 
                        {
                            client->request_len += bytes_received;
                            client->request[client->request_len] = '\0';

                            if(strstr(client->request, "\r\n\r\n") != NULL) 
                            {
                                printf("Client fd %d sent a complete request:\n%s\n", current_fd, client->request);
                            
                                client->response =
                                        "HTTP/1.1 200 OK\r\n"
                                        "Content-Type: text/plain\r\n"
                                        "Content-Length: 13\r\n"
                                        "Connection: close\r\n"
                                        "\r\n"
                                        "Hello from C!";

                                client->response_len = strlen(client->response);
                                client->response_sent = 0;

                                struct epoll_event write_event = {0};

                                write_event.events = EPOLLOUT | EPOLLET;
                                write_event.data.ptr = client;

                                if(epoll_ctl(
                                        epoll_fd,
                                        EPOLL_CTL_MOD,
                                        current_fd,
                                        &write_event
                                ) == -1)
                                {
                                    perror("epoll_ctl MOD (write)");
                                }

                                break;
                            }

                            printf("fd %d: received %zd bytes, total = %zu\n", current_fd, bytes_received, client->request_len);
                            continue;
                        }

                        if(bytes_received == 0)
                        {
                            printf("Client fd %d disconnected.\n", current_fd);

                            epoll_ctl(
                                epoll_fd,
                                EPOLL_CTL_DEL,
                                current_fd,
                                NULL
                            );

                            close(current_fd);
                            free(client);
                            break;
                        }

                        if(errno == EAGAIN || errno == EWOULDBLOCK) break;

                        perror("recv");

                        epoll_ctl(
                            epoll_fd,
                            EPOLL_CTL_DEL,
                            current_fd,
                            NULL
                        );

                        close(current_fd);
                        free(client);

                        break;

                    }
                }
                else if(events[i].events & EPOLLOUT)
                {
                    while(client->response_sent < client->response_len)
                    {
                        ssize_t bytes_sent = send(
                            current_fd,
                            client->response + client->response_sent,
                            client->response_len - client->response_sent,
                            0
                        );

                        if(bytes_sent > 0)
                        {
                            client->response_sent += bytes_sent;

                            printf(
                                "fd %d: sent %zd bytes, total %zu/%zu\n",
                                current_fd,
                                bytes_sent,
                                client->response_sent,
                                client->response_len
                            );

                            continue;
                        }

                        if(bytes_sent == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;

                        perror("send");

                        epoll_ctl(
                            epoll_fd,
                            EPOLL_CTL_DEL,
                            current_fd,
                            NULL
                        );

                        close(client_fd);
                        free(client);

                        break;
                    }

                    if(client->response_sent == client->response_len)
                    {
                        printf("Response completely sent to fd %d.\n", current_fd);

                        epoll_ctl(
                            epoll_fd,
                            EPOLL_CTL_DEL,
                            current_fd,
                            NULL
                        );

                        close(client_fd);
                        free(client);

                    }
                }
            }
        }
    }


    close(server_fd);
    return EXIT_SUCCESS;
}