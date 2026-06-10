#!/bin/bash

mkdir -p build

cd build

cmake ..

make -j4

mv ./ratus_rec ../
