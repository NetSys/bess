#include <errno.h>
#include <stddef.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "rte_hash_crc.h"

#include "namespace.h"
#include "utils/cdlist.h"

#define NS_BUCKET_SIZE_INIT 64
#define NS_BUCKET_SIZE_MAX 	1048576
#define NS_CRC_INIT	0xFFFFFFFF

struct ns_elem {
	ns_type_t type; 
	char name[SN_NAME_LEN];
	void *object;

	struct cdlist_item ns_table_bhead; 
	struct cdlist_item ns_type_iter; 
	struct cdlist_item ns_all_iter; 
};

struct ns_table {
	int bucket_size;
	int type_count;
	int item_count;

	struct cdlist_head *ns_elem_bhead; // hashtable

	struct cdlist_head ns_elem_type_iter[NS_TYPE_MAX]; // per-type iterator
	int iterator_cnt[NS_TYPE_MAX];
} ht;

static int ns_table_init()
{
	int i;

	ht.bucket_size = NS_BUCKET_SIZE_INIT;
	ht.item_count = 0;
	ht.ns_elem_bhead = malloc(ht.bucket_size * sizeof(struct cdlist_head));

	if (!ht.ns_elem_bhead)
		return -ENOMEM;

	for (i = 0; i < ht.bucket_size; i++)
		cdlist_head_init(&ht.ns_elem_bhead[i]);
	
	for (i = 0; i < NS_TYPE_MAX; i++) {
		cdlist_head_init(&ht.ns_elem_type_iter[i]);
		ht.iterator_cnt[i] = 0;
	}

	return 0;
}

static inline uint32_t ns_get_hash(const char *name) 
{
	uint32_t hash = rte_hash_crc((void *)name, strlen(name), NS_CRC_INIT);

	return hash % ht.bucket_size;
}

static inline uint32_t ns_get_hash_with_bsize(const char *name, int new_bsize) 
{
	uint32_t hash = rte_hash_crc ((void *)name, strlen(name), NS_CRC_INIT);

	return hash % new_bsize;
}

static void ns_table_print() 
{
	struct ns_elem *entry;
	struct cdlist_head *bhead;
			
	printf("== ns_table_print starts ==\n"); 
	for (int i = 0; i < ht.bucket_size; i++) {
		bhead = &ht.ns_elem_bhead[i];
		cdlist_for_each_entry(entry, bhead, ns_table_bhead) {
			printf("entry [%s] is in %d bucket\n", entry->name, i);
		}
	}
	printf("== ns_table_print ends ==\n"); 
}

static int ns_table_resize(int new_bsize) 
{
	struct ns_elem *entry;
	struct ns_elem *next;

	struct cdlist_head *new_bhead;

	struct cdlist_head *bhead;
	struct cdlist_head *to_move_bhead;

	int old_bsize = ht.bucket_size;
	
	new_bhead = malloc(new_bsize * sizeof(struct cdlist_head));
	if (!new_bhead)
		return -ENOMEM;
	
	for (int i = 0; i < new_bsize; i++)
		cdlist_head_init(&new_bhead[i]);
	
	// relocate elements
	for (int i = 0; i < old_bsize; i++) {
		bhead = &ht.ns_elem_bhead[i];
		cdlist_for_each_entry_safe(entry, next, bhead, ns_table_bhead) {
			uint32_t hash = ns_get_hash_with_bsize(entry->name, new_bsize);
			to_move_bhead = &new_bhead[hash];
			cdlist_del(&entry->ns_table_bhead);
			cdlist_add_tail(to_move_bhead, &entry->ns_table_bhead);
		}
	}
	
	ht.bucket_size = new_bsize;
	ht.ns_elem_bhead = new_bhead;

	return 0;
}

static int ns_table_rebalance() 
{
	int new_bucket_size = 0;

	if (ht.bucket_size == 0)
		return ns_table_init();

	if (ht.bucket_size < ht.item_count)
		new_bucket_size = ht.bucket_size * 2;
	else if (ht.bucket_size / 4 > ht.item_count)
		new_bucket_size = ht.bucket_size / 2;

	if (new_bucket_size == 0 || new_bucket_size >= NS_BUCKET_SIZE_MAX ||
			new_bucket_size <= NS_BUCKET_SIZE_INIT) {
		/* no resize necessary */
		return 0; 
	}

	ns_table_resize(new_bucket_size); 

	return 0;
}


