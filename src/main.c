#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <stdio.h>
#include <stdbool.h>
#include "pico/stdlib.h"

#ifndef TEST_CASE
    #define TEST_CASE 28
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
        #if ( configUSE_SRP == 1 )
            vSRPDrainEventLog();
        #endif
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
                    MS( 200 ), MS( 200 ), MS( 100 ), 0, NULL );

    xTaskCreate( vLEDTask, "LED", 256, NULL, 1, NULL );
    xTaskCreate( vUARTDrainTask, "UARTDrain", 512, NULL, 1, NULL );

    vTaskStartScheduler();
    while( 1 ) {}
}

#endif /* TEST_CASE == 1 */

/* -----------------------------------------------------------------------
 * TEST 2 — Two implicit-deadline tasks near the LL boundary
 *   Task A: T=100, D=100, C=45  → U=0.45
 *   Task B: T=200, D=200, C=95  → U=0.475
 *   Total U = 0.925
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
    printf( "\r\n=== EDF Test 2: Two tasks near LL boundary (U=0.925) ===\r\n" );

    xTaskCreateEDF( vTaskA, "T2_A", 512, NULL, 2, MS( 100 ), MS( 100 ), MS( 45 ),  0, NULL );
    xTaskCreateEDF( vTaskB, "T2_B", 512, NULL, 2, MS( 200 ), MS( 200 ), MS( 95 ), 0, NULL );
    xTaskCreate( vLEDTask, "LED", 256, NULL, 1, NULL );
    xTaskCreate( vUARTDrainTask, "UARTDrain", 512, NULL, 1, NULL );

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
                    MS( 100 ), MS( 100 ), MS( 30 ), 0, NULL );

    /* Non-EDF task at priority 1 (same numeric priority as EDF tasks, but
     * the scheduler picks EDF tasks from xEDFReadyTasksList first). */
    xTaskCreate( vBackgroundTask, "T3_BG",  512, NULL, 1, NULL );
    xTaskCreate( vLEDTask, "LED", 256, NULL, 1, NULL );
    xTaskCreate( vUARTDrainTask, "UARTDrain", 512, NULL, 1, NULL );

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
                               MS( 100 ), MS( 100 ), MS( 60 ), 0, NULL );
    printf( "T4_A create result: %s\r\n", ( xResult == pdPASS ) ? "PASS" : "FAIL" );

    /* Task B: would push total U to 1.10 — should be rejected. */
    xResult = xTaskCreateEDF( vTaskAccepted, "T4_B", 512, NULL, 2,
                               MS( 200 ), MS( 200 ), MS( 100 ), 0, NULL );
    printf( "T4_B create result (expect FAIL): %s\r\n", ( xResult == pdPASS ) ? "PASS" : "FAIL" );

    xTaskCreate( vLEDTask, "LED", 256, NULL, 1, NULL );
    xTaskCreate( vUARTDrainTask, "UARTDrain", 512, NULL, 1, NULL );

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
                    MS( 200 ), MS( 200 ), MS( 50 ), 0, NULL );

    xTaskCreate( vLEDTask, "LED", 256, NULL, 1, NULL );
    xTaskCreate( vUARTDrainTask, "UARTDrain", 512, NULL, 1, NULL );

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

    xTaskCreateEDF( vTask6A, "T6_A", 512, NULL, 2, MS( 300 ), MS( 100 ), MS( 20 ), 0, NULL );
    xTaskCreateEDF( vTask6B, "T6_B", 512, NULL, 2, MS( 200 ), MS( 150 ), MS( 30 ), 0, NULL );
    xTaskCreateEDF( vTask6C, "T6_C", 512, NULL, 2, MS( 400 ), MS( 200 ), MS( 40 ), 0, NULL );

    xTaskCreate( vLEDTask, "LED", 256, NULL, 1, NULL );
    xTaskCreate( vUARTDrainTask, "UARTDrain", 512, NULL, 1, NULL );

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

    xRA = xTaskCreateEDF( vTask7A, "T7_A", 512, NULL, 2, MS( 10 ), MS( 5 ), MS( 3 ), 0, NULL );
    xRB = xTaskCreateEDF( vTask7B, "T7_B", 512, NULL, 2, MS( 15 ), MS( 8 ), MS( 4 ), 0, NULL );

    printf( "T7_A admission: %s (expect PASS)\r\n", ( xRA == pdPASS ) ? "PASS" : "FAIL" );
    printf( "T7_B admission: %s (expect PASS)\r\n", ( xRB == pdPASS ) ? "PASS" : "FAIL" );

    vEDFPrintStats();

    xTaskCreate( vLEDTask, "LED", 256, NULL, 1, NULL );
    xTaskCreate( vUARTDrainTask, "UARTDrain", 512, NULL, 1, NULL );

    vTaskStartScheduler();
    while( 1 ) {}
}

#endif /* TEST_CASE == 7 */

/* -----------------------------------------------------------------------
 * TEST 8 — SRP blocking: two EDF tasks sharing one mutex
 *
 *   Task H: T=200, D=200, C=30   (work 10, lock R 10, work 10)
 *   Task L: T=600, D=600, C=280  (work 80, lock R 150, work remaining)
 *   R ceiling = min(200, 600) = 200
 *   U = 30/200 + 280/600 = 0.15 + 0.467 = 0.617
 *
 *   L locks R at ~t=80, holds until ~t=230. At t=200 H's 2nd job arrives
 *   but ceiling=200, H's preemption level=200, 200 < 200 is FALSE so
 *   H is SRP-blocked until L unlocks at ~t=230.
 *
 *   Expected: SRP log shows lock/unlock, H delayed ~30ms, zero misses.
 * ----------------------------------------------------------------------- */
#if ( TEST_CASE == 8 )

static SemaphoreHandle_t xMutexR;

static void vTaskH( void * pvParams )
{
    ( void ) pvParams;
    TickType_t xLWT = 0;

    for( ;; )
    {
        TickType_t xS = xTaskGetTickCount();

        /* Work 10 ms, lock R for 10 ms, work 10 ms = 30 ms total */
        while( ( xTaskGetTickCount() - xS ) < MS( 10 ) ) { __asm volatile ( "nop" ); }

        xSemaphoreTake( xMutexR, portMAX_DELAY );
        {
            TickType_t xCS = xTaskGetTickCount();
            while( ( xTaskGetTickCount() - xCS ) < MS( 10 ) ) { __asm volatile ( "nop" ); }
        }
        xSemaphoreGive( xMutexR );

        while( ( xTaskGetTickCount() - xS ) < MS( 30 ) ) { __asm volatile ( "nop" ); }

        vTaskDelayEDF( &xLWT );
    }
}

static void vTaskL( void * pvParams )
{
    ( void ) pvParams;
    TickType_t xLWT = 0;

    for( ;; )
    {
        TickType_t xS = xTaskGetTickCount();

        /* Work 80 ms before locking */
        while( ( xTaskGetTickCount() - xS ) < MS( 80 ) ) { __asm volatile ( "nop" ); }

        /* Lock R for 150 ms — this will span H's period boundary */
        xSemaphoreTake( xMutexR, portMAX_DELAY );
        {
            TickType_t xCS = xTaskGetTickCount();
            while( ( xTaskGetTickCount() - xCS ) < MS( 150 ) ) { __asm volatile ( "nop" ); }
        }
        xSemaphoreGive( xMutexR );

        /* Remaining work until C=280 ms total */
        while( ( xTaskGetTickCount() - xS ) < MS( 280 ) ) { __asm volatile ( "nop" ); }

        vTaskDelayEDF( &xLWT );
    }
}

int main( void )
{
    stdio_init_all();
    printf( "\r\n=== SRP Test 8: SRP blocking demonstration ===\r\n" );
    printf( "H: T=200,D=200,C=30  L: T=600,D=600,C=280  R ceiling=200\r\n" );
    printf( "L holds R across H's period boundary → H is SRP-blocked\r\n\r\n" );

    /* Create the shared mutex and set its SRP ceiling */
    xMutexR = xSemaphoreCreateBinary();
    configASSERT( xMutexR );
    xSemaphoreGive( xMutexR );  /* Binary semaphores start empty — make it available */
    vSemaphoreSetResourceCeiling( xMutexR, MS( 200 ) );

    xTaskCreateEDF( vTaskH, "T8_H", 512, NULL, 2,
                    MS( 200 ), MS( 200 ), MS( 30 ), 0, NULL );
    xTaskCreateEDF( vTaskL, "T8_L", 512, NULL, 2,
                    MS( 600 ), MS( 600 ), MS( 280 ), 0, NULL );

    xTaskCreate( vLEDTask, "LED", 256, NULL, 1, NULL );
    xTaskCreate( vUARTDrainTask, "UARTDrain", 512, NULL, 1, NULL );

    vTaskStartScheduler();
    while( 1 ) {}
}

#endif /* TEST_CASE == 8 */

