#include "protocol/serial_line_framer.h"

#include <cassert>
#include <cstring>
#include <string>

using Framer = protocol::SerialLineFramer<8>;

template <size_t Capacity>
static typename protocol::SerialLineFramer<Capacity>::Result feed(
    protocol::SerialLineFramer<Capacity>& framer, const std::string& bytes) {
  using Result = typename protocol::SerialLineFramer<Capacity>::Result;
  Result result = Result::None;
  for (char byte : bytes) {
    const Result next = framer.push(byte);
    if (next != Result::None) result = next;
  }
  return result;
}

int main() {
  Framer framer;

  // Fragments remain buffered until LF; CRLF is accepted and stripped.
  assert(feed(framer, "ab") == Framer::Result::None);
  assert(feed(framer, "cd\r") == Framer::Result::None);
  assert(framer.push('\n') == Framer::Result::LineReady);
  assert(std::strcmp(framer.line(), "abcd") == 0);

  // Multiple frames in a single stream are independently available.
  assert(framer.push('a') == Framer::Result::None);
  assert(framer.push('\n') == Framer::Result::LineReady);
  assert(std::strcmp(framer.line(), "a") == 0);
  assert(framer.push('b') == Framer::Result::None);
  assert(framer.push('\n') == Framer::Result::LineReady);
  assert(std::strcmp(framer.line(), "b") == 0);
  assert(feed(framer, "\n\r\n") == Framer::Result::Empty);

  // Capacity includes NUL: seven bytes fit in this eight-byte buffer.
  assert(feed(framer, "1234567\n") == Framer::Result::LineReady);
  assert(std::strcmp(framer.line(), "1234567") == 0);
  assert(feed(framer, "12345678\n") == Framer::Result::LineTooLong);

  // Discard the whole oversized frame, even when its suffix looks actionable.
  assert(feed(framer, "12345678{\"cmd\":\"ping\"}\n") ==
         Framer::Result::LineTooLong);
  assert(feed(framer, "valid\n") == Framer::Result::LineReady);
  assert(std::strcmp(framer.line(), "valid") == 0);

  // The production capacity accepts exactly 4095 payload bytes plus NUL.
  protocol::SerialLineFramer<> productionFramer;
  assert(feed(productionFramer, std::string(4095, 'x') + "\n") ==
         protocol::SerialLineFramer<>::Result::LineReady);
  assert(std::strlen(productionFramer.line()) == 4095);
  assert(feed(productionFramer, std::string(4096, 'x') + "\n") ==
         protocol::SerialLineFramer<>::Result::LineTooLong);

  // Embedded NUL invalidates the whole line, including a valid-looking prefix.
  protocol::SerialLineFramer<32> nulFramer;
  assert(feed(nulFramer, "{\"cmd\":\"ping\"}") ==
         protocol::SerialLineFramer<32>::Result::None);
  assert(nulFramer.push('\0') == protocol::SerialLineFramer<32>::Result::None);
  assert(feed(nulFramer, "\n") == protocol::SerialLineFramer<32>::Result::InvalidLine);
  assert(feed(nulFramer, "safe\n") == protocol::SerialLineFramer<32>::Result::LineReady);
  assert(std::strcmp(nulFramer.line(), "safe") == 0);

  return 0;
}
