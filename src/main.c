#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include "pico/stdlib.h"


void led_task(void *params)
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

void vApplicationDeadlineMissedHook( TaskHandle_t xTask, TickType_t xDeadline )
{
    // log the miss, otherwise do nothing (continue)
    printf("[JG] WARNING: %s missed soft deadline at tick %u!\n", pcTaskGetName(xTask), (unsigned int)xDeadline);
}

void mock_edf_task(void *params)
{   
    TickType_t xLastWakeTime = xTaskGetTickCount();
    while (true) {
        // simulate work, then yield
        TickType_t xWorkStart = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xWorkStart ) < 1 )
        {
        }

        vTaskDelayEDF(&xLastWakeTime);
    }
}

int main()
{
    stdio_init_all();

    xTaskCreate(led_task, "LED_Task", 256, NULL, 1, NULL);

    for(int i = 0; i < 100; i++) {
        char taskName[16];
        sprintf(taskName, "EDF_%d", i);
        
        // expect total utilization for 100 tasks to be 100%
        BaseType_t res = xTaskCreateEDF(mock_edf_task, taskName, 128, NULL, 2, NULL, 1, 100, 100);
        
        if (res != pdPASS) {
            printf("Task %d rejected by Admission Control!\n", i);
        }
    }

    vTaskStartScheduler();

    while(1){};
}