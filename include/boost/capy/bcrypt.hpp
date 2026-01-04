//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

/** @file
    bcrypt password hashing library.

    This header includes all bcrypt-related functionality including
    password hashing, verification, and salt generation.

    bcrypt is a password-hashing function designed by Niels Provos
    and David Mazières based on the Blowfish cipher. It incorporates
    a salt to protect against rainbow table attacks and an adaptive
    cost parameter that can be increased as hardware improves.

    @code
    #include <boost/capy/bcrypt.hpp>

    // Hash a password
    capy::bcrypt::result r;
    capy::bcrypt::hash(r, "my_password", 12);
    
    // Store r.str() in database...
    
    // Verify later
    system::error_code ec;
    bool ok = boost::capy::bcrypt::compare("my_password", stored_hash, ec);
    if (ec)
        handle_malformed_hash();
    else if (ok)
        grant_access();
    @endcode
*/

#ifndef BOOST_CAPY_BCRYPT_HPP
#define BOOST_CAPY_BCRYPT_HPP

#include <boost/capy/bcrypt/error.hpp>
#include <boost/capy/bcrypt/hash.hpp>
#include <boost/capy/bcrypt/result.hpp>
#include <boost/capy/bcrypt/version.hpp>

#endif

