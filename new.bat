rmdir /S /Q build
cmake -A Win32 -T v143 -DOCPN_TARGET=MSVC -S . -B build
cmake --build build --config RelWithDebInfo > OUTPUT.txt
