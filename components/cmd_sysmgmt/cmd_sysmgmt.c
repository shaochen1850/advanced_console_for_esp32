/*
 * SPDX-FileCopyrightText: 2026 shaochen1850
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */
/*
 * cmd_sysmgmt.c — System management commands
 *
 * Tasks, kill, suspend, resume, priority, stack
 */


#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_console.h"
#include "esp_log.h"

static const char *TAG = "sysmgmt";

/* ==================================================================== */
/*  Helper for uxTaskGetStackSize */
/* ==================================================================== */
UBaseType_t __attribute__((weak)) uxTaskGetStackSize(TaskHandle_t xTask);

/* ==================================================================== */
/*  Helper: Find task handle with PID（xTaskNumber）                    */
/* ==================================================================== */
static TaskHandle_t find_task_by_pid(unsigned long pid,
                                     const char **out_name,
                                     unsigned long *out_actual_pid)
{
    UBaseType_t count = uxTaskGetNumberOfTasks();
    if (count == 0) return NULL;

    TaskStatus_t *status = malloc(sizeof(TaskStatus_t) * count);
    if (!status) return NULL;

    unsigned long total_run_time;
    UBaseType_t actual = uxTaskGetSystemState(status, count, &total_run_time);
    TaskHandle_t ret = NULL;
    for (UBaseType_t i = 0; i < actual; i++) {
        if (status[i].xTaskNumber == pid) {
            ret = status[i].xHandle;
            if (out_name) *out_name = status[i].pcTaskName;
            if (out_actual_pid) *out_actual_pid = status[i].xTaskNumber;
            break;
        }
    }
    free(status);
    return ret;
}

/* ==================================================================== */
/*  'tasks' — List all the tasks                                        */
/* ==================================================================== */

/* compare tasks by PID */
static int compare_by_task_number(const void *a, const void *b)
{
    const TaskStatus_t *ta = (const TaskStatus_t *)a;
    const TaskStatus_t *tb = (const TaskStatus_t *)b;
    if (ta->xTaskNumber < tb->xTaskNumber) return -1;
    if (ta->xTaskNumber > tb->xTaskNumber) return 1;
    return 0;
}

static int handle_tasks(int argc, char **argv)
{
    UBaseType_t task_count = uxTaskGetNumberOfTasks();
    if (task_count == 0) {
        puts("No tasks.");
        return 0;
    }

    TaskStatus_t *status = malloc(sizeof(TaskStatus_t) * task_count);
    if (status == NULL) {
        ESP_LOGE(TAG, "Failed to allocate TaskStatus array");
        return 1;
    }

    unsigned long total_run_time = 0;
    UBaseType_t actual_count = uxTaskGetSystemState(status, task_count, &total_run_time);
    if (actual_count == 0) {
        puts("No tasks.");
        free(status);
        return 0;
    }

    qsort(status, actual_count, sizeof(TaskStatus_t), compare_by_task_number);

    /* Header Line */
    printf("%-4s %-16s %-4s %-5s %-5s %-5s %-6s %-10s %-4s\n",
           "PID", "Name", "State", "Prior", "P(B)", "Core", "CPU%", "Stack", "Use%");

    for (UBaseType_t i = 0; i < actual_count; i++) {
        const TaskStatus_t *t = &status[i];

        /* ── Status ── */
        const char *state;
        switch (t->eCurrentState) {
            case eRunning:   state = "Run"; break;
            case eReady:     state = "Rdy"; break;
            case eBlocked:   state = "Blk"; break;
            case eSuspended: state = "Sus"; break;
            case eDeleted:   state = "Del"; break;
            default:         state = "???"; break;
        }

        /* ── core ── */
        char core_str[16];
#if ( configNUMBER_OF_CORES > 1 )
        {
            BaseType_t core = (BaseType_t)-1;
            #ifdef CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID
                core = t->xCoreID;
            #else
                core = xTaskGetCoreID(t->xHandle);
            #endif
            if (core == tskNO_AFFINITY) {
                snprintf(core_str, sizeof(core_str), "Any");
            } else {
                snprintf(core_str, sizeof(core_str), "%d", (int)core);
            }
        }
#else
        snprintf(core_str, sizeof(core_str), "0");
#endif

        /* ── CPU% ── */
        double cpu_percent = 0.0;
        if (total_run_time > 0) {
            cpu_percent = (double)t->ulRunTimeCounter * 100.0 / (double)total_run_time;
        }

        /* ── stack usage ── */
        unsigned long hwm_bytes = t->usStackHighWaterMark * sizeof(StackType_t);
        unsigned long total_bytes = 0;

        if (uxTaskGetStackSize) {
            total_bytes = uxTaskGetStackSize(t->xHandle) * sizeof(StackType_t);
        }

        char stack_str[32];
        char pct_str[8];
        if (total_bytes > 0) {
            unsigned long used = total_bytes - hwm_bytes;
            int stack_percent = (int)(used * 100 / total_bytes);
            snprintf(stack_str, sizeof(stack_str), "%lu/%lu", used, total_bytes);
            snprintf(pct_str, sizeof(pct_str), "%d%%", stack_percent);
        } else {
            snprintf(stack_str, sizeof(stack_str), "free:%lu", hwm_bytes);
            snprintf(pct_str, sizeof(pct_str), "-");
        }

        printf("%-4u %-16s %-4s %-5u %-5u %-5s %-6.1f %-10s %-4s\n",
               (unsigned)t->xTaskNumber,
               t->pcTaskName,
               state,
               (unsigned)t->uxCurrentPriority,
               (unsigned)t->uxBasePriority,
               core_str,
               cpu_percent,
               stack_str,
               pct_str);
    }

    free(status);
    return 0;
}
/* ==================================================================== */
/*  'kill' — Delete a task                                              */
/* ==================================================================== */

