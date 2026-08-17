#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <encjson.h>
#include <fsdyn/charstr.h>
#include <fsdyn/fsalloc.h>

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

/* Byte sequences that are structurally plausible UTF-8 but not valid
 * UTF-8. The decoder must reject them; otherwise the decoded string
 * violates the UTF-8 invariant asserted by
 * json_adopt_bounded_string(). */
static bool test_invalid_utf8()
{
    static const char *const invalid[] = {
        "\"\xc0\x80\"",                 /* overlong U+0000 */
        "\"\xc0\xaf\"",                 /* overlong U+002F */
        "\"\xc1\xbf\"",                 /* overlong U+007F */
        "\"\xe0\x80\xaf\"",             /* overlong U+002F */
        "\"\xe0\x9f\xbf\"",             /* overlong U+07FF */
        "\"\xf0\x80\x80\xaf\"",         /* overlong U+002F */
        "\"\xf0\x8f\xbf\xbf\"",         /* overlong U+FFFF */
        "\"\xed\xa0\x80\"",             /* U+D800, a high surrogate */
        "\"\xed\xbf\xbf\"",             /* U+DFFF, a low surrogate */
        "\"\xed\xa0\xbd\xed\xb8\x80\"", /* CESU-8 surrogate pair */
        "\"\xf4\x90\x80\x80\"",         /* U+110000, out of range */
        "\"\xf5\x80\x80\x80\"",         /* out of range */
        "\"\xf7\xbf\xbf\xbf\"",         /* out of range */
        "\"\xf8\x88\x80\x80\x80\"",     /* five-byte sequence */
        "\"\x80\"",                     /* lone continuation byte */
        "\"\xe2\x82\"",                 /* truncated U+20AC */
        "\"\xe2\x82"
        "1\"",                  /* U+20AC missing a trailer */
        "{\"\xed\xa0\x80\":1}", /* invalid UTF-8 in an object key */
        NULL
    };
    for (int i = 0; invalid[i]; i++) {
        json_thing_t *thing = json_utf8_decode_string(invalid[i]);
        if (thing) {
            fprintf(stderr, "Invalid UTF-8 accepted at %d\n", i);
            json_destroy_thing(thing);
            return false;
        }
    }
    return true;
}

/* Conversely, valid UTF-8 must be decoded verbatim, including the
 * shortest-form encodings at the range boundaries. */
static bool test_valid_utf8()
{
    static const struct {
        const char *repr;
        const char *value;
    } valid[] = { { "\"\x7f\"", "\x7f" },
                  { "\"\xc2\x80\"", "\xc2\x80" },                 /* U+0080 */
                  { "\"\xdf\xbf\"", "\xdf\xbf" },                 /* U+07FF */
                  { "\"\xe0\xa0\x80\"", "\xe0\xa0\x80" },         /* U+0800 */
                  { "\"\xed\x9f\xbf\"", "\xed\x9f\xbf" },         /* U+D7FF */
                  { "\"\xee\x80\x80\"", "\xee\x80\x80" },         /* U+E000 */
                  { "\"\xef\xbf\xbf\"", "\xef\xbf\xbf" },         /* U+FFFF */
                  { "\"\xf0\x90\x80\x80\"", "\xf0\x90\x80\x80" }, /* U+10000 */
                  { "\"\xf4\x8f\xbf\xbf\"", "\xf4\x8f\xbf\xbf" }, /* U+10FFFF */
                  { "\"¿xyzzy?\"", "¿xyzzy?" },
                  { NULL } };
    for (int i = 0; valid[i].repr; i++) {
        json_thing_t *thing = json_utf8_decode_string(valid[i].repr);
        if (!thing) {
            fprintf(stderr, "Valid UTF-8 rejected at %d\n", i);
            return false;
        }
        if (json_thing_type(thing) != JSON_STRING) {
            fprintf(stderr, "Bad UTF-8 string conversion type at %d\n", i);
            json_destroy_thing(thing);
            return false;
        }
        if (strcmp(json_string_value(thing), valid[i].value)) {
            fprintf(stderr, "Bad UTF-8 string conversion at %d\n", i);
            json_destroy_thing(thing);
            return false;
        }
        json_destroy_thing(thing);
    }
    return true;
}

