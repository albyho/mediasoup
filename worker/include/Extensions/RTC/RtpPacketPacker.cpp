#define MS_CLASS "RTC::RtpPacketPacker"

#include "RtpPacketPacker.hpp"
#include "Logger.hpp"
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

const uint8_t* RtpPacketPacker::H264FindNALU(const uint8_t* data, size_t searchLength)
{
    // data 中的 NALU 包含 0x00000001
    assert(searchLength > 4);

    return H264FindNALU(data, data + searchLength);
}

const uint8_t* RtpPacketPacker::H264FindNALU(const uint8_t* data, const uint8_t* end)
{
    // data 中的 NALU 包含 0x00000001
    // end 指向 NALU 结束后的第一个字节
    assert(data + 4 < end);

    for(data += 3; data + 1 < end; data++)
    {
        if (0x01 == *data && 0x00 == *(data - 1) && 0x00 == *(data - 2) && 0x00 == *(data - 3))
        {
            return data + 1;
        }
    }
        
    return nullptr;
}

const std::vector<std::shared_ptr<BlockBuffer>> RtpPacketPacker::H264FindNALUs(const uint8_t* data, size_t searchLength)
{
    // data 中的 NALU 包含 0x00000001
    assert(searchLength > 4);

    return H264FindNALUs(data, data + searchLength);
}

const std::vector<std::shared_ptr<BlockBuffer>> RtpPacketPacker::H264FindNALUs(const uint8_t* data, const uint8_t* end)
{
    // data 中的 NALU 包含 0x00000001
    // end 指向 NALU 结束后的第一个字节
    std::vector<std::shared_ptr<BlockBuffer>> result;
    const uint8_t* ptr = data;
    std::shared_ptr<BlockBuffer> previous = nullptr;
    while (ptr < end) {
        auto* nalu = H264FindNALU(ptr, end);
        if(!nalu) {
            assert(result.size() > 0);
            break;
        }
        
        std::shared_ptr<BlockBuffer> blockBuffer(new BlockBuffer());
        blockBuffer->base = nalu;
        
        if(previous) {
            previous->len = blockBuffer->base - previous->base - 4;
        }
        previous = blockBuffer;

        result.push_back(blockBuffer);
        ptr = nalu + 1;
    }
    
    if(!result.empty())
    {
        result[result.size() - 1]->len = end - result[result.size() - 1]->base;
    }
    
    return result;
}

RtpPacket* RtpPacketPacker::H264PackNALU(const std::shared_ptr<BlockBuffer>& nalu, uint32_t timestamp, uint32_t ssrc)
{
    // data 不包含 0x00000001
    assert(0 < (*nalu->base & 0x1F) && (*nalu->base & 0x1F) < 24);
    assert(nalu->len > 0);

    size_t bufferLength = kRtpPacketHeaderSize/*sizeof(RtpPacket::Header)*/ + nalu->len;
    assert(bufferLength <= kMaxRtpPacketPayload);
    auto* buffer = new uint8_t[bufferLength];
    auto* header = reinterpret_cast<RtpPacket::Header*>(buffer);
    H264RtpHeaderInit(header);
    header->marker = 0;
    header->sequenceNumber = 0;
    header->timestamp = htonl(timestamp);
    header->ssrc = htonl(ssrc);
    std::memcpy(buffer + kRtpPacketHeaderSize/*sizeof(RtpPacket::Header)*/, nalu->base, nalu->len);

    auto* packet = RtpPacket::Parse(buffer, bufferLength);
    return packet;
}

RtpPacket* RtpPacketPacker::H264PackSTAPA(const std::vector<std::shared_ptr<BlockBuffer>>& nalus, uint32_t timestamp, uint32_t ssrc)
{
    // data 不包含 0x00000001
    assert(nalus.size() > 0);
    size_t bufferLength = kRtpPacketHeaderSize + kH264STAPAHeaderSize;
    for (auto iter = nalus.cbegin(); iter != nalus.cend(); iter++)
    {
        assert(0 < (*((*iter)->base) & 0x1F) && (*((*iter)->base) & 0x1F) < 24);
        bufferLength += sizeof(uint16_t) + (*iter)->len;
    }
    assert(bufferLength <= kMaxRtpPacketPayload);
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
        uint8_t* p = (uint8_t*)&(*iter)->len;
        buffer[payloadOffset + 0] = p[1];
        buffer[payloadOffset + 1] = p[0];
        payloadOffset += 2;
        std::memcpy(buffer + payloadOffset, (*iter)->base, (*iter)->len);
        payloadOffset += (*iter)->len;
    }
    
    auto* packet = RtpPacket::Parse(buffer, bufferLength);
    return packet;
}

std::vector<RtpPacket *> RtpPacketPacker::H264PackFUA(const std::shared_ptr<BlockBuffer>& nalu, uint32_t timestamp, uint32_t ssrc)
{
    // data 不包含 0x00000001
    assert(0 < (*nalu->base & 0x1F) && (*nalu->base & 0x1F) < 24);
    assert(nalu->len > 0);

    std::vector<RtpPacket *> result;

    uint8_t* data = const_cast<uint8_t*>(nalu->base);
    size_t length = nalu->len;
    
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

        size_t bufferLength = kRtpPacketHeaderSize + kH264FUHeaderSize + payloadLength;
        assert(bufferLength <= kMaxRtpPacketPayload);
        auto* buffer = new uint8_t[bufferLength];
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
        
        auto* packet = RtpPacket::Parse(buffer, bufferLength);
        result.push_back(packet);
    }

    return result;
}

