#include "encjson.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <fsdyn/charstr.h>
#include <fsdyn/float.h>
#include <fsdyn/fsalloc.h>
#include <fsdyn/hashtable.h>
#include <fsdyn/list.h>

#include "encjson_int.h"
#include "encjson_version.h"

#ifndef LLONG_MIN
#define ULLONG_MAX -1ULL
#define LLONG_MAX  ((long long) (ULLONG_MAX >> 1))
#define LLONG_MIN  ~LLONG_MAX
#endif

enum {
    MAX_DECODE_NESTING_LEVELS = 200,
    JIT_SIZE_LIMIT = 30,
    JIT_ACCESS_LIMIT = 1000
};

typedef struct {
    char *name;
    json_thing_t *value;
} pair_t;

/* Optimization of arrays and objects. When we detect that linear
 * lookups are taking a "long" time, we construct a C array or hash
 * table to speed up access. */
struct json_thing {
    json_thing_type_t type;
    union {
        struct {
            list_t *elements;
            uint64_t random_access_counter;
            json_thing_t **lookup_table; /* may be NULL */
        } array;
        struct {
            list_t *fields;
            uint64_t random_access_counter;
            hash_table_t *lookup_table; /* may be NULL */
        } object;
        struct {
            char *utf8;
            size_t len;
        } string;
        struct {
            long long value;
        } integer;
        struct {
            unsigned long long value;
        } u_integer;
        struct {
            double value;
        } real;
        struct {
            bool value;
        } boolean;
        struct {
            char *repr;
        } raw;
    };
};

static const char *decode(const char *p, const char *end, json_thing_t **thing,
                          unsigned levels);
static const char *decode_string_value(const char *p, const char *end,
                                       char **value, size_t *len);

static void json_error()
{
    /* set your debugger breakpoint here */
}

static bool exhausted(const char *p, const char *end)
{
    if (p < end)
        return false;
    json_error();
    return true;
}

static json_thing_t *make_thing(json_thing_type_t type)
{
    json_thing_t *thing = fsalloc(sizeof *thing);
    thing->type = type;
    return thing;
}

json_thing_t *json_make_raw(const char *encoding)
{
    json_thing_t *thing = make_thing(JSON_RAW);
    thing->raw.repr = charstr_dupstr(encoding);
    return thing;
}

json_thing_t *json_make_integer(long long n)
{
    json_thing_t *thing = make_thing(JSON_INTEGER);
    thing->integer.value = n;
    return thing;
}

json_thing_t *json_make_unsigned(unsigned long long n)
{
    json_thing_t *thing = make_thing(JSON_UNSIGNED);
    thing->u_integer.value = n;
    return thing;
}

json_thing_t *json_make_float(double n)
{
    assert(isnormal(n));
    json_thing_t *thing = make_thing(JSON_FLOAT);
    thing->real.value = n;
    return thing;
}

json_thing_t *json_make_boolean(bool truth_value)
{
    json_thing_t *thing = make_thing(JSON_BOOLEAN);
    thing->boolean.value = truth_value;
    return thing;
}

json_thing_t *json_make_null(void)
{
    return make_thing(JSON_NULL);
}

static int is_ascii_control_character(char c)
{
    return (c & 0xe0) == 0 || c == 0x7f;
}

static int is_at_latin_control_character(const char *s)
{
    return s[0] == (char) 0xc2 && (s[1] & 0xe0) == 0x80;
}

json_thing_t *json_adopt_bounded_string(char *s, size_t size)
{
    assert(charstr_valid_utf8_bounded(s, s + size));
    json_thing_t *thing = make_thing(JSON_STRING);
    thing->string.utf8 = s;
    thing->string.len = size;
    return thing;
}

json_thing_t *json_adopt_string(char *s)
{
    return json_adopt_bounded_string(s, strlen(s));
}

json_thing_t *json_make_bounded_string(const char *s, size_t size)
{
    char *dup = fsalloc(size + 1);
    memcpy(dup, s, size);
    dup[size] = '\0';
    return json_adopt_bounded_string(dup, size);
}

json_thing_t *json_make_string(const char *s)
{
    return json_make_bounded_string(s, strlen(s));
}

json_thing_t *json_make_array(void)
{
    json_thing_t *thing = make_thing(JSON_ARRAY);
    thing->array.elements = make_list();
    thing->array.random_access_counter = 0;
    thing->array.lookup_table = NULL;
    return thing;
}

static void clobber_array(json_thing_t *array)
{
    if (!array->array.lookup_table)
        return;
    fsfree(array->array.lookup_table);
    array->array.random_access_counter = 0;
    array->array.lookup_table = NULL;
}

json_thing_t *json_add_to_array(json_thing_t *array, json_thing_t *element)
{
    assert(array->type == JSON_ARRAY);
    clobber_array(array);
    list_append(array->array.elements, element);
    return array;
}

json_thing_t *json_make_object(void)
{
    json_thing_t *thing = make_thing(JSON_OBJECT);
    thing->object.fields = make_list();
    thing->object.random_access_counter = 0;
    thing->object.lookup_table = NULL;
    return thing;
}

static void clobber_object(json_thing_t *object)
{
    if (!object->object.lookup_table)
        return;
    destroy_hash_table(object->object.lookup_table);
    object->object.random_access_counter = 0;
    object->object.lookup_table = NULL;
}

static void add_to_object(json_thing_t *object, char *key, json_thing_t *value)
{
    assert(object->type == JSON_OBJECT);
    pair_t *f = fsalloc(sizeof *f);
    clobber_object(object);
    f->name = key;
    f->value = value;
    list_append(object->object.fields, f);
}

json_thing_t *json_add_to_object(json_thing_t *object, const char *field,
                                 json_thing_t *value)
{
    add_to_object(object, charstr_dupstr(field), value);
    return object;
}

void json_destroy_thing(json_thing_t *thing)
{
    list_elem_t *e;
    switch (thing->type) {
        case JSON_ARRAY:
            clobber_array(thing);
            for (e = list_get_first(thing->array.elements); e; e = list_next(e))
                json_destroy_thing((json_thing_t *) list_elem_get_value(e));
            destroy_list(thing->array.elements);
            break;
        case JSON_OBJECT:
            clobber_object(thing);
            for (e = list_get_first(thing->object.fields); e;
                 e = list_next(e)) {
                pair_t *f = (pair_t *) list_elem_get_value(e);
                fsfree(f->name);
                json_destroy_thing(f->value);
                fsfree(f);
            }
            destroy_list(thing->object.fields);
            break;
        case JSON_STRING:
            fsfree(thing->string.utf8);
            break;
        case JSON_INTEGER:
        case JSON_UNSIGNED:
        case JSON_FLOAT:
        case JSON_BOOLEAN:
        case JSON_NULL:
            break;
        case JSON_RAW:
            fsfree(thing->raw.repr);
            break;
        default:
            assert(0);
    }
    fsfree(thing);
}

