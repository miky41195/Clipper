#include <doctest/doctest.h>
#include "session/PacketBus.h"

using namespace clipper;

TEST_CASE("bus fans out to subscribers until they unsubscribe") {
  PacketBus bus;
  int a = 0, b = 0;
  const int ida = bus.subscribe([&](const Packet&) { ++a; });
  bus.subscribe([&](const Packet&) { ++b; });
  bus.publish(Packet{});
  bus.unsubscribe(ida);
  bus.publish(Packet{});
  CHECK(a == 1);
  CHECK(b == 2);
}
