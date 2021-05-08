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
  static Impl *&get_initial_holder() {
    static AM_TLS_SPEC Impl *instance;
    return instance;
  }
  static INT_PTR CALLBACK s_initial_dialog_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Window window{hWnd};
    Impl *&instance = get_initial_holder();
    if (instance == nullptr) {
      Log::fatal(TEXT("Dialog: cannot get instance in s_initial_dialog_proc"));
      abort();
    }
    Impl *self = instance;
    self->m_window = hWnd;
    window.set_dialog_user_data(self);
    instance = nullptr;
    if (window.set_dlgproc(&s_dialog_proc) != &s_initial_dialog_proc) {
      Log::fatal(TEXT("Dialog: unmatch s_initial_dialog_proc"));
      abort();
    }
    return AM::try_or([&] { return self->dialog_proc(window, msg, wParam, lParam); }, FALSE);
  }
  static INT_PTR CALLBACK s_dialog_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Window window{hWnd};
    Impl *self = window.get_dialog_user_data<Impl *>();
    if (!self) {
      Log::fatal(TEXT("Dialog: cannot get instance in s_dialog_proc"));
      abort();
    }
    return AM::try_or([&] { return self->dialog_proc(window, msg, wParam, lParam); }, FALSE);
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
  INT_PTR open_modal(Window owner) {
    HINSTANCE hInst = owner ? owner.get_instance() : reinterpret_cast<HINSTANCE>(GetModuleHandle(nullptr));
    m_owner = owner;
    if (get_initial_holder() != nullptr) {
      Log::fatal(TEXT("Dialog: other initial instance exists on the same thread"));
      abort();
    }
    get_initial_holder() = static_cast<Impl *>(this);
    return DialogBox(hInst, Impl::get_dialog_template_name(), owner.get(), &s_initial_dialog_proc);
  }
  void create_modeless(Window owner) {
    HINSTANCE hInst = owner ? owner.get_instance() : reinterpret_cast<HINSTANCE>(GetModuleHandle(nullptr));
    m_owner = owner;
    if (get_initial_holder() != nullptr) {
      Log::fatal(TEXT("Dialog: other initial instance exists on the same thread"));
      abort();
    }
    get_initial_holder() = static_cast<Impl *>(this);
    throw_if<Win32ErrorCode, HWND, nullptr>(CreateDialog(hInst, Impl::get_dialog_template_name(), owner.get(), &s_initial_dialog_proc));
  }
};

} // namespace AM::Win32::Dialog
