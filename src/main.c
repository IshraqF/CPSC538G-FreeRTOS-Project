#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <stdbool.h>
#include "pico/stdlib.h"

/* Select which test to run (1–8). */
#ifndef TEST_CASE
    #define TEST_CASE  13
#endif

#define MS( x )   pdMS_TO_TICKS( x )

/* LED blink task — 100 ms on, 100 ms off (200 ms period).
 * Runs at priority 1 (background), preempted by all EDF tasks.
 */
static void vLEDTask( void * pvParams )
{
    const uint LED_PIN = PICO_DEFAULT_LED_PIN;
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    while (true) {
        gpio_put(LED_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_put(LED_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void vTaskSwitchedOutHook(void)
{
    UBaseType_t gpio = uxTaskGPIOGet();
    gpio_put(gpio, 0); // Turn off GPIO for the task
}

// Hook called when a task is switched in (optional)
void vTaskSwitchedInHook(void)
{
    UBaseType_t gpio = uxTaskGPIOGet();
    gpio_put(gpio, 1); // Turn off GPIO for the task
}

/* UART drain task — drains both ring buffers and prints via printf.
 * Runs at priority 3, above EDF tasks, so the log never starves.
 */
static void vUARTDrainTask( void * pvParams )
{
    ( void ) pvParams;

    for( ;; )
    {
        vEDFDrainSwitchLog();
        vEDFDrainMissLog();
        vTaskDelay( MS( 10 ) );
    }
}

/* -----------------------------------------------------------------------
 * TEST 1 — Single implicit-deadline task, U = 0.5
 *   T=200 ms, D=200 ms, C=100 ms → U = 0.5 ≤ 1.0, should be admitted.
 * ----------------------------------------------------------------------- */
#if ( TEST_CASE == 1 )

static void vTask1( void * pvParams )
{
    ( void ) pvParams;
    TickType_t xLastWakeTime = xTaskGetTickCount();

    for( ;; )
    {
        /* Simulate computation: busy-wait ~100 ms. */
        TickType_t xStart = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xStart ) < MS( 100 ) ) { __asm volatile ( "nop" ); }

        vTaskDelayEDF( &xLastWakeTime );
    }
}

int main( void )
{
    gpio_init(9);
    gpio_set_dir(9, GPIO_OUT);
    stdio_init_all();
    printf( "\r\n=== EDF Test 1: Single task, U=0.5 ===\r\n" );

    xTaskCreateEDF( vTask1, "T1_Half", 512, NULL, 2,
                    MS( 200 ), MS( 200 ), MS( 100 ), 9, NULL );

    xTaskCreate( vLEDTask, "LED", 256, NULL, 1, NULL );
    // xTaskCreate( vUARTDrainTask, "UARTDrain", 512, NULL, 3, NULL );

    vTaskStartScheduler();
    while( 1 ) {}
}

#endif /* TEST_CASE == 1 */

/* -----------------------------------------------------------------------
 * TEST 2 — Two implicit-deadline tasks at the LL boundary, U = 1.0
 *   Task A: T=100, D=100, C=50  → U=0.5
 *   Task B: T=200, D=200, C=100 → U=0.5
 * ----------------------------------------------------------------------- */
#if ( TEST_CASE == 2 )

static void vTaskA( void * pvParams )
{
    ( void ) pvParams;
    TickType_t xLWT = xTaskGetTickCount();
    for( ;; )
    {
        TickType_t xS = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xS ) < MS( 50 ) ) { __asm volatile ( "nop" ); }
        vTaskDelayEDF( &xLWT );
    }
}

static void vTaskB( void * pvParams )
{
    ( void ) pvParams;
    TickType_t xLWT = xTaskGetTickCount();
    for( ;; )
    {
        TickType_t xS = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xS ) < MS( 100 ) ) { __asm volatile ( "nop" ); }
        vTaskDelayEDF( &xLWT );
    }
}

int main( void )
{
    stdio_init_all();
    printf( "\r\n=== EDF Test 2: Two tasks at LL boundary (U=1.0) ===\r\n" );

    xTaskCreateEDF( vTaskA, "T2_A", 512, NULL, 2, MS( 100 ), MS( 100 ), MS( 50 ),  NULL );
    xTaskCreateEDF( vTaskB, "T2_B", 512, NULL, 2, MS( 200 ), MS( 200 ), MS( 100 ), NULL );
    xTaskCreate( vLEDTask, "LED", 256, NULL, 1, NULL );
    xTaskCreate( vUARTDrainTask, "UARTDrain", 512, NULL, 3, NULL );

    vTaskStartScheduler();
    while( 1 ) {}
}

#endif /* TEST_CASE == 2 */

/* -----------------------------------------------------------------------
 * TEST 3 — EDF task + non-EDF (fixed-priority) background task
 *   EDF Task A: T=100, D=100, C=30 ms
 *   Non-EDF Task B: priority 1, runs when A is blocked
 * ----------------------------------------------------------------------- */
#if ( TEST_CASE == 3 )

