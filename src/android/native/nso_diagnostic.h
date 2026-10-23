#ifndef FFRS_NSO_DIAGNOSTIC_H
#define FFRS_NSO_DIAGNOSTIC_H

#include <string>

namespace ffrs {
// Loads and releases one module, reporting facts without running game code.
std::string DiagnoseNso(const std::string& module_path);
}
#endif
