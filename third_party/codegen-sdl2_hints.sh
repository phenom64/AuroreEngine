#!/bin/bash

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

grep "#define" $1 |\
    tail -n +2 |\
    cut -d" " -f2 |\
    awk '{print "constexpr auto "tolower(substr($0,10))" = "$0";"}' > $2
