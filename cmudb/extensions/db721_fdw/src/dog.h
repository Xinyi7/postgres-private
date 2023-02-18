#pragma once

// clang-format off
extern "C" {
#include "../../../../src/include/postgres.h"
}
// TODO(WAN): Hack.
//  Because PostgreSQL tries to be portable, it makes a bunch of global
//  definitions that can make your C++ libraries very sad.
//  We're just going to undefine those.
#undef vsnprintf
#undef snprintf
#undef vsprintf
#undef sprintf
#undef vfprintf
#undef fprintf
#undef vprintf
#undef printf
#undef gettext
#undef dgettext
#undef ngettext
#undef dngettext
// clang-format on

#include <string>

class Dog {
public:
  explicit Dog(std::string name);
  std::string Bark();

private:
  std::string name_;
};