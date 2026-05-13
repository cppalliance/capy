//
// Copyright (c) 2026 Mungo Gill
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include <boost/capy.hpp>
#include <iostream>
#include <string>
#include <array>
#include <vector>

namespace capy = boost::capy;

void demonstrate_single_buffers()
{
    std::cout << "=== Single Buffer Examples ===\n\n";
    
    // Create buffers from various sources
    std::string str = "Hello, World!";
    char arr[] = "Array data";
    std::vector<char> vec = {'V', 'e', 'c', 't', 'o', 'r'};
    
    // make_buffer creates buffer views (no copies)
    auto str_buf = capy::make_buffer(str);  // mutable_buffer
    auto arr_buf = capy::make_buffer(arr, sizeof(arr) - 1);  // mutable_buffer - Exclude null terminator
    auto vec_buf = capy::make_buffer(vec);  // mutable_buffer
    
    std::cout << "String buffer: " << str_buf.size() << " bytes\n";
    std::cout << "Array buffer:  " << arr_buf.size() << " bytes\n";
    std::cout << "Vector buffer: " << vec_buf.size() << " bytes\n";
}

void demonstrate_two_buffer_scatter()
{
    std::cout << "\n=== Two-Buffer Scatter/Gather ===\n\n";
    
    // Two-element scatter/gather sequence (just a std::array)
    std::string header = "Content-Type: text/plain\r\n\r\n";
    std::string body = "Hello, World!";

    std::array<capy::const_buffer, 2> message = {{
        capy::make_buffer(header),
        capy::make_buffer(body)
    }};
    
    // Calculate total size
    std::size_t total = capy::buffer_size(message);
    std::cout << "Total message size: " << total << " bytes\n";
    std::cout << "Buffer count: " << capy::buffer_length(message) << "\n";
    
    // Iterate through buffers
    std::cout << "\nBuffer contents:\n";
    for (auto const& buf : message)  // const_buffer const&
    {
        std::cout << "  [" << buf.size() << " bytes]: ";
        std::cout.write(static_cast<char const*>(buf.data()), buf.size());
        std::cout << "\n";
    }
}

void demonstrate_buffer_array()
{
    std::cout << "\n=== Multi-Buffer Array ===\n\n";
    
    // Use std::array for more than 2 buffers
    std::string status = "HTTP/1.1 200 OK\r\n";
    std::string content_type = "Content-Type: application/json\r\n";
    std::string server = "Server: Capy/1.0\r\n";
    std::string empty_line = "\r\n";
    std::string body = R"({"status":"ok"})";
    
    std::array<capy::const_buffer, 5> http_response = {{
        capy::make_buffer(status),
        capy::make_buffer(content_type),
        capy::make_buffer(server),
        capy::make_buffer(empty_line),
        capy::make_buffer(body)
    }};
    
    std::size_t total = capy::buffer_size(http_response);
    std::cout << "HTTP response size: " << total << " bytes\n";
    std::cout << "Buffer count: " << capy::buffer_length(http_response) << "\n";
    
    // In real code with streams:
    // co_await write(stream, http_response);
    // This performs scatter/gather I/O - single syscall for all buffers
}

void demonstrate_mutable_buffers()
{
    std::cout << "\n=== Mutable Buffer Example ===\n\n";
    
    // Mutable buffers for receiving data
    char buf1[64];
    char buf2[64];
    
    std::array<capy::mutable_buffer, 2> recv_buffers = {{
        capy::mutable_buffer(buf1, sizeof(buf1)),
        capy::mutable_buffer(buf2, sizeof(buf2))
    }};
    
    std::cout << "Prepared " << capy::buffer_length(recv_buffers) 
              << " buffers with " << capy::buffer_size(recv_buffers) 
              << " bytes total capacity\n";
    
    // In real code:
    // auto [ec, n] = co_await stream.read_some(recv_buffers);
}

int main()
{
    demonstrate_single_buffers();
    demonstrate_two_buffer_scatter();
    demonstrate_buffer_array();
    demonstrate_mutable_buffers();
    
    return 0;
}
