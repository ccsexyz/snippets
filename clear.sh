#!/bin/bash

rm -rf CMakeCache.txt
rm -rf CMakeFiles
rm -rf Makefile
rm -rf *.cmake

cat CMakeLists.txt| grep add_executable | sed 's/(/\ /g' | awk '{print $2}' | xargs rm
rm -rf testdb
