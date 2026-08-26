#include "path_resolver.h"
#include <cstdlib>
#include <cstring>

PathResolver& PathResolver::getInstance() {
    static PathResolver instance;
    return instance;
}

void PathResolver::init(const char* rootfsBase) {
    if (rootfsBase && strlen(rootfsBase) > 0) {
        mRootfsBase = rootfsBase;
    } else {
        const char* envRoot = getenv("AOSP_ROOTFS_DIR");
        mRootfsBase = envRoot ? envRoot : "/data/user/0/dev.itzkaguya.aospcontainer/files/rootfs";
    }
    if (!mRootfsBase.empty() && mRootfsBase.back() == '/') mRootfsBase.pop_back();

    mRedirectPrefixes = {
        {"/system", mRootfsBase + "/system"},
        {"/vendor", mRootfsBase + "/vendor"},
        {"/apex",   mRootfsBase + "/apex"},
        {"/data",   mRootfsBase + "/data"},
        {"/etc",    mRootfsBase + "/system/etc"},
        {"/bin",    mRootfsBase + "/system/bin"},
        {"/xbin",   mRootfsBase + "/system/xbin"},
        {"/storage",mRootfsBase + "/storage"}
    };
    mInitialized = true;
}

bool PathResolver::resolvePath(const char* originalPath, char* resolvedBuffer, size_t bufferSize) {
    if (!originalPath || originalPath[0] == '\0' || !resolvedBuffer || bufferSize == 0) return false;
    if (!mInitialized) init(nullptr);

    if (strncmp(originalPath, mRootfsBase.c_str(), mRootfsBase.length()) == 0) {
        strncpy(resolvedBuffer, originalPath, bufferSize - 1);
        resolvedBuffer[bufferSize - 1] = '\0';
        return false;
    }

    for (const auto& [prefix, target] : mRedirectPrefixes) {
        size_t len = prefix.length();
        if (strncmp(originalPath, prefix.c_str(), len) == 0 && (originalPath[len] == '/' || originalPath[len] == '\0')) {
            std::string redirected = target + (originalPath + len);
            strncpy(resolvedBuffer, redirected.c_str(), bufferSize - 1);
            resolvedBuffer[bufferSize - 1] = '\0';
            return true;
        }
    }
    strncpy(resolvedBuffer, originalPath, bufferSize - 1);
    resolvedBuffer[bufferSize - 1] = '\0';
    return false;
}
