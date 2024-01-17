#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <vector>
#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <algorithm>

#include "../security/Util.h"
#include "../security/crypto.h"
#include "../packets/upload.h"
#include "../packets/wrapper.h"
#include "../tools/file.h"
#include "download.h"
#include "list.h"
#include <filesystem>

using namespace std;
const int PORT = 8080;
const int BUFFER_SIZE = 4096;
const int MAX_USERNAME_LENGTH = 5;

// Function to handle each connected client
int handleClient(int clientSocket, const std::vector<std::string> &userNames)
{
    int send_counter = 0;
    int rcv_counter = 0;

    vector<unsigned char> buffer(MAX_USERNAME_LENGTH);

    if (!receiveData(clientSocket, buffer, MAX_USERNAME_LENGTH))
    {
        cerr << "Error receiving username from client" << endl;
        close(clientSocket);
        return false;
    }

    std::string receivedUsername(buffer.begin(), buffer.end());
    std::cout << "Received username from client: " << receivedUsername << std::endl;

    // Check if username exists
    bool usernameExists = false;

    for (const auto &user : userNames)
    {
        if (receivedUsername == user)
        {
            usernameExists = true;
            break;
        }
    }
    // Send result back to client
    unsigned char resultChar = (usernameExists) ? '1' : '0';
    vector<unsigned char> resultMsg;
    resultMsg.push_back(resultChar);

    if (!sendData(clientSocket, resultMsg))
    {
        return false;
    }

    if (usernameExists)
    {
        // Load the server certificate from PEM file
        FILE *server_certificate_file = fopen("Cloud Strorage Server_cert.pem", "r");
        if (!server_certificate_file)
        {
            std::cerr << "Error loading server certificate" << std::endl;
            std::cout.flush();
            close(clientSocket);
            return 0;
        }

        X509 *serverCert = PEM_read_X509(server_certificate_file, NULL, NULL, NULL);
        fclose(server_certificate_file);

        if (!serverCert)
        {
            std::cerr << "Error reading server certificate" << std::endl;
            std::cout.flush();
            close(clientSocket);
            return 0;
        }

        // Send server certificate to client
        BIO *bio = BIO_new(BIO_s_mem());
        if (!bio)
        {
            std::cerr << "Error creating BIO" << std::endl;
            std::cout.flush();
            X509_free(serverCert);
            close(clientSocket);
            return 0;
        }

        int result = PEM_write_bio_X509(bio, serverCert);

        if (!result)
        {
            std::cerr << "[-] (CertificateStore) Failed to write the certificate in the BIO" << endl;
            std::cerr.flush();
            std::cout.flush();
            BIO_free(bio);
            return 0;
        }

        // Determine the size of the certificate
        int certSize = BIO_pending(bio);

        // Resize the vector to fit the certificate
        vector<unsigned char> certBuffer(certSize);
        int readBytes = BIO_read(bio, certBuffer.data(), certSize);
        if (readBytes <= 0)
            return 0;
        BIO_free(bio);

        std::cout << "size of certificate " << certSize << std::endl;

        // Send the certificate size to the client
        if (!sendNumber(clientSocket, certSize))
        {
            std::cerr << "Error sending the certificate size" << std::endl;
            return false;
        }

        // Send the certificate data to the client
        if (!sendData(clientSocket, certBuffer))
        {
            return 0;
        }

        // receive the client ECDH public key
        EVP_PKEY *deserializedClientKey;
        std::vector<unsigned char> sClientKey;
        size_t sClientKeyLength;

        if (!receiveEphemeralPublicKey(clientSocket, deserializedClientKey, sClientKey))
        {

            // Handle the case where receiving or deserialization failed
            std::cerr << "Failed to receive or deserialize the key" << std::endl;
            return 0;
        }
        sClientKeyLength = sClientKey.size();

        // Generate the elliptic curve diffie-Hellman keys for the client
        EVP_PKEY *ECDH_Keys;
        if (!(ECDH_Keys = ECDHKeyGeneration()))
        {
            cerr << "[SERVER] ECDH key generation failed" << endl;
            return 0;
        }

        // serialize the public key

        std::vector<unsigned char> sServerKey;

        if (!serializePubKey(ECDH_Keys, sServerKey))
        {
            cerr << "[SERVER] Serialization of public key failed" << endl;
            return 0;
        }
        size_t sServerKeyLength = sServerKey.size();

        // calculate (g^a)^b
        std::vector<unsigned char> sharedSecretKey;
        size_t sharedSecretLength;
        int derivationResult = deriveSharedSecret(ECDH_Keys, deserializedClientKey, sharedSecretKey);

        if (derivationResult == -1)
        {
            return 0;
        }
        sharedSecretLength = sharedSecretKey.size();
        // generate session key Sha256((g^a)^b)
        std::vector<unsigned char> digest;
        unsigned int digestlen;

        if (!computeSHA256Digest(sharedSecretKey, digest))
        {
            cerr << "[SERVER] Shared secret derivation failed" << endl;
            return 0;
        }
        digestlen = digest.size();

        // take first 128 of the the digest
        std::vector<unsigned char> session_key;
        if (!generateSessionKey(digest, session_key))
        {
            return 0;
        }

        // concatinate (g^b,g^a)
        // Concatenate the serialized keys
        vector<unsigned char> concatenatedKeys;
        int concatenatedkeysLength = sServerKeyLength + sClientKeyLength;
        concatenateKeys(sServerKey, sClientKey, concatenatedKeys);

        printf("Concatenated keys:\n");
        for (const auto &ch : concatenatedKeys)
        {
            printf("%02x", ch); // Assuming you want to print hexadecimal values
        }
        printf("\n");
        // Now concatenatedKeys contains the serialized form of both keys

        // read server private key
        // load server private key:
        EVP_PKEY *prvkey = nullptr;
        if (!loadPrivateKey("server_private_key.pem", prvkey))
        {
            return 0;
        }

        // create the digiatl signature <(g^a,g^b)>s using the server private key
        std::vector<unsigned char> signature;

        if (!generateDigitalSignature(concatenatedKeys, prvkey, signature))
        {
            return 0;
        }
        unsigned int signatureLength = signature.size();
        EVP_PKEY_free(prvkey);
        // Signature generation successful, print the signature
        std::cout << "Digital Signature:" << endl;
        BIO_dump_fp(stdout, reinterpret_cast<const char *>(signature.data()), signatureLength);

        // encrypt  {<(g^a,g^b)>s}k  using the session key
        std::vector<unsigned char> cipher_text;
        int cipher_size;
        std::vector<unsigned char> iv;

        if (!encryptTextAES(signature, session_key, cipher_text, iv))
        {
            return 0;
        }
        cipher_size = cipher_text.size();

        // send to the client: (g^b) ,(g^b) size, {<(g^a,g^b)>s}k, IV

        vector<unsigned char> sendBuffer;
        if (!serializeLoginMessageFromTheServer(sServerKey, cipher_text, iv, sendBuffer))
        {
            return 0;
        }

        if (!sendData(clientSocket, sendBuffer))
        {
            return 0;
        }

        // receive from the client:  {<(g^a,g^b)>c}k, IV

        size_t receiveBufferSize = Encrypted_Signature_Size + CBC_IV_Length;

        vector<unsigned char> receiveBuffer;
        receiveBuffer.resize(receiveBufferSize);

        if (!receiveData(clientSocket, receiveBuffer, receiveBufferSize))
        {
            std::cerr << "Error receiving  data" << std::endl;
            return false;
        }

        // Variables to store the deserialized components {<(g^a,g^b)>c}k, IV
        cipher_text.clear();
        iv.clear();

        // Call the deserialize function
        if (!deserializeLoginMessageFromTheClient(receiveBuffer, cipher_text, iv))
        {
            std::cerr << "Error deseiralizing the message" << std::endl;
            return 0;
        }
        // decrypt  {<(g^a,g^b)>c}k  using the session key
        vector<unsigned char> plaintext;
        int plaintextSize = 0;
        if (!decryptTextAES(cipher_text, session_key, iv, plaintext))
        {
            return 0;
        }
        plaintextSize = plaintext.size();
        // load user public key
        std::string publicKeyPath = "users/" + receivedUsername + "/public.pem";
        EVP_PKEY *client_public_key = nullptr;
        if (!loadPublicKey(publicKeyPath, client_public_key))
        {
            return 0;
        }

        if (!verifyDigitalSignature(concatenatedKeys, plaintext, client_public_key))
        {
            return 0;
        }

        // receive Wrapper packet message
        Buffer message_buff(Wrapper::getSize(MAX::initial_request_length));

        if (!receiveData(clientSocket, message_buff, Wrapper::getSize(MAX::initial_request_length)))
        {
            std::cerr << "Error receiving  data" << std::endl;
            return false;
        }
        // deserialize to extract payload in plaintext
        Wrapper wrapped_packet(session_key);

        if (!wrapped_packet.deserialize(message_buff))
        {
            std::cerr << "[SERVER] Wrapper packet wasn't deserialized correctly!" << endl;
            return false;
        }

        // Extract command code from payload
        uint8_t command_code;
        Buffer payload = wrapped_packet.getPayload();
        int packet_counter = wrapped_packet.getCounter();
        memcpy(&command_code, payload.data(), sizeof(uint8_t));

        cout << "Command Code: " << command_code << endl;

        if (RequestCodes::UPLOAD_REQ == command_code)
        {
            // ------ HERE WE START THE UPLOAD ROUTINE -----
            // ------ IN CASE OF ERROR WE EXIT TO LIST COMMANDS -----

            // Deserialize m1 general packet
            UploadM1 m1;
            m1.deserialize(payload);
            m1.print(); // for debug
            Buffer serialized_packet;

            // Check counter otherwise exit
            if (packet_counter != rcv_counter)
                return false;

            rcv_counter++; // TODO create a function for the increment counter

            // Check if the file exists
            string file_path = "../data/" + receivedUsername + "/" + (string)m1.file_name;
            UploadAck ack_packet;

            if (File::exists(file_path))
                ack_packet = UploadAck(0); // in case of success
            else
                ack_packet = UploadAck(1); // if file doesn't exist, error code : 1

            Wrapper ack_wrapper(session_key, send_counter, ack_packet.serialize());
            ack_wrapper.print();

            serialized_packet = ack_wrapper.serialize();
            sendData(clientSocket, serialized_packet);

            send_counter++;
        }

        // download routine
        if (RequestCodes::DOWNLOAD_REQ == command_code)
        {
            // ------ HERE WE START THE DOWNLOAD ROUTINE -----
            // ------ IN CASE OF ERROR WE EXIT TO LIST COMMANDS -----

            // Deserialize m1 general packet
            DownloadM1 m1;
            m1.deserialize(payload);
            m1.print(); // for debug
            Buffer serialized_packet;

            // Check counter otherwise exit
            if (packet_counter != rcv_counter)
                return false;

            rcv_counter++; // TODO create a function for the increment counter

            // Check if the file exists
            string filename = (string)m1.file_name;
            string file_path = "../data/" + receivedUsername + "/" + (string)m1.file_name;

            DownloadAck ack_packet;
            File file;
            uintmax_t file_size;

            if (File::exists(file_path))
            {
                // open the file denoted in path
                try
                {
                    file_size = filesystem::file_size(file_path);
                    ack_packet = DownloadAck(0, file_size);
                }
                catch (const std::exception &e)
                {
                    std::cerr << "[DOWNLOAD] " << e.what() << std::endl;
                    ack_packet = DownloadAck(1, 0); // error code : 1
                    return 0;
                }

                // check if file is not empty
                if (file_size == 0)
                {
                    cerr << "[DOWNLOAD] Cannot download empty files!" << endl;
                    ack_packet = DownloadAck(1, 0); // error code : 1
                    return 0;
                }
            }
            else
            {
                // file does not exist
                ack_packet = DownloadAck(1, 0); // error code : 1
            }

            Wrapper ack_wrapper(session_key, send_counter, ack_packet.serialize());
            ack_wrapper.print();

            serialized_packet = ack_wrapper.serialize();
            sendData(clientSocket, serialized_packet);

            send_counter++;
        }

        // list routine
        if (RequestCodes::LIST_REQ == command_code)
        {
            // ------ HERE WE START THE List ROUTINE -----
            // ------ IN CASE OF ERROR WE EXIT TO LIST COMMANDS -----

            // Deserialize m1 general packet
            ListM1 m1;
            m1.deserialize(payload);
            Buffer serialized_packet;

            // Check counter otherwise exit
            if (packet_counter != rcv_counter)
                return false;

            rcv_counter++; // TODO create a function for the increment counter

            ListM2 ack_size_packet;
            File file;
            uintmax_t file_size;
            string folder_path = "../data/" + receivedUsername;
            string fileNames;

            if (std::filesystem::exists(folder_path) && std::filesystem::is_directory(folder_path))
            {

                try
                {
                    fileNames = file.getFileNames(folder_path);
                    ack_size_packet = ListM2(0, fileNames.length());
                }
                catch (const std::exception &e)
                {
                    std::cerr << "[List] " << e.what() << std::endl;
                    ack_size_packet = ListM2(1, 0); // error code : 1
                    return 0;
                }
            }
            else
            {
                // folder does not exist
                ack_size_packet = ListM2(1, 0); // error code : 1
                return 0;
            }

            Wrapper ack_wrapper(session_key, send_counter, ack_size_packet.serialize());
            ack_wrapper.print();

            serialized_packet = ack_wrapper.serialize();
            sendData(clientSocket, serialized_packet);

            send_counter++;

            // send the list of files to the client

            ListM3 m3(fileNames.length());
            m3.setFileListData(fileNames.c_str());

            Wrapper wrapper(session_key, send_counter, m3.serialize());
            wrapper.print();

            serialized_packet = wrapper.serialize();
            sendData(clientSocket, serialized_packet);

            send_counter++;
        }

        // Clean up
        X509_free(serverCert);
        EVP_PKEY_free(deserializedClientKey);
    }
    else
    {
        // If username does not exist
    }

    // Close client socket
    close(clientSocket);

    return 1;
}

int main()
{
    // Create socket
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1)
    {
        std::cerr << "Error creating server socket" << std::endl;
        return -1;
    }

    // Bind socket to port
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

    if (bind(serverSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == -1)
    {
        std::cerr << "Error binding socket to port" << std::endl;
        close(serverSocket);
        return -1;
    }

    // Listen for connections
    if (listen(serverSocket, 5) == -1)
    {
        std::cerr << "Error listening for connections" << std::endl;
        close(serverSocket);
        return -1;
    }

    std::cout << "Server listening on port " << PORT << "..." << std::endl;

    // List of usernames
    vector<std::string> userNames = {"user1", "user2", "user3"};

    while (true)
    {
        // Accept connection
        sockaddr_in clientAddr{};
        socklen_t clientAddrLen = sizeof(clientAddr);
        int clientSocket = accept(serverSocket, (struct sockaddr *)&clientAddr, &clientAddrLen);
        if (clientSocket == -1)
        {
            std::cerr << "Error accepting connection" << std::endl;
            continue;
        }

        // Create a new thread for each connected client
        std::thread t1(&handleClient, clientSocket, std::cref(userNames));
        t1.detach();
    }

    // Close server socket (This part will never be reached in this example)
    close(serverSocket);

    return 0;
}
