// Loads the measured instrument profiles compiled into the binary.
#pragma once

#include <QStringList>

namespace profiles {

// Attaches every profile the catalog asks for. Returns the ids that could
// not be loaded, so the window can say which instruments will be silent.
QStringList load();

} // namespace profiles
