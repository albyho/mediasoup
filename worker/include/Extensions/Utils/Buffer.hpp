#ifndef Buffer_hpp
#define Buffer_hpp

#include <cstddef>    // size_t
#include <cstdint>    // uint8_t, etc
#include <string>

namespace RTC
{

struct BlockBuffer
{
  const uint8_t* base;
  size_t len;
};

class Buffer
{

public:
    Buffer(uint8_t* data, size_t numberOfBytes);
    ~Buffer();
    
public:
    uint8_t* GetData();
    uint8_t* GetHead();
    size_t GetPosition();
    size_t GetNumberOfBytes();
    void SetNumberOfBytes(size_t n);
    size_t GetRemain();
    bool IsEmpty();
    bool Require(size_t requiredSize);
    void Skip(size_t size);
    
public:
    int8_t ReadS8();
    uint8_t ReadU8();
    int16_t ReadS16();
    uint16_t ReadU16();
    int16_t ReadS16BE();
    uint16_t ReadU16BE();
    int32_t ReadS32();
    uint32_t ReadU32();
    int32_t ReadS32BE();
    uint32_t ReadU32BE();
    void ReadBytes(uint8_t* data, size_t size);
    
public:
    void Write1Byte(uint8_t data);
    void Write2Bytes(uint16_t data);
    void Write2BytesBE(uint16_t data);
    void Write4Bytes(uint32_t data);
    void Write4BytesBE(uint32_t data);
    void WriteBytes(uint8_t* data, size_t size);
    
private:
    uint8_t* data;
    size_t numberOfBytes;
    uint8_t* position;
};

} // namespace RTC

#endif /* Buffer_hpp */
