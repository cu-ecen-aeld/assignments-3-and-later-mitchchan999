#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <syslog.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <pthread.h>
#include <stdbool.h>
#include <time.h> 
#include "queue.h"


#define buffer_size 1024

void cleanup(int exit_code);
void sig_handler(int signo);
void *timestamp(void *arg);
void *connection(void *arg);

int sockfd, client_sockfd, datafd, signal_exit = 0;

typedef struct client_info
{
    int client_sockfd;
    char client_ip[INET_ADDRSTRLEN]; 
} client_info_t;

struct thread_info_t {
    bool notification;
    pthread_t thread_id;
    client_info_t client_data;
    SLIST_ENTRY(thread_info_t) entries;
};

SLIST_HEAD(thread_list_t, thread_info_t) thread_list;

pthread_mutex_t aesddata_file_mutex = PTHREAD_MUTEX_INITIALIZER;

int main(int argc, char *argv[]) {

    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
		pid_t pid, sid;

		pid = fork();

		if (pid < 0) {
			syslog(LOG_ERR, "ERROR: Failed to fork");
			cleanup(EXIT_FAILURE);
		}

		if (pid != 0) {
			exit(EXIT_SUCCESS);
		}

		sid = setsid();
		if (sid < 0) {
			syslog(LOG_ERR, "ERROR: Failed to setsid");
			cleanup(EXIT_FAILURE);
    	}

		close(STDIN_FILENO);
		close(STDOUT_FILENO);
		close(STDERR_FILENO);
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    SLIST_INIT(&thread_list);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        syslog(LOG_ERR, "Failed to create socket");
        cleanup(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(9000);

    if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        syslog(LOG_ERR, "ERROR: Failed to bind");
        cleanup(EXIT_FAILURE);
    }

    close(-1);
    
    if (listen(sockfd, 5) == -1) {
        syslog(LOG_ERR, "ERROR: Failed to listen");
        close(sockfd);
        return -1;
    }

    char *aesddata_file = "/var/tmp/aesdsocketdata";
    datafd = open(aesddata_file, O_CREAT | O_RDWR | O_APPEND, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (datafd == -1){
        syslog(LOG_ERR, "ERROR: Failed to create file - %s", aesddata_file);
        exit(EXIT_FAILURE);
    }

    pthread_t timestamp_thread;
    if (pthread_create(&timestamp_thread, NULL, timestamp, NULL) != 0) {
        syslog(LOG_ERR, "ERROR: Failed to create timestamp thread!");
        cleanup(EXIT_FAILURE);
    }

    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    
    while (1) {
        client_sockfd = accept(sockfd, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_sockfd == -1) {
            syslog(LOG_WARNING, "Failed to accept connection");
            continue;
        }

        struct thread_info_t *new_thread = malloc(sizeof(struct thread_info_t));
        if (new_thread == NULL) {
            syslog(LOG_ERR, "ERROR: Failed to malloc");
            cleanup(EXIT_FAILURE);
        }

        inet_ntop(AF_INET, &(client_addr.sin_addr), new_thread->client_data.client_ip, INET_ADDRSTRLEN);
        syslog(LOG_INFO, "Accepted connection from %s", new_thread->client_data.client_ip);

        new_thread->client_data.client_sockfd = client_sockfd;
        new_thread->notification = 0;

        if (pthread_create(&new_thread->thread_id, NULL, connection, (void *)new_thread) != 0) {
            syslog(LOG_ERR, "ERROR: Failed to create thread!");
            cleanup(EXIT_FAILURE);
        }
        else {
            SLIST_INSERT_HEAD(&thread_list, new_thread, entries);
        }

        struct thread_info_t *thread, *thread_tmp;
        SLIST_FOREACH_SAFE(thread, &thread_list, entries, thread_tmp) {
            if (thread->notification == 1) {
                syslog(LOG_INFO, "main - joining thread %ld\n", thread->thread_id);
                if (pthread_join(thread->thread_id, NULL) != 0) {
                    syslog(LOG_ERR, "main - error joining thread!");
                    cleanup(EXIT_FAILURE);
                }
                SLIST_REMOVE(&thread_list, thread, thread_info_t, entries);
                free(thread);
            }
        }
    }
    return 0;
}

void cleanup(int exit_code) {

    syslog(LOG_INFO, "performing cleanup");
    signal_exit = 1;

    struct thread_info_t *thread;
    while (!SLIST_EMPTY(&thread_list)) {
        thread = SLIST_FIRST(&thread_list);
        SLIST_REMOVE_HEAD(&thread_list, entries);
        syslog(LOG_INFO, "cleanup - joining thread %ld", thread->thread_id);
        if (pthread_join(thread->thread_id, NULL) != 0) {
            syslog(LOG_ERR, "cleanup - error joining thread!");
            exit(EXIT_FAILURE);
        }
        free(thread);
    }

    if (sockfd >= 0) close(sockfd);

    if (datafd >= 0) close(datafd);

    remove("/var/tmp/aesdsocketdata");

    closelog();

    exit(exit_code);
}

void sig_handler(int signo) {
   if (signo == SIGINT || signo == SIGTERM) {
       syslog(LOG_INFO, "Caught signal, exiting");
       cleanup(EXIT_SUCCESS);
   }
}

void *connection(void *arg)
{
    struct thread_info_t *thread_info = (struct thread_info_t *)arg;
    client_info_t client_data = thread_info->client_data;

    char* buffer = (char *)malloc(buffer_size * sizeof(char));
    if (buffer == NULL) {
        syslog(LOG_ERR, "ERROR: Failed to malloc");
        cleanup(EXIT_FAILURE);
    }
    memset(buffer, 0, buffer_size * sizeof(char));
    ssize_t recv_size;

    while ((recv_size = recv(client_data.client_sockfd, buffer, buffer_size, 0)) > 0) {
        if (pthread_mutex_lock(&aesddata_file_mutex) != 0) {
            syslog(LOG_ERR, "ERROR: Failed to acquire mutex!");
            cleanup(EXIT_FAILURE);
        }
        if (write(datafd, buffer, recv_size) == -1) {
            syslog(LOG_ERR, "ERROR: Failed to write to file");
            cleanup(EXIT_FAILURE);
        }
        if (pthread_mutex_unlock(&aesddata_file_mutex) != 0) {
            syslog(LOG_ERR, "ERROR: Failed to release mutex!");
            cleanup(EXIT_FAILURE);
        }

        if (memchr(buffer, '\n', buffer_size) != NULL) {
            lseek(datafd, 0, SEEK_SET);
            int bytes_read = read(datafd, buffer, buffer_size);
            if (bytes_read == -1) {
                syslog(LOG_ERR, "ERROR: Failed to read from file");
                cleanup(EXIT_FAILURE);
            }
            while (bytes_read > 0) {
                send(client_data.client_sockfd, buffer, bytes_read, 0);
                bytes_read = read(datafd, buffer, buffer_size); 
            }
        }
        memset(buffer, 0, buffer_size * sizeof(char));
    }

    free(buffer);

    syslog(LOG_INFO, "Closed connection from %s", client_data.client_ip);
    close(client_data.client_sockfd);

    thread_info->notification = 1;
    return NULL;
}

void *timestamp(void *arg) {
    while (!signal_exit) {
        time_t current_time = time(NULL);
        struct tm *time_info = localtime(&current_time);

        char timestamp[100];
        strftime(timestamp, sizeof(timestamp), "timestamp: %a, %d %b %Y %H:%M:%S %z\n", time_info);

        if (pthread_mutex_lock(&aesddata_file_mutex) != 0) {
            syslog(LOG_ERR, "ERROR: Failed to acquire mutex!");
            cleanup(EXIT_FAILURE);
        }
        write(datafd, timestamp, strlen(timestamp));
        if (pthread_mutex_unlock(&aesddata_file_mutex) != 0) {
            syslog(LOG_ERR, "ERROR: Failed to release mutex!");
            cleanup(EXIT_FAILURE);
        }

        sleep(10);
    }

    return NULL;
}

