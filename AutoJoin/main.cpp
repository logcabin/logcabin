#include "autojoin.cpp"

// ./program name portnum remoteip/host

int main(int argc, char** argv)
{
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    int newsocket, bRead;
    char buffer[1024] = {0};

    if (argc == 4)
    {
        ++argv;
        string name = *argv;
        cout << name << endl;
        ++argv;
        string port = *argv;
        cout << port << endl;
        ++argv;
        string remoteaddress = *argv;
        cout << remoteaddress << endl;

        autojoin autojoinobject = autojoin();
        cout << "Parameters accepted" << endl;

        if(remoteaddress == "host" || remoteaddress == "Host")
        {

            cout << "Node set as host node" << endl;
            int hostsocket = autojoinobject.createHostConnection(stoi(port), AF_INET, address);

            if (listen(hostsocket, 3) < 0) 
            { 
            cout << "Node failure to listen on port" << endl;
            perror("listen"); 
            exit(EXIT_FAILURE); 
            } 
            if ((newsocket = accept(hostsocket, (struct sockaddr *)&address, (socklen_t*)&addrlen))<0) 
            { 
                cout << "Node failure to accept connection" << endl;
                perror("accept"); 
                exit(EXIT_FAILURE); 
            }
            cout << "Reading transmition from socket" << endl;
            bRead = read(newsocket, buffer, 1024);

            cout << buffer << endl;
            cout << "Accept connection (y or n)" << endl;

            string option = "";
            while (option != "y" && option != "n")
            {
                cin >> option;
                if(option != "y" && option != "n")
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

            send(newsocket , message , strlen(message) , 0);
        }
        else
        {
            cout << "Node configured as client to auto join" << endl;
            vector<string> ip = autojoinobject.parseString(remoteaddress, ':');
            cout << "Address: " + ip[0] << endl;
            cout << "Port: " + ip[1] << endl;

            if(ip.size() == 2)
            {
                newsocket = autojoinobject.createClientConnection(ip[0],stoi(ip[1]), AF_INET, address);
                cout << "Setting up connection for join from node " + name << endl;

                if(newsocket == -1)
                {
                    cout << "Exit" << endl;
                    exit(-1);
                }

                char message[1024];
                string text = "Requesting Connection for " + name;
                strcpy(message, text.c_str());
                cout << "Message copied in" << endl;
                send(newsocket , message , strlen(message) , 0 );
                bRead = read(newsocket, buffer, 1024);
                cout << buffer << endl;
            }
            else
            {
                cout << "Ip size incorrect actual size: " << ip.size() << endl;
            }
        }

        return 0;
    }

    else
    {
        cout << "Incorrect numeber of arguments" << endl;
        return -1;
    }
}