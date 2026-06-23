//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_READ_UNTIL_HPP
#define BOOST_CAPY_READ_UNTIL_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/cond.hpp>
#include <coroutine>
#include <boost/capy/error.hpp>
#include <boost/capy/io_result.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/capy/concept/dynamic_buffer.hpp>
#include <boost/capy/concept/match_condition.hpp>
#include <boost/capy/concept/read_stream.hpp>
#include <boost/capy/ex/io_env.hpp>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <stop_token>
#include <string_view>
#include <type_traits>

namespace boost {
namespace capy {

namespace detail {

// Linearize a buffer sequence into a string
inline
std::string
linearize_buffers(ConstBufferSequence auto const& data)
{
    std::string linear;
    linear.reserve(buffer_size(data));
    auto const end_ = end(data);
    for(auto it = begin(data); it != end_; ++it)
    {
        const_buffer b = *it;
        linear.append(
            static_cast<char const*>(b.data()),
            b.size());
    }
    return linear;
} // LCOV_EXCL_LINE gcov brace artifact (linearize_buffers is exercised)

// Search buffer using a MatchCondition, with single-buffer optimization
template<MatchCondition M>
std::size_t
search_buffer_for_match(
    ConstBufferSequence auto const& data,
    M const& match,
    std::size_t* hint = nullptr)
{
    // Fast path: single buffer - no linearization needed
    if(buffer_length(data) == 1)
    {
        auto const& buf = *begin(data);
        return match(std::string_view(
            static_cast<char const*>(buf.data()),
            buf.size()), hint);
    }
    // Multiple buffers - linearize
    return match(linearize_buffers(data), hint);
}

// Implementation coroutine for read_until with MatchCondition
template<class Stream, class B, MatchCondition M>
io_task<std::size_t>
read_until_match_impl(
    Stream& stream,
    B& buffers,
    M match,
    std::size_t initial_amount)
{
    std::size_t amount = initial_amount;

    for(;;)
    {
        // Check max_size before preparing
        if(buffers.size() >= buffers.max_size())
            co_return {error::not_found, 0};

        // Prepare space, respecting max_size
        std::size_t const available = buffers.max_size() - buffers.size();
        std::size_t const to_prepare = (std::min)(amount, available);
        if(to_prepare == 0)
            co_return {error::not_found, 0};

        auto mb = buffers.prepare(to_prepare);
        auto [ec, n] = co_await stream.read_some(mb);
        buffers.commit(n);

        if(!ec)
        {
            auto pos = search_buffer_for_match(buffers.data(), match);
            if(pos != std::string_view::npos)
                co_return {{}, pos};
        }

        if(ec == cond::eof)
            co_return {error::eof, buffers.size()};
        if(ec)
            co_return {ec, buffers.size()};

        // Grow buffer size for next iteration
        if(n == buffer_size(mb))
            amount = amount / 2 + amount;
    }
}

template<class Stream, class B, MatchCondition M, bool OwnsBuffer>
struct read_until_awaitable
{
    Stream* stream_;
    M match_;
    std::size_t initial_amount_;
    std::optional<io_result<std::size_t>> immediate_;
    std::optional<io_task<std::size_t>> inner_;

    using storage_type = std::conditional_t<OwnsBuffer, B, B*>;
    storage_type buffers_storage_;

    B& buffers() noexcept
    {
        if constexpr(OwnsBuffer)
            return buffers_storage_;
        else
            return *buffers_storage_;
    }

    // Constructor for lvalue (pointer storage)
    read_until_awaitable(
        Stream& stream,
        B* buffers,
        M match,
        std::size_t initial_amount)
        requires (!OwnsBuffer)
        : stream_(std::addressof(stream))
        , match_(std::move(match))
        , initial_amount_(initial_amount)
        , buffers_storage_(buffers)
    {
        auto pos = search_buffer_for_match(
            buffers_storage_->data(), match_);
        if(pos != std::string_view::npos)
            immediate_.emplace(io_result<std::size_t>{{}, pos});
    }