/* return 1 if they are equal, return 0 if they are not equal */
static inline int ns_is_equal_str(const char *name1, const char *name2) 
{
	return !strncmp(name1, name2, SN_NAME_LEN);
}

static inline struct ns_elem *ns_lookup_elem_by_name(const char *name)
{
	struct cdlist_head *bhead;
	struct ns_elem *i,*find_i = NULL;
	
	uint32_t hash = ns_get_hash(name);

	bhead = &ht.ns_elem_bhead[hash];

	cdlist_for_each_entry(i, bhead, ns_table_bhead) {
		if (ns_is_equal_str(i->name, name)) {
			find_i = i;
			break;
		}
	}

	return find_i;
}

int ns_is_valid_name(const char *name)
{
	char c;

	int name_len = strlen(name);
	if (name_len >= SN_NAME_LEN)
		return 0;
	
	// characters should be looks like
	// [_a-zA-Z][_a-zA-Z0-9]*
	
	c = name[0];
	if ((c != '_') && !isalpha(c))
		return 0;

	for (int i = 1; i < name_len; i++) {
		c = name[i];
		if ((c != '_') && !isalnum(c))
			return 0;
	}

	return 1;
}

int ns_name_exists(const char *name) 
{
	struct ns_elem *elem;

	if (ht.item_count == 0 || !ns_is_valid_name(name))
		return 0; 
	
	elem = ns_lookup_elem_by_name(name);
	if (elem)
		return 1;

	return 0;
}

void *ns_lookup(ns_type_t type, const char *name)
{
	struct ns_elem *elem;

	assert(type >= 0);
	assert(type < NS_TYPE_ALL);
	
	if (ht.item_count == 0 || !ns_is_valid_name(name))
		return NULL; 
	
	elem = ns_lookup_elem_by_name(name);

	if (elem && (elem->type == type))
		return elem->object;

	return NULL;
}

int ns_insert(ns_type_t type, const char *name, void *object) 
{
	uint32_t hash;
	struct cdlist_head *bhead, *ihead;
	struct ns_elem *i;

	int ret;

	if ((ret = ns_table_rebalance()) != 0)
		return ret;

	if (type < 0 || type >= NS_TYPE_ALL)
		return -EINVAL;
	
	if (ht.iterator_cnt[type] || ht.iterator_cnt[NS_TYPE_ALL])
		return -EINVAL; 
	
	if (!ns_is_valid_name(name))
		return -EINVAL; 

	if (ns_name_exists(name))
		return -EEXIST; 
	
	hash = ns_get_hash(name);
	assert(hash < ht.bucket_size);

	bhead = &ht.ns_elem_bhead[hash];
	i = malloc(sizeof(struct ns_elem));
	if (!i)
		return -ENOMEM;

	cdlist_item_init(&i->ns_table_bhead);
	cdlist_item_init(&i->ns_type_iter);
	cdlist_item_init(&i->ns_all_iter);

	i->type = type;
	strcpy(i->name, name);
	i->object = object;

	cdlist_add_tail(bhead, &i->ns_table_bhead);

	ihead = &ht.ns_elem_type_iter[type];
	cdlist_add_tail(ihead, &i->ns_type_iter); //inorder insert
	
	ihead = &ht.ns_elem_type_iter[NS_TYPE_ALL];
	cdlist_add_tail(ihead, &i->ns_all_iter); //inorder insert
	
	ht.item_count++;

	return 0;
}

int ns_remove(const char *name)
{
	int ret;
	struct ns_elem *elem;

	if ((ret = ns_table_rebalance()) != 0)
		return ret;

	if (!ns_is_valid_name(name))
		return -EINVAL; 
	
	elem = ns_lookup_elem_by_name(name);
	if (!elem)
		return -ENOENT;
	
	if (ht.iterator_cnt[elem->type] || ht.iterator_cnt[NS_TYPE_ALL])
		return -EINVAL; 

	cdlist_del(&elem->ns_table_bhead);
	cdlist_del(&elem->ns_type_iter);
	cdlist_del(&elem->ns_all_iter);
	
	ht.item_count--;

	return 0;
}

