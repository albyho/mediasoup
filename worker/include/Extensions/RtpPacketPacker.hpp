#ifndef RtpPacketPacker_hpp
#define RtpPacketPacker_hpp

#include <cstdint>    // uint8_t, etc
#include <cstddef>    // size_t
#include <set>
#include <memory>
#include "RTC/RtpPacket.hpp"
#include "Utils/Buffer.hpp"

namespace RTC {

struct NALUHeader
{
#if defined(MS_LITTLE_ENDIAN)
    uint8_t TYPE : 5;
    uint8_t NRI : 2;
    uint8_t F : 1;
#elif defined(MS_BIG_ENDIAN)
    uint8_t F : 1;
    uint8_t NRI : 2;
    uint8_t TYPE : 5;
#endif
};

struct FUIndicator
{
#if defined(MS_LITTLE_ENDIAN)
    uint8_t TYPE : 5;
    uint8_t NRI : 2;
    uint8_t F : 1;
#elif defined(MS_BIG_ENDIAN)
    uint8_t F : 1;
    uint8_t NRI : 2;
    uint8_t TYPE : 5;
#endif
};

struct FUHeader
{
#if defined(MS_LITTLE_ENDIAN)
    uint8_t TYPE : 5;
    uint8_t R : 1;
    uint8_t E : 1;
    uint8_t S : 1;
#elif defined(MS_BIG_ENDIAN)
    uint8_t S : 1;
    uint8_t E : 1;
    uint8_t R : 1;
    uint8_t TYPE : 5;
#endif
};

class RtpPacketPacker {

public:
    static BlockBuffer H264FindNALU(const uint8_t* data, size_t searchLength);
    static BlockBuffer H264FindNALU(const uint8_t* data, const uint8_t* end);
    static const std::vector<BlockBuffer> H264FindNALUs(const uint8_t* data, const size_t searchLength);
    static const std::vector<BlockBuffer> H264FindNALUs(const uint8_t* data, const uint8_t* end);
    
    static RtpPacket* H264PackNALU(BlockBuffer nalu, uint32_t timestamp, uint32_t ssrc);
    static RtpPacket* H264PackSTAPA(const std::vector<BlockBuffer>& nalus, uint32_t timestamp, uint32_t ssrc);
    static std::vector<RtpPacket *> H264PackFUA(BlockBuffer nalu, uint32_t timestamp, uint32_t ssrc);
    static std::vector<RtpPacket*> H264Pack(const uint8_t* data, size_t length, uint16_t startSequenceNumber, uint16_t endSequenceNumber, uint32_t timestamp, uint32_t ssrc);
    
    static void H264RtpHeaderInit(RtpPacket::Header* header);
    
public:
    static void G711ARtpHeaderInit(RtpPacket::Header* header);

};

} // namespace RTC

#endif /* RtpPacketPacker_hpp */
