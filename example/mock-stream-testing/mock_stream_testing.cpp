//
// Copyright (c) 2026 Mungo Gill
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// tag::full[]
#include <boost/capy.hpp>
#include <boost/capy/test/stream.hpp>
#include <boost/capy/test/fuse.hpp>
#include <boost/capy/io/any_stream.hpp>
#include <boost/capy/cond.hpp>
#include <iostream>
#include <cassert>
#include <cctype>

namespace capy = boost::capy;

// A simple protocol: read until newline, echo back uppercase
// Takes any_stream& so the function is transport-independent
capy::task<bool> echo_line_uppercase(capy::any_stream& stream)
{
    std::string line;
    char c;

    // Read character by character until newline
    while (true)
    {
        // ec: std::error_code, n: std::size_t
        auto [ec, n] = co_await stream.read_some(capy::mutable_buffer(&c, 1));

        if (n > 0)
        {
            if (c == '\n')
                break;
            line += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }

        if (ec == capy::cond::eof)
            break;

        if (ec)
            co_return false;
    }
    
    line += '\n';
    
    // Echo uppercase - loop until all bytes written
    std::size_t written = 0;  // std::size_t - total bytes written
    while (written < line.size())
    {
        // wec: std::error_code, wn: std::size_t
        auto [wec, wn] = co_await stream.write_some(
            capy::const_buffer(line.data() + written, line.size() - written));

        written += wn;

        if (wec)
            co_return false;
    }
    
    co_return true;
}

void test_happy_path()
{
    std::cout << "Test: happy path\n";
    
    auto [a, b] = capy::test::make_stream_pair();
    b.provide("hello\n");

    // tag::any_stream[]
    // Wrap one end in any_stream using pointer construction for reference semantics
    capy::any_stream stream{&a};  // any_stream
    // end::any_stream[]

    // tag::run_blocking[]
    bool result = false;  // bool
    capy::test::run_blocking([&](bool r) { result = r; })(echo_line_uppercase(stream));
    // end::run_blocking[]

    assert(result == true);
    assert(b.data() == "HELLO\n");
    
    std::cout << "  PASSED\n";
}

void test_partial_reads()
{
    std::cout << "Test: partial reads (1 byte at a time)\n";
    
    auto [a, b] = capy::test::make_stream_pair();
    a.set_max_read_size(1);
    b.provide("hi\n");

    capy::any_stream stream{&a};  // any_stream

    bool result = false;  // bool
    capy::test::run_blocking([&](bool r) { result = r; })(echo_line_uppercase(stream));
    
    assert(result == true);
    assert(b.data() == "HI\n");
    
    std::cout << "  PASSED\n";
}

void test_with_error_injection()
{
    std::cout << "Test: error injection\n";
    
    int success_count = 0;
    int error_count = 0;
    
    // fuse::armed runs the test repeatedly, failing at each
    // operation point until all paths are covered
    capy::test::fuse f;  // test::fuse
    auto r = f.armed([&](capy::test::fuse&) -> capy::task<> {  // fuse::result
        auto [a, b] = capy::test::make_stream_pair(f);
        b.provide("test\n");
        
        capy::any_stream stream{&a};  // any_stream
        
        // Run the protocol - fuse will inject errors at each step
        bool result = co_await echo_line_uppercase(stream);  // bool
        
        // Either succeeds with correct output, or fails cleanly
        if (result)
        {
            ++success_count;
            assert(b.data() == "TEST\n");
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
// end::full[]
