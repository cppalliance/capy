//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/concept/dynamic_buffer.hpp>

#include <cstddef>
#include <span>
#include <vector>

#include "test_suite.hpp"

namespace boost {
namespace capy {

namespace {

//----------------------------------------------------------
// Valid DynamicBuffer types
//----------------------------------------------------------

struct valid_dynamic_buffer
{
    using const_buffers_type = std::span<const_buffer const>;
    using mutable_buffers_type = std::span<mutable_buffer const>;

    std::size_t size() const { return 0; }
    std::size_t max_size() const { return 0; }
    std::size_t capacity() const { return 0; }
    const_buffers_type data() const { return {}; }
    mutable_buffers_type prepare(std::size_t) { return {}; }
    void commit(std::size_t) {}
    void consume(std::size_t) {}
};

// Valid: single buffer types as buffer sequences
struct valid_dynamic_buffer_single
{
    using const_buffers_type = const_buffer;
    using mutable_buffers_type = mutable_buffer;

    std::size_t size() const { return 0; }
    std::size_t max_size() const { return 0; }
    std::size_t capacity() const { return 0; }
    const_buffers_type data() const { return {}; }
    mutable_buffers_type prepare(std::size_t) { return {}; }
    void commit(std::size_t) {}
    void consume(std::size_t) {}
};

// Valid: with adapter tag (for DynamicBufferParam)
struct valid_dynamic_buffer_adapter
{
    using is_dynamic_buffer_adapter = void;
    using const_buffers_type = std::span<const_buffer const>;
    using mutable_buffers_type = std::span<mutable_buffer const>;

    std::size_t size() const { return 0; }
    std::size_t max_size() const { return 0; }
    std::size_t capacity() const { return 0; }
    const_buffers_type data() const { return {}; }
    mutable_buffers_type prepare(std::size_t) { return {}; }
    void commit(std::size_t) {}
    void consume(std::size_t) {}
};

//----------------------------------------------------------
// Invalid DynamicBuffer types
//----------------------------------------------------------

// Invalid: missing const_buffers_type
struct invalid_missing_const_buffers_type
{
    using mutable_buffers_type = std::span<mutable_buffer const>;

    std::size_t size() const { return 0; }
    std::size_t max_size() const { return 0; }
    std::size_t capacity() const { return 0; }
    const_buffer data() const { return {}; }
    mutable_buffers_type prepare(std::size_t) { return {}; }
    void commit(std::size_t) {}
    void consume(std::size_t) {}
};

// Invalid: missing mutable_buffers_type
struct invalid_missing_mutable_buffers_type
{
    using const_buffers_type = std::span<const_buffer const>;

    std::size_t size() const { return 0; }
    std::size_t max_size() const { return 0; }
    std::size_t capacity() const { return 0; }
    const_buffers_type data() const { return {}; }
    mutable_buffer prepare(std::size_t) { return {}; }
    void commit(std::size_t) {}
    void consume(std::size_t) {}
};

// Invalid: missing size()
struct invalid_missing_size
{
    using const_buffers_type = std::span<const_buffer const>;
    using mutable_buffers_type = std::span<mutable_buffer const>;

    std::size_t max_size() const { return 0; }
    std::size_t capacity() const { return 0; }
    const_buffers_type data() const { return {}; }
    mutable_buffers_type prepare(std::size_t) { return {}; }
    void commit(std::size_t) {}
    void consume(std::size_t) {}
};

// Invalid: missing max_size()
struct invalid_missing_max_size
{
    using const_buffers_type = std::span<const_buffer const>;
    using mutable_buffers_type = std::span<mutable_buffer const>;

    std::size_t size() const { return 0; }
    std::size_t capacity() const { return 0; }
    const_buffers_type data() const { return {}; }
    mutable_buffers_type prepare(std::size_t) { return {}; }
    void commit(std::size_t) {}
    void consume(std::size_t) {}
};

// Invalid: missing capacity()
struct invalid_missing_capacity
{
    using const_buffers_type = std::span<const_buffer const>;
    using mutable_buffers_type = std::span<mutable_buffer const>;

    std::size_t size() const { return 0; }
    std::size_t max_size() const { return 0; }
    const_buffers_type data() const { return {}; }
    mutable_buffers_type prepare(std::size_t) { return {}; }
    void commit(std::size_t) {}
    void consume(std::size_t) {}
};

// Invalid: missing data()
struct invalid_missing_data
{
    using const_buffers_type = std::span<const_buffer const>;
    using mutable_buffers_type = std::span<mutable_buffer const>;

    std::size_t size() const { return 0; }
    std::size_t max_size() const { return 0; }
    std::size_t capacity() const { return 0; }
    mutable_buffers_type prepare(std::size_t) { return {}; }
    void commit(std::size_t) {}
    void consume(std::size_t) {}
};

// Invalid: missing prepare()
struct invalid_missing_prepare
{
    using const_buffers_type = std::span<const_buffer const>;
    using mutable_buffers_type = std::span<mutable_buffer const>;

    std::size_t size() const { return 0; }
    std::size_t max_size() const { return 0; }
    std::size_t capacity() const { return 0; }
    const_buffers_type data() const { return {}; }
    void commit(std::size_t) {}
    void consume(std::size_t) {}
};

// Invalid: missing commit()
struct invalid_missing_commit
{
    using const_buffers_type = std::span<const_buffer const>;
    using mutable_buffers_type = std::span<mutable_buffer const>;

