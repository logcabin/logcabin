FROM ubuntu:18.04

RUN apt-get update && \
    apt-get install -y \
    gcc=4:7.4.0-1ubuntu2.3 g++=4:7.4.0-1ubuntu2.3 gdb=8.1-0ubuntu3.2 \
    scons=3.0.1-1 \
    libcrypto++-dev=5.6.4-8 \
    libprotobuf-dev=3.0.0-9.1ubuntu1 protobuf-compiler=3.0.0-9.1ubuntu1 \
    doxygen=1.8.13-10 \
    net-tools 

VOLUME /logcabin

WORKDIR /logcabin
