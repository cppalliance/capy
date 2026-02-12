#!/bin/bash

# Copyright 2020 Rene Rivera, Sam Darwin
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE.txt or copy at http://boost.org/LICENSE_1_0.txt)

set -xe

export DRONE_BUILD_DIR=$(pwd)
export VCS_COMMIT_ID=$DRONE_COMMIT
export GIT_COMMIT=$DRONE_COMMIT
export REPO_NAME=$DRONE_REPO
export USER=$(whoami)
export CC=${CC:-gcc}
export PATH=~/.local/bin:/usr/local/bin:$PATH
export TRAVIS_BUILD_DIR=$(pwd)
export TRAVIS_BRANCH=$DRONE_BRANCH
export TRAVIS_EVENT_TYPE=$DRONE_BUILD_EVENT

common_install () {
  if [ -z "$SELF" ]; then
    export SELF=`basename $REPO_NAME`
  fi

  git clone https://github.com/boostorg/boost-ci.git boost-ci-cloned --depth 1
  [ "$SELF" == "boost-ci" ] || cp -prf boost-ci-cloned/ci .
  rm -rf boost-ci-cloned

  if [ "$TRAVIS_OS_NAME" == "osx" ]; then
      unset -f cd
  fi

  export BOOST_CI_TARGET_BRANCH="$TRAVIS_BRANCH"
  export BOOST_CI_SRC_FOLDER=$(pwd)

  . ./ci/common_install.sh
}

if [[ $(uname) == "Linux" && "$B2_ASAN" == "1" ]]; then
    echo 0 | sudo tee /proc/sys/kernel/randomize_va_space > /dev/null
fi

if [ "$DRONE_JOB_BUILDTYPE" == "boost" ]; then

echo '==================================> INSTALL'

common_install

echo '==================================> SCRIPT'

. $BOOST_ROOT/libs/$SELF/ci/build.sh

elif [ "$DRONE_JOB_BUILDTYPE" == "codecov" ]; then

echo '==================================> INSTALL'

common_install

echo '==================================> SCRIPT'

cd $BOOST_ROOT/libs/$SELF
ci/travis/codecov.sh

elif [ "$DRONE_JOB_BUILDTYPE" == "valgrind" ]; then

echo '==================================> INSTALL'

common_install

echo '==================================> SCRIPT'

cd $BOOST_ROOT/libs/$SELF
ci/travis/valgrind.sh

elif [ "$DRONE_JOB_BUILDTYPE" == "coverity" ]; then

echo '==================================> INSTALL'

common_install

echo '==================================> SCRIPT'

if  [ -n "${COVERITY_SCAN_NOTIFICATION_EMAIL}" -a \( "$TRAVIS_BRANCH" = "develop" -o "$TRAVIS_BRANCH" = "master" \) -a \( "$DRONE_BUILD_EVENT" = "push" -o "$DRONE_BUILD_EVENT" = "cron" \) ] ; then
cd $BOOST_ROOT/libs/$SELF
ci/travis/coverity.sh
fi

elif [ "$DRONE_JOB_BUILDTYPE" == "cmake1" ]; then

set -xe

echo '==================================> INSTALL'

# already in the image
# pip install --user cmake

echo '==================================> SCRIPT'

export SELF=`basename $REPO_NAME`
BOOST_BRANCH=develop && [ "$DRONE_BRANCH" == "master" ] && BOOST_BRANCH=master || true
echo BOOST_BRANCH: $BOOST_BRANCH
cd ..
git clone -b $BOOST_BRANCH --depth 1 https://github.com/boostorg/boost.git boost-root
cd boost-root

mkdir -p libs/$SELF
cp -r $DRONE_BUILD_DIR/* libs/$SELF
# git submodule update --init tools/boostdep
git submodule update --init --recursive

cd libs/$SELF
mkdir __build__ && cd __build__
cmake -DCMAKE_INSTALL_PREFIX=~/.local ..
cmake --build . --target install

elif [ "$DRONE_JOB_BUILDTYPE" == "cmake-superproject" ]; then

echo '==================================> INSTALL'

common_install

echo '==================================> COMPILE'

# Warnings as errors -Werror not building. Remove for now:
# export CXXFLAGS="-Wall -Wextra -Werror"
export CXXFLAGS="-Wall -Wextra"
export CMAKE_OPTIONS=${CMAKE_OPTIONS:--DBUILD_TESTING=ON}
export CMAKE_SHARED_LIBS=${CMAKE_SHARED_LIBS:-1}

mkdir __build_static
cd __build_static
cmake -DBOOST_ENABLE_CMAKE=1 -DBoost_VERBOSE=1 ${CMAKE_OPTIONS} \
    -DBOOST_INCLUDE_LIBRARIES=$SELF ..
cmake --build .
ctest --output-on-failure -R boost_$SELF

cd ..

if [ "$CMAKE_SHARED_LIBS" = 1 ]; then

mkdir __build_shared
cd __build_shared
cmake -DBOOST_ENABLE_CMAKE=1 -DBoost_VERBOSE=1 ${CMAKE_OPTIONS} \
    -DBOOST_INCLUDE_LIBRARIES=$SELF -DBUILD_SHARED_LIBS=ON ..
cmake --build .
ctest --output-on-failure -R boost_$SELF

fi

fi
