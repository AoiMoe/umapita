#pragma once

namespace AM::Win32::Dialog {

template <class Impl>
struct Template {
  ~Template() = default;
  Template(const Template &) = delete;
  Template &operator = (const Template &) = delete;
protected:
  using MessageHandlers = Handler::Map<UINT, Handler::DialogMessageTraits>;
  using CommandHandlers = Handler::Map<int, Handler::DialogMessageTraits>;
private:
  Window m_owner;
  Window m_window;
  MessageHandlers m_message_handlers;
  CommandHandlers m_command_handlers;
  CommandHandlers m_system_command_handlers;
  //
  static INT_PTR CALLBACK s_dialog_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Window window{hWnd};
    Impl *self;
    if (msg == WM_INITDIALOG) {
      SetWindowLongPtr(hWnd, DWLP_USER, lParam);
      self = reinterpret_cast<Impl *>(lParam);
      self->m_window = hWnd;
    } else {
      self = reinterpret_cast<Impl *>(GetWindowLongPtr(hWnd, DWLP_USER));
    }
    return AM::try_or([&] {
                        if (!self)
                          return Impl::s_pre_init(window, msg, wParam, lParam);
                        return self->dialog_proc(hWnd, msg, wParam, lParam);
                      }, FALSE);
  }
  CommandHandlers::MaybeResult wm_command_handler(Window window, UINT msg, WPARAM wParam, LPARAM lParam) {
    return m_command_handlers.invoke(LOWORD(wParam), window, msg, wParam, lParam);
  }
  CommandHandlers::MaybeResult wm_system_command_handler(Window window, UINT msg, WPARAM wParam, LPARAM lParam) {
    return m_system_command_handlers.invoke(LOWORD(wParam), window, msg, wParam, lParam);
  }
protected:
  Template() {
    m_message_handlers.register_handler(WM_COMMAND, Handler::binder(*this, wm_command_handler));
    m_message_handlers.register_handler(WM_SYSCOMMAND, Handler::binder(*this, wm_system_command_handler));
  }
  INT_PTR dialog_proc(Window window, UINT msg, WPARAM wParam, LPARAM lParam) {
    return m_message_handlers.invoke(msg, window, msg, wParam, lParam);
  }
  Window get_owner() const { return m_owner; }
  Window get_window() const { return m_window; }
  template <typename Fn>
  void register_message(UINT msg, Fn handler) {
    m_message_handlers.register_handler(msg, handler);
  }
  template <typename Fn>
  void register_command(int id, Fn handler) {
    m_command_handlers.register_handler(id, handler);
  }
  template <typename Fn>
  void register_system_command(int id, Fn handler) {
    m_system_command_handlers.register_handler(id, handler);
  }
  static INT_PTR s_pre_init(Window, UINT, WPARAM, LPARAM) {
    return FALSE;
  }
  INT_PTR open_modal(Window owner) {
    HINSTANCE hInst = owner ? owner.get_instance() : reinterpret_cast<HINSTANCE>(GetModuleHandle(nullptr));
    m_owner = owner;
    return DialogBoxParam(hInst, Impl::get_dialog_template_name(), owner.get(), &s_dialog_proc, reinterpret_cast<LPARAM>(static_cast<Impl *>(this)));
  }
  Window create_modeless(Window owner) {
    HINSTANCE hInst = owner ? owner.get_instance() : reinterpret_cast<HINSTANCE>(GetModuleHandle(nullptr));
    m_owner = owner;
    m_window = CreateDialogParam(hInst, Impl::get_dialog_template_name(), owner.get(), &s_dialog_proc, reinterpret_cast<LPARAM>(static_cast<Impl *>(this)));
    return m_window;
  }
};

} // namespace AM::Win32::Dialog
