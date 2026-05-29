/* SPDX-License-Identifier: MIT */
/*
 * Author: Jianhui Zhao <zhaojh329@gmail.com>
 */

#ifndef _LUA_FFI_CONFIG_H
#define _LUA_FFI_CONFIG_H

/* Flex: avoid <unistd.h> when building the vendored lexer with MSVC (no POSIX headers). */
#if defined(_MSC_VER)
#define YY_NO_UNISTD_H 1
#endif

#define LUA_FFI_VERSION_MAJOR  1
#define LUA_FFI_VERSION_MINOR  1
#define LUA_FFI_VERSION_PATCH  0
#define LUA_FFI_VERSION_STRING "1.1.0"

#endif
