#!/usr/bin/env bash

main () {
    cd "$(dirname "$0")/.." &&
    if [ -n "$FSARCHS" ]; then
        local archs=()
        IFS=, read -ra archs <<< "$FSARCHS"
        for arch in "${archs[@]}" ; do
            run-tests "$arch"
        done
    else
        local os=$(uname -m -s)
        case $os in
            "Darwin arm64")
                run-tests darwin;;
            "Darwin x86_64")
                run-tests darwin;;
            "FreeBSD amd64")
                run-tests freebsd_amd64;;
            "Linux i686")
                run-tests linux32;;
            "Linux x86_64")
                run-tests linux64;;
            "Linux aarch64")
                run-tests linux_arm64;;
            "OpenBSD amd64")
                run-tests openbsd_amd64;;
            *)
                echo "$0: Unknown OS architecture: $os" >&2
                exit 1
        esac
    fi
}

run-tests () {
    arch=$1
    echo >&2 &&
    echo "test_encjson[$arch]..." >&2 &&
    stage/$arch/build/test/test_encjson &&
    echo >&2 &&
    echo "test_encjsoneq[$arch]..." >&2 &&
    stage/$arch/build/test/test_encjsoneq &&
    echo >&2 &&
    echo "test_decode_empty_file[$arch]..." >&2 &&
    stage/$arch/build/test/test_decode_empty_file &&
    echo >&2 &&
    echo "test_decode_file[$arch]..." >&2 &&
    stage/$arch/build/test/test_decode_file <<EOF &&
{
  "string" : "\t\"¿xyzzy? \uD852\udf62",
  "truth" : true,
  "lie" : false,
  "nothing" : null,
  "year" : 2017,
  "months" : [ 1, 3, 5, 7, 8, 10, 12 ],
  "π" : 31415.9265e-4
}
EOF
    echo >&2
}

main "$@"
