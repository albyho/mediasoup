#define MS_CLASS "RTC::PsRtpPacketBuffer"

#include "PsRtpPacketBuffer.hpp"
#include "DepLibUV.hpp"
#include "Logger.hpp"
#include "PsRtpPacket.h"

namespace RTC {

// -----------------------------------------------------------
// File: webrtc/src/modules/video_coding/packet_buffer.cc
// -----------------------------------------------------------

PsRtpPacketBuffer::Packet::Packet(const RtpPacket* rtp_packet)
    : is_last_packet_in_frame(rtp_packet->HasMarker()),
      seq_num(rtp_packet->GetSequenceNumber()),
      timestamp(rtp_packet->GetTimestamp()),
      rtp_packet(rtp_packet)
{
    auto* payload = rtp_packet->GetPayload();
    auto payloadLength = rtp_packet->GetPayloadLength();
    
    // if payload contains PS header, it's first packet in frame
    is_first_packet_in_frame = payloadLength > sizeof(PsPacketStartCode)
                                && payload[0] == 0x00
                                && payload[1] == 0x00
                                && payload[2] == 0x01
                                && payload[3] == 0xBA;
}

PsRtpPacketBuffer::PsRtpPacketBuffer(size_t start_buffer_size,
                           size_t max_buffer_size)
    : max_size_(max_buffer_size),
      first_seq_num_(0),
      first_packet_received_(false),
      is_cleared_to_first_seq_num_(false),
      buffer_(start_buffer_size) {
  //RTC_DCHECK_LE(start_buffer_size, max_buffer_size);
  // Buffer size must always be a power of 2.
  //RTC_DCHECK((start_buffer_size & (start_buffer_size - 1)) == 0);
  //RTC_DCHECK((max_buffer_size & (max_buffer_size - 1)) == 0);
}

PsRtpPacketBuffer::~PsRtpPacketBuffer() {
  Clear();
}

PsRtpPacketBuffer::InsertResult PsRtpPacketBuffer::InsertPacket(
    std::unique_ptr<PsRtpPacketBuffer::Packet> packet) {
  PsRtpPacketBuffer::InsertResult result;

  uint16_t seq_num = packet->seq_num;
  size_t index = seq_num % buffer_.size();

  if (!first_packet_received_) {
    first_seq_num_ = seq_num;
    first_packet_received_ = true;
  } else if (AheadOf(first_seq_num_, seq_num)) {
    // If we have explicitly cleared past this packet then it's old,
    // don't insert it, just silently ignore it.
    if (is_cleared_to_first_seq_num_) {
      return result;
    }

    first_seq_num_ = seq_num;
  }

  if (buffer_[index] != nullptr) {
    // Duplicate packet, just delete the payload.
    if (buffer_[index]->seq_num == packet->seq_num) {
      return result;
    }

    // The packet buffer is full, try to expand the buffer.
    while (ExpandBufferSize() && buffer_[seq_num % buffer_.size()] != nullptr) {
    }
    index = seq_num % buffer_.size();

    // Packet buffer is still full since we were unable to expand the buffer.
    if (buffer_[index] != nullptr) {
      // Clear the buffer, delete payload, and return false to signal that a
      // new keyframe is needed.
      MS_WARN_TAG(rtp, "Clear PacketBuffer and request key frame.");
      ClearInternal();
      result.buffer_cleared = true;
      return result;
    }
  }

  int64_t now_ms = DepLibUV::GetTimeMs();
  last_received_packet_ms_ = now_ms;
  if (packet->is_first_packet_in_frame ||
      last_received_keyframe_rtp_timestamp_ == packet->timestamp) {
    last_received_keyframe_packet_ms_ = now_ms;
    last_received_keyframe_rtp_timestamp_ = packet->timestamp;
  }

  packet->continuous = false;
  buffer_[index] = std::move(packet);

  UpdateMissingPackets(seq_num);

  result.packets = FindFrames(seq_num);
  return result;
}

void PsRtpPacketBuffer::ClearTo(uint16_t seq_num) {
  // We have already cleared past this sequence number, no need to do anything.
  if (is_cleared_to_first_seq_num_ &&
      AheadOf<uint16_t>(first_seq_num_, seq_num)) {
    return;
  }

  // If the packet buffer was cleared between a frame was created and returned.
  if (!first_packet_received_)
    return;

  // Avoid iterating over the buffer more than once by capping the number of
  // iterations to the |size_| of the buffer.
  ++seq_num;
  size_t diff = ForwardDiff<uint16_t>(first_seq_num_, seq_num);
  size_t iterations = std::min(diff, buffer_.size());
  for (size_t i = 0; i < iterations; ++i) {
    auto& stored = buffer_[first_seq_num_ % buffer_.size()];
    if (stored != nullptr && AheadOf<uint16_t>(seq_num, stored->seq_num)) {
      stored = nullptr;
    }
    ++first_seq_num_;
  }

  // If |diff| is larger than |iterations| it means that we don't increment
  // |first_seq_num_| until we reach |seq_num|, so we set it here.
  first_seq_num_ = seq_num;

  is_cleared_to_first_seq_num_ = true;
  auto clear_to_it = missing_packets_.upper_bound(seq_num);
  if (clear_to_it != missing_packets_.begin()) {
    --clear_to_it;
    missing_packets_.erase(missing_packets_.begin(), clear_to_it);
  }
}

void PsRtpPacketBuffer::Clear() {
  ClearInternal();
}

PsRtpPacketBuffer::InsertResult PsRtpPacketBuffer::InsertPadding(uint16_t seq_num) {
  PsRtpPacketBuffer::InsertResult result;
  UpdateMissingPackets(seq_num);
  result.packets = FindFrames(static_cast<uint16_t>(seq_num + 1));
  return result;
}

int64_t PsRtpPacketBuffer::LastReceivedPacketMs() const {
  return last_received_packet_ms_;
}

int64_t PsRtpPacketBuffer::LastReceivedKeyframePacketMs() const {
  return last_received_keyframe_packet_ms_;
}
void PsRtpPacketBuffer::ClearInternal() {
  for (auto& entry : buffer_) {
    entry = nullptr;
  }

  first_packet_received_ = false;
  is_cleared_to_first_seq_num_ = false;
  last_received_packet_ms_ = 0;
  last_received_keyframe_packet_ms_ = 0;
  newest_inserted_seq_num_ = 0;
  missing_packets_.clear();
}

bool PsRtpPacketBuffer::ExpandBufferSize() {
  if (buffer_.size() == max_size_) {
    MS_WARN_TAG(rtp, "PacketBuffer is already at max size (%zu), failed to increase size.", max_size_);
    return false;
  }

  size_t new_size = std::min(max_size_, 2 * buffer_.size());
  std::vector<std::unique_ptr<Packet>> new_buffer(new_size);
  for (std::unique_ptr<Packet>& entry : buffer_) {
    if (entry != nullptr) {
      new_buffer[entry->seq_num % new_size] = std::move(entry);
    }
  }
  buffer_ = std::move(new_buffer);
  MS_DEBUG_TAG(rtp, "PacketBuffer size expanded to %zu", new_size);
  return true;
}

bool PsRtpPacketBuffer::PotentialNewFrame(uint16_t seq_num) const {
  size_t index = seq_num % buffer_.size();
  int prev_index = index > 0 ? index - 1 : buffer_.size() - 1;
  const auto& entry = buffer_[index];
  const auto& prev_entry = buffer_[prev_index];

  if (entry == nullptr)
    return false;
  if (entry->seq_num != seq_num)
    return false;
  if (entry->is_first_packet_in_frame)
    return true;
  if (prev_entry == nullptr)
    return false;
  if (prev_entry->seq_num != static_cast<uint16_t>(entry->seq_num - 1))
    return false;
  if (prev_entry->timestamp != entry->timestamp)
    return false;
  if (prev_entry->continuous)
    return true;

  return false;
}

std::vector<std::unique_ptr<PsRtpPacketBuffer::Packet>> PsRtpPacketBuffer::FindFrames(
    uint16_t seq_num) {
  std::vector<std::unique_ptr<PsRtpPacketBuffer::Packet>> found_frames;
  for (size_t i = 0; i < buffer_.size() && PotentialNewFrame(seq_num); ++i) {
    size_t index = seq_num % buffer_.size();
    buffer_[index]->continuous = true;

    // If all packets of the frame is continuous, find the first packet of the
    // frame and add all packets of the frame to the returned packets.
    if (buffer_[index]->is_last_packet_in_frame) {
      uint16_t start_seq_num = seq_num;

      // Find the start index by searching backward until the packet with
      // the |frame_begin| flag is set.
      int start_index = index;
      size_t tested_packets = 0;

      while (true) {
        ++tested_packets;

        if (buffer_[start_index]->is_first_packet_in_frame)
          break;

        if (tested_packets == buffer_.size())
          break;

        start_index = start_index > 0 ? start_index - 1 : buffer_.size() - 1;
        --start_seq_num;
      }

      const uint16_t end_seq_num = seq_num + 1;
      // Use uint16_t type to handle sequence number wrap around case.
      uint16_t num_packets = end_seq_num - start_seq_num;
      found_frames.reserve(found_frames.size() + num_packets);
      for (uint16_t i = start_seq_num; i != end_seq_num; ++i) {
        std::unique_ptr<Packet>& packet = buffer_[i % buffer_.size()];
        //RTC_DCHECK(packet);
        //RTC_DCHECK_EQ(i, packet->seq_num);
        // Ensure frame boundary flags are properly set.
        packet->is_first_packet_in_frame = (i == start_seq_num);
        packet->is_last_packet_in_frame = (i == seq_num);
        found_frames.push_back(std::move(packet));
      }

      missing_packets_.erase(missing_packets_.begin(),
                             missing_packets_.upper_bound(seq_num));
    }
    ++seq_num;
  }
  return found_frames;
}

void PsRtpPacketBuffer::UpdateMissingPackets(uint16_t seq_num) {
  if (!newest_inserted_seq_num_)
    newest_inserted_seq_num_ = seq_num;

  const int kMaxPaddingAge = 1000;
  if (AheadOf(seq_num, newest_inserted_seq_num_)) {
    uint16_t old_seq_num = seq_num - kMaxPaddingAge;
    auto erase_to = missing_packets_.lower_bound(old_seq_num);
    missing_packets_.erase(missing_packets_.begin(), erase_to);

    // Guard against inserting a large amount of missing packets if there is a
    // jump in the sequence number.
    if (AheadOf(old_seq_num, newest_inserted_seq_num_))
      newest_inserted_seq_num_ = old_seq_num;

    ++newest_inserted_seq_num_;
    while (AheadOf(seq_num, newest_inserted_seq_num_)) {
      missing_packets_.insert(newest_inserted_seq_num_);
      ++newest_inserted_seq_num_;
    }
  } else {
    missing_packets_.erase(seq_num);
  }
}

} // namespace RTC