    // Constructor for rvalue adapter (owned storage)
    read_until_awaitable(
        Stream& stream,
        B&& buffers,
        M match,
        std::size_t initial_amount)
        requires OwnsBuffer
        : stream_(std::addressof(stream))
        , match_(std::move(match))
        , initial_amount_(initial_amount)
        , buffers_storage_(std::move(buffers))
    {
        auto pos = search_buffer_for_match(
            buffers_storage_.data(), match_);
        if(pos != std::string_view::npos)
            immediate_.emplace(io_result<std::size_t>{{}, pos});
    }

    bool
    await_ready() const noexcept
    {
        return immediate_.has_value();
    }

    std::coroutine_handle<>
    await_suspend(std::coroutine_handle<> h, io_env const* env)
    {
        inner_.emplace(read_until_match_impl(
            *stream_, buffers(), std::move(match_), initial_amount_));
        return inner_->await_suspend(h, env);
    }

    io_result<std::size_t>
    await_resume()
    {
        if(immediate_)
            return *immediate_;
        return inner_->await_resume();
    }
};

template<ReadStream Stream, class B, MatchCondition M>
using read_until_return_t = read_until_awaitable<
    Stream,
    std::remove_reference_t<B>,
    M,
    !std::is_lvalue_reference_v<B&&>>;

} // namespace detail

/** Match condition that searches for a delimiter string.

    Satisfies @ref MatchCondition. Returns the position after the
    delimiter when found, or `npos` otherwise. Provides an overlap
    hint of `delim.size() - 1` to handle delimiters spanning reads.

    @see MatchCondition, read_until
*/
struct match_delim
{
    /** The delimiter string to search for.

        @note The referenced characters must remain valid
            for the lifetime of this object and any pending
            read operation.
    */
    std::string_view delim;