static json_thing_t *clone_array(json_thing_t *array)
{
    json_thing_t *clone = json_make_array();
    json_element_t *e;
    for (e = json_array_first(array); e; e = json_element_next(e)) {
        json_thing_t *ec = json_clone(json_element_value(e));
        if (!ec) {
            json_destroy_thing(clone);
            return NULL;
        }
        if (!json_add_to_array(clone, ec)) {
            json_destroy_thing(ec);
            json_destroy_thing(clone);
            return NULL;
        }
    }
    return clone;
}

static json_thing_t *clone_object(json_thing_t *object)
{
    json_thing_t *clone = json_make_object();
    json_field_t *f;
    for (f = json_object_first(object); f; f = json_field_next(f)) {
        json_thing_t *ec = json_clone(json_field_value(f));
        json_add_to_object(clone, json_field_name(f), ec);
    }
    return clone;
}

json_thing_t *json_clone(json_thing_t *thing)
{
    switch (thing->type) {
        case JSON_ARRAY:
            return clone_array(thing);
        case JSON_OBJECT:
            return clone_object(thing);
        case JSON_STRING:
            return json_make_string(thing->string.utf8);
        case JSON_INTEGER:
            return json_make_integer(thing->integer.value);
        case JSON_UNSIGNED:
            return json_make_unsigned(thing->u_integer.value);
        case JSON_FLOAT:
            return json_make_float(thing->real.value);
        case JSON_BOOLEAN:
            return json_make_boolean(thing->boolean.value);
        case JSON_NULL:
            return json_make_null();
        case JSON_RAW:
            return json_make_raw(thing->raw.repr);
        default:
            abort();
    }
}

json_thing_type_t json_thing_type(json_thing_t *thing)
{
    return thing->type;
}

long long json_integer_value(json_thing_t *thing)
{
    assert(thing->type == JSON_INTEGER);
    return thing->integer.value;
}

unsigned long long json_unsigned_value(json_thing_t *thing)
{
    assert(thing->type == JSON_UNSIGNED);
    return thing->u_integer.value;
}

double json_double_value(json_thing_t *thing)
{
    assert(thing->type == JSON_FLOAT);
    return thing->real.value;
}

bool json_boolean_value(json_thing_t *thing)
{
    assert(thing->type == JSON_BOOLEAN);
    return thing->boolean.value;
}

const char *json_string_value(json_thing_t *thing)
{
    assert(thing->type == JSON_STRING);
    return thing->string.utf8;
}

size_t json_string_length(json_thing_t *thing)
{
    assert(thing->type == JSON_STRING);
    return thing->string.len;
}

const char *json_raw_encoding(json_thing_t *thing)
{
    assert(thing->type == JSON_RAW);
    return thing->raw.repr;
}

json_element_t *json_array_first(json_thing_t *array)
{
    assert(array->type == JSON_ARRAY);
    return (json_element_t *) list_get_first(array->array.elements);
}

json_element_t *json_element_next(json_element_t *element)
{
    return (json_element_t *) list_next((list_elem_t *) element);
}

json_thing_t *json_element_value(json_element_t *element)
{
    return (json_thing_t *) list_elem_get_value((list_elem_t *) element);
}

static void optimize_array(json_thing_t *array)
{
    array->array.lookup_table = fsalloc(list_size(array->array.elements) *
                                        sizeof array->array.lookup_table[0]);
    json_element_t *e;
    size_t i = 0;
    for (e = json_array_first(array); e; e = json_element_next(e))
        array->array.lookup_table[i++] = json_element_value(e);
}

json_thing_t *json_array_get(json_thing_t *array, unsigned n)
{
    assert(array->type == JSON_ARRAY);
    if (n >= list_size(array->array.elements))
        return NULL;
    if (array->array.lookup_table)
        return array->array.lookup_table[n];
    json_element_t *e;
    unsigned i;
    if (list_size(array->array.elements) >= JIT_SIZE_LIMIT) {
        array->array.random_access_counter += n;
        if (array->array.random_access_counter >= JIT_ACCESS_LIMIT) {
            optimize_array(array);
            return json_array_get(array, n);
        }
    }
    for (e = json_array_first(array), i = 0; i < n;
         e = json_element_next(e), i++)
        ;
    return json_element_value(e);
}

bool json_array_get_array(json_thing_t *thing, unsigned n, json_thing_t **value)
{
    json_thing_t *field = json_array_get(thing, n);
    if (field && json_thing_type(field) == JSON_ARRAY) {
        *value = field;
        return true;
    }
    return false;
}

bool json_array_get_object(json_thing_t *thing, unsigned n,
                           json_thing_t **value)
{
    json_thing_t *field = json_array_get(thing, n);
    if (field && json_thing_type(field) == JSON_OBJECT) {
        *value = field;
        return true;
    }
    return false;
}

bool json_array_get_string(json_thing_t *thing, unsigned n, const char **value)
{
    json_thing_t *field = json_array_get(thing, n);
    if (field && json_thing_type(field) == JSON_STRING) {
        *value = field->string.utf8;
        return true;
    }
    return false;
}

bool json_array_get_integer(json_thing_t *thing, unsigned n, long long *value)
{
    json_thing_t *field = json_array_get(thing, n);
    if (field && json_cast_to_integer(field, value))
        return true;
    return false;
}

bool json_array_get_unsigned(json_thing_t *thing, unsigned n,
                             unsigned long long *value)
{
    json_thing_t *field = json_array_get(thing, n);
    if (field && json_cast_to_unsigned(field, value))
        return true;
    return false;
}

bool json_array_get_double(json_thing_t *thing, unsigned n, double *value)
{
    json_thing_t *field = json_array_get(thing, n);
    if (field && json_cast_to_double(field, value))
        return true;
    return false;
}

