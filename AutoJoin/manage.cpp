#include "autojoin.cpp"
#include "zeroconf/browse-services.c"
#include "zeroconf/client-publish-service.c"
#include <unistd.h>

#include <memory>
using std::unique_ptr;

#include <stdio.h>
#include <iostream>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <exception>
extern char buf[];

// bool generate_key()
// {
//     size_t pri_len; // Length of private key
//     size_t pub_len; // Length of public key
//     char *pri_key;  // Private key in PEM
//     char *pub_key;  // Public key in PEM

//     int ret = 0;
//     RSA *r = NULL;
//     BIGNUM *bne = NULL;
//     BIO *bp_public = NULL, *bp_private = NULL;
//     int bits = 2048;
//     unsigned long e = RSA_F4;

//     EVP_PKEY *evp_pbkey = NULL;
//     EVP_PKEY *evp_pkey = NULL;

//     BIO *pbkeybio = NULL;
//     BIO *pkeybio = NULL;

//     // 1. generate rsa key
//     bne = BN_new();
//     ret = BN_set_word(bne, e);
//     if (ret != 1)
//     {
//         goto free_all;
//     }

//     r = RSA_new();
//     ret = RSA_generate_key_ex(r, bits, bne, NULL);
//     if (ret != 1)
//     {
//         goto free_all;
//     }

//     // 2. save public key
//     //bp_public = BIO_new_file("public.pem", "w+");
//     bp_public = BIO_new(BIO_s_mem());
//     ret = PEM_write_bio_RSAPublicKey(bp_public, r);
//     if (ret != 1)
//     {
//         goto free_all;
//     }

//     // 3. save private key
//     //bp_private = BIO_new_file("private.pem", "w+");
//     bp_private = BIO_new(BIO_s_mem());
//     ret = PEM_write_bio_RSAPrivateKey(bp_private, r, NULL, NULL, 0, NULL, NULL);

//     //4. Get the keys are PEM formatted strings
//     pri_len = BIO_pending(bp_private);
//     pub_len = BIO_pending(bp_public);

//     pri_key = (char *)malloc(pri_len + 1);
//     pub_key = (char *)malloc(pub_len + 1);

//     BIO_read(bp_private, pri_key, pri_len);
//     BIO_read(bp_public, pub_key, pub_len);

//     pri_key[pri_len] = '\0';
//     pub_key[pub_len] = '\0';

//     printf("\n%s\n%s\n", pri_key, pub_key);

//     //verify if you are able to re-construct the keys
//     pbkeybio = BIO_new_mem_buf((void *)pub_key, -1);
//     if (pbkeybio == NULL)
//     {
//         return -1;
//     }
//     evp_pbkey = PEM_read_bio_PUBKEY(pbkeybio, &evp_pbkey, NULL, NULL);
//     if (evp_pbkey == NULL)
//     {
//         char buffer[120];
//         ERR_error_string(ERR_get_error(), buffer);
//         printf("Error reading public key:%s\n", buffer);
//     }

//     pkeybio = BIO_new_mem_buf((void *)pri_key, -1);
//     if (pkeybio == NULL)
//     {
//         return -1;
//     }
//     evp_pkey = PEM_read_bio_PrivateKey(pkeybio, &evp_pkey, NULL, NULL);
//     if (evp_pbkey == NULL)
//     {
//         char buffer[120];
//         ERR_error_string(ERR_get_error(), buffer);
//         printf("Error reading private key:%s\n", buffer);
//     }

//     BIO_free(pbkeybio);
//     BIO_free(pkeybio);

//     // 4. free

// free_all:

//     BIO_free_all(bp_public);
//     BIO_free_all(bp_private);
//     RSA_free(r);
//     BN_free(bne);

//     return (ret == 1);
// }

#include <openssl/rsa.h>
#include <openssl/pem.h>
char *generate_key()
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

    // printf("%s", pem_key);

    // BIO_free_all(bio);
    // RSA_free(rsa);
    // free(pem_key);
    return pem_key;
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
        // address = argv[2];
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
        // cout << address << std::endl;
        // address = "abc";
        char name_as_c[80];
        // strcpy(name_as_c, name.c_str());
        cout << mode << std::endl;

        browse(PROTOCOL_TYPE, "host");
        // browse();

        // cout << "buf: " << buf << std::endl;
        mode = (std::string)buf + ";777";
        cout << mode << std::endl;

        char *key;
        key = generate_key();
        printf("key: %s\n", key);

        name = (std::string)key;
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

    // key pair
    // public
    // private

    autojoinprogram(name, "777", mode, AF_INET);
}