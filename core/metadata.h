#ifndef _METADATA_H_
#define _METADATA_H_

#include <stdint.h>

#include <string>

namespace bess {
namespace metadata {

static const int kMetadataAttrMaxSize = 32; // In bytes, per attribute.

static const int kMaxAttrsPerModule = 16; // Max number of attributes per module.

static const int kMetadataTotalSize = 96; // Total size of all metadata in bytes.

// Normal offset values are 0 or a positive value.
typedef int8_t mt_offset_t;
typedef int16_t scope_id_t;

// No downstream module reads the attribute, so the module can skip writing.
static const mt_offset_t kMetadataOffsetNoWrite = -1;

// No upstream module writes the attribute, thus garbage value will be read.
static const mt_offset_t kMetadataOffsetNoRead = -2;

// Out of space in packet buffers for the attribute.
static const mt_offset_t kMetadataOffsetNoSpace = -3;

static inline int IsValidOffset(mt_offset_t offset) {
  return (offset >= 0);
}


enum mt_access_mode {
  MT_READ = 0,
  MT_WRITE,
  MT_UPDATE
};

struct mt_attr {
  std::string name;
  int size;
  enum mt_access_mode mode;
  int scope_id;
};

void ComputeMetadataOffsets();

char *GetScopeAttrName(scope_id_t scope_id);
int *GetScopeAttrSize(scope_id_t scope_id);

}  // namespace metadata
}  // namespace bess

#endif
