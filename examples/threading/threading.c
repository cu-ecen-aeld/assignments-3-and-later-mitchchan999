#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

// Optional: use these functions to add debug or error prints to your application
//#define DEBUG_LOG(msg,...)
#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{
    struct thread_data* thread_func_args = (struct thread_data *) thread_param;

    const int ms_to_ns = 1000000;

    DEBUG_LOG("Starting thread to sleep %d ms, lock mutex, sleep %d ms, then unlock", 
              thread_func_args->wait_to_obtain_ms, 
              thread_func_args->wait_to_release_ms);

    struct timespec wait_to_obtain;
    wait_to_obtain.tv_sec = 0;
    wait_to_obtain.tv_nsec = ms_to_ns*thread_func_args->wait_to_obtain_ms;

    struct timespec wait_to_release;
    wait_to_release.tv_sec = 0;
    wait_to_release.tv_nsec = ms_to_ns*thread_func_args->wait_to_release_ms;

    struct timespec remainder;
    int ret = nanosleep(&wait_to_obtain, &remainder);
    if (ret != 0)
    {
        ERROR_LOG("Failed to perform initial sleep");
        thread_func_args->thread_complete_success = false;
        return thread_param;
    }

    DEBUG_LOG("First sleep done");

    ret = pthread_mutex_lock(thread_func_args->mutex);
    if (ret != 0)
    {
        ERROR_LOG("Failed to lock mutex");
        thread_func_args->thread_complete_success = false;
        return thread_param;
    }

    DEBUG_LOG("Locked");

    ret = nanosleep(&wait_to_release, &remainder);
    if (ret != 0)
    {
        ERROR_LOG("Failed to perform second sleep");
        thread_func_args->thread_complete_success = false;
        return thread_param;
    }

    DEBUG_LOG("Second sleep done");

    ret = pthread_mutex_unlock(thread_func_args->mutex);
    if (ret != 0)
    {
        ERROR_LOG("Failed to unlock mutex");
        thread_func_args->thread_complete_success = false;
        return thread_param;
    }

    DEBUG_LOG("Unlocked");

    thread_func_args->thread_complete_success = true;
    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    struct thread_data* td = malloc(sizeof(struct thread_data));
    td->mutex = mutex;
    td->wait_to_obtain_ms = wait_to_obtain_ms;
    td->wait_to_release_ms = wait_to_release_ms;
    td->thread_complete_success = false;

    int ret = pthread_create(thread, NULL, &threadfunc, td);
    if (ret != 0)
    {
        ERROR_LOG("Failed to create pthread");
        return false;
    }

    return true;
}

