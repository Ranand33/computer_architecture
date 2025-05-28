/**
 * timer_sync_example.c - Example usage of timer synchronization system
 */

#include "timer_sync.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

/* Global flag for termination */
volatile sig_atomic_t running = 1;

/* Signal handler for graceful termination */
void handle_signal(int sig)
{
    running = 0;
}

/* Example timer callback functions */
void timer1_callback(void *arg)
{
    int *counter = (int *)arg;
    (*counter)++;
    printf("[Timer 1] Tick: %d\n", *counter);
}

void timer2_callback(void *arg)
{
    int *counter = (int *)arg;
    (*counter)++;
    printf("[Timer 2] Tick: %d\n", *counter);
}

void timer3_callback(void *arg)
{
    int *counter = (int *)arg;
    (*counter)++;
    printf("[Timer 3] Synchronization point reached: %d\n", *counter);
    
    /* Wait at barrier */
    timer_group_t *group = (timer_group_t *)((void **)arg)[1];
    if (timer_barrier_wait(group, 1000) == 0) {
        printf("[Timer 3] All timers synchronized!\n");
    } else {
        printf("[Timer 3] Barrier timeout or error\n");
    }
}

void timer4_callback(void *arg)
{
    int *counter = (int *)arg;
    (*counter)++;
    printf("[Timer 4] Processing data: %d\n", *counter);
    
    /* Simulate some work */
    usleep(20000);  /* 20ms of work */
    
    /* Wait at barrier */
    timer_group_t *group = (timer_group_t *)((void **)arg)[1];
    if (timer_barrier_wait(group, 1000) == 0) {
        printf("[Timer 4] All timers synchronized!\n");
    } else {
        printf("[Timer 4] Barrier timeout or error\n");
    }
}

int main(void)
{
    timer_group_t timer_group;
    timer_t *timer1, *timer2, *timer3, *timer4;
    int timer1_counter = 0, timer2_counter = 0, timer3_counter = 0, timer4_counter = 0;
    struct timespec remaining;
    
    /* Set up signal handling for graceful shutdown */
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    printf("Timer Synchronization Example\n");
    printf("------------------------------\n");
    printf("Press Ctrl+C to exit\n\n");
    
    /* Initialize timer group */
    if (timer_group_init(&timer_group, "example_group") != 0) {
        fprintf(stderr, "Failed to initialize timer group\n");
        return 1;
    }
    
    /* Setup barrier for synchronization */
    timer_group_add_barrier(&timer_group, 2);  /* 2 timers must reach barrier */
    
    /* Create data structure for timer 3 and 4 to include group reference */
    void *timer3_data[2] = { &timer3_counter, &timer_group };
    void *timer4_data[2] = { &timer4_counter, &timer_group };
    
    /* Create timers */
    timer1 = timer_create(&timer_group, "timer1", TIMER_PERIODIC, 500,  /* 500ms interval */
                         timer1_callback, &timer1_counter);
    
    timer2 = timer_create(&timer_group, "timer2", TIMER_PERIODIC, 1000, /* 1000ms interval */
                         timer2_callback, &timer2_counter);
    
    timer3 = timer_create(&timer_group, "timer3", TIMER_PERIODIC, 2000, /* 2000ms interval */
                         timer3_callback, timer3_data);
    
    timer4 = timer_create(&timer_group, "timer4", TIMER_PERIODIC, 2000, /* 2000ms interval */
                         timer4_callback, timer4_data);
    
    if (!timer1 || !timer2 || !timer3 || !timer4) {
        fprintf(stderr, "Failed to create timers\n");
        timer_group_destroy(&timer_group);
        return 1;
    }
    
    /* Synchronize timers to a common time base */
    printf("Synchronizing timers...\n");
    timer_group_synchronize(&timer_group);
    
    /* Start all timers */
    timer_start(timer1);
    timer_start(timer2);
    timer_start(timer3);
    timer_start(timer4);
    
    printf("All timers started\n\n");
    
    /* Main loop */
    while (running) {
        /* Display timer status every 5 seconds */
        sleep(5);
        
        printf("\n=== Timer Status ===\n");
        timer_group_print_status(&timer_group);
        
        /* Demonstrate getting remaining time */
        timer_get_remaining(timer1, &remaining);
        printf("Timer1 remaining: %ld.%09ld s\n", 
               remaining.tv_sec, remaining.tv_nsec);
    }
    
    printf("\nShutting down...\n");
    
    /* Stop all timers */
    timer_stop(timer1);
    timer_stop(timer2);
    timer_stop(timer3);
    timer_stop(timer4);
    
    /* Clean up timer group */
    timer_group_destroy(&timer_group);
    
    printf("Completed\n");
    
    return 0;
}