/**
 * timer_sync.c - Implementation of timer synchronization system
 */

#include "timer_sync.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <assert.h>

/* Get current time with nanosecond precision */
uint64_t get_time_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* Convert milliseconds to timespec */
void ms_to_timespec(uint32_t ms, struct timespec *ts)
{
    ts->tv_sec = ms / 1000;
    ts->tv_nsec = (ms % 1000) * 1000000;
}

/* Add two timespec values */
void timespec_add(const struct timespec *a, const struct timespec *b, struct timespec *result)
{
    result->tv_sec = a->tv_sec + b->tv_sec;
    result->tv_nsec = a->tv_nsec + b->tv_nsec;
    if (result->tv_nsec >= 1000000000L) {
        result->tv_sec++;
        result->tv_nsec -= 1000000000L;
    }
}

/* Subtract two timespec values */
int timespec_subtract(const struct timespec *a, const struct timespec *b, struct timespec *result)
{
    result->tv_sec = a->tv_sec - b->tv_sec;
    result->tv_nsec = a->tv_nsec - b->tv_nsec;
    
    if (result->tv_nsec < 0) {
        result->tv_sec--;
        result->tv_nsec += 1000000000L;
    }
    
    return (result->tv_sec < 0) ? -1 : ((result->tv_sec > 0 || result->tv_nsec > 0) ? 1 : 0);
}

/* Compare two timespec values */
int timespec_compare(const struct timespec *a, const struct timespec *b)
{
    if (a->tv_sec > b->tv_sec)
        return 1;
    else if (a->tv_sec < b->tv_sec)
        return -1;
    else
        return (a->tv_nsec > b->tv_nsec) ? 1 : ((a->tv_nsec < b->tv_nsec) ? -1 : 0);
}

/* Timer processing function (runs in timer thread) */
static void *timer_thread_func(void *arg)
{
    timer_group_t *group = (timer_group_t *)arg;
    struct timespec now, interval, sleep_time;
    timer_t *timer;
    
    /* Set up a small interval for checking timers */
    ms_to_timespec(10, &interval); /* 10ms check interval */
    
    while (group->running) {
        /* Get current time */
        clock_gettime(CLOCK_MONOTONIC, &now);
        
        /* Process all timers */
        pthread_mutex_lock(&group->lock);
        timer = group->timers;
        
        while (timer != NULL) {
            pthread_mutex_lock(&timer->lock);
            
            if (timer->state == TIMER_RUNNING) {
                /* Check if timer has expired */
                if (timespec_compare(&now, &timer->expiry) >= 0) {
                    /* Timer expired */
                    timer->state = TIMER_EXPIRED;
                    
                    /* Call the callback if set */
                    if (timer->callback) {
                        pthread_mutex_unlock(&timer->lock);
                        timer->callback(timer->callback_arg);
                        pthread_mutex_lock(&timer->lock);
                    }
                    
                    /* For periodic timers, calculate next expiry */
                    if (timer->type == TIMER_PERIODIC) {
                        do {
                            timespec_add(&timer->expiry, &timer->interval, &timer->expiry);
                        } while (timespec_compare(&now, &timer->expiry) >= 0);
                        
                        timer->state = TIMER_RUNNING;
                    }
                }
            }
            
            pthread_mutex_unlock(&timer->lock);
            timer = timer->next;
        }
        
        pthread_mutex_unlock(&group->lock);
        
        /* Sleep for check interval */
        nanosleep(&interval, NULL);
    }
    
    return NULL;
}

/* Initialize a timer group */
int timer_group_init(timer_group_t *group, const char *name)
{
    if (!group || !name || strlen(name) >= sizeof(group->name))
        return -1;
    
    memset(group, 0, sizeof(timer_group_t));
    strncpy(group->name, name, sizeof(group->name) - 1);
    
    if (pthread_mutex_init(&group->lock, NULL) != 0)
        return -1;
    
    if (pthread_mutex_init(&group->barrier_lock, NULL) != 0) {
        pthread_mutex_destroy(&group->lock);
        return -1;
    }
    
    if (pthread_cond_init(&group->barrier_cond, NULL) != 0) {
        pthread_mutex_destroy(&group->barrier_lock);
        pthread_mutex_destroy(&group->lock);
        return -1;
    }
    
    group->timers = NULL;
    group->running = true;
    
    /* Set base time for synchronization */
    clock_gettime(CLOCK_MONOTONIC, &group->base_time);
    
    /* Create timer thread */
    if (pthread_create(&group->timer_thread, NULL, timer_thread_func, group) != 0) {
        pthread_cond_destroy(&group->barrier_cond);
        pthread_mutex_destroy(&group->barrier_lock);
        pthread_mutex_destroy(&group->lock);
        return -1;
    }
    
    return 0;
}

