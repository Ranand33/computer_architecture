/**
 * timer_sync.h - Header file for timer synchronization system
 */

#ifndef TIMER_SYNC_H
#define TIMER_SYNC_H

#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include <stdbool.h>

/* Timer types */
typedef enum {
    TIMER_ONESHOT,    /* Timer fires once */
    TIMER_PERIODIC    /* Timer fires repeatedly */
} timer_type_t;

/* Timer state */
typedef enum {
    TIMER_IDLE,       /* Timer is not running */
    TIMER_RUNNING,    /* Timer is running */
    TIMER_EXPIRED     /* Timer has expired */
} timer_state_t;

/* Timer callback function type */
typedef void (*timer_callback_t)(void *arg);

/* Timer structure */
typedef struct timer {
    char name[32];                /* Timer name for identification */
    timer_type_t type;            /* Type of timer */
    timer_state_t state;          /* Current state */
    struct timespec interval;     /* Timer interval */
    struct timespec expiry;       /* Absolute expiry time */
    timer_callback_t callback;    /* Function to call when timer expires */
    void *callback_arg;           /* Argument to pass to callback */
    pthread_mutex_t lock;         /* Lock for timer operations */
    struct timer *next;           /* Next timer in linked list */
} timer_t;

/* Timer group structure for synchronization */
typedef struct timer_group {
    char name[32];                /* Group name */
    timer_t *timers;              /* Linked list of timers */
    pthread_mutex_t lock;         /* Lock for group operations */
    pthread_t timer_thread;       /* Thread handling the timers */
    bool running;                 /* Is the timer thread running? */
    int timer_count;              /* Number of timers in group */
    struct timespec base_time;    /* Reference time for synchronization */
    pthread_mutex_t barrier_lock; /* Lock for barrier synchronization */
    pthread_cond_t barrier_cond;  /* Condition variable for barrier */
    int barrier_count;            /* Number of timers at barrier */
    int barrier_threshold;        /* Threshold to release barrier */
} timer_group_t;

/**
 * Initialize a new timer group
 * 
 * @param group Pointer to timer group structure
 * @param name Name for the group
 * @return 0 on success, -1 on failure
 */
int timer_group_init(timer_group_t *group, const char *name);

/**
 * Clean up and destroy a timer group
 * 
 * @param group Pointer to timer group structure
 * @return 0 on success, -1 on failure
 */
int timer_group_destroy(timer_group_t *group);

/**
 * Create a new timer and add it to a group
 * 
 * @param group Timer group
 * @param name Timer name
 * @param type Timer type (one-shot or periodic)
 * @param interval_ms Timer interval in milliseconds
 * @param callback Function to call when timer expires
 * @param arg Argument to pass to callback
 * @return Pointer to timer structure, NULL on failure
 */
timer_t *timer_create(timer_group_t *group, const char *name, timer_type_t type, 
                     uint32_t interval_ms, timer_callback_t callback, void *arg);

/**
 * Start a timer
 * 
 * @param timer Timer to start
 * @return 0 on success, -1 on failure
 */
int timer_start(timer_t *timer);

/**
 * Stop a timer
 * 
 * @param timer Timer to stop
 * @return 0 on success, -1 on failure
 */
int timer_stop(timer_t *timer);

/**
 * Reset a timer to its initial state
 * 
 * @param timer Timer to reset
 * @return 0 on success, -1 on failure
 */
int timer_reset(timer_t *timer);

/**
 * Get remaining time until timer expiry
 * 
 * @param timer Timer to query
 * @param remaining Pointer to store remaining time
 * @return 0 on success, -1 on failure
 */
int timer_get_remaining(timer_t *timer, struct timespec *remaining);

/**
 * Synchronize all timers in a group to a common base time
 * 
 * @param group Timer group to synchronize
 * @return 0 on success, -1 on failure
 */
int timer_group_synchronize(timer_group_t *group);

/**
 * Add a barrier synchronization point for timers in a group
 * 
 * @param group Timer group
 * @param threshold Number of timers that must reach barrier to release
 * @return 0 on success, -1 on failure
 */
int timer_group_add_barrier(timer_group_t *group, int threshold);

/**
 * Wait at a barrier until the threshold number of timers arrive
 * 
 * @param group Timer group
 * @param timeout_ms Maximum time to wait in milliseconds
 * @return 0 on success, -1 on timeout or failure
 */
int timer_barrier_wait(timer_group_t *group, uint32_t timeout_ms);

/**
 * Print status of all timers in a group
 * 
 * @param group Timer group
 */
void timer_group_print_status(timer_group_t *group);

/**
 * Get high-resolution time in nanoseconds
 * 
 * @return Time in nanoseconds since epoch
 */
uint64_t get_time_ns(void);

/**
 * Convert milliseconds to timespec
 * 
 * @param ms Milliseconds
 * @param ts Pointer to timespec structure
 */
void ms_to_timespec(uint32_t ms, struct timespec *ts);

/**
 * Add two timespec values
 * 
 * @param a First timespec
 * @param b Second timespec
 * @param result Result of a + b
 */
void timespec_add(const struct timespec *a, const struct timespec *b, struct timespec *result);

/**
 * Subtract two timespec values
 * 
 * @param a First timespec
 * @param b Second timespec
 * @param result Result of a - b
 * @return 1 if result is positive, 0 if zero, -1 if negative
 */
int timespec_subtract(const struct timespec *a, const struct timespec *b, struct timespec *result);

/**
 * Compare two timespec values
 * 
 * @param a First timespec
 * @param b Second timespec
 * @return 1 if a > b, 0 if a == b, -1 if a < b
 */
int timespec_compare(const struct timespec *a, const struct timespec *b);

#endif /* TIMER_SYNC_H */