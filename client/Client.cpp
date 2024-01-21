#include <iostream>
#include <cstring>
#include <arpa/inet.h>
#include <unistd.h>
#include <openssl/bio.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <limits>
#include "../security/Util.h"
#include "../security/crypto.h"
#include "../security/Diffie-Hellman.h"
#include "../packets/constants.h"
#include "Client.h"

#include "../tools/file.h"
#include "../packets/upload.h"
#include "../packets/wrapper.h"
#include "../packets/download.h"
#include "../packets/list.h"
#include "rename.h"
#include "delete.h"
#include "../packets/logout.h"

using namespace std;
Client::Client() {}

bool Client::connectToServer()
{

    // Initialize members and create a socket
    clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket == -1)
    {
        std::cerr << "Error creating client socket" << std::endl;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr) <= 0)
    {
        std::cerr << "Invalid address/Address not supported" << std::endl;
        // Handle error
        return false;
    }

    if (connect(clientSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == -1)
    {
        std::cerr << "Error connecting to server" << std::endl;
        // Handle error
        close(clientSocket);
        return false;
    }

    return true;
}

bool Client::sendUsername()
{
    std::cout << "Enter your username (up to " + to_string(MAX::username_length) + " characters): ";
    std::getline(std::cin, username);

    // make sure input was valid and non null
    if (!cin || username.empty() || username.length() > MAX::username_length)
    {
        cerr << "[Login] Invalid username input" << endl;
        std::cin.clear(); // put us back in 'normal' operation mode
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        return false;
    }

    // Send username size to the server
    size_t usernameLength = username.size();
    if (!sendNumber(clientSocket, usernameLength))
    {
        std::cerr << "Error sending the username length" << std::endl;
        return false;
    }

    // Convert the username string to a vector of unsigned char
    std::vector<unsigned char> usernameData(username.begin(), username.end());

    // Call sendData with the username data
    if (!sendData(clientSocket, usernameData))
    {
        // Handle error if sendData fails
        return false;
    }

    return true;
}

