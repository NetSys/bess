#ifndef _METADATA_H_
#define _METADATA_H_

#include <stdint.h>

#define MT_ATTR_NAME_LEN	SN_NAME_LEN

#define MT_ATTR_MAX_SIZE	32		/* in bytes, per attribute */

#define MAX_ATTRS_PER_MODULE	16

#define MT_TOTAL_SIZE		96

/* normal offset values are 0 or a positive value */
typedef int8_t mt_offset_t;
typedef int16_t scope_id_t;

/* No downstream module reads the attribute, so the module can skip writing */
static const mt_offset_t MT_OFFSET_NOWRITE = -1;

/* No upstream module writes the attribute, thus garbage value will be read */
static const mt_offset_t MT_OFFSET_NOREAD = -2;

/* Out of space in packet buffers for the attribute. */
static const mt_offset_t MT_OFFSET_NOSPACE = -3;

static inline int is_valid_offset(mt_offset_t offset)
{
	return (offset >= 0);
}

/* unsafe, but faster version. for offset use mt_attr_offset() */
#define _ptr_attr_with_offset(offset, pkt, type) \
	({ \
		promise(offset >= 0); \
		struct snbuf *_pkt = (pkt); \
		uintptr_t addr = (uintptr_t)(_pkt->_metadata + offset); \
		(type *)addr; \
	 })

#define _get_attr_with_offset(offset, pkt, type) \
	({ \
		*(_ptr_attr_with_offset(offset, pkt, type)); \
	 })

#define _set_attr_with_offset(offset, pkt, type, val) \
	((void)({ \
		type _val = (val); \
		*(_ptr_attr_with_offset(offset, pkt, type)) = _val; \
	 }))

/* safe version */
#define ptr_attr_with_offset(offset, pkt, type) \
	({ \
		mt_offset_t _offset = (offset); \
		is_valid_offset(_offset) ? \
			(type *)_ptr_attr_with_offset(_offset, pkt, type) : \
			(type *)NULL; \
	 })

#define get_attr_with_offset(offset, pkt, type) \
	({ \
		static type _zeroed; \
		mt_offset_t _offset = (offset); \
		is_valid_offset(_offset) ? \
			(type)_get_attr_with_offset(_offset, pkt, type) : \
			(type)_zeroed; \
	 })

#define set_attr_with_offset(offset, pkt, type, val) \
	((void)({ \
		mt_offset_t _offset = (offset); \
		if (is_valid_offset(_offset)) \
			_set_attr_with_offset(_offset, pkt, type, val); \
	 }))

/* slowest but easiest */
#define ptr_attr(module, attr_id, pkt, type) \
	ptr_attr_with_offset(mt_attr_offset(module, attr_id), pkt, type)

#define get_attr(module, attr_id, pkt, type) \
	get_attr_with_offset(mt_attr_offset(module, attr_id), pkt, type)

#define set_attr(module, attr_id, pkt, type, val) \
	set_attr_with_offset(mt_attr_offset(module, attr_id), pkt, type, val)

enum mt_access_mode {
	MT_READ,
	MT_WRITE,
	MT_UPDATE
};

struct mt_attr {
	char name[MT_ATTR_NAME_LEN];
	int size;
	enum mt_access_mode mode;
	int scope_id;
};

void compute_metadata_offsets();

char *get_scope_attr_name(scope_id_t scope_id);
int *get_scope_attr_size(scope_id_t scope_id);

int is_valid_attr(const char *name, int size, enum mt_access_mode mode);

/* Modules should call this function to declare additional metadata
 * attributes at initialization time.
 * Static metadata attributes that are defined in module class are
 * automatically registered, so only attributes specific to a module 'instance'
 * need this function.
 * Returns its allocated ID (>= 0), or a negative number for error */
int add_metadata_attr(struct module *m, const char *name, int size, 
		enum mt_access_mode mode);

#endif
