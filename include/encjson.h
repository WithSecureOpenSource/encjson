#ifndef __ENCJSON__
#define __ENCJSON__

#include <stdbool.h>
#include <stdio.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct json_thing json_thing_t;
typedef struct json_element json_element_t;
typedef struct json_field json_field_t;

json_thing_t *json_make_integer(long long n);
json_thing_t *json_make_unsigned(unsigned long long n);
json_thing_t *json_make_float(double n);
json_thing_t *json_make_boolean(bool truth_value);
json_thing_t *json_make_null(void);
json_thing_t *json_make_bounded_string(const char *s, size_t size);

/* While json_make_string() copies the argument string,
 * json_adopt_string() assumes the ownership of the argument string.
 * In the case of json_adopt_string(), the argument must have been
 * allocated using fsalloc(). */
json_thing_t *json_make_string(const char *s);
json_thing_t *json_adopt_string(char *s);

json_thing_t *json_make_array(void);

/* The ownership of the added element is transferred to array. The same
 * element must not be added to multiple arrays or objects. Returns
 * array if successful and NULL in case of an allocation failure. */
json_thing_t *json_add_to_array(json_thing_t *array, json_thing_t *element);

json_thing_t *json_make_object(void);

/* The ownership of the added value is transferred to object. The same
 * value must not be added to multiple arrays or objects.
 *
 * The caller is responsible for making sure each field is unique within
 * an object. Returns object if successful and NULL in case of an
 * allocation failure. */
json_thing_t *json_add_to_object(json_thing_t *object,
                                 const char *field, json_thing_t *value);

/* A "raw" thing allows you to embed a JSON-encoding inside a JSON
 * thing. The caller is responsible for the validity of the encoding. A
 * raw thing is never the outcome of decoding.
 */
json_thing_t *json_make_raw(const char *encoding);

void json_destroy_thing(json_thing_t *thing);

json_thing_t *json_clone(json_thing_t *thing);

typedef enum {
    JSON_ARRAY,
    JSON_OBJECT,
    JSON_STRING,
    JSON_INTEGER,
    JSON_UNSIGNED,
    JSON_FLOAT,
    JSON_BOOLEAN,
    JSON_NULL,
    JSON_RAW
} json_thing_type_t;

json_thing_type_t json_thing_type(json_thing_t *thing);
long long json_integer_value(json_thing_t *thing);
unsigned long long json_unsigned_value(json_thing_t *thing);
double json_double_value(json_thing_t *thing);
bool json_boolean_value(json_thing_t *thing);
const char *json_string_value(json_thing_t *thing);
const char *json_raw_encoding(json_thing_t *thing);

/* This library spits JSON's generic number type into three useful C
 * data types: JSON_INTEGER (long long), JSON_UNSIGNED (unsigned long
 * long) and JSON_FLOAT (double). That split is practical at times but
 * annoying at others. The following cast functions seek to remove some
 * of the annoyance.
 *
 * Each of the cast functions returns false if the number (or other JSON
 * thing) cannot be cast to the desired type. Any number can be cast to
 * a double, but a cast to (unsigned) long long succeeds only if no
 * information is lost. */
bool json_cast_to_integer(json_thing_t *thing, long long *n);
bool json_cast_to_unsigned(json_thing_t *thing, unsigned long long *n);
bool json_cast_to_double(json_thing_t *thing, double *n);

/* Numbering starts from 0. If the array does not have the specified
 * element, NULL is returned. */
json_thing_t *json_array_get(json_thing_t *array, unsigned n);

/* The return value is false if the requested field is either missing
 * or is not of the expected type. The numeric accessors use the
 * corresponding cast functions to get the field value. */
bool json_array_get_array(json_thing_t *thing, unsigned n,
                          json_thing_t **value);
bool json_array_get_object(json_thing_t *thing, unsigned n,
                           json_thing_t **value);
bool json_array_get_string(json_thing_t *thing, unsigned n,
                           const char **value);
bool json_array_get_integer(json_thing_t *thing, unsigned n,
                            long long *value);
