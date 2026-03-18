// modern_io.ixx
module;
#ifndef _MSC_VER
#include <cstddef>
#include <concepts>
#include <string>
#include <vector>
#include <stdexcept>
#include <fstream>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>
#include <utility>
#include <limits>
#include <bit>
#include <future>
#endif




export module modern_io;

#ifdef _MSC_VER
import <cstddef>;
import <concepts>;
import <string>;
import <vector>;
import <stdexcept>;
import <fstream>;
import <cstdint>;
import <cstring>;
import <span>;
import <type_traits>;
import <utility>;
import <limits>;
import <bit>;
import <future>;
#endif

export import modern_io.exceptions;
export import modern_io.concepts;
export import modern_io.file;
export import modern_io.data;
export import modern_io.buffered;
export import modern_io.iostream;