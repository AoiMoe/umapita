#include "am/pch.h"
#include "am/log.h"
#include "am/util.h"

#include <cstdarg>
#include <cxxabi.h>

namespace AM {

void Log::log(Level level, LPCTSTR fmt, ...) noexcept {
  std::va_list args;
  va_start(args, fmt);
  switch (level) {
  case Level::Debug:
    _ftprintf(stderr, TEXT("[debug]: "));
    break;
  case Level::Info:
    _ftprintf(stderr, TEXT("[info]: "));
    break;
  case Level::Warning:
    _ftprintf(stderr, TEXT("[warning]: "));
    break;
  case Level::Error:
    _ftprintf(stderr, TEXT("[error]: "));
    break;
  case Level::Fatal:
    _ftprintf(stderr, TEXT("[fatal]: "));
    break;
  }
  _vftprintf(stderr, fmt, args);
  _ftprintf(stderr, TEXT("\n"));
  va_end(args);
}

auto Log::demangle(const char *mangled) noexcept -> std::string {
  int status;
  auto s = make_unique_with_deleter(abi::__cxa_demangle(mangled, 0, 0, &status), [](auto ptr) { std::free(ptr); });
  return s.get();
}

} // namespace AM
