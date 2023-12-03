#include <iostream>
#include <cstring>
#include <arpa/inet.h>
#include <unistd.h>
#include <openssl/bio.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include "../Diffie-Hellman.h"
using namespace std;
const int PORT = 8080;
const int MAX_CERTIFICATE_SIZE = 4096;
const int MAX_USERNAME_LENGTH = 5;

bool verifyServerCertificate(X509 * caCert, X509_CRL * crl, X509 *serverCert) {
   
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
    if (ret != 1) {
        fprintf(stderr, "Certificate verification failed\n");
        ERR_print_errors_fp(stderr);
    } else {
        printf("Certificate verification succeeded\n");
    }

    // Clean up
    X509_STORE_CTX_free(ctx);
    X509_STORE_free(store);
    X509_free(serverCert);
    X509_free(caCert);
    X509_CRL_free(crl);

    return ret == 1;
}
// Function to receive server certificate from the server
bool receiveServerCertificate(int clientSocket, X509*& serverCert) {
    // Receive the certificate size
    int certSize;
    ssize_t bytesReceived = recv(clientSocket, &certSize, sizeof(certSize), MSG_WAITALL);
    if (bytesReceived <= 0) {
        std::cerr << "Error receiving certificate size" << std::endl;
        return false;
    }
    if(certSize>MAX_CERTIFICATE_SIZE)
    {
        std::cerr << "Certificate size exceeds the max size" << std::endl;
        return false;
    }

    // Receive the certificate data
    char certBuffer[certSize]={0};
    bytesReceived = recv(clientSocket, certBuffer, certSize, MSG_WAITALL);
    if (bytesReceived <= 0) {
        std::cerr << "Error receiving certificate data" << std::endl;
        return false;
    }

    // Create a BIO from the received data
    BIO* bio = BIO_new_mem_buf(certBuffer, certSize);
    if (!bio) {
        std::cerr << "Error creating BIO from certificate data" << std::endl;
        return false;
    }

    // Read the X509 certificate from the BIO
   
   serverCert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
  if (!serverCert) {
    std::cerr << "Error reading X509 certificate from BIO" << std::endl;
    
    ERR_print_errors_fp(stderr); // Print OpenSSL error information
    BIO_free(bio);
   
    return false;
}
    
// Print serial number
ASN1_INTEGER* serialNumber = X509_get_serialNumber(serverCert);
BIGNUM* bnSerial = ASN1_INTEGER_to_BN(serialNumber, nullptr);
char* serialHex = BN_bn2hex(bnSerial);

cout << "Serial Number: " << serialHex<< endl;
OPENSSL_free(serialHex);
BN_free(bnSerial);
    // Clean up
    BIO_free(bio);

    return true;
}


int main() {
    
    // Create socket
    int clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket == -1) {
        std::cerr << "Error creating client socket" << std::endl;
        return -1;
    }

    // Server address
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr) <= 0) {
        std::cerr << "Invalid address/Address not supported" << std::endl;
        close(clientSocket);
        return -1;
    }

    // Connect to server
    if (connect(clientSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) {
        std::cerr << "Error connecting to server" << std::endl;
        close(clientSocket);
        return -1;
    }

    // Get username from user
    std::string userName;
    std::cout << "Enter your username (up to 5 characters): ";
    std::cin >> userName;

    // Enforce the maximum username length
    if (userName.size() > MAX_USERNAME_LENGTH) {
        std::cerr << "Error: Username is too long. Maximum length is 5 characters." << std::endl;
        // Handle the error, return, or take appropriate action
        return 1;
    }

    // Send username to server
    send(clientSocket, userName.c_str(), userName.size(), 0);



    // Receive result from server
    int responseSize=1;
    char buffer[responseSize+1] = {0}; //+1 for null terminator
    ssize_t bytesRead = recv(clientSocket, buffer, responseSize, MSG_WAITALL);
    if (bytesRead <= 0) {
        std::cerr << "Error receiving result from server" << std::endl;
    } else {
        std::string serverResponse(buffer);
      std::cout << "Server response: " << buffer << std::endl;

    // Check the server response to determine the next steps
    if (serverResponse.compare("1") == 0) {
        // User exists, proceed to receive the server certificate
        X509* serverCert = nullptr;
        if (receiveServerCertificate(clientSocket, serverCert)) {
            // Use the server certificate as needed
            std::cout << "Received server certificate successfully" << std::endl;

            const char *caCertFile = "Cloud Storage CA_cert.pem";
            const char *crlFile = "Cloud Storage CA_crl.pem";
            // Load CA certificate
        X509 *caCert = nullptr;
        FILE *caCertFilePtr = fopen(caCertFile, "r");
        if (!caCertFilePtr) {
            perror("Error opening CA certificate file");
            return 0;
        }

        caCert = PEM_read_X509(caCertFilePtr, nullptr, nullptr, nullptr);
        fclose(caCertFilePtr);

        if (!caCert) {
            ERR_print_errors_fp(stderr);
            return 0;
        }

        // Load CRL
        X509_CRL *crl = nullptr;
        FILE *crlFilePtr = fopen(crlFile, "r");
        if (!crlFilePtr) {
            perror("Error opening CRL file");
            X509_free(caCert);
            return 0;
        }

        crl = PEM_read_X509_CRL(crlFilePtr, nullptr, nullptr, nullptr);
        fclose(crlFilePtr);

        if (!crl) {
            ERR_print_errors_fp(stderr);
            X509_free(caCert);
            return 0;
        }
        bool verified=verifyServerCertificate(caCert, crl,serverCert);
        if (verified) {
        std::cout << "Server certificate verification successful." << std::endl;

        } else {
            std::cerr << "Server certificate verification failed." << std::endl;
            return 0;
        }

        // generate the diffie-Hellman keys for the client
          EVP_PKEY *DH_Keys = diffieHellmanKeyGeneration();
          int len_serialized_public_key = 0;

         int keyLength;
        char* serializedKey = serializePublicKey(DH_Keys, &keyLength);

        if (serializedKey != NULL) {
        // Use the serialized key as needed
        
        // Send the key size to the server
        send(clientSocket, &keyLength, sizeof(keyLength), 0);

        // send the DH public key to the server
        send(clientSocket, serializedKey, keyLength, 0);

        // Don't forget to free the allocated memory when done
        free(serializedKey);
        } else {
            // Handle error, e.g., print an error message
            fprintf(stderr, "Error serializing public key\n");
            return 0;
        }

        // Cleanup OpenSSL (if not done already)
        EVP_cleanup();


          


            // Clean up
            X509_free(serverCert);
        } else {
            std::cerr << "Error receiving server certificate" << std::endl;
        }
    } else if (strcmp(buffer, "0") == 0) {
        // User does not exist, handle accordingly (close the connection or take other actions)
        std::cerr << "User does not exist" << std::endl;
        // You might want to close the client socket or take other appropriate actions here
    } else {
        // Unexpected response from the server, handle accordingly
        std::cerr << "Unexpected server response" << std::endl;
    }


    }

    // Close client socket
    close(clientSocket);

    return 0;
}