/* Clean up and destroy a timer group */
int timer_group_destroy(timer_group_t *group)
{
    timer_t *timer, *next;
    
    if (!group)
        return -1;
    
    /* Signal thread to stop */
    group->running = false;
    
    /* Wait for thread to exit */
    pthread_join(group->timer_thread, NULL);
    
    /* Clean up all timers */
    pthread_mutex_lock(&group->lock);
    
    timer = group->timers;
    while (timer != NULL) {
        next = timer->next;
        pthread_mutex_destroy(&timer->lock);
        free(timer);
        timer = next;
    }
    
    pthread_mutex_unlock(&group->lock);
    
    /* Destroy synchronization primitives */
    pthread_cond_destroy(&group->barrier_cond);
    pthread_mutex_destroy(&group->barrier_lock);
    pthread_mutex_destroy(&group->lock);
    
    return 0;
}

/* Create a new timer and add it to a group */
timer_t *timer_create(timer_group_t *group, const char *name, timer_type_t type, 
                     uint32_t interval_ms, timer_callback_t callback, void *arg)
{
    timer_t *timer;
    
    if (!group || !name || strlen(name) >= 32)
        return NULL;
    
    /* Allocate and initialize timer */
    timer = (timer_t *)malloc(sizeof(timer_t));
    if (!timer)
        return NULL;
    
    memset(timer, 0, sizeof(timer_t));
    strncpy(timer->name, name, sizeof(timer->name) - 1);
    
    timer->type = type;
    timer->state = TIMER_IDLE;
    timer->callback = callback;
    timer->callback_arg = arg;
    
    /* Convert interval from milliseconds to timespec */
    ms_to_timespec(interval_ms, &timer->interval);
    
    /* Initialize timer lock */
    if (pthread_mutex_init(&timer->lock, NULL) != 0) {
        free(timer);
        return NULL;
    }
    
    /* Add timer to group */
    pthread_mutex_lock(&group->lock);
    
    timer->next = group->timers;
    group->timers = timer;
    group->timer_count++;
    
    pthread_mutex_unlock(&group->lock);
    
    return timer;
}

/* Start a timer */
int timer_start(timer_t *timer)
{
    struct timespec now;
    
    if (!timer)
        return -1;
    
    pthread_mutex_lock(&timer->lock);
    
    /* Get current time */
    clock_gettime(CLOCK_MONOTONIC, &now);
    
    /* Calculate expiry time */
    timespec_add(&now, &timer->interval, &timer->expiry);
    
    /* Update state */
    timer->state = TIMER_RUNNING;
    
    pthread_mutex_unlock(&timer->lock);
    
    return 0;
}

/* Stop a timer */
int timer_stop(timer_t *timer)
{
    if (!timer)
        return -1;
    
    pthread_mutex_lock(&timer->lock);
    timer->state = TIMER_IDLE;
    pthread_mutex_unlock(&timer->lock);
    
    return 0;
}

/* Reset a timer to its initial state */
int timer_reset(timer_t *timer)
{
    if (!timer)
        return -1;
    
    pthread_mutex_lock(&timer->lock);
    timer->state = TIMER_IDLE;
    pthread_mutex_unlock(&timer->lock);
    
    return 0;
}

/* Get remaining time until timer expiry */
int timer_get_remaining(timer_t *timer, struct timespec *remaining)
{
    struct timespec now;
    int result;
    
    if (!timer || !remaining)
        return -1;
    
    pthread_mutex_lock(&timer->lock);
    
    /* If timer isn't running, return 0 */
    if (timer->state != TIMER_RUNNING) {
        remaining->tv_sec = 0;
        remaining->tv_nsec = 0;
        pthread_mutex_unlock(&timer->lock);
        return 0;
    }
    
    /* Calculate time until expiry */
    clock_gettime(CLOCK_MONOTONIC, &now);
    result = timespec_subtract(&timer->expiry, &now, remaining);
    
    /* If result is negative, the timer has already expired */
    if (result < 0) {
        remaining->tv_sec = 0;
        remaining->tv_nsec = 0;
    }
    
    pthread_mutex_unlock(&timer->lock);
    
    return 0;
}