bool json_array_get_unsigned(json_thing_t *thing, unsigned n,
                             unsigned long long *value);
bool json_array_get_double(json_thing_t *thing, unsigned n,
                           double *value);
bool json_array_get_boolean(json_thing_t *thing, unsigned n,
                            bool *value);

/* May result in an O(n) traversal. */
size_t json_array_size(json_thing_t *array);

/* NULL is returned for an empty array. */
json_element_t *json_array_first(json_thing_t *array);

/* NULL is returned when the array is exhausted. */
json_element_t *json_element_next(json_element_t *element);

json_thing_t *json_element_value(json_element_t *element);

/* If the object does not have the specified field, NULL is returned. */
json_thing_t *json_object_get(json_thing_t *object, const char *key);

/* The return value is false if the requested field is either missing
 * or is not of the expected type. The numeric accessors use the
 * corresponding cast functions to get the field value. */
bool json_object_get_array(json_thing_t *thing, const char *key,
                           json_thing_t **value);
bool json_object_get_object(json_thing_t *thing, const char *key,
                            json_thing_t **value);
bool json_object_get_string(json_thing_t *thing, const char *key,
                            const char **value);
bool json_object_get_integer(json_thing_t *thing, const char *key,
                             long long *value);
bool json_object_get_unsigned(json_thing_t *thing, const char *key,
                              unsigned long long *value);
bool json_object_get_double(json_thing_t *thing, const char *key,
                            double *value);
bool json_object_get_boolean(json_thing_t *thing, const char *key,
                             bool *value);

/* Get a value from a nested object structure using a number of keys.
 * Return NULL if a key does not match the structure or if a
 * non-object is encountered. */
json_thing_t *json_object_dig(json_thing_t *object,
                              const char *const *keys, size_t num_keys);

/* Like json_object_dig(), but the keys are given as a
 * variable-arguments list. This is a macro so the argument list
 * doesn't need to be terminated. */
#define json_object_fetch(object, ...) \
    json_object_fetch_func(object, __VA_ARGS__, (char *) NULL)

json_thing_t *json_object_fetch_func(json_thing_t *object,
                                     const char *key, ...);


/* If the object does not have the specified field, NULL is returned. */
json_thing_t *json_object_pop(json_thing_t *object, const char *key);

/* The order of object fields is unspecified. NULL is returned for an
 * empty object. */
json_field_t *json_object_first(json_thing_t *object);

/* NULL is returned when the fields are exhausted. */
json_field_t *json_field_next(json_field_t *field);

const char *json_field_name(json_field_t *field);
json_thing_t *json_field_value(json_field_t *field);

/* json_utf8_encode() has semantics analogous to snprintf(3):
 *
 *  - If size == 0, nothing is written into buffer.
 *  - If size > 0, the encoding is always NUL-terminated.
 *  - As much as possible is written into buffer.
 *  - The returned value is strlen(3) of the complete encoding.
 *
 * As a result, a return value >= size means the encoding was truncated.
 * Also, json_utf8_encode(thing, NULL, 0) can be used to calculate the
 * space requirement of the encoding.
 */
size_t json_utf8_encode(json_thing_t *thing, void *buffer, size_t size);

/* Like json_utf8_encode() but produces a prettyprinted output. The base
 * indendation is 'left_margin'. The indentation step is 'indentation'.
 * The first line of the output is not indented. The last line of the
 * output is not terminated with a newline. */
size_t json_utf8_prettyprint(json_thing_t *thing, void *buffer, size_t size,
                             unsigned left_margin, unsigned indentation);

/* Pretty-print the JSON thing to the given file. Terminate the output
 * with a newline. Return whatever fprintf(3) returns. */
int json_utf8_dump(json_thing_t *thing, FILE *f);

/* A plugin function for fstrace's %I directive. It is safe to use a
 * maximum of four times on a single fstrace line.
 *
 * See also: json_trace_max_size(). */
const char *json_trace(void /* json_thing_t */ *thing);

