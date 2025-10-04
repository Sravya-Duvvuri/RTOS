/* main_blinky.c
   Combined demos:
   - Basic EDF (kept minimal)
   - Fault-Tolerant EDF (primary + backup, random overruns, logging)
   - Watchdog Supervisor (2 workers, supervisor expects bitwise notifications every 100ms)
*/

#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <limits.h>

#define NUM_FT_TASKS 2

/* ----------------------------
   ---------- Utilities --------
   ---------------------------- */
static void vPrintTimestamped(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    TickType_t t = xTaskGetTickCount();
    printf("[%lu ms] ", (unsigned long)t);
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}

/* ----------------------------
   ----- Basic EDF (kept) -----
   ---------------------------- */
typedef struct {
    TaskHandle_t handle;
    TickType_t period;
    TickType_t next_deadline;
    const char* name;
} EDFTask;

static EDFTask edfTasks[2];

static void vEDF_TaskA(void* pvParameters) {
    EDFTask* task = (EDFTask*)pvParameters;
    TickType_t xLastWake = xTaskGetTickCount();
    for (;;) {
        vPrintTimestamped("%s: executing", task->name);
        task->next_deadline = xTaskGetTickCount() + task->period;
        vTaskDelayUntil(&xLastWake, task->period);
    }
}

static void vEDF_TaskB(void* pvParameters) {
    EDFTask* task = (EDFTask*)pvParameters;
    TickType_t xLastWake = xTaskGetTickCount();
    for (;;) {
        vPrintTimestamped("%s: executing", task->name);
        task->next_deadline = xTaskGetTickCount() + task->period;
        vTaskDelayUntil(&xLastWake, task->period);
    }
}

