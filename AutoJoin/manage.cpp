#include "autojoin.cpp"
#include "zeroconf/browse-services.c"
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
    string name = "";
    string address = "";
    string mode = "";
    if (argc >= 3)
    {
        string name = argv[0];
        string address = argv[1];

        string mode = argv[2];
    }
    else
    {
        printf("Please enter all 3 arguments (name of node), (address), (connection mode)\n");
        printf("Ex. ./manage node 192.168.1.1 auto\n");
        return -1;
    }

    if (mode == "auto")
    {
        address = "abc";
        browse();
    }

    autojoinprogram(name, "550", "host", AF_INET);
}