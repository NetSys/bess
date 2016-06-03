#include "mem_alloc.h"
#include "module.h"

#include "metadata.h"

struct scope_component {
	char name[MT_ATTR_NAME_LEN];
	int size;

	int num_modules;
	struct module **modules;

	int8_t offset;
	uint8_t visited;
};

static struct scope_component scope_components[100];
static int curr_scope_id = 0;

/* Adds module to the current scope component. */
static void 
add_module_to_component(struct module *m, struct mt_attr *attr)
{
	struct scope_component *component = &scope_components[curr_scope_id];

	/* module has already been added to current scope component */
	for (int i = 0; i < component->num_modules; i++) {
		if (component->modules[i] == m)
			return;
	}

	if (component->num_modules == 0) {
		strcpy(component->name, attr->name);
		component->size = attr->size;
		component->num_modules = 1;
		component->modules = mem_alloc(sizeof(struct module *));
		component->modules[0] = m;
	} else {
		component->num_modules++;
		component->modules = mem_realloc(component->modules,
				sizeof(struct module *) * component->num_modules);
		component->modules[component->num_modules - 1] = m;		
	}
}

static void 
identify_scope_component(struct module *m, struct mt_attr *attr);

/* Traverses graph upstream to help identify a scope component. */
static void traverse_upstream(struct module *m, struct mt_attr *attr)
{
	struct mt_attr *found_attr = NULL;

	for (int i = 0; i < m->num_attrs; i++) {
		struct mt_attr *curr_attr = &m->attrs[i];

		if (strcmp(curr_attr->name, attr->name) == 0 &&
		    curr_attr->size == attr->size) {
			found_attr = curr_attr;
		}
	}

	/* found a module that writes the attr; end of scope component */
	if (found_attr && found_attr->mode == MT_WRITE) {
		if (found_attr->scope_id == -1)
			identify_scope_component(m, found_attr);
		return;
	}

	for (int i = 0; i < m->igates.curr_size; i++) {
		struct gate *g = m->igates.arr[i];
		struct gate *og;

		cdlist_for_each_entry(og, &g->in.ogates_upstream,
				out.igate_upstream)
		{
			traverse_upstream(og->m, attr);
		}
	}
}

/*
 * Traverses graph downstream to help identify a scope component.
 * Return value of -1 indicates that module is not part of the scope component.
 * Return value of 0 indicates that module is part of the scope component.
 */
static int traverse_downstream(struct module *m, struct mt_attr *attr)
{
	struct gate *ogate;
	struct mt_attr *found_attr = NULL;
	int in_scope = 0;

	for (int i = 0; i < m->num_attrs; i++) {
		struct mt_attr *curr_attr = &m->attrs[i];

		if (strcmp(curr_attr->name, attr->name) == 0 &&
		    curr_attr->size == attr->size) {
			found_attr = curr_attr;
		}
	}

	if (found_attr && (found_attr->mode == MT_READ || found_attr->mode == MT_UPDATE)) {
		add_module_to_component(m, found_attr);
		found_attr->scope_id = curr_scope_id;

		for (int i = 0; i < m->ogates.curr_size; i++) {
			ogate = m->ogates.arr[i];
			if (!ogate)
				continue;

			traverse_downstream(ogate->out.igate->m, attr);
		}

		traverse_upstream(m, attr);
		return 0;

	} else if (found_attr && found_attr->mode == MT_WRITE)
		return -1;

	for (int i = 0; i < m->ogates.curr_size; i++) {
		ogate = m->ogates.arr[i];
		if (!ogate)
			continue;

		if (traverse_downstream(ogate->out.igate->m, attr) != -1)
			in_scope = 1;
	}

	if (in_scope) {
		add_module_to_component(m, attr);
		traverse_upstream(m, attr);
		return 0;
	}

	return -1;
}

/*
 * Given a module that writes a attr, identifies the portion of corresponding 
 * scope component that lies downstream from this module. 
 */
static void 
identify_scope_component(struct module *m, struct mt_attr *attr)
{
	struct gate *ogate;

	add_module_to_component(m, attr);
	attr->scope_id = curr_scope_id;

	for (int i = 0; i < m->ogates.curr_size; i++) {
		ogate = m->ogates.arr[i];
		if (!ogate)
			continue;

		traverse_downstream(ogate->out.igate->m, attr);
	}
}

/* static void reset_cycle_detection()
{
	struct ns_iter iter;
	ns_init_iterator(&iter, NS_TYPE_MODULE);
	while (1) {
		struct module *m = (struct module *) ns_next(&iter);
		if (!m)
			break;
		
		m->upstream_cycle = -1;
		m->downstream_cycle = -1;
	}
	ns_release_iterator(&iter);
} */

static void prepare_metadata_computation()
{
	struct ns_iter iter;
	ns_init_iterator(&iter, NS_TYPE_MODULE);
	while (1) {
		struct module *m = (struct module *) ns_next(&iter);
		if (!m)
			break;
		
		for (int i = 0; i < m->num_attrs; i++)
			m->attrs[i].scope_id = -1;
	}
	ns_release_iterator(&iter);
}

