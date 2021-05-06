#pragma once

namespace AM::Win32::CustomControl {

using MessageHandlers = Handler::Map<UINT, Handler::WindowMessageTraits>;

template <class Impl>
struct Template {
  ~Template() = default;
  Template(const Template &) = delete;
  Template &operator = (const Template &) = delete;
private:
  Window m_window;
  WNDPROC m_lpPrevWndFunc = nullptr;
  MessageHandlers m_message_handlers;
  LRESULT default_handler(Window window, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (m_lpPrevWndFunc)
      return CallWindowProc(m_lpPrevWndFunc, window.get(), msg, wParam, lParam);
    return 0;
  }
  static LRESULT CALLBACK s_window_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Window window{hWnd};
    auto self = window.get_user_data<Template *>();
    if (!self)
      return Impl::s_pre_init(window, msg, wParam, lParam);
    return AM::try_orelse([&] { return self->m_message_handlers.invoke(msg, window, msg, wParam, lParam); },
                          [&] { return self->default_handler(window, msg, wParam, lParam); });
  }
protected:
  Window get_window() const { return m_window; }
  template <typename Fn>
  void register_message(UINT msg, Fn handler) {
    m_message_handlers.register_handler(msg, handler);
  }
  Template() {
    m_message_handlers.register_default_handler(Handler::binder(*this, default_handler));
  }
  void override_window_proc(AM::Win32::Window window) {
    m_window = window;
    m_lpPrevWndFunc = m_window.get_wndproc();
    m_window.set_wndproc(s_window_proc);
    m_window.set_user_data(this);
  }
  void restore_window_proc() {
    if (m_window) {
      m_window.set_wndproc(m_lpPrevWndFunc);
      m_window.set_user_data(LONG_PTR{0});
      m_window.reset();
      m_lpPrevWndFunc = nullptr;
    }
  }
  static LPARAM s_pre_init(Window, UINT, WPARAM, LPARAM) {
    return 0;
  }
};

} // namespace AM::Win32::CustomControl
