#pragma once

#include <stdint.h>
#include "defer.hpp"

typedef int8_t       i8;
typedef int16_t      i16;
typedef int32_t      i32;
typedef int64_t      i64;
typedef intptr_t     iptr;
typedef uint8_t      u8;
typedef uint16_t     u16;
typedef uint32_t     u32;
typedef uint64_t     u64;
typedef uintptr_t    uptr;
typedef size_t       s32;
typedef char*        cstr;
typedef const char*  ccstr;
typedef wchar_t      wchar;
typedef wchar*       wstr;
typedef const wchar* cwstr;

cstr output_debug_printf(ccstr fmt, ...);

#define debug_print(fmt, ...) output_debug_printf(fmt "\n", __VA_ARGS__)
#define debug_error(fmt, ...) output_debug_printf("[err] " fmt "\n", __VA_ARGS__)
