#include "smw.h"

#include "linked_list.h"

#include <stdlib.h>
#include <string.h>

Smw g_smw;

int smw_init() {
    memset(&g_smw, 0, sizeof(g_smw));
    g_smw.tasks = linked_list_create();
    if (!g_smw.tasks) {
        return -1;
    }
    return 0;
}

SmwTask* smw_create_task(void* context,
                         void (*callback)(void* context, uint64_t mon_time)) {
    if (!g_smw.tasks) {
        return NULL;
    }

    SmwTask* task = malloc(sizeof(SmwTask));
    if (!task) {
        return NULL;
    }

    task->context  = context;
    task->callback = callback;

    if (linked_list_append(g_smw.tasks, task) != 0) {
        free(task);
        return NULL;
    }

    return task;
}

void smw_destroy_task(SmwTask* task) {
    if (!g_smw.tasks || !task) {
        return;
    }

    Node* node = g_smw.tasks->head;
    while (node) {
        Node* next = node->front;
        if (node->item == task) {
            linked_list_remove(g_smw.tasks, node, free);
            break;
        }
        node = next;
    }
}

void smw_work(uint64_t mon_time) {
    if (!g_smw.tasks) {
        return;
    }

    Node* node = g_smw.tasks->head;
    while (node) {
        Node*    next = node->front; // Save next node before callback
        SmwTask* task = (SmwTask*)node->item;
        if (task && task->callback) {
            task->callback(task->context, mon_time);
        }
        node = next;
    }
}

int smw_get_task_count() {
    if (!g_smw.tasks) {
        return 0;
    }
    return (int)g_smw.tasks->size;
}

void smw_dispose() {
    if (!g_smw.tasks) {
        return;
    }
    linked_list_dispose(&g_smw.tasks, free);
}
