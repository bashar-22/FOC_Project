#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <arpa/inet.h>
#include <unistd.h>
#include <vector>
#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "./Diffie-Hellman.h"
#include "./Util.h"
#include <limits>
#include <openssl/rand.h>
#include "../packets/constants.h"

using namespace std;
typedef std::vector<unsigned char> Buffer;

size_t calLengthLoginMessageFromTheServer()
{
    return Max_Ephemral_Public_Key_Size + sizeof(int) + Encrypted_Signature_Size + CBC_IV_Length;
}

bool receiveEphemeralPublicKey(int clientSocket, EVP_PKEY *&deserializedKey, Buffer &serializedKey)
{

    // Receive the certificate size
    size_t serializedKeyLength;

    if (!receiveSize(clientSocket, serializedKeyLength))
    {
        std::cerr << "Error receiving Key size" << std::endl;
        return false;
    }
    if (serializedKeyLength > Max_Ephemral_Public_Key_Size)
    {
        std::cerr << "Key size exceeds the max size" << std::endl;
        return false;
    }
    serializedKey.resize(serializedKeyLength);

    if (!receiveData(clientSocket, serializedKey))
    {
        std::cerr << "Error receiving key data" << std::endl;
        return false;
    }
    deserializedKey = deserializePublicKey(serializedKey);
    if (deserializedKey == NULL)
    {
        std::cerr << "Error receiving serializing data" << std::endl;
        return false;
    }
    return true;
}

bool generateDigitalSignature(Buffer &data, EVP_PKEY *privateKey, Buffer &signature)
{
    const EVP_MD *md = EVP_sha256();
    size_t dataLength = data.size();
    unsigned int signatureLength;

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx)
    {
        // Handle error
        cerr << "Error: Creating Context Failed"
             << endl;
        return false;
    }

    // Initialize signature vector with the expected size
    signature.resize(EVP_PKEY_size(privateKey));
    int ret;
    ret = EVP_SignInit(ctx, md);
    if (ret == 0)
    {
        cerr << "Error: EVP_SignInit returned " << ret << endl;
        EVP_MD_CTX_free(ctx);
        return false;
    }
    ret = EVP_SignUpdate(ctx, data.data(), dataLength);
    if (ret == 0)
    {
        cerr << "Error: EVP_SignUpdate returned " << ret << endl;
        EVP_MD_CTX_free(ctx);
        return false;
    }
    ret = EVP_SignFinal(ctx, signature.data(), &signatureLength, privateKey);

    if (ret == 0)
    {
        cerr << "Error: EVP_SignFinal returned " << ret << endl;
        EVP_MD_CTX_free(ctx);
        return false;
    }

    return true;
}

bool verifyDigitalSignature(Buffer &data, Buffer &signature, EVP_PKEY *publicKey)
{
    const EVP_MD *md = EVP_sha256();
    unsigned int signatureLength;

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx)
    {
        std::cerr << "Error: Faild creating context for verification " << std::endl;
        return false;
    }

    int ret;
    ret = EVP_VerifyInit(ctx, md);
    if (ret == 0)
    {
        std::cerr << "Error: EVP_VerifyInit returned " << ret << std::endl;
        return false;
    }
    ret = EVP_VerifyUpdate(ctx, data.data(), data.size());
    if (ret == 0)
    {
        std::cerr << "Error: EVP_VerifyUpdate returned " << ret << std::endl;
        return false;
    }
    ret = EVP_VerifyFinal(ctx, signature.data(), signature.size(), publicKey);

    if (ret != 1)
    {
        std::cerr << "Error: Signature verification failed\n"
                  << std::endl;
        return false;
    }

    EVP_MD_CTX_free(ctx);

    return true;
}