static void cleanup_metadata_computation()
{
	for (int i = 0; i < curr_scope_id; i++)
		mem_free(scope_components[i].modules);

	memset(&scope_components, 0, 100 * sizeof(struct scope_component));
	curr_scope_id = 0;
}


static int scope_overlaps(int i1, int i2) {
	struct scope_component n1 = scope_components[i1];
	struct scope_component n2 = scope_components[i2];

	for (int i = 0; i < n1.num_modules; i++) {
		for (int j = 0; j < n2.num_modules; j++) {
			if (n1.modules[i] == n2.modules[j])
				return 1;
		}
	}
	return 0;
}

/*
 * Given a scope component id, identifies the set of all overlapping components
 * including this scope component.
 */
static void find_overlapping_components(int index, int *offset)
{
	scope_components[index].offset = *offset;
	*offset = *offset + scope_components[index].size;
	scope_components[index].visited = 1;

	for (int j = index; j < curr_scope_id; j++) {
		if (scope_components[j].visited)
			continue;

		if (scope_overlaps(index,j))
			find_overlapping_components(j, offset);
	}
}

static void fill_offset_arrays()
{
	for (int i = 0; i < curr_scope_id; i++) {
		struct module **modules = scope_components[i].modules;
		char *name = scope_components[i].name;
		int size = scope_components[i].size;
		int offset = scope_components[i].offset;
		
		/* attr not read donwstream */
		if (scope_components[i].num_modules == 1) {
			scope_components[i].offset = MT_OFFSET_NOWRITE;
			offset = MT_OFFSET_NOWRITE;
		}

		for (int j = 0; j < scope_components[i].num_modules; j++) {
			struct module *m = modules[j];

			for (int k = 0; k < m->num_attrs; k++) {
				if (strcmp(m->attrs[k].name, name) == 0 &&
				    m->attrs[k].size == size) {
					m->attr_offsets[k] = offset;
				}
			}
		}
	}
}

/* Calculates offsets after scope components are identified */
static void assign_offsets()
{
	int offset = 0;
	for (int i = 0; i < curr_scope_id; i++) {
		if (scope_components[i].visited)
			continue;

		offset = 0;
		find_overlapping_components(i, &offset);
	}

	fill_offset_arrays();
}

void check_orphan_readers()
{
	struct ns_iter iter;

	ns_init_iterator(&iter, NS_TYPE_MODULE);
	while (1) {
		struct module *m = (struct module *) ns_next(&iter);
		if (!m)
			break;

		for (int i = 0; i < m->num_attrs; i++) {
			if (m->attr_offsets[i] != MT_OFFSET_NOREAD)
				continue;

			log_warn("Metadata attr '%s/%d' of module '%s' has "
				 "no upstream module that sets the value!\n",
					m->attrs[i].name, m->attrs[i].size,
					m->name);
		}
	}
	ns_release_iterator(&iter);
}

/* Main entry point for calculating metadata offsets. */
void compute_metadata_offsets()
{
	struct ns_iter iter;

	prepare_metadata_computation();

	ns_init_iterator(&iter, NS_TYPE_MODULE);
	while (1) {
		struct module *m = (struct module *) ns_next(&iter);
		if (!m)
			break;

		for (int i = 0; i < m->num_attrs; i++) {
			struct mt_attr *attr = &m->attrs[i];

			if (attr->mode == MT_READ || attr->mode == MT_UPDATE)
				m->attr_offsets[i] = MT_OFFSET_NOREAD;
			else if (attr->mode == MT_WRITE)
				m->attr_offsets[i] = MT_OFFSET_NOWRITE;

			if (attr->mode == MT_WRITE && attr->scope_id == -1) {
				identify_scope_component(m, attr);
				curr_scope_id++;
			}
		}
	}
	ns_release_iterator(&iter);

	assign_offsets();

	for (int i = 0; i < curr_scope_id; i++) {
		log_info("scope component for %d-byte attr %s "
			 "at offset%3d: {%s", 
				scope_components[i].size,
				scope_components[i].name,
				scope_components[i].offset,
				scope_components[i].modules[0]->name);

		for (int j = 1; j < scope_components[i].num_modules; j++)
			log_info(" %s", scope_components[i].modules[j]->name);

		log_info("}\n");
	}

	check_orphan_readers();

	cleanup_metadata_computation();
}

int is_valid_attr(const char *name, int size, enum mt_access_mode mode)
{
	if (!name || strlen(name) >= MT_ATTR_NAME_LEN)
		return 0;

	if (size < 1 || size > MT_ATTR_MAX_SIZE)
		return 0;

	if (mode != MT_READ && mode != MT_WRITE && mode != MT_UPDATE)
		return 0;

	return 1;
}

int add_metadata_attr(struct module *m, const char *name, int size,
		enum mt_access_mode mode)
{
	int n = m->num_attrs;

	if (n >= MAX_ATTRS_PER_MODULE)
		return -ENOSPC;

	if (!is_valid_attr(name, size, mode))
		return -EINVAL;

	strcpy(m->attrs[n].name, name);
	m->attrs[n].size = size;
	m->attrs[n].mode = mode;
	m->attrs[n].scope_id = -1;

	m->num_attrs++;

	return n;
}
