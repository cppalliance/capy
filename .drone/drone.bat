
@ECHO ON
setlocal enabledelayedexpansion

set TRAVIS_OS_NAME=windows

IF "!DRONE_BRANCH!" == "" (
  for /F %%i in ("!GITHUB_REF!") do @set TRAVIS_BRANCH=%%~nxi
) else (
  SET TRAVIS_BRANCH=!DRONE_BRANCH!
)

if "%DRONE_JOB_BUILDTYPE%" == "boost" (

echo "Running boost job"
echo '==================================> INSTALL'
REM there seems to be some problem with b2 bootstrap on Windows
REM when CXX env variable is set
SET "CXX="

git clone https://github.com/boostorg/boost-ci.git boost-ci-cloned --depth 1
cp -prf boost-ci-cloned/ci .
rm -rf boost-ci-cloned
REM source ci/travis/install.sh
REM The contents of install.sh below:

for /F %%i in ("%DRONE_REPO%") do @set SELF=%%~nxi
SET BOOST_CI_TARGET_BRANCH=!TRAVIS_BRANCH!
SET BOOST_CI_SRC_FOLDER=%cd%
if "%BOOST_BRANCH%" == "" (
    SET BOOST_BRANCH=develop
    if "%BOOST_CI_TARGET_BRANCH%" == "master" set BOOST_BRANCH=master
)

call ci\common_install.bat

echo '==================================> ZLIB'
git clone --branch v1.2.13 https://github.com/madler/zlib.git !BOOST_ROOT!\zlib-src --depth 1
set ZLIB_SOURCE=!BOOST_ROOT!\zlib-src

echo '==================================> COMPILE'

REM set B2_TARGETS=libs/!SELF!/test libs/!SELF!/example
set B2_TARGETS=libs/!SELF!/test
call !BOOST_ROOT!\libs\!SELF!\ci\build.bat

)
