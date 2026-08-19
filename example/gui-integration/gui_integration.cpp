//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

//
// GUI Integration Example
//
// Runs a Capy coroutine on a GUI framework's event loop.  The
// framework here is a stand-in: it owns the thread it is created on,
// pumps an event loop on it, and accepts work from any thread.  That
// is the only primitive a real toolkit has to provide.
//
// The interesting part is thread affinity.  The coroutine awaits work
// on a thread pool, then a dialog the toolkit answers on its own
// thread, and updates a widget after each.  Every widget access checks
// which thread it is on, so the program proves where the coroutine
// resumes instead of assuming it.
//

// tag::full[]
#include <boost/capy.hpp>
#include <boost/capy/ex/frame_allocator.hpp>

#include <condition_variable>
#include <coroutine>
#include <cstdlib>
#include <deque>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace capy = boost::capy;

// tag::check[]
// The thread checks are the point of this example, so they have to
// survive a release build.  assert() would compile out under NDEBUG.
void check(bool ok, char const* what)
{
    if(ok)
        return;
    std::cerr << "FAILED: " << what << std::endl;
    // abort() rather than exit(): a failing check can fire on a pool
    // thread, and exit() would run static destructors alongside the
    // still-running threads.
    std::abort();
}
// end::check[]

//----------------------------------------------------------
// The stand-in toolkit
//----------------------------------------------------------

// tag::gui_app[]
// Stands in for QApplication, GtkApplication, or wxApp.  It owns the
// thread it is constructed on, runs the event loop on that thread, and
// takes work from any thread.  Every real toolkit offers that last
// operation: QMetaObject::invokeMethod, g_idle_add,
// wxEvtHandler::CallAfter, PostMessage.
class gui_app : public capy::execution_context
{
    std::thread::id const gui_thread_ = std::this_thread::get_id();
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<std::coroutine_handle<>> queue_;
    bool quit_ = false;

public:
    class executor_type;

    gui_app()
        : execution_context(this)
    {
    }

    ~gui_app()
    {
        shutdown();
        destroy();
    }

    gui_app(gui_app const&) = delete;
    gui_app& operator=(gui_app const&) = delete;

    // Run a coroutine on the GUI thread.  Callable from any thread and
    // never blocks the caller.
    void post_to_gui_thread(std::coroutine_handle<> h)
    {
        {
            std::lock_guard<std::mutex> lock(m_);
            queue_.push_back(h);
        }
        cv_.notify_one();
    }

    // Ask the loop to return once it has drained its queue.
    void quit()
    {
        {
            std::lock_guard<std::mutex> lock(m_);
            quit_ = true;
        }
        cv_.notify_one();
    }

    // tag::run[]
    // The event loop.  It blocks while the queue is empty, so an
    // operation still running on another thread cannot end the loop.
    void run()
    {
        check(on_gui_thread(), "run() called off the GUI thread");
        for(;;)
        {
            std::coroutine_handle<> h;
            {
                std::unique_lock<std::mutex> lock(m_);
                cv_.wait(lock,
                    [this]{ return !queue_.empty() || quit_; });
                if(queue_.empty())
                    return;  // quit requested, nothing left to run
                h = queue_.front();
                queue_.pop_front();
            }
            capy::safe_resume(h);
        }
    }
    // end::run[]

    bool on_gui_thread() const noexcept
    {
        return std::this_thread::get_id() == gui_thread_;
    }

    executor_type get_executor() noexcept;
};
// end::gui_app[]

// tag::executor[]
// Wraps post_to_gui_thread as an Executor.  This is the whole binding
// between Capy and the toolkit.
class gui_app::executor_type
{
    friend class gui_app;
    gui_app* app_ = nullptr;

    explicit executor_type(gui_app& app) noexcept
        : app_(&app)
    {
    }

public:
    executor_type() = default;

    capy::execution_context& context() const noexcept
    {
        return *app_;
    }

    void on_work_started() const noexcept {}
    void on_work_finished() const noexcept {}

    std::coroutine_handle<> dispatch(capy::continuation& c) const
    {
        if(app_->on_gui_thread())
            return c.h;  // resume inline by symmetric transfer
        app_->post_to_gui_thread(c.h);
        return std::noop_coroutine();
    }

    void post(capy::continuation& c) const
    {
        app_->post_to_gui_thread(c.h);
    }

    bool operator==(executor_type const& other) const noexcept
    {
        return app_ == other.app_;
    }
};
// end::executor[]

inline
gui_app::executor_type
gui_app::get_executor() noexcept
{
    return executor_type{*this};
}

// tag::concept_check[]
static_assert(capy::Executor<gui_app::executor_type>);
// end::concept_check[]

// tag::label[]
// Stands in for QLabel, GtkLabel, or wxStaticText.  Real widgets are
// not thread-safe, and touching one off the GUI thread is undefined
// behavior that a real toolkit usually fails to diagnose.  This one
// diagnoses it.
class label
{
    gui_app& app_;
    std::string text_;

public:
    explicit label(gui_app& app) noexcept
        : app_(app)
    {
    }

