#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <stdarg.h>

#include "snobj.h"

#define DEF_LIST_SLOTS	4
#define DEF_MAP_SLOTS	4

static void snobj_do_free(struct snobj *m)
{
	int i;

	switch (m->type) {
	case TYPE_STR:
	case TYPE_BLOB:
		_FREE(m->data);
		break;

	case TYPE_LIST:
		for (i = 0; i < m->size; i++)
			snobj_free(m->list.arr[i]);

		_FREE(m->list.arr);
		break;

	case TYPE_MAP:
		for (i = 0; i < m->size; i++) {
			_FREE(m->map.arr_k[i]);
			snobj_free(m->map.arr_v[i]);
		}

		_FREE(m->map.arr_k);
		_FREE(m->map.arr_v);
		break;

	default:
		; /* do nothing */
	} 

	_FREE(m);
}

void snobj_free(struct snobj *m)
{
	if (!m)
		return;

	assert(m->refcnt);

	m->refcnt--;

	if (m->refcnt == 0) {
		snobj_do_free(m);
	}
}

struct snobj *snobj_nil()
{
	struct snobj *m;
	
	m = _ALLOC(sizeof(struct snobj));
	m->refcnt = 1;
	
	return m;
}

struct snobj *snobj_int(int64_t v)
{
	struct snobj *m = snobj_nil();

	m->type = TYPE_INT;
	m->size = 8;
	m->int_value = v;

	return m;
}

struct snobj *snobj_uint(uint64_t v)
{
	return snobj_int((int64_t) v);
}

struct snobj *snobj_double(double v)
{
	struct snobj *m = snobj_nil();

	m->type = TYPE_DOUBLE;
	m->size = 8;
	m->double_value = v;

	return m;
}

struct snobj *snobj_blob(const void *data, size_t size)
{
	struct snobj *m;
	void *data_copied;

	if (!data || !size)
		return NULL;

	data_copied = _ALLOC(size);

	memcpy(data_copied, data, size);

	m = snobj_nil();
	m->type = TYPE_BLOB;
	m->size = size;
	m->data = data_copied;

	return m;
}

struct snobj *snobj_str(const char *str)
{
	struct snobj *m;
	void *str_copied;

	if (!str)
		return NULL;

	str_copied = _STRDUP(str);

	m = snobj_nil();
	m->type = TYPE_STR;
	m->size = strlen(str_copied) + 1;
	m->data = str_copied;

	return m;
}

static struct snobj *snobj_str_vfmt(const char *fmt, va_list ap)
{
	const size_t init_bufsize = 128;
	char *buf;

	int ret;

	struct snobj *m;

	buf = _ALLOC(init_bufsize);
	ret = vsnprintf(buf, init_bufsize, fmt, ap);

	if (ret >= init_bufsize) {
		char *new_buf = _REALLOC(buf, ret + 1);
		int new_ret;

		buf = new_buf;
		new_ret = vsnprintf(buf, ret + 1, fmt, ap);
		assert(new_ret == ret);
	}

	m = snobj_str(buf);
	_FREE(buf);
	
	return m;
}

struct snobj *snobj_str_fmt(const char *fmt, ...)
{
	va_list ap;
	struct snobj *m;

	va_start(ap, fmt);
	m = snobj_str_vfmt(fmt, ap);
	va_end(ap);

	return m;
}

struct snobj *snobj_list()
{
	struct snobj *m = snobj_nil();

	m->type = TYPE_LIST;
	m->size = 0;
	m->max_size = DEF_LIST_SLOTS;
	m->list.arr = _ALLOC(sizeof(struct snobj *) * DEF_LIST_SLOTS);

	return m;
}

/* returns the slot idx of the new item. -1 for error for whatever reason */
struct snobj *snobj_map()
{
	struct snobj *m = snobj_nil();

	m->type = TYPE_MAP;
	m->size = 0;
	m->map.arr_k = _ALLOC(sizeof(char *) * DEF_MAP_SLOTS);
	m->map.arr_v = _ALLOC(sizeof(struct snobj *) * DEF_MAP_SLOTS);
	m->max_size = DEF_LIST_SLOTS;

	return m;
}

