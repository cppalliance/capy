//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_EXAMPLE_AWAITABLE_SENDER_DETAIL_HPP
#define BOOST_CAPY_EXAMPLE_AWAITABLE_SENDER_DETAIL_HPP

#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/capy/detail/await_suspend_helper.hpp>
#include <boost/capy/ex/execution_context.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/io_result.hpp>

#include <stdexec/execution.hpp>

#include <concepts>
#include <coroutine>
#include <exception>
#include <optional>
#include <stop_token>
#include <tuple>
#include <type_traits>
#include <utility>

namespace boost::capy {

// On graduation this alias flips to std::execution, gated on
// __cpp_lib_senders.
namespace ex = stdexec;

// -------------------------------------------------------
// CPO: query a receiver environment for a Capy executor
// -------------------------------------------------------

struct get_io_executor_t
    : ex::forwarding_query_t
{
    // Derive from forwarding_query_t so env-wrapping adaptors
    // like then forward this query: std::execution's adaptor
    // environments only re-expose queries whose CPO is itself a
    // forwarding_query, filtering out the rest by default.
    template<class Env>
    auto operator()(Env const& env) const noexcept
        -> decltype(env.query(std::declval<get_io_executor_t const&>()))
    {
        return env.query(*this);
    }
};

inline constexpr get_io_executor_t get_io_executor{};

// -------------------------------------------------------
// Environment that carries a Capy executor + stop token
// -------------------------------------------------------

struct io_sender_env
{
    executor_ref io_executor;
    std::stop_token stop_token;

    auto query(
        get_io_executor_t const&) const noexcept
        -> executor_ref
    {
        return io_executor;
    }

    auto query(
        ex::get_stop_token_t const&) const noexcept
        -> std::stop_token
    {
        return stop_token;
    }
};

namespace detail {

// Channel splitting keys on capy's io_result, the type that
// declares "element 0 is an error_code" as intent. A generic
// tuple-like that merely happens to lead with an error_code is a
// value, not an outcome to split — shape alone cannot tell a
// result protocol from a payload. An io_result with payload
// elements is rejected in make_sigs: exclusive completion
// channels cannot carry a partial success without dropping data.
// The arity trait avoids naming std::tuple_size on foreign
// types, which would hard-error for non-tuples (&& does not
// short-circuit instantiation).
template<class T>
struct io_result_arity
    : std::integral_constant<std::size_t, 0> {};

template<class... Ts>
struct io_result_arity<io_result<Ts...>>
    : std::integral_constant<std::size_t, 1 + sizeof...(Ts)> {};

template<class T>
constexpr bool is_ec_outcome_v =
    std::is_same_v<T, std::error_code> ||
    io_result_arity<T>::value == 1;

template<class T>
constexpr bool is_compound_ec_result_v =
    io_result_arity<T>::value >= 2;

// -------------------------------------------------------
// frame_cb: synthetic coroutine frame for callback handles
//
// The first two members match the coroutine frame layout
// used by MSVC, GCC, and Clang. from_address produces a
// coroutine_handle whose .resume() calls our function
// pointer and whose .destroy() is a no-op.
// -------------------------------------------------------

struct frame_cb
{
    void (*resume)(frame_cb*);
    void (*destroy)(frame_cb*);
    void* data;
};

// declared only for decltype composition
template<class... A, class... B>
auto concat_sigs(
    ex::completion_signatures<A...>,
    ex::completion_signatures<B...>)
    -> ex::completion_signatures<A..., B...>;

// Deduce completion signatures from an awaitable's result type.
template<class Aw>
auto make_sigs()
{
    using A = std::decay_t<Aw>;
    using R = awaitable_result_t<A>;

    static_assert(
        !is_compound_ec_result_v<R>,
        "IoAwaitables whose result is an io_result with payload "
        "elements cannot be senders: completion channels are "
        "exclusive, so a partial success (error_code plus "
        "payload) would be silently dropped. Wrap the operation "
        "in a task<error_code> that inspects the full result "
        "and returns the error code.");

    constexpr bool nothrow_resume =
        noexcept(std::declval<A&>().await_resume());

    auto base = []{
        if constexpr (std::is_void_v<R>)
            return ex::completion_signatures<
                ex::set_value_t()>{};
        else if constexpr (is_ec_outcome_v<R>)
            return ex::completion_signatures<
                ex::set_value_t(),
                ex::set_error_t(std::error_code)>{};
        else
            return ex::completion_signatures<
                ex::set_value_t(R)>{};
    }();

    auto tail = []{
        if constexpr (nothrow_resume)
            return ex::completion_signatures<
                ex::set_stopped_t()>{};
        else
            return ex::completion_signatures<
                ex::set_error_t(std::exception_ptr),
                ex::set_stopped_t()>{};
    }();

    return decltype(concat_sigs(base, tail)){};
}

// stdexec's get_stop_token CPO requires the C++26 stoppable_token
// concept, which today's std::stop_token fails (no callback_type);
// query the environment directly so std::stop_token environments
// keep cancellation until stdlibs catch up.
template<class Env, class = void>
struct env_stop_token
{
    using type = ex::never_stop_token;
};

template<class Env>
struct env_stop_token<Env, std::void_t<
    decltype(std::declval<Env const&>().query(
        ex::get_stop_token_t{}))>>
{
    // remove_cvref_t so an env returning `std::stop_token const&`
    // still matches token_passthrough below.
    using type = std::remove_cvref_t<decltype(
        std::declval<Env const&>().query(
            ex::get_stop_token_t{}))>;
};

template<class T>
struct is_polymorphic_allocator : std::false_type {};

template<class T>
struct is_polymorphic_allocator<
    std::pmr::polymorphic_allocator<T>> : std::true_type {};

// Bridges a std::execution scheduler to capy's Executor concept so
// ops can run under receivers that provide only get_scheduler
// (e.g. sync_wait's run_loop). Each post allocates a small holder
// for the schedule operation; receivers supplying get_io_executor
// never pay this.
template<class Sched>
class scheduler_executor
{
    Sched sched_;