static int handle_kill(int argc, char **argv)
{
    if (argc < 2) {
        puts("Usage:");
        puts("  kill <pid>           # delete task by PID (default)");
        puts("  kill -n <task_name>  # delete task by name");
        puts("  kill --name <name>   # same as -n");
        puts("  kill self            # delete current task");
        return 1;
    }

    /* ── special value self / this ── */
    if (strcmp(argv[1], "self") == 0 || strcmp(argv[1], "this") == 0) {
        printf("Deleting current task: %s\n", pcTaskGetName(xTaskGetCurrentTaskHandle()));
        vTaskDelete(NULL);
        return 0;   /* unreachable */
    }

    /* ── Delete by name (-n / --name) ── */
    if (strcmp(argv[1], "-n") == 0 || strcmp(argv[1], "--name") == 0) {
        if (argc < 3) {
            puts("Error: missing task name after -n/--name");
            return 1;
        }
        const char *name = argv[2];
        TaskHandle_t xTask = xTaskGetHandle(name);
        if (xTask == NULL) {
            printf("Error: task '%s' not found\n", name);
            return 1;
        }
        printf("Killing task '%s' ...\n", name);
        vTaskDelete(xTask);
        return 0;
    }

    /* ── Delete by PID (default) ── */
    char *endptr = NULL;
    unsigned long target_pid = strtoul(argv[1], &endptr, 10);
    if (*endptr != '\0' || target_pid == 0) {
        printf("Error: invalid PID '%s'\n", argv[1]);
        return 1;
    }

    const char *found_name = NULL;
    unsigned long actual_pid = 0;
    TaskHandle_t found = find_task_by_pid(target_pid, &found_name, &actual_pid);
    if (!found) {
        printf("Error: no task with PID %lu\n", target_pid);
        return 1;
    }

    /* Protection: No killing idle task */
    const char *idle_name = pcTaskGetName(xTaskGetIdleTaskHandle());
    if (strcmp(found_name, idle_name) == 0) {
        printf("Error: cannot kill idle task '%s'\n", found_name);
        return 1;
    }
#if ( configNUMBER_OF_CORES > 1 )
    const char *idle_name1 = pcTaskGetName(xTaskGetIdleTaskHandleForCore(1));
    if (strcmp(found_name, idle_name1) == 0) {
        printf("Error: cannot kill idle task '%s'\n", found_name);
        return 1;
    }
#endif

    printf("Killing task '%s' (PID %lu) ...\n", found_name, target_pid);
    vTaskDelete(found);
    return 0;
}

