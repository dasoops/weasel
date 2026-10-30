#include "stdafx.h"
#include <KeyEvent.h>

#include <cstdarg>
#include <cstdio>
#include <string.h>

// ---------------------------------------------------------------------------
// Diagnostic logging
//
// Appends one line per key translation to %TEMP%\weasel-keydiag.log so that a
// key which never reaches the Rime engine can be identified.
// Delete the file to reset; it is capped at ~1 MB.
// ---------------------------------------------------------------------------
void KeyDiagLog(const char* fmt, ...) {
  static char path[MAX_PATH * 3] = {0};
  if (!path[0]) {
    wchar_t tmp[MAX_PATH] = {0};
    if (!GetTempPathW(MAX_PATH, tmp))
      return;
    lstrcatW(tmp, L"weasel-keydiag.log");
    if (!WideCharToMultiByte(CP_UTF8, 0, tmp, -1, path, sizeof(path) - 1, NULL,
                             NULL))
      return;
  }

  WIN32_FILE_ATTRIBUTE_DATA fad = {0};
  if (GetFileAttributesExA(path, GetFileExInfoStandard, &fad) &&
      ((unsigned long long)fad.nFileSizeHigh << 32 | fad.nFileSizeLow) >
          1024ULL * 1024ULL) {
    return;  // stop growing
  }

  FILE* fp = NULL;
  if (fopen_s(&fp, path, "a") != 0 || !fp)
    return;

  SYSTEMTIME st = {0};
  GetLocalTime(&st);
  fprintf(fp, "%02d:%02d:%02d.%03d ", st.wHour, st.wMinute, st.wSecond,
          st.wMilliseconds);

  va_list ap;
  va_start(ap, fmt);
  vfprintf(fp, fmt, ap);
  va_end(ap);

  fputc('\n', fp);
  fclose(fp);
}

bool ConvertKeyEvent(UINT vkey,
                     KeyInfo kinfo,
                     const LPBYTE keyState,
                     weasel::KeyEvent& result) {
  const BYTE KEY_DOWN = 0x80;
  const BYTE TOGGLED = 0x01;
  ibus::Keycode TranslateKeycode(UINT vkey, KeyInfo kinfo);

  // set mask
  result.mask = ibus::NULL_MASK;

  if ((keyState[VK_SHIFT] & KEY_DOWN) != 0)
    result.mask |= ibus::SHIFT_MASK;

  if ((keyState[VK_CAPITAL] & TOGGLED) != 0)
    result.mask |= ibus::LOCK_MASK;

  if ((keyState[VK_CONTROL] & KEY_DOWN) != 0)
    result.mask |= ibus::CONTROL_MASK;

  if ((keyState[VK_MENU] & KEY_DOWN) != 0)
    result.mask |= ibus::ALT_MASK;

  if (kinfo.isKeyUp)
    result.mask |= ibus::RELEASE_MASK;

  if (vkey == VK_CAPITAL && !kinfo.isKeyUp) {
    // NOTE: rime assumes XK_Caps_Lock to be sent before modifier changes,
    // while VK_CAPITAL has the modifier changed already.
    // so it is necessary to revert LOCK_MASK.
    result.mask ^= ibus::LOCK_MASK;
  }

  // set keycode
  ibus::Keycode code = TranslateKeycode(vkey, kinfo);
  if (code) {
    result.keycode = code;
    return true;
  }

  const int buf_len = 8;
  static WCHAR buf[buf_len];
  static BYTE table[256];
  // 清除Ctrl、Alt鍵狀態，以令ToUnicodeEx()返回字符
  memcpy(table, keyState, sizeof(table));
  table[VK_CONTROL] = 0;
  table[VK_MENU] = 0;
  int ret = ToUnicodeEx(vkey, UINT(kinfo), table, buf, buf_len, 0, NULL);
  WCHAR first_char = (ret >= 1) ? buf[0] : 0;
  if (ret == 1) {
    KeyDiagLog("KEY vkey=0x%02x scan=0x%02x mask=0x%04x ret=1 ch=0x%04x",
               vkey, (unsigned)kinfo.scanCode, (unsigned)result.mask,
               (unsigned)first_char);
    result.keycode = UINT(first_char);
    return true;
  }

  // Fallback 1: some layouts emit a multi-character sequence; take the first.
  if (ret > 1) {
    KeyDiagLog(
        "FALLBACK_SEQ vkey=0x%02x scan=0x%02x mask=0x%04x ret=%d ch=0x%04x",
        vkey, (unsigned)kinfo.scanCode, (unsigned)result.mask, ret,
        (unsigned)first_char);
    result.keycode = UINT(first_char);
    return true;
  }

  // Fallback 2: retry with a clean key state. A stale modifier bit left over
  // from a previous key (or set by keyboard firmware/macro) can make
  // ToUnicodeEx() return 0 for a key that is otherwise perfectly typed.
  BYTE clean[256] = {0};
  int ret_clean = ToUnicodeEx(vkey, UINT(kinfo), clean, buf, buf_len, 0, NULL);
  WCHAR clean_char = (ret_clean >= 1) ? buf[0] : 0;
  KeyDiagLog(
      "FAIL vkey=0x%02x scan=0x%02x lparam=0x%08x mask=0x%04x ret=%d "
      "ret_clean=%d ch_clean=0x%04x",
      vkey, (unsigned)kinfo.scanCode, (unsigned)UINT(kinfo),
      (unsigned)result.mask, ret, ret_clean, (unsigned)clean_char);
  if (ret_clean >= 1) {
    result.keycode = UINT(clean_char);
    return true;
  }

  result.keycode = 0;
  return false;
}

