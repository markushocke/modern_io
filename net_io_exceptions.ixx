// net_io_exceptions.ixx
module;

#ifndef _MSC_VER
#include <stdexcept>
#include <string>
#include <optional>
#endif
#include <stdint.h>

export module net_io.exceptions;

#ifdef _MSC_VER
import <stdexcept>;
import <string>;
import <optional>;
#endif

namespace net_io
{

/**
 * @brief Base exception for network I/O errors.
 */
export class NetworkException : public std::runtime_error
{
public:
    explicit NetworkException(const std::string& message, int error_code = 0)
        : std::runtime_error(message + (error_code ? " (error: " + std::to_string(error_code) + ")" : ""))
        , error_code_(error_code)
    {}

    int error_code() const noexcept { return error_code_; }

private:
    int error_code_;
};

/**
 * @brief Exception for connection-related errors.
 */
export class ConnectionException : public NetworkException
{
public:
    ConnectionException(const std::string& message, int error_code, 
                       std::optional<std::string> endpoint = std::nullopt)
        : NetworkException(message + (endpoint ? " [" + *endpoint + "]" : ""), error_code)
        , endpoint_(endpoint)
    {}

    std::optional<std::string> endpoint() const noexcept { return endpoint_; }

private:
    std::optional<std::string> endpoint_;
};

/**
 * @brief Exception for bind/listen errors.
 */
export class BindException : public NetworkException
{
public:
    BindException(const std::string& message, int error_code, 
                  const std::string& address, uint16_t port)
        : NetworkException(message + " [" + address + ":" + std::to_string(port) + "]", error_code)
        , address_(address)
        , port_(port)
    {}

    const std::string& address() const noexcept { return address_; }
    uint16_t port() const noexcept { return port_; }

private:
    std::string address_;
    uint16_t port_;
};

/**
 * @brief Exception for timeout errors.
 */
export class TimeoutException : public NetworkException
{
public:
    explicit TimeoutException(const std::string& message)
        : NetworkException(message, 0)
    {}
};

/**
 * @brief Exception for hostname resolution errors.
 */
export class ResolutionException : public NetworkException
{
public:
    ResolutionException(const std::string& message, const std::string& hostname)
        : NetworkException(message + ": " + hostname, 0)
        , hostname_(hostname)
    {}

    const std::string& hostname() const noexcept { return hostname_; }

private:
    std::string hostname_;
};

    /**
     * @brief Exception used when function arguments are invalid for network APIs.
     *
     * This derives from std::invalid_argument to preserve the usual semantics
     * but carries the net_io namespace so callers can catch domain-specific types.
     */
    export class InvalidArgumentException : public std::invalid_argument
    {
    public:
        explicit InvalidArgumentException(const std::string& message)
            : std::invalid_argument(message)
        {}
    };

} // namespace net_io
