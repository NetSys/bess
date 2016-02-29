## Generic data structure for controller interface (snobj)

`struct snobj` is a data model that is used to exchange commands and data between the BESS daemon and a controller. While its concept and usage are very similar to [JSON](www.json.org), unlike text-based JSON, snobj is designed as a binary format for data compactness and high-performance encoding/decoding.

Because C does not natively provide high-level data structures such as lists and dictionaries, the BESS daemon uses snobj to represent structured data. While the design of snobj is language independent, there are currently two bindings: C (`core/snobj.h` and `core/snobj.h`) and Python (`libbess-python/message.py`).

> NOTE: At the moment, the C binding is embedded in the BESS daemon code, and it is not yet provided as a separate library. 


### Data structure

The type of an snobj object can be either a primitive or a compound. For example, a single integer number can be an snobj object; this is different from JSON, where the "outermost" object must be compound.

* Primitive type
    * Nil
    * Integer (64bit)
    * Floating point number (double-precison)
    * String
    * BLOB (binary data)
* Compound type
    * List (a sequence of child snobj objects)
    * Map (a key-value dictionary of string -> snobj object)

Consider the following example data represented in YAML (slightly modified from <http://www.yaml.org/start.html>):

```yaml
---
given  : Chris
family : Dumars
address:
    lines:
        - 458 Walkman Dr.
        - Suite #292
    city    : Royal Oak
    state   : MI
    postal  : 48046
```

The above data is represented as an snobj object in a recursive way. The root-level type is map.

* Map
    * "given" -> "Chris"
    * "family" -> "Dumars"
    * "address" -> Map
        * "lines" -> List
            * "458 Walkman Dr."
            * "Suite #292"
        * "city" -> "Royal Oak"
        * "state" -> "MI"
        * "postal" -> 48046


### Notes

* A string cannot have null (`\0`) characters in the middle. Use the BLOB type to store arbitrary binary data.
* Currently, strings do not perform any encoding/decoding (e.g., UTF8).
* A map or a list can be empty.
* Keys for the map type must be a string.
* The index of list starts from 0, not 1.
* Internally, there is no difference between integer and unsigned integer types.

### Using snobj in your BESS module

> NOTE: Port drivers also use snobj in a similar way to module classes.

The main usage of snobj in modules is two fold: module initialization, and module-specific "query" operations. See [this document](module_details.md) for more details. Your module is (optionally) expected to:

1. For module initialization
    * receive an snobj object, as initialization parameters.
    * return an snobj object if there was an error during module initialization.
        * the object should contain information about the error.
        * the module initialization is cancelled.
        * return a NULL pointer if there was no error.
2. For query operation
    * receive an snobj object, as a command (e.g., "read this stat value").
    * return an snobj object, as a result (e.g., requested stat value).
        * the returned object will be passed to the controller, in tact.
        * if NULL was returned by the module, A "nil" snobj object will be implicitly created and given to the controller.

The following code snippet from `core/modules/vlan_push.c` shows an example (perhaps the simplest) how to use snobj to configure modules at startup or runtime. `VLANPush` is a module that encapsulates packets with a VLAN tag. Operators can specify the TCI (VLAN priority, DEI, VLAN ID, as defined in 802.1Q) value to be added to the VLAN header.

```c
static struct snobj *vpush_init(struct module *m, struct snobj *arg)
{
        return vpush_query(m, arg);
}

static struct snobj *vpush_query(struct module *m, struct snobj *q)
{
        struct vlan_push_priv *priv = get_priv(m);
        uint16_t tci;

        if (!q || snobj_type(q) != TYPE_INT)
                return snobj_err(EINVAL, "TCI must be given as an integer");

        tci = snobj_uint_get(q);

        priv->vlan_tag = htonl((0x8100 << 16) | tci);
        priv->qinq_tag = htonl((0x88a8 << 16) | tci);

        return NULL;
}

...

static const struct mclass vlan_push = {
        .name   = "VLANPush",
...
        .init   = vpush_init,
        .query  = vpush_query,
...
};
```

The TCI value can be set at module initialization (`vpush_init()`) or at runtime (`vpush_query()`). The argument passed to the module is TCI as an integer number, so the corresponding snobj object is just of single integer primitive. Since the initial and runtime initialization have the same semantics and input, `vpush_init()` simply calls `vpush_query()`. In `vpush_query()`, sanity check is performed first for the input snobj.

```c
        if (!q || snobj_type(q) != TYPE_INT)
                return snobj_err(EINVAL, "TCI must be given as an integer");
```

This code checks if the input is given and if the type of snobj is integer. If not, the module returns an "error" object (see the bottom of this document) to indicate a failure and its cause. Note the the last line of the function returns NULL, which is commonly used when there has been no error.

```c
        tci = snobj_uint_get(q);
```

This line actually transforms an (integer) snobj into a native C integer value. 


### C API

The `snobj.h` header file defines a number of functions to read and write from/to snobj instances. When in doubt, you can dump the content of an snobj object with `snobj_dump()` for debugging purpose.

#### 1. Creating/updating an snobj object

The following functions are used to create an snobj object with a primitive type:

```c
struct snobj *snobj_nil(void);
struct snobj *snobj_int(int64_t v);
struct snobj *snobj_uint(uint64_t v);
struct snobj *snobj_double(double v);
struct snobj *snobj_blob(const void *data, size_t size);
struct snobj *snobj_str(const char *str);
struct snobj *snobj_str_fmt(const char *fmt, ...);  /* similar to printf() */
```

The content `data` and `str` will be copied to a separate buffer, so it is fine to free() after the function call. `str` and `fmt` must be a null-terminated string.

For compound types, use these functions:

```c
struct snobj *snobj_list(void);
int snobj_list_add(struct snobj *m, struct snobj *child);

struct snobj *snobj_map(void);
int snobj_map_set(struct snobj *m, const char *key, struct snobj *val);
```

A newly created list or map is empty. Use `snobj_list_add()`/`snobj_map_set()` to add elements. The following code shows how to create the object in the first example.

```c
struct snobj *chris = snobj_map();
snobj_map_set(chris, "given", snobj_str("Chris"));
snobj_map_set(chris, "family", snobj_str("Dumars"));

{
        struct snobj *addr = snobj_map();
        
        {
                struct snobj *lines = snobj_list();
                
                snobj_line_add(lines, snobj_str("458 Walkman Dr."));
                snobj_line_add(lines, snobj_str("Suite #292"));
                
                snobj_map_set(addr, "lines", lines);
        }
        
        snobj_map_set(addr, "city", snobj_str("Royal Oak"));
        snobj_map_set(addr, "state", snobj_str("MI"));
        snobj_map_set(addr, "postal", snobj_int(48046));

        snobj_map_set(chris, "address", addr);
        /* you don't need to call snobj_free(addr) */
}
```

We will come back to this example below.

#### 2. Reading an snobj object

You can check the type of an object with `snobj_type()`. The type is defined as follows:

```c
enum snobj_type {
        TYPE_NIL,
        TYPE_INT,
        TYPE_DOUBLE,
        TYPE_STR,
        TYPE_BLOB,
        TYPE_LIST,
        TYPE_MAP,
};
```

Once the type is known, you can use one of the following functions to retrieve the value.

```c
int64_t snobj_int_get(const struct snobj *m);
uint64_t snobj_uint_get(const struct snobj *m);
double snobj_double_get(const struct snobj *m);
char *snobj_str_get(const struct snobj *m);
void *snobj_blob_get(const struct snobj *m);
struct snobj *snobj_map_get(const struct snobj *m, const char *key);
struct snobj *snobj_list_get(const struct snobj *m, int idx);
```

> NOTE: An snobj can be set as a signed integer but read as an unsigned integer, or the other way around. 

`snobj_str_get()` and `snobj_blob_get()` do not copy the content. You should not free the pointer 

These functions will return a special value on error conditions: 1) the actual type of an instance does not match the function, 2) `key` does not exist in the map, or 3) `idx` is out of bounds.

