#define MS_CLASS "RTC::PsRtpPacketProcessor"

#include "PsRtpPacketProcessor.hpp"
#include "Logger.hpp"
#include "Settings.hpp"
#include "PsRtpPacket.h"
#include "RtpPacketPacker.hpp"

namespace RTC
{

constexpr size_t kMaxVideoFrameBufferBytes = 1024 * 1024;
constexpr size_t kMaxAudioFrameBufferBytes = 1024 * 1024;
constexpr size_t kStartPacketBufferSize    = 128;
constexpr size_t kMaxPacketBufferSize      = 1024;

/* Public methods. */

PsRtpPacketProcessor::PsRtpPacketProcessor()
{
    this->psRtpPacketBuffer = new PsRtpPacketBuffer(kStartPacketBufferSize, kMaxPacketBufferSize);
    this->videoFrameBuffer = new uint8_t[kMaxVideoFrameBufferBytes];
    this->audioFrameBuffer = new uint8_t[kMaxAudioFrameBufferBytes];
}

PsRtpPacketProcessor::~PsRtpPacketProcessor()
{
    delete this->psRtpPacketBuffer;
    this->psRtpPacketBuffer = nullptr;
    
    delete[] this->videoFrameBuffer;
    this->videoFrameBuffer = nullptr;
    
    delete[] this->audioFrameBuffer;
    this->audioFrameBuffer = nullptr;
}

std::vector<RtpPacket*> PsRtpPacketProcessor::InsertRtpPacket(const RtpPacket* rtp_packet)
{
    std::vector<RtpPacket*> result;
    
    PsRtpPacketBuffer::InsertResult insertResult;
    if(rtp_packet->GetPayloadLength() > 0)
    {
        std::unique_ptr<PsRtpPacketBuffer::Packet> packet(new PsRtpPacketBuffer::Packet(rtp_packet));
        insertResult = this->psRtpPacketBuffer->InsertPacket(std::move(packet));
    }
    else
    {
        // TODO：如果新的一帧的第一个包或前几包没有 payload，在重新打包 RtpPacket 的时候会导致 seq_num 与前一帧的包不连续。
        insertResult = this->psRtpPacketBuffer->InsertPadding(rtp_packet->GetSequenceNumber());
    }

    auto& packets = insertResult.packets;
    if(!packets.empty())
    {
        Demux(packets);
        // ClearTo 用于清理旧包，比如在上一帧处理后收到的重复包。
        this->psRtpPacketBuffer->ClearTo(packets[packets.size() - 1]->seq_num);
        if(this->videoFrameBufferOffset == 0 && this->audioFrameBufferOffset == 0)
        {
            MS_WARN_TAG(rtp, "Too many empty packets.");
            return result;
        }
//        MS_DEBUG_TAG(rtp, "New H.264 frame: %02X %02X %02X %02X %02X",
//                     this->videoFrameBuffer[0],
//                     this->videoFrameBuffer[1],
//                     this->videoFrameBuffer[2],
//                     this->videoFrameBuffer[3],
//                     this->videoFrameBuffer[4]
//                     );
//        auto newRtpPacketCount = packets[packets.size() - 1]->seq_num - packets[0]->seq_num;
        
        if(this->videoFrameBufferOffset > 0)
        {
            std::vector<RtpPacket*> newVideoPackets = RtpPacketPacker::H264Pack(this->videoFrameBuffer,
                                                             this->videoFrameBufferOffset,
                                                             packets[0]->seq_num,
                                                             packets[packets.size() - 1]->seq_num,
                                                             packets[0]->timestamp,
                                                             packets[0]->ssrc);
            
            for (auto& entry : newVideoPackets)
            {
                result.push_back(entry);
            }
            
            // delete 收到的、已经解析的 RtpPacket 及内部的 payload
            for (auto& entry : packets)
            {
                delete[] entry->rtp_packet->GetData();
                delete entry->rtp_packet;
                entry->rtp_packet = nullptr;
                entry = nullptr;
            }
        }
    }
    
    return result;
}

void PsRtpPacketProcessor::Demux(const std::vector<std::unique_ptr<PsRtpPacketBuffer::Packet>>& packets)
{
    this->videoFrameBufferOffset = 0;
    this->audioFrameBufferOffset = 0;
    auto* demuxNextPacketReadState = new DemuxNextPacketReadState(DemuxNextPacketReadMode::Guest, 0);
    for (auto& entry : packets)
    {
        Demux(entry->rtp_packet, demuxNextPacketReadState);
    }
    delete demuxNextPacketReadState;
    demuxNextPacketReadState = nullptr;
}

void PsRtpPacketProcessor::Demux(const RtpPacket* rtp_packet, DemuxNextPacketReadState* demuxNextPacketReadState)
{
    uint8_t* payload      = rtp_packet->GetPayload();
    size_t payloadLength  = rtp_packet->GetPayloadLength();
    uint8_t* processPtr   = payload;
    size_t completeLength = 0;

    if(payloadLength == 0)
    {
        // Padding
        return;
    }
    
    if(demuxNextPacketReadState->demuxNextPacketReadBytes > 0)
    {
        // 继续上次未读完的帧
        size_t read = demuxNextPacketReadState->demuxNextPacketReadBytes <= payloadLength ?
                        demuxNextPacketReadState->demuxNextPacketReadBytes : payloadLength;
        if(demuxNextPacketReadState->demuxNextPacketReadMode == DemuxNextPacketReadMode::ReadVideo)
        {
            FetchData(&processPtr, read, demuxNextPacketReadState, &completeLength);
        }
        else if(demuxNextPacketReadState->demuxNextPacketReadMode == DemuxNextPacketReadMode::ReadAudio)
        {
            FetchData(&processPtr, read, demuxNextPacketReadState, &completeLength);
        }
        else
        {
            MS_WARN_TAG(rtp, "Not supported DemuxNextPacketReadMode::Guest.");
        }
        // 读完本包还不够
        if(demuxNextPacketReadState->demuxNextPacketReadBytes > 0)
        {
            return;
        }
    }
    
    while (processPtr < payload + payloadLength - sizeof(PsPacketStartCode))
    {
        if (processPtr[0] == 0x00
            && processPtr[1] == 0x00
            && processPtr[2] == 0x01
            && processPtr[3] == 0xBA)
        {
            // PS Header（通常不会跨包）
            PsHeader *header = reinterpret_cast<PsHeader*>(processPtr);
            header->packStuffingLength = ntohs(header->packStuffingLength);
            
            processPtr += sizeof(PsHeader);
            processPtr += header->packStuffingLength;
            completeLength += sizeof(PsHeader) + header->packStuffingLength;
        }
        else if(processPtr[0] == 0x00
                && processPtr[1] == 0x00
                && processPtr[2] == 0x01
                && processPtr[3] == 0xBB)
        {
            // PS System Header（通常不会跨包）
            PsSystemHeaderPrefix *header = reinterpret_cast<PsSystemHeaderPrefix*>(processPtr);
            header->headerLength = ntohs(header->headerLength);

            processPtr += sizeof(PsSystemHeaderPrefix);
            processPtr += header->headerLength;
            completeLength += sizeof(PsSystemHeaderPrefix) + header->headerLength;
        }
        else if(processPtr[0] == 0x00
                && processPtr[1] == 0x00
                && processPtr[2] == 0x01
                && processPtr[3] == 0xBC)
        {
             // PSM Header prefix（通常不会跨包）
            PsPSMHeaderPrefix* header = reinterpret_cast<PsPSMHeaderPrefix*>(processPtr);
            header->programStreamMapLength = ntohs(header->programStreamMapLength);
            
            processPtr += sizeof(PsPSMHeaderPrefix);
            completeLength += sizeof(PsPSMHeaderPrefix);

            auto* psm = processPtr;
            
            // Parse PSM
            psm += 2; // Skip 2 bytes
            
            // program_stream_info (Skip)
            uint16_t programStreamInfoLength = ntohs(*reinterpret_cast<uint16_t*>(psm));
            psm += sizeof(programStreamInfoLength);
            psm += programStreamInfoLength;
            
            // elementary_stream_map
            uint16_t elementaryStreamMapLength = ntohs(*reinterpret_cast<uint16_t*>(psm));
            psm += sizeof(elementaryStreamMapLength);
            // /* at least one es available? */
            while (elementaryStreamMapLength >= 4)
            {
                PsPSMElementaryStreamMap* elementaryStreamMap = reinterpret_cast<PsPSMElementaryStreamMap*>(psm);
                elementaryStreamMap->elementaryStreamInfoLength = ntohs(elementaryStreamMap->elementaryStreamInfoLength);
                psm += sizeof(PsPSMElementaryStreamMap);
                psm += elementaryStreamMap->elementaryStreamInfoLength;
                elementaryStreamMapLength -= sizeof(PsPSMElementaryStreamMap) + elementaryStreamMap->elementaryStreamInfoLength;
                
                /* remember mapping from stream id to stream type */
                if (elementaryStreamMap->elementaryStreamId >= PS_AUDIO_ID && elementaryStreamMap->elementaryStreamId <= PS_AUDIO_ID_END)
                {
                    if (this->audioStreamType != elementaryStreamMap->streamType || this->audioElementaryStreamId != elementaryStreamMap->elementaryStreamId)
                    {
                        MS_DEBUG_TAG(rtp, "PS map audio streamType=%s(%02x), elementaryStreamId=%02x, elementaryStreamInfoLength=%d",
                                     GetPSMapTypeString(elementaryStreamMap->streamType).c_str(),
                                     elementaryStreamMap->streamType,
                                     elementaryStreamMap->elementaryStreamId,
                                     elementaryStreamMap->elementaryStreamInfoLength);
                        
                        this->audioStreamType = elementaryStreamMap->streamType;
                        this->audioElementaryStreamId = elementaryStreamMap->elementaryStreamId;
                    }
                }
                else if (elementaryStreamMap->elementaryStreamId >= PS_VIDEO_ID && elementaryStreamMap->elementaryStreamId <= PS_VIDEO_ID_END)
                {
                    if (this->videoStreamType != elementaryStreamMap->streamType || this->videoElementaryStreamId != elementaryStreamMap->elementaryStreamId)
                    {
                        MS_DEBUG_TAG(rtp, "PS map video streamType=%s(%02x), elementaryStreamId=%02x, elementaryStreamInfoLength=%d",
                                     GetPSMapTypeString(elementaryStreamMap->streamType).c_str(),
                                     elementaryStreamMap->streamType,
                                     elementaryStreamMap->elementaryStreamId,
                                     elementaryStreamMap->elementaryStreamInfoLength);
                        this->videoStreamType = elementaryStreamMap->streamType;
                        this->videoElementaryStreamId = elementaryStreamMap->elementaryStreamId;
                    }
                }
            }
            
            processPtr += header->programStreamMapLength; // programStreamMapLength 不包含自身
            completeLength += header->programStreamMapLength;
        }
        else if(processPtr[0] == 0x00
                && processPtr[1] == 0x00
                && processPtr[2] == 0x01
                && processPtr[3] == 0xBD)
        {
            // Private Stream（通常不会跨包）
            PsePacketHeaderPrefix* header = reinterpret_cast<PsePacketHeaderPrefix*>(processPtr);
            header->pesPacketLength = ntohs(header->pesPacketLength);

            size_t pesPayloadLength = header->pesPacketLength - (sizeof(header->info) + sizeof(header->pesHeaderDataLength) + header->pesHeaderDataLength);
            processPtr += sizeof(PsePacketHeaderPrefix) + header->pesHeaderDataLength + pesPayloadLength;
            completeLength += sizeof(PsePacketHeaderPrefix) + header->pesHeaderDataLength + pesPayloadLength;
            
        }
        else if(processPtr[0] == 0x00
                && processPtr[1] == 0x00
                && processPtr[2] == 0x01
                && processPtr[3] == 0xE0)
        {
            // PES video stream
            PsePacketHeaderPrefix* header = reinterpret_cast<PsePacketHeaderPrefix*>(processPtr);
            header->pesPacketLength = ntohs(header->pesPacketLength);

            size_t pesBodyLength = header->pesPacketLength - (sizeof(header->info) + sizeof(header->pesHeaderDataLength) + header->pesHeaderDataLength);
            processPtr += sizeof(PsePacketHeaderPrefix);
            processPtr += header->pesHeaderDataLength;  // 指向 PES Body
            completeLength += sizeof(PsePacketHeaderPrefix) + header->pesHeaderDataLength;

            size_t read = completeLength + pesBodyLength <= payloadLength ? pesBodyLength : payloadLength - completeLength;
            demuxNextPacketReadState->demuxNextPacketReadMode = DemuxNextPacketReadMode::ReadVideo;
            demuxNextPacketReadState->demuxNextPacketReadBytes = pesBodyLength;
            FetchData(&processPtr, read, demuxNextPacketReadState, &completeLength);
            if(demuxNextPacketReadState->demuxNextPacketReadBytes > 0)
            {
                assert(processPtr == payload + payloadLength);
                return;
            }
        }
        else if(processPtr[0] == 0x00
                && processPtr[1] == 0x00
                && processPtr[2] == 0x01
                && processPtr[3] == 0xC0)
         {
            // PES audio stream
             PsePacketHeaderPrefix* header = reinterpret_cast<PsePacketHeaderPrefix*>(processPtr);
             header->pesPacketLength = ntohs(header->pesPacketLength);

             size_t pesBodyLength = header->pesPacketLength - (sizeof(header->info) + sizeof(header->pesHeaderDataLength) + header->pesHeaderDataLength);
             processPtr += sizeof(PsePacketHeaderPrefix);
             processPtr += header->pesHeaderDataLength; // 指向 PES Body
             completeLength += sizeof(PsePacketHeaderPrefix) + header->pesHeaderDataLength;

             size_t read = completeLength + pesBodyLength <= payloadLength ? pesBodyLength : payloadLength - completeLength;
             demuxNextPacketReadState->demuxNextPacketReadMode = DemuxNextPacketReadMode::ReadAudio;
             demuxNextPacketReadState->demuxNextPacketReadBytes = pesBodyLength;
             FetchData(&processPtr, read, demuxNextPacketReadState, &completeLength);
             if(demuxNextPacketReadState->demuxNextPacketReadBytes > 0)
             {
                 assert(processPtr == payload + payloadLength);
                 return;
             }
        }
        else
        {
             MS_WARN_TAG(rtp, "Unknow PS data.");
             return;
        }
    }
}

void PsRtpPacketProcessor::FetchData(uint8_t** pesBody,
                                     size_t read,
                                     DemuxNextPacketReadState* demuxNextPacketReadState,
                                     size_t* completeLength)
{
    assert(demuxNextPacketReadState->demuxNextPacketReadMode != DemuxNextPacketReadMode::Guest);

    if(read > 0)
    {
        if(demuxNextPacketReadState->demuxNextPacketReadMode == DemuxNextPacketReadMode::ReadVideo)
        {
            std::memcpy(this->videoFrameBuffer + this->videoFrameBufferOffset, *pesBody, read);
            this->videoFrameBufferOffset += read;
        }
        else
        {
            std::memcpy(this->audioFrameBuffer + this->audioFrameBufferOffset, *pesBody, read);
            this->audioFrameBufferOffset += read;
        }

        *pesBody += read;
        *completeLength += read;
        demuxNextPacketReadState->demuxNextPacketReadBytes -= read;
        if(demuxNextPacketReadState->demuxNextPacketReadBytes == 0)
        {
            demuxNextPacketReadState->demuxNextPacketReadMode = DemuxNextPacketReadMode::Guest;
            return;
        }
    }
    else if(read == 0)
    {
        // 本包没有数据
        return;
    }
}

/* Class methods. */

std::string PsRtpPacketProcessor::GetPSMapTypeString(uint8_t type)
{
    switch(type){
        case STREAM_TYPE_VIDEO_MPEG1:       // 0x01
           return "mpeg1";
        case STREAM_TYPE_VIDEO_MPEG2:       // 0x02
            return "mpeg2";
        case STREAM_TYPE_AUDIO_MPEG1:       // 0x03
            return "mpeg1";
        case STREAM_TYPE_AUDIO_MPEG2:       // 0x04
            return "mpeg2";
        case STREAM_TYPE_PRIVATE_SECTION:   // 0x05
            return "private_section";
        case STREAM_TYPE_PRIVATE_DATA:      // 0x06
            return "private_data";
        case STREAM_TYPE_AUDIO_AAC:         // 0x0f
            return "aac";
        case STREAM_TYPE_VIDEO_MPEG4:       // 0x10
            return "mpeg4";
        case STREAM_TYPE_VIDEO_H264:        // 0x1b
            return "h264";
        case STREAM_TYPE_VIDEO_HEVC:        // 0x24
            return "hevc";
        case STREAM_TYPE_VIDEO_CAVS:        // 0x42
            return "cavs";
        case STREAM_TYPE_VIDEO_SAVC:        // 0x80
            return "savc";
        case STREAM_TYPE_AUDIO_AC3:         // 0x81
            return "ac3";
        case STREAM_TYPE_AUDIO_G711:        // 0x90
            return "g711";
        case STREAM_TYPE_AUDIO_G711ULAW:    // 0x91
            return "g711ulaw";
        case STREAM_TYPE_AUDIO_G722_1:      // 0x92
            return "g722_1";
        case STREAM_TYPE_AUDIO_G723_1:      // 0x93
            return "g723_1";
        case STREAM_TYPE_AUDIO_G726:        // 0x96
            return "g726";
        case STREAM_TYPE_AUDIO_G729_1:      // 0x99
            return "g729_1";
        case STREAM_TYPE_AUDIO_SVAC:        // 0x9b
            return "svac";
        case STREAM_TYPE_AUDIO_PCM:         // 0x9c
            return "pcm";
        default:
            return "unknow";
    }
}

} // namespace RTC
