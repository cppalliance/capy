//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_EXAMPLE_CUDA_DATAMOVEMENT_HPP
#define BOOST_CAPY_EXAMPLE_CUDA_DATAMOVEMENT_HPP

#include <boost/capy/continuation.hpp>
#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/io_result.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/concept/const_buffer_sequence.hpp>

#include <cuda_runtime.h>

#include <coroutine>
#include <cstddef>
#include <string>
#include <system_error>
#include <utility>

namespace boost {
namespace capy {
namespace example {

/// Error category for `cudaError_t` values.
class cuda_error_category
    : public std::error_category
{
public:
    char const* name() const noexcept override
    {
        return "cuda";
    }

    std::string message(int ev) const override
    {
        return ::cudaGetErrorString(static_cast<cudaError_t>(ev));
    }
};

/// Return the singleton CUDA error category.
inline std::error_category const& cuda_category() noexcept
{
    static cuda_error_category const cat;
    return cat;
}

/// Convert a `cudaError_t` to a `std::error_code`.
inline std::error_code make_cuda_error(cudaError_t e) noexcept
{
    return std::error_code(static_cast<int>(e), cuda_category());
}

/// Return the stream's sticky error, or success if it is idle or busy.
///
/// `cudaLaunchHostFunc` passes no completion status to its host function,
/// so a callback-based awaitable queries the stream after resumption,
/// back on a worker thread where CUDA calls are permitted.
inline std::error_code stream_error(cudaStream_t s) noexcept
{
    auto st = cudaStreamQuery(s);
    if(st == cudaSuccess || st == cudaErrorNotReady)
        return {};
    return make_cuda_error(st);
}

/// A minimal hand-rolled CUDA-completion awaitable (no executor
/// affinity, cancellation, or frame allocator). Resumes on the CUDA
/// driver callback thread.
struct cuda_stream_awaiter
{
    cudaStream_t stream;

    bool await_ready() const noexcept
    {
        return false;
    }

    void await_suspend(std::coroutine_handle<> h)
    {
        cudaLaunchHostFunc(stream,
            [](void* data)
            {
                std::coroutine_handle<>::from_address(data).resume();
            },
            h.address());
    }

    void await_resume() noexcept
    {
    }
};

/// A CUDA stream whose data-movement operations are IoAwaitables.
///
/// `memcpy_h2d`/`memcpy_d2h` issue a `cudaMemcpyAsync` and resume the
/// awaiting coroutine on `env->executor` when the stream's
/// `cudaLaunchHostFunc` callback fires. One operation is in flight per
/// stream at a time, so the resume context is a pre-allocated member
/// rather than a per-operation allocation.
class cuda_stream
{
    cudaStream_t stream_ = nullptr;
    continuation cont_;
    std::error_code error_;

    struct resume_ctx
    {
        executor_ref ex;
        continuation* cont = nullptr;
    };

    resume_ctx ctx_;

    static void CUDART_CB
    on_complete(void* arg)
    {
        auto* ctx = static_cast<resume_ctx*>(arg);
        ctx->ex.post(*ctx->cont);
    }

    // The paper hardcodes HostToDevice and describes memcpy_d2h as "the
    // same pattern"; a kind field unifies both without duplicating the
    // awaitable.
    struct copy_awaitable
    {
        cuda_stream* self;
        void* dst;
        void const* src;
        std::size_t count;
        cudaMemcpyKind kind;

        bool await_ready() const noexcept
        {
            return false;
        }

        std::coroutine_handle<>
        await_suspend(std::coroutine_handle<> h, io_env const* env)
        {
            auto err = cudaMemcpyAsync(
                dst, src, count, kind, self->stream_);
            if(err != cudaSuccess)
            {
                self->error_ = make_cuda_error(err);
                return h;
            }
            self->cont_.h = h;
            self->ctx_ = resume_ctx{env->executor, &self->cont_};
            err = cudaLaunchHostFunc(
                self->stream_, &on_complete, &self->ctx_);
            if(err != cudaSuccess)
            {
                self->error_ = make_cuda_error(err);
                return h;
            }
            return std::noop_coroutine();
        }

        void await_resume()
        {
            if(! self->error_)
                self->error_ = stream_error(self->stream_);
            if(self->error_)
                throw std::system_error(
                    std::exchange(self->error_, {}));
        }
    };

    struct sync_awaitable
    {
        cuda_stream* self;

        bool await_ready() const noexcept
        {
            return false;
        }

        std::coroutine_handle<>
        await_suspend(std::coroutine_handle<> h, io_env const* env)
        {
            self->cont_.h = h;
            self->ctx_ = resume_ctx{env->executor, &self->cont_};
            auto err = cudaLaunchHostFunc(
                self->stream_, &on_complete, &self->ctx_);
            if(err != cudaSuccess)
            {
                self->error_ = make_cuda_error(err);
                return h;
            }
            return std::noop_coroutine();
        }

