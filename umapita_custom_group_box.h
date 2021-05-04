#pragma once

//
// カスタムグループボックス
//
// WndProc をオーバライドしていくつかの WM を置き換える
//
class UmapitaCustomGroupBox {
  AM::Win32::Window m_window;
  WNDPROC m_lpPrevWndFunc = nullptr;
  bool m_isSelected = false;
  HFONT m_hFont = nullptr;
  //
  static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    AM::Win32::Window window{hWnd};
    auto self = window.get_user_data<UmapitaCustomGroupBox *>();
    switch (msg) {
    case WM_PAINT:
      AM::try_or_void([self, window]() { self->on_paint(window); });
      return 0;
    case WM_GETDLGCODE:
      return DLGC_STATIC;
    case WM_NCHITTEST:
      return HTTRANSPARENT;
    }
    return CallWindowProc(self->m_lpPrevWndFunc, hWnd, msg, wParam, lParam);
  }
  void on_paint(AM::Win32::Window window) {
    auto p = window.begin_paint();

    // テキスト描画
    auto scopedSelect = AM::Win32::scoped_select_font(p.hdc(), m_hFont); // m_hFont は nullptr でも問題ない

    TEXTMETRIC tm;
    GetTextMetrics(p.hdc(), &tm);

    auto text = window.get_text();
    int len = text.size();

    SIZE size;
    GetTextExtentPoint32(p.hdc(), text.c_str(), len, &size);

    {
      auto scopedBkMode = AM::Win32::scoped_set_bk_mode(p.hdc(), TRANSPARENT);
      auto scopedTextColor = AM::Win32::scoped_set_text_color(p.hdc(), GetSysColor(COLOR_WINDOWTEXT));
      TextOut(p.hdc(), tm.tmAveCharWidth*5/4, 0, text.c_str(), len);
    }

    // 枠描画
    auto rect = window.get_client_rect();
    if (len) {
      // テキスト部分を描画エリアから除外する
      auto r = AM::Win32::create_rect_region(tm.tmAveCharWidth, 0, tm.tmAveCharWidth*3/2 + size.cx, size.cy);
      ExtSelectClipRgn(p.hdc(), r.get(), RGN_DIFF);
    }
    {
      // 選択状態のときは黒くて幅 2 のラインを、非選択状態のときは灰色で幅 1 のラインを描く
      auto r = AM::Win32::create_rect_region(rect.left, rect.top+size.cy/2, rect.right, rect.bottom);
      auto hBrush = reinterpret_cast<HBRUSH>(GetStockObject(m_isSelected ? BLACK_BRUSH : LTGRAY_BRUSH));
      auto w = m_isSelected ? 2 : 1;
      FrameRgn(p.hdc(), r.get(), hBrush, w, w);
    }
    SelectClipRgn(p.hdc(), nullptr);
  }
  void redraw() {
    if (m_window) {
      auto parent = m_window.get_parent();
      auto rect = m_window.get_window_rect();
      MapWindowPoints(HWND_DESKTOP, parent.get(), reinterpret_cast<LPPOINT>(&rect), 2);
      parent.invalidate_rect(rect, true);
      parent.update_window();
    }
  }
public:
  UmapitaCustomGroupBox() { }
  void override_window_proc(AM::Win32::Window window) {
    m_window = window;
    m_lpPrevWndFunc = m_window.get_wndproc();
    m_window.set_wndproc(WndProc);
    m_window.set_user_data(this);
    redraw();
  }
  void restore_window_proc() {
    if (m_window) {
      m_window.set_wndproc(m_lpPrevWndFunc);
      m_window.set_user_data(LONG_PTR{0});
      m_window.reset();
      m_lpPrevWndFunc = nullptr;
    }
  }
  void set_selected(bool isSelected) {
    if ((m_isSelected && !isSelected) || (!m_isSelected && isSelected)) {
      m_isSelected = isSelected;
      redraw();
    }
  }
  void set_font(HFONT hFont) {
    m_hFont = hFont;
    redraw();
  }
};
