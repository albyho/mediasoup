#ifndef PsRtpPacketProcessor_hpp
#define PsRtpPacketProcessor_hpp

#include <cstdint>    // uint8_t, etc
#include <memory>
#include "RTC/RtpPacket.hpp"
#include "PsRtpPacketBuffer.hpp"

#define PS_Payload_Type 96u

#define PS_AUDIO_ID 0xc0
#define PS_AUDIO_ID_END 0xdf
#define PS_VIDEO_ID 0xe0
#define PS_VIDEO_ID_END 0xef

#define STREAM_TYPE_VIDEO_MPEG1     0x01
#define STREAM_TYPE_VIDEO_MPEG2     0x02
#define STREAM_TYPE_AUDIO_MPEG1     0x03
#define STREAM_TYPE_AUDIO_MPEG2     0x04
#define STREAM_TYPE_PRIVATE_SECTION 0x05
#define STREAM_TYPE_PRIVATE_DATA    0x06
#define STREAM_TYPE_AUDIO_AAC       0x0f
#define STREAM_TYPE_VIDEO_MPEG4     0x10
#define STREAM_TYPE_VIDEO_H264      0x1b
#define STREAM_TYPE_VIDEO_HEVC      0x24
#define STREAM_TYPE_VIDEO_CAVS      0x42
#define STREAM_TYPE_VIDEO_SAVC      0x80
#define STREAM_TYPE_AUDIO_AC3       0x81
#define STREAM_TYPE_AUDIO_G711      0x90
#define STREAM_TYPE_AUDIO_G711ULAW  0x91
#define STREAM_TYPE_AUDIO_G722_1    0x92
#define STREAM_TYPE_AUDIO_G723_1    0x93
#define STREAM_TYPE_AUDIO_G726      0x96
#define STREAM_TYPE_AUDIO_G729_1    0x99
#define STREAM_TYPE_AUDIO_SVAC      0x9b
#define STREAM_TYPE_AUDIO_PCM       0x9c

namespace RTC
{
class PsRtpPacketProcessor
{
public:
    PsRtpPacketProcessor();
    ~PsRtpPacketProcessor();
    
public:
    std::vector<RtpPacket*> InsertRtpPacket(const RtpPacket* rtp_packet);
    static std::string GetPSMapTypeString(uint8_t type);

private:
    enum DemuxNextPacketReadMode { Guest, ReadVideo, ReadAudio };
    struct DemuxNextPacketReadState {
        DemuxNextPacketReadMode demuxNextPacketReadMode;
        size_t demuxNextPacketReadBytes;
        DemuxNextPacketReadState(DemuxNextPacketReadMode demuxNextPacketReadMode, size_t demuxNextPacketReadBytes) :
            demuxNextPacketReadMode(demuxNextPacketReadMode),
            demuxNextPacketReadBytes(demuxNextPacketReadBytes)
        {
            
        }
    };
    void Demux(const std::vector<std::unique_ptr<PsRtpPacketBuffer::Packet>>& packets);
    void Demux(const RtpPacket* rtp_packet, DemuxNextPacketReadState* demuxNextPacketReadState);
    void FetchData(uint8_t** pesBody,
                   size_t read,
                   DemuxNextPacketReadState* demuxNextPacketReadState,
                   size_t* completeLength);
    
private:
    PsRtpPacketBuffer* psRtpPacketBuffer;
    uint8_t* videoFrameBuffer;
    uint8_t* audioFrameBuffer;
    size_t videoFrameBufferOffset;
    size_t audioFrameBufferOffset;
    uint8_t videoStreamType;
    uint8_t audioStreamType;
    uint8_t videoElementaryStreamId;
    uint8_t audioElementaryStreamId;
};

} // namespace RTC

#endif /* PsRtpPacketProcessor_hpp */
