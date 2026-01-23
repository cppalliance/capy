//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_TEST_READ_STREAM_HPP
#define BOOST_CAPY_TEST_READ_STREAM_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/copy.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/coro.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/io_result.hpp>
#include <boost/capy/test/fuse.hpp>

#include <stop_token>
#include <string>
#include <string_view>

namespace boost {
namespace capy {
namespace test {

/** A mock stream for testing read operations.

    This class provides a controllable stream for unit testing.
    Data is supplied via @ref provide and returned by @ref read_some.
    Error injection is supported through the @ref fuse passed at
    construction.

    @par Thread Safety

    @b Not @b thread @b safe. Instances must not be accessed
    from different logical threads of operation concurrently.

    @par Example

    @code
    fuse f;
    read_stream rs(f);
    rs.provide("Hello, ");
    rs.provide("World!");

    auto r = f.armed([&](fuse&) -> task<void> {
        char buf[32];
        auto [ec, n] = co_await rs.read_some(mutable_buffer(buf, sizeof(buf)));
        if(ec.failed())
            co_return;
        // buf contains "Hello, World!"
    });
    @endcode

    @see fuse, ReadStream
*/
class read_stream
{
    fuse& f_;
    std::string data_;
    std::size_t pos_ = 0;

public:
    /** Construct a read stream with a fuse for error injection.

        @param f The fuse to use for error injection during read operations.
    */
    explicit read_stream(fuse& f) noexcept
        : f_(f)
    {
    }

    /** Provide data to be returned by subsequent reads.

        Data is appended to an internal buffer. Multiple calls
        accumulate data that will be returned by @ref read_some.

        @param sv The data to make available for reading.

        @par Example

        @code
        read_stream rs(f);
        rs.provide("first chunk");
        rs.provide("second chunk");
        // read_some will return "first chunksecond chunk"
        @endcode
    */
    void
    provide(std::string_view sv)
    {
        data_.append(sv);
    }

    /** Clear all provided data and reset the read position.

        @par Postconditions
        The stream has no data available for reading.
    */
    void
    clear() noexcept
    {
        data_.clear();
        pos_ = 0;
    }

    /** Return the number of bytes available for reading.

        @return The number of bytes that can be read before EOF.
    */
    std::size_t
    available() const noexcept
    {
        return data_.size() - pos_;
    }

    /** Read some data from the stream.

        Reads up to `buffer_size(buffers)` bytes into the buffer
        sequence. If no data is available, returns EOF. Error
        injection occurs via the fuse before returning data.

        @tparam MB The buffer sequence type satisfying
            @ref MutableBufferSequence.

        @param buffers The buffer sequence to read into.

        @return An awaitable that yields `(system::error_code, std::size_t)`.
            On success, `ec` is default-constructed and `n` is the
            number of bytes read. On EOF, `ec == cond::eof` and `n`
            is 0. On injected error, `ec` contains the fuse's error
            code and `n` is 0.

        @par Example

        @code
        read_stream rs(f);
        rs.provide("test data");

        task<void> example()
        {
            char buf[16];
            auto [ec, n] = co_await rs.read_some(mutable_buffer(buf, sizeof(buf)));
            if(ec == cond::eof)
            {
                // No more data
            }
            else if(ec.failed())
            {
                // Handle error
            }
            // n bytes were read into buf
        }
        @endcode

        @see ReadStream
    */
    template<MutableBufferSequence MB>
    auto
    read_some(MB const& buffers)
    {
        struct awaitable
        {
            read_stream* self_;
            MB const& buffers_;

            bool await_ready() const noexcept { return true; }

            void await_suspend(
                coro,
                executor_ref,
                std::stop_token) const noexcept
            {
            }

            io_result<std::size_t>
            await_resume()
            {
                auto ec = self_->f_.maybe_fail();
                if(ec.failed())
                    return {ec, 0};

                if(self_->pos_ >= self_->data_.size())
                    return {error::eof, 0};

                std::size_t const avail = self_->data_.size() - self_->pos_;
                auto src = make_buffer(self_->data_.data() + self_->pos_, avail);
                std::size_t const n = copy(buffers_, src);
                self_->pos_ += n;
                return {{}, n};
            }
        };
        return awaitable{this, buffers};
    }
};

} // test
} // capy
} // boost

#endif