static void vEDF_Scheduler(void* pvParameters) {
    (void)pvParameters;
    for (;;) {
        // simple reorder by deadline
        for (int i = 0; i < 1; ++i) {
            for (int j = i + 1; j < 2; ++j) {
                if (edfTasks[j].next_deadline < edfTasks[i].next_deadline) {
                    EDFTask tmp = edfTasks[i];
                    edfTasks[i] = edfTasks[j];
                    edfTasks[j] = tmp;
                }
            }
        }
        for (int i = 0; i < 2; ++i) {
            vTaskPrioritySet(edfTasks[i].handle, 3 - i); // dynamic priority assignment
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void main_blinky(void) {
    /* This function is left as a simple EDF demo entry.
       To run other demos, change the call in main.c (see instructions). */
    srand((unsigned)time(NULL));
    edfTasks[0].period = pdMS_TO_TICKS(300);
    edfTasks[0].name = "EDF_TaskA";
    xTaskCreate(vEDF_TaskA, edfTasks[0].name, configMINIMAL_STACK_SIZE, &edfTasks[0], 1, &edfTasks[0].handle);

    edfTasks[1].period = pdMS_TO_TICKS(500);
    edfTasks[1].name = "EDF_TaskB";
    xTaskCreate(vEDF_TaskB, edfTasks[1].name, configMINIMAL_STACK_SIZE, &edfTasks[1], 1, &edfTasks[1].handle);

    xTaskCreate(vEDF_Scheduler, "EDF_Scheduler", configMINIMAL_STACK_SIZE, NULL, configMAX_PRIORITIES - 1, NULL);

    vTaskStartScheduler();
}

/* ----------------------------
   11) Fault-Tolerant EDF Demo
   ---------------------------- */

typedef struct {
    TaskHandle_t primaryHandle;
    TaskHandle_t backupHandle;
    TickType_t period;     // period of primary
    TickType_t deadline;   // relative time backups wait before activation
    const char* name;
    BaseType_t primarySuccess; // result of last primary run
    // stats:
    uint32_t successCount;
    uint32_t backupActivations;
    uint32_t deadlineMisses;
} FaultTolerantTask;

static FaultTolerantTask ftTasks[NUM_FT_TASKS];

static void vFT_Primary(void* pvParameters) {
    FaultTolerantTask* task = (FaultTolerantTask*)pvParameters;
    TickType_t xNextWake = xTaskGetTickCount();
    for (;;) {
        // start of cycle: assume fail until proven otherwise
        task->primarySuccess = pdFALSE;
        vTaskDelayUntil(&xNextWake, task->period); // periodic release

        vPrintTimestamped("[%s] Primary started", task->name);

        // simulate random overrun (10% chance)
        if ((rand() % 10) == 0) {
            // take longer than deadline (overrun)
            vTaskDelay(pdMS_TO_TICKS((task->deadline * 2) / 1)); // big overrun
            vPrintTimestamped("[%s] Primary: OVERRUN", task->name);
            task->primarySuccess = pdFALSE;
        }
        else {
            // normal execution shorter than deadline
            vTaskDelay(pdMS_TO_TICKS(task->period / 2));
            task->primarySuccess = pdTRUE;
            task->successCount++;
            vPrintTimestamped("[%s] Primary: SUCCESS", task->name);
        }

        // If primary succeeded, nothing for backup to do this cycle.
        // Just log. Deadline misses:
        TickType_t now = xTaskGetTickCount();
        if (now > (xNextWake - task->period + task->deadline)) {
            // if now is past the deadline relative to cycle start
            if (!task->primarySuccess) {
                task->deadlineMisses++;
                vPrintTimestamped("[%s] ⚠️ PRIMARY missed deadline (primarySuccess=false) at %lu", task->name, (unsigned long)now);
            }
            else {
                // if primary succeeded but still past deadline it means it finished late
                // count as missed as well
                task->deadlineMisses++;
                vPrintTimestamped("[%s] ⚠️ PRIMARY finished after deadline (late success) at %lu", task->name, (unsigned long)now);
            }
        }

        // Primary does NOT directly suspend backup — backup checks primarySuccess at its activation time.
    }
}

static void vFT_Backup(void* pvParameters) {
    FaultTolerantTask* task = (FaultTolerantTask*)pvParameters;
    for (;;) {
        // Backup waits "deadline" ticks from its last activation time.
        vTaskDelay(pdMS_TO_TICKS(task->deadline));

        // If primary didn't succeed this cycle, backup activates.
        if (!task->primarySuccess) {
            task->backupActivations++;
            vPrintTimestamped("[%s] BACKUP activated (primary failed)", task->name);
            // Simulate backup execution (lighter)
            vTaskDelay(pdMS_TO_TICKS(task->period / 4));
        }
        else {
            // primary succeeded -> backup cancels itself for this cycle
            vPrintTimestamped("[%s] Backup checked: primary succeeded -> skipping", task->name);
        }
        // loop: will delay again for next cycle
    }
}

static void vFT_Monitor(void* pvParameters) {
    (void)pvParameters;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000)); // print summary every 5 seconds
        vPrintTimestamped("---- Fault Tolerant EDF Summary ----");
        for (int i = 0; i < NUM_FT_TASKS; ++i) {
            FaultTolerantTask* t = &ftTasks[i];
            printf("  [%s] successes=%lu backups=%lu deadline_misses=%lu\n",
                t->name,
                (unsigned long)t->successCount,
                (unsigned long)t->backupActivations,
                (unsigned long)t->deadlineMisses);
        }
    }
}

void main_fault_tolerant_demo(void) {
    srand((unsigned)time(NULL));

    // Configure two FT tasks
    ftTasks[0].name = "JobA";
    ftTasks[0].period = 500;   // ms
    ftTasks[0].deadline = 800; // ms
    ftTasks[0].primarySuccess = pdTRUE;
    ftTasks[0].successCount = ftTasks[0].backupActivations = ftTasks[0].deadlineMisses = 0;

    ftTasks[1].name = "JobB";
    ftTasks[1].period = 700;   // ms
    ftTasks[1].deadline = 1000; // ms
    ftTasks[1].primarySuccess = pdTRUE;
    ftTasks[1].successCount = ftTasks[1].backupActivations = ftTasks[1].deadlineMisses = 0;

    // create tasks (primary & backup)
    for (int i = 0; i < NUM_FT_TASKS; ++i) {
        // convert ms to ticks in the task's fields for portability
        ftTasks[i].period = pdMS_TO_TICKS(ftTasks[i].period);
        ftTasks[i].deadline = ftTasks[i].deadline; // keep in ms; Backup uses pdMS_TO_TICKS when delaying
        xTaskCreate(vFT_Primary, ftTasks[i].name, configMINIMAL_STACK_SIZE + 50, &ftTasks[i], 3, &ftTasks[i].primaryHandle);
        xTaskCreate(vFT_Backup, "FT_Backup", configMINIMAL_STACK_SIZE + 40, &ftTasks[i], 2, &ftTasks[i].backupHandle);
    }

    xTaskCreate(vFT_Monitor, "FT_Monitor", configMINIMAL_STACK_SIZE + 60, NULL, 1, NULL);

    vTaskStartScheduler();
}

