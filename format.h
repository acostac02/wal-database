//
// Created by arisu on 09/11/2026.
//

#ifndef KWDB_FORMAT_H
#define KWDB_FORMAT_H
#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <istream>
#include <ostream>
#include <string>
#include <stdexcept>

namespace format {
    template <typename T>
        void writeValue(std::ostream& out, const T& value) {
        static_assert(std::is_arithmetic_v<T>);
        out.write(reinterpret_cast<const char*>(&value), sizeof(T));
    }

    //reading values from file
    template <typename T>
    T readValue(std::istream& in) {
        static_assert(std::is_arithmetic_v<T>);
        T v{};
        in.read(reinterpret_cast<char*>(&v), sizeof(T));
        if (!in) throw std::runtime_error("Unexpected end of file");
        return v;
    }

    inline void writeString(std::ostream& out, const std::string& s) {
        if (s.size() > UINT32_MAX)
            throw std::runtime_error("String too long.");
        writeValue(out, static_cast<uint32_t>(s.size()));
        out.write(s.data(), static_cast<std::streamsize>(s.size()));
    }

    inline std::string readString(std::istream& in) {
        auto len = readValue<uint32_t>(in);
        std::string s(len, '\0');
        in.read(s.data(), len);
        if (!in) throw std::runtime_error("Unexpected end of file");
        return s;
    }

    //Checksum function.
    //Initialize bits at 1, for each byte in the payload apply xor and do a polynomial division.
    inline uint32_t crc32(const std::string& data) {
        uint32_t crc = 0xFFFFFFFFu;
        for (unsigned char c : data) {
            crc ^= c;
            for (int i = 0; i < 8; ++i)
                crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
        return ~crc;
    }

}
#endif //KWDB_FORMAT_H