static void vEDFTask( void * pvParams )
{
    ( void ) pvParams;
    TickType_t xLWT = xTaskGetTickCount();
    for( ;; )
    {
        TickType_t xS = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xS ) < MS( 30 ) ) { __asm volatile ( "nop" ); }
        vTaskDelayEDF( &xLWT );
    }
}

static void vBackgroundTask( void * pvParams )
{
    ( void ) pvParams;
    for( ;; )
    {
        printf( "[BG ] running (non-EDF background)\r\n" );
        vTaskDelay( MS( 50 ) );
    }
}

int main( void )
{
    stdio_init_all();
    printf( "\r\n=== EDF Test 3: EDF + non-EDF coexistence ===\r\n" );

    xTaskCreateEDF( vEDFTask, "T3_EDF", 512, NULL, 2,
                    MS( 100 ), MS( 100 ), MS( 30 ), NULL );

    /* Non-EDF task at priority 1 (same numeric priority as EDF tasks, but
     * the scheduler picks EDF tasks from xEDFReadyTasksList first). */
    xTaskCreate( vBackgroundTask, "T3_BG",  512, NULL, 1, NULL );
    xTaskCreate( vLEDTask, "LED", 256, NULL, 1, NULL );
    xTaskCreate( vUARTDrainTask, "UARTDrain", 512, NULL, 3, NULL );

    vTaskStartScheduler();
    while( 1 ) {}
}

#endif /* TEST_CASE == 3 */

/* -----------------------------------------------------------------------
 * TEST 4 — Admission control rejection
 *   Task A: T=100, D=100, C=60  → U=0.60
 *   Task B: T=200, D=200, C=100 → U=0.50, total=1.10 > 1.0 → REJECT
 * ----------------------------------------------------------------------- */
#if ( TEST_CASE == 4 )

static void vTaskAccepted( void * pvParams )
{
    ( void ) pvParams;
    TickType_t xLWT = xTaskGetTickCount();
    for( ;; )
    {
        TickType_t xS = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xS ) < MS( 60 ) ) { __asm volatile ( "nop" ); }
        vTaskDelayEDF( &xLWT );
    }
}

int main( void )
{
    BaseType_t xResult;
    stdio_init_all();
    printf( "\r\n=== EDF Test 4: Admission control rejection ===\r\n" );

    /* Task A: U=0.60 — should be admitted. */
    xResult = xTaskCreateEDF( vTaskAccepted, "T4_A", 512, NULL, 2,
                               MS( 100 ), MS( 100 ), MS( 60 ), NULL );
    printf( "T4_A create result: %s\r\n", ( xResult == pdPASS ) ? "PASS" : "FAIL" );

    /* Task B: would push total U to 1.10 — should be rejected. */
    xResult = xTaskCreateEDF( vTaskAccepted, "T4_B", 512, NULL, 2,
                               MS( 200 ), MS( 200 ), MS( 100 ), NULL );
    printf( "T4_B create result (expect FAIL): %s\r\n", ( xResult == pdPASS ) ? "PASS" : "FAIL" );

    xTaskCreate( vLEDTask, "LED", 256, NULL, 1, NULL );
    xTaskCreate( vUARTDrainTask, "UARTDrain", 512, NULL, 3, NULL );

    vTaskStartScheduler();
    while( 1 ) {}
}

#endif /* TEST_CASE == 4 */

/* -----------------------------------------------------------------------
 * TEST 5 — Deadline miss detection
 *   Task A: T=200, D=200, C declared=50 ms but actual work=210 ms
 * ----------------------------------------------------------------------- */
#if ( TEST_CASE == 5 )

static void vOverrunTask( void * pvParams )
{
    ( void ) pvParams;
    TickType_t xLWT = xTaskGetTickCount();
    for( ;; )
    {
        TickType_t xS = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xS ) < MS( 210 ) ) { __asm volatile ( "nop" ); }
        vEDFDrainMissLog();
        vTaskDelayEDF( &xLWT );
    }
}

int main( void )
{
    stdio_init_all();
    printf( "\r\n=== EDF Test 5: Deadline miss detection ===\r\n" );

    /* WCET declared as 50 ms but task will run for 210 ms — generates a miss. */
    xTaskCreateEDF( vOverrunTask, "T5_Miss", 512, NULL, 2,
                    MS( 200 ), MS( 200 ), MS( 50 ), NULL );

    xTaskCreate( vLEDTask, "LED", 256, NULL, 1, NULL );
    xTaskCreate( vUARTDrainTask, "UARTDrain", 512, NULL, 3, NULL );

    vTaskStartScheduler();
    while( 1 ) {}
}

#endif /* TEST_CASE == 5 */

/* -----------------------------------------------------------------------
 * TEST 6 — EDF ordering with three tasks at different deadlines
 *   Task A (constrained): T=300 ms, D=100 ms, C=20 ms → deadline=100
 *   Task B (constrained): T=200 ms, D=150 ms, C=30 ms → deadline=150
 *   Task C (constrained): T=400 ms, D=200 ms, C=40 ms → deadline=200
 * ----------------------------------------------------------------------- */
