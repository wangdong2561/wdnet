#ifndef __MBNET_LIST_H
#define __MBNET_LIST_H
struct list_head{
	struct list_head *next,*prev;
};

#define list_entry(link, type, member) ((type *)((char *)(link)-(unsigned long)(&((type *)0)->member)))

#define LIST_HEAD_INIT(name) { &(name), &(name) }

#define LIST_HEAD(name) struct list_head name = LIST_HEAD_INIT(name)

#define list_for_each(pos, head) for(pos = (head)->next; pos != (head); pos = pos->next)

#define list_for_each_safe(pos, n, head) for(pos = (head)->next, n = (pos)->next; pos != (head); pos = n, n = pos->next )

static inline void __list_add(struct list_head *new,
		struct list_head *prev, struct list_head *next)
{
	next->prev = new;
	new->next = next;
	new->prev = prev;
	prev->next = new;
}

static inline void __list_del(struct list_head * prev, struct list_head * next)
{
	next->prev = prev;
	prev->next = next;
}

static inline void list_head_init(struct list_head *list)
{
	list->next = list;
	list->prev = list;
}

static inline void list_add_tail(struct list_head *new, struct list_head *head)
{
	__list_add(new, head->prev, head);
}

static inline void list_del(struct list_head *entry)
{
	__list_del(entry->prev, entry->next);
	entry->next = entry;
	entry->prev = entry;
}

static inline int list_empty(const struct list_head *head)
{
	return head->next == head;
}


#endif