/* -----------------------------------------------------------------------
 * TEST 9 — 100 implicit-deadline tasks (LL bound admission)
 * TEST 10 — 100 constrained-deadline tasks (processor demand admission)
 *
 *   Both tests create 100 tasks with T=1000 ms, C=9 ms (U_i = 0.009).
 *   Test 9:  D=1000 (implicit) → LL bound: U_total = 0.9 ≤ 1.0 → all admitted.
 *   Test 10: D=500  (constrained) → processor demand: at t=500,
 *            h(500) = n×9. Fails when n > 55 (504 > 500). ~55 admitted.
 *
 *   This demonstrates:
 *   1. LL bound admits all 100 tasks (utilization sufficient).
 *   2. Processor demand rejects tasks earlier due to tighter deadlines.
 *   3. Processor demand is computationally more expensive (timing shown).
 * ----------------------------------------------------------------------- */
#if ( TEST_CASE == 9 ) || ( TEST_CASE == 10 )

static void vGenericEDFTask( void * pvParams )
{
    ( void ) pvParams;
    TickType_t xLWT = 0;

    for( ;; )
    {
        TickType_t xS = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xS ) < MS( 9 ) ) { __asm volatile ( "nop" ); }
        vTaskDelayEDF( &xLWT );
    }
}

int main( void )
{
    stdio_init_all();

    #if ( TEST_CASE == 9 )
        const TickType_t xDeadline = MS( 1000 );  /* implicit: D = T */
        printf( "\r\n=== EDF Test 9: 100 implicit-deadline tasks (LL bound) ===\r\n" );
        printf( "Each task: T=1000, D=1000, C=9 → U_i=0.009\r\n\r\n" );
    #else
        const TickType_t xDeadline = MS( 500 );   /* constrained: D < T */
        printf( "\r\n=== EDF Test 10: 100 constrained-deadline tasks (processor demand) ===\r\n" );
        printf( "Each task: T=1000, D=500, C=9 → U_i=0.009\r\n\r\n" );
    #endif

    UBaseType_t uxAdmitted = 0;
    uint32_t xStartUs = time_us_32();

    for( int i = 0; i < 100; i++ )
    {
        char cName[ configMAX_TASK_NAME_LEN ];
        snprintf( cName, sizeof( cName ), "T_%02d", i );

        BaseType_t xResult = xTaskCreateEDF( vGenericEDFTask, cName, 256, NULL, 2,
                                              MS( 1000 ), xDeadline, MS( 9 ), 0, NULL );

        if( xResult == pdPASS )
        {
            uxAdmitted++;
            /* Print running utilization: U = admitted * C / T, scaled by 10000 */
            uint32_t ulUtil = ( uint32_t ) uxAdmitted * 9 * 10000 / 1000;
            printf( "  [%3d] ADMIT  U=%lu.%02lu%%\r\n", i,
                    ( unsigned long ) ( ulUtil / 100 ),
                    ( unsigned long ) ( ulUtil % 100 ) );
        }
        else
        {
            if( uxAdmitted == ( UBaseType_t ) i )
            {
                /* First rejection — print where it stopped */
                uint32_t ulUtil = ( uint32_t ) uxAdmitted * 9 * 10000 / 1000;
                printf( "  [%3d] REJECT (first) U=%lu.%02lu%%\r\n", i,
                        ( unsigned long ) ( ulUtil / 100 ),
                        ( unsigned long ) ( ulUtil % 100 ) );
            }
        }
    }

    uint32_t xElapsedUs = time_us_32() - xStartUs;

    printf( "\r\nAdmitted: %lu / 100\r\n", ( unsigned long ) uxAdmitted );
    printf( "Total admission time: %lu us\r\n\r\n", ( unsigned long ) xElapsedUs );

    xTaskCreate( vLEDTask, "LED", 256, NULL, 1, NULL );
    xTaskCreate( vUARTDrainTask, "UARTDrain", 512, NULL, 1, NULL );

    vTaskStartScheduler();
    while( 1 ) {}
}

#endif /* TEST_CASE == 9 || TEST_CASE == 10 */

/* -----------------------------------------------------------------------
 * TEST 13 — Nested resources: ceiling stack push/pop correctness
 *
 *   Task H: T=200, D=200, C=30   (uses R1 briefly)
 *   Task L: T=600, D=600, C=300  (uses R2, then R1 nested inside R2)
 *   R1 ceiling = min(200, 600) = 200,  R2 ceiling = 600
 *
 *   L takes R2 at ~t=80 (ceil=600), nests R1 at ~t=180 (ceil=200).
 *   At t=200 H arrives but 200 < 200 is FALSE so H is SRP-blocked.
 *   L releases R1 at ~t=280 (ceil back to 600), H preempts.
 *   SRP log should show push(600), push(200), pop(200), pop(600).
 * ----------------------------------------------------------------------- */
#if ( TEST_CASE == 13 )

static SemaphoreHandle_t xSemR1;
static SemaphoreHandle_t xSemR2;

static void vTask13H( void * pvParams )
{
    ( void ) pvParams;
    TickType_t xLWT = 0;

    for( ;; )
    {
        TickType_t xS = xTaskGetTickCount();
        printf( "[T13_H] job START at tick %lu\r\n", ( unsigned long ) xS );

        /* Work 10 ms, lock R1 for 10 ms, work 10 ms = 30 ms total */
        while( ( xTaskGetTickCount() - xS ) < MS( 10 ) ) { __asm volatile ( "nop" ); }

        printf( "[T13_H] taking R1 at tick %lu\r\n",
                ( unsigned long ) xTaskGetTickCount() );
        xSemaphoreTake( xSemR1, portMAX_DELAY );
        printf( "[T13_H] got R1 at tick %lu\r\n",
                ( unsigned long ) xTaskGetTickCount() );
        {
            TickType_t xCS = xTaskGetTickCount();
            while( ( xTaskGetTickCount() - xCS ) < MS( 10 ) ) { __asm volatile ( "nop" ); }
        }
        xSemaphoreGive( xSemR1 );
        printf( "[T13_H] released R1 at tick %lu\r\n",
                ( unsigned long ) xTaskGetTickCount() );

        while( ( xTaskGetTickCount() - xS ) < MS( 30 ) ) { __asm volatile ( "nop" ); }

        printf( "[T13_H] job DONE at tick %lu\r\n",
                ( unsigned long ) xTaskGetTickCount() );

        vTaskDelayEDF( &xLWT );
    }
}

static void vTask13L( void * pvParams )
{
    ( void ) pvParams;
    TickType_t xLWT = 0;

    for( ;; )
    {
        TickType_t xS = xTaskGetTickCount();
        printf( "[T13_L] job START at tick %lu\r\n", ( unsigned long ) xS );

        /* Work 50 ms before taking R2 */
        while( ( xTaskGetTickCount() - xS ) < MS( 50 ) ) { __asm volatile ( "nop" ); }

        printf( "[T13_L] taking R2 at tick %lu\r\n",
                ( unsigned long ) xTaskGetTickCount() );
        xSemaphoreTake( xSemR2, portMAX_DELAY );
        printf( "[T13_L] got R2 at tick %lu\r\n",
                ( unsigned long ) xTaskGetTickCount() );
        {
            /* Work 100 ms inside R2 before nesting R1 */
            TickType_t xCS2 = xTaskGetTickCount();
            while( ( xTaskGetTickCount() - xCS2 ) < MS( 100 ) ) { __asm volatile ( "nop" ); }

            /* Take R1 (nested inside R2) — ceiling becomes 200 */
            printf( "[T13_L] taking R1 (nested) at tick %lu\r\n",
                    ( unsigned long ) xTaskGetTickCount() );
            xSemaphoreTake( xSemR1, portMAX_DELAY );
            printf( "[T13_L] got R1 at tick %lu\r\n",
                    ( unsigned long ) xTaskGetTickCount() );
            {
                TickType_t xCS1 = xTaskGetTickCount();
                while( ( xTaskGetTickCount() - xCS1 ) < MS( 100 ) ) { __asm volatile ( "nop" ); }
            }
            xSemaphoreGive( xSemR1 );
            printf( "[T13_L] released R1 at tick %lu\r\n",
                    ( unsigned long ) xTaskGetTickCount() );
            /* ceiling back to 600 — H can preempt now */

            /* Work 50 ms more inside R2 after releasing R1 */
            TickType_t xCS2b = xTaskGetTickCount();
            while( ( xTaskGetTickCount() - xCS2b ) < MS( 50 ) ) { __asm volatile ( "nop" ); }
        }
        xSemaphoreGive( xSemR2 );
        printf( "[T13_L] released R2 at tick %lu\r\n",
                ( unsigned long ) xTaskGetTickCount() );

        /* Remaining work */
        while( ( xTaskGetTickCount() - xS ) < MS( 300 ) ) { __asm volatile ( "nop" ); }

        printf( "[T13_L] job DONE at tick %lu\r\n",
                ( unsigned long ) xTaskGetTickCount() );

        vTaskDelayEDF( &xLWT );
    }
}