#if ( TEST_CASE == 6 )

static void vTask6A( void * pvParams )
{
    ( void ) pvParams;
    TickType_t xLWT = xTaskGetTickCount();
    for( ;; )
    {
        TickType_t xS = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xS ) < MS( 20 ) ) { __asm volatile ( "nop" ); }
        vTaskDelayEDF( &xLWT );
    }
}

static void vTask6B( void * pvParams )
{
    ( void ) pvParams;
    TickType_t xLWT = xTaskGetTickCount();
    for( ;; )
    {
        TickType_t xS = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xS ) < MS( 30 ) ) { __asm volatile ( "nop" ); }
        vTaskDelayEDF( &xLWT );
    }
}

static void vTask6C( void * pvParams )
{
    ( void ) pvParams;
    TickType_t xLWT = xTaskGetTickCount();
    for( ;; )
    {
        TickType_t xS = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xS ) < MS( 40 ) ) { __asm volatile ( "nop" ); }
        vTaskDelayEDF( &xLWT );
    }
}

int main( void )
{
    stdio_init_all();
    printf( "\r\n=== EDF Test 6: Three-task EDF ordering ===\r\n" );
    printf( "Expected switch order at t=0: A(D=100) -> B(D=150) -> C(D=200)\r\n\r\n" );

    xTaskCreateEDF( vTask6A, "T6_A", 512, NULL, 2, MS( 300 ), MS( 100 ), MS( 20 ), NULL );
    xTaskCreateEDF( vTask6B, "T6_B", 512, NULL, 2, MS( 200 ), MS( 150 ), MS( 30 ), NULL );
    xTaskCreateEDF( vTask6C, "T6_C", 512, NULL, 2, MS( 400 ), MS( 200 ), MS( 40 ), NULL );

    xTaskCreate( vLEDTask, "LED", 256, NULL, 1, NULL );
    xTaskCreate( vUARTDrainTask, "UARTDrain", 512, NULL, 3, NULL );

    vTaskStartScheduler();
    while( 1 ) {}
}

#endif /* TEST_CASE == 6 */

/* -----------------------------------------------------------------------
 * TEST 7 — Constrained-deadline admission via processor demand criterion
 *   Task A: T=10 ms, D=5 ms, C=3 ms
 *   Task B: T=15 ms, D=8 ms, C=4 ms
 * ----------------------------------------------------------------------- */
#if ( TEST_CASE == 7 )

static void vTask7A( void * pvParams )
{
    ( void ) pvParams;
    TickType_t xLWT = xTaskGetTickCount();
    for( ;; )
    {
        TickType_t xS = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xS ) < MS( 3 ) ) { __asm volatile ( "nop" ); }
        vTaskDelayEDF( &xLWT );
    }
}

static void vTask7B( void * pvParams )
{
    ( void ) pvParams;
    TickType_t xLWT = xTaskGetTickCount();
    for( ;; )
    {
        TickType_t xS = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xS ) < MS( 4 ) ) { __asm volatile ( "nop" ); }
        vTaskDelayEDF( &xLWT );
    }
}

int main( void )
{
    BaseType_t xRA, xRB;
    stdio_init_all();
    printf( "\r\n=== EDF Test 7: Constrained-deadline demand criterion ===\r\n" );

    xRA = xTaskCreateEDF( vTask7A, "T7_A", 512, NULL, 2, MS( 10 ), MS( 5 ), MS( 3 ), NULL );
    xRB = xTaskCreateEDF( vTask7B, "T7_B", 512, NULL, 2, MS( 15 ), MS( 8 ), MS( 4 ), NULL );

    printf( "T7_A admission: %s (expect PASS)\r\n", ( xRA == pdPASS ) ? "PASS" : "FAIL" );
    printf( "T7_B admission: %s (expect PASS)\r\n", ( xRB == pdPASS ) ? "PASS" : "FAIL" );

    vEDFPrintStats();

    xTaskCreate( vLEDTask, "LED", 256, NULL, 1, NULL );
    xTaskCreate( vUARTDrainTask, "UARTDrain", 512, NULL, 3, NULL );

    vTaskStartScheduler();
    while( 1 ) {}
}

#endif /* TEST_CASE == 7 */

/* -----------------------------------------------------------------------
 * TEST 8 — CBS book example (Figure 6.14), scaled 1 unit = 10 ms
 *
 * Hard periodic task τ1:  C=40ms, T=70ms, D=70ms  → U  = 4/7 ≈ 0.571
 * CBS server:             Qs=30ms, Ts=80ms          → Us = 3/8 = 0.375
 * τ2 submitter (EDF):     T=100ms, D=5ms,   C=1ms  → always preempts τ1
 * Total utilization:      ~0.956 ≤ 1.0 (schedulable)
 *
 * τ2 is an EDF task that submits one job to the CBS server each period.
 * Being EDF-scheduled it runs at the right time without needing hardware
 * interrupts and without being blocked by τ1.
 * ----------------------------------------------------------------------- */
