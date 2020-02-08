#pragma once
static __attribute__((constructor)) void ENCJSON_VERSION()
{
    extern const char *encjson_version_tag;
    if (!*encjson_version_tag)
        encjson_version_tag++;
}
