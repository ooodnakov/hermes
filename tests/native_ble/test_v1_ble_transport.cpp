#include "../../src/network/v1_ble_transport.h"

#include <cassert>
#include <cstring>
#include <string>
#include <vector>

using network::V1BleTransport;

int main() {
  V1BleTransport transport;
  using Result = V1BleTransport::InputResult;

  const uint8_t first[] = {'{', '}', '\n', 'a'};
  assert(transport.enqueueInput(first, sizeof(first)));
  assert(transport.nextLine() == Result::LineReady);
  assert(!strcmp(transport.line(), "{}"));
  const uint8_t rest[] = {'b', '\r', '\n', '\n'};
  assert(transport.enqueueInput(rest, sizeof(rest)));
  assert(transport.nextLine() == Result::LineReady);
  assert(!strcmp(transport.line(), "ab"));
  assert(transport.nextLine() == Result::None);

  const uint8_t nul[] = {'{', 0, '}', '\n', 'o', 'k', '\n'};
  assert(transport.enqueueInput(nul, sizeof(nul)));
  assert(transport.nextLine() == Result::InvalidLine);
  assert(transport.nextLine() == Result::LineReady);
  assert(!strcmp(transport.line(), "ok"));

  std::vector<uint8_t> oversized(V1BleTransport::kMaxLine + 2, 'x');
  oversized.push_back('\n');
  oversized.push_back('z');
  oversized.push_back('\n');
  assert(transport.enqueueInput(oversized.data(), oversized.size()));
  assert(transport.nextLine() == Result::LineTooLong);
  assert(transport.nextLine() == Result::LineReady);
  assert(!strcmp(transport.line(), "z"));

  std::vector<uint8_t> queueOverflow(V1BleTransport::kInputCapacity + 1, 'q');
  assert(!transport.enqueueInput(queueOverflow.data(), queueOverflow.size()));
  const uint8_t recovery[] = {'\n', 'r', '\n'};
  assert(transport.enqueueInput(recovery, sizeof(recovery)));
  assert(transport.nextLine() == Result::LineTooLong);
  assert(transport.nextLine() == Result::LineReady);
  assert(!strcmp(transport.line(), "r"));
  assert(transport.inputQueueDrops() == 1);
  assert(transport.inputHighWater() >= oversized.size());

  assert(transport.enqueueOutput("one"));
  assert(transport.enqueueOutput("two"));
  uint8_t output[8]{};
  assert(transport.peekOutput(output, 5) == 5);
  assert(std::string(reinterpret_cast<char*>(output), 5) == "one\nt");
  transport.consumeOutput(5);
  assert(transport.peekOutput(output, sizeof(output)) == 3);
  assert(std::string(reinterpret_cast<char*>(output), 3) == "wo\n");

  std::string tooLong(V1BleTransport::kMaxLine + 1, 'x');
  assert(!transport.enqueueOutput(tooLong.c_str()));
  assert(transport.outputQueueDrops() == 1);
  transport.reset();
  assert(transport.inputSize() == 0);
  assert(transport.outputSize() == 0);
}
