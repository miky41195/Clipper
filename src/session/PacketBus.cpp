#include "session/PacketBus.h"

namespace clipper {

int PacketBus::subscribe(Listener l) {
  std::lock_guard lk(mu_);
  listeners_[next_] = std::move(l);
  return next_++;
}

void PacketBus::unsubscribe(int id) {
  std::lock_guard lk(mu_);
  listeners_.erase(id);
}

void PacketBus::publish(const Packet& p) {
  std::lock_guard lk(mu_);
  for (auto& [id, l] : listeners_) l(p);
}

}  // namespace clipper
