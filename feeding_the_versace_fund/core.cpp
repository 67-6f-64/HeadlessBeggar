#include "core.hpp"
#include <windows.h>
#include <stdarg.h>

static char output_debug_buf[1024];

cstr output_debug_printf(ccstr fmt, ...) {
  va_list args;
  va_start(args, fmt);

  vsnprintf(output_debug_buf, _countof(output_debug_buf), fmt, args);
  OutputDebugStringA(output_debug_buf);
  printf("%s", output_debug_buf);

  va_end(args);
  return output_debug_buf;
}
