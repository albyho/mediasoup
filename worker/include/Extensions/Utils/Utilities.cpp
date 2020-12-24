#include "Utilities.hpp"

namespace RTC {

bool Utilities::IsLittleEndian()
{
    // convert to network(big-endian) order, if not equals,
    // the system is little-endian, so need to convert the int64
    static int littleEndianCheck = -1;
    if(littleEndianCheck == -1) {
        union {
            int32_t i;
            int8_t c;
        } littleCheckUnion;
        
        littleCheckUnion.i = 0x01;
        littleEndianCheck = littleCheckUnion.c;
    }
    
    return (littleEndianCheck == 1);
}

} // namespace RTC
