#pragma once
#include <string>
#include <vector>

class PathResolver {
public:
    static PathResolver& getInstance();
    void init(const char* rootfsBase);
    bool resolvePath(const char* originalPath, char* resolvedBuffer, size_t bufferSize);
    /* True once init() has populated the prefix table. Guards against
     * lazy env-based initialization leaking into host-side processes. */
    bool isReady() const { return mInitialized; }
private:
    PathResolver() = default;
    std::string mRootfsBase;
    std::vector<std::pair<std::string, std::string>> mRedirectPrefixes;
    bool mInitialized{false};
};
