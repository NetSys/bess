#include "mem_alloc.h"
#include "module.h"

#include "metadata.h"

struct scope_component {
	char name[MT_ATTR_NAME_LEN];
	int size;

	int num_modules;
	//struct module_info *modules;
	struct module **modules;

	mt_offset_t offset;
	uint8_t assigned;
	uint8_t invalid;
};

/* links module to scope component */
/* struct module_info {
	struct module *module;
	uint8_t index;
	enum mt_access_mode mode;
}; */

static struct scope_component *scope_components;
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

	add_module_to_component(m, attr);

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

	/* cycle detection */
	if (m->curr_scope == curr_scope_id)
		return;
	m->curr_scope = curr_scope_id;
	
	for (int i = 0; i < m->igates.curr_size; i++) {
		struct gate *g = m->igates.arr[i];
		struct gate *og;

		cdlist_for_each_entry(og, &g->in.ogates_upstream,
				out.igate_upstream)
		{
			traverse_upstream(og->m, attr);
		}
	}

	if (m->igates.curr_size == 0)
		scope_components[curr_scope_id].invalid = 1;
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

	/* cycle detection */
	if (m->curr_scope == curr_scope_id)
		return -1;
	m->curr_scope = curr_scope_id;

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

		m->curr_scope = -1;
		traverse_upstream(m, attr);
		return 0;

	} else if (found_attr && found_attr->mode == MT_WRITE) {
		m->curr_scope = -1;
		return -1;
	}

	for (int i = 0; i < m->ogates.curr_size; i++) {
		ogate = m->ogates.arr[i];
		if (!ogate)
			continue;

		if (traverse_downstream(ogate->out.igate->m, attr) != -1)
			in_scope = 1;
	}

	if (in_scope) {
		add_module_to_component(m, attr);
		m->curr_scope = -1;
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

	/* cycle detection */
	m->curr_scope = curr_scope_id;

	for (int i = 0; i < m->ogates.curr_size; i++) {
		ogate = m->ogates.arr[i];
		if (!ogate)
			continue;

		traverse_downstream(ogate->out.igate->m, attr);
	}
}

static void prepare_metadata_computation()
{
	struct ns_iter iter;
	ns_init_iterator(&iter, NS_TYPE_MODULE);
	while (1) {
		struct module *m = (struct module *) ns_next(&iter);
		if (!m)
			break;

		m->curr_scope = -1;
		
		for (int i = 0; i < m->num_attrs; i++) {
			m->attrs[i].scope_id = -1;
		}
	}
	ns_release_iterator(&iter);
}

static void cleanup_metadata_computation()
{
	for (int i = 0; i < curr_scope_id; i++) {
		mem_free(scope_components[i].modules);
	}
	
	mem_free(scope_components);
	curr_scope_id = 0;
}


static void fill_offset_arrays()
{
	struct module **modules;
	char *name;
	int size;
	mt_offset_t offset;
	uint8_t invalid;
	struct module *m;
	uint8_t upstream_attr;

	for (int i = 0; i < curr_scope_id; i++) {
		modules = scope_components[i].modules;
		name = scope_components[i].name;
		size = scope_components[i].size;
		offset = scope_components[i].offset;
		invalid = scope_components[i].invalid; 

		/* attr not read donwstream */
		if (scope_components[i].num_modules == 1) {
			scope_components[i].offset = MT_OFFSET_NOWRITE;
			offset = MT_OFFSET_NOWRITE;
		}

		for (int j = 0; j < scope_components[i].num_modules; j++) {
			m = modules[j];
			upstream_attr = 1;

			for (int k = 0; k < m->num_attrs; k++) {
				if (strcmp(m->attrs[k].name, name) == 0 &&
				    m->attrs[k].size == size) {
					upstream_attr = 0;
					if (invalid && m->attrs[k].mode == MT_READ)
						m->attr_offsets[k] = MT_OFFSET_NOREAD;
					else if (invalid)
						m->attr_offsets[k] = MT_OFFSET_NOWRITE;
					else
						m->attr_offsets[k] = offset;
				}
			}

			if (upstream_attr) {
				m->num_upstream_attrs++;
				m->upstream_attrs[m->num_upstream_attrs - 1] = name;
				m->upstream_offsets[m->num_upstream_attrs - 1] = offset;	
			}
		}
	}
}

/*
 * Checks if two scope components are disjoint.
 */
static int disjoint(int scope1, int scope2)
{
	struct scope_component comp1 = scope_components[scope1];
	struct scope_component comp2 = scope_components[scope2];

	for (int i = 0; i < comp1.num_modules; i++) {
		for (int j = 0; j < comp2.num_modules; j++) {
			if (comp1.modules[i] == comp2.modules[j])
				return 0; 
		}
	}	
	return 1;
}

/* 
 * Returns the smallest power of two that is...
 * 1). greater than or equal to num
 * 2). smaller than or equal to 64 
 */
static uint8_t next_power_of_two(int8_t num)
{
	int i = 7;

	while(i) {
		if (num >> i == 1 && (num << (8-i) & 0xFF) == 0)
			return num;
		else if (num >> i == 1)
			return 1 << (i + 1);
		
		i--;
	}
	return 0;
}	

static mt_offset_t next_offset(mt_offset_t curr_offset, int8_t size)
{
	uint32_t overflow = (uint32_t) curr_offset + (uint32_t) size;
	int8_t rounded_size = next_power_of_two(size);

	if (curr_offset % rounded_size != 0)
		curr_offset = ((curr_offset / rounded_size) + 1) * rounded_size;
	
	return overflow > MT_TOTAL_SIZE ? MT_OFFSET_NOSPACE : curr_offset;
}


static void assign_offsets()
{
	mt_offset_t offset = 0;
	struct heap h;
	struct scope_component *comp1;
	struct scope_component *comp2;
	uint8_t comp1_size;

	for (int i = 0; i < curr_scope_id; i++) {
		comp1 = &scope_components[i];
		if (comp1->invalid) {
			offset = MT_OFFSET_NOREAD;
			comp1->offset = offset;
			comp1->assigned = 1;
			continue;
		}
		
		if (comp1->assigned || comp1->num_modules == 1)
			continue;

		offset = 0;
		comp1_size = next_power_of_two(comp1->size);
		heap_init(&h);


		for (int j = 0; j < curr_scope_id; j++) {
			if (i == j)
				continue;
			
			if (!disjoint(i,j) && scope_components[j].assigned)
				heap_push(&h, scope_components[j].offset, &scope_components[j]);
		}

		while ((comp2 = (struct scope_component *)heap_peek(&h))) 
		{
			heap_pop(&h);

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

		heap_close(&h);
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

static void allocate_scope_components()
{
	if (curr_scope_id == 0)
		scope_components = mem_alloc(sizeof(struct scope_component) * 100);
	else
		scope_components = mem_realloc(scope_components, sizeof(struct scope_component)
					   * 100 * ((curr_scope_id / 100) + 1));
	return;
}


/* Debugging tool for upstream attributes */
void log_upstream_attrs()
{
	struct ns_iter iter;

	ns_init_iterator(&iter, NS_TYPE_MODULE);

	while (1) {
		struct module *m = (struct module *) ns_next(&iter);
		if (!m)
			break;

		log_info("Module %s preserving the following upstream attrs: ", m->name);
		for (int i = 0; i < m->num_upstream_attrs; i++) {
			log_info("%s at offset %d, ", m->upstream_attrs[i], m->upstream_offsets[i]);
		}
		log_info("\n");
	}
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
			    	if (curr_scope_id % 100 == 0) 
					allocate_scope_components();
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

	log_upstream_attrs();

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
