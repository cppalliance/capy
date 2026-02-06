//
// Copyright (c) 2026 Mungo Gill
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

//
// Stream Pipeline Example
//
// This example demonstrates chaining buffer sources to create a data
// processing pipeline. Data flows through transform stages:
//
//   input -> uppercase_transform -> line_numbering_transform -> output
//
// Each transform is a BufferSource that wraps an upstream any_buffer_source,
// enabling type-erased composition of arbitrary transform chains.
//
// The transforms use task<> coroutines for their pull() methods, allowing
// them to properly co_await the upstream source.
//

#include <boost/capy.hpp>
#include <boost/capy/test/run_blocking.hpp>
#include <boost/capy/test/buffer_source.hpp>
#include <boost/capy/test/write_sink.hpp>
#include <boost/capy/io/any_buffer_source.hpp>
#include <boost/capy/io/any_write_sink.hpp>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <system_error>

using namespace boost::capy;

//------------------------------------------------------------------------------
//
// Transform: uppercase_transform
//
// A BufferSource that pulls from an upstream source and converts all
// characters to uppercase. Demonstrates a simple byte-by-byte transform.
//
//------------------------------------------------------------------------------

class uppercase_transform
{
    any_buffer_source* source_;  // any_buffer_source*
    std::vector<char> buffer_;   // std::vector<char> - transformed data
    std::size_t consumed_ = 0;   // std::size_t - bytes consumed by downstream
    bool exhausted_ = false;     // bool - upstream exhausted
    
public:
    explicit uppercase_transform(any_buffer_source& source)
        : source_(&source)
    {
    }
    
    // BufferSource::consume - advance past processed bytes
    void
    consume(std::size_t n) noexcept
    {
        consumed_ += n;
        // Compact buffer when fully consumed
        if (consumed_ >= buffer_.size())
        {
            buffer_.clear();
            consumed_ = 0;
        }
    }
    
    // BufferSource::pull - returns task<> to enable co_await on upstream
    io_task<std::span<const_buffer>>
    pull(std::span<const_buffer> dest)
    {
        // Already have unconsumed data?
        if (consumed_ < buffer_.size())
        {
            if (dest.empty())
                co_return {std::error_code{}, std::span<const_buffer>{}};
            
            dest[0] = const_buffer(
                buffer_.data() + consumed_,
                buffer_.size() - consumed_);
            co_return {std::error_code{}, dest.first(1)};
        }
        
        // Upstream exhausted?
        if (exhausted_)
            co_return {std::error_code{}, std::span<const_buffer>{}};
        
        // Pull from upstream
        buffer_.clear();
        consumed_ = 0;
        
        const_buffer upstream[8];  // const_buffer[8]
        // ec: std::error_code, bufs: std::span<const_buffer>
        auto [ec, bufs] = co_await source_->pull(upstream);
        
        if (ec)
            co_return {ec, std::span<const_buffer>{}};
        
        if (bufs.empty())
        {
            exhausted_ = true;
            co_return {std::error_code{}, std::span<const_buffer>{}};
        }
        
        // Transform: uppercase each byte
        for (auto const& buf : bufs)  // const_buffer const&
        {
            auto const* data = static_cast<char const*>(buf.data());  // char const*
            auto size = buf.size();  // std::size_t
            
            for (std::size_t i = 0; i < size; ++i)
            {
                buffer_.push_back(static_cast<char>(
                    std::toupper(static_cast<unsigned char>(data[i]))));
            }
        }
        
        // Consume from upstream
        source_->consume(buffer_size(bufs));
        
        // Return transformed data
        if (dest.empty() || buffer_.empty())
            co_return {std::error_code{}, std::span<const_buffer>{}};
        
        dest[0] = const_buffer(buffer_.data(), buffer_.size());
        co_return {std::error_code{}, dest.first(1)};
    }
};

//------------------------------------------------------------------------------
//
// Transform: line_numbering_transform
//
// A BufferSource that pulls from an upstream source and prepends line
// numbers to each line. Demonstrates a transform that changes data size.
//
//------------------------------------------------------------------------------

class line_numbering_transform
{
    any_buffer_source* source_;  // any_buffer_source*
    std::string buffer_;         // std::string - transformed data
    std::size_t consumed_ = 0;   // std::size_t - bytes consumed by downstream
    std::size_t line_num_ = 1;   // std::size_t - current line number
    bool at_line_start_ = true;  // bool - are we at start of a line?
    bool exhausted_ = false;     // bool - upstream exhausted
    
public:
    explicit line_numbering_transform(any_buffer_source& source)
        : source_(&source)
    {
    }
    
    // BufferSource::consume - advance past processed bytes
    void
    consume(std::size_t n) noexcept
    {
        consumed_ += n;
        // Compact buffer when fully consumed
        if (consumed_ >= buffer_.size())
        {
            buffer_.clear();
            consumed_ = 0;
        }
    }
    
