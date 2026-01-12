//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
//#include <boost/capy/buffers/buffer.hpp>

#if 0

#include <boost/capy/buffers/detail/config.hpp>

#include <boost/capy/buffers/buffer.hpp>

#include "test_buffers.hpp"

#__include <boost/capy/async_op.hpp>

namespace boost {
namespace capy {
namespace buffers {

template<bool IsMutable>
class consumer
{
public:
    /** The type of buffer returned by the consumer
    */
    using value_type = std::conditional_t<
        IsMutable, mutable_buffer, const_buffer>;

    /** Destructor
    */
    virtual ~consumer() = default;

    /** Check if the sequence is empty
        @return `true` if the sequence is empty, `false` otherwise.
    */
    virtual bool empty() const noexcept = 0;

    /** Get the next sequence of buffers

        @param dest A pointer to an array of buffers to be
        populated with the next sequence of buffers.
        
        @param n The maximum number of buffers to be written
        to the array pointed to by `dest`.
        
        @return The number of buffers written to the array
        pointed to by `dest`.
    */
    virtual std::size_t next(
        value_type* dest, std::size_t n) = 0;
};

/** A container for consuming a buffer sequence

    This class adapts a BufferSequence to the consumer interface.
*/
template<class BufferSequence>
class consumer_param : public consumer<
    is_mutable_buffer_sequence_v<BufferSequence>>
{
    BufferSequence bs_;
    decltype(begin(bs_)) it_;
    decltype(end(bs_)) end_;

public:
    template<class T>
    consumer_param(T&& t)
        : bs_(std::forward<T>(t))
        , it_(begin(bs_))
        , end_(end(bs_))
    {
    }

    bool empty() const noexcept override
    {
        return it_ == end_;
    }

    std::size_t next(typename
        consumer_param::value_type* dest, std::size_t n) override
    {
        std::size_t rv = 0;
        while(it_ != end_ && n--)
        {
            *dest++ = typename consumer_param::value_type(*it_++);
            ++rv;
        }
        return rv;
    }
};

struct io_param_test
{
    void run()
    {
    }
};

TEST_SUITE(
    io_param_test,
    "boost.capy.buffers.io_param");

} // buffers
} // capy
} // boost

#endif