    void set_text(std::string text)
    {
        check(app_.on_gui_thread(), "set_text off the GUI thread");
        text_ = std::move(text);
        std::cout << "[gui] label: " << text_ << "\n";
    }

    std::string const& text() const
    {
        check(app_.on_gui_thread(), "text() off the GUI thread");
        return text_;
    }
};
// end::label[]

//----------------------------------------------------------
// Work that leaves the GUI thread
//----------------------------------------------------------

// tag::pool_task[]
// A task meant to run somewhere other than the GUI thread.  It fails
// the program if it finds itself on the GUI thread, which would mean
// the work never left it.
capy::task<std::string>
count_rows(gui_app& app)
{
    check(!app.on_gui_thread(), "count_rows ran on the GUI thread");
    co_return "42";
}
// end::pool_task[]

//----------------------------------------------------------
// A completion from the toolkit's own thread
//----------------------------------------------------------

// tag::dialog[]
// Stands in for QMessageBox, GtkDialog, or wxMessageDialog.  A toolkit
// reports the user's answer on whichever thread it chooses, so a plain
// thread is the honest stand-in.  It is not the GUI thread, and it is
// not a thread Capy scheduled.
class dialog
{
    gui_app& app_;
    std::thread thread_;

public:
    explicit dialog(gui_app& app) noexcept
        : app_(app)
    {
    }

    // Joins the toolkit's thread, so no answer outlives main.
    ~dialog()
    {
        if(thread_.joinable())
            thread_.join();
    }

    // Show the dialog.  Returns at once; the answer arrives later, on
    // the toolkit's thread.
    void show(std::function<void(std::string)> on_closed)
    {
        // The stand-in delivers one answer at a time.  A previous
        // thread has already posted its answer by the time the
        // coroutine can ask again, so this join does not block.
        if(thread_.joinable())
            thread_.join();
        thread_ = std::thread(
            [this, cb = std::move(on_closed)]
            {
                check(!app_.on_gui_thread(),
                    "dialog answered on the GUI thread");
                cb("OK");  // the user chose OK
            });
    }
};
// end::dialog[]

// tag::dialog_awaitable[]
// An IoAwaitable for an operation the toolkit completes.  The protocol
// is the subject of the IoAwaitable page; one line of it matters here.
struct show_dialog
{
    dialog& dialog_;
    capy::io_env const* env_ = nullptr;
    capy::continuation cont_ = {};
    std::string answer_ = {};

    bool await_ready() const noexcept
    {
        return false;
    }

    std::coroutine_handle<> await_suspend(
        std::coroutine_handle<> h, capy::io_env const* env)
    {
        env_ = env;
        cont_.h = h;
        dialog_.show([this](std::string answer)
        {
            answer_ = std::move(answer);
            // The toolkit's thread must not resume the coroutine
            // itself.  Handing the continuation to the executor is what
            // puts the resumption back on the GUI thread.  This is also
            // the last read of *this: the executor may resume the
            // coroutine -- and then destroy this awaitable -- before
            // this call returns.
            env_->executor.post(cont_);
        });
        return std::noop_coroutine();
    }

    std::string await_resume()
    {
        return std::move(answer_);
    }
};
// end::dialog_awaitable[]

// tag::coroutine[]
capy::task<>
refresh(gui_app& app, label& status,
    capy::thread_pool& pool, dialog& confirm)
{
    // Started on the GUI executor, so the body runs on the GUI thread
    // and touching the widget is safe.
    status.set_text("Loading...");

    // run() starts count_rows on the pool and posts this coroutine
    // back through the executor it was started with.
    auto rows = co_await capy::run(pool.get_executor())(count_rows(app));

    // Back on the GUI thread, without a hop written by hand.
    status.set_text("Loaded " + rows + " rows");

    // The toolkit answers on its own thread.  show_dialog posts the
    // continuation through this coroutine's executor, so the resumption
    // lands on the GUI thread again.
    auto answer = co_await show_dialog{confirm};

    status.set_text("Confirmed: " + answer);
}
// end::coroutine[]

// tag::main[]
int main()
{
    // The GUI thread is whichever thread constructs the app.
    gui_app app;
    label status(app);
    capy::thread_pool pool(1);
    dialog confirm(app);

    capy::run_async(app.get_executor(), [&app]
    {
        // The task completed on the GUI thread, so this handler runs
        // there too.
        check(app.on_gui_thread(), "handler off the GUI thread");
        app.quit();
    })(refresh(app, status, pool, confirm));

    app.run();
    pool.join();

    // The updates are sequential, so the last one proves all of them.
    check(status.text() == "Confirmed: OK", "wrong final text");
    std::cout << "[gui] event loop finished\n";
    return 0;
}
// end::main[]
// end::full[]
