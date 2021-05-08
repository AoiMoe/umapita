#pragma once

#include "am/log.h"

// XXX: __declspec(thread)
#define AM_TLS_SPEC __thread

namespace AM {

//
// false_v : for constexpr if
//
template <typename>
constexpr bool false_v = false;

//
// std::unique_ptr util
//

// simple pointer w/ simple deleter function
template<typename T, typename D>
auto make_unique_with_deleter(T *t, D &&d) noexcept -> std::unique_ptr<T, D> {
  return std::unique_ptr<T, D>(t, std::move(d));
}

namespace Bits_ {

template <typename T, typename D>
class Deleter {
  D m_deleter_;
public:
  using pointer = T;
  Deleter(D &&d) : m_deleter_(std::move(d)) { }
  void operator () (pointer &&v) { m_deleter_(std::move(v)); }
};

} // namespace Bits_

// simple copyable variable w/ simple deleter function
template<typename T, typename D>
auto make_unique_with_deleter(T &&t, D &&d) noexcept -> std::unique_ptr<T, Bits_::Deleter<T, D>> {
  return std::unique_ptr<T, Bits_::Deleter<T, D>>(t, std::move(d));
}

//
// results and exceptions
//
template <typename T, typename F>
auto try_or(F f, T o) -> T {
  try {
    return f();
  } catch (...) {
    return o;
  }
}

template <typename F>
auto try_or_void(F f) -> void {
  static_assert(std::is_void_v<std::invoke_result_t<F>>);
  try {
    f();
  } catch (...) {
  }
}

template <typename F, typename G>
auto try_orelse(F f, G of) -> std::enable_if_t<std::is_void_v<std::invoke_result_t<F>>, void> {
  static_assert(std::is_void_v<std::invoke_result_t<F>> && std::is_void_v<std::invoke_result_t<G>>);
  try {
    f();
  } catch (...) {
    of();
  }
}

template <typename F, typename G>
auto try_orelse(F f, G of) -> std::enable_if_t<!std::is_void_v<std::invoke_result_t<F>>, std::invoke_result_t<F>> {
  static_assert(std::is_same_v<std::invoke_result_t<F>, std::invoke_result_t<G>>);
  try {
    return f();
  } catch (...) {
    return of();
  }
}

template <class Inherited, class Base>
struct Error : Base {
  using Base::Base;
  Error(const char *msg = Log::demangled<Inherited>().c_str()) : Base{msg} { }
};

template <class Inherited>
using RuntimeError = Error<Inherited, std::runtime_error>;

template <class Inherited>
using LogicError = Error<Inherited, std::logic_error>;

struct IllegalArgument : LogicError<IllegalArgument> {
  IllegalArgument() : LogicError<IllegalArgument>{} { }
  IllegalArgument(const char *msg) : LogicError<IllegalArgument>{msg} { }
};

template <class Exception, typename T>
T ensure_expected(T c, const char * = nullptr) {
  return c;
}

template <class Exception, auto expect0, decltype(expect0)... expects>
decltype(expect0) ensure_expected(decltype(expect0) c, const char *msg = nullptr) {
  if (c != expect0) {
    throw Exception{c, msg};
  }
  return ensure_expected<Exception, expects...>(c, msg);
}

template <class Exception, typename T>
T throw_if(T c, const char * = nullptr) {
  return c;
}

template <class Exception, auto error0, decltype(error0)... errors>
decltype(error0) throw_if(decltype(error0) c, const char *msg = nullptr) {
  if (c == error0) {
    throw Exception{c, msg};
  }
  return throw_if<Exception, errors...>(c, msg);
}

template <class Exception, typename Value, Value error0, Value... errors>
Value throw_if(Value c, const char *msg = nullptr) {
  if (c == error0) {
    throw Exception{c, msg};
  }
  return throw_if<Exception, Value, errors...>(c, msg);
}

template <typename Code, Code OK, class Identifier = Code>
struct ErrorCode : public RuntimeError<ErrorCode<Code, OK, Identifier>> {
  explicit ErrorCode(Code c = Code{}, const char *msg = nullptr) : RuntimeError<ErrorCode<Code, OK, Identifier>>{msg}, code{c} {}
  const Code code;
  template <class Exception = ErrorCode>
  static Code ensure_ok(Code c, const char *msg = nullptr) {
    return ensure_expected<Exception, OK>(c, msg);
  }
};

//
// tuple
//
template <std::size_t i=0, typename Fn, typename ...Args>
void tuple_foreach(const std::tuple<Args...> &t, [[maybe_unused]] Fn f) {
  if constexpr (i < sizeof...(Args)) {
    f(std::get<i>(t));
    tuple_foreach<i+1>(t, f);
  }
}

} // namespace AM
