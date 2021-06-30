#include <errno.h>
#include <stdio.h>

#include <encjson.h>

int main()
{
    FILE *f = tmpfile();
    json_thing_t *thing = json_utf8_decode_file(f, (size_t) -1);
    fclose(f);
    return thing || errno != EINVAL;
}
