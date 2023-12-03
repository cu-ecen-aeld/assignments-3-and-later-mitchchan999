#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <syslog.h>
#include <stdbool.h>
#include <poll.h>
#include <pthread.h>

#define PORT "9000"  // the port users will be connecting to

#define EXIT_FAILURE -1 // return code for error

#define TMPFILE "/var/tmp/aesdsocketdata" // temp file to read and write to

static bool KILL_SIG = false;

// handle sigint
void sigint_handler(int s)
{
    syslog(LOG_INFO, "Caught signal, exiting");
    KILL_SIG = true;
}

// handle sigterm
void sigterm_handler(int s)
{
    syslog(LOG_INFO, "Caught signal, exiting");
    KILL_SIG = true;
}

// handle child process exit
void sigchld_handler(int s)
{
    // waitpid() might overwrite errno, so we save and restore it:
    int saved_errno = errno;

    while(waitpid(-1, NULL, WNOHANG) > 0);

    errno = saved_errno;
}


// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(int argc, char *argv[])
{
    bool daemon = false;
    if ( argc >= 2 ) {
        if (strcmp(argv[1],"-d") == 0){
            daemon = true;
        }
    }

    openlog("aesdsocket", LOG_PID, LOG_USER);

    int sockfd, new_fd;  // listen on sock_fd, new connection on new_fd
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr; // connector's address information
    socklen_t sin_size;
    struct sigaction sa;
    int yes=1;
    char s[INET6_ADDRSTRLEN];
    int rv;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP

    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return EXIT_FAILURE;
    }

    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
                sizeof(int)) == -1) {
            perror("setsockopt");
            return EXIT_FAILURE;
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("server: bind");
            continue;
        }

        break;
    }

    freeaddrinfo(servinfo); // all done with this structure

    if (p == NULL)  {
        fprintf(stderr, "server: failed to bind\n");
        return EXIT_FAILURE;
    }

    if (listen(sockfd, 10) == -1) {
        perror("listen");
        return EXIT_FAILURE;
    }

    if (daemon)
    {
        int pid = fork();
        switch (pid) {
        case -1:
            perror("fork");
            exit(EXIT_FAILURE);
        case 0:
            printf("Forking child\n");
            break;
        default:
            exit(EXIT_SUCCESS);
        }
    }

    // Gracefully exit when SIGINT or SIGTERM is received

     // handle sigint
    sa.sa_handler = sigint_handler; 
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    // handle sigterm
    sa.sa_handler = sigterm_handler; 
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    // handle child process exit
    sa.sa_handler = sigchld_handler; 
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    printf("Waiting for connection\n");

    struct pollfd  *pfds;
    nfds_t nfds = 1;
    pfds = calloc(nfds, sizeof(struct pollfd));
    pfds[0].fd = sockfd;
    pfds[0].events = POLLIN;

    while(!KILL_SIG) { 

        // poll until the socket has data to read
        if (poll(pfds, POLLIN, 100) <= 0){
            continue;
        }

        // accept the connection
        sin_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (new_fd == -1) {
            perror("accept");
            continue;
        }

        // get information about the connection
        inet_ntop(their_addr.ss_family,
            get_in_addr((struct sockaddr *)&their_addr),
            s, sizeof s);
        printf("Accepted connection from %s\n", s);
        syslog(LOG_INFO, "Accepted connection from %s\n", s);

        // create a child process to handle the connection
        if (!fork()) {

            // child doesn't need the listener
            close(sockfd); 

            FILE* file;
            const int BUFFER_SIZE = 1024;
            char* str = calloc(sizeof(char), BUFFER_SIZE);
            char* token;
            char* strptr;
            char* saveptr;

            // I don't know why but Windows style line feeds are being sent when I run the tests
            char delim[] = "\r\n"; 

            // wait for data to read
            ssize_t bytesreceived;
            while( (bytesreceived = recv(new_fd, str, BUFFER_SIZE-1, MSG_DONTWAIT | MSG_PEEK)) == 0 ) {
                printf("Waiting\n");
            }

            // read from socket and write to file
            if ((file = fopen(TMPFILE, "a")))
            {
                printf("Receiving\n");
                while( (bytesreceived = recv(new_fd, str, BUFFER_SIZE-1, MSG_DONTWAIT)) > 0 ) {
                    printf("%ld bytes received\n", bytesreceived);
                    strptr = str;
                    while (true){
                        // str either has "\r\n", "\n", or not
                        token = strtok_r(strptr, delim, &saveptr);
                        strptr = NULL;
                        if (token != NULL){
                            if ((saveptr-str) == BUFFER_SIZE-1){
                                if ((rv = fprintf(file, "%s", token)) < 0){
                                    perror("fprintf");
                                }
                                // nothing more to process
                                break;
                            } else {
                                if ((rv = fprintf(file, "%s\n", token)) < 0){
                                    perror("fprintf");
                                }
                                // process next token
                            }
                        } else {
                            // nothing more to process
                            break;
                        }
                    }
                    // re-zero the buffer so string processing works
                    memset(str, 0, BUFFER_SIZE);
                }
                printf("Done receiving\n\n");
                fclose(file);
            } else {
                perror("fopen for write");
            }

             // read from file and write to socket
            if ((file = fopen(TMPFILE, "r")))
            {
                printf("Sending:\n");
                size_t bytessent;
                while( (bytessent = fread(str, 1, BUFFER_SIZE-1, file)) > 0 ) {
                    printf("sending %ld bytes:\n", bytessent);
                    if ((rv = send(new_fd, str, bytessent, MSG_NOSIGNAL)) < 0){
                        perror("send");
                    }
                }
                printf("Done sending\n\n");
                fclose(file);
            } else {
                perror("fopen for read");
            }

            fsync(new_fd);

            // close the connection descriptor
            printf("Closed connection from %s\n", s);
            syslog(LOG_INFO, "Closed connection from %s\n", s);
            close(new_fd);

            // clean up 
            free(str);

            // exit the child
            exit(0);
        }

        // parent doesn't need connection descriptor
        close(new_fd);  
    }

    // wait for all children to exit
    while(wait(NULL) > 0);

    // delete file
    if ((rv = remove(TMPFILE)) != 0){
        perror("remove");
    }

    // clean up
    close(sockfd); 
    free(pfds);

    // say goodbye
    printf("goodbye\n");
    return 0;
}

/*
int main(int argc, char *argv[])
{



    FILE *file;
    if ((file = fopen(argv[1], "w")))
    {
        syslog(LOG_DEBUG, "Writing %s to %s\n", argv[1], argv[2]);
        fprintf(file, "%s", argv[2]);
        fclose(file);
        return 0;
    }

    syslog(LOG_ERR, "Error opening file %s\n", argv[1]);
    return 1;
}
*/