bool Client::receiveServerResponse()
{

    size_t server_response;
    if (!receiveNumber(clientSocket, server_response))
    {
        // Handle the error if receiving data fails
        cerr << "Error receiving the response from server" << endl;
        return false;
    }

    std::cout << "Server response: " << server_response << std::endl;

    // Implement the logic to handle the server response

    if (server_response == 0)
    {
        std::cerr << "User does not exist" << std::endl;
        return false;
    }
    // User exists, read the password of private key of the user

    // Read password from console
    std::cout << "Enter the password of the private key: " << endl;
    std::getline(std::cin, password);

    // make sure input was valid and non null
    if (!cin || password.empty() || password.length() > MAX::passowrd_length)
    {
        cerr << "[Login] Invalid password input" << endl;
        std::cin.clear(); // put us back in 'normal' operation mode
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        return false;
    }
    // read user private key
    std::string privateKeyPath = "users/" + username + "/key.pem";
    EVP_PKEY *prvkey = nullptr;

    if (!loadPrivateKey(privateKeyPath, prvkey, password))
    {
        cerr << "[Login] Invalid password for the private key" << endl;

        return false;
    }

    // User exists, proceed to receive the server certificate
    X509 *serverCert = nullptr;
    if (receiveServerCertificate(serverCert))
    {
        // Use the server certificate as needed
        std::cout << "Received server certificate successfully" << std::endl;

        const char *caCertFile = "Cloud Storage CA_cert.pem";
        const char *crlFile = "Cloud Storage CA_crl.pem";
        // Load CA certificate
        X509 *caCert = nullptr;
        FILE *caCertFilePtr = fopen(caCertFile, "r");
        if (!caCertFilePtr)
        {
            perror("Error opening CA certificate file");
            return 0;
        }

        caCert = PEM_read_X509(caCertFilePtr, nullptr, nullptr, nullptr);
        fclose(caCertFilePtr);

        if (!caCert)
        {
            ERR_print_errors_fp(stderr);
            return 0;
        }

        // Load CRL
        X509_CRL *crl = nullptr;
        FILE *crlFilePtr = fopen(crlFile, "r");
        if (!crlFilePtr)
        {
            perror("Error opening CRL file");
            X509_free(caCert);
            return 0;
        }

        crl = PEM_read_X509_CRL(crlFilePtr, nullptr, nullptr, nullptr);
        fclose(crlFilePtr);

        if (!crl)
        {
            ERR_print_errors_fp(stderr);
            X509_free(caCert);
            return 0;
        }
        bool verified = verifyServerCertificate(caCert, crl, serverCert);
        if (verified)
        {
            std::cout << "Server certificate verification successful." << std::endl;
        }
        else
        {
            std::cerr << "Server certificate verification failed." << std::endl;
            return 0;
        }

        // Generate the elliptic curve diffie-Hellman keys for the client
        EVP_PKEY *ECDH_Keys;
        if (!(ECDH_Keys = ECDHKeyGeneration()))
        {
            cerr << "[CLIENT] ECDH key generation failed" << endl;
            return 0;
        }

        vector<unsigned char> sClientKey;

        if (!serializePubKey(ECDH_Keys, sClientKey))
        {
            cerr << "[CLIENT] Serialization of public key failed" << endl;
            return 0;
        }

        // Use the serialized key as needed

        // Send the key size to the server
        size_t sClientKeyLength = sClientKey.size();
        if (!sendNumber(clientSocket, sClientKeyLength))
        {
            std::cerr << "Error sending the key size" << std::endl;
            return false;
        }
        // send the DH public key to the server
        if (!sendData(clientSocket, sClientKey))
        {
            return false;
        }

        // receive from the server: (g^b) ,(g^b) size, {<(g^a,g^b)>s}k, IV

        size_t receiveBufferSize = calLengthLoginMessageFromTheServer();
        vector<unsigned char> receiveBuffer;
        receiveBuffer.resize(receiveBufferSize);

        if (!receiveData(clientSocket, receiveBuffer, receiveBufferSize))
        {
            std::cerr << "Error receiving certificate data" << std::endl;
            return false;
        }

        // Variables to store the deserialized components (g^b) ,(g^b) size, {<(g^a,g^b)>s}k, IV
        vector<unsigned char> sServerEphemeralKey;
        int sServerEphemeralKeyLength = 0;
        vector<unsigned char> cipher_text;
        vector<unsigned char> iv;

        // Call the deserialize function
        if (!deserializeLoginMessageFromTheServer(receiveBuffer, sServerEphemeralKey, cipher_text, iv))
        {
            std::cerr << "Error deseiralizing the message" << std::endl;
            return 0;
        }
        sServerEphemeralKeyLength = sServerEphemeralKey.size();
        if (sServerEphemeralKeyLength > Max_Ephemral_Public_Key_Size)
        {
            cerr << "Key size exceeds the max size" << std::endl;
            return 0;
        }

        EVP_PKEY *deserializedServerEphemeralKey = deserializePublicKey(sServerEphemeralKey);
        if (deserializedServerEphemeralKey == NULL)
        {
            cerr << "Error receiving serializing data" << std::endl;
            return 0;
        }

        // calculate (g^a)^b
        vector<unsigned char> sharedSecretKey;
        size_t sharedSecretLength;
        int derivationResult = deriveSharedSecret(ECDH_Keys, deserializedServerEphemeralKey, sharedSecretKey);

        if (derivationResult == -1)
        {
            return 0;
        }
        sharedSecretLength = sharedSecretKey.size();
        // generate session key Sha256((g^a)^b)
        vector<unsigned char> digest;
        unsigned int digestlen;

        if (!computeSHA256Digest(sharedSecretKey, digest))
        {
            return 0;
        }
        digestlen = digest.size();

        // take first 128 of the the digest
        if (!generateSessionKey(digest, session_key))
        {
            return 0;
        }

        // concatinate (g^b,g^a)
        // Concatenate the serialized keys

        vector<unsigned char> concatenatedKeys;
        int concatenatedkeysLength = sServerEphemeralKeyLength + sClientKeyLength;
        concatenateKeys(sServerEphemeralKey, sClientKey, concatenatedKeys);

        printf("Concatenated keys:\n");
        for (const auto &ch : concatenatedKeys)
        {
            printf("%02x", ch); // Assuming you want to print hexadecimal values
        }
        printf("\n");

        // Now concatenatedKeys contains the serialized form of both keys

        // decrypt  {<(g^a,g^b)>s}k  using the session key
        vector<unsigned char> plaintext;
        int plaintextSize = 0;
        if (!decryptTextAES(cipher_text, session_key, iv, plaintext))
        {
            return 0;
        }
        plaintextSize = plaintext.size();

        // verify the <(g^a,g^b)>s
        EVP_PKEY *server_public_key = X509_get_pubkey(serverCert);

        if (!verifyDigitalSignature(concatenatedKeys, plaintext, server_public_key))
        {
            return 0;
        }

        // create the digiatl signature <(g^a,g^b)>c using the client private key
        vector<unsigned char> signature;

        if (!generateDigitalSignature(concatenatedKeys, prvkey, signature))
        {
            return 0;
        }
        unsigned int signatureLength = signature.size();
        EVP_PKEY_free(prvkey);

        // encrypt  {<(g^a,g^b)>c}k  using the session key
        cipher_text.clear();
        int cipher_size;
        iv.clear();

        if (!encryptTextAES(signature, session_key, cipher_text, iv))
        {
            return 0;
        }
        cipher_size = cipher_text.size();

        //  send to the server: {<(g^a,g^b)>c}k, IV
        vector<unsigned char> sendBuffer;
        if (!serializeLoginMessageFromTheClient(cipher_text, iv, sendBuffer))
        {
            return 0;
        }
        if (!sendData(clientSocket, sendBuffer))
        {
            // Handle error if sendData fails
            return false;
        }

        // Cleanup OpenSSL (if not done already)
        EVP_cleanup();

        // Clean up
        X509_free(serverCert);
    }
    else
    {
        std::cerr << "Error receiving server certificate" << std::endl;
    }

    return true;
}

