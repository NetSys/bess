#ifndef _METADATA_H_
#define _METADATA_H_

#include <stdint.h>

#include <string>

// TODO(barath): Replace with inline template functions?  Also, these macros
// need to live outside of the namespace because otherwise the expansion won't
// work in all cases, but at the same time we want to eventually move them into
// the namespace.
//
// Unsafe, but faster version. for offset use mt_attr_offset().
#define _ptr_attr_with_offset(offset, pkt, type)            \
  ({                                                        \
    promise(offset >= 0);                                   \
    struct snbuf *_pkt = (pkt);                             \
    uintptr_t addr = (uintptr_t)(_pkt->_metadata + offset); \
    (type *)addr;                                           \
  })

#define _get_attr_with_offset(offset, pkt, type) \
  ({ *(_ptr_attr_with_offset(offset, pkt, type)); })

#define _set_attr_with_offset(offset, pkt, type, val)   \
  ((void)({                                             \
    type _val = (val);                                  \
    *(_ptr_attr_with_offset(offset, pkt, type)) = _val; \
  }))

// Safe version.
#define ptr_attr_with_offset(offset, pkt, type)             \
  ({                                                        \
    bess::metadata::mt_offset_t _offset = (offset);         \
    bess::metadata::IsValidOffset(_offset)                  \
        ? (type *)_ptr_attr_with_offset(_offset, pkt, type) \
        : (type *)nullptr;                                  \
  })

#define get_attr_with_offset(offset, pkt, type)                                              \
  ({                                                                                         \
    static type _zeroed;                                                                     \
    bess::metadata::mt_offset_t _offset = (offset);                                          \
    bess::metadata::IsValidOffset(_offset) ? (type)_get_attr_with_offset(_offset, pkt, type) \
                             : (type)_zeroed;                                                \
  })

#define set_attr_with_offset(offset, pkt, type, val)  \
  ((void)({                                           \
    bess::metadata::mt_offset_t _offset = (offset);   \
    if (bess::metadata::IsValidOffset(_offset))        \
      _set_attr_with_offset(_offset, pkt, type, val); \
  }))

// Slowest but easiest.
#define ptr_attr(module, attr_id, pkt, type) \
  ptr_attr_with_offset(module->attr_offsets[attr_id], pkt, type)

#define get_attr(module, attr_id, pkt, type) \
  get_attr_with_offset(module->attr_offsets[attr_id], pkt, type)

#define set_attr(module, attr_id, pkt, type, val) \
  set_attr_with_offset(module->attr_offsets[attr_id], pkt, type, val)

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
