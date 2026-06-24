#ifndef ZIPFX_BINARY_UTILS_H
#define ZIPFX_BINARY_UTILS_H

#include <cstdint>
#include <fstream>

inline uint32_t readLE32(const uint8_t* d)
{
    return static_cast<uint32_t>(d[0]) | (static_cast<uint32_t>(d[1]) << 8)
         | (static_cast<uint32_t>(d[2]) << 16) | (static_cast<uint32_t>(d[3]) << 24);
}

inline uint16_t readLE16(const uint8_t* d)
{
    return static_cast<uint16_t>(d[0]) | (static_cast<uint16_t>(d[1]) << 8);
}

inline void writeLE32(std::ofstream& f, uint32_t v)
{
    uint8_t b[4] = { static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8),
                     static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 24) };
    f.write(reinterpret_cast<const char*>(b), 4);
}

inline void writeLE16(std::ofstream& f, uint16_t v)
{
    uint8_t b[2] = { static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8) };
    f.write(reinterpret_cast<const char*>(b), 2);
}

#endif