bool Client::receiveServerCertificate(X509 *&serverCert)
{
    // Implement the logic to receive the server certificate

    // Receive the certificate size
    size_t certSize;
    if (!receiveNumber(clientSocket, certSize))
    {
        // Handle the error if receiving data fails
        return false;
    }
    if (certSize > MAX_CERTIFICATE_SIZE)
    {
        std::cerr << "Certificate size exceeds the max size" << std::endl;
        return false;
    }

    // Receive the certificate data
    vector<unsigned char> certBuffer(certSize);
    if (!receiveData(clientSocket, certBuffer, certSize))
    {
        std::cerr << "Error receiving certificate data" << std::endl;
        return false;
    }
    std::cout << "Certificate Buffer: ";
    for (char i : certBuffer)
        std::cout << i << ' ';

    // Create a BIO from the received data
    BIO *bio = BIO_new_mem_buf(certBuffer.data(), certSize);
    if (!bio)
    {
        std::cerr << "Error creating BIO from certificate data" << std::endl;
        return false;
    }

    // Read the X509 certificate from the BIO

    serverCert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
    if (!serverCert)
    {
        std::cerr << "Error reading X509 certificate from BIO" << std::endl;

        ERR_print_errors_fp(stderr); // Print OpenSSL error information
        BIO_free(bio);

        return false;
    }

    // Print serial number
    ASN1_INTEGER *serialNumber = X509_get_serialNumber(serverCert);
    BIGNUM *bnSerial = ASN1_INTEGER_to_BN(serialNumber, nullptr);
    char *serialHex = BN_bn2hex(bnSerial);

    cout << "Serial Number: " << serialHex << endl;
    OPENSSL_free(serialHex);
    BN_free(bnSerial);
    // Clean up
    BIO_free(bio);

    return true;
}

