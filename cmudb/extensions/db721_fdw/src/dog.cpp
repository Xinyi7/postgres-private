#include "dog.h"

Dog::Dog(std::string name) : name_(name) {}

std::string Dog::Bark() { return name_ + ": Woof!"; }