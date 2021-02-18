#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h> 
#include <cstring>

using namespace std;

class autojoin {
    string receiveport;

    public:
    autojoin() {}
    vector<string> parseString(string input, char delimeter);
    int createHostConnection(int port, int mode, struct sockaddr_in sock);
    int createClientConnection(string ip, int port, int mode, struct sockaddr_in socketadd);
};

