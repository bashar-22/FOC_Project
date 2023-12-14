#include <iostream>
#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <cstring>
#include "./security/Diffie-Hellman.h"
#include "Upload.h"
#include "./tools/file.h"
#include "crypto.h"

using namespace std;

int main()
{
  // Create a BIO object (memory buffer in this case, you might replace it with a file BIO)
  BIO *bio;
  X509 *certificate;

  // Insert PEM-formatted X.509 certificate into the BIO

  const char *pem_data = "-----BEGIN CERTIFICATE-----\n"
                         "MIIDsjCCApqgAwIBAgIGAYwIZOoPMA0GCSqGSIb3DQEBBQUAMGkxCzAJBgNVBAYT\n"
                         "AklUMRswGQYDVQQKDBJVbml2ZXJzaXR5IG9mIFBpc2ExIDAeBgNVBAsMF0NlcnRp\n"
                         "ZmljYXRpb24gQXV0aG9yaXR5MRswGQYDVQQDDBJVbml2ZXJzaXR5IG9mIFBpc2Ew\n"
                         "HhcNMjMxMTI1MjEzMTIwWhcNMzMxMTI1MjEzMTMzWjBpMQswCQYDVQQGEwJJVDEb\n"
                         "MBkGA1UECgwSVW5pdmVyc2l0eSBvZiBQaXNhMSAwHgYDVQQLDBdDZXJ0aWZpY2F0\n"
                         "aW9uIEF1dGhvcml0eTEbMBkGA1UEAwwSVW5pdmVyc2l0eSBvZiBQaXNhMIIBIjAN\n"
                         "BgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAoWvSudFRe6ggjNAch04t8UxD+tOt\n"
                         "e55T0Q8EWFvGjxgYTOVU3BaOIDXGTAwYOp6R3VPSKQry7FFELcWFO8Bp0SEPSunl\n"
                         "kYR1+PzsCr2ashj4l6axVumT7IXVoj7DUj3pe2ObbP0cazCaUK2rmfgue3Nr7NFW\n"
                         "ddrX5dUAeZguz+ffmPbnoGL20zKRZLao0vOz9Jsm4tL7ZQluG3Y1saoy41+3q8/q\n"
                         "fgNlsNOx+LisHJI13Hb1vKrN0JYdN/ay/S8P4alGf5pXGy3jOujtTU4GMHFaDXtm\n"
                         "EMwYqiiq/RUW08H1/1H5wCQuGu+wSJn4kChNNXwWjvYOHatYLnR6X7hYGQIDAQAB\n"
                         "o2AwXjAfBgNVHSMEGDAWgBQhROJlPu5Y2PL57P++eEULLeMfcDAdBgNVHQ4EFgQU\n"
                         "IUTiZT7uWNjy+ez/vnhFCy3jH3AwDgYDVR0PAQH/BAQDAgGGMAwGA1UdEwQFMAMB\n"
                         "Af8wDQYJKoZIhvcNAQEFBQADggEBAAxCn8ObWx/E8fcnKtcl1FQEK4EQUutJzQ5b\n"
                         "RH+E3xbqsDax52Fwht5n0ArFCV8CtG9dRQmFHBcwkaCQUyiEPwQrPIufOCgbTUKy\n"
                         "Hc/okZXvtiIyrcsAdKXaaVyUK8+YUh/db8l7MqmaQsaqwkEEGrL+FTPNQfz3pQJT\n"
                         "RMrKJNFImAqwCYaPA6YojZEmuBLWJ2Qe6geW8dV/10pXB/bwVNDap0iSlcW4YlS4\n"
                         "7+9xWRfSFDdR3weD2ikxQwv6eKxTEWsOgtwS+Dn1ml+SbeNNWlmxKx0qx53kvJ0b\n"
                         "6XsupvuiZwDwIAaaC4OdNWx4q6OD+KLwG83Hj9T8tti3pMdgdPY=\n"
                         "-----END CERTIFICATE-----";

  // Create a memory BIO and write PEM data into it
  bio = BIO_new(BIO_s_mem());
  BIO_write(bio, pem_data, strlen(pem_data));

  // Read the X.509 certificate from the BIO
  certificate = PEM_read_bio_X509(bio, NULL, NULL, NULL);

  if (certificate == NULL)
  {
    // Handle error
    fprintf(stderr, "Error reading X.509 certificate\n");
    ERR_print_errors_fp(stderr);
    BIO_free(bio);
    return 1;
  }
  else
  {

    // Accessing certificate information
    // Example: Get subject and issuer names
    X509_NAME *subject_name = X509_get_subject_name(certificate);
    X509_NAME *issuer_name = X509_get_issuer_name(certificate);

    printf("Subject: %s\n", X509_NAME_oneline(subject_name, NULL, 0));
    printf("Issuer: %s\n", X509_NAME_oneline(issuer_name, NULL, 0));
  }

  // Clean up
  X509_free(certificate);
  BIO_free(bio);
  vector<unsigned char> buff;
  size_t sharedKeyLen;
  EVP_PKEY *key1, *key2;
  key1 = ECDHKeyGeneration();
  key2 = ECDHKeyGeneration();
  serializePubKey(key1, buff);
  cout << "the serialized key size is : " << buff.size() << endl;
  char *canon_dir = realpath("../README.md", NULL);
  cout << canon_dir << endl;

  string message_to_encrypt = "Hello Arsalen!";
  string secretKey = "7gHtR4eL9oPqW2sX";
  string aad_body = "this message is going on the clear";
  // -----
  vector<unsigned char> clear_buff(message_to_encrypt.begin(), message_to_encrypt.end());
  vector<unsigned char> cipher_buffer;
  vector<unsigned char> tag;
  vector<unsigned char> iv;
  vector<unsigned char> key(secretKey.begin(), secretKey.end());
  vector<unsigned char> aad(aad_body.begin(), aad_body.end());

  // encrypt into cipher_buffer
  encrypt_aes_ccm(clear_buff, cipher_buffer, key, iv, aad, tag);

  // priting results
  std::vector<unsigned char>::iterator it;

  std::cout << "cipher buff  is :";
  for (it = cipher_buffer.begin(); it < cipher_buffer.end(); it++)
    printf("%02X", *it);
  std::cout << '\n';

  std::cout << "tag is :";
  for (it = tag.begin(); it < tag.end(); it++)
    printf("%02X", *it);
  std::cout << '\n';

  // decrypt into decrypted_buff
  vector<unsigned char> decrypted_buff;
  decrypt_aes_ccm(cipher_buffer, decrypted_buff, key, iv, aad, tag);

  // print results
  std::cout << "Decrypted and authenticated message is: ";
  for (it = decrypted_buff.begin(); it < decrypted_buff.end(); it++)
    printf("%c", *it);
  std::cout << '\n';

  return 0;
}
