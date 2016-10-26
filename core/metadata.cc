#include "metadata.h"

#include <string.h>

#include <algorithm>
#include <functional>
#include <map>
#include <queue>
#include <vector>

#include <glog/logging.h>

#include "module.h"

struct scope_component {
  /* identification fields */
  std::string name;
  int size;
  mt_offset_t offset;
  scope_id_t scope_id;

  /* computation state fields */
  uint8_t assigned;
  uint8_t invalid;
  std::vector<Module *> modules;
  int degree;
};

static bool scope_component_less(const struct scope_component *a,
                                 const struct scope_component *b) {
  return a->offset < b->offset;
}

static std::vector<struct scope_component> scope_components;
static std::map<Module *, scope_id_t> module_scopes;

/* TODO: make more efficient */
/* Adds module to the current scope component. */
static void add_module_to_component(Module *m, struct mt_attr *attr) {
  struct scope_component &component = scope_components.back();

  /* module has already been added to current scope component */
  for (size_t i = 0; i < component.modules.size(); i++) {
    if (component.modules[i] == m) return;
  }

  if (component.modules.size() == 0) {
    component.name = attr->name;
    component.size = attr->size;
  }
  component.modules.push_back(m);
}

static void identify_scope_component(Module *m, struct mt_attr *attr);

static struct mt_attr *find_attr(Module *m, struct mt_attr *attr) {
  struct mt_attr *curr_attr;

  for (int i = 0; i < m->num_attrs; i++) {
    curr_attr = &m->attrs[i];
    if (curr_attr->name == attr->name && curr_attr->size == attr->size) {
      return curr_attr;
    }
  }

  return nullptr;
}

/* Traverses module graph upstream to help identify a scope component. */
static void traverse_upstream(Module *m, struct mt_attr *attr) {
  struct mt_attr *found_attr;

  add_module_to_component(m, attr);
  found_attr = find_attr(m, attr);

  /* end of scope component */
  if (found_attr && found_attr->mode == MT_WRITE) {
    if (found_attr->scope_id == -1) identify_scope_component(m, found_attr);
    return;
  }

  /* cycle detection */
  if (module_scopes[m] == static_cast<int>(scope_components.size())) return;
  module_scopes[m] = static_cast<int>(scope_components.size());

  for (int i = 0; i < m->igates.curr_size; i++) {
    struct gate *g = m->igates.arr[i];
    struct gate *og;

    CDLIST_FOR_EACH_ENTRY(og, &g->in.ogates_upstream, out.igate_upstream) {
      traverse_upstream(og->m, attr);
    }
  }

  if (m->igates.curr_size == 0) scope_components.back().invalid = 1;
}

/*
 * Traverses module graph downstream to help identify a scope component.
 * Returns 0 if module is part of the scope component, -1 if not.
 */
static int traverse_downstream(Module *m, struct mt_attr *attr) {
  struct gate *ogate;
  struct mt_attr *found_attr;
  int8_t in_scope = 0;

  /* cycle detection */
  if (module_scopes[m] == static_cast<int>(scope_components.size())) return -1;
  module_scopes[m] = static_cast<int>(scope_components.size());

  found_attr = find_attr(m, attr);

  if (found_attr &&
      (found_attr->mode == MT_READ || found_attr->mode == MT_UPDATE)) {
    add_module_to_component(m, found_attr);
    found_attr->scope_id = scope_components.size();

    for (int i = 0; i < m->ogates.curr_size; i++) {
      ogate = m->ogates.arr[i];
      if (!ogate) continue;

      traverse_downstream(ogate->out.igate->m, attr);
    }

    module_scopes[m] = -1;
    traverse_upstream(m, attr);
    in_scope = 1;
    goto ret;

  } else if (found_attr) {
    module_scopes[m] = -1;
    in_scope = 0;
    goto ret;
  }

  for (int i = 0; i < m->ogates.curr_size; i++) {
    ogate = m->ogates.arr[i];
    if (!ogate) continue;

    if (traverse_downstream(ogate->out.igate->m, attr) != -1) in_scope = 1;
  }

  if (in_scope) {
    add_module_to_component(m, attr);
    module_scopes[m] = -1;
    traverse_upstream(m, attr);
  }

ret:
  return in_scope ? 0 : -1;
}

