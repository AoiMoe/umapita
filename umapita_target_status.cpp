#include "pch.h"
#include "am/win32util.h"
#include "umapita_def.h"
#include "umapita_monitors.h"
#include "umapita_setting.h"
#include "umapita_target_status.h"

using namespace Umapita;
using namespace AM;
using Win32::Window;

TargetStatus TargetStatus::get(Win32::StrPtr winclass, Win32::StrPtr winname) {
  if (auto target = Window::find(winclass, winname); target) {
    try {
      auto wi = target.get_info();
      return {target, wi.rcWindow, wi.rcClient};
    }
    catch (Win32::Win32ErrorCode &) {
    }
  }
  return {};
}

void TargetStatus::adjust(const UmapitaMonitors &monitors, const UmapitaSetting::PerProfile &profile) {
  if (this->window && this->window.is_visible()) {
    // ターゲットのジオメトリを更新する
    auto cW = Win32::width(this->clientRect);
    auto cH = Win32::height(this->clientRect);
    auto wW = Win32::width(this->windowRect);
    auto wH = Win32::height(this->windowRect);
    // ncX, ncY : クライアント領域の左上端を原点とした非クライアント領域の左上端（一般に負）
    // ncW, ncH : クライアント領域の占める幅と高さ（両サイドの和）
    auto ncX = this->windowRect.left - this->clientRect.left;
    auto ncY = this->windowRect.top - this->clientRect.top;
    auto ncW = wW - cW;
    auto ncH = wH - cH;
    const UmapitaSetting::PerOrientation &s = cW > cH ? profile.horizontal : profile.vertical;

    auto maybeMonitor = monitors.get_monitor_by_number(s.monitorNumber);
    if (!maybeMonitor) {
      Log::warning(TEXT("invalid monitor number: %d"), s.monitorNumber);
      return;
    }

    auto const & mR = s.isConsiderTaskbar ? maybeMonitor->work : maybeMonitor->whole;
    auto [mW, mH] = Win32::extent(mR);

    // idealCW, idealCH : 理想のクライアント領域サイズ
    // 縦横比は s.windowArea の設定に関係なくクライアント領域の縦横比で固定されるため、
    // ひとまず s.size をクライアント領域のサイズに換算してクライアント領域の W, H を求める
    LONG idealCW = 0, idealCH = 0;
    switch (s.axis) {
    case UmapitaSetting::PerOrientation::Width: {
      // 幅方向でサイズ指定
      // - s.size が正ならウィンドウの幅を s.size にする
      // - s.size が 0 ならウィンドウの幅を画面幅に合わせる
      // - s.size が負ならウィンドウの幅を画面の幅から abs(s.size) を引いた値にする
      auto sz = s.size > 0 ? s.size : mW + s.size;
      idealCW = s.windowArea == UmapitaSetting::PerOrientation::Client ? sz : sz - ncW;
      idealCH = s.aspectY * idealCW / s.aspectX;
      break;
    }
    case UmapitaSetting::PerOrientation::Height: {
      // 高さ方向でサイズ指定
      // s.size の符号については同上
      auto sz = s.size > 0 ? s.size : mH + s.size;
      idealCH = s.windowArea == UmapitaSetting::PerOrientation::Client ? sz : sz - ncH;
      idealCW = s.aspectX * idealCH / s.aspectY;
      break;
    }
    }

    // 原点に対してウィンドウを配置する
    // idealX, idealY, idealW, idealH : s.windowArea の設定により、ウィンドウ領域またはクライアント領域の座標値
    LONG idealX = 0, idealY = 0;
    auto idealW = s.windowArea == UmapitaSetting::PerOrientation::Client ? idealCW : idealCW + ncW;
    auto idealH = s.windowArea == UmapitaSetting::PerOrientation::Client ? idealCH : idealCH + ncH;
    switch (s.origin) {
    case UmapitaSetting::PerOrientation::NW:
    case UmapitaSetting::PerOrientation::W:
    case UmapitaSetting::PerOrientation::SW:
      idealX = mR.left + s.offsetX;
      break;
    case UmapitaSetting::PerOrientation::C:
    case UmapitaSetting::PerOrientation::N:
    case UmapitaSetting::PerOrientation::S:
      idealX = mR.left + mW/2 - idealW/2  + s.offsetX;
      break;
    case UmapitaSetting::PerOrientation::NE:
    case UmapitaSetting::PerOrientation::E:
    case UmapitaSetting::PerOrientation::SE:
      idealX = mR.right - idealW - s.offsetX;
      break;
    }
    switch (s.origin) {
    case UmapitaSetting::PerOrientation::NW:
    case UmapitaSetting::PerOrientation::N:
    case UmapitaSetting::PerOrientation::NE:
      idealY = mR.top + s.offsetY;
      break;
    case UmapitaSetting::PerOrientation::C:
    case UmapitaSetting::PerOrientation::W:
    case UmapitaSetting::PerOrientation::E:
      idealY = mR.top + mH/2 - idealH/2 + s.offsetY;
      break;
    case UmapitaSetting::PerOrientation::SW:
    case UmapitaSetting::PerOrientation::S:
    case UmapitaSetting::PerOrientation::SE:
      idealY = mR.bottom - idealH - s.offsetY;
      break;
    }

    // idealX, idealY, idealW, idealH をウィンドウ全体領域に換算する
    if (s.windowArea == UmapitaSetting::PerOrientation::Client) {
      idealX += ncX;
      idealY += ncY;
      idealW += ncW;
      idealH += ncH;
    }
    // idealCX, idealCY : クライアント領域の左上の座標値を計算する
    auto idealCX = idealX - ncX;
    auto idealCY = idealY - ncY;
    Log::debug(TEXT("%p, x=%ld, y=%ld, w=%ld, h=%ld"), this->window.get(), idealX, idealY, idealW, idealH);
    if ((idealX != this->windowRect.left || idealY != this->windowRect.top || idealW != wW || idealH != wH) &&
        idealW > MIN_WIDTH && idealH > MIN_HEIGHT) {
      auto willingToUpdate = true;
      try {
        this->window.set_pos(Window{}, idealX, idealY, idealW, idealH, SWP_NOACTIVATE | SWP_NOZORDER);
      }
      catch (Win32::Win32ErrorCode &ex) {
        // SetWindowPos に失敗
        Log::error(TEXT("SetWindowPos failed: %lu\n"), ex.code);
        if (ex.code == ERROR_ACCESS_DENIED) {
          // 権限がない場合、どうせ次も失敗するので ts を変更前の値のままにしておく。
          // これで余計な更新が走らなくなる。
          willingToUpdate = false;
        }
      }
      if (willingToUpdate) {
        this->windowRect = RECT{idealX, idealY, idealX+idealW, idealY+idealH};
        this->clientRect = RECT{idealCX, idealCY, idealCX+idealCW, idealCY+idealCH};
      }
    }
  }
}
