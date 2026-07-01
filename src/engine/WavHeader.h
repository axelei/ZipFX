#ifndef ZIPFX_WAV_HEADER_H
#define ZIPFX_WAV_HEADER_H

#include <cstdint>

// Minimal 44-byte canonical PCM RIFF/WAVE header. All fields are naturally
// 4-byte aligned so sizeof(WavHeader) == 44 with no compiler-inserted padding.
struct WavHeader
{
    char     riff[4]       = {'R','I','F','F'};
    uint32_t fileSize      = 0;
    char     wave[4]       = {'W','A','V','E'};
    char     fmt[4]        = {'f','m','t',' '};
    uint32_t fmtSize       = 16;
    uint16_t audioFormat   = 1;
    uint16_t numChannels   = 1;
    uint32_t sampleRate    = 44100;
    uint32_t byteRate      = 0;
    uint16_t blockAlign    = 0;
    uint16_t bitsPerSample = 0;
    char     data[4]       = {'d','a','t','a'};
    uint32_t dataSize      = 0;
};
static_assert(sizeof(WavHeader) == 44, "WavHeader must be exactly 44 bytes");

#endif
