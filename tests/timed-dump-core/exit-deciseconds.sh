#!/bin/bash

deciseconds=$( /usr/bin/time -f "%U" "$@" 2>&1 | sed 's/\.\([0-9]\)[0-9]*$/\1/' )

echo deciseconds is $deciseconds 1>&2

exit $deciseconds