        void await_resume()
        {
            if(! self->error_)
                self->error_ = stream_error(self->stream_);
            if(self->error_)
                throw std::system_error(
                    std::exchange(self->error_, {}));
        }
    };

public:
    cuda_stream()
    {
        auto err = cudaStreamCreate(&stream_);
        if(err != cudaSuccess)
            throw std::system_error(make_cuda_error(err));
    }

    ~cuda_stream()
    {
        if(stream_)
            cudaStreamDestroy(stream_);
    }

    cuda_stream(cuda_stream&& other) noexcept
        : stream_(std::exchange(other.stream_, nullptr))
    {
    }

    cuda_stream& operator=(cuda_stream&& other) noexcept
    {
        if(this != &other)
        {
            if(stream_)
                cudaStreamDestroy(stream_);
            stream_ = std::exchange(other.stream_, nullptr);
        }
        return *this;
    }

    cuda_stream(cuda_stream const&) = delete;
    cuda_stream& operator=(cuda_stream const&) = delete;

    /// Return the underlying CUDA stream handle.
    cudaStream_t native_handle() const noexcept
    {
        return stream_;
    }

    /// Asynchronously copy `count` bytes from host `src` to device `dst`.
    auto memcpy_h2d(void* dst, void const* src, std::size_t count)
    {
        return copy_awaitable{
            this, dst, src, count, cudaMemcpyHostToDevice};
    }

    /// Asynchronously copy `count` bytes from device `src` to host `dst`.
    auto memcpy_d2h(void* dst, void const* src, std::size_t count)
    {
        return copy_awaitable{
            this, dst, src, count, cudaMemcpyDeviceToHost};
    }

    /// Asynchronously wait for all pending stream operations to complete.
    auto synchronize()
    {
        return sync_awaitable{this};
    }
};

/// GPU device memory exposed as a WriteStream.
///
/// Reshapes the `cuda_stream` memcpy pattern to satisfy `WriteStream`, so device
/// memory can hide behind `any_write_stream`. A buffer sequence is one
/// batch: every buffer is enqueued as its own `cudaMemcpyAsync` and a
/// single host function follows the last, so the stream keeps its queue
/// depth and the coroutine suspends once per `write_some`. Because
/// `cudaMemcpyAsync` transfers each buffer in one operation, `write_some`
/// never performs a partial write. Errors are delivered via `io_result`
/// rather than exceptions. Does not own `stream_`; the caller is
/// responsible for the stream's lifetime.
class cuda_device_stream
{
    cudaStream_t stream_;
    std::byte* d_ptr_;
    std::size_t offset_ = 0;
    continuation cont_;
    std::error_code error_;

    struct resume_ctx
    {
        executor_ref ex;
        continuation* cont = nullptr;
    };

    resume_ctx ctx_;

    static void CUDART_CB
    on_complete(void* arg)
    {
        auto* ctx = static_cast<resume_ctx*>(arg);
        ctx->ex.post(*ctx->cont);
    }

public:
    cuda_device_stream(cudaStream_t s, std::byte* device_ptr)
        : stream_(s)
        , d_ptr_(device_ptr)
    {
    }

    template<ConstBufferSequence Buffers>
    auto write_some(Buffers buffers)
    {
        struct awaitable
        {
            cuda_device_stream* self;
            Buffers buffers;
            std::size_t total = 0;

            bool await_ready() const noexcept
            {
                return false;
            }

            std::coroutine_handle<>
            await_suspend(std::coroutine_handle<> h, io_env const* env)
            {
                // Enqueue the whole sequence before the host function so
                // the stream runs the batch back to back.
                auto const end = capy::end(buffers);
                for(auto it = capy::begin(buffers); it != end; ++it)
                {
                    const_buffer b = *it;
                    auto err = cudaMemcpyAsync(
                        self->d_ptr_ + self->offset_ + total,
                        b.data(), b.size(),
                        cudaMemcpyHostToDevice,
                        self->stream_);
                    if(err != cudaSuccess)
                    {
                        self->error_ = make_cuda_error(err);
                        return h;
                    }
                    total += b.size();
                }
                self->cont_.h = h;
                self->ctx_ = resume_ctx{env->executor, &self->cont_};
                auto err = cudaLaunchHostFunc(
                    self->stream_, &on_complete, &self->ctx_);
                if(err != cudaSuccess)
                {
                    self->error_ = make_cuda_error(err);
                    return h;
                }
                return std::noop_coroutine();
            }

            io_result<std::size_t>
            await_resume()
            {
                if(! self->error_)
                    self->error_ = stream_error(self->stream_);
                if(self->error_)
                    return {std::exchange(self->error_, {}), 0};
                self->offset_ += total;
                return {std::error_code(), total};
            }
        };
        return awaitable{this, std::move(buffers)};
    }
};

} // namespace example
} // namespace capy
} // namespace boost

#endif