void ns_init_iterator(struct ns_iter *iter, ns_type_t type)
{
	struct cdlist_head *ihead;
	
	assert(type >= 0);
	assert(type < NS_TYPE_MAX);
		
	ht.iterator_cnt[type]++;

	if (ht.item_count == 0) {
		iter->next = NULL;
		return;
	}
	
	iter->type = type;
	iter->ns_elem_iter = &ht.ns_elem_type_iter[type];
	ihead = iter->ns_elem_iter;

	if (cdlist_is_empty(ihead))
		iter->next = NULL;
	else 
		if (iter->type == NS_TYPE_ALL)
			iter->next = container_of(ihead->next, 
						struct ns_elem, ns_all_iter);
		else 
			iter->next = container_of(ihead->next, 
						struct ns_elem, ns_type_iter);
}

void ns_release_iterator(struct ns_iter* iter) 
{
	ht.iterator_cnt[iter->type]--;
}

void *ns_next(struct ns_iter *iter) 
{
	struct ns_elem *cur_elem;
	struct cdlist_head *ihead = iter->ns_elem_iter;
	
	if (!iter->next)
		return NULL;
	cur_elem = iter->next;

	if (iter->type == NS_TYPE_ALL) {
		if (cur_elem->ns_all_iter.next == (struct cdlist_item *)(ihead))
			iter->next = NULL;
		else 
			iter->next = container_of(cur_elem->ns_all_iter.next,
						struct ns_elem, ns_all_iter);

	} else {
		if (cur_elem->ns_type_iter.next == (struct cdlist_item *)(ihead))
			iter->next = NULL;
		else 
			iter->next = container_of(cur_elem->ns_type_iter.next, 
						struct ns_elem, ns_type_iter);
	}
	return cur_elem->object;
}

/* namespace (ns_table) test */
void ns_valid_name_test() 
{
	int ret;
	
	char *name1 = "_Sangjin09";		// valid
	char *name2 = "E2Classifier";	// valid	
	char *name3 = "101Source";		// invalid: cannot start with numbers
	char *name4 = "-Source";			// invliad: cannot include other than alnum
	char *name5 = "Sink.port0";		// invliad: cannot include other than alnum

	ret = ns_is_valid_name(name1);
	assert(ret);

	ret = ns_is_valid_name(name2);
	assert(ret);

	ret = ns_is_valid_name(name3);
	assert(!ret);

	ret = ns_is_valid_name(name4);
	assert(!ret);

	ret = ns_is_valid_name(name5);
	assert(!ret);

	printf("PASS: ns_valid_name_test\n");
}

void ns_hashtable_test() 
{
	int ret;

	char *class1_name = "_Sangjin09";
	char *module1_name = "E2Calssifier";
	char *driver1_name = "ixgbe";
	char *port1_name = "in1";
	char *port2_name = "in2";
	char *port3_name = "in3";

	typedef struct {
		int value;
	} object;

	object class1_obj;
	object module1_obj;
	object driver1_obj;
	object port1_obj;
	object port2_obj;

	ns_table_init();

	//1. insert and name_exist test
	ret = ns_insert(NS_TYPE_MCLASS, class1_name, &class1_obj);
	assert(!ret); // no error
	ret = ns_name_exists(class1_name);
	assert(ret);
	ret = ns_name_exists(module1_name);
	assert(!ret);
	
	//2. insert and name_exist test
	ret = ns_insert(NS_TYPE_MODULE, module1_name, &module1_obj);
	assert(!ret); // no error
	ret = ns_name_exists(class1_name);
	assert(ret);
	ret = ns_name_exists(module1_name);
	assert(ret);
	
	//3. insert same elements twice
	ret = ns_insert(NS_TYPE_DRIVER, driver1_name, &driver1_obj);
	assert(!ret); // no error
	ret = ns_insert(NS_TYPE_PORT, driver1_name, &driver1_obj);
	assert(ret == -EINVAL); // error, fail to insert

	//4. lookup test
	void* ret_obj = ns_lookup(NS_TYPE_DRIVER, driver1_name);
	assert (ret_obj == &driver1_obj);
	ret_obj = ns_lookup(NS_TYPE_PORT, driver1_name);
	assert (!ret_obj); // error, type is not matching

	//5. insert and lookup test
	ret = ns_insert(NS_TYPE_PORT, port1_name, &port1_obj);
	assert(!ret); // no error
	ret = ns_insert(NS_TYPE_PORT, port2_name, &port2_obj);
	assert(!ret); // no error
	ret_obj = ns_lookup(NS_TYPE_PORT, port1_name);
	assert (ret_obj == &port1_obj);
	ret_obj = ns_lookup(NS_TYPE_PORT, port2_name);
	assert (ret_obj == &port2_obj);
	ret_obj = ns_lookup(NS_TYPE_PORT, port3_name);
	assert (!ret_obj); // error, no such name

	// 6. remove test
	ns_remove(port2_name);
	ret_obj = ns_lookup(NS_TYPE_PORT, port2_name);
	assert (!ret_obj);
	
	printf("PASS: ns_hastable_test\n");
}

