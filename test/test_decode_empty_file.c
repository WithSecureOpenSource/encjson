#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <encjson.h>

int main()
{
    FILE *f = tmpfile();
    json_thing_t *thing = json_utf8_decode_file(f, (size_t) -1);
    fclose(f);
    if (thing || errno != EINVAL)
        return EXIT_FAILURE;
    fprintf(stderr, "Ok\n");
    return EXIT_SUCCESS;
}