int snobj_list_add(struct snobj *m, struct snobj *child)
{
	int idx;

	if (m->type != TYPE_LIST || !child)
		return -1;

	if (m->size == m->max_size) {
		/* too aggressive? */
		m->max_size *= 2;
		m->list.arr = _REALLOC(m->list.arr, m->max_size * sizeof(struct snobj *));
	}

	idx = m->size;
	m->list.arr[idx] = child;
	m->size++;

	return idx;
}

/* returns -1 on error */
int snobj_list_del(struct snobj *m, int idx)
{
	int i;

	if (m->type != TYPE_LIST)
		return -1;

	if (idx < 0 || idx >= m->size)
		return -1;

	/* shift */
	for (i = idx; i < m->size - 1; i++)
		m->list.arr[i] = m->list.arr[i + 1];

	m->size--;

	return 0;
}

/* returns -1 on error. -2 if not found */
static int snobj_map_index(const struct snobj *m, const char *key)
{
	int i;

	if (m->type != TYPE_MAP)
		return -1;

	for (i = 0; i < m->size; i++) {
		if (strcmp(key, m->map.arr_k[i]) == 0)
			return i;
	}

	return -2;
}

struct snobj *snobj_map_get(const struct snobj *m, const char *key)
{
	int i;
	
	i = snobj_map_index(m, key);

	if (i < 0)
		return NULL;
	else
		return m->map.arr_v[i];
}

/* returns -1 on error.
 * if overwritten, the old conf struct is freed */
int snobj_map_set(struct snobj *m, const char *key, struct snobj *val)
{
	int i;

	if (!val)
		return -1;

	i = snobj_map_index(m, key);

	if (i == -1)
		return -1;

	if (i == -2) {
		char *key_copied = _STRDUP(key);

		/* expand? */
		if (m->size == m->max_size) {
			m->max_size *= 2;
			m->map.arr_k = _REALLOC(m->map.arr_k,
					m->max_size * sizeof(char *));
			m->map.arr_v = _REALLOC(m->map.arr_v,
					m->max_size * sizeof(struct snobj *));
		}
		
		i = m->size;
		m->map.arr_k[i] = key_copied;
		m->map.arr_v[i] = val;
		m->size = i + 1;
	} else {
		snobj_free(m->map.arr_v[i]);
		m->map.arr_v[i] = val;
	}

	return 0;
}

struct snobj *snobj_eval(const struct snobj *m, const char *expr)
{
	char buf[MAX_EXPR_LEN];
	const char *p = expr;

	strncpy(buf, expr, MAX_EXPR_LEN);
	if (buf[MAX_EXPR_LEN - 1] != '\0')
		return NULL;

	while (m && *p) {
		const char *end;
		int idx;

		switch (*p) {
		case '[':
			idx = (int)strtol(p + 1, (char **)&end, 10);
			if (*end != ']')
				return NULL;

			m = snobj_list_get(m, idx);
			p = end + 1;
			break;

		case '.':
			p++;
			break;

		default:
			end = p;
			idx = p - expr;

			for (;;) {
				if (*end == '.') break;
				if (*end == '[') break;
				if (*end == ']') break;
				if (*end == '\0') break;
				end++;
			}

			if (p == end)	/* empty token? */
				return NULL;

			buf[end - expr] = '\0';
			m = snobj_map_get(m, &buf[idx]);

			p = end;
		}
	}

	return (struct snobj *)m;
}

static void print_heading(int indent, int list_depth)
{
	printf("%*s", indent - list_depth * 2, "");

	while (list_depth--)
		printf("- ");
}

