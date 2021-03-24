#include "autojoin.h"

vector<string> autojoin::parseString(string input, char delimeter)
{
    vector<string> returnlist = vector<string>();
    int length = (int)input.length();
    string temp = "";

    for (int index = 0; index < length; index++)
    {
        if (input[index] == delimeter && temp != "")
        {
            returnlist.push_back(temp);
            temp = "";
        }
        else
        {
            temp += input[index];
        }
    }

    if (temp != "")
    {
        returnlist.push_back(temp);
    }

    return returnlist;
}

int autojoin::createHostConnection(int port, int mode, struct sockaddr_in sock)
{
    int host_fd;
    int opt = 1;

    // Creating socket file descriptor
    if ((host_fd = socket(mode, SOCK_STREAM, 0)) == 0)
    {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Forcefully attaching socket to the port 8080
    if (setsockopt(host_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)))
    {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }
    sock.sin_family = mode;
    sock.sin_addr.s_addr = INADDR_ANY;
    sock.sin_port = htons(port);

    // Forcefully attaching socket to the port
    if (bind(host_fd, (struct sockaddr *)&sock, sizeof(sock)) < 0)
    {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    return host_fd;
}

int autojoin::createClientConnection(string ip, int port, int mode, struct sockaddr_in socketadd)
{
    int sock = 0, valread;
    char address[100];
    cout << "Copying address into ip" << endl;
    strcpy(address, ip.c_str());
    char buffer[1024] = {0};
    cout << "Creating socket" << endl;
    if ((sock = socket(mode, SOCK_STREAM, 0)) < 0)
    {
        printf("\n Socket creation error \n");
        return -1;
    }

    socketadd.sin_family = mode;
    socketadd.sin_port = htons(port);

    cout << "Convert Address" << endl;
    // Convert IPv4 and IPv6 addresses from text to binary form
    if (inet_pton(mode, address, &socketadd.sin_addr) <= 0)
    {
        printf("\nInvalid address/ Address not supported \n");
        return -1;
    }

    cout << "Connecting to remote host" << endl;
    if (connect(sock, (struct sockaddr *)&socketadd, sizeof(socketadd)) < 0)
    {
        printf("\nConnection Failed \n");
        return -1;
    }

    return sock;
}
// Name is the hash for identicon, port is the port that it will listen on, remote address/host/node 
// remote address is the address to reach out to for join, host makes the node the host, node puts it into passive mode
// Mode is for AF_INET for ipv4 and AF_INET6 for ipv6
int autojoinprogram(string name, string port, string remoteaddress, int mode)
{
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    int newsocket, bRead;
    char buffer[1024] = {0};
    autojoin autojoinobject = autojoin();
    while (1)
    {
        if (remoteaddress == "host" || remoteaddress == "Host")
        {

            cout << "Node set as host node" << endl;
            int hostsocket = autojoinobject.createHostConnection(stoi(port), mode, address);

            if (listen(hostsocket, 3) < 0)
            {
                cout << "Node failure to listen on port" << endl;
                perror("listen");
                exit(EXIT_FAILURE);
            }
            while (1)
            {
                if ((newsocket = accept(hostsocket, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0)
                {
                    cout << "Node failure to accept connection" << endl;
                    perror("accept");
                    exit(EXIT_FAILURE);
                }
                cout << "Reading transmition from socket" << endl;
                bRead = read(newsocket, buffer, 1024);
                string temp = buffer;
                identicon printiden = identicon();
                printiden.generate(temp);

                if (temp == "node_no_longer_host")
                {
                    remoteaddress = "node";
                    break;
                }

                cout << "Accept connection (y or n)" << endl;

                string option = "";
                while (option != "y" && option != "n")
                {
                    cin >> option;
                    if (option != "y" && option != "n")
                    {
                        cout << "Invalid choice." << endl;
                    }
                }

                char *message = "";
                if (option == "y")
                {
                    message = "connection-accepted";
                }
                else
                {
                    message = "connection-denied";
                }

                send(newsocket, message, strlen(message), 0);
            }
        }
        if (remoteaddress == "node" || remoteaddress == "Node")
        {
            cout << "Node set as node listening for connections but not host for join" << endl;
            int hostsocket = autojoinobject.createHostConnection(stoi(port), mode, address);

            if (listen(hostsocket, 3) < 0)
            {
                cout << "Node failure to listen on port" << endl;
                perror("listen");
                exit(EXIT_FAILURE);
            }
            while (1)
            {
                if ((newsocket = accept(hostsocket, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0)
                {
                    cout << "Reading transmition from socket" << endl;
                    bRead = read(newsocket, buffer, 1024);
                    string temp = buffer;
                    if (temp == "node_set_as_host")
                    {
                        remoteaddress = "host";
                        break;
                    }
                    else
                    {
                        char message[1024] = "not_host_node";
                        send(newsocket, message, strlen(message), 0);
                    }
                }
                else
                {
                    perror("accept");
                    exit(EXIT_FAILURE);
                }
            }
        }
        else
        {
            cout << "Node configured as client to auto join" << endl;
            vector<string> ip = autojoinobject.parseString(remoteaddress, ':');
            cout << "Address: " + ip[0] << endl;
            cout << "Port: " + ip[1] << endl;

            if (ip.size() == 2)
            {
                newsocket = autojoinobject.createClientConnection(ip[0], stoi(ip[1]), mode, address);
                cout << "Setting up connection for join from node " + name << endl;

                if (newsocket == -1)
                {
                    cout << "Exit" << endl;
                    exit(-1);
                }

                char message[1024];
                identicon printiden = identicon();
                printiden.generate(name);
                strcpy(message, name.c_str());
                cout << "Message copied in" << endl;
                send(newsocket, message, strlen(message), 0);
                bRead = read(newsocket, buffer, 1024);
                cout << buffer << endl;
                string temp = buffer;
                if (temp == "connection-accepted")
                {

                    cout << "Connection accepted configuring as node" << endl;
                    remoteaddress = "node";
                }
                else if (temp == "not_host_node")
                {
                    cout << "Node was not host, cannot join cluster" << endl;
                }
                else
                {
                    cout << "Connection denied, shutting down..." << endl;
                    exit(-1);
                }
            }
            else
            {
                cout << "Ip size incorrect actual size: " << ip.size() << endl;
                exit(0);
            }
        }
    }
    return 0;
}