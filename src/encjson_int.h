#include <stdbool.h>

/* Serialize the given value, which isnormal(3), into buffer, which
 * can be assumed to be sufficiently large to hold the entire
 * encoding. Terminate the encoding with NUL.  */
extern void encjson_internal_encode_float(double value, char buffer[]);

/* Deserialize the NUL-terminated buffer into value. Ignore possible
 * trailing bytes. Return false if the input is invalid. It is ok to
 * pass a value that !isnormal(3). */
extern bool encjson_internal_decode_float(const char buffer[], double *value);