std::vector<RtpPacket*> RtpPacketPacker::H264Pack(const uint8_t* data, size_t length, uint16_t startSequenceNumber, uint16_t endSequenceNumber, uint32_t timestamp, uint32_t ssrc)
{
    // data 包含 0x00000001，且至有 5 个字节。
    assert(length > 4);
    std::vector<RtpPacket*> result;

    const std::vector<std::shared_ptr<BlockBuffer>> nalus = H264FindNALUs(data, length);
    if(nalus.size() == 0) return result;
    
    size_t freeSize = 0; // 尚未处理的单元的长度。
    size_t naluCount = 0; // 尚未处理的单元的数量
    for(size_t i = 0; i < nalus.size(); i++) {
        freeSize += nalus[i]->len;
        naluCount++;
        
        if(naluCount == 1) {
            // 刚好够单包，或者不够却是最后一包
            if(
               (kRtpPacketHeaderSize + freeSize == kMaxRtpPacketPayload) ||
               (kRtpPacketHeaderSize + freeSize < kMaxRtpPacketPayload && i == (nalus.size() - 1))
              )
            {
                // 处理当前包
                // Single NAL unit packet.
                auto* packet = H264PackNALU(nalus[i], timestamp, ssrc);
                result.push_back(packet);
                
                freeSize = 0;
                naluCount = 0;
            }
            // 包已超标（不管是不是最后一包）
            else if(kRtpPacketHeaderSize + freeSize > kMaxRtpPacketPayload)
            {
                // 处理当前包
                // Aggreation packet.
                // FU-A
                auto packets = H264PackFUA(nalus[i], timestamp, ssrc);
                for (auto iter = packets.cbegin(); iter != packets.cend(); iter++)
                {
                    result.push_back(*iter);
                }
                
                freeSize = 0;
                naluCount = 0;
            }
        }
        else
        {
            // 刚好够单包，或者不够却是最后一包
            if (
                (kRtpPacketHeaderSize + kH264STAPAHeaderSize + freeSize + (sizeof(uint16_t) * naluCount) == kMaxRtpPacketPayload) ||
                ((kRtpPacketHeaderSize + kH264STAPAHeaderSize + freeSize + (sizeof(uint16_t) * naluCount) < kMaxRtpPacketPayload) && (i == nalus.size() - 1))
               )
            {
                // 处理当前和前面的所有包
                // Aggreation packet.
                // STAP-A
                std::vector<std::shared_ptr<BlockBuffer>> aggreationNALUs;
                for (size_t j = i - (naluCount - 1); j <= i/*包含本包*/; j++) {
                    aggreationNALUs.push_back(nalus[j]);
                }
                auto* packet = H264PackSTAPA(aggreationNALUs, timestamp, ssrc);
                result.push_back(packet);

                freeSize = 0;
                naluCount = 0;
            }
            // 包已超标（如果是当前包是最后一包会有特殊处理）
            else if (kRtpPacketHeaderSize + kH264STAPAHeaderSize + freeSize + (sizeof(uint16_t) * naluCount) > kMaxRtpPacketPayload)
            {
                if(naluCount - 1 == 1)
                {
                    // 处理前面唯一的一包，本包暂不处理(前一包不会超标)
                    // Single NAL unit packet.
                    auto* packet = H264PackNALU(nalus[i - 1], timestamp, ssrc);
                    result.push_back(packet);
                }
                else
                {
                    // 处理前面的所有包，本包暂不处理
                    // Aggreation packet.
                    // STAP-A
                    std::vector<std::shared_ptr<BlockBuffer>> aggreationNALUs;
                    for (size_t j = i - (naluCount - 1); j < i/*不包含本包*/; j++) {
                        aggreationNALUs.push_back(nalus[j]);
                    }
                    auto* packet = H264PackSTAPA(aggreationNALUs, timestamp, ssrc);
                    result.push_back(packet);
                }
                
                // 因为还没有处理本包，而本包有可能超标还有可能是最后一包，所以重新计算本包。
                freeSize = 0;
                naluCount = 0;
                i--;
            }
        }
    }
    
    if(!result.empty())
    {
        size_t packetCount = (endSequenceNumber - startSequenceNumber + 1);
        // 如果 result.size() 大于 packetCount 说明序列号会发生了超标。
        if(result.size() > packetCount)
        {
            MS_WARN_TAG(rtp, "Too many new packets.");
            for (auto iter = result.cbegin(); iter != result.cend(); iter++)
            {
                delete[] (*iter)->GetData();
                delete *iter;
            }
            std::vector<RtpPacket *> emptyResult;
            return emptyResult;
        }
        // 设置所有包的 sequenceNumber 以及最后一包的 marker。如有必要，创建空包以保证新生成的包的数量。
        size_t index = 0;
        for(uint16_t i = startSequenceNumber; i <= endSequenceNumber; i++)
        {
            if(index < result.size()) {
                result[index]->SetSequenceNumber(i);
            } else {
                auto* paddingRtpPacket = GeneratePaddingH264RtpPacket(i, timestamp, ssrc);
                result.push_back(paddingRtpPacket);
            }
            index++;
        }
        result[result.size() - 1]->SetMarker(true);
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

RtpPacket* RtpPacketPacker::GeneratePaddingH264RtpPacket(uint16_t sequenceNumber, uint32_t timestamp, uint32_t ssrc)
{
    auto* buffer = new uint8_t[kRtpPacketHeaderSize];
    auto* header = reinterpret_cast<RtpPacket::Header*>(buffer);
    H264RtpHeaderInit(header);
    header->marker = 0;
    header->sequenceNumber = htons(sequenceNumber);
    header->timestamp = htonl(timestamp);
    header->ssrc = htonl(ssrc);
    
    auto* packet = RtpPacket::Parse(buffer, kRtpPacketHeaderSize);
    return packet;
}

} // namespace RTC
