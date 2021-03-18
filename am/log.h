#pragma once

namespace AM {

class Log {
public:
  enum class Level {
    Debug,
    Info,
    Warning,
    Error,
    Fatal,
  };
  template <typename... ArgTypes>
  static void debug(LPCTSTR fmt, ArgTypes... args) noexcept {
    log(Level::Debug, fmt, args...);
  }
  template <typename... ArgTypes>
  static void info(LPCTSTR fmt, ArgTypes... args) noexcept {
    log(Level::Info, fmt, args...);
  }
  template <typename... ArgTypes>
  static void warning(LPCTSTR fmt, ArgTypes... args) noexcept {
    log(Level::Warning, fmt, args...);
  }
  template <typename... ArgTypes>
  static void error(LPCTSTR fmt, ArgTypes... args) noexcept {
    log(Level::Error, fmt, args...);
  }
  template <typename... ArgTypes>
  static void fatal(LPCTSTR fmt, ArgTypes... args) noexcept {
    log(Level::Fatal, fmt, args...);
  }
  static void log(Level, LPCTSTR fmt, ...) noexcept ;
  static auto demangle(const char *mangled) noexcept -> std::string;
  template <typename T>
  static auto demangled() noexcept {
    return demangle(typeid(T).name());
  }
};

} // namespace AM
