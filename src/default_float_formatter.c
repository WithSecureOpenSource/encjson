#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "encjson_int.h"

void encjson_internal_encode_float(double value, char buffer[])
{
    /* Note: this is sensitive to setlocale() in the application. */
    sprintf(buffer, "%.21g", value);
}

bool encjson_internal_decode_float(const char buffer[], double *value)
{
    /* Note: this is sensitive to setlocale() in the application. */
    *value = strtod(buffer, NULL);
    return *value == 0 || errno != ERANGE;
}
