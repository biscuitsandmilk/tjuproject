#include "util.h"

double max(double a, double b) {
  return b < a ? a : b;
}

double min(double a, double b) {
  return b > a ? a : b;
}