/* Wrapper for identifying scope components */
static void identify_single_scope_component(Module *m, struct mt_attr *attr) {
  scope_components.emplace_back();
  identify_scope_component(m, attr);
  scope_components.back().scope_id = scope_components.size();
}

/* Given a module that writes an attr,
 * identifies the corresponding scope component. */
static void identify_scope_component(Module *m, struct mt_attr *attr) {
  struct gate *ogate;

  add_module_to_component(m, attr);
  attr->scope_id = scope_components.size();

  /* cycle detection */
  module_scopes[m] = static_cast<int>(scope_components.size());

  for (int i = 0; i < m->ogates.curr_size; i++) {
    ogate = m->ogates.arr[i];
    if (!ogate) continue;

    traverse_downstream(ogate->out.igate->m, attr);
  }
}

static void prepare_metadata_computation() {
  for (const auto &it : ModuleBuilder::all_modules()) {
    Module *m = it.second;
    if (!m) break;

    module_scopes[m] = -1;
    memset(m->scope_components, -1, sizeof(scope_id_t) * MT_TOTAL_SIZE);

    for (int i = 0; i < m->num_attrs; i++) {
      m->attrs[i].scope_id = -1;
    }
  }
}

static void cleanup_metadata_computation() {
  for (size_t i = 0; i < scope_components.size(); i++) {
    scope_components[i].modules.clear();
  }
  scope_components.clear();
}

/* TODO: simplify/optimize */

static void fill_offset_arrays() {
  std::vector<Module *> *modules;
  std::string name;
  int size;
  mt_offset_t offset;
  uint8_t invalid;
  Module *m;
  int num_modules;

  for (size_t i = 0; i < scope_components.size(); i++) {
    modules = &scope_components[i].modules;
    name = scope_components[i].name;
    size = scope_components[i].size;
    offset = scope_components[i].offset;
    invalid = scope_components[i].invalid;
    num_modules = scope_components[i].modules.size();

    /* attr not read donwstream */
    if (num_modules == 1) {
      scope_components[i].offset = MT_OFFSET_NOWRITE;
      offset = MT_OFFSET_NOWRITE;
    }

    for (size_t j = 0; j < scope_components[i].modules.size(); j++) {
      m = modules->at(j);

      for (int k = 0; k < m->num_attrs; k++) {
        if (m->attrs[k].name == name && m->attrs[k].size == size) {
          if (invalid && m->attrs[k].mode == MT_READ)
            m->attr_offsets[k] = MT_OFFSET_NOREAD;
          else if (invalid)
            m->attr_offsets[k] = MT_OFFSET_NOWRITE;
          else {
            m->attr_offsets[k] = offset;
          }
          break;
        }
      }

      if (!invalid && offset >= 0) {
        for (int l = 0; l < size; l++) m->scope_components[offset + l] = i;
      }
    }
  }
}

static int disjoint(int scope1, int scope2) {
  struct scope_component comp1 = scope_components[scope1];
  struct scope_component comp2 = scope_components[scope2];

  for (size_t i = 0; i < comp1.modules.size(); i++) {
    for (size_t j = 0; j < comp2.modules.size(); j++) {
      if (comp1.modules[i] == comp2.modules[j]) return 0;
    }
  }
  return 1;
}

static mt_offset_t next_offset(mt_offset_t curr_offset, int8_t size) {
  uint32_t overflow;
  int8_t rounded_size;

  rounded_size = align_ceil_pow2(size);

  if (curr_offset % rounded_size != 0)
    curr_offset = ((curr_offset / rounded_size) + 1) * rounded_size;

  overflow = (uint32_t)curr_offset + (uint32_t)size;

  return overflow > MT_TOTAL_SIZE ? MT_OFFSET_NOSPACE : curr_offset;
}