bool Client::verifyServerCertificate(X509 *caCert, X509_CRL *crl, X509 *serverCert)
{

    // Create a store and add the CA certificate and CRL to it
    X509_STORE *store = X509_STORE_new();
    X509_STORE_add_cert(store, caCert);
    X509_STORE_add_crl(store, crl);

    // Set the flags to check against the CRL
    X509_STORE_set_flags(store, X509_V_FLAG_CRL_CHECK | X509_V_FLAG_CRL_CHECK_ALL);

    // Create a context and set the store
    X509_STORE_CTX *ctx = X509_STORE_CTX_new();
    X509_STORE_CTX_init(ctx, store, serverCert, nullptr);

    // Perform the verification
    int ret = X509_verify_cert(ctx);
    if (ret != 1)
    {
        fprintf(stderr, "Certificate verification failed\n");
        ERR_print_errors_fp(stderr);
    }
    else
    {
        printf("Certificate verification succeeded\n");
    }

    // Clean up
    X509_STORE_CTX_free(ctx);
    X509_STORE_free(store);

    X509_free(caCert);
    X509_CRL_free(crl);

    return ret == 1;
}

void Client::performClientJob()
{
    int rcv_counter = 0;

    if (!connectToServer())
    {
        return;
    }
    if (!sendUsername())
    {
        return;
    }
    if (!receiveServerResponse())
    {
        return;
    }

    // end login phase
    // upload_file();
    download_file();
    // list_files();
    // rename_file();
    delete_file();
}

