/**
 * @file    server.c
 * @author  Md Shuaib Khan 
 * @brief   A TCP echo server 
 *          i,e single threaded ,
 *              non blocking ,
 *              event-driven / event loop ,
 *              binary protocol 
 *          that runs on UNIX like environment
 * @version 0.1
 * @date    2024-01-01
 * @copyright Copyright (c) 2024
 */

/**
 * standard libary
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
/**
 * libary  
 * - to set up an address information
 * - to open a socket 
 * - to bind an address to that socket
 * - to listen for incoming connection to that socket
 * and other related function's and MACROS
 */
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>
/**
 * libary 
 * - to monitor the socket file descriptor
 * - to make the socket file descriptor a non blocking socket
 */
#include <sys/select.h>
#include <fcntl.h>

#define PORT "3040"
#define BACKLOG 10
#define MAX_RETRY_ATTEMPTS 2
/**
 * global BUFFER to store the
 * recived data from the client
 */
void *buffer = NULL;
ssize_t buffer_size = 1024;
int try = 0;

/**
 * setup_server_address()
 * - Configures address information (ai) such as IP and port for the socket (endpoint).
 * - Sets up the address based on specified hints (struct addrinfo).
 * - Resolves the address information in the result (struct addrinfo).
 * - The result (res) contains the head pointer of the linked list (struct addrinfo).
 * - Uses getaddrinfo() to set up the address information (ai).
 * 
 * Returns:
 * - result (struct addrinfo) containing the configured address information , if successfull.
 * - NULL , if fails to setup the ai.

 */
struct addrinfo* setup_server_address(){
    struct addrinfo hints;
    hints.ai_flags = AI_PASSIVE;     //wildcard address
    hints.ai_family = AF_INET;       //ipv4
    hints.ai_socktype = SOCK_STREAM; //stream socket / tcp socket
    hints.ai_protocol = 0;           //any protocol
    
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
            if(try < MAX_RETRY_ATTEMPTS){
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

/**
 * set_nonblocking()
 * - Sets up the passed socket file descriptor to operate in non-blocking mode.
 * - Non-blocking mode prevents I/O functions like send() or recv() from blocking
 *   when there's no data to receive or insufficient space to send.
 * - Instead of blocking, these functions return an error code (EWOULDBLOCK / EAGAIN).
 * - The same non-blocking behavior applies to accept() as well.
 * 
 * Details:
 * - Retrieves the current flags (F_GETFL) of the sock_fd.
 * - Performs bitwise OR (|) operation to align a specific bit (O_NONBLOCK) on the flags.
 * - Sets the modified flags (F_SETFL) back to the sock_fd.
 * 
 * Returns:
 * - 1 if the sock_fd is successfully set to non-blocking mode.
 * - 0 if unable to set the sock_fd to non-blocking mode.
 */
int set_nonblocking(const int sock_fd){
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

char* resize_mem(const uint16_t size){
    void *new_buffer = realloc(buffer,size);
    if(new_buffer == NULL){
        void *temp_buffer = calloc(1,size);
        if(temp_buffer == NULL){
            return NULL;
        }
        memcpy(temp_buffer,buffer,buffer_size-1);
        free(buffer);
        return temp_buffer;
    }
    return new_buffer;
}

int data_transmission_in_binary(int client_sockfd){
    char flag = 1;
    memset(buffer,0,buffer_size);

    /*--- recive case ---*/
    uint16_t recv_pack_size = sizeof(uint16_t);
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
        if(recv_tracker >= (buffer_size - 1)){
            void *new_buffer = resize_mem(recv_pack_size);
            if(new_buffer == NULL){
                return 1;
            }
            buffer = new_buffer;
        }
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
                case ECONNRESET:
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

/**
 * init_server()
 * This function sets up and initializes the server by creating a socket
 * and binding it to a specific address and port. It prepares the socket
 * to listen for incoming connections.
 * 
 * Details:
 * - Uses setup_server_address() to get address information.
 * - Iterates through available addresses and attempts to bind to one.
 * - Sets socket options to handle address reuse (SO_REUSEADDR).
 * - Sets the socket to non-blocking mode for efficient handling.
 * - Listens for incoming connections with a specified backlog.
 * 
 * Returns:
 * - On successful initialization, returns the socket file descriptor.
 * - If an error occurs during setup, returns -1.
 */
int init_server(){
    struct addrinfo *addr = setup_server_address();
    if(addr == NULL){
        return -1;
    }
    
    int sock_fd = -1;

    struct addrinfo *temp = addr;
    while(temp != NULL){
        // loop through the avilable address and try to bind to one of the address     
        if(temp->ai_family == AF_INET){
            sock_fd = socket(temp->ai_family,
                    temp->ai_socktype,
                    temp->ai_protocol);
            if(sock_fd == -1){
                temp = temp->ai_next;
                continue;
            }

            int yes = 1; //to resolve EADDRINUSE error in bind()
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
    printf("Successfully init the server...\n\n");
    
    label_fai:
    freeaddrinfo(addr);

    if(sock_fd < 0){
        return -1;
    }
    return sock_fd;
}

/**
 * ev_lp()
 * - Manages server concurrency using I/O multiplexing via select().
 * - Sets up the listening socket (listen_sockfd) for monitoring incoming connections.
 * - Monitors events like incoming connections, data arrival, and EOF using select().
 * - Upon select() return:
 *   - Checks if the event is on the listening socket:
 *     - Accepts the incoming connection using accept().
 *     - Sets the accepted socket to be non-blocking if successful; otherwise, shuts down the connection.
 *     - Adds the accepted socket to the master set for monitoring data arrival.
 *   - If the event is on a connected client:
 *     - Initiates data transmission using data_transmission_in_binary().
 *     - Clears the socket from the master set if data transmission returns 1 (indicating termination).
 * - Continues monitoring events in an infinite loop.
 *
 * Params:
 * - listen_sockfd: The socket file descriptor being monitored for incoming connections.
 *
 * Returns:
 * - -1 if an error occurs during select().
 */
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
                //check for new incoming conection
                if(fd == listen_sockfd){
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
                    printf("A client with %d is connected\n",client_sockfd);
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
                    if(data_transmission_in_binary(fd)){
                        printf("A client with %d is terminated\n",fd);
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

/**
 * handler()
 * since our server is long running process
 * i,e a daemon process 
 * the only way to terminate this process 
 * is to interrupt by using crtl+C ,
 * kill the process by using kill process
 * TODO finished explaining this
 */
void handler(int signum){
    if(signum == SIGINT || signum == SIGTERM ){
        printf("CAUGHT YOU!!\n\n");
        free(buffer);
        exit(EXIT_SUCCESS);
    }
}

int main(void){
    struct sigaction sa;
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT,&sa,NULL);
    sigaction(SIGTERM,&sa,NULL);

    buffer = calloc(1,buffer_size);
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