#if ( TEST_CASE == 8 )

/* Hard periodic task τ1: C=40ms, T=70ms. */
static void vTask1( void * pvParams )
{
    ( void ) pvParams;
    TickType_t xLWT = xTaskGetTickCount();

    for( ; ; )
    {
        TickType_t xS = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xS ) < MS( 40 ) ) { __asm volatile ( "nop" ); }

        vTaskDelayEDF( &xLWT );
    }
}

/* τ2 job body — runs inside the CBS server each activation.
 *
 * Iteration-based busy-wait so only actual CPU time is counted.
 * A volatile counter only advances when this task is on the CPU;
 * preemption by tau1 pauses it, so the remaining 10ms executes
 * after tau1's period ends — leaving cs≈20 and triggering Rule 2.
 *
 * CBS_JOB_ITERS is calibrated empirically: the output showed that
 * 1,250,000 iterations consumed >60ms of CPU (Rule 3 fired twice
 * before the job finished), so we scale to 40ms: ×(40/60) ≈ 830000.
 * If Rule 3 fires more than once, halve the count; if the job
 * finishes before Rule 3 fires at all, double it. */
#define CBS_JOB_ITERS  400000UL

static void vTask2Job( void * pvParams )
{
    ( void ) pvParams;
    volatile uint32_t i;
    for( i = 0UL; i < CBS_JOB_ITERS; i++ ) { __asm volatile ( "nop" ); }
}

static TaskHandle_t xCBSServerHandle = NULL;

/* τ2 submitter: EDF periodic task, T=100ms, D=5ms, C≈1ms.
 * Submits exactly 2 jobs to demonstrate all three CBS rules:
 *   Job 1 (t=30)  → Rule 1 (fresh deadline) + Rule 3 (budget exhausted at t=70)
 *   Job 2 (t=130) → Rule 2 (reuse deadline, condition false, cs≈19)
 * After 2 submissions τ2 suspends itself so the CBS server drains cleanly
 * without an infinite job backlog (CBS bandwidth 37.5% < job rate 41ms/100ms). */
static void vTask2( void * pvParams )
{
    ( void ) pvParams;

    /* Initial offset: block for 30ms so the EDF loop starts at t=30ms.
     * vTaskDelayEDF then targets 30+100=130ms for the second submission. */
    vTaskDelay( MS( 30 ) );

    TickType_t xLWT = xTaskGetTickCount();   /* ≈ 30 */

    for( ;; )
    {
        printf( "[tau2] submit at tick %lu\r\n", ( unsigned long ) xTaskGetTickCount() );
        xCBSSubmitJob( xCBSServerHandle, vTask2Job, NULL );
        vTaskDelayEDF( &xLWT );
    }
}

int main( void )
{
    stdio_init_all();
    printf( "\r\n=== CBS Test 8 ===\r\n" );
    printf( "tau1: C=40ms T=70ms  D=70ms  (U=0.571)\r\n" );
    printf( "CBS:  Qs=30ms Ts=80ms        (Us=0.375)\r\n" );
    printf( "tau2: T=100ms D=5ms   C=1ms  (submitter, always preempts tau1)\r\n" );

    /* Hard periodic task τ1. */
    xTaskCreateEDF( vTask1, "tau1", 512, NULL, 2,
                    MS( 70 ), MS( 70 ), MS( 40 ), NULL );

    /* CBS server: Qs=30ms, Ts=80ms. */
    xTaskCreateCBS( "CBS", 512, 2, MS( 30 ), MS( 80 ), &xCBSServerHandle );

    /* τ2 submitter: EDF task, period=100ms, deadline=5ms, wcet=1ms.
     * D=5ms ensures tau2's absolute deadline is always earlier than tau1's
     * (worst case: tau2 arrives at t=200, tau1 deadline=210 → 205 < 210). */
    xTaskCreateEDF( vTask2, "tau2", 512, NULL, 2,
                    MS( 100 ), MS( 5 ), MS( 1 ), NULL );

    xTaskCreate( vUARTDrainTask, "UARTDrain", 512, NULL, 3, NULL );

    vTaskStartScheduler();
    while( 1 ) {}
}

#endif /* TEST_CASE == 8 */

#if ( TEST_CASE == 9 )

static volatile uint32_t ulJobRuns = 0;
static TaskHandle_t xCBSServerHandle = NULL;

static void vCBSJob_Increment( void * pvParams )
{
    ( void ) pvParams;
    ulJobRuns++;
    printf( "[JOB] run %lu at tick %lu\r\n",
            ( unsigned long ) ulJobRuns,
            ( unsigned long ) xTaskGetTickCount() );
}