bool computeSHA256Digest(Buffer &data, Buffer &digest)
{
    size_t dataLength = data.size();
    unsigned int digestLength;

    // Create and init context
    EVP_MD_CTX *Hctx = EVP_MD_CTX_new();

    if (!Hctx)
    {
        fprintf(stderr, "Error creating EVP_MD_CTX\n");
        return false;
    }
    digest.resize(EVP_MD_size(EVP_sha256()));

    // Initialize, Update (only once), and finalize digest
    if (EVP_DigestInit(Hctx, EVP_sha256()) != 1 ||
        EVP_DigestUpdate(Hctx, data.data(), dataLength) != 1 ||
        EVP_DigestFinal(Hctx, digest.data(), &digestLength) != 1)
    {
        fprintf(stderr, "Error computing SHA-256 digest\n");
        EVP_MD_CTX_free(Hctx);
        return false;
    }

    //  free context
    EVP_MD_CTX_free(Hctx);

    return true;
}

void serializeLoginMessageFromTheServer(Buffer &serializedServerEphemralKey,
                                        Buffer &cipher_text, Buffer &iv, Buffer &sendBuffer)
{
    int serializedServerrEphemralKeyLength = serializedServerEphemralKey.size();

    // Calculate the total length of the data to be sent
    size_t totalLength = calLengthLoginMessageFromTheServer();

    // Resize the sendBuffer to hold the concatenated data
    sendBuffer.resize(totalLength);

    // Copy serializedServerKey to the buffer
    std::memcpy(sendBuffer.data(), serializedServerEphemralKey.data(), Max_Ephemral_Public_Key_Size);

    // Copy serializedServerKeyLength to the buffer
    std::memcpy(sendBuffer.data() + Max_Ephemral_Public_Key_Size, &serializedServerrEphemralKeyLength, sizeof(int));

    // Copy cipher_text to the buffer
    std::memcpy(sendBuffer.data() + Max_Ephemral_Public_Key_Size + sizeof(int), cipher_text.data(), Encrypted_Signature_Size);

    // Copy iv to the buffer
    std::memcpy(sendBuffer.data() + Max_Ephemral_Public_Key_Size + sizeof(int) + Encrypted_Signature_Size, iv.data(), CBC_IV_Length);
}

bool deserializeLoginMessageFromTheServer(Buffer &receivedBuffer,
                                          Buffer &serializedServerEphemralKey,
                                          Buffer &cipher_text, Buffer &iv)
{

    // Allocate memory for the individual components
    Buffer maxSerializedServerEphemralKey(Max_Ephemral_Public_Key_Size);

    cipher_text.resize(Encrypted_Signature_Size);
    iv.resize(CBC_IV_Length);

    // Copy data from the received buffer to the individual components
    std::memcpy(maxSerializedServerEphemralKey.data(), receivedBuffer.data(), Max_Ephemral_Public_Key_Size);

    int serializedServerrEphemralKeyLength;
    std::memcpy(&serializedServerrEphemralKeyLength, receivedBuffer.data() + Max_Ephemral_Public_Key_Size, sizeof(int));

    std::memcpy(cipher_text.data(), receivedBuffer.data() + Max_Ephemral_Public_Key_Size + sizeof(int), Encrypted_Signature_Size);
    std::memcpy(iv.data(), receivedBuffer.data() + Max_Ephemral_Public_Key_Size + sizeof(int) + Encrypted_Signature_Size, CBC_IV_Length);

    // copy portion to the serialized ephemral key
    // Create a new vector with the specified length and copy the data

    serializedServerEphemralKey.insert(serializedServerEphemralKey.begin(), maxSerializedServerEphemralKey.begin(),
                                       maxSerializedServerEphemralKey.begin() + serializedServerrEphemralKeyLength);

    return true;
}

bool serializeLoginMessageFromTheClient(Buffer &cipher_text, Buffer &iv, Buffer &sendBuffer)
{
    // Calculate the total length of the data to be sent
    size_t totalLength = Encrypted_Signature_Size + CBC_IV_Length;

    // Resize the sendBuffer to hold the concatenated data
    sendBuffer.resize(totalLength);

    // Copy cipher_text to the buffer
    std::memcpy(sendBuffer.data(), cipher_text.data(), Encrypted_Signature_Size);

    // Copy iv to the buffer
    std::memcpy(sendBuffer.data() + Encrypted_Signature_Size, iv.data(), CBC_IV_Length);

    return true;
}

