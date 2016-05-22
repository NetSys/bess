#ifndef _METADATA_H_
#define _METADATA_H_

#include <stdint.h>

#define MAX_FIELDS_PER_MODULE	16

#define ACCESS_METADATA(pkt, offset, type) \
	(*(type *)(pkt->_metadata_buf + offset))

typedef enum metadata_mode {
	READ,
	WRITE,
	UPDATE
} metadata_mode;

struct metadata_field {
	char *name;
	uint8_t len;
	metadata_mode mode;
	int scope_id;
};

#endif