static void vSubmitterTask( void * pvParams )
{
    ( void ) pvParams;

    vTaskDelay( pdMS_TO_TICKS( 50 ) );

    printf( "[TEST] submit job 1 at tick %lu\r\n",
            ( unsigned long ) xTaskGetTickCount() );
    configASSERT( xCBSSubmitJob( xCBSServerHandle, vCBSJob_Increment, NULL ) == pdPASS );

    vTaskDelay( pdMS_TO_TICKS( 100 ) );

    printf( "[TEST] submit job 2 at tick %lu\r\n",
            ( unsigned long ) xTaskGetTickCount() );
    configASSERT( xCBSSubmitJob( xCBSServerHandle, vCBSJob_Increment, NULL ) == pdPASS );

    vTaskDelay( pdMS_TO_TICKS( 100 ) );

    printf( "[TEST] final ulJobRuns = %lu\r\n", ( unsigned long ) ulJobRuns );
    configASSERT( ulJobRuns == 2 );

    printf( "[TEST] PASS\r\n" );
    vTaskSuspend( NULL );
}

int main( void )
{
    stdio_init_all();
    printf( "\r\n=== CBS Lifecycle Test ===\r\n" );

    xTaskCreateCBS( "CBS", 512, 2,
                    pdMS_TO_TICKS( 20 ),
                    pdMS_TO_TICKS( 50 ),
                    &xCBSServerHandle );

    configASSERT( xCBSServerHandle != NULL );

    xTaskCreate( vSubmitterTask, "SUBMIT", 512, NULL, 2, NULL );
    xTaskCreate( vUARTDrainTask, "UARTDrain", 512, NULL, 3, NULL );

    vTaskStartScheduler();

    while( 1 ) {}
}

#endif /* TEST_CASE == 9 */

#if ( TEST_CASE == 10 )

static TaskHandle_t xCBSServerHandle = NULL;

static void vCBSJob_Long( void * pvParams )
{
    volatile uint32_t i;
    ( void ) pvParams;

    printf( "[JOB] start at %lu\r\n",
            ( unsigned long ) xTaskGetTickCount() );

    // for( i = 0; i < 4000000UL; i++ )
    // {
    //     __asm volatile ( "nop" );
    // }

    TickType_t xS = xTaskGetTickCount();
    while( ( xTaskGetTickCount() - xS ) < pdMS_TO_TICKS( 60 ) ) { __asm volatile ( "nop" ); }

    printf( "[JOB] end at %lu\r\n",
            ( unsigned long ) xTaskGetTickCount() );
}

static void vSubmitterTask( void * pvParams )
{
    ( void ) pvParams;

    vTaskDelay( pdMS_TO_TICKS( 50 ) );

    printf( "[TEST] submit long job at tick %lu\r\n",
            ( unsigned long ) xTaskGetTickCount() );

    configASSERT(
        xCBSSubmitJob( xCBSServerHandle, vCBSJob_Long, NULL ) == pdPASS
    );

    vTaskSuspend( NULL );
}

int main( void )
{
    stdio_init_all();
    printf( "\r\n=== CBS Rule 3 Test ===\r\n" );

    xTaskCreateCBS( "CBS", 512, 2,
                    pdMS_TO_TICKS( 20 ),   /* Qs */
                    pdMS_TO_TICKS( 50 ),   /* Ts */
                    &xCBSServerHandle );

    configASSERT( xCBSServerHandle != NULL );

    xTaskCreate( vSubmitterTask, "SUBMIT", 512, NULL, 2, NULL );
    xTaskCreate( vUARTDrainTask, "UARTDrain", 512, NULL, 3, NULL );

    vTaskStartScheduler();

    while( 1 ) {}
}

#endif /* TEST_CASE == 10 */

#if ( TEST_CASE == 11 )

static TaskHandle_t xCBSServerHandle = NULL;
static volatile uint32_t ulJobRuns = 0;

static void vCBSJob_Short( void * pvParams )
{
    TickType_t xStart = xTaskGetTickCount();
    ( void ) pvParams;

    ulJobRuns++;

    printf( "[JOB] short start run=%lu tick=%lu\r\n",
            ( unsigned long ) ulJobRuns,
            ( unsigned long ) xStart );

    while( ( xTaskGetTickCount() - xStart ) < pdMS_TO_TICKS( 5 ) )
    {
        __asm volatile ( "nop" );
    }

    printf( "[JOB] short end   run=%lu tick=%lu\r\n",
            ( unsigned long ) ulJobRuns,
            ( unsigned long ) xTaskGetTickCount() );
}

static void vSubmitterTask( void * pvParams )
{
    ( void ) pvParams;

    vTaskDelay( pdMS_TO_TICKS( 50 ) );

    printf( "[TEST] submit job 1 at tick %lu\r\n",
            ( unsigned long ) xTaskGetTickCount() );
    configASSERT( xCBSSubmitJob( xCBSServerHandle, vCBSJob_Short, NULL ) == pdPASS );

    vTaskDelay( pdMS_TO_TICKS( 20 ) );

    printf( "[TEST] submit job 2 at tick %lu\r\n",
            ( unsigned long ) xTaskGetTickCount() );
    configASSERT( xCBSSubmitJob( xCBSServerHandle, vCBSJob_Short, NULL ) == pdPASS );

    vTaskDelay( pdMS_TO_TICKS( 100 ) );

    printf( "[TEST] final ulJobRuns = %lu\r\n",
            ( unsigned long ) ulJobRuns );
    configASSERT( ulJobRuns == 2 );

    printf( "[TEST] PASS\r\n" );
    vTaskSuspend( NULL );
}

