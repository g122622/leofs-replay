#include "trace_replay/core/Error.hpp"

#include <format>
#include <utility>

namespace trace_replay {

std::string Error::toString() const
{
    return std::format("[{}] {} (at {})",
                       static_cast<int>(m_code), m_message, m_source);
}

Error Error::invalidArgument(std::string msg, std::string src)
{
    return {Code::InvalidArgument, std::move(msg), std::move(src)};
}

Error Error::invalidFormat(std::string msg, std::string src)
{
    return {Code::InvalidFormat, std::move(msg), std::move(src)};
}

Error Error::notFound(std::string msg, std::string src)
{
    return {Code::NotFound, std::move(msg), std::move(src)};
}

Error Error::ioError(std::string msg, std::string src)
{
    return {Code::IOError, std::move(msg), std::move(src)};
}

Error Error::syscallError(std::string msg, std::string src)
{
    return {Code::Syscall, std::move(msg), std::move(src)};
}

Error Error::internal(std::string msg, std::string src)
{
    return {Code::Internal, std::move(msg), std::move(src)};
}

}  // namespace trace_replay
