#!/bin/bash

# 'fracseconds'
case "$(basename "$0")" in
	(*deciseconds*) ord=1 ;;
	(*centiseconds*) ord=2 ;;
	(*) echo "Did not understand `basename $0`" 1>&2; exit 1 ;;
esac

timefile=`mktemp`
/usr/bin/time -f "%U" "$@" > "$timefile" 2>&1
status=$?
fracseconds="$( cat "$timefile" | sed "s/\.\([0-9]\)\{${ord}\}[0-9]*\$/\1/" )"
rm -f "$timefile"

echo status is $status 1>&2
echo fracseconds is $fracseconds 1>&2

exit $fracseconds