bool json_array_get_boolean(json_thing_t *thing, unsigned n, bool *value)
{
    json_thing_t *field = json_array_get(thing, n);
    if (field && json_thing_type(field) == JSON_BOOLEAN) {
        *value = field->boolean.value;
        return true;
    }
    return false;
}

size_t json_array_size(json_thing_t *array)
{
    assert(array->type == JSON_ARRAY);
    return list_size(array->array.elements);
}

json_field_t *json_object_first(json_thing_t *object)
{
    assert(object->type == JSON_OBJECT);
    return (json_field_t *) list_get_first(object->object.fields);
}

json_field_t *json_field_next(json_field_t *field)
{
    return (json_field_t *) list_next((list_elem_t *) field);
}

const char *json_field_name(json_field_t *field)
{
    return ((pair_t *) list_elem_get_value((list_elem_t *) field))->name;
}

json_thing_t *json_field_value(json_field_t *field)
{
    return ((pair_t *) list_elem_get_value((list_elem_t *) field))->value;
}

static void optimize_object(json_thing_t *object)
{
    object->object.lookup_table =
        make_hash_table(list_size(object->object.fields),
                        (uint64_t(*)(const void *)) hash_string,
                        (int (*)(const void *, const void *)) strcmp);
    json_field_t *f;
    for (f = json_object_first(object); f; f = json_field_next(f)) {
        hash_elem_t *he =
            hash_table_put(object->object.lookup_table, json_field_name(f),
                           json_field_value(f));
        if (he) /* forget conflicting entries */
            destroy_hash_element(he);
    }
}

json_thing_t *json_object_get(json_thing_t *object, const char *key)
{
    assert(object->type == JSON_OBJECT);
    if (object->object.lookup_table) {
        hash_elem_t *he = hash_table_get(object->object.lookup_table, key);
        return he ? (json_thing_t *) hash_elem_get_value(he) : NULL;
    }
    json_field_t *f;
    if (list_size(object->object.fields) >= JIT_SIZE_LIMIT)
        for (f = json_object_first(object); f; f = json_field_next(f)) {
            if (++object->object.random_access_counter >= JIT_ACCESS_LIMIT) {
                optimize_object(object);
                return json_object_get(object, key);
            }
            if (!strcmp(key, json_field_name(f)))
                return json_field_value(f);
        }
    else
        for (f = json_object_first(object); f; f = json_field_next(f))
            if (!strcmp(key, json_field_name(f)))
                return json_field_value(f);
    return NULL;
}

json_thing_t *json_object_dig(json_thing_t *thing, const char *const *keys,
                              size_t num_keys)
{
    int i;
    for (i = 0; i < num_keys; i++) {
        if (!thing || json_thing_type(thing) != JSON_OBJECT)
            return NULL;
        thing = json_object_get(thing, keys[i]);
    }
    return thing;
}

json_thing_t *json_object_fetch_func(json_thing_t *thing, const char *key, ...)
{
    va_list ap;
    va_start(ap, key);
    while (key) {
        if (!thing || json_thing_type(thing) != JSON_OBJECT) {
            va_end(ap);
            return NULL;
        }
        thing = json_object_get(thing, key);
        key = va_arg(ap, const char *);
    }
    va_end(ap);
    return thing;
}

bool json_object_get_array(json_thing_t *thing, const char *key,
                           json_thing_t **value)
{
    json_thing_t *field = json_object_get(thing, key);
    if (field && json_thing_type(field) == JSON_ARRAY) {
        *value = field;
        return true;
    }
    return false;
}

bool json_object_get_object(json_thing_t *thing, const char *key,
                            json_thing_t **value)
{
    json_thing_t *field = json_object_get(thing, key);
    if (field && json_thing_type(field) == JSON_OBJECT) {
        *value = field;
        return true;
    }
    return false;
}

bool json_object_get_string(json_thing_t *thing, const char *key,
                            const char **value)
{
    json_thing_t *field = json_object_get(thing, key);
    if (field && json_thing_type(field) == JSON_STRING) {
        *value = field->string.utf8;
        return true;
    }
    return false;
}

bool json_object_get_integer(json_thing_t *thing, const char *key,
                             long long *value)
{
    json_thing_t *field = json_object_get(thing, key);
    if (field && json_cast_to_integer(field, value))
        return true;
    return false;
}

bool json_object_get_unsigned(json_thing_t *thing, const char *key,
                              unsigned long long *value)
{
    json_thing_t *field = json_object_get(thing, key);
    if (field && json_cast_to_unsigned(field, value))
        return true;
    return false;
}

bool json_object_get_double(json_thing_t *thing, const char *key, double *value)
{
    json_thing_t *field = json_object_get(thing, key);
    if (field && json_cast_to_double(field, value))
        return true;
    return false;
}

bool json_object_get_boolean(json_thing_t *thing, const char *key, bool *value)
{
    json_thing_t *field = json_object_get(thing, key);
    if (field && json_thing_type(field) == JSON_BOOLEAN) {
        *value = field->boolean.value;
        return true;
    }
    return false;
}

json_thing_t *json_object_pop(json_thing_t *object, const char *key)
{
    assert(object->type == JSON_OBJECT);
    clobber_object(object);
    list_elem_t *e;
    for (e = list_get_first(object->object.fields); e; e = list_next(e)) {
        pair_t *f = (pair_t *) list_elem_get_value(e);
        if (!strcmp(key, f->name)) {
            fsfree(f->name);
            json_thing_t *value = f->value;
            fsfree(f);
            list_remove(object->object.fields, e);
            return value;
        }
    }
    return NULL;
}

static void encode_char(char c, char **q, char *end)
{
    if (*q < end)
        *(*q)++ = c;
}

static size_t encode_repr(const char *repr, char **q, char *end)
{
    size_t count = 0;
    while (*repr) {
        encode_char(*repr++, q, end);
        count++;
    }
    return count;
}

static size_t encode_raw(json_thing_t *thing, char **q, char *end);
static size_t prettyprint_raw(json_thing_t *thing, char **q, char *end,
                              unsigned left_margin, unsigned indentation);

static size_t encode_array(json_thing_t *thing, char **q, char *end)
{
    encode_char('[', q, end);
    size_t count = 1;
    json_element_t *e = json_array_first(thing);
    if (e)
        for (;;) {
            count += encode_raw(json_element_value(e), q, end);
            e = json_element_next(e);
            if (!e)
                break;
            encode_char(',', q, end);
            count++;
        }
    encode_char(']', q, end);
    count++;
    return count;
}

