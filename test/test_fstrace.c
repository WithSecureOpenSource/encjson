#include <stdio.h>
#include <string.h>
#include <encjson.h>
#include <fstrace.h>

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

FSTRACE_DECL(XYZ_CONFIGURE,
             "UID=%64u CFG200=%I%I CFG20=%I%I CFG=%I CFG2=%I%I");

int main()
{
    fstrace_t *trace = fstrace_direct(stderr);
    fstrace_declare_globals(trace);
    fstrace_select_regex(trace, ".", NULL);
    json_thing_t *thing = json_utf8_decode_string(data);
    const size_t size200 = 200;
    const size_t size20 = 20;
    const size_t size2 = 2;
    FSTRACE(XYZ_CONFIGURE, (uint64_t) 123,
            json_trace_max_size, &size200, json_trace, thing,
            json_trace_max_size, &size20, json_trace, thing,
            json_trace, thing,
            json_trace_max_size, &size2, json_trace, thing);
    json_destroy_thing(thing);
    return 0;
}
