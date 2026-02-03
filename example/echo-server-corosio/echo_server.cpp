//
// Copyright (c) 2026 Mungo Gill
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include <boost/capy.hpp>
#include <boost/corosio.hpp>
#include <iostream>

using namespace boost::capy;
namespace tcp = boost::corosio::tcp;

// Echo handler: receives data and sends it back
task<> echo_session(any_stream& stream, std::string client_info)
{
    std::cout << "[" << client_info << "] Session started\n";
    
    char buffer[1024];
    std::size_t total_bytes = 0;
    
    try
    {
        for (;;)
        {
            // Read some data
            // ec: std::error_code, n: std::size_t
            auto [ec, n] = co_await stream.read_some(mutable_buffer(buffer));
            
            if (ec == cond::eof)
            {
                std::cout << "[" << client_info << "] Client disconnected\n";
                break;
            }
            
            if (ec)
            {
                std::cout << "[" << client_info << "] Read error: " 
                          << ec.message() << "\n";
                break;
            }
            
            total_bytes += n;
            
            // Echo it back
            // wec: std::error_code, wn: std::size_t
            auto [wec, wn] = co_await write(stream, const_buffer(buffer, n));
            
            if (wec)
            {
                std::cout << "[" << client_info << "] Write error: " 
                          << wec.message() << "\n";
                break;
            }
        }
    }
    catch (std::exception const& e)
    {
        std::cout << "[" << client_info << "] Exception: " << e.what() << "\n";
    }
    
    std::cout << "[" << client_info << "] Session ended, "
              << total_bytes << " bytes echoed\n";
}

// Accept loop: accepts connections and spawns handlers
task<> accept_loop(tcp::acceptor& acceptor, executor_ref ex)
{
    std::cout << "Server listening on port " 
              << acceptor.local_endpoint().port() << "\n";
    
    int connection_id = 0;
    
    for (;;)
    {
        // Accept a connection
        // ec: std::error_code, socket: tcp::socket
        auto [ec, socket] = co_await acceptor.async_accept();
        
        if (ec)
        {
            std::cout << "Accept error: " << ec.message() << "\n";
            continue;
        }
        
        // Build client info string
        auto remote = socket.remote_endpoint();  // tcp::endpoint
        std::string client_info = 
            std::to_string(++connection_id) + ":" +
            remote.address().to_string() + ":" +
            std::to_string(remote.port());
        
        std::cout << "[" << client_info << "] Connection accepted\n";
        
        // Wrap socket and spawn handler
        // Note: socket ownership transfers to the lambda
        run_async(ex)(
            [](tcp::socket sock, std::string info) -> task<> {
                any_stream stream{sock};
                co_await echo_session(stream, std::move(info));
            }(std::move(socket), std::move(client_info))
        );
    }
}

int main(int argc, char* argv[])
{
    try
    {
        // Parse port from command line
        unsigned short port = 8080;
        if (argc > 1)
            port = static_cast<unsigned short>(std::stoi(argv[1]));
        
        // Create I/O context and thread pool
        boost::corosio::io_context ioc;
        thread_pool pool(4);
        
        // Create acceptor
        tcp::endpoint endpoint(tcp::v4(), port);
        tcp::acceptor acceptor(ioc, endpoint);
        acceptor.set_option(tcp::acceptor::reuse_address(true));
        
        std::cout << "Starting echo server...\n";
        
        // Run accept loop
        run_async(pool.get_executor())(
            accept_loop(acceptor, pool.get_executor())
        );
        
        // Run the I/O context (this blocks)
        ioc.run();
    }
    catch (std::exception const& e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
