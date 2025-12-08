#include "linked_list.h"

#include <stdio.h>
#include <stdlib.h>

LinkedList* linked_list_create() {
    LinkedList* new_list = calloc(
        1, sizeof(LinkedList)); /* zeroed allocation, just what we need */
    if (!new_list) {
        printf("[LinkedList] Allocation error in LinkedList_create\n");
        return NULL;
    }
    return new_list;
}

Node* linked_list_get_index(LinkedList* list, size_t index) {
    if (list == NULL || index >= list->size) {
        return NULL;
    }

    size_t pos = 0;
    Node*  cur = NULL;

    if (index <= list->size / 2) {
        cur = list->head;
        while (pos < index) {
            cur = cur->front;
            pos++;
        }
    } else {
        cur = list->tail;
        pos = list->size - 1;
        while (pos > index) {
            cur = cur->back;
            pos--;
        }
    }
    return cur;
}

int linked_list_append(LinkedList* list, void* item) {
    if (list == NULL) {
        return 1;
    }
    Node* new_node = calloc(1, sizeof(Node));
    if (new_node == NULL) {
        return 1;
    }

    new_node->item = item;
    list->size++;

    if (list->tail == NULL) {
        list->head = new_node;
    } else {
        new_node->back    = list->tail;
        list->tail->front = new_node;
    }
    list->tail = new_node;

    return 0;
}

int linked_list_insert(LinkedList* list, size_t index, void* item) {
    if (list == NULL) {
        return 1;
    }
    if (index >= list->size) {
        return linked_list_append(list, item); /* append fallback */
    }
    Node* target = linked_list_get_index(list, index);
    if (target == NULL) {
        return 1;
    }

    Node* new_node = calloc(1, sizeof(Node));
    if (new_node == NULL) {
        return 1;
    }

    new_node->item = item;
    list->size++;

    new_node->back  = target->back;
    new_node->front = target;

    if (target->back != NULL) {
        target->back->front = new_node;
    } else {
        list->head = new_node;
    }
    target->back = new_node;

    return 0;
}

int linked_list_remove(LinkedList* list, Node* item,
                       void (*free_function)(void*)) {
    if (list == NULL || item == NULL) {
        return 1;
    }

    Node* back  = item->back;
    Node* front = item->front;

    if (back != NULL) {
        back->front = front;
    } else {
        list->head = front;
    }

    if (front != NULL) {
        front->back = back;
    } else {
        list->tail = back;
    }

    list->size--;

    item->back  = NULL;
    item->front = NULL;

    if (free_function != NULL) {
        free_function(item->item);
    }

    free(item);

    return 0;
}

int linked_list_pop(LinkedList* list, size_t index,
                    void (*free_function)(void*)) {
    Node* item = linked_list_get_index(list, index);
    if (item == NULL) {
        return 1;
    }
    return linked_list_remove(list, item, free_function);
}

void linked_list_clear(LinkedList* list, void (*free_function)(void*)) {
    if (list == NULL) {
        return;
    }
    Node* cur = list->head;
    while (cur) {
        Node* next = cur->front;
        if (free_function != NULL) {
            free_function(cur->item);
        }
        free(cur);
        cur = next;
    }

    list->head = NULL;
    list->tail = NULL;
    list->size = 0;
}
void linked_list_dispose(LinkedList** list, void (*free_function)(void*)) {
    linked_list_clear(*list, free_function);
    free(*list);
    *list = NULL;
}
