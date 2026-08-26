#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/epoll.h>


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

    event.events = EPOLLIN;
    event.data.fd = server_fd;

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
            int current_fd = events[i].data.fd;

            if(current_fd == server_fd)
            {
                printf("Incoming connection ready.\n");
                client_addr_len = sizeof(client_addr);


                client_fd = accept(
                    server_fd,
                    (struct sockaddr *)&client_addr,
                    &client_addr_len
                );

                if(client_fd == -1){

                    if (errno == EAGAIN || errno == EWOULDBLOCK) continue;

                    perror("accept");
                    continue;
                }

                printf("Client accepted. fd = %d\n", client_fd);

                if(set_nonblocking(client_fd) == -1){
                    perror("set_nonblocking client");
                    close(client_fd);
                    continue;
                }

                struct epoll_event client_event = {0};

                client_event.events = EPOLLIN;
                client_event.data.fd = client_fd;

                if(epoll_ctl(
                        epoll_fd,
                        EPOLL_CTL_ADD,
                        client_fd,
                        &client_event
                    ) == -1) 
                {

                        perror("epoll_ctl client");
                        close(client_fd);
                        continue;
                }

                printf("Client fd %d added to epoll.\n", client_fd);
            }
            else{
                
                char buffer[4096];

                ssize_t bytes_received = recv(
                    current_fd,
                    buffer,
                    sizeof(buffer) - 1,
                    0
                );

                if(bytes_received == -1) 
                {
                    if(errno == EAGAIN || errno == EWOULDBLOCK) continue;
                

                    perror("recv");

                    epoll_ctl(
                        epoll_fd,
                        EPOLL_CTL_DEL,
                        current_fd,
                        NULL
                    );

                    close(current_fd);
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
                    continue;
                }

                buffer[bytes_received] = '\0';

                printf("Received request from fd %d:\n%s\n", current_fd, buffer);

                const char *response =
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: text/plain\r\n"
                        "Content-Length: 13\r\n"
                        "Connection: close\r\n"
                        "\r\n"
                        "Hello from C!";

                ssize_t bytes_sent = send(
                        current_fd,
                        response,
                        strlen(response),
                        0
                );

                if (bytes_sent == -1)
                {
                    if (errno != EAGAIN && errno != EWOULDBLOCK)
                    {
                        perror("send");
                    }
                }
                else
                {
                    printf(
                        "Response sent to fd %d. %zd bytes sent.\n",
                        current_fd,
                        bytes_sent
                    );
                }

                epoll_ctl(
                    epoll_fd,
                    EPOLL_CTL_DEL,
                    current_fd,
                    NULL
                );

                close(current_fd);

            }
        }
    }


    close(server_fd);
    return EXIT_SUCCESS;
}