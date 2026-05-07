// M(rddads-compat): stubs that used to live here are now consolidated
// into src/abi/ace_exports.cpp at the bottom of the file (search for
// "M(rddads-compat)"). The new versions match the declarations in
// include/openads/ace.h exactly and use UNSIGNED32 / UNSIGNED16 plus
// extern "C" linkage, so Harbour's contrib/rddads links cleanly without
// the type-coercion warnings the older `uint32_t Foo()` empty-paren
// stubs produced.
//
// This file is intentionally empty; CMake still includes it so the
// historic translation-unit ordering (and any future per-file flag
// overrides) stays untouched.
