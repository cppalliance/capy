//
// Adapted from beman execution examples (Apache-2.0 WITH LLVM-exception)
// for benchmark use.
//

#ifndef BOOST_CAPY_BENCH_REPEAT_EFFECT_UNTIL_HPP
#define BOOST_CAPY_BENCH_REPEAT_EFFECT_UNTIL_HPP

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

inline constexpr struct repeat_effect_until_t
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

            auto start() & noexcept -> void
            {
                run_one();
            }

            auto run_one() & noexcept -> void
            {
                child_op.emplace(child, own_receiver{this});
                child_op->start();
            }

            auto next() & noexcept -> void
            {
                if (pred())
                    bex::set_value(std::move(receiver));
                else
                    run_one();
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
} repeat_effect_until{};

#endif