    struct holder;

    struct resume_receiver
    {
        using receiver_concept = ex::receiver_t;

        holder* self_;
        std::coroutine_handle<> h_;

        void set_value() && noexcept
        {
            auto h = h_;
            delete self_;
            h.resume();
        }

        // The scheduler refused the work (its context has
        // finished): abandon without resuming, like a post to a
        // stopped execution context.
        template<class E>
        void set_error(E&&) && noexcept
        {
            delete self_;
        }

        void set_stopped() && noexcept
        {
            delete self_;
        }
    };

    struct holder
    {
        // schedule() wants a non-const scheduler; schedulers are
        // cheap copyable handles, so take one by value.
        using op_t = decltype(ex::connect(
            ex::schedule(std::declval<Sched>()),
            std::declval<resume_receiver>()));

        op_t op_;

        holder(Sched s, std::coroutine_handle<> h)
            : op_(ex::connect(
                ex::schedule(std::move(s)),
                resume_receiver{this, h}))
        {
        }
    };

public:
    explicit scheduler_executor(Sched sched)
        : sched_(std::move(sched))
    {
    }

    // Foreign schedulers have no capy execution_context; ops in
    // the sender path must not rely on context services.
    execution_context& context() const noexcept
    {
        static execution_context ctx;
        return ctx;
    }

    void on_work_started() const noexcept {}
    void on_work_finished() const noexcept {}

    bool operator==(
        scheduler_executor const& other) const noexcept
    {
        return sched_ == other.sched_;
    }

    void post(continuation& c) const
    {
        // the holder frees itself from the receiver on completion
        auto* p = new holder(sched_, c.h);
        ex::start(p->op_);
    }

