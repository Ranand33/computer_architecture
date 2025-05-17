#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include <signal.h>

#define MAX_THREADS 16
#define MAX_QUEUE_SIZE 256
#define MAX_TASK_NAME 64

// Volatile flag for graceful shutdown
volatile bool shutdown_requested = false;

// Task structure
typedef struct {
    void (*function)(void *);    // Function to execute
    void *argument;              // Function argument
    char name[MAX_TASK_NAME];    // Task name for identification
    int priority;                // Task priority (higher number = higher priority)
} Task;

// Thread pool structure
typedef struct {
    pthread_t threads[MAX_THREADS];          // Worker threads
    pthread_mutex_t queue_mutex;             // Mutex for queue access
    pthread_cond_t queue_not_empty;          // Condition for signaling queue has tasks
    pthread_cond_t queue_not_full;           // Condition for signaling queue has space
    pthread_cond_t all_tasks_completed;      // Condition for wait_all function
    
    Task task_queue[MAX_QUEUE_SIZE];         // Task queue
    int queue_head;                          // Index of queue head
    int queue_tail;                          // Index of queue tail
    int queue_size;                          // Current number of tasks in queue
    
    int thread_count;                        // Number of threads in the pool
    bool *thread_busy;                       // Array tracking if a thread is busy
    
    int active_tasks;                        // Number of tasks currently being executed
    bool running;                            // Whether the pool is running or shutting down
    
    // Statistics
    unsigned long tasks_completed;           // Total number of completed tasks
    unsigned long tasks_submitted;           // Total number of submitted tasks
} ThreadPool;

// Thread worker function prototype
void *thread_worker(void *arg);

// Signal handler for graceful termination
void handle_signal(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        printf("\nShutdown signal received. Initiating graceful shutdown...\n");
        shutdown_requested = true;
    }
}

// Shutdown the thread pool
void pool_shutdown(ThreadPool *pool) {
    if (pool == NULL) {
        return;
    }
    
    pthread_mutex_lock(&pool->queue_mutex);
    pool->running = false;
    pthread_cond_broadcast(&pool->queue_not_empty);
    pthread_cond_broadcast(&pool->queue_not_full);
    pthread_mutex_unlock(&pool->queue_mutex);
    
    // Wait for all threads to finish
    for (int i = 0; i < pool->thread_count; i++) {
        pthread_join(pool->threads[i], NULL);
        printf("Thread %d terminated\n", i);
    }
}

// Destroy the thread pool and free resources
void pool_destroy(ThreadPool *pool) {
    if (pool == NULL) {
        return;
    }
    
    // Shutdown if not already done
    if (pool->running) {
        pool_shutdown(pool);
    }
    
    // Destroy mutex and condition variables
    pthread_mutex_destroy(&pool->queue_mutex);
    pthread_cond_destroy(&pool->queue_not_empty);
    pthread_cond_destroy(&pool->queue_not_full);
    pthread_cond_destroy(&pool->all_tasks_completed);
    
    // Free memory
    free(pool->thread_busy);
    free(pool);
    
    printf("Thread pool destroyed\n");
}

// Initialize the thread pool
ThreadPool *pool_init(int thread_count) {
    if (thread_count <= 0 || thread_count > MAX_THREADS) {
        thread_count = MAX_THREADS;
    }

    // Allocate pool structure
    ThreadPool *pool = (ThreadPool *)malloc(sizeof(ThreadPool));
    if (pool == NULL) {
        perror("Failed to allocate thread pool");
        return NULL;
    }

    // Initialize pool state
    pool->thread_count = thread_count;
    pool->queue_head = 0;
    pool->queue_tail = 0;
    pool->queue_size = 0;
    pool->active_tasks = 0;
    pool->running = true;
    pool->tasks_completed = 0;
    pool->tasks_submitted = 0;

    // Initialize thread busy status array
    pool->thread_busy = (bool *)calloc(thread_count, sizeof(bool));
    if (pool->thread_busy == NULL) {
        perror("Failed to allocate thread status array");
        free(pool);
        return NULL;
    }

    // Initialize mutex and condition variables
    if (pthread_mutex_init(&pool->queue_mutex, NULL) != 0 ||
        pthread_cond_init(&pool->queue_not_empty, NULL) != 0 ||
        pthread_cond_init(&pool->queue_not_full, NULL) != 0 ||
        pthread_cond_init(&pool->all_tasks_completed, NULL) != 0) {
        
        perror("Failed to initialize mutex or condition variable");
        free(pool->thread_busy);
        free(pool);
        return NULL;
    }

    // Create worker threads
    for (int i = 0; i < thread_count; i++) {
        if (pthread_create(&pool->threads[i], NULL, thread_worker, pool) != 0) {
            perror("Failed to create thread");
            // Cleanup already created threads
            pool->thread_count = i;  // Only destroy the threads we created
            pool_destroy(pool);
            return NULL;
        }
        pool->thread_busy[i] = false;
    }

    printf("Thread pool initialized with %d threads\n", thread_count);
    return pool;
}