/* Synchronize all timers in a group to a common base time */
int timer_group_synchronize(timer_group_t *group)
{
    timer_t *timer;
    struct timespec now, offset;
    
    if (!group)
        return -1;
    
    /* Get current time */
    clock_gettime(CLOCK_MONOTONIC, &now);
    
    /* Calculate time to next 100ms boundary */
    offset.tv_sec = now.tv_sec;
    offset.tv_nsec = ((now.tv_nsec / 100000000UL) + 1) * 100000000UL;
    if (offset.tv_nsec >= 1000000000L) {
        offset.tv_sec++;
        offset.tv_nsec -= 1000000000L;
    }
    
    /* Update base time */
    pthread_mutex_lock(&group->lock);
    group->base_time = offset;
    
    /* Adjust all running timers to align with base time */
    timer = group->timers;
    while (timer != NULL) {
        pthread_mutex_lock(&timer->lock);
        
        if (timer->state == TIMER_RUNNING) {
            /* Recalculate expiry based on new base time */
            int intervals = 1;
            struct timespec temp = timer->interval;
            
            while (timespec_compare(&temp, &offset) < 0) {
                intervals++;
                timespec_add(&temp, &timer->interval, &temp);
            }
            
            timer->expiry = temp;
        }
        
        pthread_mutex_unlock(&timer->lock);
        timer = timer->next;
    }
    
    pthread_mutex_unlock(&group->lock);
    
    /* Sleep until we reach the synchronization point */
    if (timespec_subtract(&offset, &now, &offset) > 0) {
        nanosleep(&offset, NULL);
    }
    
    return 0;
}

/* Add a barrier synchronization point for timers in a group */
int timer_group_add_barrier(timer_group_t *group, int threshold)
{
    if (!group || threshold <= 0 || threshold > group->timer_count)
        return -1;
    
    pthread_mutex_lock(&group->barrier_lock);
    
    group->barrier_count = 0;
    group->barrier_threshold = threshold;
    
    pthread_mutex_unlock(&group->barrier_lock);
    
    return 0;
}

/* Wait at a barrier until the threshold number of timers arrive */
int timer_barrier_wait(timer_group_t *group, uint32_t timeout_ms)
{
    struct timespec abs_time;
    int result = 0;
    
    if (!group)
        return -1;
    
    /* Calculate absolute timeout time */
    clock_gettime(CLOCK_REALTIME, &abs_time);
    ms_to_timespec(timeout_ms, &abs_time);
    
    pthread_mutex_lock(&group->barrier_lock);
    
    /* Increment barrier count */
    group->barrier_count++;
    
    /* If we haven't reached threshold, wait */
    if (group->barrier_count < group->barrier_threshold) {
        result = pthread_cond_timedwait(&group->barrier_cond, 
                                       &group->barrier_lock, &abs_time);
    } else {
        /* Last to arrive, reset barrier and signal all waiters */
        group->barrier_count = 0;
        pthread_cond_broadcast(&group->barrier_cond);
    }
    
    pthread_mutex_unlock(&group->barrier_lock);
    
    return (result == 0) ? 0 : -1;
}

/* Print status of all timers in a group */
void timer_group_print_status(timer_group_t *group)
{
    timer_t *timer;
    struct timespec remaining;
    
    if (!group)
        return;
    
    printf("Timer Group: %s\n", group->name);
    printf("Total Timers: %d\n\n", group->timer_count);
    
    pthread_mutex_lock(&group->lock);
    
    timer = group->timers;
    while (timer != NULL) {
        pthread_mutex_lock(&timer->lock);
        
        printf("Timer: %s\n", timer->name);
        printf("  Type: %s\n", (timer->type == TIMER_ONESHOT) ? "One-shot" : "Periodic");
        printf("  State: %s\n", 
               (timer->state == TIMER_IDLE) ? "Idle" : 
               (timer->state == TIMER_RUNNING) ? "Running" : "Expired");
        
        printf("  Interval: %ld.%09ld s\n", 
               timer->interval.tv_sec, timer->interval.tv_nsec);
        
        if (timer->state == TIMER_RUNNING) {
            timer_get_remaining(timer, &remaining);
            printf("  Remaining: %ld.%09ld s\n", 
                   remaining.tv_sec, remaining.tv_nsec);
        }
        
        printf("\n");
        
        pthread_mutex_unlock(&timer->lock);
        timer = timer->next;
    }
    
    pthread_mutex_unlock(&group->lock);
}