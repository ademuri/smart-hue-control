#ifndef STATE_H_
#define STATE_H_

#include <functional>
#include <map>

template <typename Context, typename EventType>
struct State {
  State(const char* name, std::map<EventType, State<Context, EventType>*> transitions, std::function<uint32_t(Context*, bool)> on_enter) : name(name), transitions(transitions), on_enter(on_enter) {}

  const char* name;

  std::map<EventType, State<Context, EventType>*> transitions;

  // Function to run when entering this state. Returns a timer to set, in
  // milliseconds, or 0 if none should be set.
  std::function<uint32_t(Context*, bool state_changed)> on_enter;

  // Sentinel state that indicates not to change state, and not to re-run
  // on_enter code. Does, however, reset timers.
  static constexpr State<Context, EventType>* NO_CHANGE = nullptr;
};

#endif  // STATE_H_
