#ifndef PsRtpPacketBuffer_hpp
#define PsRtpPacketBuffer_hpp

#include <cstdint>    // uint8_t, etc
#include <set>
#include "RTC/RtpPacket.hpp"
#include "Extensions/Utils/SequenceNumberUtils.h"

namespace RTC {

// -----------------------------------------------------------
// File: webrtc/src/modules/video_coding/packet_buffer.h
// -----------------------------------------------------------

class PsRtpPacketBuffer {
    
public:
    struct Packet {
        Packet(const RtpPacket* rtp_packet);
        Packet(const Packet&) = delete;
        Packet(Packet&&) = delete;
        Packet& operator=(const Packet&) = delete;
        Packet& operator=(Packet&&) = delete;
        ~Packet() = default;

        bool is_first_packet_in_frame = false;
        bool is_last_packet_in_frame = false;

        // If all its previous packets have been inserted into the packet buffer.
        // Set and used internally by the PacketBuffer.
        bool continuous = false;
        bool marker_bit = false;
        uint16_t seq_num = 0;
        uint32_t timestamp = 0;
        uint32_t ssrc = 0;

        const RtpPacket* rtp_packet;
    };
    struct InsertResult {
        std::vector<std::unique_ptr<Packet>> packets;
        // Indicates if the packet buffer was cleared, which means that a key
        // frame request should be sent.
        bool buffer_cleared = false;
    };

    // Both |start_buffer_size| and |max_buffer_size| must be a power of 2.
    PsRtpPacketBuffer(size_t start_buffer_size, size_t max_buffer_size);
    ~PsRtpPacketBuffer();
    
    InsertResult InsertPacket(std::unique_ptr<Packet> packet);
    InsertResult InsertPadding(uint16_t seq_num);
    void ClearTo(uint16_t seq_num);
    void Clear();
    
    // Timestamp (not RTP timestamp) of the last received packet/keyframe packet.
    int64_t LastReceivedPacketMs() const;
    int64_t LastReceivedKeyframePacketMs() const;

private:
     // Clears with |mutex_| taken.
    void ClearInternal();
     // Tries to expand the buffer.
    bool ExpandBufferSize();

     // Test if all previous packets has arrived for the given sequence number.
     bool PotentialNewFrame(uint16_t seq_num) const;

     // Test if all packets of a frame has arrived, and if so, returns packets to
     // create frames.
     std::vector<std::unique_ptr<Packet>> FindFrames(uint16_t seq_num);

     void UpdateMissingPackets(uint16_t seq_num);

     // buffer_.size() and max_size_ must always be a power of two.
     const size_t max_size_;

     // The fist sequence number currently in the buffer.
     uint16_t first_seq_num_;

     // If the packet buffer has received its first packet.
     bool first_packet_received_;

     // If the buffer is cleared to |first_seq_num_|.
     bool is_cleared_to_first_seq_num_;

     // Buffer that holds the the inserted packets and information needed to
     // determine continuity between them.
     std::vector<std::unique_ptr<Packet>> buffer_;

     // Timestamp of the last received packet/keyframe packet.
     int64_t last_received_packet_ms_;
     int64_t last_received_keyframe_packet_ms_;
     uint32_t last_received_keyframe_rtp_timestamp_;

     uint16_t newest_inserted_seq_num_;
     std::set<uint16_t, DescendingSeqNumComp<uint16_t>> missing_packets_;
};

} // namespace RTC

#endif /* PsRtpPacketBuffer_hpp */

