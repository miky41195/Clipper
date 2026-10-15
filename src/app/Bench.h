#pragma once

namespace clipper {

// Captures for `seconds` with the user's settings, saves one clip and reports CPU/RAM/save time.
// Report is printed to the parent console and written to %LOCALAPPDATA%\Clipper\bench.txt.
int runBench(int seconds);

}  // namespace clipper
