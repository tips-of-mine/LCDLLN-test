/**
 * @file FileSystem.cpp
 * @brief File system implementation. Read, exists, list, path join; paths relative to content root.
 */

#include "engine/platform/FileSystem.h"
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace engine::platform {

namespace {
std::string g_contentRoot;
}

void FileSystem::SetContentRoot(std::string rootPath) {
    g_contentRoot = std::move(rootPath);
    while (!g_contentRoot.empty() && (g_contentRoot.back() == '/' || g_contentRoot.back() == '\\'))
        g_contentRoot.pop_back();
}

const std::string& FileSystem::GetContentRoot() {
    return g_contentRoot;
}

std::string FileSystem::PathJoin(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    char sep = '/';
#ifdef _WIN32
    sep = '\\';
#endif
    std::string r = a;
    if (r.back() != '/' && r.back() != '\\') r += sep;
    if (b.front() == '/' || b.front() == '\\') r += b.substr(1);
    else r += b;
    return r;
}

static std::string ResolvePath(const std::string& path) {
    if (path.empty()) return g_contentRoot;
#ifdef _WIN32
    if (path.size() >= 2 && path[1] == ':') return path;
    if (path.size() >= 1 && (path[0] == '/' || path[0] == '\\')) return path;
#else
    if (path.size() >= 1 && path[0] == '/') return path;
#endif
    return FileSystem::PathJoin(g_contentRoot, path);
}

std::vector<uint8_t> FileSystem::ReadAllBytes(const std::string& path) {
    std::string full = ResolvePath(path);
    std::ifstream f(full, std::ios::binary | std::ios::ate);
    if (!f) return {};
    auto size = f.tellg();
    if (size <= 0) return {};
    f.seekg(0);
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    if (!f.read(reinterpret_cast<char*>(buf.data()), size)) return {};
    return buf;
}

std::string FileSystem::ReadAllText(const std::string& path) {
    std::string full = ResolvePath(path);
    std::ifstream f(full);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool FileSystem::Exists(const std::string& path) {
    std::string full = ResolvePath(path);
#ifdef _WIN32
    DWORD att = GetFileAttributesA(full.c_str());
    return att != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st = {};
    return stat(full.c_str(), &st) == 0;
#endif
}

std::vector<std::string> FileSystem::List(const std::string& path) {
    std::string full = ResolvePath(path);
    std::vector<std::string> out;
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    std::string pattern = full;
    if (pattern.empty() || (pattern.back() != '/' && pattern.back() != '\\')) pattern += "\\*";
    else pattern += "*";
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (fd.cFileName[0] == '.' && (fd.cFileName[1] == '\0' || (fd.cFileName[1] == '.' && fd.cFileName[2] == '\0')))
            continue;
        out.push_back(fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR* d = opendir(full.c_str());
    if (!d) return out;
    while (struct dirent* e = readdir(d)) {
        if (e->d_name[0] == '.' && (e->d_name[1] == '\0' || (e->d_name[1] == '.' && e->d_name[2] == '\0')))
            continue;
        out.push_back(e->d_name);
    }
    closedir(d);
#endif
    return out;
}

} // namespace engine::platform
