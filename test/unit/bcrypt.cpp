//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include <boost/capy/bcrypt.hpp>

#include "test_helpers.hpp"

namespace boost {
namespace capy {

struct bcrypt_test
{
    void
    test_error_code()
    {
        // Test error codes can be created
        system::error_code ec1 = bcrypt::make_error_code(bcrypt::error::ok);
        BOOST_TEST(! ec1.failed());

        system::error_code ec2 = bcrypt::make_error_code(bcrypt::error::invalid_salt);
        BOOST_TEST(ec2.failed());
        BOOST_TEST(ec2.message() == "invalid salt");

        system::error_code ec3 = bcrypt::make_error_code(bcrypt::error::invalid_hash);
        BOOST_TEST(ec3.failed());
        BOOST_TEST(ec3.message() == "invalid hash");
    }

    void
    test_result()
    {
        // Default construction
        bcrypt::result r;
        BOOST_TEST(r.empty());
        BOOST_TEST(r.size() == 0);
        BOOST_TEST(! r);

        // After hashing
        r = bcrypt::hash("password", 4);
        BOOST_TEST(! r.empty());
        BOOST_TEST(r.size() == 60);
        BOOST_TEST(static_cast<bool>(r));
        BOOST_TEST(r.c_str()[60] == '\0');
    }

    void
    test_gen_salt()
    {
        // Default rounds (10)
        bcrypt::result r = bcrypt::gen_salt();
        BOOST_TEST(r.size() == 29);
        BOOST_TEST(r.str().substr(0, 4) == "$2b$");
        BOOST_TEST(r.str().substr(4, 2) == "10");

        // Custom rounds
        r = bcrypt::gen_salt(12);
        BOOST_TEST(r.str().substr(4, 2) == "12");

        // Version 2a
        r = bcrypt::gen_salt(10, bcrypt::version::v2a);
        BOOST_TEST(r.str().substr(0, 4) == "$2a$");

        // Different salts each time
        bcrypt::result r1 = bcrypt::gen_salt(4);
        bcrypt::result r2 = bcrypt::gen_salt(4);
        BOOST_TEST(r1.str() != r2.str());
    }

    void
    test_hash_with_rounds()
    {
        // Basic hash
        bcrypt::result r = bcrypt::hash("password", 4);
        BOOST_TEST(r.size() == 60);
        BOOST_TEST(r.str().substr(0, 7) == "$2b$04$");

        // Different passwords produce different hashes
        bcrypt::result r1 = bcrypt::hash("password1", 4);
        bcrypt::result r2 = bcrypt::hash("password2", 4);
        BOOST_TEST(r1.str() != r2.str());

        // Same password with different salts produces different hashes
        r1 = bcrypt::hash("password", 4);
        r2 = bcrypt::hash("password", 4);
        BOOST_TEST(r1.str() != r2.str());
    }

    void
    test_hash_with_salt()
    {
        bcrypt::result salt = bcrypt::gen_salt(4);

        // Generate salt, then hash
        {
            system::error_code ec;
            bcrypt::result h = bcrypt::hash("password", salt.str(), ec);
            BOOST_TEST(! ec.failed());
            BOOST_TEST(h.size() == 60);
        }

        // Same password + salt = same hash
        {
            system::error_code ec1;
            bcrypt::result hash1 = bcrypt::hash("password", salt.str(), ec1);
            BOOST_TEST(! ec1.failed());

            system::error_code ec2;
            bcrypt::result hash2 = bcrypt::hash("password", salt.str(), ec2);
            BOOST_TEST(! ec2.failed());

            BOOST_TEST(hash1.str() == hash2.str());
        }

        // Invalid salt
        {
            system::error_code ec;
            bcrypt::result h = bcrypt::hash("password", "invalid", ec);
            BOOST_TEST(ec == bcrypt::error::invalid_salt);
            BOOST_TEST(h.empty());
        }

        // Malformed salt
        {
            system::error_code ec;
            bcrypt::result h = bcrypt::hash("password", "$2b$04$", ec);
            BOOST_TEST(ec == bcrypt::error::invalid_salt);
            BOOST_TEST(h.empty());
        }
    }

