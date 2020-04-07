#!/bin/bash

> $2

while read line; do
        echo ${line:6:2}${line:4:2}${line:2:2}${line:0:2} | xxd -r -p >> $2
        done <$1