static size_t encode_string_value(const char *value, char **q, char *end)
{
    encode_char('"', q, end);
    size_t count = 1;
    const char *p;
    for (p = value; *p; p++)
        if (is_ascii_control_character(*p))
            switch (*p) {
                case '\b':
                    count += encode_repr("\\b", q, end);
                    break;
                case '\f':
                    count += encode_repr("\\f", q, end);
                    break;
                case '\n':
                    count += encode_repr("\\n", q, end);
                    break;
                case '\r':
                    count += encode_repr("\\r", q, end);
                    break;
                case '\t':
                    count += encode_repr("\\t", q, end);
                    break;
                default: {
                    char buf[10];
                    sprintf(buf, "\\u%04x", *p & 0xff);
                    count += encode_repr(buf, q, end);
                }
            }
        else if (is_at_latin_control_character(p)) {
            char buf[10];
            sprintf(buf, "\\u%04x", (p[0] & 0x1f) << 6 | (p[1] & 0x3f));
            p++;
            count += encode_repr(buf, q, end);
        } else if (*p == '\\' || *p == '"') {
            encode_char('\\', q, end);
            encode_char(*p, q, end);
            count += 2;
        } else {
            encode_char(*p, q, end);
            count++;
        }
    encode_char('"', q, end);
    return count + 1;
}

static size_t encode_object(json_thing_t *thing, char **q, char *end)
{
    encode_char('{', q, end);
    size_t count = 1;
    json_field_t *f = json_object_first(thing);
    if (f)
        for (;;) {
            count += encode_string_value(json_field_name(f), q, end);
            encode_char(':', q, end);
            count++;
            count += encode_raw(json_field_value(f), q, end);
            f = json_field_next(f);
            if (!f)
                break;
            encode_char(',', q, end);
            count++;
        }
    encode_char('}', q, end);
    count++;
    return count;
}

static size_t encode_string(json_thing_t *thing, char **q, char *end)
{
    return encode_string_value(thing->string.utf8, q, end);
}

static size_t encode_integer(json_thing_t *thing, char **q, char *end)
{
    char buf[4 * sizeof thing->integer.value];
    sprintf(buf, "%lld", thing->integer.value);
    return encode_repr(buf, q, end);
}

static size_t encode_unsigned(json_thing_t *thing, char **q, char *end)
{
    char buf[4 * sizeof thing->u_integer.value];
    sprintf(buf, "%llu", thing->u_integer.value);
    return encode_repr(buf, q, end);
}

static size_t encode_float(json_thing_t *thing, char **q, char *end)
{
    char buf[100];
    encjson_internal_encode_float(thing->real.value, buf);
    return encode_repr(buf, q, end);
}

static size_t encode_raw(json_thing_t *thing, char **q, char *end)
{
    switch (thing->type) {
        case JSON_ARRAY:
            return encode_array(thing, q, end);
        case JSON_OBJECT:
            return encode_object(thing, q, end);
        case JSON_STRING:
            return encode_string(thing, q, end);
        case JSON_INTEGER:
            return encode_integer(thing, q, end);
        case JSON_UNSIGNED:
            return encode_unsigned(thing, q, end);
        case JSON_FLOAT:
            return encode_float(thing, q, end);
        case JSON_BOOLEAN:
            return encode_repr(thing->boolean.value ? "true" : "false", q, end);
        case JSON_NULL:
            return encode_repr("null", q, end);
        case JSON_RAW:
            return encode_repr(thing->raw.repr, q, end);
        default:
            abort();
    }
}

size_t json_utf8_encode(json_thing_t *thing, void *buffer, size_t size)
{
    char *q = buffer;
    if (!size)
        return encode_raw(thing, &q, q);
    size_t count = encode_raw(thing, &q, q + size - 1);
    *q = '\0';
    return count;
}

static void indent(char **q, char *end, unsigned left_margin)
{
    while (left_margin--)
        encode_char(' ', q, end);
}

static size_t prettyprint_array(json_thing_t *thing, char **q, char *end,
                                unsigned left_margin, unsigned indentation)
{
    encode_char('[', q, end);
    size_t count = 1;
    json_element_t *e = json_array_first(thing);
    if (e) {
        unsigned deeper = left_margin + indentation;
        for (;;) {
            encode_char('\n', q, end);
            indent(q, end, deeper);
            count += deeper + 1;
            count += prettyprint_raw(json_element_value(e), q, end, deeper,
                                     indentation);
            e = json_element_next(e);
            if (!e)
                break;
            encode_char(',', q, end);
            count++;
        }
        encode_char('\n', q, end);
        indent(q, end, left_margin);
        count += left_margin + 1;
    }
    encode_char(']', q, end);
    count++;
    return count;
}

static size_t prettyprint_object(json_thing_t *thing, char **q, char *end,
                                 unsigned left_margin, unsigned indentation)
{
    encode_char('{', q, end);
    size_t count = 1;
    json_field_t *f = json_object_first(thing);
    if (f) {
        unsigned deeper = left_margin + indentation;
        for (;;) {
            encode_char('\n', q, end);
            indent(q, end, deeper);
            count += deeper + 1;
            count += encode_string_value(json_field_name(f), q, end);
            encode_char(':', q, end);
            encode_char(' ', q, end);
            count += 2;
            count += prettyprint_raw(json_field_value(f), q, end, deeper,
                                     indentation);
            f = json_field_next(f);
            if (!f)
                break;
            encode_char(',', q, end);
            count++;
        }
        encode_char('\n', q, end);
        indent(q, end, left_margin);
        count += left_margin + 1;
    }
    encode_char('}', q, end);
    count++;
    return count;
}

static size_t prettyprint_raw(json_thing_t *thing, char **q, char *end,
                              unsigned left_margin, unsigned indentation)
{
    switch (thing->type) {
        case JSON_ARRAY:
            return prettyprint_array(thing, q, end, left_margin, indentation);
        case JSON_OBJECT:
            return prettyprint_object(thing, q, end, left_margin, indentation);
        case JSON_STRING:
            return encode_string(thing, q, end);
        case JSON_INTEGER:
            return encode_integer(thing, q, end);
        case JSON_UNSIGNED:
            return encode_unsigned(thing, q, end);
        case JSON_FLOAT:
            return encode_float(thing, q, end);
        case JSON_BOOLEAN:
            return encode_repr(thing->boolean.value ? "true" : "false", q, end);
        case JSON_NULL:
            return encode_repr("null", q, end);
        case JSON_RAW:
            return encode_repr(thing->raw.repr, q, end);
        default:
            abort();
    }
}

