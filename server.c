/*This file implements a simple chat server using C and POSIX sockets.
  This program is to be deployed as a server, not as a client.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define PORT 5000
#define BUFF_SIZE 1024
#define OUT_BUFF_SIZE (BUFF_SIZE + 128)
#define MAX_CLIENTS 10

typedef struct{
    int fd;
    int joined;
    char username[64];
    struct sockaddr_in addr;
} Client;

Client clients[MAX_CLIENTS];
int listen_fd;

// Function for initalizing client array.
void init_clients(void){
    for (int i =0; i < MAX_CLIENTS; i++){
        clients[i].fd = -1;
        clients[i].joined = 0;
        clients[i].username[0] = '\0';
    }
}

// Function to add a new client, returns index or -1 if full
int add_client(int fd, struct sockaddr_in *addr){
    for (int i =0; i < MAX_CLIENTS; i++){
        if (clients[i].fd == -1){
            clients[i].fd = fd;
            clients[i].addr = *addr;
            clients[i].joined = 0;
            clients[i].username[0] = '\0';
            return i;
        }
    }
    return -1;
}

// Function for removing a client
void remove_client(int idx){
    if (clients[idx].fd != -1){
        close(clients[idx].fd);
        clients[idx].fd = -1;
        clients[idx].joined = 0;
        clients[idx].username[0] = '\0';
    }
}

// Function for broadcasting a single clients message to all connected clients
void broadcast_message(int sender_idx, const char *msg){
    for (int i =0; i < MAX_CLIENTS; i++){
        if (i != sender_idx && clients[i].fd != -1 && clients[i].joined){
            send(clients[i].fd, msg, strlen(msg), 0);
        }
    }
}

int main(void) {
    struct sockaddr_in server_addr;
    fd_set readfds;
    int maxfd;
    char buffer[BUFF_SIZE];

    // Create listening socket
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    //Bind to 0.0.0.0:PORT
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(PORT);

    if (bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    //Listen
    if (listen(listen_fd, 5) == -1) {
        perror("listen");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    printf("Chat server listening on port %d...\n", PORT);

    //Init clients
    init_clients();

    //Main loop with select()
    while (1) {
        FD_ZERO(&readfds);
        FD_SET(listen_fd, &readfds);
        maxfd = listen_fd;

        // add all active client fds
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].fd != -1) {
                FD_SET(clients[i].fd, &readfds);
                if (clients[i].fd > maxfd) {
                    maxfd = clients[i].fd;
                }
            }
        }

        int ready = select(maxfd + 1, &readfds, NULL, NULL, NULL);
        if (ready == -1) {
            perror("select");
            break;
        }

        //New incoming connection?
        if (FD_ISSET(listen_fd, &readfds)) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
            if (client_fd == -1) {
                perror("accept");
            } else {
                char ipstr[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &client_addr.sin_addr, ipstr, sizeof(ipstr));
                printf("New connection from %s:%d (fd=%d)\n",
                       ipstr, ntohs(client_addr.sin_port), client_fd);

                int idx = add_client(client_fd, &client_addr);
                if (idx == -1) {
                    printf("Max clients reached, rejecting.\n");
                    close(client_fd);
                } else {
                    const char *welcome = "Welcome!\n";
                    send(client_fd, welcome, strlen(welcome), 0);
                }
            }
            if (--ready <= 0) continue;
        }

        //Check all clients for data
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].fd == -1) continue;
            int fd = clients[i].fd;

            if (FD_ISSET(fd, &readfds)) {
                ssize_t n = recv(fd, buffer, BUFF_SIZE - 1, 0);
                if (n <= 0) {
                    if (n == 0) {
                        printf("Client ");
                        if (clients[i].joined) printf("%s ", clients[i].username);
                        printf("(fd=%d) disconnected.\n", fd);
                    } else {
                        perror("recv");
                    }
                    remove_client(i);
                } else {
                    buffer[n] = '\0';

                    //If not joined yet, treat first line as JOIN
                    if (!clients[i].joined) {
                        if (strncmp(buffer, "JOIN ", 5) == 0) {
                            strncpy(clients[i].username, buffer + 5,
                                    sizeof(clients[i].username) - 1);
                            clients[i].username[sizeof(clients[i].username) - 1] = '\0';

                            //strip trailing newline
                            size_t ulen = strlen(clients[i].username);
                            if (ulen > 0 && clients[i].username[ulen - 1] == '\n') {
                                clients[i].username[ulen - 1] = '\0';
                            }

                            clients[i].joined = 1;
                            printf("Client fd=%d joined as '%s'\n",
                                   fd, clients[i].username);

                            //notify others
                            char join_msg[BUFF_SIZE];
                            snprintf(join_msg, sizeof(join_msg),
                                     "[server] %s has joined the chat.\n",
                                     clients[i].username);
                            broadcast_message(i, join_msg);

                        } else {
                            printf("Client fd=%d did not send JOIN, closing.\n", fd);
                            remove_client(i);
                        }
                    } else {
                        //Normal chat message
                        printf("[%s] %s", clients[i].username, buffer);

                        //Build broadcast line: "[user] msg"
                        char out[OUT_BUFF_SIZE];
                        snprintf(out, sizeof(out), "[%s] %s", clients[i].username, buffer);
                        broadcast_message(i, out);
                    }
                }

                if (--ready <= 0) break; // no more fds ready this round
            }
        }
    }

    close(listen_fd);
    return 0;
}