// Add a task to the thread pool queue
int pool_add_task(ThreadPool *pool, void (*function)(void *), void *argument, const char *name, int priority) {
    if (pool == NULL || function == NULL) {
        return -1;
    }

    pthread_mutex_lock(&pool->queue_mutex);

    // Wait until queue has space or pool is shutting down
    while (pool->queue_size == MAX_QUEUE_SIZE && pool->running && !shutdown_requested) {
        pthread_cond_wait(&pool->queue_not_full, &pool->queue_mutex);
    }

    // Check if pool is shutting down
    if (!pool->running || shutdown_requested) {
        pthread_mutex_unlock(&pool->queue_mutex);
        return -1;
    }

    // Find correct position based on priority (insertion sort)
    int insert_pos = pool->queue_tail;
    
    // Create a temporary task
    Task new_task;
    new_task.function = function;
    new_task.argument = argument;
    strncpy(new_task.name, name ? name : "unnamed_task", MAX_TASK_NAME - 1);
    new_task.name[MAX_TASK_NAME - 1] = '\0';
    new_task.priority = priority;

    // Add new task to queue (with priority handling)
    if (pool->queue_size > 0) {
        // Find the position to insert based on priority
        int current_pos = (pool->queue_head + pool->queue_size - 1) % MAX_QUEUE_SIZE;
        while (pool->queue_size > 0 && 
               pool->task_queue[current_pos].priority < priority && 
               current_pos != pool->queue_head) {
            
            // Move higher priority position back
            int next_pos = (current_pos + 1) % MAX_QUEUE_SIZE;
            pool->task_queue[next_pos] = pool->task_queue[current_pos];
            
            // Move to previous position
            if (current_pos == 0) {
                current_pos = MAX_QUEUE_SIZE - 1;
            } else {
                current_pos--;
            }
        }
        
        // Check if we need to insert after the current position
        if (pool->task_queue[current_pos].priority >= priority) {
            insert_pos = (current_pos + 1) % MAX_QUEUE_SIZE;
        } else {
            insert_pos = current_pos;
        }
    }
    
    // Insert task at the determined position
    pool->task_queue[insert_pos] = new_task;
    
    // Update queue pointers
    if (pool->queue_size == 0) {
        // Queue was empty, update head
        pool->queue_head = insert_pos;
        pool->queue_tail = (insert_pos + 1) % MAX_QUEUE_SIZE;
    } else {
        // Update tail if needed
        if (insert_pos == pool->queue_tail) {
            pool->queue_tail = (pool->queue_tail + 1) % MAX_QUEUE_SIZE;
        } else {
            // Otherwise we're inserting in the middle, so shift everything
            int i = pool->queue_tail;
            while (i != insert_pos) {
                int prev = (i - 1 + MAX_QUEUE_SIZE) % MAX_QUEUE_SIZE;
                pool->task_queue[i] = pool->task_queue[prev];
                i = prev;
            }
            pool->queue_tail = (pool->queue_tail + 1) % MAX_QUEUE_SIZE;
        }
    }
    
    pool->queue_size++;
    pool->tasks_submitted++;

    // Signal waiting threads that there's work to do
    pthread_cond_signal(&pool->queue_not_empty);
    pthread_mutex_unlock(&pool->queue_mutex);

    return 0;
}

