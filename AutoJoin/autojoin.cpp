#include "autojoin.h"

vector<string> autojoin::parseString(string input, char delimeter)
{
    vector<string> returnlist = vector<string>();
    int length = (int)input.length();
    string temp = "";

    for(int index = 0; index < length; index++)
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

    if(temp != "")
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
    sock.sin_port = htons( port ); 
       
    // Forcefully attaching socket to the port
    if (bind(host_fd, (struct sockaddr *)&sock, sizeof(sock))<0) 
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
    if(inet_pton(mode, address, &socketadd.sin_addr)<=0)  
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