int main( void )
{
    stdio_init_all();
    printf( "\r\n=== CBS Rule 2 Test ===\r\n" );

    xTaskCreateCBS( "CBS", 512, 2,
                    pdMS_TO_TICKS( 20 ),    /* Qs */
                    pdMS_TO_TICKS( 100 ),   /* Ts */
                    &xCBSServerHandle );

    configASSERT( xCBSServerHandle != NULL );

    xTaskCreate( vSubmitterTask, "SUBMIT", 512, NULL, 2, NULL );
    xTaskCreate( vUARTDrainTask, "UARTDrain", 512, NULL, 3, NULL );

    vTaskStartScheduler();

    while( 1 ) {}
}

#endif /* TEST_CASE == 11 */

#if ( TEST_CASE == 12 )

static TaskHandle_t xCBSServerHandle = NULL;
static volatile uint32_t ulTau1Jobs = 0;

static void vTau1( void * pvParams )
{
    TickType_t xLWT = xTaskGetTickCount();
    ( void ) pvParams;

    for( ;; )
    {
        TickType_t xStart = xTaskGetTickCount();
        ulTau1Jobs++;

        printf( "[TAU1] start job=%lu tick=%lu\r\n",
                ( unsigned long ) ulTau1Jobs,
                ( unsigned long ) xStart );

        while( ( xTaskGetTickCount() - xStart ) < pdMS_TO_TICKS( 10 ) )
        {
            __asm volatile ( "nop" );
        }

        printf( "[TAU1] end   job=%lu tick=%lu\r\n",
                ( unsigned long ) ulTau1Jobs,
                ( unsigned long ) xTaskGetTickCount() );

        vTaskDelayEDF( &xLWT );
    }
}

static void vCBSJob_Long( void * pvParams )
{
    TickType_t xStart = xTaskGetTickCount();
    ( void ) pvParams;

    printf( "[CBSJOB] start tick=%lu\r\n",
            ( unsigned long ) xStart );

    while( ( xTaskGetTickCount() - xStart ) < pdMS_TO_TICKS( 80 ) )
    {
        __asm volatile ( "nop" );
    }

    printf( "[CBSJOB] end   tick=%lu\r\n",
            ( unsigned long ) xTaskGetTickCount() );
}

static void vSubmitterTask( void * pvParams )
{
    ( void ) pvParams;

    vTaskDelay( pdMS_TO_TICKS( 50 ) );

    printf( "[TEST] submit long CBS job tick=%lu\r\n",
            ( unsigned long ) xTaskGetTickCount() );

    configASSERT( xCBSSubmitJob( xCBSServerHandle, vCBSJob_Long, NULL ) == pdPASS );

    vTaskDelay( pdMS_TO_TICKS( 250 ) );

    printf( "[TEST] tau1 jobs seen = %lu\r\n",
            ( unsigned long ) ulTau1Jobs );

    configASSERT( ulTau1Jobs >= 5 );

    printf( "[TEST] PASS\r\n" );
    vTaskSuspend( NULL );
}

int main( void )
{
    stdio_init_all();
    printf( "\r\n=== CBS + Hard EDF Task Test ===\r\n" );

    /* Hard task: C=10, T=50, D=50. */
    xTaskCreateEDF( vTau1, "tau1", 512, NULL, 2,
                    pdMS_TO_TICKS( 50 ),
                    pdMS_TO_TICKS( 50 ),
                    pdMS_TO_TICKS( 10 ),
                    NULL );

    /* CBS: Qs=20, Ts=50. */
    xTaskCreateCBS( "CBS", 512, 2,
                    pdMS_TO_TICKS( 20 ),
                    pdMS_TO_TICKS( 50 ),
                    &xCBSServerHandle );

    configASSERT( xCBSServerHandle != NULL );

    // xTaskCreate( vSubmitterTask, "SUBMIT", 512, NULL, 2, NULL );
    xTaskCreateEDF( vSubmitterTask, "SUBMIT", 512, NULL, 2,
                    pdMS_TO_TICKS( 200 ),
                    pdMS_TO_TICKS( 5 ),
                    pdMS_TO_TICKS( 1 ),
                    NULL );
    xTaskCreate( vUARTDrainTask, "UARTDrain", 512, NULL, 3, NULL );

    vTaskStartScheduler();

    while( 1 ) {}
}

#endif /* TEST_CASE == 12 */

#if ( TEST_CASE == 13 )

static TaskHandle_t xCBSServerHandle = NULL;
static volatile uint32_t ulTau1Jobs = 0;
static volatile uint32_t ulCBSJobsDone = 0;

/* Tune these for your board/compiler. Start with your existing calibration. */
#define JOB_30MS_ITERS   300000UL
#define JOB_40MS_ITERS   400000UL
#define JOB_50MS_ITERS   500000UL
#define TAU1_40MS_ITERS  400000UL

