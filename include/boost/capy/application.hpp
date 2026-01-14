//
// Copyright (c) 2022 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/beast2
//

#ifndef BOOST_CAPY_APPLICATION_HPP
#define BOOST_CAPY_APPLICATION_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/core/polystore.hpp>
#include <memory>
#include <type_traits>

namespace boost {
namespace capy {

/** A collection of type-erased parts and process state

    An object of this type holds a collection of type-erased parts,
    where each part undergoes two-phase initialization. First the part is
    constructed by calling @ref emplace. The @ref start function invokes
    `start()` on each part. When @ref stop is called, each part has its
    `stop()` member invoked. And when the application object is destroyed,
    all the parts are destroyed in reverse order of construction.
*/
class BOOST_SYMBOL_VISIBLE
    application : public capy::polystore
{
public:
    application(application const&) = delete;
    application& operator=(application const&) = delete;

    /** Destructor

        All stored objects will be destroyed in the reverse order of creation.
    */
    BOOST_CAPY_DECL
    ~application();

    /** Constructor
    */
    BOOST_CAPY_DECL
    application();

    /** Invoke `start` on each part in creation order
        Each call is performed synchronously; this function blocks until each
        part returns. Only one invocation of `start` is permitted.
    */
    BOOST_CAPY_DECL
    void start();

    /** Invoke `stop` on each part in reverse creation order
        This function is idempotent and returns immediately.
        @par Thread Safety
        May be called concurrently.
    */
    BOOST_CAPY_DECL
    void stop();

    /** Wait until the application is stopped
    */
    BOOST_CAPY_DECL
    void join();

private:
    enum state : char;
    struct impl;
    impl* impl_;
};

} // capy
} // boost

#endif
