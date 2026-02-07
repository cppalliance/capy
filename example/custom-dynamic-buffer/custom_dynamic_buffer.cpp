//
// Copyright (c) 2026 Mungo Gill
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include <boost/capy.hpp>
#include <boost/capy/test/run_blocking.hpp>
#include <boost/capy/test/stream.hpp>
#include <boost/capy/test/fuse.hpp>
#include <boost/capy/cond.hpp>
#include <iostream>
#include <vector>
#include <cassert>
#include <cstring>

using namespace boost::capy;

// Custom dynamic buffer with statistics tracking
class tracked_buffer
{
    std::vector<char> storage_;
    std::size_t read_pos_ = 0;   // Start of readable data
    std::size_t write_pos_ = 0;  // End of readable data
    std::size_t max_size_;
    
    // Statistics
    std::size_t total_prepared_ = 0;
    std::size_t total_committed_ = 0;
    std::size_t total_consumed_ = 0;
    
public:
    explicit tracked_buffer(std::size_t max_size = 65536)
        : max_size_(max_size)
    {
        storage_.reserve(1024);
    }
    
    // === DynamicBuffer interface ===
    
    // Consumer: readable data
    const_buffer data() const noexcept
    {
        return const_buffer(
            storage_.data() + read_pos_,
            write_pos_ - read_pos_);
    }
    
    // Capacity queries
    std::size_t size() const noexcept
    {
        return write_pos_ - read_pos_;
    }
    
    std::size_t max_size() const noexcept
    {
        return max_size_;
    }
    
    std::size_t capacity() const noexcept
    {
        return storage_.capacity() - read_pos_;
    }
    
    // Producer: prepare space for writing
    mutable_buffer prepare(std::size_t n)
    {
        total_prepared_ += n;
        
        // Compact if needed
        if (storage_.size() + n > storage_.capacity() && read_pos_ > 0)
        {
            compact();
        }
        
        // Grow if needed
        std::size_t required = write_pos_ + n;
        if (required > max_size_)
            throw std::length_error("tracked_buffer: max_size exceeded");
        
        if (required > storage_.size())
            storage_.resize(required);
        
        return mutable_buffer(
            storage_.data() + write_pos_,
            n);
    }
    
    // Producer: mark bytes as written
    void commit(std::size_t n)
    {
        total_committed_ += n;
        write_pos_ += n;
    }
    
    // Consumer: mark bytes as processed
    void consume(std::size_t n)
    {
        std::size_t actual = std::min(n, size());  // std::size_t
        total_consumed_ += actual;
        read_pos_ += actual;
        
        if (read_pos_ == write_pos_)
        {
            // Buffer empty, reset positions
            read_pos_ = 0;
            write_pos_ = 0;
        }
    }
    
    // === Statistics ===
    
    void print_stats() const
    {
        std::cout << "Buffer statistics:\n"
                  << "  Total prepared:  " << total_prepared_ << " bytes\n"
                  << "  Total committed: " << total_committed_ << " bytes\n"
                  << "  Total consumed:  " << total_consumed_ << " bytes\n"
                  << "  Current size:    " << size() << " bytes\n"
                  << "  Capacity:        " << capacity() << " bytes\n";
    }
    
private:
    void compact()
    {
        if (read_pos_ == 0)
            return;
        
        std::size_t len = write_pos_ - read_pos_;
        std::memmove(storage_.data(), storage_.data() + read_pos_, len);
        read_pos_ = 0;
        write_pos_ = len;
    }
};

// Demonstrate using the custom buffer
task<> read_into_tracked_buffer(test::stream& stream, tracked_buffer& buffer)
{
    // Read data in chunks
    while (true)
    {
        auto space = buffer.prepare(256);  // mutable_buffer
        // ec: std::error_code, n: std::size_t
        auto [ec, n] = co_await stream.read_some(space);
        
        if (ec == cond::eof)
            break;
        
        if (ec)
            throw std::system_error(ec);
        
        buffer.commit(n);
        
        std::cout << "Read " << n << " bytes, buffer size now: " 
                  << buffer.size() << "\n";
    }
}

void demo_tracked_buffer()
{
    std::cout << "=== Tracked Buffer Demo ===\n\n";
    
    auto [reader, writer] = test::make_stream_pair();
    writer.provide("Hello, ");
    writer.provide("World! ");
    writer.provide("This is a test of the custom buffer.\n");
    writer.close();
    
    tracked_buffer buffer;
    
    test::run_blocking()(read_into_tracked_buffer(reader, buffer));
    
    std::cout << "\nFinal buffer contents: ";
    auto data = buffer.data();  // const_buffer
    std::cout.write(static_cast<char const*>(data.data()), data.size());
    std::cout << "\n\n";
    
    buffer.print_stats();
    
    // Consume some data
    std::cout << "\nConsuming 7 bytes...\n";
    buffer.consume(7);
    buffer.print_stats();
}

int main()
{
    demo_tracked_buffer();
    return 0;
}
