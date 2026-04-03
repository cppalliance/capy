//
// Adapted from stdexec (Apache-2.0 WITH LLVM-exception)
// for benchmark use.
//

#ifndef BOOST_CAPY_BENCH_REPEAT_UNTIL_HPP
#define BOOST_CAPY_BENCH_REPEAT_UNTIL_HPP

#include <beman/execution/execution.hpp>

#include <optional>
#include <system_error>
#include <type_traits>
#include <utility>

namespace bex = beman::execution;

template <bex::sender Sndr, bex::receiver Rcvr>
struct repeat_connector
{
    decltype(bex::connect(
        std::declval<Sndr>(),
        std::declval<Rcvr>())) op;

    repeat_connector(auto sndr, auto rcvr)
        : op(bex::connect(std::move(sndr), std::move(rcvr)))
    {}

    auto start() & noexcept -> void { bex::start(op); }
};

/// Sender algorithm that repeats a child sender until
/// a predicate returns true. Predicate is called with
/// no arguments (child values are discarded).
///
/// Includes a trampoline that bounds recursion depth
/// for synchronous completions (max_depth = 19).
inline constexpr struct repeat_until_t
{
    template <bex::sender Child, typename Pred>
    struct sender
    {
        using sender_concept = bex::sender_t;
        using completion_signatures = bex::completion_signatures<
            bex::set_value_t(),
            bex::set_error_t(std::error_code),
            bex::set_error_t(std::exception_ptr),
            bex::set_stopped_t()>;

        template <bex::receiver Receiver>
        struct state
        {
            using operation_state_concept =
                bex::operation_state_t;

            static constexpr std::size_t max_depth = 19;

            struct own_receiver
            {
                using receiver_concept = bex::receiver_t;
                state* s;

                auto get_env() const noexcept
                {
                    return bex::get_env(s->receiver);
                }

                void set_value() && noexcept
                {
                    s->next();
                }

                template <class... Args>
                void set_value(Args&&...) && noexcept
                {
                    s->next();
                }

                void set_error(
                    std::exception_ptr e) && noexcept
                {
                    bex::set_error(
                        std::move(s->receiver),
                        std::move(e));
                }

                void set_error(
                    std::error_code e) && noexcept
                {
                    bex::set_error(
                        std::move(s->receiver),
                        std::move(e));
                }

                void set_stopped() && noexcept
                {
                    bex::set_stopped(
                        std::move(s->receiver));
                }
            };

            std::remove_cvref_t<Child> child;
            std::remove_cvref_t<Pred> pred;
            std::remove_cvref_t<Receiver> receiver;
            std::optional<repeat_connector<
                std::remove_cvref_t<Child>,
                own_receiver>> child_op;
            std::size_t depth_ = 0;
            bool draining_ = false;
            bool again_ = false;

            auto start() & noexcept -> void
            {
                drain();
            }

            // Iterative trampoline that bounds stack
            // depth for synchronous completions
            auto drain() & noexcept -> void
            {
                draining_ = true;
                do
                {
                    again_ = false;
                    depth_ = 0;
                    child_op.emplace(
                        child, own_receiver{this});
                    child_op->start();
                }
                while (again_);
                draining_ = false;
            }

            auto next() & noexcept -> void
            {
                if (pred())
                {
                    bex::set_value(std::move(receiver));
                    return;
                }

                if (!draining_)
                {
                    // Async completion — enter drain loop
                    drain();
                    return;
                }

                if (++depth_ >= max_depth)
                {
                    // Hit depth limit — trampoline
                    again_ = true;
                    return;
                }

                // Within limit — recurse inline
                child_op.emplace(
                    child, own_receiver{this});
                child_op->start();
            }
        };

        std::remove_cvref_t<Child> child;
        std::remove_cvref_t<Pred> pred;

        template <bex::receiver Receiver>
        auto connect(Receiver&& rcvr) const&
            -> state<Receiver>
        {
            return {child, pred,
                std::forward<Receiver>(rcvr)};
        }
    };

    template <bex::sender Child, typename Pred>
    auto operator()(Child&& child, Pred&& pred) const
        -> sender<Child, Pred>
    {
        return {std::forward<Child>(child),
            std::forward<Pred>(pred)};
    }
} repeat_until{};

#endif
