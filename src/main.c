#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <stdbool.h>
#include "pico/stdlib.h"

/* Select which test to run (1–7). */
#ifndef TEST_CASE
    #define TEST_CASE  2
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
    TickType_t xLastWakeTime = 0;

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
    stdio_init_all();
    printf( "\r\n=== EDF Test 1: Single task, U=0.5 ===\r\n" );

    xTaskCreateEDF( vTask1, "T1_Half", 512, NULL, 2,
                    MS( 200 ), MS( 200 ), MS( 100 ), NULL );

    xTaskCreate( vLEDTask, "LED", 256, NULL, 1, NULL );
    xTaskCreate( vUARTDrainTask, "UARTDrain", 512, NULL, 3, NULL );

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
    TickType_t xLWT = 0;
    for( ;; )
    {
        TickType_t xS = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xS ) < MS( 45 ) ) { __asm volatile ( "nop" ); }
        vTaskDelayEDF( &xLWT );
    }
}

static void vTaskB( void * pvParams )
{
    ( void ) pvParams;
    TickType_t xLWT = 0;
    for( ;; )
    {
        TickType_t xS = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xS ) < MS( 95 ) ) { __asm volatile ( "nop" ); }
        vTaskDelayEDF( &xLWT );
    }
}

int main( void )
{
    stdio_init_all();
    printf( "\r\n=== EDF Test 2: Two tasks at LL boundary (U=1.0) ===\r\n" );

    xTaskCreateEDF( vTaskA, "T2_A", 512, NULL, 2, MS( 100 ), MS( 100 ), MS( 45 ),  NULL );
    xTaskCreateEDF( vTaskB, "T2_B", 512, NULL, 2, MS( 200 ), MS( 200 ), MS( 95 ), NULL );
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
    TickType_t xLWT = 0;
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
    TickType_t xLWT = 0;
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
    TickType_t xLWT = 0;
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
    TickType_t xLWT = 0;
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
    TickType_t xLWT = 0;
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
    TickType_t xLWT = 0;
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
    TickType_t xLWT = 0;
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
    TickType_t xLWT = 0;
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
