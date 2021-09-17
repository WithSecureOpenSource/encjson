#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <encjson.h>
#include <fsdyn/charstr.h>

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
        return false;
    }
    json_destroy_thing(thing);
    return true;
}

static bool test_simple2()
{
    json_thing_t *thing = json_utf8_decode_string(data);
    size_t size = json_utf8_prettyprint(thing, NULL, 0, 0, 4) + 1;
    char buffer[size];
    (void) json_utf8_prettyprint(thing, buffer, size, 0, 4);
    printf("%s\n", buffer);
    const char *expected_prefix = "{\n"
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
                                  "    \"π\": ";
    double expected_middle = 31415.9265e-4;
    const char *expected_suffix = "\n}";
    const char *then = charstr_skip_prefix(buffer, expected_prefix);
    const char *finally = "";
    double value = 0;
    if (then)
        value = strtod(then, (char **) &finally);
    if (!then || value != expected_middle || finally == then) {
        fprintf(stderr, "Error: expected\n%s%g%s\n", expected_prefix,
                expected_middle, expected_suffix);
        return false;
    }
    json_destroy_thing(thing);
    return true;
}

static bool test_integers()
{
    static struct {
        const char *repr;
        unsigned long long value;
    } integers[] = { { "-0.0", 0 },
                     { "20", 20 },
                     { "9223372036854775807", 9223372036854775807 },
                     { "922337203685477580.7E1", 9223372036854775807 },
                     { "4E9", 4000000000 },
                     { "5.000000001E9", 5000000001 },
                     { "5.000000001E9", 5000000001 },
                     { "0.0001E4", 1 },
                     { NULL } };
    for (int i = 0; integers[i].repr; i++) {
        json_thing_t *thing = json_utf8_decode_string(integers[i].repr);
        switch (json_thing_type(thing)) {
            case JSON_INTEGER:
                if (json_integer_value(thing) != integers[i].value) {
                    fprintf(stderr,
                            "Bad integer conversion at %d: %lld~ %lld\n", i,
                            json_integer_value(thing), integers[i].value);
                    return false;
                }
                break;
            case JSON_UNSIGNED:
                if (json_unsigned_value(thing) != integers[i].value) {
                    fprintf(stderr,
                            "Bad integer conversion at %d: %llu ~ %llu\n", i,
                            json_unsigned_value(thing), integers[i].value);
                    return false;
                }
                break;
            default:
                fprintf(stderr, "Bad integer conversion type at %d\n", i);
                return false;
        }
        json_destroy_thing(thing);
    }
    return true;
}

static bool test_signed()
{
    static struct {
        const char *repr;
        long long value;
    } s_integers[] = { { "-9223372036854775808", -9223372036854775808U },
                       { "-922337203685477580.8E1", -9223372036854775808U },
                       { NULL } };
    for (int i = 0; s_integers[i].repr; i++) {
        json_thing_t *thing = json_utf8_decode_string(s_integers[i].repr);
        switch (json_thing_type(thing)) {
            case JSON_INTEGER:
                if (json_integer_value(thing) != s_integers[i].value) {
                    fprintf(stderr, "Bad signed conversion at %d: %lld~ %lld\n",
                            i, json_integer_value(thing), s_integers[i].value);
                    return false;
                }
                break;
            default:
                fprintf(stderr, "Bad signed conversion type at %d\n", i);
                return false;
        }
        json_destroy_thing(thing);
    }
    return true;
}

static bool test_unsigned()
{
    static struct {
        const char *repr;
        unsigned long long value;
    } u_integers[] = { { "9223372036854775808", 9223372036854775808U },
                       { "922337203685477580.8E1", 9223372036854775808U },
                       { "18446744073709551615", 18446744073709551615U },
                       { "1844674407370955161.5E1", 18446744073709551615U },
                       { "1844674407370955161500.0000E-2",
                         18446744073709551615U },
                       { NULL } };
    for (int i = 0; u_integers[i].repr; i++) {
        json_thing_t *thing = json_utf8_decode_string(u_integers[i].repr);
        switch (json_thing_type(thing)) {
            case JSON_UNSIGNED:
                if (json_unsigned_value(thing) != u_integers[i].value) {
                    fprintf(stderr,
                            "Bad unsigned conversion at %d: %llu ~ %llu\n", i,
                            json_unsigned_value(thing), u_integers[i].value);
                    return false;
                }
                break;
            default:
                fprintf(stderr, "Bad unsigned conversion type at %d\n", i);
                return false;
        }
        json_destroy_thing(thing);
    }
    return true;
}

static bool test_float()
{
    static struct {
        const char *repr;
        double value;
    } floats[] = { { "-1.1", -1.1 },
                   { "18446744073709551616", 18446744073709551616.0 },
                   { "1844674407370955161.6E1", 18446744073709551616.0 },
                   { "184467440737095516160E-1", 18446744073709551616.0 },
                   { "-1844674407370955161500.0001E-2",
                     -18446744073709551615.0 },
                   { "18446744073709551620", 18446744073709551620.0 },
                   { "18446744073709551700", 18446744073709551700.0 },
                   { "18446744073709552000", 18446744073709552000.0 },
                   { NULL } };
    for (int i = 0; floats[i].repr; i++) {
        json_thing_t *thing = json_utf8_decode_string(floats[i].repr);
        switch (json_thing_type(thing)) {
            case JSON_FLOAT:
                if (json_double_value(thing) != floats[i].value) {
                    fprintf(stderr,
                            "Bad float conversion at %d: %.21g ~ %.21g\n", i,
                            json_double_value(thing), floats[i].value);
                    return false;
                }
                break;
            default:
                fprintf(stderr, "Bad float conversion type at %d\n", i);
                return false;
        }
        json_destroy_thing(thing);
    }
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
    if (!test_simple2())
        return EXIT_FAILURE;
    if (!test_integers())
        return EXIT_FAILURE;
    if (!test_signed())
        return EXIT_FAILURE;
    if (!test_unsigned())
        return EXIT_FAILURE;
    if (!test_float())
        return EXIT_FAILURE;
    if (!test_big_array())
        return EXIT_FAILURE;
    if (!test_big_object())
        return EXIT_FAILURE;
    if (!test_nested_object())
        return EXIT_FAILURE;
    fprintf(stderr, "Ok\n");
    return EXIT_SUCCESS;
}
