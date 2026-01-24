//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BUFFERS_SINK_HPP
#define BOOST_CAPY_BUFFERS_SINK_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/detail/except.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/system/error_code.hpp>

namespace boost {
namespace capy {

/*
struct buffer_sink
{
    void size_hint( std::size_t size );

    struct MutableBufferSequence;

    auto prepare( std::size_t size )
      -> MutableBufferSequence;

    auto commit(
        std::size_t n,
        bool finished = false ) ->
            system::error_code;

    auto finish() -> system::error_code;
};
*/

/** Determine if T is a WriteSink

    A type T is a Write Sink if it meets the following
    requirements:

    @code
    struct T
    {
        // Provide a hint about the amount of data to be written.
        void size_hint( std::size_t size );
        // Write data from the ConstBufferSequence.
        system::error_code write(
            ConstBufferSequence const& data,
            bool finished = false );
        // Indicate that no more data will be written.
        system::error_code finish();
    };
    @endcode
    @see buffers_sink
*/
template<class T, class = void>
struct is_write_sink : std::false_type {};

template<class T>
struct is_write_sink<T, std::void_t<
    decltype(std::declval<T&>().size_hint(std::declval<std::size_t>())),
    typename std::enable_if<
        std::is_same<
            decltype(std::declval<T&>().write(
                std::declval<char const*>(),
                std::declval<bool>())),
            system::error_code>::value>::type,
    typename std::enable_if<
        std::is_same<
            decltype(std::declval<T&>().finish()),
            system::error_code>::value>::type
>> : std::true_type {};

//-----------------------------------------------

class any_write_sink
{
public:

private:
};


struct body
{

};

} // capy
} // boost

#endif
