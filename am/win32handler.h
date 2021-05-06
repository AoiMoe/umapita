#pragma once

#include "am/win32util.h"

namespace AM::Win32::Handler {

namespace HandlerConverter {

template <typename Fn, std::enable_if_t<std::is_invocable_v<Fn, Window, UINT, WPARAM, LPARAM>, bool> = true>
auto convert(Fn h) {
  return h;
}

template <typename Fn, std::enable_if_t<std::is_invocable_v<Fn, Window>, bool> = true>
auto convert(Fn h) {
  return [h](Window dialog, UINT, WPARAM, LPARAM) { return h(dialog); };
}

template <typename Fn, std::enable_if_t<std::is_invocable_v<Fn, Window, Window, int, int>, bool> = true>
auto convert(Fn h) {
  return [h](Window dialog, UINT msg, WPARAM wParam, LPARAM lParam) {
           auto id = LOWORD(wParam);
           auto notify = HIWORD(wParam);
           auto control = Window::from(lParam);
           return h(dialog, control, id, notify);
         };
}

template <typename Fn, std::enable_if_t<std::is_invocable_v<Fn>, bool> = true>
auto convert(Fn h) {
  return [h](Window, UINT, WPARAM, LPARAM) { return h(); };
}

}

template <typename ResultT, ResultT DEFAULT_RESULT_VALUE>
struct MessageTraits {
  using Result = ResultT;
  using MaybeResult = std::optional<Result>;
  using Handler = std::function<MaybeResult (Window, UINT, WPARAM, LPARAM)>;
  static constexpr Result DEFAULT_RESULT = DEFAULT_RESULT_VALUE;
  template <typename ...Args>
  static constexpr bool IS_INVOKER_ARGS = std::is_invocable_v<MaybeResult (Window, UINT, WPARAM, LPARAM), Args...>;

  // Don't use std::bind() to generate handler parameter because its result function object accepts excess arguments and
  // it jams SFINAE on overloading of HandlerConverter::convert.
  template <typename Fn>
  static Handler convert(Fn handler) noexcept {
    return HandlerConverter::convert(handler);
  }
};
using WindowMessageTraits = MessageTraits<LRESULT, 0>;
using DialogMessageTraits = MessageTraits<INT_PTR, FALSE>;

template <typename KeyT, class HandlerTraitsT>
struct Map {
  using Key = KeyT;
  using HandlerTraits = HandlerTraitsT;
  using Handler = typename HandlerTraits::Handler;
  using Result = typename HandlerTraits::Result;
  using MaybeResult = typename HandlerTraits::MaybeResult;

  // Don't use std::bind() to generate handler parameter because its result function object accepts excess arguments and
  // it jams SFINAE on overloading of HandlerConverter::convert.
  template <typename Fn>
  void register_handler(Key key, Fn handler) {
    m_map.emplace(key, HandlerTraits::convert(handler));
  }
  template <typename Fn>
  void register_default_handler(Fn handler) {
    m_default = HandlerTraits::convert(handler);
  }
  template <typename ...Args>
  std::enable_if_t<HandlerTraits::template IS_INVOKER_ARGS<Args...>, Result> invoke(Key id, Args &&...args) const {
    if (auto i = m_map.find(id); i != m_map.end())
      if (auto ret = i->second(std::forward<Args>(args)...); ret)
        return *ret;
    if (m_default)
      if (auto ret = m_default(std::forward<Args>(args)...); ret)
        return *ret;
    return HandlerTraits::DEFAULT_RESULT;
  }
  ~Map() { }
  Map() { }
  Map(const Map &) = delete;
  Map &operator = (const Map &) = delete;
private:
  std::unordered_map<Key, Handler> m_map;
  Handler m_default;
};

namespace Bits_ {

template <class Class, typename Result, typename ...Args>
struct PerfectBinder {
  Class &cls;
  Result (Class::*fp)(Args...);
  template <typename ...Params>
  std::enable_if_t<std::is_invocable_v<Result (Args...), Params...>, Result> operator () (Params &&...params) const {
    return (cls.*fp)(std::forward<Params>(params)...);
  }
  PerfectBinder(Class &acls, Result (Class::*afp)(Args...)) : cls{acls}, fp{afp} { }
};

template <class Class, typename Result, typename ...Args>
struct PerfectBinder<const Class, Result, Args...> {
  const Class &cls;
  Result (Class::*fp)(Args...) const;
  template <typename ...Params>
  std::enable_if_t<std::is_invocable_v<Result (Args...), Params...>, Result> operator () (Params &&...params) const {
    return (cls.*fp)(std::forward<Params>(params)...);
  }
  PerfectBinder(const Class &acls, Result (Class::*afp)(Args...) const) : cls{acls}, fp{afp} { }
};

}

//
// bind this pointer and its member function.
//
template <class Class, typename Result, typename ...Args>
auto binder(Class &cls, Result (Class::*fp)(Args...)) {
  return Bits_::PerfectBinder<Class, Result, Args...>{cls, fp};
}

template <class Class, typename Result, typename ...Args>
auto binder(const Class &cls, Result (Class::*fp)(Args...) const) {
  return Bits_::PerfectBinder<const Class, Result, Args...>{cls, fp};
}

} // namespace AM::Win32::Handler
