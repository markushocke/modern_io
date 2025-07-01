// modern_io.ixx
module;
#include <cstddef>
#include <concepts>
#include <string>
#include <vector>
#include <stdexcept>
#include <fstream>
#include <stdint.h>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>
#include <utility>
#include <limits>
#include <bit>

export module modern_io;

export import :concepts;
export import :file;
export import :data;
export import :buffered;