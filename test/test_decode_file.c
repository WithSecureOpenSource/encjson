#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <encjson.h>
#include <fsdyn/charstr.h>

int main()
{
    json_thing_t *thing = json_utf8_decode_file(stdin, (size_t) -1);
    size_t size = json_utf8_encode(thing, NULL, 0) + 1;
    char buffer[size];
    (void) json_utf8_encode(thing, buffer, size);
    printf("%s\n", buffer);
    const char *expected_prefix = "{"
                                  "\"string\":\"\\t\\\"¿xyzzy? 𤭢\","
                                  "\"truth\":true,"
                                  "\"lie\":false,"
                                  "\"nothing\":null,"
                                  "\"year\":2017,"
                                  "\"months\":[1,3,5,7,8,10,12],"
                                  "\"π\":";
    double expected_middle = 31415.9265e-4;
    const char *expected_suffix = "}";
    const char *then = charstr_skip_prefix(buffer, expected_prefix);
    const char *finally = "";
    double value = 0;
    if (then)
        value = strtod(then, (char **) &finally);
    if (!then || value != expected_middle || finally == then) {
        fprintf(stderr, "Error: expected\n%s%g%s\n", expected_prefix,
                expected_middle, expected_suffix);
        return EXIT_FAILURE;
    }
    json_destroy_thing(thing);
    fprintf(stderr, "Ok\n");
    return EXIT_SUCCESS;
}
