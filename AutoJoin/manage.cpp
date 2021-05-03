#include "autojoin.cpp"
#include "zeroconf/browse-services.c"
#include "zeroconf/client-publish-service.c"
#include <unistd.h>

#include <memory>

#include <stdio.h>
#include <iostream>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <exception>

#include <sstream>

extern char buf[];

std::string string_to_hex(char *data)
{
    int data_length = std::strlen(data);
    std::stringstream ss;
    for (int i = 0; i < data_length; ++i)
        ss << std::hex << (int)data[i];
    std::string hex_str = ss.str();
    return hex_str;
}

std::string generate_key()
{
    const int kBits = 1024;
    const int kExp = 3;

    int keylen;
    char *pem_key;

    RSA *rsa = RSA_generate_key(kBits, kExp, 0, 0);

    /* To get the C-string PEM form: */
    BIO *bio = BIO_new(BIO_s_mem());
    PEM_write_bio_RSAPrivateKey(bio, rsa, NULL, NULL, 0, NULL, NULL);

    keylen = BIO_pending(bio);
    pem_key = calloc(keylen + 1, 1); /* Null-terminate */
    BIO_read(bio, pem_key, keylen);

    cout << "bio: " << bio << endl;

    cout << "pem key: " << pem_key << endl;

    std::string output = string_to_hex(pem_key);
    BIO_free_all(bio);
    RSA_free(rsa);
    free(pem_key);
    return output;
}

int main(int argc, char **argv)
{
    std::string name = "";
    std::string address = "";
    std::string mode = "";
    std::string protocol = "";

    AvahiProtocol PROTOCOL_TYPE = AVAHI_PROTO_UNSPEC;

    if (argc >= 3)
    {
        name = argv[1];
        mode = argv[2];
        protocol = argv[3];
    }
    else
    {
        printf("Please enter all 3 arguments (name of node), (connection mode), protocol(4/6)\n");
        printf("Ex. ./manage node auto 4\n");
        return -1;
    }

    if (protocol == (std::string) "6")
    {
        PROTOCOL_TYPE = AVAHI_PROTO_INET6;
    }
    else
    {
        PROTOCOL_TYPE = AVAHI_PROTO_INET;
    }

    if (mode == (std::string) "auto")
    {
        char name_as_c[80];
        cout << mode << std::endl;

        browse(PROTOCOL_TYPE, "host");

        mode = (std::string)buf + ";777";
        cout << mode << std::endl;

        name = generate_key();
        printf("key: %s\n", name);
    }
    else if (mode == (std::string) "host")
    {
        int pid = fork();
        if (pid == 0)
        {
            usleep(3000 * 1000);
        }
        else
        {
            publish(true);
        }
    }

    cout << "name: " << name << " mode: " << mode << std::endl;

    autojoinprogram(name, "7777", mode, AF_INET);
}