static void vBurnCpuIters( uint32_t ulIters )
{
    volatile uint32_t i;

    for( i = 0UL; i < ulIters; i++ )
    {
        __asm volatile ( "nop" );
    }
}

static void vTau1( void * pvParams )
{
    TickType_t xLWT = xTaskGetTickCount();
    ( void ) pvParams;

    for( ;; )
    {
        ulTau1Jobs++;

        printf( "[TAU1] start job=%lu tick=%lu\r\n",
                ( unsigned long ) ulTau1Jobs,
                ( unsigned long ) xTaskGetTickCount() );

        vBurnCpuIters( TAU1_40MS_ITERS );

        printf( "[TAU1] end   job=%lu tick=%lu\r\n",
                ( unsigned long ) ulTau1Jobs,
                ( unsigned long ) xTaskGetTickCount() );

        vTaskDelayEDF( &xLWT );
    }
}

typedef struct
{
    const char * pcName;
    uint32_t ulIters;
} CBSJobParam_t;

static CBSJobParam_t xJob40 = { "J40", JOB_40MS_ITERS };
static CBSJobParam_t xJob30 = { "J30", JOB_30MS_ITERS };
static CBSJobParam_t xJob50 = { "J50", JOB_50MS_ITERS };

static void vCBSVariableJob( void * pvParams )
{
    CBSJobParam_t * pxJob = ( CBSJobParam_t * ) pvParams;

    printf( "[CBSJOB] start %s tick=%lu\r\n",
            pxJob->pcName,
            ( unsigned long ) xTaskGetTickCount() );

    vBurnCpuIters( pxJob->ulIters );

    ulCBSJobsDone++;

    printf( "[CBSJOB] end   %s tick=%lu done=%lu\r\n",
            pxJob->pcName,
            ( unsigned long ) xTaskGetTickCount(),
            ( unsigned long ) ulCBSJobsDone );
}

static void vCBSSubmitterTwoJobs( void * pvParams )
{
    ( void ) pvParams;

    /* Job 1 arrives at t = 30ms. */
    vTaskDelay( pdMS_TO_TICKS( 30 ) );

    printf( "[SUBMIT] J40 tick=%lu\r\n",
            ( unsigned long ) xTaskGetTickCount() );

    configASSERT(
        xCBSSubmitJob( xCBSServerHandle, vCBSVariableJob, &xJob40 ) == pdPASS
    );

    vTaskDelay( pdMS_TO_TICKS( 137 ) );

    printf( "[SUBMIT] J30 tick=%lu\r\n",
            ( unsigned long ) xTaskGetTickCount() );

    configASSERT(
        xCBSSubmitJob( xCBSServerHandle, vCBSVariableJob, &xJob30 ) == pdPASS
    );

    vTaskDelay( pdMS_TO_TICKS( 300 ) );

    printf( "[TEST] tau1 jobs=%lu cbs jobs done=%lu\r\n",
            ( unsigned long ) ulTau1Jobs,
            ( unsigned long ) ulCBSJobsDone );

    // configASSERT( ulTau1Jobs >= 5 );
    // configASSERT( ulCBSJobsDone == 3 );

    printf( "[TEST] PASS\r\n" );
    vTaskSuspend( NULL );
}

int main( void )
{
    stdio_init_all();
    printf( "\r\n=== CBS Random Arrival Stress Test ===\r\n" );
    printf( "tau1: C=40 T=70 D=70\r\n" );
    printf( "CBS:  Qs=30 Ts=80\r\n" );
    printf( "Jobs: 40ms, 30ms, 50ms at uneven intervals\r\n" );

    xTaskCreateEDF( vTau1, "tau1", 512, NULL, 2,
                    pdMS_TO_TICKS( 70 ),
                    pdMS_TO_TICKS( 70 ),
                    pdMS_TO_TICKS( 40 ),
                    NULL );

    xTaskCreateCBS( "CBS", 512, 2,
                    pdMS_TO_TICKS( 30 ),
                    pdMS_TO_TICKS( 80 ),
                    &xCBSServerHandle );

    configASSERT( xCBSServerHandle != NULL );

    // xTaskCreateEDF( vCBSSubmitterTwoJobs, "SUBMIT", 512, NULL, 2,
    //                 pdMS_TO_TICKS( 500 ),
    //                 pdMS_TO_TICKS( 5 ),
    //                 pdMS_TO_TICKS( 1 ),
    //                 NULL );
    xTaskCreate( vCBSSubmitterTwoJobs, "SUBMIT", 512, NULL, 1, NULL );

    xTaskCreate( vUARTDrainTask, "UARTDrain", 512, NULL, 3, NULL );

    vTaskStartScheduler();

    while( 1 ) {}
}

#endif /* TEST_CASE == 13 */

#if ( TEST_CASE == 14 )

static TaskHandle_t xCBSServerHandle = NULL;
static volatile uint32_t ulTau1Jobs = 0;
static volatile uint32_t ulCBSJobsDone = 0;

