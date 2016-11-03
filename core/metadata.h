#ifndef _METADATA_H_
#define _METADATA_H_

#include <stdint.h>

#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include <gtest/gtest_prod.h>

#include "snbuf_layout.h"

class Module;

namespace bess {
namespace metadata {

// In bytes, per attribute.
static const size_t kMetadataAttrMaxSize = 32;
static_assert(kMetadataAttrMaxSize <= SIZE_MAX,
              "Max metadata attr size check failed");

// Max number of attributes per module.
static const size_t kMaxAttrsPerModule = 16;
static_assert(kMaxAttrsPerModule <= SIZE_MAX,
              "Max metadata attrs per module check failed");

// Total size of all metadata in bytes.
static const size_t kMetadataTotalSize = SNBUF_METADATA;
static_assert(kMetadataTotalSize <= SIZE_MAX,
              "Total metadata size check failed");

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

enum class AccessMode { READ = 0, WRITE, UPDATE };

struct mt_attr {
  std::string name;
  int size;
  AccessMode mode;
  int scope_id;
};

typedef std::string attr_id_t;

class ScopeComponent {
 public:
  ScopeComponent()
      : attr_id_(),
        size_(),
        offset_(),
        scope_id_(),
        assigned_(),
        invalid_(),
        modules_(),
        degree_() {}

  ~ScopeComponent(){};

  const attr_id_t &attr_id() const { return attr_id_; };
  void set_attr_id(attr_id_t id) { attr_id_ = id; };

  int size() const { return size_; };
  void set_size(int size) { size_ = size; };

  mt_offset_t offset() const { return offset_; };
  void set_offset(mt_offset_t offset) { offset_ = offset; };

  scope_id_t scope_id() const { return scope_id_; };
  void set_scope_id(scope_id_t id) { scope_id_ = id; };

  bool assigned() const { return assigned_; };
  void set_assigned(bool assigned) { assigned_ = assigned; };

  bool invalid() const { return invalid_; };
  void set_invalid(bool invalid) { invalid_ = invalid; };

  const std::set<Module *> &modules() const { return modules_; };
  void add_module(Module *m) { modules_.insert(m); };
  void clear_modules() { modules_.clear(); };

  int degree() const { return degree_; };
  void incr_degree() { degree_++; };

  bool DisjointFrom(const ScopeComponent &rhs);

 private:
  /* identification fields */
  attr_id_t attr_id_;
  int size_;
  mt_offset_t offset_;
  scope_id_t scope_id_;

  /* computation state fields */
  bool assigned_;
  bool invalid_;
  std::set<Module *> modules_;
  int degree_;
};

class Pipeline {
 public:
  Pipeline() : scope_components_(), module_scopes_(){};

  // Debugging tool.
  void LogAllScopesPerModule();

  // Main entry point for calculating metadata offsets.
  int ComputeMetadataOffsets();

  // Registers attr and returns 0 if no attribute named attr->name with size
  // other than attr->size has already been registered for this pipeline.
  // Returns -errno on error.
  int RegisterAttribute(const struct mt_attr *attr);

 private:
  friend class MetadataTest;

  // Allocate and initiliaze scope component storage.
  // Returns 0 on sucess, -errno on failure.
  int PrepareMetadataComputation();

  void CleanupMetadataComputation();

  // Add a module to the current scope component.
  void AddModuleToComponent(Module *m, struct mt_attr *attr);

  // Returns a pointer to an attribute if it's contained within a module.
  struct mt_attr *FindAttr(Module *m, struct mt_attr *attr);

  // Traverses module graph upstream to help identify a scope component.
  void TraverseUpstream(Module *m, struct mt_attr *attr);

  // Traverses module graph downstream to help identify a scope component.
  // Returns 0 if module is part of the scope component, -1 if not.
  int TraverseDownstream(Module *m, struct mt_attr *attr);

  // Wrapper for identifying scope components.
  void IdentifySingleScopeComponent(Module *m, struct mt_attr *attr);

  // Given a module that writes an attr, identifies the corresponding scope
  // component.
  void IdentifyScopeComponent(Module *m, struct mt_attr *attr);

  void FillOffsetArrays();
  void AssignOffsets();
  void ComputeScopeDegrees();

  std::vector<ScopeComponent> scope_components_;

  // Maps modules to the
  std::map<const Module *, scope_id_t> module_scopes_;

  // Maps modules to the scope componenets they belong to.
  std::map<const Module *, scope_id_t *> module_components_;

  // Keeps track of the attributes used by modules in this pipeline
  std::map<attr_id_t, const struct mt_attr *> attributes_;
};

}  // namespace metadata
}  // namespace bess

#endif