static void snobj_dump_recur(const struct snobj *m, int indent, int list_depth)
{
	const int blob_byte_limit = 16;
	const int list_item_limit = 8;

	int i;

	switch (m->type) {
	case TYPE_NIL:
		if (list_depth)
			print_heading(indent, list_depth);

		printf("<nil>\n");
		break;

	case TYPE_INT:
		if (list_depth)
			print_heading(indent, list_depth);

		printf("%ld\n", m->int_value);
		break;

	case TYPE_DOUBLE:
		if (list_depth)
			print_heading(indent, list_depth);

		printf("%f\n", m->double_value);
		break;

	case TYPE_STR:
		if (list_depth)
			print_heading(indent, list_depth);

		printf("'%s'\n", (char *)m->data);
		break;

	case TYPE_BLOB:
		if (list_depth)
			print_heading(indent, list_depth);

		printf("ptr=%p, size=%u, data=", m->data, m->size);
		for (i = 0; i < m->size; i++) {
			if (i == blob_byte_limit) {
				printf("...");
				break;
			}
			printf("%02hhx ", ((char *)m->data)[i]);
		}

		printf("\n");
		break;

	case TYPE_LIST:
		for (i = 0; i < m->size; i++) {
			struct snobj *child = m->list.arr[i];

			if (i == list_item_limit) {
				printf("(... %d more)\n", (int)m->size - list_item_limit);
				break;
			}

			snobj_dump_recur(child, indent + 2, list_depth + 1);
			list_depth = 0;
		}
		break;

	case TYPE_MAP:
		for (i = 0; i < m->size; i++) {
			struct snobj *child = m->map.arr_v[i];

			if (list_depth) {
				print_heading(indent, list_depth);
				list_depth = 0;
			} else 
				printf("%*s", indent, "");

			printf("%s: ", m->map.arr_k[i]);

			if (child->type == TYPE_LIST || child->type == TYPE_MAP)
				printf("\n");

			snobj_dump_recur(child, indent + 4, 0);
		}
		break;

	default:
		printf("INVALID_TYPE\n");
	}
}

void snobj_dump(const struct snobj *m)
{
	printf("--- (%p)\n", m);
	snobj_dump_recur(m, 0, 0);
}

struct encode_state {
	char *buf;
	size_t offset;
	size_t buf_size;
};

static void reserve_more(struct encode_state *s, size_t bytes)
{
	char *new_buf;
	size_t new_buf_size = s->buf_size;

	if (s->offset + bytes <= s->buf_size)
		return;

	while (new_buf_size < s->offset + bytes)
		new_buf_size = new_buf_size * 2;

	new_buf = _REALLOC(s->buf, new_buf_size);

	s->buf = new_buf;
	s->buf_size = new_buf_size;
}

/* return non-zero if fails */
static int snobj_encode_recur(const struct snobj *m, struct encode_state *s)
{
	int ret = 0;
	int i;

	assert(s->offset % 8 == 0);

#define NEED(x) do { reserve_more(s, (x)); } while(0)
	
	NEED(8);

	*(uint32_t *)(s->buf + s->offset) = (uint32_t) m->type;
	*(uint32_t *)(s->buf + s->offset + 4) = (uint32_t) m->size;
	s->offset += 8;

	switch (m->type) {
	case TYPE_NIL:
		/* do nothing */
		break;

	case TYPE_INT:
	case TYPE_DOUBLE:
		NEED(8);
		*(int64_t *)(s->buf + s->offset) = m->int_value;
		s->offset += 8;
		break;

	case TYPE_STR:
	case TYPE_BLOB:
		{
			size_t data_size = m->size;

			while (data_size % 8)
				data_size++;

			NEED(data_size);
			memcpy(s->buf + s->offset, m->data, m->size);
			memset(s->buf + s->offset + m->size, 0, data_size - m->size);
			s->offset += data_size;
		}
		break;

	case TYPE_LIST:
		for (i = 0; i < m->size; i++) {
			ret = snobj_encode_recur(m->list.arr[i], s);
			if (ret)
				return ret;
		}
		break;

	case TYPE_MAP:
		for (i = 0; i < m->size; i++) {
			size_t key_size; /* including zeroed trailing bytes */
			
			key_size = strlen(m->map.arr_k[i]) + 1;
			while (key_size % 8)
				key_size++;

			NEED(key_size);
			strncpy(s->buf + s->offset, m->map.arr_k[i], key_size);
			s->offset += key_size;

			ret = snobj_encode_recur(m->map.arr_v[i], s);
			if (ret)
				return ret;
		}
		break;
#undef NEED

	default:
		/* unknown type */
		ret = -1;
	}

	return ret;
}

size_t snobj_encode(const struct snobj *m, char **pbuf, size_t hint)
{
	struct encode_state s;
	int ret;

	if (hint < 16)
		hint = 16;

	if (hint > 1024)
		hint = 1024;

	s.offset = 0;
	s.buf_size = hint;
	s.buf = _ALLOC(s.buf_size);
	
	if (!s.buf)
		return 0;

	ret = snobj_encode_recur(m, &s);
	if (ret) {
		_FREE(s.buf);
		*pbuf = NULL;
		return 0;
	}

	*pbuf = s.buf;
	return s.offset;
}