// Worker thread function
void *thread_worker(void *arg) {
    ThreadPool *pool = (ThreadPool *)arg;
    
    // Find my thread ID in the pool
    pthread_t my_id = pthread_self();
    int thread_index = -1;
    
    for (int i = 0; i < pool->thread_count; i++) {
        if (pthread_equal(pool->threads[i], my_id)) {
            thread_index = i;
            break;
        }
    }
    
    Task task;
    
    while (1) {
        pthread_mutex_lock(&pool->queue_mutex);
        
        // Set status to not busy
        if (thread_index >= 0) {
            pool->thread_busy[thread_index] = false;
        }
        
        // Wait until there are tasks or shutdown is requested
        while (pool->queue_size == 0 && pool->running && !shutdown_requested) {
            pthread_cond_wait(&pool->queue_not_empty, &pool->queue_mutex);
        }
        
        // Check if we should exit
        if ((!pool->running || shutdown_requested) && pool->queue_size == 0) {
            pthread_mutex_unlock(&pool->queue_mutex);
            pthread_exit(NULL);
        }
        
        // Get a task from the queue
        task = pool->task_queue[pool->queue_head];
        pool->queue_head = (pool->queue_head + 1) % MAX_QUEUE_SIZE;
        pool->queue_size--;
        pool->active_tasks++;
        
        // Set status to busy
        if (thread_index >= 0) {
            pool->thread_busy[thread_index] = true;
        }
        
        // Signal that the queue is not full
        pthread_cond_signal(&pool->queue_not_full);
        pthread_mutex_unlock(&pool->queue_mutex);
        
        // Execute the task
        printf("Thread %d executing task '%s' (priority: %d)\n", 
               thread_index, task.name, task.priority);
        (*(task.function))(task.argument);
        
        // Update task completion status
        pthread_mutex_lock(&pool->queue_mutex);
        pool->active_tasks--;
        pool->tasks_completed++;
        
        // If all tasks are done, signal waiting threads
        if (pool->active_tasks == 0 && pool->queue_size == 0) {
            pthread_cond_broadcast(&pool->all_tasks_completed);
        }
        
        pthread_mutex_unlock(&pool->queue_mutex);
    }
    
    return NULL;
}

// Wait for all tasks to complete
void pool_wait_all(ThreadPool *pool) {
    if (pool == NULL) {
        return;
    }
    
    pthread_mutex_lock(&pool->queue_mutex);
    
    // Wait until all tasks are completed
    while ((pool->active_tasks > 0 || pool->queue_size > 0) && pool->running) {
        pthread_cond_wait(&pool->all_tasks_completed, &pool->queue_mutex);
    }
    
    pthread_mutex_unlock(&pool->queue_mutex);
}

// Get thread pool status
void pool_get_status(ThreadPool *pool, char *buffer, size_t buffer_size) {
    if (pool == NULL || buffer == NULL) {
        return;
    }
    
    pthread_mutex_lock(&pool->queue_mutex);
    
    snprintf(buffer, buffer_size,
             "Thread pool status:\n"
             "  Threads: %d\n"
             "  Tasks in queue: %d\n"
             "  Active tasks: %d\n"
             "  Tasks completed: %lu/%lu\n"
             "  Running: %s\n",
             pool->thread_count,
             pool->queue_size,
             pool->active_tasks,
             pool->tasks_completed, pool->tasks_submitted,
             pool->running ? "yes" : "no");
    
    pthread_mutex_unlock(&pool->queue_mutex);
}



//-------------------------------------------------------------------------
// Multiplexer implementation
//-------------------------------------------------------------------------

typedef struct {
    ThreadPool *pool;
    int input_channels;
    void (*channel_handler)(int channel_id, void *data);
    void *channel_data[256];  // Data for each channel
    bool channel_active[256]; // Whether each channel is active
} Multiplexer;

// Initialize a multiplexer with a thread pool
Multiplexer *multiplexer_init(ThreadPool *pool, int channels) {
    if (pool == NULL || channels <= 0 || channels > 256) {
        return NULL;
    }
    
    Multiplexer *mux = (Multiplexer *)malloc(sizeof(Multiplexer));
    if (mux == NULL) {
        perror("Failed to allocate multiplexer");
        return NULL;
    }
    
    mux->pool = pool;
    mux->input_channels = channels;
    mux->channel_handler = NULL;
    
    memset(mux->channel_data, 0, sizeof(mux->channel_data));
    memset(mux->channel_active, 0, sizeof(mux->channel_active));
    
    printf("Multiplexer initialized with %d channels\n", channels);
    return mux;
}

// Structure to pass to thread tasks
typedef struct {
    Multiplexer *mux;
    int channel_id;
    void *data;
} ChannelTask;

// Function executed by thread pool tasks for channel processing
void process_channel_task(void *arg) {
    ChannelTask *task = (ChannelTask *)arg;
    
    if (task && task->mux && task->mux->channel_handler) {
        task->mux->channel_handler(task->channel_id, task->data);
    }
    
    free(task);
}

// Set the channel handler function
void multiplexer_set_handler(Multiplexer *mux, void (*handler)(int, void *)) {
    if (mux) {
        mux->channel_handler = handler;
    }
}