    std::size_t size() const { return 0; }
    std::size_t max_size() const { return 0; }
    std::size_t capacity() const { return 0; }
    const_buffers_type data() const { return {}; }
    mutable_buffers_type prepare(std::size_t) { return {}; }
    void consume(std::size_t) {}
};

// Invalid: missing consume()
struct invalid_missing_consume
{
    using const_buffers_type = std::span<const_buffer const>;
    using mutable_buffers_type = std::span<mutable_buffer const>;

    std::size_t size() const { return 0; }
    std::size_t max_size() const { return 0; }
    std::size_t capacity() const { return 0; }
    const_buffers_type data() const { return {}; }
    mutable_buffers_type prepare(std::size_t) { return {}; }
    void commit(std::size_t) {}
};

// Invalid: data() returns wrong type
struct invalid_data_wrong_type
{
    using const_buffers_type = std::span<const_buffer const>;
    using mutable_buffers_type = std::span<mutable_buffer const>;

    std::size_t size() const { return 0; }
    std::size_t max_size() const { return 0; }
    std::size_t capacity() const { return 0; }
    int data() const { return 0; }
    mutable_buffers_type prepare(std::size_t) { return {}; }
    void commit(std::size_t) {}
    void consume(std::size_t) {}
};

// Invalid: prepare() returns wrong type
struct invalid_prepare_wrong_type
{
    using const_buffers_type = std::span<const_buffer const>;
    using mutable_buffers_type = std::span<mutable_buffer const>;

    std::size_t size() const { return 0; }
    std::size_t max_size() const { return 0; }
    std::size_t capacity() const { return 0; }
    const_buffers_type data() const { return {}; }
    int prepare(std::size_t) { return 0; }
    void commit(std::size_t) {}
    void consume(std::size_t) {}
};

// Invalid: const_buffers_type not a ConstBufferSequence
struct invalid_const_buffers_not_sequence
{
    using const_buffers_type = int;
    using mutable_buffers_type = std::span<mutable_buffer const>;

    std::size_t size() const { return 0; }
    std::size_t max_size() const { return 0; }
    std::size_t capacity() const { return 0; }
    const_buffers_type data() const { return 0; }
    mutable_buffers_type prepare(std::size_t) { return {}; }
    void commit(std::size_t) {}
    void consume(std::size_t) {}
};

// Invalid: mutable_buffers_type not a MutableBufferSequence
struct invalid_mutable_buffers_not_sequence
{
    using const_buffers_type = std::span<const_buffer const>;
    using mutable_buffers_type = int;

    std::size_t size() const { return 0; }
    std::size_t max_size() const { return 0; }
    std::size_t capacity() const { return 0; }
    const_buffers_type data() const { return {}; }
    mutable_buffers_type prepare(std::size_t) { return 0; }
    void commit(std::size_t) {}
    void consume(std::size_t) {}
};

} // namespace

//----------------------------------------------------------
// Static assertions: DynamicBuffer
//----------------------------------------------------------

// Valid types satisfy DynamicBuffer
static_assert(DynamicBuffer<valid_dynamic_buffer>);
static_assert(DynamicBuffer<valid_dynamic_buffer_single>);
static_assert(DynamicBuffer<valid_dynamic_buffer_adapter>);

// Missing type aliases
static_assert(!DynamicBuffer<invalid_missing_const_buffers_type>);
static_assert(!DynamicBuffer<invalid_missing_mutable_buffers_type>);

// Missing member functions
static_assert(!DynamicBuffer<invalid_missing_size>);
static_assert(!DynamicBuffer<invalid_missing_max_size>);
static_assert(!DynamicBuffer<invalid_missing_capacity>);
static_assert(!DynamicBuffer<invalid_missing_data>);
static_assert(!DynamicBuffer<invalid_missing_prepare>);
static_assert(!DynamicBuffer<invalid_missing_commit>);
static_assert(!DynamicBuffer<invalid_missing_consume>);

// Wrong return types
static_assert(!DynamicBuffer<invalid_data_wrong_type>);
static_assert(!DynamicBuffer<invalid_prepare_wrong_type>);

// Buffer sequence type constraints
static_assert(!DynamicBuffer<invalid_const_buffers_not_sequence>);
static_assert(!DynamicBuffer<invalid_mutable_buffers_not_sequence>);

//----------------------------------------------------------
// Static assertions: DynamicBufferParam
//----------------------------------------------------------

// Lvalue references satisfy DynamicBufferParam
static_assert(DynamicBufferParam<valid_dynamic_buffer&>);
static_assert(DynamicBufferParam<valid_dynamic_buffer_adapter&>);

// Rvalue adapters satisfy DynamicBufferParam
static_assert(DynamicBufferParam<valid_dynamic_buffer_adapter&&>);
static_assert(DynamicBufferParam<valid_dynamic_buffer_adapter>);

// Rvalue non-adapters do NOT satisfy DynamicBufferParam
static_assert(!DynamicBufferParam<valid_dynamic_buffer&&>);
static_assert(!DynamicBufferParam<valid_dynamic_buffer>);

// Invalid types don't satisfy DynamicBufferParam
static_assert(!DynamicBufferParam<invalid_missing_size&>);
static_assert(!DynamicBufferParam<int&>);

//----------------------------------------------------------

struct dynamic_buffer_test
{
    void
    run()
    {
    }
};

TEST_SUITE(
    dynamic_buffer_test,
    "boost.capy.concept.dynamic_buffer");

} // namespace capy
} // namespace boost
