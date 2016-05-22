#ifndef _METADATA_H_
#define _METADATA_H_

#include <stdint.h>

#define MAX_FIELDS_PER_MODULE	16

/* normal offset values are 0 or a positive value */
typedef int8_t metadata_offset_t;

/* No downstream module reads this field, so the module can skip writing */
#define MT_NOWRITE	-1

/* No upstream module writes this field, thus garbage value will be read */
#define MT_NOREAD	-2

static inline int is_valid_offset(metadata_offset_t offset)
{
	return (offset >= 0);
}

/* You can retrieve the offset of a field with get_metadata_offset() */
#define ACCESS_METADATA(pkt, offset, type) \
	(*(type *)(pkt->_metadata_buf + offset))

enum metadata_mode {
	READ,
	WRITE,
	UPDATE
};

struct metadata_field {
	char *name;
	uint8_t len;
	enum metadata_mode mode;
	int scope_id;
};

int valid_metadata_configuration();
void compute_metadata_offsets();

#endif
