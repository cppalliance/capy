//
// Copyright (c) 2026 Mungo Gill
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include <boost/capy.hpp>
#include <boost/capy/test/stream.hpp>
#include <boost/capy/test/fuse.hpp>
#include <boost/capy/io/any_stream.hpp>
#include <boost/capy/cond.hpp>
#include <iostream>
#include <cassert>
#include <cctype>

using namespace boost::capy;

// A simple protocol: read until newline, echo back uppercase
// Takes any_stream& so the function is transport-independent
task<bool> echo_line_uppercase(any_stream& stream)
{
    std::string line;
    char c;
    
    // Read character by character until newline
    while (true)
    {
        // ec: std::error_code, n: std::size_t
        auto [ec, n] = co_await stream.read_some(mutable_buffer(&c, 1));
        
        if (ec)
        {
            if (ec == cond::eof)
                break;
            co_return false;
        }
        
        if (c == '\n')
            break;
        
        line += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    
    line += '\n';
    
    // Echo uppercase - loop until all bytes written
    std::size_t written = 0;  // std::size_t - total bytes written
    while (written < line.size())
    {
        // wec: std::error_code, wn: std::size_t
        auto [wec, wn] = co_await stream.write_some(
            const_buffer(line.data() + written, line.size() - written));
        
        if (wec)
            co_return false;
        
        written += wn;
    }
    
    co_return true;
}

void test_happy_path()
{
    std::cout << "Test: happy path\n";
    
    // Use fuse in disarmed mode (no error injection) for happy path
    test::fuse f;  // test::fuse
    test::stream mock(f);  // test::stream
    mock.provide("hello\n");
    
    // Wrap mock in any_stream using pointer construction for reference semantics
    any_stream stream{&mock};  // any_stream
    
    bool result = false;  // bool
    test::run_blocking([&](bool r) { result = r; })(echo_line_uppercase(stream));
    
    assert(result == true);
    assert(mock.data() == "HELLO\n");
    
    std::cout << "  PASSED\n";
}

void test_partial_reads()
{
    std::cout << "Test: partial reads (1 byte at a time)\n";
    
    // Use fuse in disarmed mode (no error injection)
    test::fuse f;  // test::fuse
    // Mock returns at most 1 byte per read_some
    test::stream mock(f, 1);  // test::stream, max_read_size = 1
    mock.provide("hi\n");
    
    // Wrap mock in any_stream using pointer construction for reference semantics
    any_stream stream{&mock};  // any_stream
    
    bool result = false;  // bool
    test::run_blocking([&](bool r) { result = r; })(echo_line_uppercase(stream));
    
    assert(result == true);
    assert(mock.data() == "HI\n");
    
    std::cout << "  PASSED\n";
}

void test_with_error_injection()
{
    std::cout << "Test: error injection\n";
    
    int success_count = 0;
    int error_count = 0;
    
    // fuse::armed runs the test repeatedly, failing at each
    // operation point until all paths are covered
    test::fuse f;  // test::fuse
    auto r = f.armed([&](test::fuse&) -> task<> {  // fuse::result
        test::stream mock(f);  // test::stream
        mock.provide("test\n");
        
        // Wrap mock in any_stream using pointer construction for reference semantics
        any_stream stream{&mock};  // any_stream
        
        // Run the protocol - fuse will inject errors at each step
        bool result = co_await echo_line_uppercase(stream);  // bool
        
        // Either succeeds with correct output, or fails cleanly
        if (result)
        {
            ++success_count;
            assert(mock.data() == "TEST\n");
        }
        else
        {
            ++error_count;
        }
    });
    
    // Verify that fuse testing exercised both paths
    std::cout << "  Runs: " << (success_count + error_count) 
              << " (success=" << success_count 
              << ", error=" << error_count << ")\n";
    
    assert(r.success);
    assert(success_count > 0);  // At least one successful run
    assert(error_count > 0);    // At least one error-injected run
    
    std::cout << "  PASSED (all error paths tested)\n";
}

int main()
{
    test_happy_path();
    test_partial_reads();
    test_with_error_injection();
    
    std::cout << "\nAll tests passed!\n";
    return 0;
}
