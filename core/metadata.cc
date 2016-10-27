#include "metadata.h"

#include <string.h>

#include <algorithm>
#include <functional>
#include <map>
#include <queue>
#include <vector>

#include <glog/logging.h>

#include "module.h"

namespace bess {
namespace metadata {

struct scope_component {
  scope_component() :
      name(),
      size(),
      offset(),
      scope_id(),
      assigned(),
      invalid(),
      modules(),
      degree() {}

  /* identification fields */
  std::string name;
  int size;
  mt_offset_t offset;
  scope_id_t scope_id;

  /* computation state fields */
  bool assigned;
  bool invalid;
  std::vector<Module *> modules;
  int degree;
};

static bool ScopeComponentLess(const struct scope_component *a,
                                 const struct scope_component *b) {
  return a->offset < b->offset;
}

static std::vector<struct scope_component> scope_components;
static std::map<Module *, scope_id_t> module_scopes;

// Adds module to the current scope component.
//
// TODO: make more efficient.
//
// TODO(barath): Consider making these members of a class instance, so that we
// can add modules for a specific pipeline rather than the single global
// pipeline.
static void AddModuleToComponent(Module *m, struct mt_attr *attr) {
  struct scope_component &component = scope_components.back();

  // Module has already been added to current scope component.
  if (std::find(component.modules.begin(), component.modules.end(), m) !=
      component.modules.end()) {
    return;
  }

  if (component.modules.empty()) {
    component.name = attr->name;
    component.size = attr->size;
  }
  component.modules.push_back(m);
}

static void IdentifyScopeComponent(Module *m, struct mt_attr *attr);

static struct mt_attr *FindAttr(Module *m, struct mt_attr *attr) {
  struct mt_attr *curr_attr;

  for (int i = 0; i < m->num_attrs; i++) {
    curr_attr = &m->attrs[i];
    if (curr_attr->name == attr->name && curr_attr->size == attr->size) {
      return curr_attr;
    }
  }

