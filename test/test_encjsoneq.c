#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <encjson.h>

static const char *a_repr = "{                                        "
                            "  \"string\" : \"hello\",                "
                            "  \"truth\" : true,                      "
                            "  \"lie\" : false,                       "
                            "  \"nothing\" : null,                    "
                            "  \"year\" : 2017,                       "
                            "  \"months\" : [ 1, 3, 5, 7, 8, 10, 12 ],"
                            "  \"float\" : 0.1                        "
                            "}                                        ";

static const char *b_repr = "{                                        "
                            "  \"nothing\" : null,                    "
                            "  \"string\" : \"hello\",                "
                            "  \"lie\" : false,                       "
                            "  \"truth\" : true,                      "
                            "  \"year\" : 2017,                       "
                            "  \"float\" : 0.10000002,                "
                            "  \"months\" : [ 1, 3, 5, 7, 8, 10, 12 ] "
                            "}                                        ";

static const char *b_repr_missing = "{                                        "
                                    "  \"nothing\" : null,                    "
                                    "  \"string\" : \"hello\",                "
                                    "  \"lie\" : false,                       "
                                    "  \"year\" : 2017,                       "
                                    "  \"float\" : 0.10000002,                "
                                    "  \"months\" : [ 1, 3, 5, 7, 8, 10, 12 ] "
                                    "}                                        ";

static const char *b_repr_extra = "{                                        "
                                  "  \"nothing\" : null,                    "
                                  "  \"something\" : null,                  "
                                  "  \"string\" : \"hello\",                "
                                  "  \"lie\" : false,                       "
                                  "  \"truth\" : true,                      "
                                  "  \"year\" : 2017,                       "
                                  "  \"float\" : 0.10000002,                "
                                  "  \"months\" : [ 1, 3, 5, 7, 8, 10, 12 ] "
                                  "}                                        ";

static const char *b_repr_bad_string =
    "{                                        "
    "  \"nothing\" : null,                    "
    "  \"string\" : \"hell\",                 "
    "  \"lie\" : false,                       "
    "  \"truth\" : true,                      "
    "  \"year\" : 2017,                       "
    "  \"float\" : 0.10000002,                "
    "  \"months\" : [ 1, 3, 5, 7, 8, 10, 12 ] "
    "}                                        ";

static const char *b_repr_bad_int = "{                                        "
                                    "  \"nothing\" : null,                    "
                                    "  \"string\" : \"hello\",                "
                                    "  \"lie\" : false,                       "
                                    "  \"truth\" : true,                      "
                                    "  \"year\" : -2017,                      "
                                    "  \"float\" : 0.10000002,                "
                                    "  \"months\" : [ 1, 3, 5, 7, 8, 10, 12 ] "
                                    "}                                        ";

static const char *b_repr_bad_float =
    "{                                        "
    "  \"nothing\" : null,                    "
    "  \"string\" : \"hello\",                "
    "  \"lie\" : false,                       "
    "  \"truth\" : true,                      "
    "  \"year\" : 2017,                       "
    "  \"float\" : 0.1002,                    "
    "  \"months\" : [ 1, 3, 5, 7, 8, 10, 12 ] "
    "}                                        ";

static const char *b_repr_elem_missing =
    "{                                        "
    "  \"nothing\" : null,                    "
    "  \"string\" : \"hello\",                "
    "  \"lie\" : false,                       "
    "  \"truth\" : true,                      "
    "  \"year\" : 2017,                       "
    "  \"float\" : 0.10000002,                "
    "  \"months\" : [ 1, 3, 5, 8, 10, 12 ]    "
    "}                                        ";

static const char *b_repr_elem_extra =
    "{                                            "
    "  \"nothing\" : null,                        "
    "  \"string\" : \"hello\",                    "
    "  \"lie\" : false,                           "
    "  \"truth\" : true,                          "
    "  \"year\" : 2017,                           "
    "  \"float\" : 0.10000002,                    "
    "  \"months\" : [ 1, 3, 5, 7, 8, 10, 12, 13 ] "
    "}                                            ";

static void ineq_test(json_thing_t *a, const char *diff_repr, const char *msg)
{
    json_thing_t *b = json_utf8_decode_string(diff_repr);
    if (json_thing_equal(a, b, 1e-3)) {
        fprintf(stderr, "Error: a == b_%s\n", msg);
        exit(EXIT_FAILURE);
    }
    json_destroy_thing(b);
}

static void test_raw_equality()
{
    json_thing_t *a = json_make_object();
    json_add_to_object(a, "x", json_make_raw("[ 1, 2, 3]"));
    json_add_to_object(a, "y", json_make_string("hello"));
    json_thing_t *b = json_make_raw("{ \"y\": \"hello\", \"x\": [1,2,3]}");
    if (!json_thing_equal(a, b, 1e-3)) {
        fprintf(stderr, "Error: raw equality fail\n");
        exit(EXIT_FAILURE);
    }
    json_thing_t *c = json_make_raw("{ \"y\": 7, \"x\": [1,2,3]}");
    if (json_thing_equal(a, c, 1e-3)) {
        fprintf(stderr, "Error: raw inequality fail\n");
        exit(EXIT_FAILURE);
    }
    json_destroy_thing(a);
    json_destroy_thing(b);
    json_destroy_thing(c);
}

int main()
{
    json_thing_t *a = json_utf8_decode_string(a_repr);
    json_thing_t *b = json_utf8_decode_string(b_repr);
    if (!json_thing_equal(a, b, 1e-3)) {
        fprintf(stderr, "Error: a != b\n");
        exit(EXIT_FAILURE);
    }
    json_destroy_thing(b);
    ineq_test(a, b_repr_missing, "missing");
    ineq_test(a, b_repr_extra, "extra");
    ineq_test(a, b_repr_bad_string, "bad_string");
    ineq_test(a, b_repr_bad_int, "bad_int");
    ineq_test(a, b_repr_bad_float, "bad_float");
    ineq_test(a, b_repr_elem_missing, "elem_missing");
    ineq_test(a, b_repr_elem_extra, "elem_extra");
    json_destroy_thing(a);
    test_raw_equality();
    fprintf(stderr, "Ok\n");
    return EXIT_SUCCESS;
}
