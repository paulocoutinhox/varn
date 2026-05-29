# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/Users/paulo/.cache/CPM/openssl-source/f9ec")
  file(MAKE_DIRECTORY "/Users/paulo/.cache/CPM/openssl-source/f9ec")
endif()
file(MAKE_DIRECTORY
  "/Users/paulo/Developer/workspaces/cpp/varn/build-ziptest/_deps/openssl-source-build"
  "/Users/paulo/Developer/workspaces/cpp/varn/build-ziptest/_deps/openssl-subbuild/openssl-populate-prefix"
  "/Users/paulo/Developer/workspaces/cpp/varn/build-ziptest/_deps/openssl-subbuild/openssl-populate-prefix/tmp"
  "/Users/paulo/Developer/workspaces/cpp/varn/build-ziptest/_deps/openssl-subbuild/openssl-populate-prefix/src/openssl-populate-stamp"
  "/Users/paulo/Developer/workspaces/cpp/varn/build-ziptest/_deps/openssl-subbuild/openssl-populate-prefix/src"
  "/Users/paulo/Developer/workspaces/cpp/varn/build-ziptest/_deps/openssl-subbuild/openssl-populate-prefix/src/openssl-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/Users/paulo/Developer/workspaces/cpp/varn/build-ziptest/_deps/openssl-subbuild/openssl-populate-prefix/src/openssl-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/Users/paulo/Developer/workspaces/cpp/varn/build-ziptest/_deps/openssl-subbuild/openssl-populate-prefix/src/openssl-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
