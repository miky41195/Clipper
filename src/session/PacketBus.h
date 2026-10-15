#pragma once
#include "core/Media.h"
#include <functional>
#include <map>
#include <mutex>

namespace clipper {

// Fan-out of encoded packets to live listeners (the manual recorder).
class PacketBus {
public:
  using Listener = std::function<void(const Packet&)>;
  int subscribe(Listener l);
  void unsubscribe(int id);
  void publish(const Packet& p);

private:
  std::mutex mu_;
  std::map<int, Listener> listeners_;
  int next_ = 1;
};

}  // namespace clipper