size_t json_utf8_prettyprint(json_thing_t *thing, void *buffer, size_t size,
                             unsigned left_margin, unsigned indentation)
{
    char *q = buffer;
    if (!size)
        return prettyprint_raw(thing, &q, q, left_margin, indentation);
    size_t count =
        prettyprint_raw(thing, &q, q + size - 1, left_margin, indentation);
    *q = '\0';
    return count;
}

static const char *skip_ws(const char *p, const char *end)
{
    if (!p)
        return p;
    while (p < end)
        switch (*p) {
            case ' ':
            case '\t':
            case '\r':
            case '\n':
                p++;
                break;
            default:
                return p;
        }
    return p;
}

static const char *skip(const char *p, const char *end, char c)
{
    if (!p || exhausted(p, end))
        return NULL;
    if (*p != c) {
        json_error();
        return NULL;
    }
    return p + 1;
}

static const char *decode_array(const char *p, const char *end,
                                json_thing_t **thing, unsigned levels)
{
    json_thing_t *array;
    *thing = array = json_make_array();
    p = skip_ws(skip(p, end, '['), end);
    if (!p || exhausted(p, end)) {
        json_destroy_thing(array);
        return NULL;
    }
    if (*p == ']') {
        p = skip(p, end, ']');
        if (!p) {
            json_destroy_thing(array);
            return NULL;
        }
        return p;
    }
    for (;;) {
        json_thing_t *element;
        p = decode(p, end, &element, levels - 1);
        if (!p) {
            json_destroy_thing(array);
            return NULL;
        }
        if (!json_add_to_array(array, element)) {
            json_destroy_thing(array);
            json_destroy_thing(element);
            return NULL;
        }
        p = skip_ws(p, end);
        if (!p || exhausted(p, end)) {
            json_destroy_thing(array);
            return NULL;
        }
        if (*p == ']') {
            p = skip(p, end, ']');
            if (!p) {
                json_destroy_thing(array);
                return NULL;
            }
            return p;
        }
        p = skip_ws(skip(p, end, ','), end);
        if (!p) {
            json_destroy_thing(array);
            return NULL;
        }
    }
}

static const char *decode_object(const char *p, const char *end,
                                 json_thing_t **thing, unsigned levels)
{
    json_thing_t *object;
    *thing = object = json_make_object();
    p = skip_ws(skip(p, end, '{'), end);
    if (!p || exhausted(p, end)) {
        json_destroy_thing(object);
        return NULL;
    }
    if (*p == '}') {
        p = skip(p, end, '}');
        if (!p) {
            json_destroy_thing(object);
            return NULL;
        }
        return p;
    }
    for (;;) {
        char *key;
        size_t len;
        p = decode_string_value(p, end, &key, &len);
        if (!p) {
            json_destroy_thing(object);
            return NULL;
        }
        p = skip_ws(skip(skip_ws(p, end), end, ':'), end);
        json_thing_t *value;
        p = decode(p, end, &value, levels - 1);
        if (!p) {
            json_destroy_thing(object);
            fsfree(key);
            return NULL;
        }
        add_to_object(object, key, value);
        p = skip_ws(p, end);
        if (!p || exhausted(p, end)) {
            json_destroy_thing(object);
            return NULL;
        }
        if (*p == '}') {
            p = skip(p, end, '}');
            if (!p) {
                json_destroy_thing(object);
                return NULL;
            }
            return p;
        }
        p = skip_ws(skip(p, end, ','), end);
        if (!p) {
            json_destroy_thing(object);
            return NULL;
        }
    }
}

static const char *scan_hex_digit(const char *p, const char *end, int *digit)
{
    if (!p || exhausted(p, end)) {
        *digit = -1; /* don't-care to avoid compiler warnings */
        return NULL;
    }
    *digit = charstr_digit_value(*p);
    if (*digit == -1) {
        json_error();
        return NULL;
    }
    return skip(p, end, *p);
}

static const char *scan_ucs2(const char *p, const char *end, int *value)
{
    int d0, d1, d2, d3;
    p = scan_hex_digit(p, end, &d0);
    p = scan_hex_digit(p, end, &d1);
    p = scan_hex_digit(p, end, &d2);
    p = scan_hex_digit(p, end, &d3);
    *value = d0 << 12 | d1 << 8 | d2 << 4 | d3;
    return p;
}

static bool high_surrogate(int ucs2)
{
    return ucs2 >= 0xd800 && ucs2 <= 0xdbff;
}

static bool low_surrogate(int ucs2)
{
    return ucs2 >= 0xdc00 && ucs2 <= 0xdfff;
}

static bool valid_unicode(int unic)
{
    return (unic >= 0 && unic <= 0xd7ff) ||
        (unic >= 0xe000 && unic <= 0x10ffff);
}

static size_t utf8_length(int unic)
{
    if (unic < 0x80)
        return 1;
    if (unic < 0x800)
        return 2;
    if (unic < 0x10000)
        return 3;
    return 4;
}

static char *utf8_encode(char *q, int unic)
{
    if (unic < 0x80)
        *q++ = unic;
    else if (unic < 0x800) {
        *q++ = 0xc0 | unic >> 6;
        *q++ = 0x80 | (unic & 0x3f);
    } else if (unic < 0x10000) {
        *q++ = 0xe0 | unic >> 12;
        *q++ = 0x80 | (unic >> 6 & 0x3f);
        *q++ = 0x80 | (unic & 0x3f);
    } else {
        *q++ = 0xf0 | unic >> 18;
        *q++ = 0x80 | (unic >> 12 & 0x3f);
        *q++ = 0x80 | (unic >> 6 & 0x3f);
        *q++ = 0x80 | (unic & 0x3f);
    }
    return q;
}

static const char *scan_utf16(const char *p, const char *end, int *value)
{
    /* Scanning begins after the initial \u */
    int ucs2;
    p = scan_ucs2(p, end, &ucs2);
    if (!p)
        return NULL;
    if (low_surrogate(ucs2)) {
        json_error();
        return NULL;
    }
    if (!high_surrogate(ucs2)) {
        *value = ucs2;
        return p;
    }
    p = skip(p, end, '\\');
    p = skip(p, end, 'u');
    int low;
    p = scan_ucs2(p, end, &low);
    if (!p)
        return NULL;
    if (!low_surrogate(low)) {
        json_error();
        return NULL;
    }
    *value = 0x10000 + ((ucs2 - 0xd800) << 10 | (low - 0xdc00));
    if (!valid_unicode(*value)) {
        json_error();
        return NULL;
    }
    return p;
}