// Submit data to a channel for processing
int multiplexer_submit(Multiplexer *mux, int channel, void *data, int priority) {
    if (mux == NULL || channel < 0 || channel >= mux->input_channels) {
        return -1;
    }
    
    if (mux->channel_handler == NULL) {
        printf("Error: No channel handler set\n");
        return -1;
    }
    
    // Store data for the channel
    mux->channel_data[channel] = data;
    mux->channel_active[channel] = true;
    
    // Create a task
    ChannelTask *task = (ChannelTask *)malloc(sizeof(ChannelTask));
    if (task == NULL) {
        perror("Failed to allocate channel task");
        return -1;
    }
    
    task->mux = mux;
    task->channel_id = channel;
    task->data = data;
    
    // Create task name
    char task_name[MAX_TASK_NAME];
    snprintf(task_name, MAX_TASK_NAME, "channel_%d_task", channel);
    
    // Submit task to thread pool
    if (pool_add_task(mux->pool, process_channel_task, task, task_name, priority) != 0) {
        free(task);
        return -1;
    }
    
    return 0;
}

// Wait for all channel tasks to complete
void multiplexer_wait_all(Multiplexer *mux) {
    if (mux) {
        pool_wait_all(mux->pool);
    }
}

// Reset channel status
void multiplexer_reset(Multiplexer *mux) {
    if (mux) {
        memset(mux->channel_active, 0, sizeof(mux->channel_active));
        memset(mux->channel_data, 0, sizeof(mux->channel_data));
    }
}

// Destroy the multiplexer
void multiplexer_destroy(Multiplexer *mux) {
    if (mux) {
        // Note: We don't destroy the pool here since it might be shared
        free(mux);
        printf("Multiplexer destroyed\n");
    }
}

//-------------------------------------------------------------------------
// Example usage
//-------------------------------------------------------------------------

// Example channel handler
void example_channel_handler(int channel_id, void *data) {
    char *message = (char *)data;
    
    // Simulate some work
    printf("Channel %d processing message: %s\n", channel_id, message);
    
    // Simulate varying workloads
    int sleep_time = 1 + (channel_id % 3);
    sleep(sleep_time);
    
    printf("Channel %d completed processing (took %d seconds)\n", channel_id, sleep_time);
}

// Example task for the thread pool
void example_task(void *arg) {
    int task_id = *((int *)arg);
    
    // Simulate work
    printf("Regular task %d started\n", task_id);
    sleep(2);
    printf("Regular task %d completed\n", task_id);
    
    free(arg);
}

int main() {
    // Set up signal handler for graceful termination
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    // Initialize thread pool with 4 worker threads
    ThreadPool *pool = pool_init(4);
    if (pool == NULL) {
        fprintf(stderr, "Failed to initialize thread pool\n");
        return 1;
    }
    
    // Initialize multiplexer with 8 channels
    Multiplexer *mux = multiplexer_init(pool, 8);
    if (mux == NULL) {
        fprintf(stderr, "Failed to initialize multiplexer\n");
        pool_destroy(pool);
        return 1;
    }
    
    // Set channel handler
    multiplexer_set_handler(mux, example_channel_handler);
    
    // Submit some regular tasks to the pool
    for (int i = 0; i < 5; i++) {
        int *task_id = (int *)malloc(sizeof(int));
        *task_id = i;
        
        char task_name[MAX_TASK_NAME];
        snprintf(task_name, MAX_TASK_NAME, "task_%d", i);
        
        pool_add_task(pool, example_task, task_id, task_name, i % 3);
    }
    
    // Submit data to multiplexer channels
    const char *messages[] = {
        "High priority message",
        "Medium priority data stream",
        "Low priority background task",
        "Urgent notification",
        "Standard processing job",
        "Bulk data transfer",
        "System status update",
        "User interaction event"
    };
    
    for (int i = 0; i < 8; i++) {
        // Alternating priorities
        int priority = (i % 3 == 0) ? 3 : ((i % 3 == 1) ? 2 : 1);
        multiplexer_submit(mux, i, (void *)messages[i], priority);
    }
    
    // Get pool status
    char status_buffer[512];
    pool_get_status(pool, status_buffer, sizeof(status_buffer));
    printf("\n%s\n", status_buffer);
    
    // Wait for all tasks to complete
    printf("Waiting for all tasks to complete...\n");
    multiplexer_wait_all(mux);
    
    // Get final status
    pool_get_status(pool, status_buffer, sizeof(status_buffer));
    printf("\n%s\n", status_buffer);
    
    // Submit a second batch with different priorities
    printf("\nSubmitting second batch of tasks...\n");
    const char *batch2[] = {
        "Follow-up message 1",
        "Follow-up message 2",
        "Follow-up message 3",
        "Follow-up message 4"
    };
    
    for (int i = 0; i < 4; i++) {
        // Reverse priorities this time
        int priority = 3 - (i % 3);
        multiplexer_submit(mux, i, (void *)batch2[i], priority);
    }
    
    // Wait for second batch
    multiplexer_wait_all(mux);
    
    // Clean up resources
    multiplexer_destroy(mux);
    pool_destroy(pool);
    
    printf("Program completed successfully\n");
    return 0;
}