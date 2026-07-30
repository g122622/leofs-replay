#include "trace_replay/core/SyscallClassify.hpp"

#include <array>
#include <string_view>

namespace trace_replay {
namespace {

// Faithfully ported from rawproc's PATH_OPS (see context.md lines 102-107)
constexpr std::array PATH_OPS = {
    std::string_view{"newfstatat"}, std::string_view{"statx"},
    std::string_view{"stat"},       std::string_view{"lstat"},
    std::string_view{"openat"},     std::string_view{"openat2"},
    std::string_view{"open"},       std::string_view{"getdents64"},
    std::string_view{"getdents"},   std::string_view{"mkdir"},
    std::string_view{"mkdirat"},    std::string_view{"rmdir"},
    std::string_view{"renameat"},   std::string_view{"renameat2"},
    std::string_view{"utimensat"},
    std::string_view{"setxattr"},   std::string_view{"lsetxattr"},
    std::string_view{"getxattr"},   std::string_view{"lgetxattr"},
    std::string_view{"listxattr"},  std::string_view{"llistxattr"},
    std::string_view{"unlink"},     std::string_view{"unlinkat"},
    std::string_view{"linkat"},     std::string_view{"symlinkat"},
};

// Faithfully ported from rawproc's IO_OPS (see context.md line 108)
constexpr std::array IO_OPS = {
    std::string_view{"read"},   std::string_view{"write"},
    std::string_view{"pread64"}, std::string_view{"pwrite64"},
    std::string_view{"readv"},  std::string_view{"writev"},
};

// Faithfully ported from rawproc's RENAME_SC (see context.md line 116)
constexpr std::array RENAME_SC = {
    std::string_view{"renameat"}, std::string_view{"renameat2"},
    std::string_view{"rename"},
};

// Operations that need to really open a file and register a mapping in the fd
// table.
constexpr std::array OPEN_SC = {
    std::string_view{"openat"}, std::string_view{"openat2"},
    std::string_view{"open"},
};

template <std::size_t N>
bool contains(const std::array<std::string_view, N>& table, std::string_view sc) noexcept
{
    for (auto entry : table) {
        if (entry == sc) {
            return true;
        }
    }
    return false;
}

}  // namespace

bool isPathOp(std::string_view sc) noexcept { return contains(PATH_OPS, sc); }
bool isIoOp(std::string_view sc) noexcept   { return contains(IO_OPS, sc); }
bool isRenameOp(std::string_view sc) noexcept { return contains(RENAME_SC, sc); }
bool isOpenOp(std::string_view sc) noexcept { return contains(OPEN_SC, sc); }

OpClass classifyOp(std::string_view sc) noexcept
{
    if (isIoOp(sc)) {
        return OpClass::Io;
    }
    if (isPathOp(sc)) {
        return OpClass::Meta;
    }
    return OpClass::Other;
}

}  // namespace trace_replay
