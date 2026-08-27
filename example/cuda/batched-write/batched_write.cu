//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Runs the batched write path of cuda_device_stream: three host buffers
// go through any_write_stream in one write_some, so a single co_await
// covers the whole sequence, and the device is checked to hold their
// concatenation.

#include "../datamovement/cuda_datamovement.hpp"

#include <boost/capy.hpp>
#include <boost/capy/ex/thread_pool.hpp>

#include <array>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <latch>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace capy = boost::capy;
namespace ex   = capy::example;

capy::io_task<std::size_t>
write_batch(capy::any_write_stream& dest,
    std::array<capy::const_buffer, 3> const& bufs)
{
    co_return co_await dest.write_some(bufs);
}

int main()
{
    constexpr std::string_view parts[] = {"abc", "defg", "hi"};
    std::string expected;
    for(auto p : parts)
        expected += p;

    std::byte* d_ptr = nullptr;
    if(cudaMalloc(&d_ptr, expected.size()) != cudaSuccess)
    {
        std::cout << "cudaMalloc failed; no device available\n";
        return 1;
    }
    cudaStream_t s = nullptr;
    cudaStreamCreate(&s);

    ex::cuda_device_stream gpu(s, d_ptr);
    capy::any_write_stream dest(&gpu);
    std::array<capy::const_buffer, 3> bufs{
        capy::make_buffer(parts[0].data(), parts[0].size()),
        capy::make_buffer(parts[1].data(), parts[1].size()),
        capy::make_buffer(parts[2].data(), parts[2].size())};

    capy::thread_pool pool(2);
    capy::io_result<std::size_t> r;
    std::latch done{1};
    capy::run_async(pool.get_executor(),
        [&](capy::io_result<std::size_t> ir) { r = ir; done.count_down(); })(
            write_batch(dest, bufs));
    done.wait();
    auto const& [ec, n] = r;

    std::vector<char> back(expected.size());
    cudaMemcpy(back.data(), d_ptr, back.size(), cudaMemcpyDeviceToHost);
    cudaStreamDestroy(s);
    cudaFree(d_ptr);

    bool const ok = ! ec && n == expected.size()
        && std::memcmp(back.data(), expected.data(), expected.size()) == 0;
    std::cout << "batched write_some: " << bufs.size() << " buffers, "
        << "one await, " << n << " of " << expected.size()
        << " bytes, device holds \""
        << std::string_view(back.data(), back.size()) << "\": "
        << (ok ? "ok" : "FAILED")
        << (ec ? " (" + ec.message() + ")" : "") << "\n";
    return ok ? 0 : 1;
}
