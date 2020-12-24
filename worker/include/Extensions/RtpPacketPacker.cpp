#include "RtpPacketPacker.hpp"
#include <assert.h>

namespace RTC {

constexpr size_t kMaxRtpPacketPayload = 1400;
constexpr size_t kRtpPacketHeaderSize = 12;
constexpr size_t kH264FUHeaderSize = 2;
constexpr size_t kH264STAPAHeaderSize = 1;
//constexpr size_t kH265FUHeaderSize = 3;
constexpr uint8_t kFUStart = 0x80;
//constexpr uint8_t kFMid = 0x00;
constexpr uint8_t kFUEnd = 0x40;
constexpr uint8_t kSTAPAHeader = 24;

BlockBuffer RtpPacketPacker::H264FindNALU(const uint8_t* data, const size_t searchLength)
{
    // data 中的 NALU 包含 0x00000001
    BlockBuffer result {nullptr, 0};
    if(searchLength <= 4) return result;

    return H264FindNALU(data, data + searchLength);
}

BlockBuffer RtpPacketPacker::H264FindNALU(const uint8_t* data, const uint8_t* end)
{
    // data 中的 NALU 包含 0x00000001
    // end 指向 NALU 结束后的第一个字节
    BlockBuffer result {nullptr, 0};
    if(data + 4 >= end) return result;

    for(data += 3; data + 1 < end; data++)
    {
        if (0x01 == *data && 0x00 == *(data - 1) && 0x00 == *(data - 2) && 0x00 == *(data - 3))
        {
            result.base = data + 1;
            result.len = end - data - 4;
            return result;
        }
    }
        
    return result;
}

const std::vector<BlockBuffer> RtpPacketPacker::H264FindNALUs(const uint8_t* data, const size_t searchLength)
{
    // data 中的 NALU 包含 0x00000001
    if(searchLength <= 4) {
        std::vector<BlockBuffer> result;
        return result;
    }

    return H264FindNALUs(data, data + searchLength);
}

const std::vector<BlockBuffer> RtpPacketPacker::H264FindNALUs(const uint8_t* data, const uint8_t* end)
{
    // data 中的 NALU 包含 0x00000001
    // end 指向 NALU 结束后的第一个字节
    std::vector<BlockBuffer> result;
    const uint8_t* ptr = data;
    while (ptr < end) {
        BlockBuffer n = H264FindNALU(ptr, end);
        if(!n.len) break;
        result.push_back(n);
        ptr = n.base;
    }
    
    return result;
}

RtpPacket* RtpPacketPacker::H264PackNALU(BlockBuffer nalu, uint32_t timestamp, uint32_t ssrc)
{
    // data 不包含 0x00000001
    assert(0 < (*nalu.base & 0x1F) && (*nalu.base & 0x1F) < 24);

    auto* buffer = new uint8_t[kRtpPacketHeaderSize/*sizeof(RtpPacket::Header)*/ + nalu.len];
    auto* header = reinterpret_cast<RtpPacket::Header*>(buffer);
    H264RtpHeaderInit(header);
    header->marker = 0;
    header->sequenceNumber = 0;
    header->timestamp = htonl(timestamp);
    header->ssrc = htonl(ssrc);
    std::memcpy(buffer + kRtpPacketHeaderSize/*sizeof(RtpPacket::Header)*/, nalu.base, nalu.len);

    auto* packet = reinterpret_cast<RtpPacket*>(buffer);
    return packet;
}

RtpPacket* RtpPacketPacker::H264PackSTAPA(const std::vector<BlockBuffer>& nalus, uint32_t timestamp, uint32_t ssrc)
{
    // data 不包含 0x00000001
    
    size_t bufferLength = kRtpPacketHeaderSize + kH264STAPAHeaderSize;
    for (auto iter = nalus.cbegin(); iter != nalus.cend(); iter++)
    {
        bufferLength += sizeof(uint16_t) + iter->len;
    }
    auto* buffer = new uint8_t[bufferLength];
    auto* header = reinterpret_cast<RtpPacket::Header*>(buffer);
    H264RtpHeaderInit(header);
    header->marker = 0;
    header->sequenceNumber = 0;
    header->timestamp = htonl(timestamp);
    header->ssrc = htonl(ssrc);

    buffer[kRtpPacketHeaderSize + 0] = kSTAPAHeader;
    size_t payloadOffset = kRtpPacketHeaderSize + kH264STAPAHeaderSize;

    for (auto iter = nalus.cbegin(); iter != nalus.cend(); iter++)
    {
        // NALU Size(Big endian)
        uint8_t* p = (uint8_t*)&iter->len;
        buffer[payloadOffset + 0] = p[0];
        buffer[payloadOffset + 1] = p[1];
        payloadOffset += 2;
        std::memcpy(buffer + payloadOffset, iter->base, iter->len);
        payloadOffset += iter->len;
    }
    
    auto* packet = reinterpret_cast<RtpPacket*>(buffer);
    return packet;
}

std::vector<RtpPacket *> RtpPacketPacker::H264PackFUA(BlockBuffer nalu, uint32_t timestamp, uint32_t ssrc)
{
    // data 不包含 0x00000001
    assert(0 < (*nalu.base & 0x1F) && (*nalu.base & 0x1F) < 24);
    assert(nalu.len > 1);

    std::vector<RtpPacket *> result;

    uint8_t* data = const_cast<uint8_t*>(nalu.base);
    size_t length = nalu.len;
    
    // RFC6184 5.3. NAL Unit Header Usage: Table 2 (p15)
    // RFC6184 5.8. Fragmentation Units (FUs) (p29)
    uint8_t fuIndicator = (*data & 0xE0) | 28; // FU-A
    uint8_t fuHeader = *data & 0x1F;

    data += 1; // Skip NAL Unit Type byte
    length -= 1;
    
    size_t payloadLength = 0;
    fuHeader |= kFUStart;
    while (length > 0)
    {
        if (kRtpPacketHeaderSize + kH264FUHeaderSize + length <= kMaxRtpPacketPayload)
        {
            assert(0 == (fuHeader & kFUStart));
            fuHeader = kFUEnd | (fuHeader & 0x1F); // FU-A end
            payloadLength = length;
        }
        else
        {
            payloadLength = kMaxRtpPacketPayload - kRtpPacketHeaderSize - kH264FUHeaderSize;
        }

        auto* buffer = new uint8_t[kRtpPacketHeaderSize + kH264FUHeaderSize + payloadLength];
        auto* header = reinterpret_cast<RtpPacket::Header*>(buffer);
        H264RtpHeaderInit(header);
        header->marker = 0;
        header->sequenceNumber = 0;
        header->timestamp = htonl(timestamp);
        header->ssrc = htonl(ssrc);
        size_t payloadOffset = kRtpPacketHeaderSize + kH264FUHeaderSize;
        std::memcpy(buffer + payloadOffset, data, payloadLength);

        /*fu_indicator + fu_header*/
        buffer[kRtpPacketHeaderSize + 0] = fuIndicator;
        buffer[kRtpPacketHeaderSize + 1] = fuHeader;

        data += payloadLength;
        length -= payloadLength;
        fuHeader &= 0x1F; // Clear flags
        
        auto* packet = reinterpret_cast<RtpPacket*>(buffer);
        result.push_back(packet);
    }

    return result;
}

std::vector<RtpPacket*> RtpPacketPacker::H264Pack(const uint8_t* data, size_t length, uint16_t startSequenceNumber, uint16_t endSequenceNumber, uint32_t timestamp, uint32_t ssrc)
{
    // data 包含 0x00000001，且至有 5 个字节。
    std::vector<RtpPacket *> result;
    if(length <= 4) return result;

    const std::vector<BlockBuffer> nalus = H264FindNALUs(data, length);
    if(nalus.size() == 0) return result;
    
    size_t freeSize = 0; // 尚未处理的单元的长度
    ssize_t naluCount = 0; // 尚未处理的单元的数量
    for(size_t i = 0; i < nalus.size(); i++) {
        freeSize += nalus[i].len;
        naluCount++;
        
        // 如果长度已经足够或超标，则打包
        while (freeSize + kRtpPacketHeaderSize >= kMaxRtpPacketPayload) // 每次 for 循环， while 循环最多迭代两次。
        {
            if(naluCount == 1)
            {
                // 本单元导致的足够或超标
                if(freeSize + kRtpPacketHeaderSize <= kMaxRtpPacketPayload)
                {
                    // Single NAL unit packet.
                    auto* packet = H264PackNALU(nalus[i], timestamp, ssrc);
                    result.push_back(packet);
                }
                else
                {
                    // Aggreation packet.
                    // FU-A
                    auto packets = H264PackFUA(nalus[i], timestamp, ssrc);
                    for (auto iter = packets.cbegin(); iter != packets.cend(); iter++)
                    {
                        result.push_back(*iter);
                    }
                }
                
                freeSize = 0;
                naluCount = 0;
                break;
            }
            else
            {
                if(freeSize + kRtpPacketHeaderSize == kMaxRtpPacketPayload)
                {
                    // 本单元和之前单元刚好凑够一包
                    
                    // Aggreation packet.
                    // STAP-A
                    std::vector<BlockBuffer> aggreationNALUs;
                    for (size_t j = i - naluCount; j <= i; j++) {
                        aggreationNALUs.push_back(nalus[j]);
                    }
                    auto* packet = H264PackSTAPA(aggreationNALUs, timestamp, ssrc);
                    result.push_back(packet);

                    freeSize = 0;
                    naluCount = 0;
                    break;
                }
                else
                {
                    // 本单元导致的超标
                    
                    // Aggreation packet.
                    // STAP-A
                    std::vector<BlockBuffer> aggreationNALUs;
                    for (size_t j = i - naluCount; j <= i; j++) {
                        aggreationNALUs.push_back(nalus[j]);
                    }
                    auto* packet = H264PackSTAPA(aggreationNALUs, timestamp, ssrc);
                    result.push_back(packet);
                    
                    // 如果本单元是最后一个单元，则直接处理
                    if(i == nalus.size() - 1)
                    {
                        if(freeSize + kRtpPacketHeaderSize <= kMaxRtpPacketPayload)
                        {
                            // Single NAL unit packet.
                            auto* packet = H264PackNALU(nalus[i], timestamp, ssrc);
                            result.push_back(packet);
                        }
                        else
                        {
                            // Aggreation packet.
                            // FU-A
                            auto packets = H264PackFUA(nalus[i], timestamp, ssrc);
                            for (auto iter = packets.cbegin(); iter != packets.cend(); iter++)
                            {
                                result.push_back(*iter);
                            }
                        }
                        freeSize = 0;
                        naluCount = 0;
                        break;
                    }
                    else
                    {
                        // 如果本单元足够或超标，会在循环的下一次迭代处理;否则继续保留着。
                        freeSize = nalus[i].len;
                        naluCount = 1;
                    }
                }
            }
        }
    }
    
    if(result.size() > 0)
    {
        // TODO:
        // 如果 result.size() 大于 (endSequenceNumber - startSequenceNumber + 1) 抛出异常。
        // 设置 sequenceNumber 以及最后一包的 marker。如有必要，创建空包。
    }
        
    return result;
}

void RtpPacketPacker::H264RtpHeaderInit(RtpPacket::Header* header)
{
    header->version = 2;
    header->padding = 0;
    header->extension = 0;
    header->csrcCount = 0;
    header->marker = 0;
    header->payloadType = 98;
    header->sequenceNumber = 0;
    header->timestamp = 0;
    header->ssrc = 0;
}

void RtpPacketPacker::G711ARtpHeaderInit(RtpPacket::Header* header)
{
    header->version = 2;
    header->padding = 0;
    header->extension = 0;
    header->csrcCount = 0;
    header->marker = 0;
    header->payloadType = 8;
    header->sequenceNumber = 0;
    header->timestamp = 0;
    header->ssrc = 0;
}

} // namespace RTC