int main( void )
{
    stdio_init_all();
    printf( "\r\n=== SRP Test 13: Nested resources ===\r\n" );
    printf( "H: T=200,D=200,C=30 (uses R1)\r\n" );
    printf( "L: T=600,D=600,C=300 (uses R2, then R1 nested inside R2)\r\n" );
    printf( "R1 ceiling=200, R2 ceiling=600\r\n\r\n" );

    xSemR1 = xSemaphoreCreateBinary();
    xSemR2 = xSemaphoreCreateBinary();
    configASSERT( xSemR1 );
    configASSERT( xSemR2 );
    xSemaphoreGive( xSemR1 );
    xSemaphoreGive( xSemR2 );

    vSemaphoreSetResourceCeiling( xSemR1, MS( 200 ) );
    vSemaphoreSetResourceCeiling( xSemR2, MS( 600 ) );

    xTaskCreateEDF( vTask13H, "T13_H", 512, NULL, 2,
                    MS( 200 ), MS( 200 ), MS( 30 ), 0, NULL );
    xTaskCreateEDF( vTask13L, "T13_L", 512, NULL, 2,
                    MS( 600 ), MS( 600 ), MS( 300 ), 0, NULL );

    xTaskCreate( vLEDTask, "LED", 256, NULL, 1, NULL );
    xTaskCreate( vUARTDrainTask, "UARTDrain", 512, NULL, 1, NULL );

    vTaskStartScheduler();
    while( 1 ) {}
}

#endif /* TEST_CASE == 13 */

/* -----------------------------------------------------------------------
 * TEST 14 — SRP prevents deadlock (opposite lock order)
 *
 *   Task A: T=300, D=300, C=60   (takes R1 then R2)
 *   Task B: T=900, D=900, C=500  (takes R2 then R1 — opposite order)
 *   R1 ceiling = min(300, 900) = 300,  R2 ceiling = 300
 *
 *   Without SRP this deadlocks. With SRP, when B holds R2 the ceiling
 *   is 300. A's preemption level is 300, so 300 < 300 is FALSE and A
 *   is blocked until B releases everything. No deadlock possible.
 * ----------------------------------------------------------------------- */
#if ( TEST_CASE == 14 )

static SemaphoreHandle_t xSemR1;
static SemaphoreHandle_t xSemR2;

static void vTask14A( void * pvParams )
{
    ( void ) pvParams;
    TickType_t xLWT = 0;

    for( ;; )
    {
        TickType_t xS = xTaskGetTickCount();

        /* Work 10 ms */
        while( ( xTaskGetTickCount() - xS ) < MS( 10 ) ) { __asm volatile ( "nop" ); }

        /* Take R1 first, then R2 (nested) */
        xSemaphoreTake( xSemR1, portMAX_DELAY );
        {
            xSemaphoreTake( xSemR2, portMAX_DELAY );
            {
                TickType_t xCS = xTaskGetTickCount();
                while( ( xTaskGetTickCount() - xCS ) < MS( 20 ) ) { __asm volatile ( "nop" ); }
            }
            xSemaphoreGive( xSemR2 );
        }
        xSemaphoreGive( xSemR1 );

        while( ( xTaskGetTickCount() - xS ) < MS( 60 ) ) { __asm volatile ( "nop" ); }

        vTaskDelayEDF( &xLWT );
    }
}

static void vTask14B( void * pvParams )
{
    ( void ) pvParams;
    TickType_t xLWT = 0;

    for( ;; )
    {
        TickType_t xS = xTaskGetTickCount();

        /* Work 50 ms */
        while( ( xTaskGetTickCount() - xS ) < MS( 50 ) ) { __asm volatile ( "nop" ); }

        /* Take R2 first, then R1 — OPPOSITE ORDER from Task A */
        xSemaphoreTake( xSemR2, portMAX_DELAY );
        {
            xSemaphoreTake( xSemR1, portMAX_DELAY );
            {
                TickType_t xCS = xTaskGetTickCount();
                while( ( xTaskGetTickCount() - xCS ) < MS( 200 ) ) { __asm volatile ( "nop" ); }
            }
            xSemaphoreGive( xSemR1 );
        }
        xSemaphoreGive( xSemR2 );

        while( ( xTaskGetTickCount() - xS ) < MS( 500 ) ) { __asm volatile ( "nop" ); }

        vTaskDelayEDF( &xLWT );
    }
}

int main( void )
{
    stdio_init_all();
    printf( "\r\n=== SRP Test 14: Deadlock prevention ===\r\n" );
    printf( "A: T=300,D=300,C=60 (takes R1 then R2)\r\n" );
    printf( "B: T=900,D=900,C=500 (takes R2 then R1 — opposite order)\r\n" );
    printf( "R1 ceiling=300, R2 ceiling=300\r\n" );
    printf( "Without SRP this would deadlock. With SRP, A is blocked while B holds resources.\r\n\r\n" );

    xSemR1 = xSemaphoreCreateBinary();
    xSemR2 = xSemaphoreCreateBinary();
    configASSERT( xSemR1 );
    configASSERT( xSemR2 );
    xSemaphoreGive( xSemR1 );
    xSemaphoreGive( xSemR2 );

    vSemaphoreSetResourceCeiling( xSemR1, MS( 300 ) );
    vSemaphoreSetResourceCeiling( xSemR2, MS( 300 ) );

    xTaskCreateEDF( vTask14A, "T14_A", 512, NULL, 2,
                    MS( 300 ), MS( 300 ), MS( 60 ), 0, NULL );
    xTaskCreateEDF( vTask14B, "T14_B", 512, NULL, 2,
                    MS( 900 ), MS( 900 ), MS( 500 ), 0, NULL );

    xTaskCreate( vLEDTask, "LED", 256, NULL, 1, NULL );
    xTaskCreate( vUARTDrainTask, "UARTDrain", 512, NULL, 1, NULL );

    vTaskStartScheduler();
    while( 1 ) {}
}

#endif /* TEST_CASE == 14 */

/* -----------------------------------------------------------------------
 * TEST 15 — SRP internal state verification
 *
 *   Single task locks/unlocks R1(ceil=100), R2(ceil=300), R3(ceil=500) in
 *   various nesting orders. After each operation, checks ceiling value and
 *   stack depth match expectations. Tests triple nesting and min tracking.
 * ----------------------------------------------------------------------- */
#if ( TEST_CASE == 15 )

static SemaphoreHandle_t xSemR1;
static SemaphoreHandle_t xSemR2;
static SemaphoreHandle_t xSemR3;

static UBaseType_t uxTestStep = 0;
static UBaseType_t uxPassCount = 0;
static UBaseType_t uxFailCount = 0;

static void prvCheckState( TickType_t xExpCeiling, UBaseType_t uxExpDepth, const char * pcDesc )
{
    TickType_t xActCeiling = xSRPGetCurrentCeiling();
    UBaseType_t uxActDepth = uxSRPGetCeilingStackDepth();
    BaseType_t xCeilOk  = ( xActCeiling == xExpCeiling );
    BaseType_t xDepthOk = ( uxActDepth  == uxExpDepth );
    const char * pcVerdict = ( xCeilOk && xDepthOk ) ? "PASS" : "FAIL";

    uxTestStep++;

    printf( "  [%2lu] %s | ceil=%10lu exp=%10lu | depth=%lu exp=%lu | %s\r\n",
            ( unsigned long ) uxTestStep,
            pcVerdict,
            ( unsigned long ) xActCeiling,
            ( unsigned long ) xExpCeiling,
            ( unsigned long ) uxActDepth,
            ( unsigned long ) uxExpDepth,
            pcDesc );

    if( xCeilOk && xDepthOk )
    {
        uxPassCount++;
    }
    else
    {
        uxFailCount++;
    }
}

static void vStateVerifyTask( void * pvParams )
{
    ( void ) pvParams;
    TickType_t xLWT = 0;

    /* Run the verification sequence once per period */
    for( ;; )
    {
        uxTestStep  = 0;
        uxPassCount = 0;
        uxFailCount = 0;

        printf( "\r\n--- SRP state verification run (tick=%lu) ---\r\n",
                ( unsigned long ) xTaskGetTickCount() );

        /* Step 0: nothing locked — depth=0, ceiling=MAX */
        prvCheckState( portMAX_DELAY, 0, "initial: no locks" );

        /* Step 1: take R2 (ceiling=300) — depth=1, ceiling=300 */
        xSemaphoreTake( xSemR2, portMAX_DELAY );
        prvCheckState( MS( 300 ), 1, "after take R2(300)" );

        /* Step 2: take R1 nested (ceiling=100) — depth=2, ceiling=100 */
        xSemaphoreTake( xSemR1, portMAX_DELAY );
        prvCheckState( MS( 100 ), 2, "after take R1(100) nested in R2" );

        /* Step 3: release R1 → depth=1, ceiling=300 */
        xSemaphoreGive( xSemR1 );
        prvCheckState( MS( 300 ), 1, "after release R1, only R2 held" );

        /* Step 4: release R2 → depth=0, ceiling=MAX */
        xSemaphoreGive( xSemR2 );
        prvCheckState( portMAX_DELAY, 0, "after release R2, nothing held" );

        /* Step 5: take R1 (ceiling=100) — depth=1, ceiling=100 */
        xSemaphoreTake( xSemR1, portMAX_DELAY );
        prvCheckState( MS( 100 ), 1, "after take R1(100)" );

        /* Step 6: take R3 nested (ceiling=500) — depth=2, ceiling=100 (R1 still min) */
        xSemaphoreTake( xSemR3, portMAX_DELAY );
        prvCheckState( MS( 100 ), 2, "after take R3(500) nested, R1 still min" );

        /* Step 7: take R2 nested (ceiling=300) — depth=3, ceiling=100 (R1 still min) */
        xSemaphoreTake( xSemR2, portMAX_DELAY );
        prvCheckState( MS( 100 ), 3, "after take R2(300) triple-nested, R1 still min" );

        /* Step 8: release R2 — depth=2, ceiling=100 (R1 still min) */
        xSemaphoreGive( xSemR2 );
        prvCheckState( MS( 100 ), 2, "after release R2, R1+R3 held, R1 still min" );

        /* Step 9: release R3 — depth=1, ceiling=100 (only R1 held) */
        xSemaphoreGive( xSemR3 );
        prvCheckState( MS( 100 ), 1, "after release R3, only R1 held" );

        /* Step 10: release R1 → depth=0, ceiling=MAX */
        xSemaphoreGive( xSemR1 );
        prvCheckState( portMAX_DELAY, 0, "after release R1, nothing held" );

        printf( "\r\n=== Results: %lu/%lu passed ===\r\n",
                ( unsigned long ) uxPassCount,
                ( unsigned long ) ( uxPassCount + uxFailCount ) );

        if( uxFailCount == 0 )
        {
            printf( "ALL SRP STATE CHECKS PASSED\r\n" );
        }

        vTaskDelayEDF( &xLWT );
    }
}

