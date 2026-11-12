#pragma once
// Which character a key types on the Cardputer Zero's keyboard, which reports plain
// Linux input key codes (US layout). Enter, Backspace, Fn, Sym and friends type nothing.
namespace hal {

// The character for a Linux input key code, with or without Shift; 0 if it types none.
char keyToChar(int linuxKeyCode, bool shift);

}  // namespace hal
