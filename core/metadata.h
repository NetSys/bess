#ifndef _METADATA_H_
#define _METADATA_H_

#include <stdint.h>

#define MAX_FIELDS_PER_MODULE	16

#define METADATA_NAME_LEN	SN_NAME_LEN

#define METADATA_MAX_SIZE	64		/* in bytes, per field */

/* normal offset values are 0 or a positive value */
typedef int8_t metadata_offset_t;

/* No downstream module reads this field, so the module can skip writing */
static const metadata_offset_t MT_OFFSET_NOWRITE = -1;

/* No upstream module writes this field, thus garbage value will be read */
static const metadata_offset_t MT_OFFSET_NOREAD = -2;

static inline int is_valid_offset(metadata_offset_t offset)
{
	return (offset >= 0);
}

/* Access a metadata field as a lvalue or a rvalue.
 * you can retrieve the offset of a field with get_metadata_offset() */
#define METADATA_OFFSET(pkt, offset, type) \
	(is_valid_offset(offset) ? \
	 	_ACCESS_METADATA(pkt, offset, type) : (type){})

/* no-check version of the above */
#define _METADATA_OFFSET(pkt, offset, type) \
	(*(type *)(pkt->_metadata_buf + offset))

enum metadata_mode {
	READ,
	WRITE,
	UPDATE
};

struct metadata_field {
	char name[METADATA_NAME_LEN];
	int size;
	enum metadata_mode mode;
	int scope_id;
};

void compute_metadata_offsets();

/* Modules should call this function to declare additional metadata
 * fields at initialization time. 
 * Returns its allocated ID (>= 0), or a negative number for error */
int register_metadata_field(struct module *m, const char *name, int size, 
		enum metadata_mode mode);

#endif