    // A generic scheduler cannot answer "is the current thread
    // yours", so dispatch degrades to post.
    std::coroutine_handle<> dispatch(continuation& c) const
    {
        post(c);
        return std::noop_coroutine();
    }
};

template<class Aw, class Receiver>
struct awaitable_op_state
{
    using operation_state_concept = ex::operation_state_t;
    using result_type = awaitable_result_t<Aw>;

    using env_type =
        decltype(ex::get_env(std::declval<Receiver const&>()));
    using stop_token_type =
        typename env_stop_token<env_type>::type;

    // io_env only carries std::stop_token, so any other
    // stoppable token needs an owned std::stop_source relayed
    // through a stop_callback; unstoppable tokens need neither.
    // The stoppable_token conjunct keeps stop_callback_for_t
    // instantiable in the bridged branch.
    static constexpr bool token_passthrough =
        std::is_same_v<stop_token_type, std::stop_token>;
    static constexpr bool token_bridged =
        !token_passthrough &&
        ex::stoppable_token<stop_token_type> &&
        !ex::unstoppable_token<stop_token_type>;

    struct on_stop
    {
        std::stop_source& src;

        void operator()() const noexcept
        {
            src.request_stop();
        }
    };

    // stop_callback_for_t needs the token's callback_type alias,
    // which std::stop_token lacks today; select the storage type
    // lazily so only bridged tokens ever instantiate it.
    struct no_stop_callback {};
    static auto stop_cb_storage()
    {
        if constexpr (token_bridged)
            return std::type_identity<
                ex::stop_callback_for_t<stop_token_type, on_stop>>{};
        else
            return std::type_identity<no_stop_callback>{};
    }

    static constexpr bool has_io_executor =
        requires(env_type const& e) { get_io_executor(e); };
    static constexpr bool has_scheduler =
        requires(env_type const& e) { ex::get_scheduler(e); };

    // Lazy type selection, as with stop_cb_storage: the scheduler
    // bridge type only exists for envs that need it.
    struct no_scheduler_bridge {};
    static auto sched_ex_storage()
    {
        if constexpr (!has_io_executor && has_scheduler)
            return std::type_identity<scheduler_executor<
                std::remove_cvref_t<decltype(ex::get_scheduler(
                    std::declval<env_type const&>()))>>>{};
        else
            return std::type_identity<no_scheduler_bridge>{};
    }

    Aw aw_;
    Receiver rcvr_;
    io_env env_;
    frame_cb cb_;
    // stop_cb_ declared after stop_src_ so it is destroyed first:
    // on_stop holds a reference to stop_src_.
    std::stop_source stop_src_;
    std::optional<
        typename decltype(stop_cb_storage())::type>
            stop_cb_;
    // env_.executor may reference this member; op_states are
    // immovable, so the address is stable.
    std::optional<
        typename decltype(sched_ex_storage())::type>
            sched_ex_;

    // std::stop_source allocates shared state; only pay for it
    // when the receiver's token actually needs bridging.
    static std::stop_source make_stop_source()
    {
        if constexpr (token_bridged)
            return std::stop_source();
        else
            return std::stop_source(std::nostopstate);
    }

    awaitable_op_state(Aw aw, Receiver rcvr)
        : aw_(std::move(aw))
        , rcvr_(std::move(rcvr))
        , cb_{}
        , stop_src_(make_stop_source())
    {
    }

    awaitable_op_state(awaitable_op_state const&) = delete;
    awaitable_op_state(awaitable_op_state&&) = delete;
    awaitable_op_state& operator=(awaitable_op_state const&) = delete;
    awaitable_op_state& operator=(awaitable_op_state&&) = delete;

    static void on_resume(frame_cb* p) noexcept
    {
        auto* self = static_cast<awaitable_op_state*>(p->data);
        self->complete();
    }

    static void on_destroy(frame_cb*) noexcept
    {
    }

    static constexpr bool nothrow_resume =
        noexcept(std::declval<Aw&>().await_resume());

