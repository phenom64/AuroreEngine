#!/bin/bash

grep "#define" $1 |\
    tail -n +2 |\
    cut -d" " -f2 |\
    awk '{print "constexpr auto "tolower(substr($0,10))" = "$0";"}' > $2
