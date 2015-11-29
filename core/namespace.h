#ifndef _NAMESPACE_H
#define _NAMESPACE_H

/* namespace lookup/delete/iteration by hashtable
 * for types: mclass, module, driver, port, TC
 * 
 * Naming rule
 * [_a-zA-Z][_a-zA-Z0-9]*
 * 1-32 characters (including trailing null char)
 * */

#define SN_NAME_LEN 32 /* including trailing null char */

typedef enum {
	NS_TYPE_MCLASS,
	NS_TYPE_MODULE,
	NS_TYPE_DRIVER,
	NS_TYPE_PORT,
	NS_TYPE_TC,
	NS_TYPE_ALL,
	NS_TYPE_MAX
} ns_type_t;

struct ns_iter {
	ns_type_t type;
	struct cdlist_head *ns_elem_iter;
	struct ns_elem* next;
};

void *ns_lookup(ns_type_t type, const char *name);
int ns_is_valid_name(const char *name);
int ns_name_exists(const char *name);
int ns_insert(ns_type_t type, const char *name, void *object);
int ns_remove(const char *name);

void ns_init_iterator(struct ns_iter *iter, ns_type_t type);
void ns_release_iterator(struct ns_iter* iter);
void *ns_next(struct ns_iter *iter);

// test code
void ns_valid_name_test();
void ns_hashtable_test();
void ns_iterator_test();
void ns_table_resize_test();

#endif