    // A receiver connected against nothrow signatures has no
    // exception_ptr overload, so the try/catch only exists when
    // await_resume() can actually throw.
    void complete() noexcept
    {
        if constexpr (nothrow_resume)
        {
            do_complete();
        }
        else
        {
            try
            {
                do_complete();
            }
            catch(...)
            {
                ex::set_error(
                    std::move(rcvr_),
                    std::current_exception());
            }
        }
    }

    void do_complete()
    {
        if constexpr (std::is_void_v<result_type>)
        {
            // void and plain-value results carry no error_code, so
            // there is no in-band disposition; token state is the
            // only cancellation signal available for these rows.
            aw_.await_resume();
            if(env_.stop_token.stop_requested())
                ex::set_stopped(
                    std::move(rcvr_));
            else
                ex::set_value(
                    std::move(rcvr_));
        }
        else if constexpr (is_ec_outcome_v<result_type>)
        {
            // The op's own disposition picks the channel: a canceled
            // completion surfaces as stopped even if the env token
            // never fired, and a successful result survives a stop
            // request that merely landed by completion time.
            auto result = aw_.await_resume();
            std::error_code ec;
            if constexpr (std::is_same_v<
                result_type, std::error_code>)
                ec = result;
            else
                ec = get<0>(result);
            // Success first: comparing ec against a condition goes
            // through the category's virtual default_error_condition,
            // which the hot path should not pay.
            if(!ec)
                ex::set_value(
                    std::move(rcvr_));
            else if(ec == std::errc::operation_canceled)
                ex::set_stopped(
                    std::move(rcvr_));
            else
                ex::set_error(
                    std::move(rcvr_), ec);
        }
        else
        {
            auto result = aw_.await_resume();
            if(env_.stop_token.stop_requested())
                ex::set_stopped(
                    std::move(rcvr_));
            else
                ex::set_value(
                    std::move(rcvr_),
                    std::move(result));
        }
    }

    void start() noexcept
    {
        auto renv = ex::get_env(rcvr_);

        static_assert(has_io_executor || has_scheduler,
            "the receiver's environment must provide "
            "capy::get_io_executor or a scheduler via "
            "std::execution::get_scheduler");

        executor_ref io_ex = [&]() -> executor_ref
        {
            if constexpr (has_io_executor)
            {
                return get_io_executor(renv);
            }
            else
            {
                sched_ex_.emplace(ex::get_scheduler(renv));
                return executor_ref(*sched_ex_);
            }
        }();

        std::stop_token st;
        if constexpr (token_passthrough)
        {
            st = renv.query(
                ex::get_stop_token_t{});
        }
        else if constexpr (token_bridged)
        {
            stop_cb_.emplace(
                renv.query(
                    ex::get_stop_token_t{}),
                on_stop{stop_src_});
            st = stop_src_.get_token();
        }
        // Map the environment's allocator into io_env when it
        // speaks pmr; other allocator types have no
        // memory_resource to lend.
        std::pmr::memory_resource* fa = nullptr;
        if constexpr (requires { ex::get_allocator(renv); })
        {
            auto alloc = ex::get_allocator(renv);
            if constexpr (is_polymorphic_allocator<
                decltype(alloc)>::value)
                fa = alloc.resource();
        }

        env_ = io_env{io_ex, st, fa};

        if(aw_.await_ready())
        {
            complete();
            return;
        }

        cb_.resume = &on_resume;
        cb_.destroy = &on_destroy;
        cb_.data = this;

        auto h = std::coroutine_handle<>::from_address(
            static_cast<void*>(&cb_));

        // Not a real coroutine caller, so symmetric transfer
        // must be driven by hand: any non-noop handle (our own
        // frame on immediate completion, or a wrapped task's
        // handle that still needs to run) has to be resumed
        // explicitly or nothing ever completes.
        auto resumed = detail::call_await_suspend(&aw_, h, &env_);
        if(resumed != std::noop_coroutine())
            resumed.resume();
    }
};

} // namespace detail

} // namespace boost::capy

#endif
