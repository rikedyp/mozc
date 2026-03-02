// Copyright 2010-2021, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef MOZC_MAC_APL_KEYCODE_MAP_H_
#define MOZC_MAC_APL_KEYCODE_MAP_H_

// Maps macOS Carbon virtual key codes (kVK_ANSI_*) to the ASCII character
// at that physical key position on a US QWERTY layout.  This is used by the
// APL shifting-key feature to perform layout-independent glyph lookup:
//   virtual key code  →  ASCII char  →  APL glyph (via apl_keymap)
//
// Returns '\0' if the key code has no mapping (non-printable or unknown key).

#import <Carbon/Carbon.h>

namespace mozc {
namespace mac {

inline char AplVirtualKeyToChar(unsigned short keyCode) {
  // Table indexed by Carbon virtual key code.  Covers all printable keys
  // on a standard ANSI keyboard.  Entries are lowercase / unshifted.
  // The table size is 0x33 + 1 = 52 entries (kVK_ANSI_Grave = 0x32 is
  // the highest code we need).
  static constexpr char kTable[0x33] = {
      // 0x00  kVK_ANSI_A
      'a',
      // 0x01  kVK_ANSI_S
      's',
      // 0x02  kVK_ANSI_D
      'd',
      // 0x03  kVK_ANSI_F
      'f',
      // 0x04  kVK_ANSI_H
      'h',
      // 0x05  kVK_ANSI_G
      'g',
      // 0x06  kVK_ANSI_Z
      'z',
      // 0x07  kVK_ANSI_X
      'x',
      // 0x08  kVK_ANSI_C
      'c',
      // 0x09  kVK_ANSI_V
      'v',
      // 0x0A  (ISO Section key — not used)
      0,
      // 0x0B  kVK_ANSI_B
      'b',
      // 0x0C  kVK_ANSI_Q
      'q',
      // 0x0D  kVK_ANSI_W
      'w',
      // 0x0E  kVK_ANSI_E
      'e',
      // 0x0F  kVK_ANSI_R
      'r',
      // 0x10  kVK_ANSI_Y
      'y',
      // 0x11  kVK_ANSI_T
      't',
      // 0x12  kVK_ANSI_1
      '1',
      // 0x13  kVK_ANSI_2
      '2',
      // 0x14  kVK_ANSI_3
      '3',
      // 0x15  kVK_ANSI_4
      '4',
      // 0x16  kVK_ANSI_6
      '6',
      // 0x17  kVK_ANSI_5
      '5',
      // 0x18  kVK_ANSI_Equal
      '=',
      // 0x19  kVK_ANSI_9
      '9',
      // 0x1A  kVK_ANSI_7
      '7',
      // 0x1B  kVK_ANSI_Minus
      '-',
      // 0x1C  kVK_ANSI_8
      '8',
      // 0x1D  kVK_ANSI_0
      '0',
      // 0x1E  kVK_ANSI_RightBracket
      ']',
      // 0x1F  kVK_ANSI_O
      'o',
      // 0x20  kVK_ANSI_U
      'u',
      // 0x21  kVK_ANSI_LeftBracket
      '[',
      // 0x22  kVK_ANSI_I
      'i',
      // 0x23  kVK_ANSI_P
      'p',
      // 0x24  kVK_Return (not a printable key position)
      0,
      // 0x25  kVK_ANSI_L
      'l',
      // 0x26  kVK_ANSI_J
      'j',
      // 0x27  kVK_ANSI_Quote
      '\'',
      // 0x28  kVK_ANSI_K
      'k',
      // 0x29  kVK_ANSI_Semicolon
      ';',
      // 0x2A  kVK_ANSI_Backslash
      '\\',
      // 0x2B  kVK_ANSI_Comma
      ',',
      // 0x2C  kVK_ANSI_Slash
      '/',
      // 0x2D  kVK_ANSI_N
      'n',
      // 0x2E  kVK_ANSI_M
      'm',
      // 0x2F  kVK_ANSI_Period
      '.',
      // 0x30  kVK_Tab (not a printable key position)
      0,
      // 0x31  kVK_Space
      ' ',
      // 0x32  kVK_ANSI_Grave
      '`',
  };

  if (keyCode >= sizeof(kTable)) {
    return '\0';
  }
  return kTable[keyCode];
}

}  // namespace mac
}  // namespace mozc

#endif  // MOZC_MAC_APL_KEYCODE_MAP_H_
