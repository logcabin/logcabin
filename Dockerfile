FROM ubuntu:18.04

RUN apt-get update && \
    apt-get install -y gcc g++ scons libgtest-dev libcrypto++-dev libprotobuf-dev protobuf-compiler

RUN apt-get -y install doxygen
RUN apt-get -y install net-tools
RUN apt-get -y install gdb

VOLUME /logcabin

WORKDIR /logcabin
