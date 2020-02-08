#include <stdio.h>
#include <string.h>
#include <encjson.h>

int main()
{
    json_thing_t *thing = json_utf8_decode_file(stdin, (size_t)-1);
    size_t size = json_utf8_encode(thing, NULL, 0) + 1;
    char buffer[size];
    (void) json_utf8_encode(thing, buffer, size);
    printf("%s\n", buffer);
    const char *expected =
        "{"
            "\"string\":\"\\t\\\"¿xyzzy? 𤭢\","
            "\"truth\":true,"
            "\"lie\":false,"
            "\"nothing\":null,"
            "\"year\":2017,"
            "\"months\":[1,3,5,7,8,10,12],"
            "\"π\":3.14159265000000020862"
        "}";
    if (strcmp(buffer, expected)) {
        fprintf(stderr, "Error: expected\n%s\n", expected);
        return 1;
    }
    json_destroy_thing(thing);
    fprintf(stderr, "Ok\n");
    return 0;
}