ibus::Keycode TranslateKeycode(UINT vkey, KeyInfo kinfo) {
  switch (vkey) {
    case VK_BACK:
      return ibus::BackSpace;
    case VK_TAB:
      return ibus::Tab;
    case VK_CLEAR:
      return ibus::Clear;
    case VK_RETURN: {
      if (kinfo.isExtended == 1)
        return ibus::KP_Enter;
      else
        return ibus::Return;
    }
    case VK_SHIFT: {
      if (kinfo.scanCode == 0x36)
        return ibus::Shift_R;
      else
        return ibus::Shift_L;
    }
    case VK_CONTROL: {
      if (kinfo.isExtended == 1)
        return ibus::Control_R;
      else
        return ibus::Control_L;
    }
    case VK_MENU:
      return ibus::Alt_L;
    case VK_PAUSE:
      return ibus::Pause;
    case VK_CAPITAL:
      return ibus::Caps_Lock;

    case VK_KANA:
      return ibus::Hiragana_Katakana;
    // case VK_JUNJA:	return 0;
    // case VK_FINAL:	return 0;
    case VK_KANJI:
      return ibus::Kanji;

    case VK_ESCAPE:
      return ibus::Escape;

    case VK_CONVERT:
      return ibus::Henkan;
    case VK_NONCONVERT:
      return ibus::Muhenkan;
      // case VK_ACCEPT:	return 0;
      // case VK_MODECHANGE:	return 0;

    case VK_SPACE:
      return ibus::space;
    case VK_PRIOR:
      return ibus::Prior;
    case VK_NEXT:
      return ibus::Next;
    case VK_END:
      return ibus::End;
    case VK_HOME:
      return ibus::Home;
    case VK_LEFT:
      return ibus::Left;
    case VK_UP:
      return ibus::Up;
    case VK_RIGHT:
      return ibus::Right;
    case VK_DOWN:
      return ibus::Down;
    case VK_SELECT:
      return ibus::Select;
    case VK_PRINT:
      return ibus::Print;
    case VK_EXECUTE:
      return ibus::Execute;
    // case VK_SNAPSHOT:	return 0;
    case VK_INSERT:
      return ibus::Insert;
    case VK_DELETE:
      return ibus::Delete;
    case VK_HELP:
      return ibus::Help;

    case VK_LWIN:
      return ibus::Meta_L;
    case VK_RWIN:
      return ibus::Meta_R;
    // case VK_APPS:	return 0;
    // case VK_SLEEP:	return 0;
    case VK_NUMPAD0:
      return ibus::KP_0;
    case VK_NUMPAD1:
      return ibus::KP_1;
    case VK_NUMPAD2:
      return ibus::KP_2;
    case VK_NUMPAD3:
      return ibus::KP_3;
    case VK_NUMPAD4:
      return ibus::KP_4;
    case VK_NUMPAD5:
      return ibus::KP_5;
    case VK_NUMPAD6:
      return ibus::KP_6;
    case VK_NUMPAD7:
      return ibus::KP_7;
    case VK_NUMPAD8:
      return ibus::KP_8;
    case VK_NUMPAD9:
      return ibus::KP_9;
    case VK_MULTIPLY:
      return ibus::KP_Multiply;
    case VK_ADD:
      return ibus::KP_Add;
    case VK_SEPARATOR:
      return ibus::KP_Separator;
    case VK_SUBTRACT:
      return ibus::KP_Subtract;
    case VK_DECIMAL:
      return ibus::KP_Decimal;
    case VK_DIVIDE:
      return ibus::KP_Divide;
    case VK_F1:
      return ibus::F1;
    case VK_F2:
      return ibus::F2;
    case VK_F3:
      return ibus::F3;
    case VK_F4:
      return ibus::F4;
    case VK_F5:
      return ibus::F5;
    case VK_F6:
      return ibus::F6;
    case VK_F7:
      return ibus::F7;
    case VK_F8:
      return ibus::F8;
    case VK_F9:
      return ibus::F9;
    case VK_F10:
      return ibus::F10;
    case VK_F11:
      return ibus::F11;
    case VK_F12:
      return ibus::F12;
    case VK_F13:
      return ibus::F13;
    case VK_F14:
      return ibus::F14;
    case VK_F15:
      return ibus::F15;
    case VK_F16:
      return ibus::F16;
    case VK_F17:
      return ibus::F17;
    case VK_F18:
      return ibus::F18;
    case VK_F19:
      return ibus::F19;
    case VK_F20:
      return ibus::F20;
    case VK_F21:
      return ibus::F21;
    case VK_F22:
      return ibus::F22;
    case VK_F23:
      return ibus::F23;
    case VK_F24:
      return ibus::F24;

    case VK_NUMLOCK:
      return ibus::Num_Lock;
    case VK_SCROLL:
      return ibus::Scroll_Lock;

    case VK_LSHIFT:
      return ibus::Shift_L;
    case VK_RSHIFT:
      return ibus::Shift_R;
    case VK_LCONTROL:
      return ibus::Control_L;
    case VK_RCONTROL:
      return ibus::Control_R;
    case VK_LMENU:
      return ibus::Alt_L;
    case VK_RMENU:
      return ibus::Alt_R;

    case VK_OEM_AUTO:
      return ibus::Zenkaku_Hankaku;
    case VK_OEM_ENLW:
      return ibus::Zenkaku_Hankaku;
  }
  return ibus::Null;
}

/*
struct ModifierNameTableEntry
{
        LPCTSTR name;
        ibus::Modifier modifier;
};

ModifierNameTableEntry MODIFIER_NAME_TABLE[] = {
        { L"Shift", ibus::SHIFT_MASK },
        { L"CapsLock", ibus::LOCK_MASK },
        { L"Ctrl", ibus::CONTROL_MASK },
        { L"Alt", ibus::MOD1_MASK },
        { L"SUPER", ibus::SUPER_MASK },
        { L"Hyper", ibus::HYPER_MASK },
        { L"Meta", ibus::META_MASK },
        { L"Release", ibus::RELEASE_MASK },
        { NULL, ibus::NULL_MASK }
 };
*/