struct decode_state {
	char *buf;
	size_t offset;
	size_t buf_size;
};

struct snobj *snobj_decode_recur(struct decode_state *s)
{
	struct snobj *m = NULL;

	snobj_type_t type;
	size_t size;

	int i;

	assert(s->offset % 8 == 0);

	if (s->offset + 8 > s->buf_size)
		goto err;

	type = *(snobj_type_t *)(s->buf + s->offset);
	size = *(uint32_t *)(s->buf + s->offset + 4);
	s->offset += 8;

	switch (type) {
	case TYPE_NIL:
		if (size != 0)
			goto err;
		m = snobj_nil();
		break;

	case TYPE_INT:
	case TYPE_DOUBLE:
		if (size != 8 || s->offset + 8 > s->buf_size)
			goto err;

		m = snobj_int(*(int64_t *)(s->buf + s->offset));
		s->offset += 8;
		break;

	case TYPE_STR:
	case TYPE_BLOB:
		m = (type == TYPE_STR) ?
			snobj_str(s->buf + s->offset) :
			snobj_blob(s->buf + s->offset, size);

		s->offset += size;
		while (s->offset % 8)
			s->offset++;
		break;

	case TYPE_LIST:
		m = snobj_list();

		for (i = 0; i < size; i++) {
			struct snobj *child;
			int ret;

			child = snobj_decode_recur(s);
			if (!child)
				goto err;
			
			ret = snobj_list_add(m, child);
			assert(ret == i);
		}
		break;

	case TYPE_MAP:
		m = snobj_map();

		for (i = 0; i < size; i++) {
			const char *key;
			struct snobj *child;
			int ret;

			key = s->buf + s->offset;
			s->offset += strlen(key) + 1;

			while (s->offset % 8)
				s->offset++;

			child = snobj_decode_recur(s);
			if (!child)
				goto err;
			
			ret = snobj_map_set(m, key, child);
			assert(ret == 0);
		}
		break;

	default:
		/* unknown type */
		goto err;
	}

	return m;

err:
	snobj_free(m);
	return NULL;
}

struct snobj *snobj_decode(char *buf, size_t buf_size)
{
	struct decode_state s;

	if (buf_size % 8)
		return NULL;

	s.buf = buf;
	s.offset = 0;
	s.buf_size = buf_size;

	return snobj_decode_recur(&s);
}

/* details is optional (can be NULL) */
struct snobj *snobj_err_details(int err, struct snobj *details, const char *fmt, ...)
{
	struct snobj *m;
	struct snobj *m_err;
	struct snobj *m_errmsg;

	va_list ap;
	int ret;

	/* errno should be a positive number, but humans make mistakes... */
	if (err < 0)
		err = -err;

	m = snobj_map();

	m_err = snobj_int(err);

	va_start(ap, fmt);
	m_errmsg = snobj_str_vfmt(fmt, ap);
	va_end(ap);

	ret = snobj_map_set(m, "err", m_err);
	assert(ret == 0);

	ret = snobj_map_set(m, "errmsg", m_errmsg);
	assert(ret == 0);

	if (details) {
		ret = snobj_map_set(m, "details", details);
		assert(ret == 0);
	}

	return m;
}

struct snobj *snobj_errno(int err)
{
	return snobj_err_details(err, NULL, "%s", strerror(err));
}

struct snobj *snobj_errno_details(int err, struct snobj *details)
{
	return snobj_err_details(err, details, "%s", strerror(err));
}

/* example taken from http://www.yaml.org/start.html */
static struct snobj *create_invoice()
{
	struct snobj *m = snobj_map();

	snobj_map_set(m, "invoice", snobj_int(34943));
	snobj_map_set(m, "date", snobj_str("2001-01-23"));

