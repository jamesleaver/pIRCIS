// Ported from IRCIS -- "I Run Chars I See" -- by Arjun Nair (batman-nair):
//   https://github.com/batman-nair/IRCIS
//
// Copyright (c) 2019 Arjun Nair
// Licensed under the MIT License. See lib/ircis/LICENSE for the full text.
//
// Modified for pIRCIS by James Leaver: bounded memory, no iostream
// or filesystem, a seeded RNG and a ring-buffer trail, so the interpreter
// runs unchanged on an ESP32. Behaviour is deliberately byte-identical to
// the reference build.

#include "Runner.h"

#include <algorithm>
#include <cstdio>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace ircis {

  const char kBase64Chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"   // 26
    "abcdefghijklmnopqrstuvwxyz"   // 26
    "0123456789+/";                // 12
  static const std::size_t kBase64Len = 64;

  const char* mode_end_chars(Mode mode) {
    switch (mode) {
    case NONE:       return "";
    case STACK:      return "\"";                  // CH_STACK
    case STACK_PUSH: return ". ";                  // CH_DOT, CH_SPC
    case STACK_POP:  return ". ";                  // CH_DOT, CH_SPC
    }
    return "";
  }

  bool is_mode_end_char(Mode mode, char current_char) {
    const char* ends = mode_end_chars(mode);
    if (!*ends) return false;
    return std::strchr(ends, current_char) != nullptr;
  }

  bool isbase64(char current_char) {
    if (current_char == '\0') return false;
    return std::strchr(kBase64Chars, current_char) != nullptr;
  }

  int base64_decode_int(std::string input) {
    // Remove leading As
    std::size_t first = input.find_first_not_of('A');
    std::size_t last = input.empty() ? 0 : input.size() - 1;
    input.erase(0, first < last ? first : last);
    std::string str = input;
    if (str.empty()) return 0;
    int pos_val = 1;
    int result = 0;
    for (auto strpos = str.rbegin(); strpos < str.rend(); strpos++) {
      const char* found = std::strchr(kBase64Chars, *strpos);
      if (!found || *strpos == '\0') return 0;
      result += pos_val * static_cast<int>(found - kBase64Chars);
      pos_val *= 64;
    }
    return result;
  }

  std::string base64_encode_int(int value) {
    std::string result;
    int pos_value;
    // NB: in stock IRCIS this reads `(value >> 30) > base64_chars.length()`.
    // length() is unsigned, so a negative shift result converts to a huge
    // unsigned value and the comparison is true -- that is how negative
    // numbers get their '-' sign. Preserved deliberately, cast made explicit.
    if (static_cast<std::size_t>(value >> 30) > kBase64Len) {
      result.push_back('-');
      pos_value = -value;
    }
    else {
      pos_value = value;
    }
    result.push_back(kBase64Chars[pos_value >> 30]);
    result.push_back(kBase64Chars[(pos_value & 1056964608) >> 24]);
    result.push_back(kBase64Chars[(pos_value & 16515072) >> 18]);
    result.push_back(kBase64Chars[(pos_value & 258048) >> 12]);
    result.push_back(kBase64Chars[(pos_value & 4032) >> 6]);
    result.push_back(kBase64Chars[(pos_value & 63)]);
    std::size_t first = result.find_first_not_of('A');
    std::size_t last = result.size() - 1;
    return result.erase(0, first < last ? first : last);
  }

  // Step function processes the current char for Runner and moves it a step.
  // Returns false if Runner is dead.
  bool Runner::step() {
    trail_.push(position_);
    ++steps_taken_;
    if (pause_time_) {
      log_line("Pausing. Pause time ", pause_time_);
      return pause_time_--;
    }

    int current_char = grid_->get(position_.get_x(), position_.get_y());
#if IRCIS_TRACK_PROCESSED_CHARS
    processed_chars_.push_back(static_cast<char>(current_char));
#endif

    // --- Integer mode related processing ---
    if (integer_mode_ || current_char == CH_INT) {
      if (integer_mode_ && current_char == CH_INT) {
        set_error("Quote character is invalid in integer mode");
        return false;
      }

      integer_mode_ = true;
      if (!is_blank(current_char))
        integer_mode_buffer_.push_back(current_char);
      else {
        if (!process_integer_buffer())
          return false;
      }
    }
    // --- Mode processing ---
    else if (mode_ != Mode::NONE) {
      if (!is_mode_end_char(mode_, current_char))
        mode_buffer_.push_back(current_char);
      else {
        if (!process_mode_buffer())
          return false;
      }
    }
    // --- Any other char processing ---
    else if (!process_char(current_char)) {
      return false;
    }

    // --- Update Runner position after processing ---
    position_.update();
    if (!grid_->is_inside(position_)) {
      set_error("Runner went outside grid");
      return false;
    }

    return true;
  }

  bool Runner::process_char(char current_char) {
    switch (current_char) {
    case CH_STACK:
      mode_ = Mode::STACK;
      stack_mode_ = !stack_mode_;
      break;
    case CH_WEST:
      position_.change_dir(Direction::WEST);
      break;
    case CH_NORTH:
      position_.change_dir(Direction::NORTH);
      break;
    case CH_EAST:
      position_.change_dir(Direction::EAST);
      break;
    case CH_SOUTH:
      position_.change_dir(Direction::SOUTH);
      break;
    case CH_PUSH:
      mode_ = Mode::STACK_PUSH;
      break;
    case CH_POP:
      mode_ = Mode::STACK_POP;
      break;
    case CH_ENDL:
      log_->print_line();
      break;
    case CH_SPLIT:
      if (!process_split()) {
        return false;
      }
      break;
    case CH_CHECK:
      {
        if (st_.top().value) {
          log_line("Condition check true");
          break;
        }
        log_line("Condition check false");
        // Check if char exists in right or left and go there
        auto templ = position_;
        templ.move(position_.get_left());
        if (grid_->is_inside(templ) && !is_blank(grid_->get(templ))) {
          position_.change_dir(position_.get_left());
          break;
        }
        auto tempr = position_;
        tempr.move(position_.get_right());
        if (grid_->is_inside(tempr) && !is_blank(grid_->get(tempr))) {
          position_.change_dir(position_.get_right());
          break;
        }
        set_error("False direction for check not found.");
        return false;
      }
    case CH_RAND_INT:
      {
        if (st_.empty()) {
          set_error("Empty stack when looking for random limit");
          return false;
        }
        Data dat = st_.top();
        if (!dat.is_integer or dat.value <= 0) {
          set_error("Random limit must be an integer greater than 0");
          return false;
        }
        st_.pop();
        push_random_number_to_stack(dat.value);
        break;
      }
    case CH_RAND:
      push_random_number_to_stack(1);
      break;
    case CH_PAUSE:
      {
        if (st_.empty()) {
          set_error("Empty stack when looking for pause time");
          return false;
        }
        Data dat = st_.top();
        if (!dat.is_integer or dat.value < 0) {
          set_error("Pause time must be an integer greater than or equal to 0");
          return false;
        }
        st_.pop();
        pause_time_ = dat.value;
        break;
      }
    case CH_END:
      set_error("End character reached");
      return false;
    case CH_PRINT:
      {
        if (st_.empty()) {
          log_->print_line();
          set_error("Stack is empty, forcing exit.");
          return false;
        }
        auto top = st_.top();
        log_line("Printing and popping: ", st_.top());
        st_.pop();
        log_->print(top.to_string());
        break;
      }
    case CH_PRINT_BASE64:
      {
        if (st_.empty()) {
          log_->print_line();
          set_error("Stack is empty, forcing exit.");
          return false;
        }
        auto top = st_.top();
        log_line("Printing and popping as Base 64: ", st_.top());
        st_.pop();
        if (top.is_integer) {
          log_->print(base64_encode_int(top.value));
        }
        else {
          log_->print(top.to_string());
        }
        break;
      }
    default:
      if (!is_blank(current_char)) {
        err_line("Character could not be processed: ", static_cast<char>(current_char));
      }
    }
    return true;
  }

  // if the first and second character of the buffer are both valid base64
  // characters, don't treat it as arith (even if the first chars are / or +)
  static bool is_not_arith(char first_char, char second_char) {
    return (!is_arith(first_char)) || (isbase64(first_char) && isbase64(second_char));
  }

  bool Runner::process_integer_buffer() {
    auto it = integer_mode_buffer_.begin();
    ++it;                       // Skip starting quote char
    char start_ch = *it;
    ++it;
    char second_ch = *it;

    if (isbase64(start_ch) && is_not_arith(start_ch, second_ch)) {  // Integer processing
      std::string buffer;
      buffer.push_back(start_ch);
      if (!isdigit(start_ch) && isbase64(start_ch)) {
        base64_mode = true;
      }
      while (it != integer_mode_buffer_.end()) {
        char ch = *it++;
        if (!isdigit(ch) && isbase64(ch)) {
          base64_mode = true;
        }
        if (!isbase64(ch)) {
          set_error("Non integer character in integer processing.");
          return false;
        }
        buffer.push_back(ch);
      }
      int num;
      if (base64_mode) {
        num = base64_decode_int(buffer);
      }
      else {
        num = std::atoi(buffer.c_str());
      }
      Data dat(num, true);
      log_line("Pushing value to stack ", dat);
      st_.push(dat);
    }
    else {
      if (is_arith(start_ch)) {
        if (st_.size() < 2) {
          set_error("Not enough elements for arithmetic operation");
          return false;
        }
        Data num1 = st_.top();
        log_line("Stack value popped ", st_.top());
        st_.pop();
        Data num2 = st_.top();
        log_line("Stack value popped ", st_.top());
        st_.pop();
        if (num1.is_integer && num2.is_integer) {
          switch (start_ch) {
            case CH_ADD: log_line("Arith: ", num1, " + ", num2); num1 = num1 + num2; break;
            case CH_SUB: log_line("Arith: ", num1, " - ", num2); num1 = num1 - num2; break;
            case CH_MUL: log_line("Arith: ", num1, " * ", num2); num1 = num1 * num2; break;
            case CH_DIV:
              if (num2.value == 0) {
                set_error("Division by zero error");
                return false;
              }
              log_line("Arith: ", num1, " / ", num2); num1 = num1 / num2; break;
            case CH_MOD: log_line("Arith: ", num1, " % ", num2); num1 = num1 % num2; break;
            case CH_POW: log_line("Arith: ", num1, " ^ ", num2); num1 = num1 ^ num2; break;
            case CH_AND: log_line("Arith: ", num1, " & ", num2); num1 = num1 & num2; break;
            case CH_OR:  log_line("Arith: ", num1, " | ", num2); num1 = num1 | num2; break;
            case CH_XOR: log_line("Arith: ", num1, " V ", num2); num1 = num1.V(num2); break;
            case CH_BL:  log_line("Arith: ", num1, " < ", num2); num1 = num1 < num2; break;
            case CH_BR:  log_line("Arith: ", num1, " > ", num2); num1 = num1 > num2; break;
            default:
              set_error("Unknown arithmetic op found: ", start_ch);
              return false;
          };
          log_line("Pushing value to stack ", num1);
          st_.push(num1);
        }
      }
    }

    integer_mode_ = false;
    base64_mode = false;
    integer_mode_buffer_.clear();
    return true;
  }

  bool Runner::process_mode_buffer() {
    switch (mode_) {
    case NONE:
      set_error("Shouldn't reach process_mode_buffer with NONE mode");
      return false;
    case STACK:
      {
        for (char ch : mode_buffer_) {
          Data data(ch);
          log_line("Pushing value to stack ", data);
          st_.push(data);
        }
        break;
      }
    case STACK_PUSH:
      {
        if (islower(mode_buffer_[0])) {
          if (!process_local_var_fetch()) return false;
        }
        else if (isupper(mode_buffer_[0])) {
          if (!process_global_var_fetch()) return false;
        }
        else {
          if (!process_stack_push()) return false;
        }
        break;
      }
    case STACK_POP:
      {
        if (islower(mode_buffer_[0])) {
          if (!process_local_var_insert()) return false;
        }
        else if (isupper(mode_buffer_[0])) {
          if (!process_global_var_insert()) return false;
        }
        else {
          if (!process_stack_pop()) return false;
        }
        break;
      }
    default:
      set_error("Unknown mode_ value specified. mode_: ", mode_);
      return false;
    }
    mode_buffer_.clear();
    mode_ = Mode::NONE;
    return true;
  }

  static char get_direction_char(Direction dir) {
    switch (dir) {
    case Direction::NORTH: return CH_NORTH;
    case Direction::EAST:  return CH_EAST;
    case Direction::WEST:  return CH_WEST;
    case Direction::SOUTH: return CH_SOUTH;
    }
    Logger::err_line("Got invalid Direction!");
    return CH_NORTH;
  }

  bool Runner::process_split() {
    DirVec curr_position = position_;
    bool create_runner = false;
    auto process_split_for_direction =
      [this, &create_runner, &curr_position] (Direction dir) {
        DirVec temp = curr_position;
        temp.move(dir);
        if (grid_->is_inside(temp) && grid_->get(temp) == get_direction_char(dir)) {
          if (!create_runner) {
            position_.change_dir(dir);
            create_runner = true;
          }
          else {
            new_runners_list_->push({temp, st_, var_map_, trail_});
          }
        }
      };

    for (Direction dir : {Direction::NORTH, Direction::EAST, Direction::SOUTH, Direction::WEST}) {
      process_split_for_direction(dir);
    }
    return true;
  }

  bool Runner::process_stack_push() {
    int num = 0;
    for (char ch : mode_buffer_) {
      if (!isdigit(ch)) {
        set_error("Invalid character in Stack Push mode!");
        return false;
      }
      num = num * 10 + (ch - '0');
    }
    Data temp = st_[-num];
    log_line("Pushing value to stack ", temp);
    st_.push(temp);
    return true;
  }

  bool Runner::process_stack_pop() {
    int num = 0;
    for (char ch : mode_buffer_) {
      if (!isdigit(ch)) {
        set_error("Invalid character in Stack Pop mode!");
        return false;
      }
      num = num * 10 + (ch - '0');
    }
    for (int ii = 0; ii < num; ++ii) {
      if (st_.empty()) {
        log_line("Stack pop preemptive finish");
        return true;
      }
      log_line("Stack value popped ", st_.top());
      st_.pop();
    }
    return true;
  }

  bool Runner::process_global_var_fetch() {
    std::string var(mode_buffer_);
    if (std::find_if_not(var.begin(), var.end(), ::isalpha) != var.end()) {
      set_error("Variable name '", var, "' should contain only alphabets");
      return false;
    }
    if (global_var_map_->find(var) == global_var_map_->end()) {
      set_error("Couldn't find global variable ", var);
      return false;
    }
    Data temp = (*global_var_map_)[var];
    log_line("Pushing variable(", var, ") value to stack ", temp);
    st_.push(temp);
    return true;
  }

  bool Runner::process_global_var_insert() {
    std::string var(mode_buffer_);
    if (std::find_if_not(var.begin(), var.end(), ::isalpha) != var.end()) {
      set_error("Variable name '", var, "' should contain only alphabets");
      return false;
    }
    log_line("Saving value ", st_.top(), " to variable ", var);
    (*global_var_map_)[var] = st_.top();
    return true;
  }

  bool Runner::process_local_var_fetch() {
    std::string var(mode_buffer_);
    if (std::find_if_not(var.begin(), var.end(), ::isalpha) != var.end()) {
      set_error("Variable name '", var, "' should contain only alphabets");
      return false;
    }
    if (var_map_.find(var) == var_map_.end()) {
      set_error("Couldn't find local variable ", var);
      return false;
    }
    Data temp = var_map_[var];
    log_line("Pushing variable(", var, ") value to stack ", temp);
    st_.push(temp);
    return true;
  }

  bool Runner::process_local_var_insert() {
    std::string var(mode_buffer_);
    if (std::find_if_not(var.begin(), var.end(), ::isalpha) != var.end()) {
      set_error("Variable name '", var, "' should contain only alphabets");
      return false;
    }
    log_line("Saving value ", st_.top(), " to variable ", var);
    var_map_[var] = st_.top();
    return true;
  }

  void Runner::describe(char* out, std::size_t n) const {
    out[0] = 0;
    if (pause_time_) { std::snprintf(out, n, "pause %d", pause_time_); return; }
    if (!grid_->is_inside(position_)) return;
    const char c = grid_->get(position_);
    // A stack read that would step past the bottom answers 0 in step(); the
    // same here, without touching the counter that says it happened.
    auto at = [this](std::size_t k) { return k < st_.size() ? st_.from_top(k) : Data(); };

    // --- a number being read, and the blank that ends it ---
    if (integer_mode_ || c == CH_INT) {
      if (integer_mode_ && c == CH_INT) { std::snprintf(out, n, "quote in int mode"); return; }
      if (!integer_mode_) { std::snprintf(out, n, "int mode on"); return; }
      if (!is_blank(c)) { std::snprintf(out, n, "int %s%c", integer_mode_buffer_.c_str() + 1, c); return; }
      const std::string& b = integer_mode_buffer_;      // starts with the quote
      if (b.size() < 2) { std::snprintf(out, n, "int ends"); return; }
      const char start = b[1], second = b.size() > 2 ? b[2] : '\0';
      if (isbase64(start) && (!is_arith(start) || isbase64(second))) {
        bool b64 = false;
        for (std::size_t i = 1; i < b.size(); ++i) {
          if (!isbase64(b[i])) { std::snprintf(out, n, "not a number"); return; }
          if (!std::isdigit((unsigned char)b[i])) b64 = true;
        }
        const int num = b64 ? base64_decode_int(b.substr(1)) : std::atoi(b.c_str() + 1);
        std::snprintf(out, n, "push %d", num);
        return;
      }
      if (is_arith(start)) {
        if (st_.size() < 2) { std::snprintf(out, n, "arith needs 2"); return; }
        const Data a = at(0), bb = at(1);
        if (!a.is_integer || !bb.is_integer) { std::snprintf(out, n, "no arith on chars"); return; }
        Data r;
        switch (start) {
          case CH_ADD: r = a + bb; break;
          case CH_SUB: r = a - bb; break;
          case CH_MUL: r = a * bb; break;
          case CH_DIV:
            if (bb.value == 0) { std::snprintf(out, n, "divide by zero"); return; }
            r = a / bb; break;
          case CH_MOD:
            if (bb.value == 0) { std::snprintf(out, n, "divide by zero"); return; }
            r = a % bb; break;
          case CH_POW: r = a ^ bb; break;
          case CH_AND: r = a & bb; break;
          case CH_OR:  r = a | bb; break;
          case CH_XOR: r = a.V(bb); break;
          case CH_BL:  r = a < bb; break;
          case CH_BR:  r = a > bb; break;
          default: std::snprintf(out, n, "unknown op %c", start); return;
        }
        std::snprintf(out, n, "%d%c%d=%d", a.value, start, bb.value, r.value);
        return;
      }
      std::snprintf(out, n, "int ends");
      return;
    }

    // --- a string, a variable name or a count being read, and its end ---
    if (mode_ != Mode::NONE) {
      const char pre = mode_ == Mode::STACK ? CH_STACK : mode_ == Mode::STACK_PUSH ? CH_PUSH : CH_POP;
      const std::string& m = mode_buffer_;
      if (!is_mode_end_char(mode_, c)) { std::snprintf(out, n, "%c%s%c", pre, m.c_str(), c); return; }
      if (mode_ == Mode::STACK) { std::snprintf(out, n, "push \"%s\"", m.c_str()); return; }
      const bool lower = !m.empty() && std::islower((unsigned char)m[0]);
      const bool upper = !m.empty() && std::isupper((unsigned char)m[0]);
      if (mode_ == Mode::STACK_PUSH) {
        if (lower || upper) {
          const auto& map = lower ? var_map_ : *global_var_map_;
          const auto it = map.find(m);
          if (it == map.end()) std::snprintf(out, n, "no %s %s", lower ? "local" : "global", m.c_str());
          else std::snprintf(out, n, "push %s=%s", m.c_str(), it->second.to_string().c_str());
          return;
        }
        int num = 0;
        for (char ch : m) {
          if (!std::isdigit((unsigned char)ch)) { std::snprintf(out, n, "bad push %s", m.c_str()); return; }
          num = num * 10 + (ch - '0');
        }
        std::snprintf(out, n, "push @%d=%s", num, at((std::size_t)num).to_string().c_str());
        return;
      }
      // STACK_POP
      if (lower || upper) { std::snprintf(out, n, "save %s=%s", m.c_str(), at(0).to_string().c_str()); return; }
      int num = 0;
      for (char ch : m) {
        if (!std::isdigit((unsigned char)ch)) { std::snprintf(out, n, "bad pop %s", m.c_str()); return; }
        num = num * 10 + (ch - '0');
      }
      std::string popped;
      for (int i = 0; i < num && (std::size_t)i < st_.size(); ++i) popped += " " + st_.from_top((std::size_t)i).to_string();
      std::snprintf(out, n, "pop%s", popped.empty() ? " nothing" : popped.c_str());
      return;
    }

    // --- a command on its own ---
    switch (c) {
      case CH_STACK: std::snprintf(out, n, "stack mode on"); return;
      case CH_WEST:  std::snprintf(out, n, "turn west");  return;
      case CH_NORTH: std::snprintf(out, n, "turn north"); return;
      case CH_EAST:  std::snprintf(out, n, "turn east");  return;
      case CH_SOUTH: std::snprintf(out, n, "turn south"); return;
      case CH_PUSH:  std::snprintf(out, n, "push mode"); return;
      case CH_POP:   std::snprintf(out, n, "pop mode");  return;
      case CH_ENDL:  std::snprintf(out, n, "newline"); return;
      case CH_SPLIT: std::snprintf(out, n, "split"); return;
      case CH_CHECK: std::snprintf(out, n, at(0).value ? "check true" : "check false"); return;
      case CH_RAND_INT:
        if (st_.empty()) std::snprintf(out, n, "rand needs a limit");
        else std::snprintf(out, n, "rand 0..%s", at(0).to_string().c_str());
        return;
      case CH_RAND:  std::snprintf(out, n, "rand 0 or 1"); return;
      case CH_PAUSE:
        if (st_.empty()) std::snprintf(out, n, "pause needs a time");
        else std::snprintf(out, n, "pause %s", at(0).to_string().c_str());
        return;
      case CH_END:   std::snprintf(out, n, "end"); return;
      case CH_PRINT:
        if (st_.empty()) std::snprintf(out, n, "print, stack empty");
        else std::snprintf(out, n, "print %s", at(0).to_string().c_str());
        return;
      case CH_PRINT_BASE64:
        if (st_.empty()) std::snprintf(out, n, "print, stack empty");
        else if (at(0).is_integer)
          std::snprintf(out, n, "print %s as %s", at(0).to_string().c_str(), base64_encode_int(at(0).value).c_str());
        else std::snprintf(out, n, "print %s", at(0).to_string().c_str());
        return;
      default: return;      // a blank, or a character IRCIS steps over
    }
  }

  void Runner::push_random_number_to_stack(int limit) {
    // Uniform in [0, limit] inclusive, the guarantee uniform_int_distribution
    // gives upstream. A bare modulo would favour the low residues, since 2^32
    // rarely divides the span; draws landing in that short tail are discarded
    // and taken again. IRCIS_TRACK: std::uniform_int_distribution(0, limit).
    const uint32_t span = static_cast<uint32_t>(limit) + 1u;
    const uint32_t tail = (0u - span) % span;   // == 2^32 mod span
    uint32_t v;
    do { v = rng_->next(); } while (v < tail);
    Data dat(static_cast<int>(v % span), true);
    log_line("Pushing random number to stack ", dat);
    st_.push(dat);
  }
}