static const char *skip_utf8(const char *p, const char *end)
{
    if (!p || exhausted(p, end))
        return NULL;
    char c0 = *p;
    p = skip(p, end, *p);
    if (!(c0 & 0x80))
        return p;
    if (!(c0 & 0x40)) {
        json_error();
        return NULL;
    }
    if (!p || exhausted(p, end))
        return NULL;
    if ((*p & 0xc0) != 0x80) {
        json_error();
        return NULL;
    }
    p = skip(p, end, *p);
    if (!(c0 & 0x20))
        return p;
    if (!p || exhausted(p, end))
        return NULL;
    if ((*p & 0xc0) != 0x80) {
        json_error();
        return NULL;
    }
    p = skip(p, end, *p);
    if (!(c0 & 0x10))
        return p;
    if (c0 & 0x08) {
        json_error();
        return NULL;
    }
    if (!p || exhausted(p, end))
        return NULL;
    if ((*p & 0xc0) != 0x80) {
        json_error();
        return NULL;
    }
    return skip(p, end, *p);
}

static ssize_t scan_string_repr(const char *p, const char *end)
{
    size_t count = 0;
    p = skip(p, end, '"');
    while (p && !exhausted(p, end))
        switch (*p) {
            case '\\':
                p = skip(p, end, '\\');
                if (!p || exhausted(p, end))
                    return -1;
                if (*p & 0x80) {
                    json_error();
                    return -1;
                }
                if (*p != 'u') {
                    count++;
                    p = skip(p, end, *p);
                } else {
                    p = skip(p, end, 'u');
                    int unic;
                    p = scan_utf16(p, end, &unic);
                    if (!p)
                        return -1;
                    count += utf8_length(unic);
                }
                break;
            case '"':
                /* Theoretically, count might have overflowed the signed
                 * range. That works perfectly. */
                return count;
            default: {
                const char *p0 = p;
                p = skip_utf8(p, end);
                count += p - p0;
            }
        }
    return -1;
}

static const char *decode_string_value(const char *p, const char *end,
                                       char **value, size_t *len)
{
    ssize_t size = scan_string_repr(p, end);
    if (size < 0)
        return NULL;
    *len = size;
    char *buffer = *value = fsalloc(size + 1);
    p = skip(p, end, '"');
    char *q = buffer;
    /* scan_string_repr() above has already validated the string; no
     * need to do bounds or error checking */
    while (*p != '"')
        if (*p != '\\')
            *q++ = *p++;
        else {
            p++;
            switch (*p) {
                case 'b':
                    *q++ = '\b', p++;
                    break;
                case 'f':
                    *q++ = '\f', p++;
                    break;
                case 'n':
                    *q++ = '\n', p++;
                    break;
                case 'r':
                    *q++ = '\r', p++;
                    break;
                case 't':
                    *q++ = '\t', p++;
                    break;
                case 'u': {
                    int unic;
                    p = scan_utf16(p + 1, end, &unic);
                    q = utf8_encode(q, unic);
                } break;
                default:
                    *q++ = *p++;
            }
        }
    *q = '\0';
    return p + 1;
}

static const char *decode_string(const char *p, const char *end,
                                 json_thing_t **thing)
{
    char *value;
    size_t len;
    p = decode_string_value(p, end, &value, &len);
    if (p)
        *thing = json_adopt_bounded_string(value, len);
    return p;
}

static const char *scan_integral(const char *p, const char *end)
{
    if (!p || exhausted(p, end))
        return NULL;
    if (*p < '0' || *p > '9') {
        json_error();
        return NULL;
    }
    do {
        p = skip(p, end, *p);
    } while (p && p < end && *p >= '0' && *p <= '9');
    return p;
}

static const char *decode_float(const char *p, const char *end,
                                json_thing_t **thing)
{
    /* assumption: p thru end is a formally valid floating-point number */
    size_t size = end - p;
    char *copy = fsalloc(size + 1);
    memcpy(copy, p, size);
    copy[size] = '\0';
    errno = 0;
    double value;
    if (!encjson_internal_decode_float(copy, &value)) {
        fsfree(copy);
        return NULL;
    }
    fsfree(copy);
    switch (fpclassify(value)) {
        case FP_NAN:
        case FP_INFINITE:
            return NULL;
        case FP_ZERO:
        case FP_SUBNORMAL:
            *thing = json_make_float(0);
            return end;
        default:
            *thing = json_make_float(value);
            return end;
    }
}

static const char *decode_unsigned(const char *p, const char *end,
                                   json_thing_t **thing)
{
    /* assumption: p thru end is a formally valid unsigned integer */
    unsigned long long value = 0;
    const char *start = p;
    while (p < end) {
        unsigned long long new_value = value * 10 + *p++ - '0';
        if (new_value < value) {
            /* unsigned integer overflow */
            return decode_float(start, end, thing);
        }
        value = new_value;
    }
    if ((long long) value >= 0)
        *thing = json_make_integer((long long) value);
    else
        *thing = json_make_unsigned(value);
    return end;
}

static const char *scan_exponent(const char *p, const char *end)
{
    if (!p || exhausted(p, end))
        return p;
    switch (*p) {
        default:
            return p;
        case 'E':
        case 'e':;
    }
    p = skip(p, end, *p);
    if (!p || exhausted(p, end))
        return NULL;
    switch (*p) {
        case '+':
        case '-':
            p = skip(p, end, *p);
            break;
        default:;
    }
    return scan_integral(p, end);
}

static const char *decode_number(const char *p, const char *end,
                                 json_thing_t **thing)
{
    const char *start = p;
    p = scan_integral(p, end);
    if (!p)
        return NULL;
    if (!exhausted(p, end))
        switch (*p) {
            case '.':
                p = skip(p, end, '.');
                p = scan_integral(p, end);
                /* flow through */
            case 'E':
            case 'e':
                p = scan_exponent(p, end);
                if (!p)
                    return NULL;
                return decode_float(start, p, thing);
            default:;
        }
    return decode_unsigned(start, p, thing);
}