	{
		struct snobj *c_1 = snobj_map();
		snobj_map_set(c_1, "given", snobj_str("Chris"));
		snobj_map_set(c_1, "family", snobj_str("Dumars"));
		
		{
			struct snobj *c_1_1 = snobj_map();
			snobj_map_set(c_1_1, "lines", snobj_str("458 Walkman Dr. Suite #292"));
			snobj_map_set(c_1_1, "city", snobj_str("Royal Oak"));
			snobj_map_set(c_1_1, "state", snobj_str("MI"));
			snobj_map_set(c_1_1, "postal", snobj_int(48046));

			snobj_map_set(c_1, "address", c_1_1);
		}

		{
			struct snobj *c_1_2 = snobj_list();
			snobj_list_add(c_1_2, snobj_str("foo"));
			snobj_list_add(c_1_2, snobj_str("bar"));

			snobj_map_set(c_1, "nested", c_1_2);
		}

		snobj_map_set(m, "bill-to", c_1);
	}

	snobj_map_set(m, "ship-to", snobj_str("same"));

	{
		struct snobj *c_2 = snobj_list();

		{
			struct snobj *c_2_1 = snobj_map();
			snobj_map_set(c_2_1, "sku", snobj_str("BL394D"));
			snobj_map_set(c_2_1, "quantity", snobj_int(4));
			snobj_map_set(c_2_1, "description", snobj_str("BL394D"));
			snobj_map_set(c_2_1, "price", snobj_int(450));

			snobj_list_add(c_2, c_2_1);
		}

		{
			struct snobj *c_2_2 = snobj_map();
			snobj_map_set(c_2_2, "sku", snobj_str("BL4438H"));
			snobj_map_set(c_2_2, "quantity", snobj_int(1));
			snobj_map_set(c_2_2, "description", snobj_str("Super Hoop"));
			snobj_map_set(c_2_2, "price", snobj_int(2392));

			snobj_list_add(c_2, c_2_2);
		}

		{
			struct snobj *c_2_3 = snobj_list();
			snobj_list_add(c_2_3, snobj_str("list in"));
			snobj_list_add(c_2_3, snobj_str("another list"));

			snobj_list_add(c_2, c_2_3);
		}
		snobj_map_set(m, "product", c_2);
	}

	snobj_map_set(m, "tax", snobj_int(251));
	snobj_map_set(m, "total", snobj_int(4443));

	return m;
}

static void test_dump()
{
	struct snobj *m;

	m = snobj_nil();
	snobj_dump(m);
	snobj_free(m);

	m = snobj_int(999);
	snobj_dump(m);
	snobj_free(m);

	m = snobj_str("hello world");
	snobj_dump(m);
	snobj_free(m);

	m = snobj_list();
	snobj_list_add(m, snobj_str("foo"));
	snobj_list_add(m, snobj_str("bar"));
	snobj_list_add(m, snobj_int(1234));
	snobj_list_add(m, snobj_int(5678));
	snobj_dump(m);
	snobj_free(m);

	m = snobj_map();
	snobj_map_set(m, "baz", snobj_int(42));
	snobj_map_set(m, "kitty", snobj_str("meow"));
	snobj_dump(m);
	snobj_free(m);

	m = create_invoice();
	snobj_dump(m);
	snobj_free(m);
}

static void test_invoice()
{
	struct snobj *m = create_invoice();
	char *buf;
	size_t size;

	struct snobj *m2;
	char *buf2;
	size_t size2;

	assert(snobj_eval_exists(m, "date"));
	assert(snobj_eval_exists(m, "bill-to"));
	assert(!snobj_eval_exists(m, "name"));
	assert(snobj_eval_int(m, "invoice") == 34943);
	assert(snobj_eval_str(m, "invoice") == NULL);
	assert(snobj_eval_exists(m, "bill-to.address.city"));
	assert(!snobj_eval_exists(m, "bill-to.address.zip"));
	assert(snobj_eval_int(m, "bill-to.address.postal") == 48046);
	assert(strcmp(snobj_eval_str(m, "product[1].sku"), "BL4438H") == 0);

	size = snobj_encode(m, &buf, 32);
	assert(size > 0);

	snobj_free(m);

	m2 = snobj_decode(buf, size);
	assert(m2);

	size2 = snobj_encode(m, &buf2, 32);
	assert(size2 > 0);

	assert(size == size2);
	assert(memcmp(buf, buf2, size) == 0);

	snobj_free(m2);

	_FREE(buf);
	_FREE(buf2);
}