    /** Search for the delimiter in `data`.

        @param data The data to search.
        @param hint If non-null, receives the overlap hint
            on miss.
        @return `0` if `delim` is empty; otherwise the position
            just past the delimiter, or `npos` if not found.
    */
    std::size_t
    operator()(
        std::string_view data,
        std::size_t* hint) const noexcept
    {
        if(delim.empty())
            return 0;
        auto pos = data.find(delim);
        if(pos != std::string_view::npos)
            return pos + delim.size();
        if(hint)
            *hint = delim.size() > 1 ? delim.size() - 1 : 0;
        return std::string_view::npos;
    }
};

/** Asynchronously read until a match condition is satisfied.

    Reads data from `stream` and appends it to `dynbuf` via calling
    `stream.read_some` zero or more times and using the prepare/commit
    interface until:

    @li either @c match returns a valid position,
    @li or @c dynbuf.size() == @c dynbuf.max_size() ,
    @li or a contingency on @c stream.read_some occurs.

    If the match condition is satisfied by data in `dynbuf.data()` upon  entry,
    no call to `stream.read_some` is performed.


    @par Await-returns

    An object of type `io_result<std::size_t>` destructuring as `[ec, n]`.

    If `bool(ec)`, `n` is the position returned by the match condition
        (bytes up to and including the matched delimiter).


    Contingencies:

    @li The first contingency, reported from awaiting @c stream.read_some .
    @li @c cond::not_found -- when @c dynbuf.size() == @c dynbuf.max_size() 
        and the match condition is not satisfied by data in  @c dynbuf.data() .

    @param stream The stream to read from. The caller retains ownership.
    @param dynbuf The dynamic buffer to append data to. Must remain
        valid until the operation completes.
    @param match The match condition callable. Copied into the awaitable.
    @param initial_amount Initial bytes to read per iteration (default
        2048). Grows by 1.5x when filled.

     


    @par Await-throws
    
    Whatever operations on @c dunbuf throw.

    (Note: types modeling @c DynamicBufferParam provided by Capy throw 
     @c std::bad_alloc from member function 
     @c prepare .)

    @par Remarks
    Supports _IoAwaitable cancellation_.

    @par Example

    @code
    task<> read_http_header( ReadStream auto& stream )
    {
        std::string header;
        auto [ec, n] = co_await read_until(
            stream,
            string_dynamic_buffer( &header ),
            []( std::string_view data, std::size_t* hint ) {
                auto pos = data.find( "\r\n\r\n" );
                if( pos != std::string_view::npos )
                    return pos + 4;
                if( hint )
                    (*hint) = 3;  // partial "\r\n\r" possible
                return std::string_view::npos;
            } );
        if( ec )
            detail::throw_system_error( ec );
        // header contains data through "\r\n\r\n"
    }
    @endcode

    @see read_some, MatchCondition, DynamicBufferParam
*/
template<ReadStream Stream, class B, MatchCondition M>
    requires DynamicBufferParam<B&&>
detail::read_until_return_t<Stream, B, M>
read_until(
    Stream& stream,
    B&& dynbuf,
    M match,
    std::size_t initial_amount = 2048)
{
    constexpr bool is_lvalue = std::is_lvalue_reference_v<B&&>;
    using BareB = std::remove_reference_t<B>;

    if constexpr(is_lvalue)
        return detail::read_until_awaitable<Stream, BareB, M, false>(
            stream, std::addressof(dynbuf), std::move(match), initial_amount);
    else
        return detail::read_until_awaitable<Stream, BareB, M, true>(
            stream, std::move(dynbuf), std::move(match), initial_amount);
}

/** Asynchronously read until a delimiter string is found.

    Reads data from the stream until the delimiter is found. This is
    a convenience overload equivalent to calling `read_until` with
    `match_delim{delim}`. If the delimiter already exists in the
    buffer, returns immediately without I/O.

    @li The operation completes when:
    @li The delimiter string is found
    @li End-of-stream is reached (`cond::eof`)
    @li The buffer's `max_size()` is reached (`cond::not_found`)
    @li An error occurs
    @li The operation is cancelled

    @par Cancellation
    Supports cancellation via `stop_token` propagated through the
    IoAwaitable protocol. When cancelled, returns with `cond::canceled`.

    @param stream The stream to read from. The caller retains ownership.
    @param buffers The dynamic buffer to append data to. Must remain
        valid until the operation completes.
    @param delim The delimiter string to search for.
    @param initial_amount Initial bytes to read per iteration (default
        2048). Grows by 1.5x when filled.

    @return An awaitable that await-returns `(error_code, std::size_t)`.
        On success, `n` is bytes up to and including the delimiter.
        Compare error codes to conditions:
        @li `cond::eof` - EOF before delimiter; `n` is buffer size
        @li `cond::not_found` - `max_size()` reached before delimiter
        @li `cond::canceled` - Operation was cancelled

    @par Example

    @code
    task<std::string> read_line( ReadStream auto& stream )
    {
        std::string line;
        auto [ec, n] = co_await read_until(
            stream, string_dynamic_buffer( &line ), "\r\n" );
        if( ec == cond::eof )
            co_return line;  // partial line at EOF
        if( ec )
            detail::throw_system_error( ec );
        line.resize( n - 2 );  // remove "\r\n"
        co_return line;
    }
    @endcode

    @see read_until, match_delim, DynamicBufferParam
*/
template<ReadStream Stream, class B>
    requires DynamicBufferParam<B&&>
detail::read_until_return_t<Stream, B, match_delim>
read_until(
    Stream& stream,
    B&& buffers,
    std::string_view delim,
    std::size_t initial_amount = 2048)
{
    return read_until(
        stream,
        std::forward<B>(buffers),
        match_delim{delim},
        initial_amount);
}

} // namespace capy
} // namespace boost

#endif
