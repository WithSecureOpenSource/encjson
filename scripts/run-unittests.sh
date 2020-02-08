#!/bin/bash

main () {
    cd "$(dirname "$0")/.." &&
    if [ -n "$FSARCHS" ]; then
        local archs=()
        IFS=, read -ra archs <<< "$FSARCHS"
        for arch in "${archs[@]}" ; do
            do_test "$arch"
        done
        return
    fi
    case "$(uname -s)" in
        Linux)
            case "$(uname -m)" in
                x86_64)
                    do_test linux64
                    ;;
                i686)
                    do_test linux32
                    ;;
                *)
                    echo "Bad CPU" >&2
                    exit 1
            esac
            ;;
        Darwin)
            do_test darwin
            ;;
        *)
            echo "Bad system" >&2
            exit 1
            ;;
    esac
}

do_test () {
    arch=$1
    echo >&2 &&
    echo "test_encjson[$arch]..." >&2 &&
    stage/$arch/build/test/test_encjson &&
    echo >&2 &&
    echo "test_enjsoneq[$arch]..." >&2 &&
    stage/$arch/build/test/test_encjsoneq &&
    echo >&2 &&
    echo "test_decode_empty_file[$arch]..." >&2 &&
    stage/$arch/build/test/test_decode_empty_file &&
    echo "test_decode_file[$arch]..." >&2 &&
    stage/$arch/build/test/test_decode_file <<EOF
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
}

main "$@"