static const char *decode_negative_number(const char *p, const char *end,
                                          json_thing_t **thing)
{
    p = skip(p, end, '-');
    p = decode_number(p, end, thing);
    if (!p)
        return NULL;
    json_thing_t *number = *thing;
    switch (number->type) {
        case JSON_UNSIGNED:
            if (number->u_integer.value < (unsigned long long) LLONG_MIN) {
                number->type = JSON_INTEGER;
                number->integer.value = -number->u_integer.value;
            } else {
                number->type = JSON_FLOAT;
                number->real.value = -(double) number->u_integer.value;
            }
            break;
        case JSON_INTEGER:
            if (number->integer.value == LLONG_MIN) {
                number->type = JSON_UNSIGNED;
                number->u_integer.value = LLONG_MIN;
            } else
                number->integer.value = -number->integer.value;
            break;
        case JSON_FLOAT:
            number->real.value = -number->real.value;
            break;
        default:
            abort();
    }
    return p;
}

static const char *decode_true(const char *p, const char *end,
                               json_thing_t **thing)
{
    p = skip(p, end, 't');
    p = skip(p, end, 'r');
    p = skip(p, end, 'u');
    p = skip(p, end, 'e');
    if (!p)
        return NULL;
    *thing = json_make_boolean(true);
    return p;
}

static const char *decode_false(const char *p, const char *end,
                                json_thing_t **thing)
{
    p = skip(p, end, 'f');
    p = skip(p, end, 'a');
    p = skip(p, end, 'l');
    p = skip(p, end, 's');
    p = skip(p, end, 'e');
    if (!p)
        return NULL;
    *thing = json_make_boolean(false);
    return p;
}

static const char *decode_null(const char *p, const char *end,
                               json_thing_t **thing)
{
    p = skip(p, end, 'n');
    p = skip(p, end, 'u');
    p = skip(p, end, 'l');
    p = skip(p, end, 'l');
    if (!p)
        return NULL;
    *thing = json_make_null();
    return p;
}

static const char *decode(const char *p, const char *end, json_thing_t **thing,
                          unsigned levels)
{
    if (!levels) {
        json_error();
        return NULL;
    }
    p = skip_ws(p, end);
    if (!p || exhausted(p, end))
        return NULL;
    switch (*p) {
        case '[':
            return decode_array(p, end, thing, levels);
        case '{':
            return decode_object(p, end, thing, levels);
        case '"':
            return decode_string(p, end, thing);
        case '-':
            return decode_negative_number(p, end, thing);
        case 't':
            return decode_true(p, end, thing);
        case 'f':
            return decode_false(p, end, thing);
        case 'n':
            return decode_null(p, end, thing);
        default:
            if (*p >= '0' && *p <= '9')
                return decode_number(p, end, thing);
            json_error();
            return NULL;
    }
}

json_thing_t *json_utf8_decode(const void *buffer, size_t size)
{
    const char *p = buffer;
    const char *end = p + size;
    json_thing_t *thing;
    p = decode(p, end, &thing, MAX_DECODE_NESTING_LEVELS);
    if (!p)
        return NULL;
    p = skip_ws(p, end);
    if (p != end) {
        json_destroy_thing(thing);
        return NULL;
    }
    return thing;
}

json_thing_t *json_utf8_decode_string(const char *encoding)
{
    return json_utf8_decode(encoding, strlen(encoding));
}

static char *read_file(FILE *f, size_t max_size, size_t *size)
{
    size_t nbytes = 512;
    char *buffer = fsalloc(nbytes);
    *size = 0;
    for (;;) {
        char buf[4096];
        size_t count = fread(buf, 1, sizeof buf, f);
        if (ferror(f)) {
            fsfree(buffer);
            return NULL;
        }
        if (count == 0)
            break;
        if (count > max_size - *size) {
            errno = ENOMEM;
            fsfree(buffer);
            return NULL;
        }
        size_t n = *size + count;
        if (nbytes < n) {
            if (n > max_size / 2)
                nbytes = max_size;
            else {
                nbytes = 1;
                while (nbytes < n)
                    nbytes <<= 1;
            }
            char *ptr = fsrealloc(buffer, nbytes);
            buffer = ptr;
        }
        memcpy(buffer + *size, buf, count);
        *size += count;
    }
    return buffer;
}

json_thing_t *json_utf8_decode_file(FILE *f, size_t max_size)
{
    size_t size;
    char *buffer = read_file(f, max_size, &size);
    if (!buffer)
        return NULL;
    json_thing_t *thing = json_utf8_decode(buffer, size);
    fsfree(buffer);
    if (!thing) {
        errno = EINVAL;
        return NULL;
    }
    return thing;
}

bool json_cast_to_integer(json_thing_t *thing, long long *n)
{
    switch (thing->type) {
        case JSON_INTEGER:
            *n = json_integer_value(thing);
            return true;
        case JSON_UNSIGNED: {
            long long value = json_unsigned_value(thing);
            if (value < 0)
                return false;
            *n = value;
            return true;
        }
        case JSON_FLOAT: {
            _Static_assert(sizeof(double) == sizeof(uint64_t),
                           "encjson requires a 64-bit double type.");
            union {
                double f;
                uint64_t i;
            } value;
            value.f = json_double_value(thing);
            return binary64_to_integer(value.i, n);
        }
        default:
            return false;
    }
}

bool json_cast_to_unsigned(json_thing_t *thing, unsigned long long *n)
{
    switch (thing->type) {
        case JSON_INTEGER: {
            long long value = json_integer_value(thing);
            if (value < 0)
                return false;
            *n = value;
            return true;
        }
        case JSON_UNSIGNED:
            *n = json_unsigned_value(thing);
            return true;
        case JSON_FLOAT: {
            _Static_assert(sizeof(double) == sizeof(uint64_t),
                           "encjson requires a 64-bit double type.");
            union {
                double f;
                uint64_t i;
            } value;
            value.f = json_double_value(thing);
            return binary64_to_unsigned(value.i, n);
        }
        default:
            return false;
    }
}

bool json_cast_to_double(json_thing_t *thing, double *n)
{
    switch (thing->type) {
        case JSON_INTEGER:
            *n = json_integer_value(thing);
            return true;
        case JSON_UNSIGNED:
            *n = json_unsigned_value(thing);
            return true;
        case JSON_FLOAT:
            *n = json_double_value(thing);
            return true;
        default:
            return false;
    }
}

