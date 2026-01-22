//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BUFFERS_BUFFER_PARAM_HPP
#define BOOST_CAPY_BUFFERS_BUFFER_PARAM_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/assert.hpp>

#include <array>
#include <span>
#include <type_traits>

namespace boost {
namespace capy {

/** A type-erased const buffer sequence for function call boundaries.

    This class enables functions to accept any const buffer sequence
    type across a non-template interface, while the caller retains
    ownership of the underlying buffer data. The class provides
    incremental access to the buffer sequence through a sliding
    window, allowing efficient processing of arbitrarily large
    sequences without copying all buffer descriptors upfront.

    @par Purpose

    When building I/O abstractions, a common need arises: accepting
    buffer sequences of any type through a non-template function
    signature. This class solves the type-erasure problem by storing
    function pointers that operate on the concrete buffer sequence
    type, while presenting a uniform interface to the callee.

    @par Lifetime Model

    @warning This class has DANGEROUS lifetime semantics. The safety
    of this class depends entirely on the lifetime of the object
    returned by @ref make_const_buffer_param. When that object is
    destroyed, this class becomes invalid and any use is undefined
    behavior.

    The intended usage pattern exploits C++ temporary lifetime
    extension: when passing the result of @ref make_const_buffer_param
    directly to a function taking `const_buffer_param` by value,
    slicing occurs and the temporary implementation object lives
    until the function returns. This is safe:

    @code
    void write_some(const_buffer_param buffers);

    // SAFE: temporary lives through the function call
    write_some(make_const_buffer_param(my_buffers));
    @endcode

    @par Correct Usage

    The implementation receiving `const_buffer_param` MUST:

    @li Use `data()` and `consume()` immediately within the call
    @li Never store the `const_buffer_param` object
    @li Never return the `const_buffer_param` from the function
    @li Complete all buffer processing before returning

    @par Example: Correct Usage

    @code
    // Function accepts type-erased buffer parameter
    std::size_t write_all(socket& sock, const_buffer_param buffers)
    {
        std::size_t total = 0;
        while(true)
        {
            auto bufs = buffers.data();
            if(bufs.empty())
                break;
            auto n = sock.write_some(bufs);
            total += n;
            buffers.consume(n);
        }
        return total;
    }

    // Caller creates temporary that lives through the call
    std::vector<const_buffer> chunks = get_chunks();
    auto n = write_all(sock, make_const_buffer_param(chunks));
    @endcode

    @par UNSAFE USAGE: Storing const_buffer_param

    @warning Never store `const_buffer_param` for later use.

    @code
    class broken_writer
    {
        const_buffer_param saved_;  // UNSAFE: member storage

        void start_write(const_buffer_param buffers)
        {
            saved_ = buffers;  // UNSAFE: storing for later
            schedule_write();
        }

        void do_write()
        {
            // UNSAFE: The temporary from make_const_buffer_param
            // was destroyed when start_write returned!
            auto bufs = saved_.data();  // UNDEFINED BEHAVIOR
        }
    };
    @endcode

    @par UNSAFE USAGE: Returning const_buffer_param

    @warning Never return `const_buffer_param` from a function.

    @code
    // UNSAFE: Returning causes the temporary to be destroyed
    const_buffer_param get_buffers()
    {
        std::vector<const_buffer> bufs = prepare_buffers();
        return make_const_buffer_param(bufs);  // UNSAFE!
        // The temporary AND the vector are destroyed here
    }

    void caller()
    {
        auto bp = get_buffers();  // Receives invalid object
        bp.data();  // UNDEFINED BEHAVIOR
    }
    @endcode

    @par UNSAFE USAGE: Storing in a Container

    @warning Never store `const_buffer_param` in containers or
    data structures.

    @code
    std::vector<const_buffer_param> pending;  // UNSAFE

    void queue_write(const_buffer_param buffers)
    {
        pending.push_back(buffers);  // UNSAFE: storing
    }

    void process_pending()
    {
        for(auto& bp : pending)
            bp.data();  // UNDEFINED BEHAVIOR
    }
    @endcode

    @par UNSAFE USAGE: Assigning to a Variable

    @warning Assigning to a named variable extends the wrong
    lifetime.

    @code
    void broken()
    {
        auto bp = make_const_buffer_param(my_buffers);
        // bp is valid here...

        some_function();  // ...but if this modifies my_buffers...

        bp.data();  // ...this may see inconsistent state
    }

    void also_broken()
    {
        const_buffer_param bp = make_const_buffer_param(
            std::vector<const_buffer>{{data, size}});
        // UNSAFE: The temporary vector is destroyed immediately!
        bp.data();  // UNDEFINED BEHAVIOR
    }
    @endcode

    @par Passing Convention

    Pass by value. The class contains only three pointers (24 bytes
    on 64-bit systems), making copies trivial. Pass-by-value also
    makes the slicing behavior explicit at call sites.

    @see make_const_buffer_param, mutable_buffer_param
*/
class const_buffer_param
{
protected:
    void* ctx_;
    std::span<const_buffer>(*data_)(void*);
    bool (*consume_)(void*, std::size_t);

public:
    /** Return the current window of buffer descriptors.

        Returns a span of buffer descriptors representing the
        currently available portion of the buffer sequence. The
        span may contain fewer buffers than the total sequence;
        call @ref consume to advance through the sequence and
        then call `data()` again to get the next window.

        @return A span of const buffer descriptors. Empty span
            indicates no more data is available.
    */
    std::span<const_buffer>
    data() const noexcept
    {
        return data_(ctx_);
    }

