#!/bin/bash

g++ -pthread -ldl -std=c++17 test.cpp -o test
./test
rm ./test