int json_utf8_dump(json_thing_t *thing, FILE *f)
{
    size_t size = json_utf8_prettyprint(thing, NULL, 0, 0, 2) + 1;
    char *buffer = fsalloc(size);
    json_utf8_prettyprint(thing, buffer, size, 0, 2);
    int n = fprintf(f, "%s\n", buffer);
    fsfree(buffer);
    return n;
}

/* This data structure is used by fstrace inside a safe critical
 * section. */
enum {
    TRACE_SLOTS = 4, /* power of two, please */
    TRACE_DEFAULT_SIZE = 2048
};

static struct {
    unsigned next_slot;
    char *slots[TRACE_SLOTS];
    size_t max_size;
} trace_data = {
    .max_size = TRACE_DEFAULT_SIZE,
};

const char *json_trace(void *p)
{
    json_thing_t *thing = p;
    size_t size = json_utf8_encode(thing, NULL, 0);
    if (size > trace_data.max_size)
        size = trace_data.max_size;
    trace_data.max_size = TRACE_DEFAULT_SIZE;
    char *slot = trace_data.slots[trace_data.next_slot % TRACE_SLOTS];
    char *buf = fsrealloc(slot, size + 1);
    if (!buf)
        return "";
    if (!slot)
        fs_reallocator_skew(-1);
    trace_data.slots[trace_data.next_slot++ % TRACE_SLOTS] = buf;
    json_utf8_encode(thing, buf, size + 1);
    return buf;
}

const char *json_trace_max_size(void *p)
{
    trace_data.max_size = *(size_t *) p;
    return "";
}

const char *json_trace_type(void /* json_thing_type_t */ *ptype)
{
    switch (*(json_thing_type_t *) ptype) {
        case JSON_ARRAY:
            return "JSON_ARRAY";
        case JSON_OBJECT:
            return "JSON_OBJECT";
        case JSON_STRING:
            return "JSON_STRING";
        case JSON_INTEGER:
            return "JSON_INTEGER";
        case JSON_UNSIGNED:
            return "JSON_UNSIGNED";
        case JSON_FLOAT:
            return "JSON_FLOAT";
        case JSON_BOOLEAN:
            return "JSON_BOOLEAN";
        case JSON_NULL:
            return "JSON_NULL";
        case JSON_RAW:
            return "JSON_RAW";
        default:
            return "?";
    }
}

const char *json_trace_thing_type(void /* json_thing_t */ *thing)
{
    return json_trace_type(&((json_thing_t *) thing)->type);
}

static bool equal_arrays(json_thing_t *a, json_thing_t *b, double tolerance)
{
    if (list_size(a->array.elements) != list_size(b->array.elements))
        return false;
    json_element_t *ea = json_array_first(a);
    json_element_t *eb = json_array_first(b);
    for (;;) {
        if (!ea)
            return !eb;
        if (!eb ||
            !json_thing_equal(json_element_value(ea), json_element_value(eb),
                              tolerance))
            return false;
        ea = json_element_next(ea);
        eb = json_element_next(eb);
    }
}

static bool equal_objects(json_thing_t *a, json_thing_t *b, double tolerance)
{
    if (list_size(a->object.fields) != list_size(b->object.fields))
        return false;
    if (!b->object.lookup_table)
        optimize_object(b);
    json_field_t *fa;
    for (fa = json_object_first(a); fa; fa = json_field_next(fa)) {
        json_thing_t *bval = json_object_get(b, json_field_name(fa));
        if (!bval || !json_thing_equal(json_field_value(fa), bval, tolerance))
            return false;
    }
    return true;
}

static bool equal_doubles(double a, double b, double tolerance)
{
    return a == b || fabs(b - a) / fmax(fabs(a), fabs(b)) < tolerance;
}

static bool equal_to_integer(long long n, json_thing_t *b, double tolerance)
{
    switch (json_thing_type(b)) {
        case JSON_INTEGER:
            return n == json_integer_value(b);
        case JSON_UNSIGNED:
            return n >= 0 && n == json_unsigned_value(b);
        case JSON_FLOAT:
            return equal_doubles(n, json_double_value(b), tolerance);
        default:
            return false;
    }
}

static bool equal_to_unsigned(unsigned long long n, json_thing_t *b,
                              double tolerance)
{
    switch (json_thing_type(b)) {
        case JSON_INTEGER: {
            long long q = json_integer_value(b);
            return q >= 0 && n == q;
        }
        case JSON_UNSIGNED:
            return n == json_unsigned_value(b);
        case JSON_FLOAT:
            return equal_doubles(n, json_double_value(b), tolerance);
        default:
            return false;
    }
}

static bool equal_to_float(double n, json_thing_t *b, double tolerance)
{
    switch (json_thing_type(b)) {
        case JSON_INTEGER:
            return equal_doubles(n, json_integer_value(b), tolerance);
        case JSON_UNSIGNED:
            return equal_doubles(n, json_unsigned_value(b), tolerance);
        case JSON_FLOAT:
            return equal_doubles(n, json_double_value(b), tolerance);
        default:
            return false;
    }
}

bool json_thing_equal(json_thing_t *a, json_thing_t *b, double tolerance)
{
    if (json_thing_type(b) == JSON_RAW) {
        json_thing_t *b_dec = json_utf8_decode_string(json_raw_encoding(b));
        bool result = json_thing_equal(a, b_dec, tolerance);
        json_destroy_thing(b_dec);
        return result;
    }
    switch (json_thing_type(a)) {
        case JSON_ARRAY:
            return json_thing_type(b) == JSON_ARRAY &&
                equal_arrays(a, b, tolerance);
        case JSON_OBJECT:
            return json_thing_type(b) == JSON_OBJECT &&
                equal_objects(a, b, tolerance);
        case JSON_STRING:
            return json_thing_type(b) == JSON_STRING &&
                !strcmp(json_string_value(a), json_string_value(b));
        case JSON_INTEGER:
            return equal_to_integer(json_integer_value(a), b, tolerance);
        case JSON_UNSIGNED:
            return equal_to_unsigned(json_unsigned_value(a), b, tolerance);
        case JSON_FLOAT:
            return equal_to_float(json_double_value(a), b, tolerance);
        case JSON_BOOLEAN:
            return json_thing_type(b) == JSON_BOOLEAN &&
                json_boolean_value(a) == json_boolean_value(b);
        case JSON_NULL:
            return json_thing_type(b) == JSON_NULL;
        case JSON_RAW:
            return json_thing_equal(b, a, tolerance);
        default:
            assert(false);
    }
}
