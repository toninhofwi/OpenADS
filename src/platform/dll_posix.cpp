#include "platform/dll.h"

#ifndef _WIN32

#include <dlfcn.h>

namespace openads::platform {

util::Result<DllHandle> dll_load(const std::string& path) {
    void* m = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!m) {
        const char* err = dlerror();
        return util::Error{5000, 0,
                           err ? err : "dlopen failed", path};
    }
    DllHandle h;
    h.native = m;
    return h;
}

util::Result<void*> dll_symbol(DllHandle h, const std::string& name) {
    if (!dll_valid(h)) {
        return util::Error{5000, 0, "invalid DLL handle", name};
    }
    dlerror();
    void* p = dlsym(h.native, name.c_str());
    const char* err = dlerror();
    if (err) {
        return util::Error{5000, 0, err, name};
    }
    return p;
}

void dll_close(DllHandle h) noexcept {
    if (h.native) dlclose(h.native);
}

std::string dll_probe_ace(const std::string& path) noexcept {
    void* m = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!m) return {};
    using pfnGetVer = unsigned int(*)(
        unsigned int*, unsigned int*,
        unsigned char*, unsigned char*, unsigned short*);
    auto* fn = reinterpret_cast<pfnGetVer>(dlsym(m, "AdsGetVersion"));
    std::string result;
    if (fn) {
        unsigned char desc[256] = {};
        unsigned short len = static_cast<unsigned short>(sizeof(desc) - 1);
        fn(nullptr, nullptr, nullptr, desc, &len);
        result.assign(reinterpret_cast<char*>(desc),
                      static_cast<std::size_t>(len));
    }
    dlclose(m);
    if (result.find("OpenADS") == std::string::npos) return {};
    return result;
}

} // namespace openads::platform

#endif // !_WIN32
