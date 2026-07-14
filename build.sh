#!/bin/bash

mkdir -p build

cd build

cmake ..

make -j4

mv ./predictionmarkets_rec ../predictionmarkets-rec