/* ----------------------------
   12) Watchdog Supervisor Demo
   ---------------------------- */

   /* Supervisor expects bitwise notification:
      - bit 0 (1<<0) = heartbeat from worker1
      - bit 1 (1<<1) = heartbeat from worker2
      Each cycle supervisor waits up to 100 ms to collect notifications, then inspects bits.
   */

static TaskHandle_t xWorker1 = NULL;
static TaskHandle_t xWorker2 = NULL;
static TaskHandle_t xSupervisor = NULL;

static void vWorkerTask(void* pvParameters) {
    const TickType_t xPeriod = pdMS_TO_TICKS(100);
    const uint32_t ulBit = (uint32_t)pvParameters; // 1<<0 or 1<<1
    for (;;) {
        // send bitwise notification to supervisor
        if (xSupervisor != NULL) {
            xTaskNotify(xSupervisor, ulBit, eSetBits);
        }
        vPrintTimestamped("Worker (bit %lu) heartbeat sent", (unsigned long)ulBit);
        vTaskDelay(xPeriod);
    }
}

static void vSupervisorTask(void* pvParameters) {
    (void)pvParameters;
    uint32_t ulMissed1 = 0, ulMissed2 = 0;

    for (;;) {
        uint32_t ulReceivedBits = 0;

        // Wait up to 100 ms and collect any notification bits set in that period.
        // xTaskNotifyWait will return pdTRUE if any notification arrived; the value stored
        // in ulReceivedBits is the task's notification value (bitwise OR of set bits).
        BaseType_t xGot = xTaskNotifyWait(0, ULONG_MAX, &ulReceivedBits, pdMS_TO_TICKS(100));

        if (xGot == pdTRUE) {
            // check each bit
            if (ulReceivedBits & (1UL << 0)) {
                ulMissed1 = 0;
            }
            else {
                ulMissed1++;
            }
            if (ulReceivedBits & (1UL << 1)) {
                ulMissed2 = 0;
            }
            else {
                ulMissed2++;
            }
        }
        else {
            // no notification in this 100ms window => both missed this cycle
            ulMissed1++;
            ulMissed2++;
        }

        // If any worker missed two consecutive cycles -> restart it
        if (ulMissed1 >= 2) {
            vPrintTimestamped("Supervisor: Restarting Worker1 (missed %lu cycles)", (unsigned long)ulMissed1);
            if (xWorker1 != NULL) {
                vTaskDelete(xWorker1);
            }
            // recreate
            xTaskCreate(vWorkerTask, "Worker1", configMINIMAL_STACK_SIZE + 20, (void*)(uintptr_t)(1UL << 0), 2, &xWorker1);
            ulMissed1 = 0;
        }
        if (ulMissed2 >= 2) {
            vPrintTimestamped("Supervisor: Restarting Worker2 (missed %lu cycles)", (unsigned long)ulMissed2);
            if (xWorker2 != NULL) {
                vTaskDelete(xWorker2);
            }
            xTaskCreate(vWorkerTask, "Worker2", configMINIMAL_STACK_SIZE + 20, (void*)(uintptr_t)(1UL << 1), 2, &xWorker2);
            ulMissed2 = 0;
        }
    }
}

void main_watchdog_demo(void) {
    srand((unsigned)time(NULL));
    // create supervisor first so workers can notify it
    xTaskCreate(vSupervisorTask, "Supervisor", configMINIMAL_STACK_SIZE + 50, NULL, 4, &xSupervisor);

    // create two workers: pass the bit mask (1<<0 and 1<<1) via pvParameters
    xTaskCreate(vWorkerTask, "Worker1", configMINIMAL_STACK_SIZE + 20, (void*)(uintptr_t)(1UL << 0), 2, &xWorker1);
    xTaskCreate(vWorkerTask, "Worker2", configMINIMAL_STACK_SIZE + 20, (void*)(uintptr_t)(1UL << 1), 2, &xWorker2);

    vTaskStartScheduler();
}
