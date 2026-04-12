#!/bin/bash

g++ -D TEST_BUILD -pthread -ldl -std=c++17 test.cpp -o test
./test
rm ./test