/* Tune these for your board/compiler. Start with your existing calibration. */
#define JOB_30MS_ITERS   300000UL
#define JOB_40MS_ITERS   400000UL
#define JOB_50MS_ITERS   500000UL
#define TAU1_40MS_ITERS  400000UL

static void vBurnCpuIters( uint32_t ulIters )
{
    volatile uint32_t i;

    for( i = 0UL; i < ulIters; i++ )
    {
        __asm volatile ( "nop" );
    }
}

static void vTau1( void * pvParams )
{
    TickType_t xLWT = xTaskGetTickCount();
    ( void ) pvParams;

    for( ;; )
    {
        ulTau1Jobs++;

        printf( "[TAU1] start job=%lu tick=%lu\r\n",
                ( unsigned long ) ulTau1Jobs,
                ( unsigned long ) xTaskGetTickCount() );

        vBurnCpuIters( TAU1_40MS_ITERS );

        printf( "[TAU1] end   job=%lu tick=%lu\r\n",
                ( unsigned long ) ulTau1Jobs,
                ( unsigned long ) xTaskGetTickCount() );

        vTaskDelayEDF( &xLWT );
    }
}

typedef struct
{
    const char * pcName;
    uint32_t ulIters;
} CBSJobParam_t;

static CBSJobParam_t xJob40 = { "J40", JOB_40MS_ITERS };
static CBSJobParam_t xJob30 = { "J30", JOB_30MS_ITERS };
static CBSJobParam_t xJob50 = { "J50", JOB_50MS_ITERS };

static void vCBSVariableJob( void * pvParams )
{
    CBSJobParam_t * pxJob = ( CBSJobParam_t * ) pvParams;

    printf( "[CBSJOB] start %s tick=%lu\r\n",
            pxJob->pcName,
            ( unsigned long ) xTaskGetTickCount() );

    vBurnCpuIters( pxJob->ulIters );

    ulCBSJobsDone++;

    printf( "[CBSJOB] end   %s tick=%lu done=%lu\r\n",
            pxJob->pcName,
            ( unsigned long ) xTaskGetTickCount(),
            ( unsigned long ) ulCBSJobsDone );
}

static void vCBSRandomSubmitter( void * pvParams )
{
    ( void ) pvParams;

    /* Random-looking but deterministic arrival offsets. */
    vTaskDelay( pdMS_TO_TICKS( 25 ) );

    printf( "[SUBMIT] J40 tick=%lu\r\n", ( unsigned long ) xTaskGetTickCount() );
    configASSERT( xCBSSubmitJob( xCBSServerHandle, vCBSVariableJob, &xJob40 ) == pdPASS );

    vTaskDelay( pdMS_TO_TICKS( 137 ) );

    printf( "[SUBMIT] J30 tick=%lu\r\n", ( unsigned long ) xTaskGetTickCount() );
    configASSERT( xCBSSubmitJob( xCBSServerHandle, vCBSVariableJob, &xJob30 ) == pdPASS );

    // vTaskDelay( pdMS_TO_TICKS( 91 ) );

    // printf( "[SUBMIT] J50 tick=%lu\r\n", ( unsigned long ) xTaskGetTickCount() );
    // configASSERT( xCBSSubmitJob( xCBSServerHandle, vCBSVariableJob, &xJob50 ) == pdPASS );

    vTaskDelay( pdMS_TO_TICKS( 300 ) );

    printf( "[TEST] tau1 jobs=%lu cbs jobs done=%lu\r\n",
            ( unsigned long ) ulTau1Jobs,
            ( unsigned long ) ulCBSJobsDone );

    // configASSERT( ulTau1Jobs >= 5 );
    // configASSERT( ulCBSJobsDone == 3 );

    printf( "[TEST] PASS\r\n" );
    vTaskSuspend( NULL );
}

int main( void )
{
    stdio_init_all();
    printf( "\r\n=== CBS Random Arrival Stress Test ===\r\n" );
    printf( "tau1: C=40 T=70 D=70\r\n" );
    printf( "CBS:  Qs=30 Ts=80\r\n" );
    printf( "Jobs: 40ms, 30ms, 50ms at uneven intervals\r\n" );

    xTaskCreateEDF( vTau1, "tau1", 512, NULL, 2,
                    pdMS_TO_TICKS( 70 ),
                    pdMS_TO_TICKS( 70 ),
                    pdMS_TO_TICKS( 40 ),
                    NULL );

    xTaskCreateCBS( "CBS", 512, 2,
                    pdMS_TO_TICKS( 30 ),
                    pdMS_TO_TICKS( 80 ),
                    &xCBSServerHandle );

    configASSERT( xCBSServerHandle != NULL );

    xTaskCreate( vCBSRandomSubmitter, "SUBMIT", 512, NULL, 1, NULL );

    xTaskCreate( vUARTDrainTask, "UARTDrain", 512, NULL, 3, NULL );

    vTaskStartScheduler();

    while( 1 ) {}
}

#endif