/* ==================================================================== */
/*  'suspend' — suspend a task                                                */
/* ==================================================================== */
static int handle_suspend(int argc, char **argv)
{
    if (argc < 2) {
        puts("Usage:");
        puts("  suspend <pid>           # suspend by PID");
        puts("  suspend -n <task_name>  # suspend by name");
        return 1;
    }

    TaskHandle_t xTask = NULL;
    const char *task_name = NULL;

    /* -n / --name */
    if (strcmp(argv[1], "-n") == 0 || strcmp(argv[1], "--name") == 0) {
        if (argc < 3) {
            puts("Error: missing task name after -n/--name");
            return 1;
        }
        task_name = argv[2];
        xTask = xTaskGetHandle(task_name);
        if (xTask == NULL) {
            printf("Error: task '%s' not found\n", task_name);
            return 1;
        }
        /* Don't allow suspending itself */
        if (xTask == xTaskGetCurrentTaskHandle()) {
            puts("Error: cannot suspend the current task (use 'kill self' if you intend to delete it)");
            return 1;
        }
        printf("Suspending task '%s' ...\n", task_name);
        vTaskSuspend(xTask);
        return 0;
    }

    /* default: by PID */
    char *endptr = NULL;
    unsigned long target_pid = strtoul(argv[1], &endptr, 10);
    if (*endptr != '\0' || target_pid == 0) {
        printf("Error: invalid PID '%s'\n", argv[1]);
        return 1;
    }

    const char *found_name = NULL;
    unsigned long actual_pid = 0;
    TaskHandle_t found = find_task_by_pid(target_pid, &found_name, &actual_pid);
    if (!found) {
        printf("Error: no task with PID %lu\n", target_pid);
        return 1;
    }
    xTask = found;
    task_name = found_name;

    /* Don't allow suspending itself */
    if (xTask == xTaskGetCurrentTaskHandle()) {
        puts("Error: cannot suspend the current task (use 'kill self' if you intend to delete it)");
        return 1;
    }

    printf("Suspending task '%s' (PID %lu) ...\n", task_name, target_pid);
    vTaskSuspend(xTask);
    return 0;
}

/* ==================================================================== */
/*  'resume' — resume a task                                                */
/* ==================================================================== */
static int handle_resume(int argc, char **argv)
{
    if (argc < 2) {
        puts("Usage:");
        puts("  resume <pid>           # resume by PID");
        puts("  resume -n <task_name>  # resume by name");
        return 1;
    }

    TaskHandle_t xTask = NULL;
    const char *task_name = NULL;

    if (strcmp(argv[1], "-n") == 0 || strcmp(argv[1], "--name") == 0) {
        if (argc < 3) {
            puts("Error: missing task name after -n/--name");
            return 1;
        }
        task_name = argv[2];
        xTask = xTaskGetHandle(task_name);
        if (xTask == NULL) {
            printf("Error: task '%s' not found\n", task_name);
            return 1;
        }
        if (xTask == xTaskGetCurrentTaskHandle()) {
            puts("Error: cannot resume the current task (it is already running)");
            return 1;
        }
        printf("Resuming task '%s' ...\n", task_name);
        vTaskResume(xTask);
        return 0;
    }

    char *endptr = NULL;
    unsigned long target_pid = strtoul(argv[1], &endptr, 10);
    if (*endptr != '\0' || target_pid == 0) {
        printf("Error: invalid PID '%s'\n", argv[1]);
        return 1;
    }

    const char *found_name = NULL;
    unsigned long actual_pid = 0;
    TaskHandle_t found = find_task_by_pid(target_pid, &found_name, &actual_pid);
    if (!found) {
        printf("Error: no task with PID %lu\n", target_pid);
        return 1;
    }
    xTask = found;
    task_name = found_name;

    if (xTask == xTaskGetCurrentTaskHandle()) {
        puts("Error: cannot resume the current task (it is already running)");
        return 1;
    }

    printf("Resuming task '%s' (PID %lu) ...\n", task_name, target_pid);
    vTaskResume(xTask);
    return 0;
}

