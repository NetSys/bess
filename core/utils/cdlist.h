#ifndef BESS_CORE_UTILS_CDLIST_H_
#define BESS_CORE_UTILS_CDLIST_H_

#include <stddef.h> /* offsetof */

#include "../common.h"

/* Circular, doubly linked list implementation. The idea is very similar to
 * the one in Linux kernel, while we distinguish head and item types.
 * (identical but separated for type checking)
 *
 * NOTE: all heads and items must be initiailized before using.
 *
 * Notational convention:
 * head: pointer to struct cdlist_head
 * item: pointer to struct cdlist_item
 * entry: pointer to a struct that embeds the item
 *
 * Suggested naming scheme:
 * for head: <item struct name (plural)>_<predicate>
 * for item: <head struct/var name>_<predicate>
 * (predicate can be omitted if unambiguous)
 *
 * e.g., (master has linked lists of clients)
 *
 * struct {
 *   ...
 *   struct cdlist_head clients_all;
 *   struct cdlist_head clients_lock_waiting;
 *   struct cdlist_head clients_pause_holding;
 * }
 *
 * struct client {
 *   ...
 *   struct cdlist_item master_all;
 *   struct cdlist_item master_lock_waiting;
 *   struct cdlist_item master_pause_holding;
 * }
 */

struct cdlist_item;

struct cdlist_head {
  struct cdlist_item *next;
  struct cdlist_item *prev;
};

struct cdlist_item {
  struct cdlist_item *next;
  struct cdlist_item *prev;
};

/* for static declaration of an empty list */
#define CDLIST_HEAD_INIT(name) \
  { (struct cdlist_item *) & (name), (struct cdlist_item *) & (name) }

#define CDLIST_FOR_EACH(item, head)                               \
  for (item = (head)->next; item != (struct cdlist_item *)(head); \
       item = item->next)

#define CDLIST_FOR_EACH_ENTRY(entry, head, item_member)                 \
  for (entry = CONTAINER_OF((head)->next, typeof(*entry), item_member); \
       &entry->item_member != (struct cdlist_item *)(head);             \
       entry =                                                          \
           CONTAINER_OF(entry->item_member.next, typeof(*entry), item_member))

#define CDLIST_FOR_EACH_ENTRY_SAFE(entry, next_entry, head, item_member)       \
  for (entry = CONTAINER_OF((head)->next, typeof(*entry), item_member),        \
      next_entry =                                                             \
           CONTAINER_OF(entry->item_member.next, typeof(*entry), item_member); \
       &entry->item_member != (struct cdlist_item *)(head);                    \
       entry = next_entry,                                                     \
      next_entry =                                                             \
           CONTAINER_OF(entry->item_member.next, typeof(*entry), item_member))

static inline void cdlist_head_init(struct cdlist_head *head) {
  head->next = (struct cdlist_item *)head;
  head->prev = (struct cdlist_item *)head;
};

static inline void cdlist_item_init(struct cdlist_item *item) {
  item->next = item;
  item->prev = item;
}

static inline void cdlist_add_between(struct cdlist_item *prev,
                                      struct cdlist_item *next,
                                      struct cdlist_item *item) {
  prev->next = item;
  item->next = next;
  item->prev = prev;
  next->prev = item;
};

static inline void cdlist_add_after(struct cdlist_item *prev,
                                    struct cdlist_item *item) {
  cdlist_add_between(prev, prev->next, item);
}

static inline void cdlist_add_before(struct cdlist_item *next,
                                     struct cdlist_item *item) {
  cdlist_add_between(next->prev, next, item);
}

static inline void cdlist_add_head(struct cdlist_head *head,
                                   struct cdlist_item *item) {
  cdlist_add_between((struct cdlist_item *)head, head->next, item);
};

static inline void cdlist_add_tail(struct cdlist_head *head,
                                   struct cdlist_item *item) {
  cdlist_add_between(head->prev, (struct cdlist_item *)head, item);
}

static inline int cdlist_is_hooked(const struct cdlist_item *item) {
  return item->next != item;
}

static inline void _cdlist_del(struct cdlist_item *item) {
  struct cdlist_item *next;
  struct cdlist_item *prev;

  next = item->next;
  prev = item->prev;

  prev->next = next;
  next->prev = prev;
}

static inline void cdlist_del(struct cdlist_item *item) {
  _cdlist_del(item);
  cdlist_item_init(item);
}

static inline int cdlist_is_empty(const struct cdlist_head *head) {
  return head->next == (struct cdlist_item *)head;
}

static inline int cdlist_is_single(const struct cdlist_head *head) {
  return !cdlist_is_empty(head) && (head->next == head->prev);
}

static inline struct cdlist_item *cdlist_pop_head(struct cdlist_head *head) {
  struct cdlist_item *item;

  if (cdlist_is_empty(head)) return nullptr;

  item = head->next;
  cdlist_del(item);

  return item;
}

/* The first item will become the last one. Useful for round robin.
 * It returns the original first item (or the last item after rotation).
 * It returns nullptr if the list is empty. */
static inline struct cdlist_item *cdlist_rotate_left(struct cdlist_head *head) {
  struct cdlist_item *first;
  struct cdlist_item *second;
  struct cdlist_item *last;

  if (cdlist_is_empty(head)) return nullptr;

  if (cdlist_is_single(head)) return head->next;

  first = head->next;
  second = first->next;
  last = head->prev;

  head->next = second;
  second->prev = (struct cdlist_item *)head;

  first->next = (struct cdlist_item *)head;
  head->prev = first;

  last->next = first;
  first->prev = last;

  return first;
}

/* O(N). Guaranteed to be slow. */
static inline int cdlist_count(struct cdlist_head *head) {
  struct cdlist_item *i;
  int count = 0;

  CDLIST_FOR_EACH(i, head) count++;

  return count;
}

#endif