    // BufferSource::pull - returns task<> to enable co_await on upstream
    io_task<std::span<const_buffer>>
    pull(std::span<const_buffer> dest)
    {
        // Already have unconsumed data?
        if (consumed_ < buffer_.size())
        {
            if (dest.empty())
                co_return {std::error_code{}, std::span<const_buffer>{}};
            
            dest[0] = const_buffer(
                buffer_.data() + consumed_,
                buffer_.size() - consumed_);
            co_return {std::error_code{}, dest.first(1)};
        }
        
        // Upstream exhausted?
        if (exhausted_)
            co_return {std::error_code{}, std::span<const_buffer>{}};
        
        // Pull from upstream
        buffer_.clear();
        consumed_ = 0;
        
        const_buffer upstream[8];  // const_buffer[8]
        // ec: std::error_code, bufs: std::span<const_buffer>
        auto [ec, bufs] = co_await source_->pull(upstream);
        
        if (ec)
            co_return {ec, std::span<const_buffer>{}};
        
        if (bufs.empty())
        {
            exhausted_ = true;
            co_return {std::error_code{}, std::span<const_buffer>{}};
        }
        
        // Transform: add line numbers
        for (auto const& buf : bufs)  // const_buffer const&
        {
            auto const* data = static_cast<char const*>(buf.data());  // char const*
            auto size = buf.size();  // std::size_t
            
            for (std::size_t i = 0; i < size; ++i)
            {
                if (at_line_start_)
                {
                    buffer_ += std::to_string(line_num_++) + ": ";
                    at_line_start_ = false;
                }
                buffer_ += data[i];
                if (data[i] == '\n')
                    at_line_start_ = true;
            }
        }
        
        // Consume from upstream
        source_->consume(buffer_size(bufs));
        
        // Return transformed data
        if (dest.empty() || buffer_.empty())
            co_return {std::error_code{}, std::span<const_buffer>{}};
        
        dest[0] = const_buffer(buffer_.data(), buffer_.size());
        co_return {std::error_code{}, dest.first(1)};
    }
};

//------------------------------------------------------------------------------
//
// transfer: Pull from source and write to sink until exhausted
//
//------------------------------------------------------------------------------

task<std::size_t> transfer(any_buffer_source& source, any_write_sink& sink)
{
    std::size_t total = 0;  // std::size_t
    const_buffer bufs[8];   // const_buffer[8]
    
    for (;;)
    {
        // ec: std::error_code, spans: std::span<const_buffer>
        auto [ec, spans] = co_await source.pull(bufs);
        
        if (ec)
            throw std::system_error(ec);
        
        if (spans.empty())
            break;
        
        // Write each buffer to sink
        for (auto const& buf : spans)  // const_buffer const&
        {
            // wec: std::error_code, n: std::size_t
            auto [wec, n] = co_await sink.write(buf);
            if (wec)
                throw std::system_error(wec);
            total += n;
        }
        
        // Consume what we read
        source.consume(buffer_size(spans));
    }
    
    io_result<> eof_result = co_await sink.write_eof();
    if (eof_result.ec)
        throw std::system_error(eof_result.ec);
    
    co_return total;
}

//------------------------------------------------------------------------------
//
// demo_pipeline: Demonstrate chained transforms
//
//------------------------------------------------------------------------------

void demo_pipeline()
{
    std::cout << "=== Stream Pipeline Demo ===\n\n";
    
    // Input data - three lines
    std::string input = "hello world\nthis is a test\nof the pipeline\n";
    std::cout << "Input:\n" << input << "\n";
    
    // Create mock source with input data
    test::fuse f;  // test::fuse
    test::buffer_source source(f);  // test::buffer_source
    source.provide(input);
    
    // Build the pipeline using type-erased buffer sources:
    //   source -> [uppercase] -> [line_numbering] -> sink
    
    // Stage 1: Wrap raw source as any_buffer_source.
    // Using pointer construction (&source) for reference semantics - the
    // wrapper does not take ownership, so source must outlive src.
    any_buffer_source src{&source};  // any_buffer_source
    
    // Stage 2: Uppercase transform wraps src.
    // Again using pointer construction so upper_src references upper
    // without taking ownership.
    uppercase_transform upper{src};  // uppercase_transform
    any_buffer_source upper_src{&upper};  // any_buffer_source
    
    // Stage 3: Line numbering transform wraps upper_src.
    line_numbering_transform numbered{upper_src};  // line_numbering_transform
    any_buffer_source numbered_src{&numbered};  // any_buffer_source
    
    // Create sink to collect output.
    // Pointer construction ensures sink outlives dst.
    test::write_sink sink(f);  // test::write_sink
    any_write_sink dst{&sink};  // any_write_sink
    
    // Run the pipeline
    std::size_t bytes = 0;  // std::size_t
    test::run_blocking([&](std::size_t n) { bytes = n; })(
        transfer(numbered_src, dst));
    
    std::cout << "Output (" << bytes << " bytes):\n";
    std::cout << sink.data() << "\n";
    
    // Expected output:
    // 1: HELLO WORLD
    // 2: THIS IS A TEST
    // 3: OF THE PIPELINE
}

int main()
{
    try
    {
        demo_pipeline();
    }
    catch (std::system_error const& e)
    {
        std::cerr << "Pipeline error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
