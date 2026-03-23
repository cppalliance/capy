//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

//
// Type-erased sender for benchmarks.
//
// any_read_sender wraps a concrete sender behind a virtual interface.
// connect() heap-allocates the operation state because its type is
// erased. This is the structural cost of type-erasing a sender:
// connect(sender, receiver) produces op_state<S, R> whose type
// depends on both arguments.
//

#ifndef BOOST_CAPY_BENCH_SENDER_ANY_SENDER_HPP
#define BOOST_CAPY_BENCH_SENDER_ANY_SENDER_HPP

#include <beman/execution/execution.hpp>

#include <coroutine>
#include <cstddef>
#include <cstring>
#include <memory>
#include <utility>

namespace ex = beman::execution;

class any_read_sender
{
    struct callback_receiver
    {
        using receiver_concept = ex::receiver_t;

        void* data_;
        void (*on_value_)(void*, std::size_t) noexcept;
        void (*on_stopped_)(void*) noexcept;

        struct env_t {};
        auto get_env() const noexcept -> env_t { return {}; }

        void set_value(std::size_t n) && noexcept
        {
            on_value_(data_, n);
        }

        void set_stopped() && noexcept
        {
            on_stopped_(data_);
        }

        template <class E>
        void set_error(E&&) && noexcept
        {
            std::terminate();
        }
    };

    struct op_base
    {
        virtual void start() noexcept = 0;
        virtual ~op_base() = default;
    };

    using factory_fn = std::unique_ptr<op_base>(*)(
        void* sender_buf, callback_receiver cr);
    using destroy_fn = void(*)(void* sender_buf) noexcept;

    static constexpr std::size_t buf_size = 64;
    alignas(std::max_align_t) char buf_[buf_size];
    factory_fn factory_;
    destroy_fn destroy_;

public:
    using sender_concept = ex::sender_t;
    using completion_signatures =
        ex::completion_signatures<ex::set_value_t(std::size_t)>;

    template <class Sender>
    explicit any_read_sender(Sender s)
    {
        static_assert(sizeof(Sender) <= buf_size);
        static_assert(alignof(Sender) <= alignof(std::max_align_t));
        new (buf_) Sender(std::move(s));

        factory_ = +[](void* stor,
            callback_receiver r) -> std::unique_ptr<op_base>
        {
            auto& sndr = *static_cast<Sender*>(stor);

            using inner_op_t = decltype(ex::connect(
                std::declval<Sender>(),
                std::declval<callback_receiver>()));

            struct concrete_op : op_base
            {
                inner_op_t inner_;
                concrete_op(Sender s, callback_receiver r)
                    : inner_(ex::connect(
                        std::move(s), std::move(r))) {}
                void start() noexcept override
                {
                    ex::start(inner_);
                }
            };

            return std::make_unique<concrete_op>(
                std::move(sndr), std::move(r));
        };

        destroy_ = +[](void* stor) noexcept {
            static_cast<Sender*>(stor)->~Sender();
        };
    }

    ~any_read_sender() { destroy_(buf_); }

    any_read_sender(any_read_sender const&) = delete;
    any_read_sender& operator=(any_read_sender const&) = delete;

    any_read_sender(any_read_sender&& o) noexcept
        : factory_(o.factory_), destroy_(o.destroy_)
    {
        std::memcpy(buf_, o.buf_, buf_size);
        o.destroy_ = +[](void*) noexcept {};
    }

    any_read_sender& operator=(any_read_sender&&) = delete;

    template <typename Promise>
    auto as_awaitable(Promise&)
    {
        struct aw
        {
            alignas(std::max_align_t) char buf_[buf_size];
            factory_fn factory_;
            destroy_fn destroy_;
            std::unique_ptr<op_base> inner_;
            std::coroutine_handle<> cont_{};
            std::size_t result_{};

            explicit aw(any_read_sender& sndr)
                : factory_(sndr.factory_)
                , destroy_(sndr.destroy_)
            {
                std::memcpy(buf_, sndr.buf_, buf_size);
                sndr.destroy_ = +[](void*) noexcept {};
            }

            ~aw() { destroy_(buf_); }

            aw(aw const&) = delete;
            aw(aw&&) = delete;
            aw& operator=(aw const&) = delete;
            aw& operator=(aw&&) = delete;

            bool await_ready() const noexcept { return false; }

            void await_suspend(
                std::coroutine_handle<> h) noexcept
            {
                cont_ = h;
                inner_ = factory_(buf_, callback_receiver{
                    this,
                    +[](void* p, std::size_t n) noexcept {
                        auto* a = static_cast<aw*>(p);
                        a->result_ = n;
                        a->cont_.resume();
                    },
                    +[](void* p) noexcept {
                        auto* a = static_cast<aw*>(p);
                        a->cont_.resume();
                    }
                });
                inner_->start();
            }

            std::size_t await_resume() noexcept { return result_; }
        };
        return aw{*this};
    }
};

#endif
