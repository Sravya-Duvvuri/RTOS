#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>

#define TOTAL_HEAP_SIZE configTOTAL_HEAP_SIZE
static uint8_t ucHeap[TOTAL_HEAP_SIZE];

// Task handles
TaskHandle_t task1Handle = NULL;
TaskHandle_t task2Handle = NULL;
TaskHandle_t task3Handle = NULL;

// Deadlines in ticks
TickType_t deadline1 = 500;
TickType_t deadline2 = 1000;
TickType_t deadline3 = 1500;

#include "portable.h" // needed for HeapRegion_t

void setupHeap() {
    HeapRegion_t xHeapRegions[] = {
        { ucHeap, TOTAL_HEAP_SIZE },
        { NULL, 0 } // Terminates the region list
    };
    vPortDefineHeapRegions(xHeapRegions);
}


// Dynamic EDF-style priority updater
void updatePriorities() {
    TickType_t currentTick = xTaskGetTickCount();

    TickType_t remaining1 = (deadline1 > currentTick) ? deadline1 - currentTick : 0;
    TickType_t remaining2 = (deadline2 > currentTick) ? deadline2 - currentTick : 0;
    TickType_t remaining3 = (deadline3 > currentTick) ? deadline3 - currentTick : 0;

    if (remaining1 <= remaining2 && remaining1 <= remaining3) {
        vTaskPrioritySet(task1Handle, 3);
        vTaskPrioritySet(task2Handle, 2);
        vTaskPrioritySet(task3Handle, 1);
    }
    else if (remaining2 <= remaining1 && remaining2 <= remaining3) {
        vTaskPrioritySet(task1Handle, 1);
        vTaskPrioritySet(task2Handle, 3);
        vTaskPrioritySet(task3Handle, 2);
    }
    else {
        vTaskPrioritySet(task1Handle, 1);
        vTaskPrioritySet(task2Handle, 2);
        vTaskPrioritySet(task3Handle, 3);
    }
}

// Tasks
void task1(void* params) {
    for (;;) {
        updatePriorities();
        printf("Task 1 running. Deadline: %u\n", (unsigned int)deadline1);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void task2(void* params) {
    for (;;) {
        updatePriorities();
        printf("Task 2 running. Deadline: %u\n", (unsigned int)deadline2);
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}

void task3(void* params) {
    for (;;) {
        updatePriorities();
        printf("Task 3 running. Deadline: %u\n", (unsigned int)deadline3);
        vTaskDelay(pdMS_TO_TICKS(400));
    }
}

int main(void) {
    printf("Starting EDF Scheduler Example...\n");

    setupHeap(); // <-- define heap regions here

    xTaskCreate(task1, "Task1", 1000, NULL, 1, &task1Handle);
    xTaskCreate(task2, "Task2", 1000, NULL, 1, &task2Handle);
    xTaskCreate(task3, "Task3", 1000, NULL, 1, &task3Handle);

    vTaskStartScheduler();

    for (;;);
}

// ----------------- REQUIRED HOOK FUNCTIONS ------------------

// Malloc failure
void vApplicationMallocFailedHook(void) {
    printf("Malloc failed!\n");
    taskDISABLE_INTERRUPTS();
    for (;;);
}

// Stack overflow
void vApplicationStackOverflowHook(TaskHandle_t xTask, char* pcTaskName) {
    printf("Stack overflow in task: %s\n", pcTaskName);
    fflush(stdout);
    taskDISABLE_INTERRUPTS();
    for (;;);
}

// Idle task hook (optional)
void vApplicationIdleHook(void) {}

// Tick hook (optional)
void vApplicationTickHook(void) {}

// Daemon task startup hook (optional)
void vApplicationDaemonTaskStartupHook(void) {}

// Provide memory for idle task
void vApplicationGetIdleTaskMemory(StaticTask_t** ppxIdleTaskTCBBuffer,
    StackType_t** ppxIdleTaskStackBuffer,
    uint32_t* pulIdleTaskStackSize) {
    static StaticTask_t xIdleTaskTCB;
    static StackType_t uxIdleTaskStack[configMINIMAL_STACK_SIZE];

    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

StackType_t uxTimerTaskStack[configTIMER_TASK_STACK_DEPTH];

// Provide memory for timer task
void vApplicationGetTimerTaskMemory(StaticTask_t** ppxTimerTaskTCBBuffer,
    StackType_t** ppxTimerTaskStackBuffer,
    uint32_t* pulTimerTaskStackSize) {
    static StaticTask_t xTimerTaskTCB;
    //static StackType_t uxTimerTaskStack[configTIMER_TASK_STACK_DEPTH];

    *ppxTimerTaskTCBBuffer = &xTimerTaskTCB;
    *ppxTimerTaskStackBuffer = uxTimerTaskStack;
    *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}

// Assert failed
void vAssertCalled(unsigned long ulLine, const char* const pcFileName) {
    printf("Assertion failed in file %s at line %lu\n", pcFileName, ulLine);
    fflush(stdout);
    taskDISABLE_INTERRUPTS();
    for (;;);
}

// ------------- OPTIONAL STUBS IF TRACEALYZER ENABLED -------------

// If Tracealyzer or tracing is enabled but unused
void vTraceTimerReset(void) {}
unsigned int uiTraceTimerGetFrequency(void) { return 1000; }
unsigned int uiTraceTimerGetValue(void) { return (unsigned int)xTaskGetTickCount(); }
