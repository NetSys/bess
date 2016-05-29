#include "mem_alloc.h"
#include "module.h"

#include "metadata.h"

struct scope_component {
	char *name;
	uint8_t len;
	struct module **modules;
	int num_modules;
	int8_t offset;
	uint8_t visited;
};

static struct scope_component scope_components[100];
static int curr_scope_id = 0;

/* Adds module to the current scope component. */
static void 
add_module_to_component(struct module *m, struct metadata_field *field)
{
	struct scope_component *component = &scope_components[curr_scope_id];

	/* module has already been added to current scope component */
	for (int i = 0; i < component->num_modules; i++) {
		if (component->modules[i] == m)
			return;
	}

	if (component->num_modules == 0) {
		component->num_modules++;
		component->len = field->len;
		component->name = field->name;
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
identify_scope_component(struct module *m, struct metadata_field *field);

/* Traverses graph upstream to help identify a scope component. */
static void traverse_upstream(struct module *m, struct metadata_field *field)
{
	struct metadata_field *found_field = NULL;

	for (int i = 0; i < m->num_fields; i++) {
		struct metadata_field *curr_field = &m->fields[i];

		if (strcmp(curr_field->name, field->name) == 0 &&
		    curr_field->len == field->len) {
			found_field = curr_field;
		}
	}

	/* found a module that writes the field; end of scope component */
	if (found_field && found_field->mode == WRITE) {
		if (found_field->scope_id == -1)
			identify_scope_component(m, found_field);
		return;
	}

	for (int i = 0; i < m->igates.curr_size; i++) {
		struct gate *g = m->igates.arr[i];
		struct gate *og;

		cdlist_for_each_entry(og, &g->in.ogates_upstream,
				out.igate_upstream)
		{
			struct module *module = og->m;
			traverse_upstream(module, field);
		}
	}
}

/*
 * Traverses graph downstream to help identify a scope component.
 * Return value of -1 indicates that module is not part of the scope component.
 * Return value of 0 indicates that module is part of the scope component.
 */
static int traverse_downstream(struct module *m, struct metadata_field *field)
{
	struct gate *gate;
	struct metadata_field *found_field = NULL;
	int in_scope = 0;

	for (int i = 0; i < m->num_fields; i++) {
		struct metadata_field *curr_field = &m->fields[i];

		if (strcmp(curr_field->name, field->name) == 0 &&
		    curr_field->len == field->len) {
			found_field = curr_field;
		}
	}

	if (found_field && (found_field->mode == READ || found_field->mode == UPDATE)) {
		add_module_to_component(m, found_field);
		found_field->scope_id = curr_scope_id;

		for (int i = 0; i < m->ogates.curr_size; i++) {
			gate = m->ogates.arr[i];
			struct module *m_next = (struct module *)gate->arg;
			traverse_downstream(m_next, field);
		}

		traverse_upstream(m, field);
		return 0;
	} else if (found_field && found_field->mode == WRITE) {
		return -1;
	} else {
		for (int i = 0; i < m->ogates.curr_size; i++) {
			gate = m->ogates.arr[i];
			struct module *m_next = (struct module *)gate->arg;
			if (traverse_downstream(m_next, field) != -1)
				in_scope = 1;
		}

		if (in_scope) {
			add_module_to_component(m, field);
			traverse_upstream(m, field);
			return 0;
		}

		return -1;
	}
}

/*
 * Given a module that writes a field, identifies the portion of corresponding 
 * scope component that lies downstream from this module. 
 */
static void 
identify_scope_component(struct module *m, struct metadata_field *field)
{
	struct gate *gate;

	add_module_to_component(m, field);
	field->scope_id = curr_scope_id;

	for (int i = 0; i < m->ogates.curr_size; i++) {
		gate = m->ogates.arr[i];
		struct module *m_next = (struct module *)gate->arg;
		traverse_downstream(m_next, field);
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
		
		for (int i = 0; i < m->num_fields; i++)
			m->fields[i].scope_id = -1;
	}
	ns_release_iterator(&iter);
}

static void cleanup_metadata_computation()
{
	for (int i = 0; i < curr_scope_id; i++) {
		struct scope_component component = scope_components[i];
		if (component.len == 0)
			continue;
		mem_free(component.modules);
	}

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
	*offset = *offset + scope_components[index].len;
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
		int len = scope_components[i].len;
		int offset = scope_components[i].offset;
		
		/* field not read donwstream */
		if (scope_components[i].num_modules == 1) {
			scope_components[i].offset = MT_NOWRITE;
			offset = MT_NOWRITE;
		}

		for (int j = 0; j < scope_components[i].num_modules; j++) {
			struct module *m = modules[j];

			for (int k = 0; k < m->num_fields; k++) {
				if (strcmp(m->fields[k].name, name) == 0 &&
				    m->fields[k].len == len) {
					m->field_offsets[k]= offset;
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

		for (int i = 0; i < m->num_fields; i++) {
			if (m->field_offsets[i] != MT_NOREAD)
				continue;

			log_warn("Metadata field '%s' of module '%s' has "
				 "no upstream module that sets the value!\n",
					m->name, m->fields[i].name);
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

		for (int i = 0; i < m->num_fields; i++) {
			struct metadata_field *field = &m->fields[i];

			if (field->mode == READ || field->mode == UPDATE)
				m->field_offsets[i] = MT_NOREAD;
			else if (field->mode == WRITE)
				m->field_offsets[i] = MT_NOWRITE;

			if (field->mode == WRITE && field->scope_id == -1) {
				identify_scope_component(m, field);
				curr_scope_id++;
			}
		}
	}
	ns_release_iterator(&iter);

	assign_offsets();

	for (int i = 0; i < curr_scope_id; i++) {
		log_info("scope component for %d-byte field %s "
			 "at offset%3d: {%s", 
				scope_components[i].len,
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

static int 
is_valid_field(const char *name, uint8_t len, enum metadata_mode mode)
{
	if (!name || strlen(name) >= METADATA_NAME_LEN)
		return 0;

	if (len < 1 || len > METADATA_MAX_SIZE)
		return 0;

	if (mode != READ && mode != WRITE && mode != UPDATE)
		return 0;

	return 1;
}

int register_metadata_field(struct module *m, const char *name, uint8_t len, 
		enum metadata_mode mode)
{
	int n = m->num_fields;

	if (n >= MAX_FIELDS_PER_MODULE)
		return -ENOSPC;

	if (!is_valid_field(name, len, mode))
		return -EINVAL;

	strcpy(m->fields[n].name, name);
	m->fields[n].len = len;
	m->fields[n].mode = mode;
	m->fields[n].scope_id = -1;

	m->num_fields++;

	return 0;
}