int main( void )
{
    stdio_init_all();
    printf( "\r\n=== SRP Test 15: Internal state verification ===\r\n" );
    printf( "Verifies ceiling value AND stack depth after every lock/unlock\r\n\r\n" );

    xSemR1 = xSemaphoreCreateBinary();
    xSemR2 = xSemaphoreCreateBinary();
    xSemR3 = xSemaphoreCreateBinary();
    configASSERT( xSemR1 );
    configASSERT( xSemR2 );
    configASSERT( xSemR3 );
    xSemaphoreGive( xSemR1 );
    xSemaphoreGive( xSemR2 );
    xSemaphoreGive( xSemR3 );

    vSemaphoreSetResourceCeiling( xSemR1, MS( 100 ) );
    vSemaphoreSetResourceCeiling( xSemR2, MS( 300 ) );
    vSemaphoreSetResourceCeiling( xSemR3, MS( 500 ) );

    /* One EDF task that runs the verification sequence.
     * Period is long enough to complete all steps + print. */
    xTaskCreateEDF( vStateVerifyTask, "T15_Verify", 1024, NULL, 2,
                    MS( 5000 ), MS( 5000 ), MS( 100 ), 0, NULL );

    xTaskCreate( vLEDTask, "LED", 256, NULL, 1, NULL );
    vTaskStartScheduler();
    while( 1 ) {}
}

#endif /* TEST_CASE == 15 */

/* -----------------------------------------------------------------------
 * TEST 16 — Admission control with SRP blocking times
 *
 *   8 scenarios covering every rejection path: C+B > D, U > 1.0,
 *   and demand-bound failure. Both the admit/reject decision and the
 *   internal admitted-task count are verified after each step.
 *   See inline comments in main() for per-step params and reasoning.
 * ----------------------------------------------------------------------- */
#if ( TEST_CASE == 16 )

/* Simple periodic body — just sleeps; no resource usage needed here.
 * Admission control is the focus; runtime SRP is covered by tests 8/13/14. */
static void vAC16Task( void * pvParams )
{
    ( void ) pvParams;
    TickType_t xLWT = 0;
    for( ;; ) { vTaskDelayEDF( &xLWT ); }
}

/* Check one admission step.
 *   xGot        — return value from xTaskCreateEDF
 *   xExpected   — pdPASS or pdFAIL
 *   uxExpCount  — expected admitted-task count AFTER this call
 *   pcWhy       — human-readable description of the internal state
 *   pcDesc      — one-line scenario label
 *
 * Two independent assertions per step:
 *   (a) Behavioral  — xGot == xExpected
 *   (b) Internal    — uxEDFGetAdmittedCount() == uxExpCount
 */
static void prvAC16Check( BaseType_t xGot,
                          BaseType_t xExpected,
                          UBaseType_t uxExpCount,
                          UBaseType_t * puxPass,
                          UBaseType_t * puxFail,
                          const char * pcWhy,
                          const char * pcDesc )
{
    UBaseType_t uxActCount = uxEDFGetAdmittedCount();
    BaseType_t  xResultOk  = ( xGot      == xExpected  );
    BaseType_t  xCountOk   = ( uxActCount == uxExpCount );
    const char *pcRes      = ( xGot      == pdPASS ) ? "ADMIT " : "REJECT";
    const char *pcExp      = ( xExpected == pdPASS ) ? "ADMIT " : "REJECT";
    const char *pcVerdict  = ( xResultOk && xCountOk ) ? "PASS" : "FAIL";

    printf( "  [%s] result=%-6s exp=%-6s | admitted=%lu exp=%lu | %s\r\n",
            pcVerdict, pcRes, pcExp,
            ( unsigned long ) uxActCount,
            ( unsigned long ) uxExpCount,
            pcDesc );
    printf( "       internal: %s\r\n", pcWhy );

    if( xResultOk && xCountOk ) { ( *puxPass )++; }
    else                        { ( *puxFail )++; }
}

int main( void )
{
    stdio_init_all();
    printf( "\r\n=== SRP Test 16: Admission control with blocking times ===\r\n" );
    printf( "Each step shows: behavioral result | admitted-task count | internal reasoning\r\n\r\n" );

    UBaseType_t uxPass = 0, uxFail = 0;
    BaseType_t  xRet;

    /* ------------------------------------------------------------------ */
    printf( "--- LL path (implicit deadline, D == T) ---\r\n" );

    /* Step 1: U=0.30, C+B=400 <= 1000 => ADMIT, count 0->1 */
    xRet = xTaskCreateEDF( vAC16Task, "T1", 512, NULL, 2,
                           MS( 1000 ), MS( 1000 ), MS( 300 ), MS( 100 ), NULL );
    prvAC16Check( xRet, pdPASS, 1, &uxPass, &uxFail,
                  "C+B=300+100=400 <= D=1000 OK; U=300/1000=0.30; sum=0.30 <= 1.0",
                  "T1  T=1000 C=300 B=100 -> ADMIT" );

    /* Step 2: U=0.30, C+B=200 <= 500, sum=0.60 => ADMIT, count 1->2 */
    xRet = xTaskCreateEDF( vAC16Task, "T2", 512, NULL, 2,
                           MS( 500 ), MS( 500 ), MS( 150 ), MS( 50 ), NULL );
    prvAC16Check( xRet, pdPASS, 2, &uxPass, &uxFail,
                  "C+B=150+50=200 <= D=500 OK; U=150/500=0.30; sum=0.60 <= 1.0",
                  "T2  T=500  C=150 B=50  -> ADMIT" );

    /* Step 3: C+B=350 > D=300 => REJECT, count stays 2 */
    xRet = xTaskCreateEDF( vAC16Task, "TR1", 512, NULL, 2,
                           MS( 300 ), MS( 300 ), MS( 200 ), MS( 150 ), NULL );
    prvAC16Check( xRet, pdFAIL, 2, &uxPass, &uxFail,
                  "C+B=200+150=350 > D=300 => LL feasibility (C+B<=D) check FAILS",
                  "TR1 T=300  C=200 B=150 -> REJECT (C+B > D)" );

    /* Step 4: C+B=401 > D=400 (off-by-one boundary) => REJECT, count stays 2 */
    xRet = xTaskCreateEDF( vAC16Task, "TR2", 512, NULL, 2,
                           MS( 400 ), MS( 400 ), MS( 300 ), MS( 101 ), NULL );
    prvAC16Check( xRet, pdFAIL, 2, &uxPass, &uxFail,
                  "C+B=300+101=401 > D=400 => boundary off-by-one, LL feasibility FAILS",
                  "TR2 T=400  C=300 B=101 -> REJECT (C+B = D+1)" );

    /* Step 5: C+B=130 <= 200 OK, but sum=0.60+0.65=1.25 > 1.0 => REJECT, count stays 2 */
    xRet = xTaskCreateEDF( vAC16Task, "TR3", 512, NULL, 2,
                           MS( 200 ), MS( 200 ), MS( 130 ), MS( 0 ), NULL );
    prvAC16Check( xRet, pdFAIL, 2, &uxPass, &uxFail,
                  "C+B=130+0=130 <= D=200 OK; U=130/200=0.65; sum=0.60+0.65=1.25 > 1.0 => utilisation FAILS",
                  "TR3 T=200  C=130 B=0   -> REJECT (sum U > 1.0)" );

    /* Step 6: U=0.05, C+B=150 <= 2000, sum=0.65 => ADMIT, count 2->3 */
    xRet = xTaskCreateEDF( vAC16Task, "T3", 512, NULL, 2,
                           MS( 2000 ), MS( 2000 ), MS( 100 ), MS( 50 ), NULL );
    prvAC16Check( xRet, pdPASS, 3, &uxPass, &uxFail,
                  "C+B=100+50=150 <= D=2000 OK; U=100/2000=0.05; sum=0.65 <= 1.0",
                  "T3  T=2000 C=100 B=50  -> ADMIT" );

    /* ------------------------------------------------------------------ */
    printf( "\r\n--- Demand-bound path (constrained deadline, D < T) ---\r\n" );

    /* Step 7: D=800 < T=1000 triggers demand path.
     * At t=800: T2(floor(800/500)*150=150) + T4(floor(1000/1000)*100=100) = 250 comp
     *           max_B of tasks with D<=800: T2(B=50), T4(B=50) => max_B=50
     *           h(800) = 250 + 50 = 300 <= 800 => ADMIT, count 3->4 */
    xRet = xTaskCreateEDF( vAC16Task, "T4", 512, NULL, 2,
                           MS( 1000 ), MS( 800 ), MS( 100 ), MS( 50 ), NULL );
    prvAC16Check( xRet, pdPASS, 4, &uxPass, &uxFail,
                  "D=800<T=1000 => demand path; h(800)=T2(150)+T4(100)+max_B(50)=300 <= 800",
                  "T4  T=1000 D=800 C=100 B=50  -> ADMIT (demand OK)" );

    /* Step 8: Large B pushes demand over the bound.
     * At t=800: T2(150)+T4(100)+TR4(200)=450 comp
     *           max_B of tasks with D<=800: T2(50), T4(50), TR4(400) => max_B=400
     *           h(800) = 450 + 400 = 850 > 800 => REJECT, count stays 4 */
    xRet = xTaskCreateEDF( vAC16Task, "TR4", 512, NULL, 2,
                           MS( 800 ), MS( 800 ), MS( 200 ), MS( 400 ), NULL );
    prvAC16Check( xRet, pdFAIL, 4, &uxPass, &uxFail,
                  "h(800)=T2(150)+T4(100)+TR4(200)+max_B(400)=850 > 800 => demand bound FAILS",
                  "TR4 T=800  D=800 C=200 B=400 -> REJECT (h(800) > 800)" );

    /* ------------------------------------------------------------------ */
    printf( "\r\n=== Admission control results: %lu/%lu passed ===\r\n",
            ( unsigned long ) uxPass,
            ( unsigned long ) ( uxPass + uxFail ) );

    if( uxFail == 0 )
    {
        printf( "ALL ADMISSION CONTROL CHECKS PASSED\r\n" );
    }

    /* Admitted tasks (T1-T4) run as simple periodic tasks after scheduler starts. */
    xTaskCreate( vLEDTask, "LED", 256, NULL, 1, NULL );
    vTaskStartScheduler();
    while( 1 ) {}
}

