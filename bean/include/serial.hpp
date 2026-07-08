#ifndef SERIAL_HPP_
#define SERIAL_HPP_

#include <string>
#include <cstdint>

class SerialPort {
public:
    SerialPort();
    ~SerialPort();

    bool open(const std::string& port);
    bool send(const uint8_t* data, size_t len);
    void close();
    bool isOpen() const;

private:
    bool configurePort();
    int fd_;
};

#endif
