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

#include "session/apl_keymap.h"

#include "absl/container/flat_hash_map.h"

namespace mozc {
namespace session {

std::optional<absl::string_view> GetAplGlyph(uint32_t key_code) {
  // Dyalog APL US keyboard layout — Ctrl+key assignments.
  // Reference: https://help.dyalog.com/latest/Content/Language/Introduction/Keyboard/APL_Keyboards.htm
  static const auto* kMap =
      new absl::flat_hash_map<uint32_t, absl::string_view>({
          // Top row (digits)
          {'1', "¨"},  // diaeresis
          {'2', "¯"},  // high minus (macron)
          {'3', "<"},  // less-than
          {'4', "≤"},  // less-than-or-equal
          {'5', "="},  // equal
          {'6', "≥"},  // greater-than-or-equal
          {'7', ">"},  // greater-than
          {'8', "≠"},  // not-equal
          {'9', "∨"},  // or (logical)
          {'0', "∧"},  // and (logical)
          {'-', "×"},  // times
          {'=', "÷"},  // divide

          // QWERTY row
          {'q', "?"},  // query / roll
          {'w', "⍵"},  // omega (right argument)
          {'e', "∊"},  // epsilon (membership)
          {'r', "⍴"},  // rho (shape)
          {'t', "∼"},  // tilde (not)
          {'y', "↑"},  // up-arrow (take)
          {'u', "↓"},  // down-arrow (drop)
          {'i', "⍳"},  // iota (index generator)
          {'o', "○"},  // circle (pi times)
          {'p', "⋆"},  // star (power)
          {'[', "←"},  // left-arrow (assignment / receive)
          {']', "→"},  // right-arrow (branch)
          {'\\', "⍀"}, // backslash-bar (scan with axis)

          // ASDF row
          {'a', "⍺"},  // alpha (left argument)
          {'s', "⌈"},  // ceiling (maximum)
          {'d', "⌊"},  // floor (minimum)
          {'f', "_"},  // underbar (base value, used in some layouts)
          {'g', "∇"},  // del (gradient / function definition)
          {'h', "∆"},  // delta (increment)
          {'j', "∘"},  // jot (compose / null)
          {'k', "'"},  // quote (print / execute)
          {'l', "⎕"},  // quad (evaluated input / output)
          {';', "⍎"},  // up-tack-jot (execute)
          {'\'', "⍕"}, // down-tack-jot (format)

          // ZXCV row
          {'z', "⊂"},  // left-shoe (enclose)
          {'x', "⊃"},  // right-shoe (disclose)
          {'c', "∩"},  // cap (intersection)
          {'v', "∪"},  // cup (union)
          {'b', "⊥"},  // up-tack (decode / base value)
          {'n', "⊤"},  // down-tack (encode)
          {'m', "∣"},  // stile (absolute value / residue)
          {',', "⍪"},  // comma-bar (table)
          {'.', "⍀"},  // dot (inner/outer product used with /)
          {'/', "⌿"},  // slash-bar (replicate with axis)
      });

  auto it = kMap->find(key_code);
  if (it != kMap->end()) return it->second;
  return std::nullopt;
}

std::optional<absl::string_view> GetAplShiftedGlyph(uint32_t key_code) {
  // Dyalog APL US keyboard layout — Ctrl+Shift+key assignments (second layer).
  //
  // Key codes are the keyvals produced with Shift held, because the IBus key
  // translator encodes Shift into the keyval for printable keys — the SHIFT
  // modifier flag is absent from KeyEvent::modifier_keys() for these keys.
  // Letter keys therefore appear as uppercase ('E', 'T', …); bracket/punctuation
  // keys appear as their shifted symbols ('{', '}', ':', '"', '<', '>', '?', '|').
  //
  // Reference: Dyalog APL US keyboard layout (Ctrl+Shift layer)
  // Note: on UK layout ≢ lives on the ISO '#' key (Ctrl+Shift+# → '~');
  // on US layout the same APL position is the apostrophe key (Ctrl+Shift+' → '"').
  static const auto* kShiftedMap =
      new absl::flat_hash_map<uint32_t, absl::string_view>({
          // Number row — Ctrl+Shift+key → shifted keyval (US layout)
          // Alignment: ` 1 _ 3 4 5 6 7 8 9 0 - =  (_ = nothing on key 2)
          {'~', "⌺"},  // stencil — Ctrl+Shift+`
          {'!', "⌶"},  // I-beam — Ctrl+Shift+1
          // '@' (Ctrl+Shift+2) → nothing
          {'#', "⍒"},  // grade down — Ctrl+Shift+3
          {'$', "⍋"},  // grade up — Ctrl+Shift+4
          {'%', "⌽"},  // rotate / reverse — Ctrl+Shift+5
          {'^', "⍉"},  // transpose — Ctrl+Shift+6
          {'&', "⊖"},  // rotate first axis — Ctrl+Shift+7
          {'*', "⍟"},  // circle-star (natural log) — Ctrl+Shift+8
          {'(', "⍱"},  // nor — Ctrl+Shift+9
          {')', "⍲"},  // nand — Ctrl+Shift+0
          {'_', "!"},  // factorial — Ctrl+Shift+-  ('!' here is the output glyph)
          {'+', "⌹"},  // quad-divide (domino) — Ctrl+Shift+=

          // QWERTY row (letters — Ctrl+Shift+key → uppercase keyval)
          {'E', "⍷"},  // epsilon-underbar (find)
          {'T', "⍨"},  // tilde-diaeresis (selfie / commute)
          {'I', "⍸"},  // iota-underbar (indices of)
          {'O', "⍥"},  // circle-diaeresis (over)
          {'P', "⍣"},  // star-diaeresis (power operator)

          // QWERTY row (punctuation — Ctrl+Shift+[ → '{', Ctrl+Shift+] → '}')
          {'{', "⍞"},  // quote-quad (character input)
          {'}', "⍬"},  // zilde (empty numeric vector)

          // ASDF row (letters)
          {'F', "⍛"},  // circle-star
          {'J', "⍤"},  // jot-diaeresis (rank)
          {'K', "⌸"},  // quad-equal (key / from)
          {'L', "⌷"},  // squish-quad (index)

          // ASDF row (punctuation — Ctrl+Shift+; → ':', Ctrl+Shift+' → '"')
          {':', "≡"},  // identical to
          {'"', "≢"},  // not-identical-to — Ctrl+Shift+' (US apostrophe, next to Enter)

          // QWERTY row tail (Ctrl+Shift+\ → '|', same keysym on UK ISO and US ANSI)
          {'|', "⊣"},  // left-tack

          // ZXCV row (letters)
          {'Z', "⊆"},  // left-shoe-underbar (partition)

          // ZXCV row (punctuation — Ctrl+Shift+, → '<', . → '>', / → '?')
          {'<', "⍪"},  // comma-bar (table)
          {'>', "⍙"},  // delta-underbar
          {'?', "⍠"},  // quad-colon (variant)
      });

  auto it = kShiftedMap->find(key_code);
  if (it != kShiftedMap->end()) return it->second;
  return std::nullopt;
}

}  // namespace session
}  // namespace mozc
