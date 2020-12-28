#ifndef MS_RTC_PS_TRANSPORT_HPP
#define MS_RTC_PS_TRANSPORT_HPP

#include "RTC/PlainTransport.hpp"
#include <map>

namespace RTC
{

    class PsTransport : public RTC::PlainTransport
    {
        
    public:
        PsTransport(const std::string& id, RTC::Transport::Listener* listener, json& data);
        ~PsTransport() override;

    };

} // namespace RTC

#endif
