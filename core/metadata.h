// Copyright (c) 2014-2016, The Regents of the University of California.
// Copyright (c) 2016-2017, Nefeli Networks, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// * Neither the names of the copyright holders nor the names of their
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#ifndef BESS_METADATA_H_
#define BESS_METADATA_H_

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

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

struct Attribute {
  Attribute() : name(), size(), mode(), scope_id() {}

  std::string name;
  size_t size;  // in bytes
  enum class AccessMode { kRead = 0, kWrite, kUpdate } mode;
  mutable int scope_id;
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

  ~ScopeComponent() {}

  const attr_id_t &attr_id() const { return attr_id_; }
  void set_attr_id(attr_id_t id) { attr_id_ = id; }

  int size() const { return size_; }
  void set_size(int size) { size_ = size; }

  mt_offset_t offset() const { return offset_; }
  void set_offset(mt_offset_t offset) { offset_ = offset; }

  scope_id_t scope_id() const { return scope_id_; }
  void set_scope_id(scope_id_t id) { scope_id_ = id; }

  bool assigned() const { return assigned_; }
  void set_assigned(bool assigned) { assigned_ = assigned; }

  bool invalid() const { return invalid_; }
  void set_invalid(bool invalid) { invalid_ = invalid; }

  const std::set<Module *> &modules() const { return modules_; }
  void add_module(Module *m) { modules_.insert(m); }
  void clear_modules() { modules_.clear(); }

  int degree() const { return degree_; }
  void incr_degree() { degree_++; }

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
  Pipeline()
      : scope_components_(),
        module_scopes_(),
        module_components_(),
        registered_attrs_() {}

  // Main entry point for calculating metadata offsets.
  int ComputeMetadataOffsets();

  // Registers attr and returns 0 if no attribute named @attr_name with size
  // other than @size has already been registered for this pipeline.
  // Returns -EINVAL on error.
  int RegisterAttribute(const std::string &attr_name, size_t size);
  void DeregisterAttribute(const std::string &attr_name);

 private:
  friend class MetadataTest;

  // Allocate and initiliaze scope component storage.
  // Returns 0 on sucess, -errno on failure.
  int PrepareMetadataComputation();

  void CleanupMetadataComputation();

  // Debugging tool.
  void LogAllScopes() const;

  // Add a module to the current scope component.
  void AddModuleToComponent(Module *m, const struct Attribute *attr);

  // Returns a pointer to an attribute if it's contained within a module.
  const struct Attribute *FindAttr(Module *m,
                                   const struct Attribute *attr) const;

  // Traverses module graph upstream to help identify a scope component.
  void TraverseUpstream(Module *m, const struct Attribute *attr);

  // Traverses module graph downstream to help identify a scope component.
  // Returns 0 if module is part of the scope component, -1 if not.
  int TraverseDownstream(Module *m, const struct Attribute *attr);

  // Wrapper for identifying scope components.
  void IdentifySingleScopeComponent(Module *m, const struct Attribute *attr);

  // Given a module that writes an attr, identifies the corresponding scope
  // component.
  void IdentifyScopeComponent(Module *m, const struct Attribute *attr);

  void FillOffsetArrays();
  void AssignOffsets();
  void ComputeScopeDegrees();

  std::vector<ScopeComponent> scope_components_;

  // Maps modules to the
  std::map<const Module *, scope_id_t> module_scopes_;

  // Maps modules to the scope componenets they belong to.
  std::map<const Module *, scope_id_t *> module_components_;

  // Keeps track of the attributes used by modules in this pipeline
  // count(=int) represents how many modules registered the attribute, and the
  // attribute is deregistered once it reaches back to 0.
  // Those modules should agree on the same size(=size_t).
  std::map<std::string, std::tuple<size_t, int> > registered_attrs_;
};

extern bess::metadata::Pipeline default_pipeline;

}  // namespace metadata
}  // namespace bess

#endif  // BESS_METADATA_H_