#endif /* TEST_CASE == 16 */

/* -----------------------------------------------------------------------
 * TEST 11 — Stack sharing quantitative study: NO sharing (baseline)
 * TEST 12 — Stack sharing quantitative study: WITH sharing (dispatcher)
 *
 *   Both tests use 100 jobs with T=1000, D=1000, C=9 ms each.
 *
 *   Test 11: Creates 100 separate EDF tasks, each with its own 256-word stack.
 *            Total stack memory = 100 × 256 × 4 = 102,400 bytes.
 *
 *   Test 12: Creates 1 dispatcher EDF task via xTaskCreateEDFSharedGroup().
 *            100 jobs share a single 256-word stack.
 *            Total stack memory = 1 × 256 × 4 = 1,024 bytes.
 *
 *   Compare heap usage before/after to measure actual savings.
 * ----------------------------------------------------------------------- */
#if ( TEST_CASE == 11 ) || ( TEST_CASE == 12 )

/* One-shot job function: busy-wait 9 ms then return. */
static void vSharedJob( void * pvParams )
{
    ( void ) pvParams;
    TickType_t xS = xTaskGetTickCount();
    while( ( xTaskGetTickCount() - xS ) < MS( 9 ) ) { __asm volatile ( "nop" ); }
}

/* Looping task for non-shared (Test 11) baseline. */
static void vSeparateEDFTask( void * pvParams )
{
    ( void ) pvParams;
    TickType_t xLWT = 0;
    for( ;; )
    {
        vSharedJob( NULL );
        vTaskDelayEDF( &xLWT );
    }
}

int main( void )
{
    stdio_init_all();

    const UBaseType_t uxJobCount    = 100U;
    const TickType_t  xPeriod       = MS( 1000 );
    const TickType_t  xDeadline     = MS( 1000 );
    const TickType_t  xWCETPerJob   = MS( 9 );
    const configSTACK_DEPTH_TYPE uxStackDepth = 256;

    size_t xHeapBefore = xPortGetFreeHeapSize();

    #if ( TEST_CASE == 11 )
    {
        printf( "\r\n=== Test 11: 100 tasks WITHOUT stack sharing ===\r\n" );

        UBaseType_t uxAdmitted = 0;

        for( UBaseType_t i = 0U; i < uxJobCount; i++ )
        {
            char cName[ configMAX_TASK_NAME_LEN ];
            snprintf( cName, sizeof( cName ), "T_%02lu", ( unsigned long ) i );

            BaseType_t xResult = xTaskCreateEDF( vSeparateEDFTask, cName,
                                                  uxStackDepth, NULL, 2,
                                                  xPeriod, xDeadline, xWCETPerJob,
                                                  0, NULL );
            if( xResult == pdPASS )
            {
                uxAdmitted++;
            }
            else
            {
                printf( "  Task %lu rejected\r\n", ( unsigned long ) i );
                break;
            }
        }

        size_t xHeapAfter = xPortGetFreeHeapSize();
        size_t xUsed = xHeapBefore - xHeapAfter;

        printf( "  Admitted: %lu / %lu\r\n", ( unsigned long ) uxAdmitted,
                ( unsigned long ) uxJobCount );
        printf( "  Heap before: %lu bytes\r\n", ( unsigned long ) xHeapBefore );
        printf( "  Heap after:  %lu bytes\r\n", ( unsigned long ) xHeapAfter );
        printf( "  Stack memory used: %lu bytes\r\n", ( unsigned long ) xUsed );
        printf( "  Per-task stack: %lu words × 4 = %lu bytes\r\n",
                ( unsigned long ) uxStackDepth,
                ( unsigned long ) ( uxStackDepth * 4U ) );
    }
    #else /* TEST_CASE == 12 */
    {
        printf( "\r\n=== Test 12: 100 jobs WITH stack sharing (dispatcher) ===\r\n" );

        BaseType_t xResult = xTaskCreateEDFSharedGroup(
                                 vSharedJob, "SharedGrp",
                                 uxStackDepth, uxJobCount, 2,
                                 xPeriod, xDeadline, xWCETPerJob,
                                 0, NULL );

        size_t xHeapAfter = xPortGetFreeHeapSize();
        size_t xUsed = xHeapBefore - xHeapAfter;

        printf( "  Result: %s\r\n", ( xResult == pdPASS ) ? "ADMITTED" : "REJECTED" );
        printf( "  Jobs in group: %lu\r\n", ( unsigned long ) uxJobCount );
        printf( "  Heap before: %lu bytes\r\n", ( unsigned long ) xHeapBefore );
        printf( "  Heap after:  %lu bytes\r\n", ( unsigned long ) xHeapAfter );
        printf( "  Stack memory used: %lu bytes\r\n", ( unsigned long ) xUsed );
        printf( "  Shared stack: %lu words × 4 = %lu bytes\r\n",
                ( unsigned long ) uxStackDepth,
                ( unsigned long ) ( uxStackDepth * 4U ) );
    }
    #endif

    printf( "\r\n" );

    xTaskCreate( vLEDTask, "LED", 256, NULL, 1, NULL );
    xTaskCreate( vUARTDrainTask, "UARTDrain", 512, NULL, 1, NULL );

    vTaskStartScheduler();
    while( 1 ) {}
}

#endif /* TEST_CASE == 11 || TEST_CASE == 12 */

/* -----------------------------------------------------------------------
 * TEST 17 — Global EDF: Two tasks on two cores, both run simultaneously
 *   Task A: T=200, D=200, C=80   U=0.4
 *   Task B: T=200, D=200, C=80   U=0.4
 *   Total U=0.8 on 2 cores. Both should run at the same time with no
 *   deadline misses.
 * ----------------------------------------------------------------------- */
#if ( TEST_CASE == 17 )

static void vTask17A( void * pvParams )
{
    ( void ) pvParams;
    TickType_t xLWT = 0;
    for( ;; )
    {
        TickType_t xS = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xS ) < MS( 80 ) ) { __asm volatile ( "nop" ); }
        vTaskDelayEDF( &xLWT );
    }
}

static void vTask17B( void * pvParams )
{
    ( void ) pvParams;
    TickType_t xLWT = 0;
    for( ;; )
    {
        TickType_t xS = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xS ) < MS( 80 ) ) { __asm volatile ( "nop" ); }
        vTaskDelayEDF( &xLWT );
    }
}