/* ==================================================================== */
/*  'priority' — view or modify task priority                           */
/* ==================================================================== */

static int handle_priority(int argc, char **argv)
{
    if (argc < 2) {
        puts("Usage:");
        puts("  priority <pid>              # get priority");
        puts("  priority <pid> <level>      # set priority");
        puts("  priority -n <name>          # get priority by name");
        puts("  priority -n <name> <level>  # set priority by name");
        return 1;
    }

    TaskHandle_t xTask = NULL;
    const char *task_name = NULL;
    int set_mode = 0;
    unsigned long new_priority = 0;

    int idx = 1;   /* current argument index */

    /* -n / --name */
    if (strcmp(argv[idx], "-n") == 0 || strcmp(argv[idx], "--name") == 0) {
        idx++;
        if (idx >= argc) {
            puts("Error: missing task name after -n/--name");
            return 1;
        }
        task_name = argv[idx++];
        xTask = xTaskGetHandle(task_name);
        if (xTask == NULL) {
            printf("Error: task '%s' not found\n", task_name);
            return 1;
        }
        /* check for set */
        if (idx < argc) {
            char *endptr = NULL;
            new_priority = strtoul(argv[idx], &endptr, 10);
            if (*endptr != '\0') {
                printf("Error: invalid priority value '%s'\n", argv[idx]);
                return 1;
            }
            if (new_priority > configMAX_PRIORITIES - 1) {
                printf("Error: priority must be 0..%d\n", configMAX_PRIORITIES - 1);
                return 1;
            }
            set_mode = 1;
        }
    } else {
        /* by PID */
        char *endptr = NULL;
        unsigned long target_pid = strtoul(argv[idx], &endptr, 10);
        if (*endptr != '\0' || target_pid == 0) {
            printf("Error: invalid PID '%s'\n", argv[idx]);
            return 1;
        }
        idx++;

        /* look up task by PID */
    const char *found_name = NULL;
    unsigned long actual_pid = 0;
    TaskHandle_t found = find_task_by_pid(target_pid, &found_name, &actual_pid);
    if (!found) {
        printf("Error: no task with PID %lu\n", target_pid);
        return 1;
    }
    xTask = found;
    task_name = found_name;
        /* check for set */
        if (idx < argc) {
            char *ep = NULL;
            new_priority = strtoul(argv[idx], &ep, 10);
            if (*ep != '\0') {
                printf("Error: invalid priority value '%s'\n", argv[idx]);
                return 1;
            }
            if (new_priority > configMAX_PRIORITIES - 1) {
                printf("Error: priority must be 0..%d\n", configMAX_PRIORITIES - 1);
                return 1;
            }
            set_mode = 1;
        }
    }

    /* ── Set or Get ── */
    if (set_mode) {
        printf("Setting priority of task '%s' to %lu ...\n", task_name, new_priority);
        vTaskPrioritySet(xTask, (UBaseType_t)new_priority);
    } else {
        UBaseType_t prio = uxTaskPriorityGet(xTask);
        printf("Task '%s' (PID ?) priority = %u\n", task_name, (unsigned)prio);
        /* We don't have PID handy in the -n path; could optionally show it */
    }

    return 0;
}

/* ==================================================================== */
/*  'stack' — view task stack remaining space (high water mark)         */
/* ==================================================================== */

