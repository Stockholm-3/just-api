#ifndef SMW_H
#define SMW_H

#include "linked_list.h"

#include <stdint.h>

#ifndef SMW_MAX_TASKS
#    define SMW_MAX_TASKS 16
#endif

typedef struct {
    void* context;
    void (*callback)(void* context, uint64_t mon_time);

} SmwTask;

typedef struct {
    LinkedList* tasks;
} Smw;

extern Smw g_smw;

int smw_init();

SmwTask* smw_create_task(void* context,
                         void (*callback)(void* context, uint64_t mon_time));
void     smw_destroy_task(SmwTask* task);

void smw_work(uint64_t mon_time);

int smw_get_task_count();

void smw_dispose();

#endif // SMW_H
