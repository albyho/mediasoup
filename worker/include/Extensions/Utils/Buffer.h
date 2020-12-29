#ifndef Buffer_hpp
#define Buffer_hpp

#include <cstddef>    // size_t
#include <cstdint>    // uint8_t, etc

struct BlockBuffer
{
  const uint8_t* base;
  size_t len;
};

#endif /* Buffer_hpp */
