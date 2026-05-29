module;

#ifndef _MSC_VER
#include <stdexcept>
#include <string>
#include <system_error>
#include <cerrno>
#endif

export module modern_io.exceptions;

#ifdef _MSC_VER
import <stdexcept>;
import <string>;
import <system_error>;
import <cerrno>;
#endif

namespace modern::io
{

/**
 * @brief Base exception class for all modern_io errors.
 */
export class IOException : public std::runtime_error
{
public:
    explicit IOException(const std::string& message)
        : std::runtime_error(message)
    {}
};

/**
 * @brief Exception thrown when file operations fail.
 */
export class FileIOException : public IOException
{
public:
    FileIOException(const std::string& message, const std::string& filepath, int error_code = 0)
        : IOException(message + ": " + filepath + (error_code ? " (errno: " + std::to_string(error_code) + ")" : ""))
        , filepath_(filepath)
        , error_code_(error_code)
    {}

    const std::string& filepath() const noexcept { return filepath_; }
    int error_code() const noexcept { return error_code_; }

private:
    std::string filepath_;
    int error_code_;
};

/**
 * @brief Exception thrown when read/write operations fail.
 */
export class ReadWriteException : public IOException
{
public:
    ReadWriteException(const std::string& message, std::size_t expected, std::size_t actual)
        : IOException(message + " (expected: " + std::to_string(expected) + ", got: " + std::to_string(actual) + ")")
        , expected_(expected)
        , actual_(actual)
    {}

    std::size_t expected() const noexcept { return expected_; }
    std::size_t actual() const noexcept { return actual_; }

private:
    std::size_t expected_;
    std::size_t actual_;
};

/**
 * @brief Exception thrown when encountering unexpected EOF.
 */
export class UnexpectedEOFException : public IOException
{
public:
    explicit UnexpectedEOFException(const std::string& message = "Unexpected end of file")
        : IOException(message)
    {}
};

/**
 * @brief Exception thrown for invalid data format.
 */
export class DataFormatException : public IOException
{
public:
    explicit DataFormatException(const std::string& message)
        : IOException(message)
    {}
};

/**
 * @brief Exception thrown for buffer-related errors.
 */
export class BufferException : public IOException
{
public:
    explicit BufferException(const std::string& message)
        : IOException(message)
    {}
};

/**
 * @brief Exception thrown when stream position operations fail.
 */
export class StreamPositionException : public IOException
{
public:
    explicit StreamPositionException(const std::string& message)
        : IOException(message)
    {}
};

} // namespace modern::io

export namespace modern_io {

using modern::io::BufferException;
using modern::io::DataFormatException;
using modern::io::FileIOException;
using modern::io::IOException;
using modern::io::ReadWriteException;
using modern::io::StreamPositionException;
using modern::io::UnexpectedEOFException;

} // namespace modern_io