void deserializeLoginMessageFromTheClient(Buffer &receivedBuffer,
                                          Buffer &cipher_text, Buffer &iv)
{

    // Resize cipher_text and iv vectors to hold the deserialized data
    cipher_text.resize(Encrypted_Signature_Size);
    iv.resize(CBC_IV_Length);

    // Copy cipher_text from the buffer
    std::memcpy(cipher_text.data(), receivedBuffer.data(), Encrypted_Signature_Size);

    // Copy iv from the buffer
    std::memcpy(iv.data(), receivedBuffer.data() + Encrypted_Signature_Size, CBC_IV_Length);
}

bool loadPrivateKey(std::string privateKeyPath, EVP_PKEY *&privateKey, string pem_pass)
{
    FILE *prvkey_file = fopen(privateKeyPath.c_str(), "r");

    if (!prvkey_file)
    {
        std::cerr << "Error: Cannot open private key file: " << privateKeyPath << "\n";
        return false;
    }

    privateKey = PEM_read_PrivateKey(prvkey_file, NULL, NULL, (void *)pem_pass.c_str());
    fclose(prvkey_file);

    if (!privateKey)
    {
        std::cerr << "Error: PEM_read_PrivateKey returned NULL\n";
        return false;
    }

    return true;
}

bool loadPublicKey(const std::string publicKeyPath, EVP_PKEY *&publicKey)
{
    FILE *file = fopen(publicKeyPath.c_str(), "r");
    if (!file)
    {
        fprintf(stderr, "Error opening file: %s\n", publicKeyPath.c_str());
        return false;
    }

    publicKey = PEM_read_PUBKEY(file, nullptr, nullptr, nullptr);

    fclose(file);

    if (!publicKey)
    {
        fprintf(stderr, "Error reading public key from PEM file\n");
        return false;
    }

    return true;
}

bool receiveData(int socket, Buffer &buffer)
{
    try
    {
        ssize_t bytesRead = recv(socket, buffer.data(), buffer.size(), MSG_WAITALL);

        if (bytesRead <= 0)
        {
            std::cerr << "Error receiving data" << std::endl;
            return false;
        }

        return true;
    }

    catch (const std::exception &e)
    {
        // Catch the exception and handle it
        std::cerr << "Exception: " << e.what() << std::endl;
        return false;
    }
}

bool sendData(int socket, Buffer &data)
{

    try
    {
        ssize_t bytesSent = send(socket, data.data(), data.size(), MSG_NOSIGNAL);

        if (bytesSent == -1)
        {

            if (errno == EPIPE)
            {
                std::cerr << "Error sending data due to a connection issue" << std::endl;

                return false;
            }
            std::cerr << "Error sending data " << std::endl;

            return false;
        }

        return true;
    }
    catch (const std::exception &e)
    {
        // Catch the exception and handle it
        std::cerr << "Exception: " << e.what() << std::endl;
        return false;
    }
}

bool receiveSize(int socket, size_t &number)
{

    try
    {
        ssize_t bytesReceived = recv(socket, &number, sizeof(number), MSG_WAITALL);

        if (bytesReceived <= 0)
        {
            std::cerr << "Error receiving the number " << std::endl;
            return false;
        }
        return true;
    }

    catch (const std::exception &e)
    {
        // Catch the exception and handle it
        std::cerr << "Exception: " << e.what() << std::endl;
        return false;
    }
}

bool sendSize(int socket, size_t number)
{
    try
    {
        ssize_t bytesSent = send(socket, &number, sizeof(number), MSG_NOSIGNAL);

        if (bytesSent != sizeof(number) || bytesSent == -1)
        {

            if (errno == EPIPE)
            {
                std::cerr << "Error sending number due to a connection issue" << std::endl;

                return false;
            }
            std::cerr << "Error sending the number " << std::endl;

            return false;
        }

        return true;
    }
    catch (const std::exception &e)
    {
        // Catch the exception and handle it
        std::cerr << "Exception: " << e.what() << std::endl;
        return false;
    }
}

void clear_vec(Buffer &v)
{
    if (!v.empty())
    {
        v.assign(v.size(), '0');
        v.clear();
    }
}
int incrementCounter(int counter)
{
    if (counter == MAX::counter_max_value)
        return -1; // reinitiate session
    else
        return ++counter;
}