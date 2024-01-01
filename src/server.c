#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/select.h>
#include <fcntl.h>

#include <errno.h>

#define PORT "3040"
#define BACKLOG 10
int try = 0;
char *buffer = NULL;
struct addrinfo* setup_server_address(){
    struct addrinfo hints;
    hints.ai_flags = AI_PASSIVE; //wildcard address
    hints.ai_family = AF_INET; //ipv4
    hints.ai_socktype = SOCK_STREAM; //stream socket / tcp socket
    hints.ai_protocol = 0; //any protocol
    
    struct addrinfo *res;
 
    label_try_gai_again:
    int gai_stat = getaddrinfo(
        NULL,
        PORT,
        &hints,
        &res
    );
    if(gai_stat != 0){
        if(gai_stat == EAI_AGAIN){
            if(try < 2){
                try++;
                goto label_try_gai_again; 
            }
        }
        printf("%s\n",gai_strerror(gai_stat));
        try = 0;
        return NULL;
    }
    
    try = 0;
    return res;
}

int set_nonblocking(int sock_fd){
    int flags = fcntl(sock_fd,
            F_GETFL,
            0);
    if(flags == -1){
        perror("FCNTL_GT ");
        return 0;
    }

    flags |= O_NONBLOCK;

    int fcntl_stat = fcntl(sock_fd,
            F_SETFL,
            flags);
    if(fcntl_stat == -1){
        perror("FCNTL_ST ");
        return 0;
    }
    return 1;
}
int data_transmission_in_binary(int client_sockfd){
    char flag = 1;
    
    /*
     *buffer allocated initially
     *buffer globally avilable
     *--- recive case ---*/
    memset(buffer,0,sizeof(uint16_t)+2);
    uint16_t recv_pack_size = sizeof(uint16_t) + 2;
    ssize_t recv_tracker = 0;
    do{
        ssize_t bytes_recv = recv(client_sockfd,
               buffer+recv_tracker,
               recv_pack_size-recv_tracker,
               0);
        if(bytes_recv == 0){
            //EOF
            return 1;
        }else if(bytes_recv >= sizeof(uint16_t) && flag){
            recv_pack_size = 0;
            memcpy(&recv_pack_size,buffer,sizeof(uint16_t));
            recv_pack_size = ntohs(recv_pack_size);
            buffer = realloc(buffer,recv_pack_size);
            if(buffer == NULL){
                //have a backup plan here
            }
            flag = 0;
        }else if(bytes_recv == -1){
            //do some error handling
            switch(errno){
                case EWOULDBLOCK || EAGAIN:
                    if(recv_tracker < recv_pack_size){
                        continue;
                    }
                default:
                    continue;
            }
        }
        recv_tracker += bytes_recv;
    }while(recv_tracker < recv_pack_size);
    
    /*
     * --- send case ---*/
    ssize_t send_tracker = 0;
    while(send_tracker < recv_pack_size){
        ssize_t bytes_send = send(client_sockfd,
                buffer+send_tracker,
                recv_pack_size-send_tracker,
                0);
        if(bytes_send == -1){
            switch(errno){
                case EPIPE:
                    return 1;
                default:
                    if(send_tracker < recv_pack_size){
                        continue;
                    }
            }
        }
        recv_tracker += bytes_send;
    }
    return 0;
}

int init_server(){
    struct addrinfo *addr = setup_server_address();
    if(addr == NULL){
        return -1;
    }
    
    int sock_fd = -1;

    struct addrinfo *temp = addr;
    while(temp != NULL){
        // TODO loop through the avilable address and try to bind to one of the address     
        if(temp->ai_family == AF_INET){
            sock_fd = socket(temp->ai_family,
                    temp->ai_socktype,
                    temp->ai_protocol);
            if(sock_fd == -1){
                temp = temp->ai_next;
                continue;
            }

            int yes = 1; //to resolve address in use /EADDRINUSE 
            setsockopt(sock_fd,
                    SOL_SOCKET,
                    SO_REUSEADDR,
                    &yes,
                    sizeof(int));

            int bind_stat = bind(sock_fd,
                    temp->ai_addr,
                    temp->ai_addrlen);
            if(bind_stat == -1){
                perror("BIND ");
                close(sock_fd);
                sock_fd = -1;
                temp = temp->ai_next;
                continue;
            }   
            break;
        }
        temp = temp->ai_next;
    }
    if(temp == NULL){
        //error in binding
        goto label_fai;
    }
    if(!set_nonblocking(sock_fd)){
        close(sock_fd);
        sock_fd = -1;
        goto label_fai;
    }

    int listen_stat = listen(sock_fd,
            BACKLOG);
    if(listen_stat == -1){
        close(sock_fd);
        sock_fd = -1;
        printf("Failed to init the server\n");
    }
    printf("Successfully init the server...\n");
    
    label_fai:
    freeaddrinfo(addr);

    if(sock_fd < 0){
        return -1;
    }
    return sock_fd;
}

int ev_lp(int listen_sockfd){
    fd_set r_set;
    fd_set master_set;
    int max_fd;

    FD_ZERO(&master_set);

    FD_SET(listen_sockfd,&master_set);
    max_fd = listen_sockfd;

    //event loop
    for(;;){
        r_set = master_set;

        int select_stat = select(
                    max_fd + 1,
                    &r_set,
                    NULL,
                    NULL,
                    NULL
                );
        if(select_stat == -1){
            return -1;
        }
        
        for(int fd = 0 ; fd <= max_fd ; fd++){
            if(FD_ISSET(fd,&r_set)){
                //check new incoming conection
                if(fd == listen_sockfd){
                    //TODO accept the new connection 
                    //from the completed connection queue
                    //and set it to the master_set 
                    //and update the max_fd
                    //if the new fd is greater than
                    struct sockaddr_storage client_addr; //can store either IPv4 or IPv6
                    socklen_t client_addr_len = sizeof client_addr;

                    int client_sockfd = accept(listen_sockfd,
                            (struct sockaddr*)&client_addr,
                            &client_addr_len);
                    if(client_sockfd == -1){
                        switch(errno){
                            case EWOULDBLOCK || EAGAIN:
                                continue;
                            default:
                                continue;
                        }
                    }
                   
                    if(!set_nonblocking(client_sockfd)){
                        shutdown(client_sockfd,SHUT_RDWR);
                        continue;
                    }

                    FD_SET(client_sockfd,&master_set);
                    if(client_sockfd > max_fd){
                        max_fd = client_sockfd;
                    }
                }
                //check data arrival , FIN,
                //RST on the connected client
                else{
                    //TODO exchange some data in binary format
                    if(data_transmission_in_binary(fd)){
                        close(fd);
                        FD_CLR(fd,&master_set);
                        if(fd == max_fd){
                            max_fd--;
                        }
                        continue;
                    }
                }
            }
        }
    }
}


int main(void){
    buffer = (char*) calloc(sizeof(uint16_t),2); 
    if(buffer == NULL){
        return -1;
    }
    int listen_sockfd = init_server();

    if(listen_sockfd == -1){
        printf("Failed in initiating a server\n");
        return -1;
    }

    int evlp_stat = ev_lp(listen_sockfd);
    
    label_close_listenfd:
    close(listen_sockfd);
    return 0;
}