  return nullptr;
}

// Traverses module graph upstream to help identify a scope component.
static void TraverseUpstream(Module *m, struct mt_attr *attr) {
  struct mt_attr *found_attr;

  AddModuleToComponent(m, attr);
  found_attr = FindAttr(m, attr);

  /* end of scope component */
  if (found_attr && found_attr->mode == MT_WRITE) {
    if (found_attr->scope_id == -1) IdentifyScopeComponent(m, found_attr);
    return;
  }

  /* cycle detection */
  if (module_scopes[m] == static_cast<int>(scope_components.size())) {
    return;
  }
  module_scopes[m] = static_cast<int>(scope_components.size());

  for (int i = 0; i < m->igates.curr_size; i++) {
    struct gate *g = m->igates.arr[i];
    struct gate *og;

    CDLIST_FOR_EACH_ENTRY(og, &g->in.ogates_upstream, out.igate_upstream) {
      TraverseUpstream(og->m, attr);
    }
  }

  if (m->igates.curr_size == 0) {
    scope_components.back().invalid = true;
  }
}

// Traverses module graph downstream to help identify a scope component.
// Returns 0 if module is part of the scope component, -1 if not.
static int TraverseDownstream(Module *m, struct mt_attr *attr) {
  struct gate *ogate;
  struct mt_attr *found_attr;
  int8_t in_scope = 0;

  // cycle detection
  if (module_scopes[m] == static_cast<int>(scope_components.size())) {
    return -1;
  }
  module_scopes[m] = static_cast<int>(scope_components.size());

  found_attr = FindAttr(m, attr);

  if (found_attr &&
      (found_attr->mode == MT_READ || found_attr->mode == MT_UPDATE)) {
    AddModuleToComponent(m, found_attr);
    found_attr->scope_id = scope_components.size();

    for (int i = 0; i < m->ogates.curr_size; i++) {
      ogate = m->ogates.arr[i];
      if (!ogate) {
        continue;
      }

      TraverseDownstream(ogate->out.igate->m, attr);
    }

    module_scopes[m] = -1;
    TraverseUpstream(m, attr);
    in_scope = 1;
    return 0;
  } else if (found_attr) {
    module_scopes[m] = -1;
    in_scope = 0;
    return -1;
  }

  for (int i = 0; i < m->ogates.curr_size; i++) {
    ogate = m->ogates.arr[i];
    if (!ogate) {
      continue;
    }

    if (TraverseDownstream(ogate->out.igate->m, attr) != -1) {
      in_scope = 1;
    }
  }

  if (in_scope) {
    AddModuleToComponent(m, attr);
    module_scopes[m] = -1;
    TraverseUpstream(m, attr);
  }

  return in_scope ? 0 : -1;
}

// Wrapper for identifying scope components.
static void IdentifySingleScopeComponent(Module *m, struct mt_attr *attr) {
  scope_components.emplace_back();
  IdentifyScopeComponent(m, attr);
  scope_components.back().scope_id = scope_components.size();
}

// Given a module that writes an attr, identifies the corresponding scope
// component.
static void IdentifyScopeComponent(Module *m, struct mt_attr *attr) {
  struct gate *ogate;

  AddModuleToComponent(m, attr);
  attr->scope_id = scope_components.size();

  /* cycle detection */
  module_scopes[m] = static_cast<int>(scope_components.size());

  for (int i = 0; i < m->ogates.curr_size; i++) {
    ogate = m->ogates.arr[i];
    if (!ogate) {
      continue;
    }

    TraverseDownstream(ogate->out.igate->m, attr);
  }
}

static void PrepareMetadataComputation() {
  for (const auto &it : ModuleBuilder::all_modules()) {
    Module *m = it.second;
    if (!m) {
      break;
    }

    module_scopes[m] = -1;
    memset(m->scope_components, -1, sizeof(scope_id_t) * kMetadataTotalSize);

    for (int i = 0; i < m->num_attrs; i++) {
      m->attrs[i].scope_id = -1;
    }
  }
}

static void CleanupMetadataComputation() {
  for (auto &c : scope_components) {
    c.modules.clear();
  }
  scope_components.clear();
}

// TODO: simplify/optimize.
static void FillOffsetArrays() {
  for (size_t i = 0; i < scope_components.size(); i++) {
    std::vector<Module *> &modules = scope_components[i].modules;
    std::string name = scope_components[i].name;
    int size = scope_components[i].size;
    mt_offset_t offset = scope_components[i].offset;
    uint8_t invalid = scope_components[i].invalid;
    int num_modules = scope_components[i].modules.size();

    // attr not read donwstream.
    if (num_modules == 1) {
      scope_components[i].offset = kMetadataOffsetNoWrite;
      offset = kMetadataOffsetNoWrite;
    }

    for (Module *m : modules) {
      for (int k = 0; k < m->num_attrs; k++) {
        if (m->attrs[k].name == name && m->attrs[k].size == size) {
          if (invalid && m->attrs[k].mode == MT_READ) {
            m->attr_offsets[k] = kMetadataOffsetNoRead;
          } else if (invalid) {
            m->attr_offsets[k] = kMetadataOffsetNoWrite;
          } else {
            m->attr_offsets[k] = offset;
          }
          break;
        }
      }

      if (!invalid && offset >= 0) {
        for (int l = 0; l < size; l++) {
          m->scope_components[offset + l] = i;
        }
      }
    }
  }
}

static int Disjoint(int scope1, int scope2) {
  const struct scope_component &comp1 = scope_components[scope1];
  const struct scope_component &comp2 = scope_components[scope2];

  for (const auto &i : comp1.modules) {
    for (const auto &j : comp2.modules) {
      if (i == j) {
        return 0;
      }
    }
  }
  return 1;
}

static mt_offset_t NextOffset(mt_offset_t curr_offset, int8_t size) {
  uint32_t overflow;
  int8_t rounded_size;

  rounded_size = align_ceil_pow2(size);

  if (curr_offset % rounded_size != 0) {
    curr_offset = ((curr_offset / rounded_size) + 1) * rounded_size;
  }

  overflow = (uint32_t)curr_offset + (uint32_t)size;

  return overflow > kMetadataTotalSize ? kMetadataOffsetNoSpace : curr_offset;
}

static void AssignOffsets() {
  mt_offset_t offset = 0;
  std::priority_queue<struct scope_component *,
                      std::vector<struct scope_component *>,
                      std::function<bool(const struct scope_component *,
                                         const struct scope_component *)>>
      h(ScopeComponentLess);
  struct scope_component *comp1;
  struct scope_component *comp2;
  uint8_t comp1_size;

  for (size_t i = 0; i < scope_components.size(); i++) {
    comp1 = &scope_components[i];

    if (comp1->invalid) {
      comp1->offset = kMetadataOffsetNoRead;
      comp1->assigned = true;
      continue;
    }

    if (comp1->assigned || comp1->modules.size() == 1) {
      continue;
    }

    offset = 0;
    comp1_size = align_ceil_pow2(comp1->size);

    for (size_t j = 0; j < scope_components.size(); j++) {
      if (i == j) {
        continue;
      }

      if (!Disjoint(i, j) && scope_components[j].assigned) {
        h.push(&scope_components[j]);
      }
    }

    while (!h.empty() && (comp2 = h.top())) {
      h.pop();

      if (comp2->offset == kMetadataOffsetNoRead ||
          comp2->offset == kMetadataOffsetNoWrite ||
          comp2->offset == kMetadataOffsetNoSpace) {
        continue;
      }

      if (offset + comp1->size > comp2->offset) {
        offset = NextOffset(comp2->offset + comp2->size, comp1->size);
      } else {
        break;
      }
    }

    comp1->offset = offset;
    comp1->assigned = true;
  }

  FillOffsetArrays();
}

void CheckOrphanReaders() {
  for (const auto &it : ModuleBuilder::all_modules()) {
    const Module *m = it.second;
    if (!m) {
      break;
    }

    for (int i = 0; i < m->num_attrs; i++) {
      if (m->attr_offsets[i] != kMetadataOffsetNoRead) {
        continue;
      }

      LOG(WARNING) << "Metadata attr " << m->attrs[i].name << "/"
                   << m->attrs[i].size << " of module " << m->name() << " has "
                   << "no upstream module that sets the value!";
    }
  }
}

// Debugging tool.
void LogAllScopesPerModule() {
  for (const auto &it : ModuleBuilder::all_modules()) {
    const Module *m = it.second;
    if (!m) {
      break;
    }

    LOG(INFO) << "Module " << m->name()
              << " part of the following scope components: ";
    for (int i = 0; i < kMetadataTotalSize; i++) {
      if (m->scope_components[i] != -1) {
        LOG(INFO) << "scope " << m->scope_components[i] << " at offset " << i;
      }
    }
  }
}

static void ComputeScopeDegrees() {
  for (size_t i = 0; i < scope_components.size(); i++) {
    for (size_t j = i + 1; j < scope_components.size(); j++) {
      if (!Disjoint(i, j)) {
        scope_components[i].degree += 1;
        scope_components[j].degree += 1;
      }
    }
  }
}

static int DegreeComp(const struct scope_component &a,
                      const struct scope_component &b) {
  return b.degree - a.degree;
}

static void SortScopeComponents() {
  ComputeScopeDegrees();
  std::sort(scope_components.begin(), scope_components.end(), DegreeComp);
}

/* Main entry point for calculating metadata offsets. */
void ComputeMetadataOffsets() {
  PrepareMetadataComputation();

  for (const auto &it : ModuleBuilder::all_modules()) {
    Module *m = it.second;
    if (!m) {
      break;
    }

    for (int i = 0; i < m->num_attrs; i++) {
      struct mt_attr *attr = &m->attrs[i];

      if (attr->mode == MT_READ || attr->mode == MT_UPDATE) {
        m->attr_offsets[i] = kMetadataOffsetNoRead;
      } else if (attr->mode == MT_WRITE) {
        m->attr_offsets[i] = kMetadataOffsetNoWrite;
      }

      if (attr->mode == MT_WRITE && attr->scope_id == -1) {
        IdentifySingleScopeComponent(m, attr);
      }
    }
  }

  SortScopeComponents();
  AssignOffsets();

  for (size_t i = 0; i < scope_components.size(); i++) {
    LOG(INFO) << "scope component for " << scope_components[i].size << "-byte"
              << "attr " << scope_components[i].name << "at offset "
              << scope_components[i].offset << ": {"
              << scope_components[i].modules[0]->name();

    for (size_t j = 1; j < scope_components[i].modules.size(); j++)
      LOG(INFO) << scope_components[i].modules[j]->name();

    LOG(INFO) << "}";
  }

  LogAllScopesPerModule();

  CheckOrphanReaders();

  CleanupMetadataComputation();
}

}  // namespace metadata
}  // namespace bess