/* By default, json_trace() limits the output to a "reasonable" size.
 * This pseudofield can be used to adjust the size for the next call to
 * json_trace(). The function always returns "". Example:
 *
 * FSTRACE_DECL(XYZ_CONFIGURE, "UID=%64u CONFIGURATION=%I%I");
 *
 * static void xyz_configure(xyz_t *xyz, json_thing_t *config)
 * {
 *     const size_t MAX_SIZE = 2000000;
 *     FSTRACE(XYZ_CONFIGURE, xyz->uid, json_trace_max_size, &MAX_SIZE,
 *             json_trace, config);
 *     ...;
 * }
 */
const char *json_trace_max_size(void /* size_t */ *size);

/* A plugin function for fstrace's %I directive. Like json_trace()
 * (q.v.) but only returns the symbolic name of the thing type. */
const char *json_trace_thing_type(void /* json_thing_t */ *thing);

/* A plugin function for fstrace's %I directive. Return the symbolic
 * name of the type. */
const char *json_trace_type(void /* json_thing_type_t */ *ptype);

/* Parse the given JSON encoding and return the corresponding decoding
 * or NULL in case of a syntax error.
 *
 * Note: numbers are coded as JSON_UNSIGNED or JSON_INTEGER if they fall
 * within [LLONG_MIN, ULLONG_MAX] and contain neither a decimal point
 * nor an exponent. It is not specified whether JSON_UNSIGNED or
 * JSON_INTEGER is chosen in the overlapping range [0..LLONG_MAX]. */
json_thing_t *json_utf8_decode(const void *buffer, size_t size);

/* Parse the given JSON encoding and return the corresponding decoding
 * or NULL in case of a syntax error. */
json_thing_t *json_utf8_decode_string(const char *encoding);

/* Parse the JSON encoding read from the given file and return the
 * corresponding decoding or NULL in case of an error (consult errno).
 * In addition to 'read' errors this function can set errno to the
 * following values:
 *
 * EINVAL syntax error
 * ENOMEM out of memory or data exceeds the given maximum size
 *
 * This function may fail with errno set to EINTR if a system call is
 * interrupted by a signal. */
json_thing_t *json_utf8_decode_file(FILE *f, size_t max_size);

/* Return true if and only if a and b are (recursively) equal. The operands must
 * not have been constructed with the help of json_make_raw().
 *
 * Note that JSON_FLOAT may yield suprising results because of
 * unpredictable rounding effects. The "tolerance" argument expresses
 * the maximum magnitude of the relative difference between a and be for
 * them to be considered equal. Specifically, JSON_FLOATs a and b are
 * considered equal if and only if:
 *
 *    a == b || fabs(b - a) / fmax(fabs(a), fabs(b)) < tolerance
 *
 * Note also that json_thing_equal() requires -lfsdyn -lm to link.
 */
bool json_thing_equal(json_thing_t *a, json_thing_t *b, double tolerance);

#ifdef __cplusplus
}

#include <functional>
#include <memory>
#include <string>

namespace fsecure {
namespace encjson {

using JsonThingPtr =
    std::unique_ptr<json_thing_t, std::function<void(json_thing_t *)>>;

inline JsonThingPtr make_json_thing_ptr()
{
    return { json_make_object(), json_destroy_thing };
}

inline JsonThingPtr make_json_thing_ptr(json_thing_t *thing)
{
    return { thing, json_destroy_thing };
}

#if __cplusplus >= 201703L

inline std::string dump(json_thing_t *thing)
{
    size_t size = json_utf8_encode(thing, nullptr, 0);
    std::string str;
    str.resize(size);
    json_utf8_encode(thing, str.data(), size + 1);
    return str;
}

inline std::string dump(json_thing_t *thing,
                        unsigned left_margin,
                        unsigned indent)
{
    size_t size = json_utf8_prettyprint(thing, nullptr, 0, left_margin, indent);
    std::string str;
    str.resize(size);
    json_utf8_prettyprint(thing, str.data(), size + 1, left_margin, indent);
    return str;
}

#endif

} // namespace encjson
} // namespace fsecure
#endif

#endif
