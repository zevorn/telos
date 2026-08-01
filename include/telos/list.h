#ifndef TELOS_LIST_H
#define TELOS_LIST_H

#include <telos/types.h>

#include <telos/container_of.h>

struct telos_list {
    struct telos_list *next;
    struct telos_list *previous;
};

#define TELOS_LIST_HEAD_INITIALIZER(name) {&(name), &(name)}
#define TELOS_LIST_HEAD(name)                                                  \
    struct telos_list name = TELOS_LIST_HEAD_INITIALIZER(name)

#define TELOS_LIST_ENTRY(node, type, member)                                   \
    TELOS_CONTAINER_OF(node, type, member)
#define TELOS_LIST_ENTRY_CONST(node, type, member)                             \
    TELOS_CONTAINER_OF_CONST(node, type, member)
#define TELOS_LIST_FIRST_ENTRY(head, type, member)                             \
    TELOS_LIST_ENTRY((head)->next, type, member)
#define TELOS_LIST_LAST_ENTRY(head, type, member)                              \
    TELOS_LIST_ENTRY((head)->previous, type, member)

#define TELOS_LIST_FOR_EACH(position, head)                                    \
    for ((position) = (head)->next; (position) != (head);                      \
         (position) = (position)->next)

#define TELOS_LIST_FOR_EACH_REVERSE(position, head)                            \
    for ((position) = (head)->previous; (position) != (head);                  \
         (position) = (position)->previous)

#define TELOS_LIST_FOR_EACH_SAFE(position, next_position, head)                \
    for ((position) = (head)->next, (next_position) = (position)->next;        \
         (position) != (head);                                                 \
         (position) = (next_position), (next_position) = (position)->next)

static inline void telos_list_initialize(struct telos_list *list)
{
    list->next = list;
    list->previous = list;
}

static inline bool telos_list_empty(const struct telos_list *list)
{
    return list->next == list;
}

static inline bool telos_list_singular(const struct telos_list *list)
{
    return !telos_list_empty(list) && list->next == list->previous;
}

static inline void telos_list_insert_between(struct telos_list *entry,
                                             struct telos_list *previous,
                                             struct telos_list *next)
{
    next->previous = entry;
    entry->next = next;
    entry->previous = previous;
    previous->next = entry;
}

static inline void telos_list_add(struct telos_list *head,
                                  struct telos_list *entry)
{
    telos_list_insert_between(entry, head, head->next);
}

static inline void telos_list_add_tail(struct telos_list *head,
                                       struct telos_list *entry)
{
    telos_list_insert_between(entry, head->previous, head);
}

static inline void telos_list_remove(struct telos_list *entry)
{
    entry->previous->next = entry->next;
    entry->next->previous = entry->previous;
    telos_list_initialize(entry);
}

static inline void telos_list_replace(struct telos_list *entry,
                                      struct telos_list *replacement)
{
    replacement->next = entry->next;
    replacement->next->previous = replacement;
    replacement->previous = entry->previous;
    replacement->previous->next = replacement;
    telos_list_initialize(entry);
}

static inline void telos_list_move(struct telos_list *head,
                                   struct telos_list *entry)
{
    telos_list_remove(entry);
    telos_list_add(head, entry);
}

static inline void telos_list_move_tail(struct telos_list *head,
                                        struct telos_list *entry)
{
    telos_list_remove(entry);
    telos_list_add_tail(head, entry);
}

static inline void telos_list_splice(struct telos_list *head,
                                     struct telos_list *source)
{
    struct telos_list *first;
    struct telos_list *last;

    if (telos_list_empty(source)) {
        return;
    }
    first = source->next;
    last = source->previous;
    first->previous = head;
    last->next = head->next;
    head->next->previous = last;
    head->next = first;
    telos_list_initialize(source);
}

static inline void telos_list_splice_tail(struct telos_list *head,
                                          struct telos_list *source)
{
    struct telos_list *first;
    struct telos_list *last;

    if (telos_list_empty(source)) {
        return;
    }
    first = source->next;
    last = source->previous;
    first->previous = head->previous;
    head->previous->next = first;
    last->next = head;
    head->previous = last;
    telos_list_initialize(source);
}

#endif
