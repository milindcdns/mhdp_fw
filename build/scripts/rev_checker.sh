#!/bin/sh

# If directory was not set, use current working dir
if [[ -z $1 ]]; then
    CSS=$(pwd)
else
    CSS=$1
fi

# Set correct command
if [[ -d $CSS/.git ]]; then
    CMD="git svn"
elif [[ -d $CSS/.svn ]]; then
    CMD="svn"
else
    exit 1
fi

# Get revision number and return it if no error occured
REV=$($CMD info $CSS | grep "Revision:" | cut -d " " -f "2")
ERR_CODE=$?

if [[ $ERR_CODE -eq 0 ]]; then
    echo $REV
fi

exit $ERR_CODE


