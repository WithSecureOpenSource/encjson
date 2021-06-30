#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <encjson.h>

static const char *data = "\n"
                          "{\n"
                          "  \"string\" : \"\\t\\\"¿xyzzy? \\uD852\\udf62\",\n"
                          "  \"truth\" : true,\n"
                          "  \"lie\" : false,\n"
                          "  \"nothing\" : null,\n"
                          "  \"year\" : 2017,\n"
                          "  \"months\" : [ 1, 3, 5, 7, 8, 10, 12 ],\n"
                          "  \"π\" : 31415.9265e-4\n"
                          "}\n";

static bool test_simple()
{
    json_thing_t *thing = json_utf8_decode_string(data);
    size_t size = json_utf8_encode(thing, NULL, 0) + 1;
    char buffer[size];
    (void) json_utf8_encode(thing, buffer, size);
    printf("%s\n", buffer);
    const char *expected = "{"
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
        return false;
    }
    const char *expected2 = "{\n"
                            "    \"string\": \"\\t\\\"¿xyzzy? 𤭢\",\n"
                            "    \"truth\": true,\n"
                            "    \"lie\": false,\n"
                            "    \"nothing\": null,\n"
                            "    \"year\": 2017,\n"
                            "    \"months\": [\n"
                            "        1,\n"
                            "        3,\n"
                            "        5,\n"
                            "        7,\n"
                            "        8,\n"
                            "        10,\n"
                            "        12\n"
                            "    ],\n"
                            "    \"π\": 3.14159265000000020862\n"
                            "}";
    size = json_utf8_prettyprint(thing, NULL, 0, 0, 4) + 1;
    char buffer2[size];
    (void) json_utf8_prettyprint(thing, buffer2, size, 0, 4);
    printf("%s\n", buffer2);
    if (strcmp(buffer2, expected2)) {
        fprintf(stderr, "Error: expected\n%s\n", expected2);
        return false;
    }
    json_destroy_thing(thing);
    fprintf(stderr, "Ok\n");
    return true;
}

static bool test_big_array()
{
    json_thing_t *array = json_make_array();
    int i;
    for (i = 0; i < 100000; i++)
        json_add_to_array(array, json_make_integer(i));
    while (--i >= 0) {
        long long value;
        if (!json_array_get_integer(array, i, &value)) {
            fprintf(stderr, "Could not retrieve element %d from array\n", i);
            return false;
        }
        if (value != i) {
            fprintf(stderr, "Bad integer at element %d of array\n", i);
            return false;
        }
    }
    json_destroy_thing(array);
    return true;
}

static bool test_big_object()
{
    json_thing_t *object = json_make_object();
    int i;
    for (i = 0; i < 100000; i++) {
        char buffer[20];
        sprintf(buffer, "%d", i);
        json_add_to_object(object, buffer, json_make_integer(i));
    }
    while (--i >= 0) {
        long long value;
        char buffer[20];
        sprintf(buffer, "%d", i);
        if (!json_object_get_integer(object, buffer, &value)) {
            fprintf(stderr, "Could not retrieve element %d from object\n", i);
            return false;
        }
        if (value != i) {
            fprintf(stderr, "Bad integer at element %d of object\n", i);
            return false;
        }
    }
    json_destroy_thing(object);
    return true;
}

static bool test_nested_object()
{
    json_thing_t *it = json_make_object();
    json_thing_t *a = json_make_object();
    json_thing_t *b = json_make_object();
    json_thing_t *c = json_make_object();
    json_add_to_object(it, "a", a);
    json_add_to_object(a, "b", b);
    json_add_to_object(b, "c", c);
    json_add_to_object(c, "d", json_make_integer(7));
    if (json_integer_value(json_object_fetch(it, "a", "b", "c", "d")) != 7)
        return false;
    static const char *const keys[] = { "a", "b", "c" };
    if (json_object_dig(it, keys, 3) != c)
        return false;
    if (json_object_fetch(it, "a", "b", "c", "d", "e"))
        return false;
    if (json_object_fetch(it, "a", "b", "c", "e"))
        return false;
    return true;
}

int main()
{
    if (!test_simple())
        return EXIT_FAILURE;
    if (!test_big_array())
        return EXIT_FAILURE;
    if (!test_big_object())
        return EXIT_FAILURE;
    if (!test_nested_object())
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