    void
    test_compare()
    {
        bcrypt::result r = bcrypt::hash("correct_password", 4);

        // Correct password
        {
            system::error_code ec;
            bool match = bcrypt::compare("correct_password", r.str(), ec);
            BOOST_TEST(! ec.failed());
            BOOST_TEST(match);
        }

        // Wrong password
        {
            system::error_code ec;
            bool match = bcrypt::compare("wrong_password", r.str(), ec);
            BOOST_TEST(! ec.failed());
            BOOST_TEST(! match);
        }

        // Empty password (should not match)
        {
            system::error_code ec;
            bool match = bcrypt::compare("", r.str(), ec);
            BOOST_TEST(! ec.failed());
            BOOST_TEST(! match);
        }

        // Invalid hash
        {
            system::error_code ec;
            bool match = bcrypt::compare("password", "invalid", ec);
            BOOST_TEST(ec == bcrypt::error::invalid_hash);
            BOOST_TEST(! match);
        }

        // Malformed hash (wrong length)
        {
            system::error_code ec;
            bool match = bcrypt::compare("password", "$2b$04$abcdefghij", ec);
            BOOST_TEST(ec == bcrypt::error::invalid_hash);
            BOOST_TEST(! match);
        }
    }

    void
    test_get_rounds()
    {
        // Valid hash
        {
            system::error_code ec;
            unsigned rounds = bcrypt::get_rounds(
                "$2b$12$abcdefghijklmnopqrstuuxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", ec);
            BOOST_TEST(! ec.failed());
            BOOST_TEST(rounds == 12);
        }

        // Different versions
        {
            system::error_code ec;
            unsigned rounds = bcrypt::get_rounds(
                "$2a$10$abcdefghijklmnopqrstuuxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", ec);
            BOOST_TEST(! ec.failed());
            BOOST_TEST(rounds == 10);
        }

        // Invalid format
        {
            system::error_code ec;
            unsigned rounds = bcrypt::get_rounds("invalid", ec);
            BOOST_TEST(ec == bcrypt::error::invalid_hash);
            BOOST_TEST(rounds == 0);
        }

        // Missing prefix
        {
            system::error_code ec;
            unsigned rounds = bcrypt::get_rounds("2b$10$abc", ec);
            BOOST_TEST(ec == bcrypt::error::invalid_hash);
            BOOST_TEST(rounds == 0);
        }
    }

    void
    test_known_vectors()
    {
        // Test vectors verified against reference implementations

        // U*U with all-C salt
        {
            system::error_code ec;
            BOOST_TEST(bcrypt::compare("U*U",
                "$2a$05$CCCCCCCCCCCCCCCCCCCCC.E5YPO9kmyuRGyh0XouQYb4YMJKvyOeW", ec));
            BOOST_TEST(! ec.failed());
        }

        // Empty password
        {
            system::error_code ec;
            BOOST_TEST(bcrypt::compare("",
                "$2a$06$DCq7YPn5Rq63x1Lad4cll.TV4S6ytwfsfvkgY8jIucDrjc8deX1s.", ec));
            BOOST_TEST(! ec.failed());
        }

        // Test that wrong password fails
        {
            system::error_code ec;
            BOOST_TEST(! bcrypt::compare("wrong",
                "$2a$05$CCCCCCCCCCCCCCCCCCCCC.E5YPO9kmyuRGyh0XouQYb4YMJKvyOeW", ec));
            BOOST_TEST(! ec.failed());
        }
    }

    void
    test_password_truncation()
    {
        // bcrypt truncates passwords to 72 bytes
        std::string long_pw(100, 'a');
        std::string truncated_pw(72, 'a');

        bcrypt::result salt = bcrypt::gen_salt(4);

        system::error_code ec1;
        bcrypt::result r1 = bcrypt::hash(long_pw, salt.str(), ec1);
        BOOST_TEST(! ec1.failed());

        system::error_code ec2;
        bcrypt::result r2 = bcrypt::hash(truncated_pw, salt.str(), ec2);
        BOOST_TEST(! ec2.failed());

        // Both should produce the same hash
        BOOST_TEST(r1.str() == r2.str());
    }

    void
    run()
    {
        test_error_code();
        test_result();
        test_gen_salt();
        test_hash_with_rounds();
        test_hash_with_salt();
        test_compare();
        test_get_rounds();
        test_known_vectors();
        test_password_truncation();
    }
};

TEST_SUITE(bcrypt_test, "boost.capy.bcrypt");

} // capy
} // boost