int main( void )
{
    stdio_init_all();
    printf( "\r\n=== Global EDF Test 17: Two tasks, two cores ===\r\n" );
    printf( "Both tasks should run simultaneously, no misses\r\n\r\n" );

    xTaskCreateEDF( vTask17A, "G17_A", 512, NULL, 2,
                    MS( 200 ), MS( 200 ), MS( 80 ), 0, NULL );
    xTaskCreateEDF( vTask17B, "G17_B", 512, NULL, 2,
                    MS( 200 ), MS( 200 ), MS( 80 ), 0, NULL );

    xTaskCreate( vLEDTask, "LED", 256, NULL, 1, NULL );
    xTaskCreate( vUARTDrainTask, "UARTDrain", 512, NULL, 1, NULL );

    vTaskStartScheduler();
    while( 1 ) {}
}

#endif /* TEST_CASE == 17 */

/* -----------------------------------------------------------------------
 * TEST 18 — Global EDF: Three tasks on two cores with migration
 *   Task A: T=100, D=100, C=30   U=0.3
 *   Task B: T=150, D=150, C=40   U=0.27
 *   Task C: T=200, D=200, C=50   U=0.25
 *   Total U=0.82 on 2 cores. Tasks must migrate between cores to meet
 *   all deadlines.
 * ----------------------------------------------------------------------- */
#if ( TEST_CASE == 18 )

static void vTask18A( void * pvParams )
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

static void vTask18B( void * pvParams )
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

static void vTask18C( void * pvParams )
{
    ( void ) pvParams;
    TickType_t xLWT = 0;
    for( ;; )
    {
        TickType_t xS = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xS ) < MS( 50 ) ) { __asm volatile ( "nop" ); }
        vTaskDelayEDF( &xLWT );
    }
}

int main( void )
{
    stdio_init_all();
    printf( "\r\n=== Global EDF Test 18: Three tasks, two cores ===\r\n" );
    printf( "Tasks should migrate between cores, no misses\r\n\r\n" );

    xTaskCreateEDF( vTask18A, "G18_A", 512, NULL, 2,
                    MS( 100 ), MS( 100 ), MS( 30 ), 0, NULL );
    xTaskCreateEDF( vTask18B, "G18_B", 512, NULL, 2,
                    MS( 150 ), MS( 150 ), MS( 40 ), 0, NULL );
    xTaskCreateEDF( vTask18C, "G18_C", 512, NULL, 2,
                    MS( 200 ), MS( 200 ), MS( 50 ), 0, NULL );

    xTaskCreate( vLEDTask, "LED", 256, NULL, 1, NULL );
    xTaskCreate( vUARTDrainTask, "UARTDrain", 512, NULL, 1, NULL );

    vTaskStartScheduler();
    while( 1 ) {}
}

#endif /* TEST_CASE == 18 */

/* -----------------------------------------------------------------------
 * TEST 19 — Global EDF: Admission control rejects at U > 2.0
 *   Task A: T=100, D=100, C=80   U=0.8
 *   Task B: T=100, D=100, C=80   U=0.8
 *   Task C: T=100, D=100, C=80   U=0.8  (total 2.4 > 2.0, rejected)
 * ----------------------------------------------------------------------- */
#if ( TEST_CASE == 19 )

static void vTask19Body( void * pvParams )
{
    ( void ) pvParams;
    TickType_t xLWT = 0;
    for( ;; )
    {
        TickType_t xS = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xS ) < MS( 80 ) ) { __asm volatile ( "nop" ); }
        vTaskDelayEDF( &xLWT );
    }
}

int main( void )
{
    stdio_init_all();
    printf( "\r\n=== Global EDF Test 19: Admission control U>2.0 ===\r\n" );

    BaseType_t xA = xTaskCreateEDF( vTask19Body, "G19_A", 512, NULL, 2,
                                    MS( 100 ), MS( 100 ), MS( 80 ), 0, NULL );
    BaseType_t xB = xTaskCreateEDF( vTask19Body, "G19_B", 512, NULL, 2,
                                    MS( 100 ), MS( 100 ), MS( 80 ), 0, NULL );
    BaseType_t xC = xTaskCreateEDF( vTask19Body, "G19_C", 512, NULL, 2,
                                    MS( 100 ), MS( 100 ), MS( 80 ), 0, NULL );

    printf( "Task A: %s\r\n", xA == pdPASS ? "ADMITTED" : "REJECTED" );
    printf( "Task B: %s\r\n", xB == pdPASS ? "ADMITTED" : "REJECTED" );
    printf( "Task C: %s\r\n", xC == pdPASS ? "ADMITTED" : "REJECTED" );

    printf( "Expected: A=ADMITTED, B=ADMITTED, C=REJECTED\r\n" );

    xTaskCreate( vLEDTask, "LED", 256, NULL, 1, NULL );
    xTaskCreate( vUARTDrainTask, "UARTDrain", 512, NULL, 1, NULL );

    vTaskStartScheduler();
    while( 1 ) {}
}

#endif /* TEST_CASE == 19 */

/* -----------------------------------------------------------------------
 * TEST 20 — Accept new EDF tasks while system is running
 *   Spawner task creates a new EDF task every 500ms (up to 4 tasks).
 *   Each spawned task: T=200, D=200, C=20.
 *   Verifies dynamic admission works after scheduler has started.
 * ----------------------------------------------------------------------- */
#if ( TEST_CASE == 20 )

static void vTask20Worker( void * pvParams )
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

static void vTask20Spawner( void * pvParams )
{
    ( void ) pvParams;
    static const char * pcNames[] = { "DYN_1", "DYN_2", "DYN_3", "DYN_4" };
    UBaseType_t ux;

    for( ux = 0; ux < 4; ux++ )
    {
        vTaskDelay( MS( 500 ) );

        BaseType_t xRet = xTaskCreateEDF( vTask20Worker, pcNames[ ux ], 512, NULL, 2,
                                          MS( 200 ), MS( 200 ), MS( 20 ), 0, NULL );

        printf( "t=%lu  Created %s: %s\r\n",
                ( unsigned long ) xTaskGetTickCount(),
                pcNames[ ux ],
                xRet == pdPASS ? "ADMITTED" : "REJECTED" );
    }

    printf( "Spawner done, admitted count = %lu\r\n",
            ( unsigned long ) uxEDFGetAdmittedCount() );

    /* Spawner has nothing left to do */
    vTaskDelete( NULL );
}

int main( void )
{
    stdio_init_all();
    printf( "\r\n=== Test 20: Dynamic task creation at runtime ===\r\n" );
    printf( "Spawner creates 4 EDF tasks 500ms apart\r\n\r\n" );

    xTaskCreate( vTask20Spawner, "Spawner", 512, NULL, 3, NULL );
    xTaskCreate( vLEDTask, "LED", 256, NULL, 1, NULL );
    xTaskCreate( vUARTDrainTask, "UARTDrain", 512, NULL, 1, NULL );

    vTaskStartScheduler();
    while( 1 ) {}
}

#endif /* TEST_CASE == 20 */

/* -----------------------------------------------------------------------
 * TEST 21 — ~100 periodic EDF tasks running simultaneously
 *   100 tasks, each T=1000, D=1000, C=1. Total U = 100*(1/1000) = 0.1
 *   on 2 cores. Should all be admitted and run with no deadline misses.
 * ----------------------------------------------------------------------- */
#if ( TEST_CASE == 21 )

static void vTask21Body( void * pvParams )
{
    ( void ) pvParams;
    TickType_t xLWT = 0;
    for( ;; )
    {
        TickType_t xS = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xS ) < MS( 1 ) ) { __asm volatile ( "nop" ); }
        vTaskDelayEDF( &xLWT );
    }
}

int main( void )
{
    stdio_init_all();
    printf( "\r\n=== Test 21: 100 periodic EDF tasks ===\r\n" );

    char cName[ configMAX_TASK_NAME_LEN ];
    UBaseType_t uxAdmitted = 0;
    UBaseType_t ux;

    for( ux = 0; ux < 100; ux++ )
    {
        snprintf( cName, sizeof( cName ), "E%lu", ( unsigned long ) ux );
        BaseType_t xRet = xTaskCreateEDF( vTask21Body, cName, 256, NULL, 2,
                                          MS( 1000 ), MS( 1000 ), MS( 1 ), 0, NULL );
        if( xRet == pdPASS )
        {
            uxAdmitted++;
        }
    }

    printf( "Admitted %lu / 100 tasks\r\n", ( unsigned long ) uxAdmitted );

    xTaskCreate( vLEDTask, "LED", 256, NULL, 1, NULL );
    xTaskCreate( vUARTDrainTask, "UARTDrain", 512, NULL, 1, NULL );

    vTaskStartScheduler();
    while( 1 ) {}
}

#endif /* TEST_CASE == 21 */

/* -----------------------------------------------------------------------
 * TEST 22 — Overrun (deadline miss) detection
 *   Task A: T=100, D=100, C=50 (declared WCET)
 *   Task A actually runs for 120ms on first job, exceeding its deadline.
 *   The miss log should report the overrun.
 * ----------------------------------------------------------------------- */
#if ( TEST_CASE == 22 )

