#pragma once

#include "am/win32util.h"

namespace AM::Win32::Handler {

template <typename ResultT, ResultT DEFAULT_RESULT_VALUE>
struct MessageTraits {
  using Result = ResultT;
  using MaybeResult = std::optional<Result>;
  using Handler = std::function<MaybeResult (Window, UINT, WPARAM, LPARAM)>;
  static constexpr Result DEFAULT_RESULT = DEFAULT_RESULT_VALUE;
  template <typename ...Args>
  static constexpr bool IS_INVOKER_ARGS = std::is_invocable_v<MaybeResult (Window, UINT, WPARAM, LPARAM), Args...>;
  //
  template <typename Fn, std::enable_if_t<std::is_invocable_r_v<MaybeResult, Fn, Window, UINT, WPARAM, LPARAM>, bool> = true>
  static Handler make(Fn h) noexcept {
    return h;
  }
  template <typename Fn, std::enable_if_t<std::is_invocable_r_v<MaybeResult, Fn, Window>, bool> = true>
  static Handler make(Fn h) noexcept {
    return [h](Window dialog, UINT, WPARAM, LPARAM) { return h(dialog); };
  };
  template <typename Fn, std::enable_if_t<std::is_invocable_r_v<MaybeResult, Fn, Window, Window, int, int>, bool> = true>
  static Handler make(Fn h) noexcept {
    return [h](Window dialog, UINT msg, WPARAM wParam, LPARAM lParam) {
             auto id = LOWORD(wParam);
             auto notify = HIWORD(wParam);
             auto control = Window::from(lParam);
             return h(dialog, control, id, notify);
           };
  }
  template <typename Fn, std::enable_if_t<std::is_invocable_r_v<MaybeResult, Fn>, bool> = true>
  static Handler make(Fn h) noexcept {
    return [h](Window, UINT, WPARAM, LPARAM) { return h(); };
  };
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
  //
  template <typename Fn>
  void register_handler(Key key, Fn handler) {
    m_map.emplace(key, HandlerTraits::make(handler));
  }
  template <typename Fn>
  void register_default_handler(Fn handler) {
    m_default = HandlerTraits::make(handler);
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

} // namespace AM::Win32::Handler