void ns_iterator_test() 
{
	char *module1_name = "Sink";
	char *module2_name = "E2Calssifier";
	char *module3_name = "E2LoadBalancer";
	char *module4_name = "Source";
	char *module5_name = "Source2";

	char *port1_name = "in1";
	char *port2_name = "in2";
	char *port3_name = "in3";
	
	char *driver1_name = "ixgbe";
	
	typedef struct {
		int value;
	} object;

	object module1_obj;
	object module2_obj;
	object module3_obj;
	object module4_obj;
	object module5_obj;

	object port1_obj;
	object port2_obj;
	object port3_obj;

	object driver1_obj;
	
	struct ns_iter iter_module;
	struct ns_iter iter_port;
	struct ns_iter iter_driver;
	struct ns_iter iter_all;
	
	int ret;
	void* ret_obj;
	int count;

	ns_table_init();

	// 1. insert elements
	ret = ns_insert(NS_TYPE_MODULE, module1_name, &module1_obj);
	assert(!ret); // no error
	ret = ns_insert(NS_TYPE_MODULE, module2_name, &module2_obj);
	assert(!ret); // no error
	ret = ns_insert(NS_TYPE_PORT, port1_name, &port1_obj);
	assert(!ret); // no error
	ret = ns_insert(NS_TYPE_PORT, port2_name, &port2_obj);
	assert(!ret); // no error
	ret = ns_insert(NS_TYPE_MODULE, module3_name, &module3_obj);
	assert(!ret); // no error
	ret = ns_insert(NS_TYPE_MODULE, module4_name, &module4_obj);
	assert(!ret); // no error
	ret = ns_insert(NS_TYPE_PORT, port3_name, &port3_obj);
	assert(!ret); // no error

	// 2. null iterator
	ns_init_iterator(&iter_driver, NS_TYPE_DRIVER);
	ret_obj = ns_next(&iter_driver);
	assert(!ret_obj); // empty iterator
	ret_obj = ns_next(&iter_driver);
	assert(!ret_obj); // empty iterator
	ns_release_iterator(&iter_driver);

	// 3. in-order element traversal test
	//XXX inorder is not a specification, but just for test
	ns_init_iterator(&iter_module, NS_TYPE_MODULE);
	ret_obj = ns_next(&iter_module);
	assert(ret_obj == &module1_obj);
	ret_obj = ns_next(&iter_module);
	assert(ret_obj == &module2_obj);
	ret_obj = ns_next(&iter_module);
	assert(ret_obj == &module3_obj);
	
	// 3-1. insert during iterator running - same type
	ret = ns_insert(NS_TYPE_MODULE, module5_name, &module5_obj);
	assert(ret < 0); // error, cannot insert during iterator of same type

	ret_obj = ns_next(&iter_module);
	assert(ret_obj == &module4_obj);

	// 3-2. insert during iterator running - different type
	ret = ns_insert(NS_TYPE_DRIVER, driver1_name, &driver1_obj);
	assert(!ret); // no error

	ret_obj = ns_next(&iter_module);
	assert(!ret_obj); // empty iterator

	// 3-3. lookup during iterator running - possible
	ret_obj = ns_lookup(NS_TYPE_DRIVER, driver1_name);
	assert (ret_obj == &driver1_obj);
	
	// 3-4. remove during iterator running - same type
	ret = ns_remove(module4_name);
	assert(ret < 0); // error, cannot insert during iterator of same type
	
	// 3-5. remove during iterator running - different type
	ret = ns_remove(driver1_name);
	assert(!ret); // no error
	
	ret_obj = ns_next(&iter_module);
	assert(!ret_obj); // empty iterator, again
	ns_release_iterator(&iter_module);

	// 4. type iterator count
	ns_init_iterator(&iter_port, NS_TYPE_PORT);
	count = 0;
	while (1) {
		ret_obj = ns_next(&iter_port);
		if (!ret_obj)
			break;
		count++;
	}
	assert(count == 3); // total 3 ports
	ns_release_iterator(&iter_port);
	
	// 4. all iterator count
	ns_init_iterator(&iter_all, NS_TYPE_ALL);
	count = 0;
	while (1) {
		ret_obj = ns_next(&iter_all);
		if (!ret_obj)
			break;
		count++;
	}
	assert(count == 7); // 4 modules, 1 driver, 3 ports
	ns_release_iterator(&iter_all);

	printf("PASS: ns_iterator_test\n");
}