static volatile UBaseType_t uxJob22Count = 0;

static void vTask22Overrun( void * pvParams )
{
    ( void ) pvParams;
    TickType_t xLWT = 0;
    for( ;; )
    {
        uxJob22Count++;
        TickType_t xRunTime;

        if( uxJob22Count <= 2 )
        {
            /* First two jobs: overrun deliberately */
            xRunTime = MS( 120 );
            printf( "Job %lu: running 120ms (will miss D=100)\r\n",
                    ( unsigned long ) uxJob22Count );
        }
        else
        {
            /* Subsequent jobs: behave normally */
            xRunTime = MS( 50 );
        }

        TickType_t xS = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xS ) < xRunTime ) { __asm volatile ( "nop" ); }

        vTaskDelayEDF( &xLWT );
    }
}

int main( void )
{
    stdio_init_all();
    printf( "\r\n=== Test 22: Overrun / deadline miss detection ===\r\n" );
    printf( "Task runs 120ms with D=100, expect miss log entries\r\n\r\n" );

    xTaskCreateEDF( vTask22Overrun, "OVERRUN", 512, NULL, 2,
                    MS( 200 ), MS( 100 ), MS( 50 ), 0, NULL );

    xTaskCreate( vLEDTask, "LED", 256, NULL, 1, NULL );
    xTaskCreate( vUARTDrainTask, "UARTDrain", 512, NULL, 1, NULL );

    vTaskStartScheduler();
    while( 1 ) {}
}

#endif /* TEST_CASE == 22 */

/* -----------------------------------------------------------------------
 * TEST 23 — Partitioned EDF: 2 tasks both fit on core 0
 *   Task A: T=200, D=200, C=80   U=0.4
 *   Task B: T=200, D=200, C=80   U=0.4
 *   Total U per core: both fit on core 0 (U=0.8 <= 1.0).
 * ----------------------------------------------------------------------- */
#if ( TEST_CASE == 23 )

static void vTask23Body( void * pvParams )
{
    ( void ) pvParams;
    TickType_t xLWT = 0;
    for( ;; )
    {
        TickType_t xS = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xS ) < MS( 80 ) ) { __asm volatile ( "nop" ); }
        vTaskDelayEDF( &xLWT );
    }
}

int main( void )
{
    stdio_init_all();
    printf( "\r\n=== Partitioned EDF Test 23: 2 tasks both fit on core 0 ===\r\n" );
    printf( "Expected: both tasks admitted, both pinned to C0\r\n\r\n" );

    BaseType_t xA = xTaskCreateEDF( vTask23Body, "P23_A", 512, NULL, 2,
                                    MS( 200 ), MS( 200 ), MS( 80 ), 0, NULL );
    BaseType_t xB = xTaskCreateEDF( vTask23Body, "P23_B", 512, NULL, 2,
                                    MS( 200 ), MS( 200 ), MS( 80 ), 0, NULL );

    printf( "Task A: %s\r\n", xA == pdPASS ? "ADMITTED" : "REJECTED" );
    printf( "Task B: %s\r\n", xB == pdPASS ? "ADMITTED" : "REJECTED" );

    xTaskCreate( vLEDTask, "LED", 256, NULL, 1, NULL );
    xTaskCreate( vUARTDrainTask, "UARTDrain", 512, NULL, 1, NULL );

    vTaskStartScheduler();
    while( 1 ) {}
}

#endif /* TEST_CASE == 23 */

/* -----------------------------------------------------------------------
 * TEST 24 — Partitioned EDF: first-fit overflow to core 1
 *   Task A: T=100, D=100, C=60   U=0.6  → core 0 (60% used)
 *   Task B: T=100, D=100, C=30   U=0.3  → core 0 (90% used)
 *   Task C: T=100, D=100, C=20   U=0.2  → core 0 full (would reach 110%), first-fit → core 1
 * ----------------------------------------------------------------------- */
#if ( TEST_CASE == 24 )

static void vTask24Body( void * pvParams )
{
    TickType_t xC = ( TickType_t ) pvParams;
    TickType_t xLWT = 0;
    for( ;; )
    {
        TickType_t xS = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xS ) < xC ) { __asm volatile ( "nop" ); }
        vTaskDelayEDF( &xLWT );
    }
}

int main( void )
{
    stdio_init_all();
    printf( "\r\n=== Partitioned EDF Test 24: first-fit overflow to core 1 ===\r\n" );
    printf( "Expected: A,B → C0; C → C1\r\n\r\n" );

    BaseType_t xA = xTaskCreateEDF( vTask24Body, "P24_A", 512, (void *) MS( 60 ), 2,
                                    MS( 100 ), MS( 100 ), MS( 60 ), 0, NULL );
    BaseType_t xB = xTaskCreateEDF( vTask24Body, "P24_B", 512, (void *) MS( 30 ), 2,
                                    MS( 100 ), MS( 100 ), MS( 30 ), 0, NULL );
    BaseType_t xC = xTaskCreateEDF( vTask24Body, "P24_C", 512, (void *) MS( 20 ), 2,
                                    MS( 100 ), MS( 100 ), MS( 20 ), 0, NULL );

    printf( "Task A: %s\r\n", xA == pdPASS ? "ADMITTED" : "REJECTED" );
    printf( "Task B: %s\r\n", xB == pdPASS ? "ADMITTED" : "REJECTED" );
    printf( "Task C: %s\r\n", xC == pdPASS ? "ADMITTED" : "REJECTED" );
    printf( "Expected: A=ADMITTED, B=ADMITTED, C=ADMITTED\r\n" );

    xTaskCreate( vLEDTask, "LED", 256, NULL, 1, NULL );
    xTaskCreate( vUARTDrainTask, "UARTDrain", 512, NULL, 1, NULL );

    vTaskStartScheduler();
    while( 1 ) {}
}

#endif /* TEST_CASE == 24 */

/* -----------------------------------------------------------------------
 * TEST 25 — Partitioned EDF: per-core rejection (no core fits)
 *   Task A: T=100, D=100, C=90   U=0.9  → core 0
 *   Task B: T=100, D=100, C=90   U=0.9  → core 1
 *   Task C: T=100, D=100, C=20   U=0.2  → rejected (core 0: 1.1, core 1: 1.1)
 * ----------------------------------------------------------------------- */
#if ( TEST_CASE == 25 )

static void vTask25Body( void * pvParams )
{
    TickType_t xC = ( TickType_t ) pvParams;
    TickType_t xLWT = 0;
    for( ;; )
    {
        TickType_t xS = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xS ) < xC ) { __asm volatile ( "nop" ); }
        vTaskDelayEDF( &xLWT );
    }
}

int main( void )
{
    stdio_init_all();
    printf( "\r\n=== Partitioned EDF Test 25: per-core rejection ===\r\n" );
    printf( "Expected: A=ADMITTED, B=ADMITTED, C=REJECTED\r\n\r\n" );

    BaseType_t xA = xTaskCreateEDF( vTask25Body, "P25_A", 512, (void *) MS( 90 ), 2,
                                    MS( 100 ), MS( 100 ), MS( 90 ), 0, NULL );
    BaseType_t xB = xTaskCreateEDF( vTask25Body, "P25_B", 512, (void *) MS( 90 ), 2,
                                    MS( 100 ), MS( 100 ), MS( 90 ), 0, NULL );
    BaseType_t xC = xTaskCreateEDF( vTask25Body, "P25_C", 512, (void *) MS( 20 ), 2,
                                    MS( 100 ), MS( 100 ), MS( 20 ), 0, NULL );

    printf( "Task A: %s\r\n", xA == pdPASS ? "ADMITTED" : "REJECTED" );
    printf( "Task B: %s\r\n", xB == pdPASS ? "ADMITTED" : "REJECTED" );
    printf( "Task C: %s\r\n", xC == pdPASS ? "ADMITTED" : "REJECTED" );

    xTaskCreate( vLEDTask, "LED", 256, NULL, 1, NULL );
    xTaskCreate( vUARTDrainTask, "UARTDrain", 512, NULL, 1, NULL );

    vTaskStartScheduler();
    while( 1 ) {}
}

#endif /* TEST_CASE == 25 */

/* -----------------------------------------------------------------------
 * TEST 26 — Partitioned EDF: no migration across jobs
 *   3 tasks on 2 cores. Each task prints its running core via the switch log.
 *   Task A: T=100, D=100, C=40  → core 0
 *   Task B: T=150, D=150, C=40  → core 0
 *   Task C: T=200, D=200, C=70  → core 1
 * ----------------------------------------------------------------------- */
#if ( TEST_CASE == 26 )

static void vTask26Body( void * pvParams )
{
    TickType_t xC = ( TickType_t ) pvParams;
    TickType_t xLWT = 0;
    for( ;; )
    {
        TickType_t xS = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xS ) < xC ) { __asm volatile ( "nop" ); }
        vTaskDelayEDF( &xLWT );
    }
}

static void vTask26Drain( void * pvParams )
{
    ( void ) pvParams;
    vTaskDelay( MS( 10000 ) );
    printf( "\r\n--- Switch log (verify no task changes core) ---\r\n" );
    vEDFDrainSwitchLog();
    printf( "--- End of log ---\r\n" );
    vTaskDelete( NULL );
}

