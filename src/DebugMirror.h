// DebugMirror.h — best-effort copy of a handful of key diagnostic lines
// to the native USB console (USBSerial, GPIO19/20 — the panel-mount
// breakout wired for the final enclosure), alongside Serial (UART0,
// bridged out via CP2102) which is completely unaffected by this file.
//
// Why: the final product only exposes the new breakout; the CP2102 port
// stays on the board for whenever it's opened up again later. Only one of
// the two is ever connected at a time, and nobody wants to reflash just to
// pick which — so both are live in every build, and usbMirror() is a
// no-op when nothing's listening on the USB side (HWCDC::write() is
// non-blocking when disconnected — verified by reading
// framework-arduinoespressif32/cores/esp32/HWCDC.cpp directly, not
// assumed; bounded even if a host IS connected but stalls, via
// setTxTimeoutMs() below, so this can never hang the voice pipeline).
//
// Deliberately NOT a full mirror of every verbose [DBG] line — that would
// mean touching nearly every file in the project just to duplicate output.
// Just the moments actually useful for "something's wrong, let me check
// without opening the case": boot confirmation, what REX heard, what it
// replied, errors, watchdog timeouts.
#pragma once

#ifdef ENABLE_USB_CONSOLE
#include <HWCDC.h>

inline void usbConsoleBegin() {
  USBSerial.setTxTimeoutMs(50);  // bound worst-case stall if a host connects but stops reading
  USBSerial.begin(115200);
}

inline void usbMirror(const String& line) {
  USBSerial.println(line);
}
#else
inline void usbConsoleBegin() {}
inline void usbMirror(const String&) {}
#endif
