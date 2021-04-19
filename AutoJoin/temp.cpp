#include "autojoin.cpp"
#include "string"

// #include <gtest/gtest.h>
// #include <thread>

// #include "Core/Debug.h"
// #include "Event/Loop.h"
// #include "Protocol/Common.h"
// #include "RPC/Address.h"
// #include "RPC/OpaqueServer.h"
// #include "RPC/OpaqueServerRPC.h"

int main(int argc, char **argv)
{
    string name = argv[0];
    string address = argv[1];

    string mode = argv[2];

    if (mode == "auto"):

        address = ''

    autojoinprogram(name, "550", "host", AF_INET);
}