    /** Consume bytes from the buffer sequence.

        Advances the current position by `n` bytes, consuming
        data from the front of the sequence. After consuming,
        call @ref data to get the updated window of remaining
        buffers.

        @param n Number of bytes to consume.

        @return `true` if more data remains in the sequence,
            `false` if the sequence is exhausted.
    */
    bool
    consume(std::size_t n)
    {
        return consume_(ctx_, n);
    }
};

/** A type-erased mutable buffer sequence for function call boundaries.

    This class is identical to @ref const_buffer_param except it
    operates on mutable buffer sequences, allowing the callee to
    write into the buffers.

    @warning This class has the same dangerous lifetime semantics
    as @ref const_buffer_param. Read that documentation carefully
    before using this class.

    @see make_buffer_param, const_buffer_param
*/
class mutable_buffer_param
{
protected:
    void* ctx_;
    std::span<mutable_buffer>(*data_)(void*);
    bool (*consume_)(void*, std::size_t);

public:
    /** Return the current window of buffer descriptors.

        @return A span of mutable buffer descriptors. Empty span
            indicates no more data is available.

        @see const_buffer_param::data
    */
    std::span<mutable_buffer>
    data() const noexcept
    {
        return data_(ctx_);
    }

    /** Consume bytes from the buffer sequence.

        @param n Number of bytes to consume.

        @return `true` if more data remains in the sequence,
            `false` if the sequence is exhausted.

        @see const_buffer_param::consume
    */
    bool
    consume(std::size_t n)
    {
        return consume_(ctx_, n);
    }
};

/** Create a const buffer parameter from a buffer sequence.

    Constructs a type-erased buffer parameter that provides
    incremental access to the given buffer sequence. The returned
    object inherits from @ref const_buffer_param and can be passed
    to functions accepting that type by value (causing slicing).

    @warning The returned object contains pointers to internal
    state. The object MUST remain alive while any sliced
    @ref const_buffer_param copy is in use. The intended pattern
    is to pass the result directly to a function:

    @code
    void process(const_buffer_param bp);

    // CORRECT: temporary lives through the call
    process(make_const_buffer_param(buffers));

    // DANGEROUS: storing extends the wrong lifetime
    auto bp = make_const_buffer_param(buffers);
    process(bp);  // Works, but fragile
    @endcode

    @par Thread Safety
    Not thread-safe. The returned object and any sliced copies
    must not be used concurrently.

    @param bs The buffer sequence to wrap. Must remain valid
        for the lifetime of the returned object.

    @return An implementation object that inherits from
        @ref const_buffer_param.
*/
template<ConstBufferSequence BS>
[[nodiscard]] auto
make_const_buffer_param(BS const& bs)
{
    class impl : public const_buffer_param
    {
        std::array<const_buffer, 16> arr_;
        decltype(begin(std::declval<BS const&>())) it_;
        decltype(end(std::declval<BS const&>())) end_;
        std::size_t size_ = 0;
        std::size_t pos_ = 0;
        std::size_t offset_ = 0;

        void
        refill_window()
        {
            pos_ = 0;
            size_ = 0;
            offset_ = 0;
            for(; it_ != end_ && size_ < 16; ++it_)
            {
                const_buffer buf(*it_);
                if(buf.size() == 0)
                    continue;
                arr_[size_++] = buf;
            }
        }

        static
        std::span<const_buffer>
        data_impl(void* ctx)
        {
            auto& self = *static_cast<impl*>(ctx);
            if(self.pos_ >= self.size_)
                self.refill_window();
            if(self.size_ == 0)
                return {};
            if(self.offset_ > 0)
            {
                auto& buf = self.arr_[self.pos_];
                buf = const_buffer(
                    static_cast<char const*>(buf.data()) + self.offset_,
                    buf.size() - self.offset_);
                self.offset_ = 0;
            }
            return { self.arr_.data() + self.pos_, self.size_ - self.pos_ };
        }

        static
        bool
        consume_impl(void* ctx, std::size_t n)
        {
            auto& self = *static_cast<impl*>(ctx);
            while(n > 0 && self.pos_ < self.size_)
            {
                auto const avail = self.arr_[self.pos_].size() - self.offset_;
                if(n < avail)
                {
                    self.offset_ += n;
                    n = 0;
                }
                else
                {
                    n -= avail;
                    self.offset_ = 0;
                    ++self.pos_;
                }
            }
            if(self.pos_ >= self.size_ && self.it_ != self.end_)
                self.refill_window();
            return self.pos_ < self.size_;
        }

        static
        std::span<const_buffer>
        data_after_destroy(void*)
        {
            BOOST_ASSERT(!"const_buffer_param used after impl destroyed");
            return {};
        }

        static
        bool
        consume_after_destroy(void*, std::size_t)
        {
            BOOST_ASSERT(!"const_buffer_param used after impl destroyed");
            return false;
        }

    public:
        impl(impl const&) = delete;
        impl(impl&&) = delete;
        impl& operator=(impl const&) = delete;
        impl& operator=(impl&&) = delete;

        explicit
        impl(BS const& bs)
            : it_(begin(bs))
            , end_(end(bs))
        {
            ctx_ = this;
            data_ = &data_impl;
            consume_ = &consume_impl;
            refill_window();
        }

        ~impl()
        {
            ctx_ = nullptr;
            data_ = &data_after_destroy;
            consume_ = &consume_after_destroy;
        }
    };

    return impl(bs);
}

/** @copydoc make_const_buffer_param(BS const&)
    @note Deleted overload to prevent passing temporary buffer sequences.
*/
template<class BS>
    requires ConstBufferSequence<std::remove_cvref_t<BS>> &&
             (!std::is_lvalue_reference_v<BS>)
auto
make_const_buffer_param(BS&&) = delete;

/** Create a mutable buffer parameter from a buffer sequence.

    Constructs a type-erased buffer parameter that provides
    incremental access to the given mutable buffer sequence.

    @warning The returned object has the same dangerous lifetime
    semantics as @ref make_const_buffer_param. Read that
    documentation carefully.

    @par Thread Safety
    Not thread-safe.

    @param bs The mutable buffer sequence to wrap. Must remain
        valid for the lifetime of the returned object.

    @return An implementation object that inherits from
        @ref mutable_buffer_param.

    @see make_const_buffer_param, mutable_buffer_param
*/
template<MutableBufferSequence BS>
[[nodiscard]] auto
make_buffer_param(BS const& bs)
{
    class impl : public mutable_buffer_param
    {
        std::array<mutable_buffer, 16> arr_;
        decltype(begin(std::declval<BS const&>())) it_;
        decltype(end(std::declval<BS const&>())) end_;
        std::size_t size_ = 0;
        std::size_t pos_ = 0;
        std::size_t offset_ = 0;

        void
        refill_window()
        {
            pos_ = 0;
            size_ = 0;
            offset_ = 0;
            for(; it_ != end_ && size_ < 16; ++it_)
            {
                mutable_buffer buf(*it_);
                if(buf.size() == 0)
                    continue;
                arr_[size_++] = buf;
            }
        }

        static
        std::span<mutable_buffer>
        data_impl(void* ctx)
        {
            auto& self = *static_cast<impl*>(ctx);
            if(self.pos_ >= self.size_)
                self.refill_window();
            if(self.size_ == 0)
                return {};
            if(self.offset_ > 0)
            {
                auto& buf = self.arr_[self.pos_];
                buf = mutable_buffer(
                    static_cast<char*>(buf.data()) + self.offset_,
                    buf.size() - self.offset_);
                self.offset_ = 0;
            }
            return { self.arr_.data() + self.pos_, self.size_ - self.pos_ };
        }

        static
        bool
        consume_impl(void* ctx, std::size_t n)
        {
            auto& self = *static_cast<impl*>(ctx);
            while(n > 0 && self.pos_ < self.size_)
            {
                auto const avail = self.arr_[self.pos_].size() - self.offset_;
                if(n < avail)
                {
                    self.offset_ += n;
                    n = 0;
                }
                else
                {
                    n -= avail;
                    self.offset_ = 0;
                    ++self.pos_;
                }
            }
            if(self.pos_ >= self.size_ && self.it_ != self.end_)
                self.refill_window();
            return self.pos_ < self.size_;
        }

        static
        std::span<mutable_buffer>
        data_after_destroy(void*)
        {
            BOOST_ASSERT(!"mutable_buffer_param used after impl destroyed");
            return {};
        }

        static
        bool
        consume_after_destroy(void*, std::size_t)
        {
            BOOST_ASSERT(!"mutable_buffer_param used after impl destroyed");
            return false;
        }

    public:
        impl(impl const&) = delete;
        impl(impl&&) = delete;
        impl& operator=(impl const&) = delete;
        impl& operator=(impl&&) = delete;

        explicit
        impl(BS const& bs)
            : it_(begin(bs))
            , end_(end(bs))
        {
            ctx_ = this;
            data_ = &data_impl;
            consume_ = &consume_impl;
            refill_window();
        }

        ~impl()
        {
            ctx_ = nullptr;
            data_ = &data_after_destroy;
            consume_ = &consume_after_destroy;
        }
    };

    return impl(bs);
}

/** @copydoc make_buffer_param(BS const&)
    @note Deleted overload to prevent passing temporary buffer sequences.
*/
template<class BS>
    requires MutableBufferSequence<std::remove_cvref_t<BS>> &&
             (!std::is_lvalue_reference_v<BS>)
auto
make_buffer_param(BS&&) = delete;

} // namespace capy
} // namespace boost

#endif
