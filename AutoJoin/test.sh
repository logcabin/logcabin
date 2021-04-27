#!/bin/bash
if [ -d "./googletest" ]
then
    echo "Google test already exists"
else
    git clone https://github.com/google/googletest.git
fi
cd build
cmake ..
make
./test