void ns_table_resize_test() 
{
	char *module1_name = "Sink";
	char *module2_name = "E2Calssifier";
	char *module3_name = "E2LoadBalancer";
	char *module4_name = "Source";
	char *module5_name = "Source2";

	char *port1_name = "in1";
	char *port2_name = "in2";
	char *port3_name = "in3";
	
	typedef struct {
		int value;
	} object;

	object module1_obj;
	object module2_obj;
	object module3_obj;
	object module4_obj;

	object port1_obj;
	object port2_obj;
	object port3_obj;

	int ret;
	void* ret_obj;
	
	ns_table_init();

	//insert elements
	ret = ns_insert(NS_TYPE_MODULE, module1_name, &module1_obj);
	assert(!ret); // no error
	ret = ns_insert(NS_TYPE_MODULE, module2_name, &module2_obj);
	assert(!ret); // no error
	ret = ns_insert(NS_TYPE_PORT, port1_name, &port1_obj);
	assert(!ret); // no error
	ret = ns_insert(NS_TYPE_PORT, port2_name, &port2_obj);
	assert(!ret); // no error
	ret = ns_insert(NS_TYPE_MODULE, module3_name, &module3_obj);
	assert(!ret); // no error
	ret = ns_insert(NS_TYPE_MODULE, module4_name, &module4_obj);
	assert(!ret); // no error
	ret = ns_insert(NS_TYPE_PORT, port3_name, &port3_obj);
	assert(!ret); // no error
	
	// resize table
	ret = ns_table_resize(64);
	if (ret) {
		printf("FAIL: ns_table_resize()\n");
		return;
	}

	// lookup elements	
	ret_obj = ns_lookup(NS_TYPE_MODULE, module4_name);
	assert (ret_obj == &module4_obj);
	ret_obj = ns_lookup(NS_TYPE_MODULE, module2_name);
	assert (ret_obj == &module2_obj);
	ret_obj = ns_lookup(NS_TYPE_MODULE, module1_name);
	assert (ret_obj == &module1_obj);
	ret_obj = ns_lookup(NS_TYPE_MODULE, module3_name);
	assert (ret_obj == &module3_obj);
	ret_obj = ns_lookup(NS_TYPE_MODULE, module5_name);
	assert (!ret_obj);

	ret_obj = ns_lookup(NS_TYPE_PORT, port3_name);
	assert (ret_obj == &port3_obj);
	ret_obj = ns_lookup(NS_TYPE_PORT, port1_name);
	assert (ret_obj == &port1_obj);
	ret_obj = ns_lookup(NS_TYPE_PORT, port2_name);
	assert (ret_obj == &port2_obj);
	
	// resize table
	ret = ns_table_resize(16);
	if (ret) {
		printf("FAIL: ns_table_resize()\n");
		return;
	}
	
	// lookup elements	
	ret_obj = ns_lookup(NS_TYPE_MODULE, module4_name);
	assert (ret_obj == &module4_obj);
	ret_obj = ns_lookup(NS_TYPE_MODULE, module2_name);
	assert (ret_obj == &module2_obj);
	ret_obj = ns_lookup(NS_TYPE_MODULE, module1_name);
	assert (ret_obj == &module1_obj);
	ret_obj = ns_lookup(NS_TYPE_MODULE, module3_name);
	assert (ret_obj == &module3_obj);
	ret_obj = ns_lookup(NS_TYPE_MODULE, module5_name);
	assert (!ret_obj);

	ret_obj = ns_lookup(NS_TYPE_PORT, port3_name);
	assert (ret_obj == &port3_obj);
	ret_obj = ns_lookup(NS_TYPE_PORT, port1_name);
	assert (ret_obj == &port1_obj);
	ret_obj = ns_lookup(NS_TYPE_PORT, port2_name);
	assert (ret_obj == &port2_obj);
	
	printf("PASS: ns_table_resize_test\n");

}
