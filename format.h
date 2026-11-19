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

/**
 * @brief Binary serialization utilities.
 *
 * Provides low-level functions used to serialize and deserialize primitive values and strings for
 * database snapshots and WAL records.
 *
 * Values must always be read in the same order and with the same types  used during serialization,
 * since the binary format does not store field names or type information.
 */
namespace format {

    /**
     * @brief Writes an arithmetic value as raw binary bytes.
     *
     * The value is copied directly to the output stream using its in-memory  representation.
     * The serialized size is sizeof(T) bytes and uses the platform native endianness.
     *
     * This function is intended for primitive arithmetic types only. Strings must be serialized using writeString().
     *
     * @tparam T Arithmetic type to serialize.
     * @param out Destination stream.
     * @param value Value to write.
     */
    template<typename T>
    void writeValue(std::ostream &out, const T &value) {
        static_assert(std::is_arithmetic_v<T>);
        out.write(reinterpret_cast<const char *>(&value), sizeof(T));
    }

    /**
     * @brief Reads an arithmetic value from a binary stream.
     *
     * Reads sizeof(T) bytes from the stream and reconstructs a value of type T using the platform
     * native representation.
     *
     * The type and read order must exactly match the corresponding writeValue() call used during serialization.
     *
     * @tparam T Arithmetic type to read.
     * @param in Source stream.
     * @return Deserialized value.
     *
     * @throws std::runtime_error If the stream does not contain enough data.
     */
    template<typename T>
    T readValue(std::istream &in) {
        static_assert(std::is_arithmetic_v<T>);
        T v{};
        in.read(reinterpret_cast<char *>(&v), sizeof(T));
        if (!in) throw std::runtime_error("Unexpected end of file");
        return v;
    }

    /**
     * @brief Serializes a string using a length-prefixed format.
     *
     * The string length is written first as a uint32_t value, followed by the string characters without
     * a null terminator.
     *
     * Format:
     * [uint32_t length][raw character bytes]
     *
     * @param out Destination stream.
     * @param s String to serialize.
     *
     * @throws std::runtime_error If the string length exceeds UINT32_MAX.
     */
    inline void writeString(std::ostream &out, const std::string &s) {
        if (s.size() > UINT32_MAX)
            throw std::runtime_error("String too long.");
        writeValue(out, static_cast<uint32_t>(s.size()));
        out.write(s.data(), static_cast<std::streamsize>(s.size()));
    }

    /**
     * @brief Reads a string serialized by writeString().
     *
     * The function first reads the string length as a uint32_t value and then reads the corresponding
     * number of bytes from the stream.
     *
     * @param in Source stream.
     * @return Deserialized string.
     *
     * @throws std::runtime_error If the stream is truncated or corrupted.
     */
    inline std::string readString(std::istream &in) {
        auto len = readValue<uint32_t>(in);
        std::string s(len, '\0');
        in.read(s.data(), len);
        if (!in) throw std::runtime_error("Unexpected end of file");
        return s;
    }

    /**
     * @brief Computes the CRC32 checksum of a byte sequence.
     *
     * The checksum is used by WAL records to detect corruption caused by incomplete writes, torn writes
     * or storage failures.
     *
     * Each input byte is processed through the CRC32 polynomial division algorithm and the resulting
     * checksum is returned.
     *
     * @param data Input data.
     * @return CRC32 checksum value.
     */
    inline uint32_t crc32(const std::string &data) {
        uint32_t crc = 0xFFFFFFFFu;
        for (unsigned char c: data) {
            crc ^= c;
            for (int i = 0; i < 8; ++i)
                crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
        return ~crc;
    }
}
#endif //KWDB_FORMAT_H