int main( void )
{
    stdio_init_all();
    printf( "\r\n=== Partitioned EDF Test 26: no migration ===\r\n" );
    printf( "A,B → C0; C → C1. Each task must stay on its assigned core.\r\n\r\n" );

    xTaskCreateEDF( vTask26Body, "P26_A", 512, (void *) MS( 40 ), 2,
                    MS( 100 ), MS( 100 ), MS( 40 ), 0, NULL );
    xTaskCreateEDF( vTask26Body, "P26_B", 512, (void *) MS( 40 ), 2,
                    MS( 150 ), MS( 150 ), MS( 40 ), 0, NULL );
    xTaskCreateEDF( vTask26Body, "P26_C", 512, (void *) MS( 70 ), 2,
                    MS( 200 ), MS( 200 ), MS( 70 ), 0, NULL );

    xTaskCreate( vTask26Drain, "Drain", 512, NULL, 3, NULL );
    xTaskCreate( vLEDTask, "LED", 256, NULL, 1, NULL );
    xTaskCreate( vUARTDrainTask, "UARTDrain", 512, NULL, 1, NULL );

    vTaskStartScheduler();
    while( 1 ) {}
}

#endif /* TEST_CASE == 26 */

/* -----------------------------------------------------------------------
 * TEST 27 — Partitioned EDF: deadline miss detection
 *   Same structure as Test 22 but under partitioned scheduling.
 *   Task A: T=200, D=100, C=50 (declared). First 2 jobs run 120ms → miss.
 * ----------------------------------------------------------------------- */
#if ( TEST_CASE == 27 )

static volatile UBaseType_t uxJob27Count = 0;

static void vTask27Overrun( void * pvParams )
{
    ( void ) pvParams;
    TickType_t xLWT = 0;
    for( ;; )
    {
        uxJob27Count++;
        TickType_t xRunTime = ( uxJob27Count <= 2 ) ? MS( 120 ) : MS( 50 );

        if( uxJob27Count <= 2 )
        {
            printf( "Job %lu: running 120ms (will miss D=100)\r\n",
                    ( unsigned long ) uxJob27Count );
        }

        TickType_t xS = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xS ) < xRunTime ) { __asm volatile ( "nop" ); }

        vTaskDelayEDF( &xLWT );
    }
}

int main( void )
{
    stdio_init_all();
    printf( "\r\n=== Partitioned EDF Test 27: deadline miss detection ===\r\n" );
    printf( "Task runs 120ms with D=100 for first 2 jobs, expect miss log entries\r\n\r\n" );

    xTaskCreateEDF( vTask27Overrun, "P27_OVR", 512, NULL, 2,
                    MS( 200 ), MS( 100 ), MS( 50 ), 0, NULL );

    xTaskCreate( vLEDTask, "LED", 256, NULL, 1, NULL );
    xTaskCreate( vUARTDrainTask, "UARTDrain", 512, NULL, 1, NULL );

    vTaskStartScheduler();
    while( 1 ) {}
}

#endif /* TEST_CASE == 27 */

/* -----------------------------------------------------------------------
 * TEST 28 — Partitioned EDF: dynamic task admission while system is running
 *   A spawner creates 5 EDF tasks 500ms apart while the scheduler is live.
 *   All tasks use T=200ms, D=200ms.  First-fit core assignment:
 *
 *   DYN_1: C=120ms  U=0.60  C0: 0.60        → C0  ADMITTED
 *   DYN_2: C=100ms  U=0.50  C0: 1.10 > 1.0  → C1: 0.50  ADMITTED
 *   DYN_3: C= 60ms  U=0.30  C0: 0.90        → C0  ADMITTED
 *   DYN_4: C= 60ms  U=0.30  C0: 1.20 > 1.0  → C1: 0.80  ADMITTED
 *   DYN_5: C= 60ms  U=0.30  C0: 1.20 > 1.0  → C1: 1.10 > 1.0  REJECTED
 *
 *   Final admitted count = 4  (2 on C0, 2 on C1)
 * ----------------------------------------------------------------------- */
#if ( TEST_CASE == 28 )

static void vTask28Worker( void * pvParams )
{
    TickType_t xWCET = ( TickType_t ) pvParams;
    TickType_t xLWT  = xTaskGetTickCount();

    for( ;; )
    {
        TickType_t xS = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xS ) < xWCET ) { __asm volatile ( "nop" ); }
        vTaskDelayEDF( &xLWT );
    }
}

static void vTask28Spawner( void * pvParams )
{
    ( void ) pvParams;

    static const struct
    {
        const char * pcName;
        TickType_t   xWCET;
        const char * pcExpected;
    } xJobs[] =
    {
        { "DYN_1", MS( 120 ), "ADMITTED → C0" },
        { "DYN_2", MS( 100 ), "ADMITTED → C1" },
        { "DYN_3", MS(  60 ), "ADMITTED → C0" },
        { "DYN_4", MS(  60 ), "ADMITTED → C1" },
        { "DYN_5", MS(  60 ), "REJECTED"       },
    };

    UBaseType_t ux;

    for( ux = 0; ux < ( UBaseType_t ) ( sizeof( xJobs ) / sizeof( xJobs[ 0 ] ) ); ux++ )
    {
        vTaskDelay( MS( 500 ) );

        BaseType_t xRet = xTaskCreateEDF( vTask28Worker,
                                          xJobs[ ux ].pcName,
                                          512,
                                          ( void * ) xJobs[ ux ].xWCET,
                                          2,
                                          MS( 200 ), MS( 200 ), xJobs[ ux ].xWCET,
                                          0, NULL );

        printf( "t=%lu  %-6s: %-9s  (expected: %s)\r\n",
                ( unsigned long ) xTaskGetTickCount(),
                xJobs[ ux ].pcName,
                xRet == pdPASS ? "ADMITTED" : "REJECTED",
                xJobs[ ux ].pcExpected );
    }

    printf( "\r\nSpawner done — admitted count = %lu (expected 4)\r\n",
            ( unsigned long ) uxEDFGetAdmittedCount() );
    vEDFPrintStats();

    vTaskDelete( NULL );
}

int main( void )
{
    stdio_init_all();
    printf( "\r\n=== Partitioned EDF Test 28: dynamic admission while running ===\r\n" );
    printf( "Spawner creates 5 EDF tasks 500ms apart (T=D=200ms)\r\n" );
    printf( "Expected: DYN_1→C0, DYN_2→C1, DYN_3→C0, DYN_4→C1, DYN_5 rejected\r\n\r\n" );

    xTaskCreate( vTask28Spawner, "Spawner", 512, NULL, 3, NULL );
    xTaskCreate( vLEDTask,       "LED",     256, NULL, 1, NULL );
    xTaskCreate( vUARTDrainTask, "UARTDrain", 512, NULL, 1, NULL );

    vTaskStartScheduler();
    while( 1 ) {}
}

#endif /* TEST_CASE == 28 */

/* -----------------------------------------------------------------------
 * TEST 29 — ~100 periodic Partitioned EDF tasks across both cores
 *   100 tasks, each T=1000, D=1000, C=12. U_i = 0.012 per task.
 *   First-fit fills core 0 to U=1.0 with the first 82 tasks; the
 *   remaining tasks are assigned to core 1.
 * ----------------------------------------------------------------------- */
#if ( TEST_CASE == 29 )

static void vTask29Body( void * pvParams )
{
    ( void ) pvParams;
    TickType_t xLWT = 0;
    for( ;; )
    {
        TickType_t xS = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xS ) < MS( 12 ) ) { __asm volatile ( "nop" ); }
        vTaskDelayEDF( &xLWT );
    }
}

int main( void )
{
    stdio_init_all();
    printf( "\r\n=== Test 29: 100 periodic Partitioned EDF tasks ===\r\n" );
    printf( "T=1000 D=1000 C=12 x100  U_i=0.012  total U=1.20\r\n" );
    printf( "Expected: first 82 tasks on C0 (U=0.984), next 18 tasks on C1 (U=0.216)\r\n\r\n" );

    char cName[ configMAX_TASK_NAME_LEN ];
    UBaseType_t uxAdmitted = 0;
    UBaseType_t ux;

    for( ux = 0; ux < 100; ux++ )
    {
        snprintf( cName, sizeof( cName ), "P%lu", ( unsigned long ) ux );
        BaseType_t xRet = xTaskCreateEDF( vTask29Body, cName, 256, NULL, 2,
                                          MS( 1000 ), MS( 1000 ), MS( 12 ), 0, NULL );
        if( xRet == pdPASS )
        {
            uxAdmitted++;
        }
    }

    printf( "Admitted %lu / 100 tasks\r\n", ( unsigned long ) uxAdmitted );

    xTaskCreate( vLEDTask, "LED", 256, NULL, 1, NULL );
    xTaskCreate( vUARTDrainTask, "UARTDrain", 512, NULL, 1, NULL );

    vTaskStartScheduler();
    while( 1 ) {}
}

#endif /* TEST_CASE == 29 */