static int handle_stack(int argc, char **argv)
{
    /* ── List all tasks without parameter ── */
    if (argc == 1) {
        UBaseType_t count = uxTaskGetNumberOfTasks();
        if (count == 0) { puts("No tasks."); return 1; }

        TaskStatus_t *status = malloc(sizeof(TaskStatus_t) * count);
        if (status == NULL) { ESP_LOGE(TAG, "malloc failed"); return 1; }

        unsigned long total_run_time;
        UBaseType_t actual = uxTaskGetSystemState(status, count, &total_run_time);
        if (actual == 0) { puts("No tasks."); free(status); return 1; }

        printf("%-4s %-20s %s\n", "PID", "TASK", "STACK_HWM");
        for (UBaseType_t i = 0; i < actual; i++) {
            configSTACK_DEPTH_TYPE hwm = uxTaskGetStackHighWaterMark(status[i].xHandle);
            printf("%-4lu %-20s %u\n",
                   (unsigned long)status[i].xTaskNumber,
                   status[i].pcTaskName,
                   (unsigned)hwm);
        }
        free(status);
        return 0;
    }

    /* ── Look up by name or PID ── */
    TaskHandle_t xTask = NULL;
    const char *task_name = NULL;

    /* -n / --name */
    if (strcmp(argv[1], "-n") == 0 || strcmp(argv[1], "--name") == 0) {
        if (argc < 3) {
            puts("Error: missing task name after -n/--name");
            return 1;
        }
        task_name = argv[2];
        xTask = xTaskGetHandle(task_name);
        if (xTask == NULL) {
            printf("Error: task '%s' not found\n", task_name);
            return 1;
        }
        configSTACK_DEPTH_TYPE hwm = uxTaskGetStackHighWaterMark(xTask);
        printf("Task '%s' stack high water mark = %u\n", task_name, (unsigned)hwm);
        return 0;
    }

    /* By PID */
    char *endptr = NULL;
    unsigned long target_pid = strtoul(argv[1], &endptr, 10);
    if (*endptr != '\0' || target_pid == 0) {
        printf("Error: invalid PID '%s'\n", argv[1]);
        return 1;
    }

    const char *found_name = NULL;
    unsigned long actual_pid = 0;
    TaskHandle_t found = find_task_by_pid(target_pid, &found_name, &actual_pid);
    if (!found) {
        printf("Error: no task with PID %lu\n", target_pid);
        return 1;
    }
    xTask = found;
    task_name = found_name;
    configSTACK_DEPTH_TYPE hwm = uxTaskGetStackHighWaterMark(xTask);
    printf("Task '%s' (PID %lu) stack high water mark = %u\n",
           task_name, target_pid, (unsigned)hwm);
    return 0;
}
/* ==================================================================== */
/*  Register entry                                                      */
/* ==================================================================== */

void register_sysmgmt(void)
{
    /* tasks */
    const esp_console_cmd_t tasks_cmd = {
        .command = "tasks",
        .help = "List all running tasks with details",
        .hint = NULL,
        .func = &handle_tasks,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&tasks_cmd));

    /* kill */
    const esp_console_cmd_t kill_cmd = {
        .command = "kill",
        .help = "Delete a task by PID (default) or by name (-n <name>). Use 'self' to delete yourself.",
        .hint = NULL,
        .func = &handle_kill,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&kill_cmd));

        /* suspend */
    const esp_console_cmd_t suspend_cmd = {
        .command = "suspend",
        .help = "Suspend a task (by PID or -n <name>)",
        .hint = NULL,
        .func = &handle_suspend,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&suspend_cmd));

    /* resume */
    const esp_console_cmd_t resume_cmd = {
        .command = "resume",
        .help = "Resume a suspended task (by PID or -n <name>)",
        .hint = NULL,
        .func = &handle_resume,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&resume_cmd));

        /* priority */
    const esp_console_cmd_t priority_cmd = {
        .command = "priority",
        .help = "Get or set task priority. Usage: priority <pid> [level] | priority -n <name> [level]",
        .hint = NULL,
        .func = &handle_priority,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&priority_cmd));

    /* stack */
    const esp_console_cmd_t stack_cmd = {
        .command = "stack",
        .help = "Show task stack high water mark. Usage: stack [pid] | stack -n <name>",
        .hint = NULL,
        .func = &handle_stack,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&stack_cmd));
}