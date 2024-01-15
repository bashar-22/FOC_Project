#include "download.h"
#include <vector>
#include <arpa/inet.h>
// ----------------------------------- DOWNLOAD M1 ------------------------------------

DownloadM1::DownloadM1() {}

DownloadM1::DownloadM1(string file_name)
{
    this->command_code = RequestCodes::DOWNLOAD_REQ;
    strncpy(this->file_name, file_name.c_str(), MAX::file_name + 1);
}

Buffer DownloadM1::serialize() const
{
    Buffer buff(MAX::initial_request_length);
    size_t position = 0;

    // insert the command code uint8_t (one byte) interpreted as unsigned char
    memcpy(buff.data(), &command_code, sizeof(uint8_t));
    position += sizeof(uint8_t);

    // insert the file string with a size of max of file name (50) +1
    unsigned char const *file_name_pointer = reinterpret_cast<unsigned char const *>(&file_name);
    memcpy(buff.data() + position, file_name_pointer, ((MAX::file_name + 1) * sizeof(char)));

    return buff;
}

void DownloadM1::deserialize(Buffer input)
{
    size_t position = 0;

    memcpy(&this->command_code, input.data(), sizeof(uint8_t));
    position += sizeof(uint8_t);

    memcpy(&this->file_name, input.data() + position, (MAX::file_name + 1) * sizeof(char));
}

int DownloadM1::getSize()
{
    int size = 0;

    size += sizeof(uint8_t);
    size += (MAX::file_name + 1) * sizeof(char);

    return size;
}

void DownloadM1::print() const
{
    cout << "---------- DOWNLOAD M1 ---------" << endl;
    cout << "FILE NAME: " << file_name << endl;
    cout << "--------------------------------" << endl;
}

// ----------------------------------- DOWNLOAD ACKNOWLEDGEMENT ------------------------------------

DownloadM2::DownloadM2() {}

DownloadM2::DownloadM2(string ack_msg, uint32_t file_size)
{
    this->command_code = RequestCodes::DOWNLOAD_REQ;
    this->file_size = file_size;
    strncpy(this->ack_msg, ack_msg.c_str(), MAX::ack_msg + 1);
}

Buffer DownloadM2::serialize() const
{
    Buffer buff;

    buff.insert(buff.begin(), command_code);
    // Convert file_size to network byte order
    uint32_t no_file_size = htonl(file_size);

    // Insert file_size into the buffer
    unsigned char const *file_size_begin = reinterpret_cast<unsigned char const *>(&no_file_size);
    buff.insert(buff.end(), file_size_begin, file_size_begin + sizeof(uint32_t));

    // Insert ack_msg into the buffer
    unsigned char const *ack_msg_pointer = reinterpret_cast<unsigned char const *>(&ack_msg);
    buff.insert(buff.end(), ack_msg_pointer, ack_msg_pointer + ((MAX::ack_msg + 1) * sizeof(char)));

    return buff;
}

void DownloadM2::deserialize(Buffer input)
{

    size_t position = 0;

    // Extract command_code from the buffer
    memcpy(&this->command_code, input.data(), sizeof(uint8_t));
    position += sizeof(uint8_t);

    // Extract file_size from the buffer
    uint32_t network_filesize = 0;
    memcpy(&network_filesize, input.data() + position, sizeof(uint32_t));
    file_size = ntohl(network_filesize);
    position += sizeof(uint32_t);

    // Extract ack_msg from the buffer
    memcpy(&this->ack_msg, input.data() + position, (MAX::ack_msg + 1) * sizeof(char));
}

int DownloadM2::getSize()
{
    int size = 0;

    size += sizeof(uint8_t);
    size += sizeof(uint32_t); // file_size
    size += (MAX::ack_msg + 1) * sizeof(char);

    return size;
}

void DownloadM2::print() const
{
    cout << "---------- DOWNLOAD ACKNOWLEDGEMENT ---------" << endl;
    cout << "Acknowledge message: " << ack_msg << endl;
    cout << "--------------------------------------------" << endl;
}

// ---------------------------------- DOWNLOAD M3 -----------------------------------

DownloadM3::DownloadM3(Buffer file_chunk)
{
    command_code = RequestCodes::DOWNLOAD_CHUNK;
    this->file_chunk = file_chunk;
}

Buffer DownloadM3::serialize() const
{
    Buffer buff;

    buff.insert(buff.begin(), command_code);
    buff.insert(buff.end(), file_chunk.data(), file_chunk.data() + file_chunk.size());

    return buff;
}

void DownloadM3::deserialize(Buffer input)
{
    size_t position = 0;

    memcpy(&this->command_code, input.data(), sizeof(uint8_t));
    position += sizeof(uint8_t);

    memcpy(&this->file_chunk, input.data() + position, input.size() - sizeof(uint8_t));
}

size_t DownloadM3::getSize(size_t chunk_size)
{
    int size = 0;

    size += sizeof(uint8_t);
    size += chunk_size * sizeof(unsigned char);

    return size;
}

void DownloadM3::print() const
{
    cout << "--------- DOWNLOAD M3 --------" << endl;
    cout << "File chunk: ";
    for (Buffer::const_iterator it = file_chunk.begin(); it < file_chunk.end(); ++it)
        printf("%02X", *it);
    cout << "\n CHUNK SIZE: " << file_chunk.size() << endl;
    cout << "------------------------------" << endl;
}