static bool encodes_as(json_thing_t *thing, const char *expected)
{
    size_t size = json_utf8_encode(thing, NULL, 0) + 1;
    char buffer[size];
    (void) json_utf8_encode(thing, buffer, size);
    if (size - 1 != strlen(expected) || strcmp(buffer, expected)) {
        fprintf(stderr, "Error: expected %s, got %s\n", expected, buffer);
        return false;
    }
    return true;
}

/* A string value carries an explicit byte length: it may contain NUL
 * characters, and a value adopted with json_adopt_bounded_string() need
 * not have a NUL terminator at all. Encoding, cloning and comparing
 * must all honor that length instead of scanning for a NUL. */
static bool test_bounded_string()
{
    static const char bytes[] = { 'a', '\0', 'b' };
    json_thing_t *thing = json_make_bounded_string(bytes, sizeof bytes);
    if (json_string_length(thing) != sizeof bytes) {
        fprintf(stderr, "Bad bounded string length\n");
        return false;
    }
    if (!encodes_as(thing, "\"a\\u0000b\""))
        return false;
    json_thing_t *clone = json_clone(thing);
    if (json_string_length(clone) != sizeof bytes ||
        memcmp(json_string_value(clone), bytes, sizeof bytes)) {
        fprintf(stderr, "Clone did not preserve the bounded string\n");
        return false;
    }
    if (!json_thing_equal(thing, clone, 0)) {
        fprintf(stderr, "Bounded string not equal to its clone\n");
        return false;
    }
    json_destroy_thing(clone);
    /* Neither the bytes past the first NUL nor the length may be
     * ignored by the comparison. */
    static const char other_bytes[] = { 'a', '\0', 'c' };
    json_thing_t *other = json_make_bounded_string(other_bytes, sizeof bytes);
    json_thing_t *prefix = json_make_bounded_string(bytes, 1);
    if (json_thing_equal(thing, other, 0) ||
        json_thing_equal(thing, prefix, 0)) {
        fprintf(stderr, "Distinct bounded strings compared equal\n");
        return false;
    }
    json_destroy_thing(other);
    json_destroy_thing(prefix);
    /* The embedded NUL must survive a round trip. */
    json_thing_t *decoded = json_utf8_decode_string("\"a\\u0000b\"");
    if (!decoded || json_thing_type(decoded) != JSON_STRING ||
        json_string_length(decoded) != sizeof bytes ||
        memcmp(json_string_value(decoded), bytes, sizeof bytes)) {
        fprintf(stderr, "Bad NUL round trip\n");
        return false;
    }
    json_destroy_thing(decoded);
    json_destroy_thing(thing);
    return true;
}

/* Same, for a value whose buffer has no NUL terminator whatsoever. A
 * deterministic detection of the over-read needs a sanitizer; this at
 * least pins the encoded output and the byte count. */
static bool test_adopted_string()
{
    enum { SIZE = 8 };
    char *buffer = fsalloc(SIZE); /* deliberately not SIZE + 1 */
    memset(buffer, 'A', SIZE);
    json_thing_t *thing = json_adopt_bounded_string(buffer, SIZE);
    if (json_string_length(thing) != SIZE) {
        fprintf(stderr, "Bad adopted string length\n");
        return false;
    }
    if (!encodes_as(thing, "\"AAAAAAAA\""))
        return false;
    json_thing_t *clone = json_clone(thing);
    if (json_string_length(clone) != SIZE ||
        memcmp(json_string_value(clone), buffer, SIZE)) {
        fprintf(stderr, "Clone did not preserve the adopted string\n");
        return false;
    }
    json_destroy_thing(clone);
    json_destroy_thing(thing);
    /* A Latin-1 control character escape at the very end of the buffer
     * must not make the encoder look at the byte after it. */
    char *tail = fsalloc(2);
    tail[0] = (char) 0xc2;
    tail[1] = (char) 0x85; /* U+0085 */
    thing = json_adopt_bounded_string(tail, 2);
    if (!encodes_as(thing, "\"\\u0085\""))
        return false;
    json_destroy_thing(thing);
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
    if (!test_invalid_utf8())
        return EXIT_FAILURE;
    if (!test_valid_utf8())
        return EXIT_FAILURE;
    if (!test_bounded_string())
        return EXIT_FAILURE;
    if (!test_adopted_string())
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