int Client::upload_file()
{
    bool file_valid = false;
    File file;

    cout << "****************************************" << endl;
    cout << "*********     UPLOAD FILE      *********" << endl;
    cout << "****************************************" << endl;

    // Read file path from console
    std::cout << "Enter file path:" << endl;
    std::string file_path;
    std::getline(std::cin, file_path);

    // make sure input was valid and non null
    if (!cin || file_path.empty())
    {
        cerr << "[UPLOAD] Invalid file path input" << endl;
        std::cin.clear(); // put us back in 'normal' operation mode
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
    // open the file denoted in path
    try
    {
        file.read(file_path);
        file.displayFileInfo();
        file_valid = true; // break out of loop
    }
    catch (const std::exception &e)
    {
        std::cerr << "[UPLOAD] " << e.what() << std::endl;
        return 0;
    }

    // check if file is not empty
    if (file.getFileSize() == 0)
    {
        cerr << "[UPLOAD] Cannot upload empty files!" << endl;
        return 0;
    }

    // check if file size doesn't exceed 4GB
    if (file.getFileSize() >= MAX::max_file_size)
    {
        cerr << "[UPLOAD] File is too large!" << endl;
        return 0;
    }

    // Create Upload M1 type packet
    UploadM1 m1(file.get_file_name(), file.getFileSize());

    m1.print();
    Buffer serializedPacket = m1.serialize();

    // Create on the M1 message the wrapper packet to be sent
    Wrapper m1_wrapper(session_key, send_counter, serializedPacket);
    m1_wrapper.print(); // debug

    Buffer serialized_packet = m1_wrapper.serialize();

    // Send wrapped packet to server
    if (!sendData(clientSocket, serialized_packet))
        return false;

    clear_vec(serialized_packet);
    incrementCounter(); // increment send counter

    // -------------- HANDLE ACK PACKET ---------------------
    Buffer ack_buffer(Wrapper::getSize(UploadAck::getSize()));
    if (!receiveData(clientSocket, ack_buffer, ack_buffer.size()))
    {
        std::cerr << "Error receiving  data" << std::endl;
        return false;
    }
    // deserialize to extract payload in plaintext
    Wrapper wrapped_packet(session_key);

    if (!wrapped_packet.deserialize(ack_buffer))
    {
        std::cerr << "[CLIENT] Wrapper packet wasn't deserialized correctly!" << endl;
        return false;
    }

    if (wrapped_packet.getCounter() != rcv_counter)
        return 0;

    rcv_counter++;

    UploadAck ack;
    ack.deserialize(wrapped_packet.getPayload());

    if (!ack.getAckCode())
    {
        std::cerr << "[CLIENT] File already exists on the cloud!" << endl;
        return 0;
    }

    // -------------- HANDLE SENDING FILE CHUNKS ---------------------
    size_t chunk_size = MAX::max_file_chunk;
    int num_file_chunks = file.getFileSize() / chunk_size;
    int last_chunk_size = file.getFileSize() % chunk_size;
    UploadM2 m2_packet;
    Wrapper m2_wrapper;

    // Send chunks to server
    for (int i = 0; i < num_file_chunks; i++)
    {
        m2_packet = UploadM2(file.readChunk(chunk_size));

        m2_wrapper = Wrapper(session_key, send_counter, m2_packet.serialize());

        serialized_packet = m2_wrapper.serialize();
        if (!sendData(clientSocket, serialized_packet))
            return false;

        incrementCounter();

        // Log upload progess
        cout << "[UPLOAD] Uploaded " << (i + 1) * chunk_size << "/" << file.getFileSize() << "Bytes" << endl;
    }
    // send remaining data in file (if there's any)
    if (last_chunk_size != 0)
    {
        m2_packet = UploadM2(file.readChunk(last_chunk_size));

        Wrapper m2_wrapper(session_key, send_counter, m2_packet.serialize());

        serialized_packet = m2_wrapper.serialize();
        if (!sendData(clientSocket, serialized_packet))
            return false;

        incrementCounter();
    }

    cout << "[UPLOAD] Uploaded " << file.getFileSize() << "/" << file.getFileSize() << "Bytes" << endl;

    // -------------- HANDLE ACK PACKET ---------------------
    Buffer final_ack_buffer(Wrapper::getSize(UploadAck::getSize()));
    if (!receiveData(clientSocket, final_ack_buffer, final_ack_buffer.size()))
    {
        std::cerr << "Error receiving data" << std::endl;
        return false;
    }
    // deserialize to extract payload in plaintext
    wrapped_packet = Wrapper(session_key);

    if (!wrapped_packet.deserialize(final_ack_buffer))
    {
        std::cerr << "[UPLOAD] Wrapper packet wasn't deserialized correctly!" << endl;
        return false;
    }

    if (wrapped_packet.getCounter() != rcv_counter)
        return 0;

    rcv_counter++;

    ack = UploadAck();
    ack.deserialize(wrapped_packet.getPayload());

    if (!ack.getAckCode())
    {
        std::cerr << "[CLIENT] Uploading file " << file.get_file_name() << " has failed!" << endl;
        return 0;
    }
    else
    {
        cout << "[CLIENT] " << file.get_file_name() << " uploaded successfully" << endl;
        return 1;
    }
}

int Client::download_file()
{
    bool file_valid = false;

    cout << "****************************************" << endl;
    cout << "*********     Download File    *********" << endl;
    cout << "****************************************" << endl;

    // Read file path from console
    std::cout << "[Download] Enter file name:" << endl;
    std::string filename;
    std::getline(std::cin, filename);

    // make sure input was valid and non null
    if (!cin || filename.empty())
    {
        cerr << "[Download] Invalid filename input" << endl;
        std::cin.clear(); // put us back in 'normal' operation mode
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        return 0;
    }
    // Create Download M1 type packet
    DownloadM1 m1(filename);

    // Create on the M1 message the wrapper packet to be sent
    Wrapper m1_wrapper(session_key, send_counter, m1.serialize());

    // serialize M1 Wrapper packet
    Buffer serialized_packet = m1_wrapper.serialize();

    // send wrapped packet to server
    if (!sendData(clientSocket, serialized_packet))
    {
        return false;
    }
    clear_vec(serialized_packet);

    // increment counter
    incrementCounter();

    // -------------- HANDLE ACK PACKET ---------------------
    Buffer ack_buffer(Wrapper::getSize(DownloadAck::getSize()));
    if (!receiveData(clientSocket, ack_buffer, ack_buffer.size()))
    {
        std::cerr << "[Download] Error receiving data" << std::endl;
        return false;
    }
    // deserialize to extract payload in plaintext
    Wrapper wrapped_packet(session_key);

    if (!wrapped_packet.deserialize(ack_buffer))
    {
        std::cerr << "[Download] Wrapper packet wasn't deserialized correctly!" << endl;
        return false;
    }

    if (wrapped_packet.getCounter() != rcv_counter)
        return 0;

    rcv_counter++;

    DownloadAck ack;
    ack.deserialize(wrapped_packet.getPayload());

    if (ack.getAckCode())
    {
        std::cerr << "[Download] File does not exist on the cloud!" << endl;
        return 0;
    }
    uint32_t file_size = ack.getFileSize();

    // -------------- HANDLE RECEIVING FILE CHUNKS ---------------------

    File file;
    size_t chunk_size = MAX::max_file_chunk;
    int num_file_chunks = file_size / chunk_size;
    int last_chunk_size = file_size % chunk_size;
    DownloadM2 m2_packet;
    Wrapper m2_wrapper;
    bool error_occured = false;

    // Create "downloads" folder if it doesn't exist
    string downloads_path = "../downloads";
    if (!(std::filesystem::exists(downloads_path) && std::filesystem::is_directory(downloads_path)))
    {
        if (!std::filesystem::create_directory(downloads_path))
            return 0;
    }

    try
    {
        file.create(downloads_path + "/" + (string)filename);
    }
    catch (const std::exception &e)
    {
        std::cerr << "[DOWNLOAD] " << e.what() << std::endl;
        error_occured = true;
    }

    // Receive chunks from server
    for (int i = 0; i < num_file_chunks; i++)
    {
        // receive Wrapper packet message
        Buffer message_buff(Wrapper::getSize(DownloadM2::getSize(chunk_size)));

        if (!receiveData(clientSocket, message_buff, message_buff.size()))
        {
            std::cerr << "[Download] Error receiving data" << std::endl;
            error_occured = true;
            continue;
        }

        m2_wrapper = Wrapper(session_key);

        if (!m2_wrapper.deserialize(message_buff))
        {
            std::cerr << "[Download] Wrapper packet wasn't deserialized correctly!" << endl;
            error_occured = true;
            continue;
        }

        // Check counter otherwise exit
        if (m2_wrapper.getCounter() != rcv_counter)
            return false;

        rcv_counter++;

        m2_packet = DownloadM2();
        m2_packet.deserialize(m2_wrapper.getPayload());

        if (!error_occured)
            file.writeChunk(m2_packet.getFileChunk());

        // Log receival progess
        if (!error_occured)
            cout << "[Download] Downloaded " << (i + 1) * chunk_size << "B/ " << file_size << "B" << endl;
    }

    // receive remaining data in file (if there's any)
    if (last_chunk_size != 0)
    {
        Buffer message_buff(Wrapper::getSize(DownloadM2::getSize(last_chunk_size)));

        if (!receiveData(clientSocket, message_buff, message_buff.size()))
        {
            std::cerr << "[Download] Error receiving  data" << std::endl;
            error_occured = true;
            return false;
        }

        m2_wrapper = Wrapper(session_key);

        if (!m2_wrapper.deserialize(message_buff))
        {
            std::cerr << "[Download] Wrapper packet wasn't deserialized correctly!" << endl;
            error_occured = true;
            return false;
        }

        // Check counter otherwise exit
        if (m2_wrapper.getCounter() != rcv_counter)
            return false;

        rcv_counter++;

        m2_packet = DownloadM2();
        m2_packet.deserialize(m2_wrapper.getPayload());

        if (!error_occured)
            file.writeChunk(m2_packet.getFileChunk());
    }
    if (!error_occured)
        cout << "[Download] Downloaded " << file_size << "B/ " << file_size << "B" << endl;

    // ----------------------------------------------------------------------------

    if (error_occured)
        std::cerr << "[Download] File wasn't downloaded correctly!" << endl;
    else
        cout << "[Download] File downloaded correctly! " << endl;

    return 1;
}

int Client::list_files()
{
    bool file_valid = false;
    File file;

    cout << "****************************************" << endl;
    cout << "*********     List Files    *********" << endl;
    cout << "****************************************" << endl;

    // Create List M1 type packet
    ListM1 m1;

    Buffer serializedPacket = m1.serialize();
    // Create on the M1 message the wrapper packet to be sent
    Wrapper m1_wrapper(session_key, send_counter, serializedPacket);

    m1_wrapper.print(); // debug

    // serialize M1 Wrapper packet
    Buffer serialized_packet = m1_wrapper.serialize();

    // send wrapped packet to server
    if (!sendData(clientSocket, serialized_packet))
    {
        return false;
    }
    clear_vec(serialized_packet);

    // increment counter
    incrementCounter();

    // -------------- HANDLE ACK PACKET ---------------------
    Buffer ack_buffer(Wrapper::getSize(ListM2::getSize()));
    if (!receiveData(clientSocket, ack_buffer, ack_buffer.size()))
    {
        std::cerr << "Error receiving  data" << std::endl;
        return false;
    }
    // deserialize to extract payload in plaintext
    Wrapper wrapped_packet(session_key);

    if (!wrapped_packet.deserialize(ack_buffer))
    {
        std::cerr << "[CLIENT] Wrapper packet wasn't deserialized correctly!" << endl;
        return false;
    }

    if (wrapped_packet.getCounter() != rcv_counter)
        return 0;

    rcv_counter++;

    ListM2 ack_size_packet;
    ack_size_packet.deserialize(wrapped_packet.getPayload());

    if (ack_size_packet.getAckCode() == 1)
    {
        std::cerr << "[CLIENT] Folder doe not exist on the cloud!" << endl;
        return 0;
    }

    uint32_t file_list_size = ack_size_packet.getFile_List_Size();

    // receive the list of files from the server

    ListM3 m3(file_list_size);

    Buffer list_buffer(Wrapper::getSize(m3.getSize()));
    if (!receiveData(clientSocket, list_buffer, list_buffer.size()))
    {
        std::cerr << "Error receiving  data" << std::endl;
        return false;
    }

    // deserialize to extract payload in plaintext
    Wrapper m3_wrapper(session_key);

    if (!m3_wrapper.deserialize(list_buffer))
    {
        std::cerr << "[CLIENT] Wrapper packet wasn't deserialized correctly!" << endl;
        return false;
    }

    if (m3_wrapper.getCounter() != rcv_counter)
        return 0;

    m3.deserialize(m3_wrapper.getPayload());
    std::string fileListData = m3.getFileListData();

    rcv_counter++;

    // print the file names
    std::istringstream ss(fileListData);

    // Temporary string to store each element
    std::string token;

    // Use std::getline to split the string by commas
    while (std::getline(ss, token, ','))
    {
        // Print the file name
        std::cout << token << std::endl;
    }

    return 1;
}

int Client::rename_file()
{
    bool file_valid = false;
    File file;

    cout << "****************************************" << endl;
    cout << "*********     RENAME FILE      *********" << endl;
    cout << "****************************************" << endl;

    // Read file name from console
    std::cout << "Enter file name:" << endl;
    std::string file_name;
    std::getline(std::cin, file_name);

    // make sure input was valid and non null
    if (!cin || file_name.empty())
    {
        cerr << "[RENAME] Invalid file name input" << endl;
        std::cin.clear(); // put us back in 'normal' operation mode
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        return 0;
    }

    // Read file name from console
    std::cout << "Enter new file name:" << endl;
    std::string new_file_name;
    std::getline(std::cin, new_file_name);

    // make sure input was valid and non null
    if (!cin || new_file_name.empty())
    {
        cerr << "[RENAME] Invalid new file name input" << endl;
        std::cin.clear(); // put us back in 'normal' operation mode
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        return 0;
    }

    // Create Rename M1 type packet
    RenameM1 m1(file_name, new_file_name);

    m1.print();
    Buffer serializedPacket = m1.serialize();

    // Create on the M1 message the wrapper packet to be sent
    Wrapper m1_wrapper(session_key, send_counter, serializedPacket);
    m1_wrapper.print(); // debug

    Buffer serialized_packet = m1_wrapper.serialize();

    // Send wrapped packet to server
    if (!sendData(clientSocket, serialized_packet))
        return false;

    clear_vec(serialized_packet);
    incrementCounter(); // increment send counter

    // -------------- HANDLE ACK PACKET ---------------------
    Buffer ack_buffer(Wrapper::getSize(RenameAck::getSize()));
    if (!receiveData(clientSocket, ack_buffer, ack_buffer.size()))
    {
        std::cerr << "Error receiving  data" << std::endl;
        return false;
    }
    // deserialize to extract payload in plaintext
    Wrapper wrapped_packet(session_key);

    if (!wrapped_packet.deserialize(ack_buffer))
    {
        std::cerr << "[CLIENT] Wrapper packet wasn't deserialized correctly!" << endl;
        return false;
    }

    if (wrapped_packet.getCounter() != rcv_counter)
        return 0;

    rcv_counter++;

    RenameAck ack;
    ack.deserialize(wrapped_packet.getPayload());

    if (ack.getAckCode() == 0)
    {
        std::cout << "[CLIENT] File renamed successfully on the cloud!" << endl;
        return 1;
    }
    else if (ack.getAckCode() == 1)
    {
        std::cerr << "[CLIENT] File rename failed on the cloud!" << endl;
        return 0;
    }
    if (ack.getAckCode() == 2)
    {
        std::cerr << "[CLIENT] File does not exist on the cloud!" << endl;
        return 0;
    }
    return 0;
}
int Client::delete_file()
{
    bool file_valid = false;
    File file;

    cout << "****************************************" << endl;
    cout << "*********     DELETE FILE      *********" << endl;
    cout << "****************************************" << endl;

    // Read file name from console
    std::cout << "Enter file name:" << endl;
    std::string file_name;
    std::getline(std::cin, file_name);

    // make sure input was valid and non null
    if (!cin || file_name.empty())
    {
        cerr << "[Delete] Invalid file name input" << endl;
        std::cin.clear(); // put us back in 'normal' operation mode
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        return 0;
    }

    // Create Delete M1 type packet
    DeleteM1 m1(file_name);

    m1.print();
    Buffer serializedPacket = m1.serialize();

    // Create on the M1 message the wrapper packet to be sent
    Wrapper m1_wrapper(session_key, send_counter, serializedPacket);
    m1_wrapper.print(); // debug

    Buffer serialized_packet = m1_wrapper.serialize();

    // Send wrapped packet to server
    if (!sendData(clientSocket, serialized_packet))
        return false;

    clear_vec(serialized_packet);
    incrementCounter(); // increment send counter

    // -------------- HANDLE ACK PACKET ---------------------
    Buffer ack_buffer(Wrapper::getSize(DeleteAck::getSize()));
    if (!receiveData(clientSocket, ack_buffer, ack_buffer.size()))
    {
        std::cerr << "Error receiving  data" << std::endl;
        return false;
    }
    // deserialize to extract payload in plaintext
    Wrapper wrapped_packet(session_key);

    if (!wrapped_packet.deserialize(ack_buffer))
    {
        std::cerr << "[CLIENT] Wrapper packet wasn't deserialized correctly!" << endl;
        return false;
    }

    if (wrapped_packet.getCounter() != rcv_counter)
        return 0;

    rcv_counter++;

    DeleteAck ack;
    ack.deserialize(wrapped_packet.getPayload());

    if (ack.getAckCode() == 0)
    {
        std::cout << "[CLIENT] File deleted successfully on the cloud!" << endl;
        return 1;
    }
    else if (ack.getAckCode() == 1)
    {
        std::cerr << "[CLIENT] File deletion failed on the cloud!" << endl;
        return 0;
    }
    if (ack.getAckCode() == 2)
    {
        std::cerr << "[CLIENT] File does not exist on the cloud!" << endl;
        return 0;
    }
    return 0;
}

int Client::logout()
{
    LogoutM1 m1;

    Wrapper m1_wrapper(session_key, send_counter, m1.serialize());

    Buffer serialized_packet = m1_wrapper.serialize();

    // Send wrapped packet to server
    if (!sendData(clientSocket, serialized_packet))
        return false;

    clear_vec(serialized_packet);
    incrementCounter(); // increment send counter
}
void Client::incrementCounter()
{
    if (send_counter == MAX::counter_max_value)
        performClientJob(); // reinitiate session
    else

        send_counter++;
}

Client::~Client()
{
    // Clean up resources, close the socket, etc.
    close(clientSocket);
}
