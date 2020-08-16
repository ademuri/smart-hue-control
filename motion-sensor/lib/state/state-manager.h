#ifndef STATE_MANAGER_H_
#define STATE_MANAGER_H_

#include "state.h"

template <typename Context, typename EventType, EventType TimerEnum>
class StateManager {
  public:
    StateManager(State<Context, EventType>* current_state, Context* context);

    // Runs the state machine. Checks whether a timer has expired.
    void Run();

    void HandleEvent(EventType event);

    const char* CurrentStateName();

    Context* context();

  private:
    State<Context, EventType>* current_state_;
    Context* context_;
    uint32_t timer_ = 0;
};

template <typename Context, typename EventType, EventType TimerEnum>
StateManager<Context, EventType, TimerEnum>::StateManager(State<Context, EventType>* current_state, Context* context) : current_state_(current_state), context_(context) {
}

template <typename Context, typename EventType, EventType TimerEnum>
void StateManager<Context, EventType, TimerEnum>::Run() {
  if (timer_ > 0 && millis() > timer_) {
    HandleEvent(TimerEnum);
  }
}

template <typename Context, typename EventType, EventType TimerEnum>
void StateManager<Context, EventType, TimerEnum>::HandleEvent(EventType event) {
  auto it = current_state_->transitions.find(event);
  if (it == current_state_->transitions.end()) {
    // TODO: Invalid type
    Serial.printf("Event type not found: %d\n", static_cast<int>(event));
  } else {
    bool state_changed = it->second != State<Context, EventType>::NO_CHANGE;
    if (state_changed) {
      current_state_ = it->second;
      Serial.printf("Entering state: %s\n", current_state_->name);
    } else {
      Serial.printf("Resetting timer for state: %s\n", current_state_->name);
    }

    uint32_t new_timer_value = current_state_->on_enter(context_, state_changed);
    if (new_timer_value > 0) {
      timer_ = millis() + new_timer_value;
    } else {
      timer_ = 0;
    }
  }
}

template <typename Context, typename EventType, EventType TimerEnum>
const char* StateManager<Context, EventType, TimerEnum>::CurrentStateName() {
  return current_state_->name;
}

template <typename Context, typename EventType, EventType TimerEnum>
Context* StateManager<Context, EventType, TimerEnum>::context() {
  return context_;
}

#endif  // STATE_MANAGER_H_
