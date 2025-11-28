//
// Copyright (c) 2022 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/beast2
//

#include <boost/capy/application.hpp>
#include <boost/capy/detail/except.hpp>
#include <boost/assert.hpp>
#include <mutex>
#include <vector>

namespace boost {
namespace capy {

enum application::state : char
{
    none,
    starting,
    running,
    stopping,
    stopped
};

struct application::impl
{
    std::mutex m;
    state st = state::none;
};

application::
~application()
{
    {
        std::lock_guard<std::mutex> lock(impl_->m);
        if( impl_->st != state::stopped &&
            impl_->st != state::none)
        {
            // stop() hasn't returned yet
            detail::throw_invalid_argument();
        }
    }
    delete impl_;
}

application::
application()
    : impl_(new impl)
{
}

void
application::
start()
{
    struct action
    {
        action(application& self)
            : self_(self)
        {
            std::lock_guard<
                std::mutex> lock(self_.impl_->m);
            // can't call twice
            if(self_.impl_->st != state::none)
                detail::throw_invalid_argument();
            self_.impl_->st = state::starting;
        }

        ~action()
        {
            if(n_ == 0)
                return;
            {
                std::lock_guard<
                    std::mutex> lock(self_.impl_->m);
                BOOST_ASSERT(
                    self_.impl_->st == state::stopping);
                self_.impl_->st = state::stopping;
            }
            // stop what we started
            auto v = self_.get_elements();
            while(n_-- > 0)
                v[n_].stop();
            {
                std::lock_guard<std::mutex> lock(
                    self_.impl_->m);
                self_.impl_->st = state::stopped;
            }
        }

        void apply()
        {
            auto v = self_.get_elements();
            while(n_ < v.size())
            {
                v[n_].start();
                ++n_;
            }
            n_ = 0;
            std::lock_guard<
                std::mutex> lock(self_.impl_->m);
            self_.impl_->st = state::running;
        }

    private:
        application& self_;
        std::size_t n_ = 0;
    };

    action(*this).apply();
}

void
application::
stop()
{
    {
        std::lock_guard<std::mutex> lock(impl_->m);
        if(impl_->st != state::running)
            detail::throw_invalid_argument();
        impl_->st = state::stopping;
    }

    auto v = get_elements();
    for(std::size_t i = v.size(); i--;)
        v[i].stop();

    {
        std::lock_guard<std::mutex> lock(impl_->m);
        impl_->st = state::stopped;
    }
}

} // capy
} // boost
