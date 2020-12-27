#include "Buffer.hpp"
#include "Utilities.hpp"

namespace RTC
{

Buffer::Buffer(uint8_t* data, size_t numberOfBytes)
{
#if defined(MS_BIG_ENDIAN)
    assert(0);
#endif
    this->data = data;
    this->numberOfBytes = numberOfBytes;
    this->position = data;
}

Buffer::~Buffer()
{
}

uint8_t* Buffer::GetData()
{
    return data;
}

uint8_t* Buffer::GetHead()
{
    return position;
}

size_t Buffer::GetPosition()
{
    return position - data;
}

size_t Buffer::GetNumberOfBytes()
{
    return numberOfBytes;
}

void Buffer::SetNumberOfBytes(size_t n)
{
    numberOfBytes = n;
}

size_t Buffer::GetRemain()
{
    return numberOfBytes - (position - data);
}

bool Buffer::IsEmpty()
{
    return !data || (position >= data + numberOfBytes);
}

bool Buffer::Require(size_t requiredSize)
{
    assert(requiredSize >= 0);
    
    return requiredSize <= numberOfBytes - (position - data);
}

void Buffer::Skip(size_t size)
{
    assert(position);
    assert(position + size >= data);
    assert(position + size <= data + numberOfBytes);
    
    position += size;
}

int8_t Buffer::ReadS8()
{
    return (int8_t)ReadU8();
}

uint8_t Buffer::ReadU8()
{
    assert(Require(sizeof(uint8_t)));
    
    return *position++;
}

int16_t Buffer::ReadS16()
{
    return (int16_t)ReadU16();
}

uint16_t Buffer::ReadU16()
{
    assert(Require(sizeof(uint16_t)));
    
    uint16_t value;
    uint8_t* p = (uint8_t*)&value;
    p[0] = *position++;
    p[1] = *position++;
    
    return value;
}

int16_t Buffer::ReadS16BE()
{
    return (int16_t)ReadU16BE();
}

uint16_t Buffer::ReadU16BE()
{
    assert(Require(sizeof(uint16_t)));
    
    uint16_t value;
    uint8_t* p = (uint8_t*)&value;
    p[1] = *position++;
    p[0] = *position++;
    
    return value;
}

int32_t Buffer::ReadS32()
{
    return (int32_t)ReadU32();
}

uint32_t Buffer::ReadU32()
{
    assert(Require(sizeof(uint32_t)));
    
    uint32_t value;
    uint8_t* p = (uint8_t*)&value;
    p[0] = *position++;
    p[1] = *position++;
    p[2] = *position++;
    p[3] = *position++;
    
    return value;
}

int32_t Buffer::ReadS32BE()
{
    return (int16_t)ReadU16BE();
}

uint32_t Buffer::ReadU32BE()
{
    assert(Require(sizeof(uint32_t)));
    
    uint32_t value;
    uint8_t* p = (uint8_t*)&value;
    p[3] = *position++;
    p[2] = *position++;
    p[1] = *position++;
    p[0] = *position++;
    
    return value;
}

void Buffer::ReadBytes(uint8_t* data, size_t size)
{
    assert(Require(size));
    
    std::memcpy(data, position, size);
    position += size;
}

void Buffer::Write1Byte(uint8_t data)
{
    assert(Require(sizeof(uint8_t)));
    
    *position++ = data;
}

void Buffer::Write2Bytes(uint16_t data)
{
    assert(Require(sizeof(uint16_t)));

    uint8_t* p = (uint8_t*)&data;
    *position++ = p[0];
    *position++ = p[1];
}

void Buffer::Write2BytesBE(uint16_t data)
{
    assert(Require(sizeof(uint16_t)));

    uint8_t* p = (uint8_t*)&data;
    *position++ = p[1];
    *position++ = p[0];
}

void Buffer::Write4Bytes(uint32_t data)
{
    assert(Require(sizeof(uint32_t)));

    uint8_t* p = (uint8_t*)&data;
    *position++ = p[0];
    *position++ = p[1];
    *position++ = p[2];
    *position++ = p[3];
}

void Buffer::Write4BytesBE(uint32_t data)
{
    assert(Require(sizeof(uint32_t)));

    uint8_t* p = (uint8_t*)&data;
    *position++ = p[3];
    *position++ = p[2];
    *position++ = p[1];
    *position++ = p[0];
}

void Buffer::WriteBytes(uint8_t* data, size_t size)
{
    assert(Require(size));
    
    std::memcpy(position, data, size);
    position += size;
}

} // namespace RTC