static void assign_offsets() {
  mt_offset_t offset = 0;
  std::priority_queue<struct scope_component *,
                      std::vector<struct scope_component *>,
                      std::function<bool(const struct scope_component *,
                                         const struct scope_component *)>>
      h(scope_component_less);
  struct scope_component *comp1;
  struct scope_component *comp2;
  uint8_t comp1_size;

  for (size_t i = 0; i < scope_components.size(); i++) {
    comp1 = &scope_components[i];

    if (comp1->invalid) {
      comp1->offset = MT_OFFSET_NOREAD;
      comp1->assigned = 1;
      continue;
    }

    if (comp1->assigned || comp1->modules.size() == 1) continue;

    offset = 0;
    comp1_size = align_ceil_pow2(comp1->size);

    for (size_t j = 0; j < scope_components.size(); j++) {
      if (i == j) continue;

      if (!disjoint(i, j) && scope_components[j].assigned)
        h.push(&scope_components[j]);
    }

    while (!h.empty() && (comp2 = h.top())) {
      h.pop();

      if (comp2->offset == MT_OFFSET_NOREAD ||
          comp2->offset == MT_OFFSET_NOWRITE ||
          comp2->offset == MT_OFFSET_NOSPACE)
        continue;

      if (offset + comp1->size > comp2->offset)
        offset = next_offset(comp2->offset + comp2->size, comp1->size);
      else
        break;
    }

    comp1->offset = offset;
    comp1->assigned = 1;
  }

  fill_offset_arrays();
}

void check_orphan_readers() {
  for (const auto &it : ModuleBuilder::all_modules()) {
    const Module *m = it.second;
    if (!m) break;

    for (int i = 0; i < m->num_attrs; i++) {
      if (m->attr_offsets[i] != MT_OFFSET_NOREAD) continue;

      LOG(WARNING) << "Metadata attr " << m->attrs[i].name << "/"
                   << m->attrs[i].size << " of module " << m->name() << " has "
                   << "no upstream module that sets the value!";
    }
  }
}

/* Debugging tool */
void log_all_scopes_per_module() {
  for (const auto &it : ModuleBuilder::all_modules()) {
    const Module *m = it.second;
    if (!m) break;

    LOG(INFO) << "Module " << m->name()
              << " part of the following scope components: ";
    for (int i = 0; i < MT_TOTAL_SIZE; i++) {
      if (m->scope_components[i] != -1)
        LOG(INFO) << "scope " << m->scope_components[i] << " at offset " << i;
    }
  }
}

static void compute_scope_degrees() {
  for (size_t i = 0; i < scope_components.size(); i++) {
    for (size_t j = i + 1; j < scope_components.size(); j++) {
      if (!disjoint(i, j)) {
        scope_components[i].degree += 1;
        scope_components[j].degree += 1;
      }
    }
  }
}

static int degreeComp(const struct scope_component &a,
                      const struct scope_component &b) {
  return b.degree - a.degree;
}

static void sort_scope_components() {
  compute_scope_degrees();
  sort(scope_components.begin(), scope_components.end(), degreeComp);
}

/* Main entry point for calculating metadata offsets. */
void compute_metadata_offsets() {
  prepare_metadata_computation();

  for (const auto &it : ModuleBuilder::all_modules()) {
    Module *m = it.second;
    if (!m) break;

    for (int i = 0; i < m->num_attrs; i++) {
      struct mt_attr *attr = &m->attrs[i];

      if (attr->mode == MT_READ || attr->mode == MT_UPDATE)
        m->attr_offsets[i] = MT_OFFSET_NOREAD;
      else if (attr->mode == MT_WRITE)
        m->attr_offsets[i] = MT_OFFSET_NOWRITE;

      if (attr->mode == MT_WRITE && attr->scope_id == -1)
        identify_single_scope_component(m, attr);
    }
  }

  sort_scope_components();
  assign_offsets();

  for (size_t i = 0; i < scope_components.size(); i++) {
    LOG(INFO) << "scope component for " << scope_components[i].size << "-byte"
              << "attr " << scope_components[i].name << "at offset "
              << scope_components[i].offset << ": {"
              << scope_components[i].modules[0]->name();

    for (size_t j = 1; j < scope_components[i].modules.size(); j++)
      LOG(INFO) << scope_components[i].modules[j]->name();

    LOG(INFO) << "}";
  }

  log_all_scopes_per_module();

  check_orphan_readers();

  cleanup_metadata_computation();
}
