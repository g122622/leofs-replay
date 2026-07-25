#include "trace_replay/core/SyscallClassify.hpp"

#include <array>
#include <string_view>

namespace trace_replay {
namespace {

// 忠实移植 rawproc 的 PATH_OPS（见 context.md 第 102-107 行）
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

// 忠实移植 rawproc 的 IO_OPS（见 context.md 第 108 行）
constexpr std::array IO_OPS = {
    std::string_view{"read"},   std::string_view{"write"},
    std::string_view{"pread64"}, std::string_view{"pwrite64"},
    std::string_view{"readv"},  std::string_view{"writev"},
};

// 忠实移植 rawproc 的 RENAME_SC（见 context.md 第 116 行）
constexpr std::array RENAME_SC = {
    std::string_view{"renameat"}, std::string_view{"renameat2"},
    std::string_view{"rename"},
};

// 需要真实打开文件、向 fd 表登记映射的操作
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
