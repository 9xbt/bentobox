#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

/* Test data passed to threads */
typedef struct {
    int thread_id;
    int iterations;
} thread_data_t;

/* Shared counter for mutex test */
static int shared_counter = 0;
static pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Condition variable test */
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t cond_mutex = PTHREAD_MUTEX_INITIALIZER;
static int ready = 0;

/* Basic thread function */
void* basic_thread(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;
    printf("Thread %d: Starting\n", data->thread_id);
    
    for (int i = 0; i < data->iterations; i++) {
        printf("Thread %d: iteration %d\n", data->thread_id, i);
        usleep(100000); /* 100ms */
    }
    
    printf("Thread %d: Exiting\n", data->thread_id);
    return (void*)(long)data->thread_id;
}

/* Thread that increments shared counter with mutex */
void* mutex_thread(void* arg) {
    int id = *(int*)arg;
    
    for (int i = 0; i < 1000; i++) {
        pthread_mutex_lock(&counter_mutex);
        shared_counter++;
        pthread_mutex_unlock(&counter_mutex);
    }
    
    printf("Thread %d: Done incrementing\n", id);
    return NULL;
}

/* Thread that waits on condition variable */
void* waiter_thread(void* arg) {
    int id = *(int*)arg;
    
    printf("Thread %d: Waiting for signal...\n", id);
    
    pthread_mutex_lock(&cond_mutex);
    while (!ready) {
        pthread_cond_wait(&cond, &cond_mutex);
    }
    pthread_mutex_unlock(&cond_mutex);
    
    printf("Thread %d: Received signal!\n", id);
    return NULL;
}

/* Thread that signals condition variable */
void* signaler_thread(void* arg) {
    printf("Signaler: Sleeping before signal...\n");
    sleep(1);
    
    printf("Signaler: Broadcasting signal\n");
    pthread_mutex_lock(&cond_mutex);
    ready = 1;
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&cond_mutex);
    
    return NULL;
}

int main() {
    pthread_t threads[5];
    thread_data_t data[2];
    int ids[5] = {0, 1, 2, 3, 4};
    void* retval;
    
    printf("=== Basic Thread Creation Test ===\n");
    
    /* Test 1: Basic thread creation and joining */
    data[0].thread_id = 1;
    data[0].iterations = 3;
    data[1].thread_id = 2;
    data[1].iterations = 3;
    
    if (pthread_create(&threads[0], NULL, basic_thread, &data[0]) != 0) {
        fprintf(stderr, "Failed to create thread 1\n");
        return 1;
    }
    
    if (pthread_create(&threads[1], NULL, basic_thread, &data[1]) != 0) {
        fprintf(stderr, "Failed to create thread 2\n");
        return 1;
    }
    
    pthread_join(threads[0], &retval);
    printf("Thread 1 returned: %ld\n", (long)retval);
    
    pthread_join(threads[1], &retval);
    printf("Thread 2 returned: %ld\n\n", (long)retval);
    
    /* Test 2: Mutex synchronization */
    printf("=== Mutex Test ===\n");
    printf("Initial counter: %d\n", shared_counter);
    
    for (int i = 0; i < 3; i++) {
        if (pthread_create(&threads[i], NULL, mutex_thread, &ids[i]) != 0) {
            fprintf(stderr, "Failed to create mutex thread %d\n", i);
            return 1;
        }
    }
    
    for (int i = 0; i < 3; i++) {
        pthread_join(threads[i], NULL);
    }
    
    printf("Final counter: %d (expected 3000)\n\n", shared_counter);
    
    /* Test 3: Condition variables */
    printf("=== Condition Variable Test ===\n");
    
    /* Create waiter threads */
    for (int i = 0; i < 3; i++) {
        if (pthread_create(&threads[i], NULL, waiter_thread, &ids[i]) != 0) {
            fprintf(stderr, "Failed to create waiter thread %d\n", i);
            return 1;
        }
    }
    
    /* Create signaler thread */
    if (pthread_create(&threads[3], NULL, signaler_thread, NULL) != 0) {
        fprintf(stderr, "Failed to create signaler thread\n");
        return 1;
    }
    
    /* Join all threads */
    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }
    
    printf("\n=== All Tests Completed Successfully ===\n");
    
    /* Test pthread_self and pthread_equal */
    pthread_t self = pthread_self();
    printf("Main thread ID: %lu\n", (unsigned long)self);
    printf("pthread_equal(self, self) = %d (should be non-zero)\n", 
           pthread_equal(self, self));
    
    return 0;
}