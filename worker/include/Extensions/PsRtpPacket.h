#ifndef PsRtpPacket_h
#define PsRtpPacket_h

namespace RTC {

#pragma pack(push)
#pragma pack(1)

/* Struct for PS Packet start code prefix. */
struct PsPacketStartCodePrefix {
    uint8_t prefix[3];                      // 3 bytes: '0x000001'
};

/* Struct for PS Header start code. */
struct PsPacketStartCode
{
    PsPacketStartCodePrefix prefix;
    uint8_t code[1];
};

/* Struct for PS header. (P.55: Table 2-33 - Program pack header) */
struct PsHeader
{
    // 0
    PsPacketStartCode startCode;            // 4 bytes: '0x000001BA'

    // 4
    uint8_t info[9];
    
    // 13
#if defined(MS_LITTLE_ENDIAN)
    uint8_t reserved:5;
    uint8_t packStuffingLength:3;
#elif defined(MS_BIG_ENDIAN)
    uint8_t packStuffingLength:3;
    uint8_t reserved:5;
#endif
}; // 14 + (packStuffingLength)

/* Struct for PS System header prefix. (P.56: Table 2-34 - Program Stream system header) */
struct PsSystemHeaderPrefix
{
    // 0
    PsPacketStartCode startCode;                // 4 bytes: '0x000001BB'
    
    // 4
    uint16_t headerLength;                      // Note: Big Endian
}; // 6 + (6) + (N * 3): while(nextbits() == 1) { stream_id:8bits, '11':2bits, P-STD_buffer_bound_scale:1bits, P-STD_buffer_size_bound:13bits}

/* Struct for PSM Header prefix (P.59: Table 2-17 - Program Stream map) */
struct PsPSMHeaderPrefix
{
    // 0
    PsPacketStartCode startCode;                // 4 bytes: '0x000001BC'

    // 4
    uint16_t programStreamMapLength;            // Note: Big Endian, include `programStreamMapLength` self.
}; // 6 + programStreamInfoLength:2bytes + (N * x) + (elementaryStreamMapLength:2bytes) + (N1 * x1) + (CRC_32:4bytes)

/* Struct for PES packet header prefix (P.31: Table 2-17 - PES packet) */
struct PsePacketHeaderPrefix
{
    // 0
    PsPacketStartCode startCode;                // 4 bytes: '0x000001E0' or '0x000001C0'
    
    // 4
    uint16_t pesPacketLength; // exclude `startCode` and `pesPacketLength`.
    
    // 6
    uint8_t info[2];
    
    // 8
    uint8_t pesHeaderDataLength;
}; // 9 + x

#pragma pack(pop)

} // namespace RTC

#endif /* PsRtpPacket_h */
