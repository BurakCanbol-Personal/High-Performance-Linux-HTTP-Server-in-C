#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>

int main(void)
{
    int server_fd;

    struct sockaddr_in server_addr = {0};

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
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
        return EXIT_FAILURE;
    }

    printf("Socket bound successfully to port 8080.\n");

    return EXIT_SUCCESS;
}