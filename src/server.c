#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>

#include <errno.h>

#define PORT "3040"
#define BACKLOG 10
int try = 0;

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
                    (struct sockaddr*)&temp->ai_addr,
                    (socklen_t)temp->ai_addrlen);
            if(bind_stat == -1){
                close(sock_fd);
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

    int listen_stat = listen(sock_fd,
            BACKLOG);
    if(listen_stat == -1){
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
                    r_set,
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
                if(i == listen_sockfd){
                    //TODO accept the new connection 
                    //from the completed connection queue
                    //and set it to the master_set 
                    //and update the max_fd
                    //if the new fd is greater than
                }
                //check data arrival , FIN,
                //RST on the connected client
                else{
                    //TODO exchange some data in binary format
                }
            }
        }
    }
}


int main(void){
    int listen_sockfd = init_server();
    printf("%d\n",listen_sockfd);

    if(listen_sockfd == -1){
        close(passive_sock_fd);
    }
    
    int evlp_stat = ev_lp(listen_sockfd);
    return 0;
}