Function                | Return value
:---                    | :---
`snobj_int_get()`       | `0`
`snobj_uint_get()`      | `0`
`snobj_double_get()`    | `NAN` (as defined in `math.h`)
`snobj_str_get()`       | `NULL`
`snobj_blob_get()`      | `NULL`
`snobj_map_get()`       | `NULL`
`snobj_list_get()`      | `NULL`


#### 3. `snobj_eval()` and `snobj_eval_*()` functions

The API provides a family of functions to conveniently fetch values in a nested data structure. 

```c
struct snobj *snobj_eval(const struct snobj *m, const char *expr);
int snobj_eval_exists(const struct snobj *m, const char *expr);
```

The following `snobj_eval_<type>()` functions are a combination of `snobj_eval()` and `snobj_<type>_get()` functions; i.e., they return a primitive value rather than an embedded snobj object.

```c
int64_t snobj_eval_int(const struct snobj *m, const char *expr);
uint64_t snobj_eval_uint(const struct snobj *m, const char *expr);
double snobj_eval_double(const struct snobj *m, const char *expr);
int64_t snobj_eval_str(const struct snobj *m, const char *expr);
void *snobj_eval_blob(const struct snobj *m, const char *expr);
```

`expr` is an expression to specify which embedded "member" to be fetched. The expression format is very simple: use a dot-separated string for maps, and `[<index>]` for lists. Consider the "Chris Dumars" example above:

Call                                        | Return value
:---                                        | :---
`snobj_eval(chris, "")`                     | a map-type snobj (`chris` itself)
`snobj_eval(chris, "family")`               | a string-type snobj
`snobj_eval_str(chris, "family")`           | `"Dumars"`
`snobj_eval_exists(chris, "given")`         | `1`
`snobj_eval_exists(chris, "middle")`        | `0`
`snobj_eval_int(chris, "address.postal")`   | `48046`
`snobj_eval(chris, "address.lines")`        | a list-type snobj
`snobj_eval_str(chris, "address.lines[1]")` | `"Suite #292"`


#### 4. Deallocation and reference counting

Since every snobj instance (whether it is root or embedded) is stored in a dynamically allocated memory, snobj internally manages a reference counter for each instance to ensure its memory is properly deallocated when the counter reaches zero. *While you do not need to worry about deallocation in most cases, it is important to understand how object lifetime is managed to avoid potential memory leaks*. There are a few rules to remember:

* When an snobj instance is allocated, its initial reference counter is 1 (the only holder is "you").
* `snobj_acquire()` increments the counter by 1.
* `snobj_free()` decreases the counter by 1 (you "release" the object). If it becomes 0 (i.e., no one else is using the object), the function will actually deallocate memory.
* `snobj_*_get()` and `snobj_eval_*()` do **NOT** affect any reference counters of instances (root or embedded).
* You implicitly "hand over" your reference to an snobj instance when you pass it as a *child* to be embedded in another map/list object.
  * `snobj_list_add()` and `snobj_map_set()` are such functions.
  * If you want to keep using the child object, you should call `snobj_acquire()` before embedding, and `snobj_free()` after you are done with the object.


### Python API

TODO

### "Error" snobj objects

TODO