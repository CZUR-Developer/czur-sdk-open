#ifndef CZUR_SDK_OPEN_RUNTIME_SDK_TLS_FILE_TRANSACTION_H_
#define CZUR_SDK_OPEN_RUNTIME_SDK_TLS_FILE_TRANSACTION_H_

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace editor {
namespace sdk {

struct TlsTransactionFile {
    std::string path;
    std::string contents;
    int mode;
};

#if defined(SDK_OPEN_TLS_TRANSACTION_TESTING)
enum TlsTransactionTestFaultAction {
    kTlsTransactionTestNoFault = 0,
    kTlsTransactionTestFail = 1,
    kTlsTransactionTestExit = 2
};
#endif

namespace tls_transaction_detail {

struct JournalEntry {
    std::string name;
    bool existed;
    int mode;
};

struct TestFaultState {
    int point;
    int action;
    TestFaultState() : point(-1), action(0) {}
};

inline TestFaultState& FaultState() {
    static TestFaultState state;
    return state;
}

inline TestFaultState& RecoveryFaultState() {
    static TestFaultState state;
    return state;
}

inline void SetError(std::string* error, const std::string& value) {
    if (error) *error = value;
}

inline std::string ErrnoMessage(const std::string& operation) {
    std::ostringstream stream;
    stream << operation << ": " << std::strerror(errno);
    return stream.str();
}

inline bool WriteAll(int fd, const std::string& contents) {
    std::size_t offset = 0;
    while (offset < contents.size()) {
        const ssize_t written =
            ::write(fd, contents.data() + offset, contents.size() - offset);
        if (written < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (written == 0) {
            errno = EIO;
            return false;
        }
        offset += static_cast<std::size_t>(written);
    }
    return true;
}

inline bool ReadAll(int fd, std::string* contents) {
    contents->clear();
    char buffer[8192];
    for (;;) {
        const ssize_t count = ::read(fd, buffer, sizeof(buffer));
        if (count < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (count == 0) return true;
        contents->append(buffer, static_cast<std::size_t>(count));
    }
}

inline bool IsDirectName(const std::string& name) {
    return !name.empty() && name != "." && name != ".." &&
           name.find('/') == std::string::npos;
}

inline bool NormalizeName(const std::string& tls_dir,
                          const std::string& path,
                          std::string* name) {
    if (IsDirectName(path)) {
        *name = path;
        return true;
    }
    const std::string prefix = tls_dir + "/";
    if (path.compare(0, prefix.size(), prefix) != 0) return false;
    const std::string relative = path.substr(prefix.size());
    if (!IsDirectName(relative)) return false;
    *name = relative;
    return true;
}

inline bool OpenDirectoryNoFollow(const std::string& path,
                                  int* fd,
                                  std::string* error) {
    struct stat info;
    if (::lstat(path.c_str(), &info) != 0) {
        SetError(error, ErrnoMessage("lstat " + path));
        return false;
    }
    if (!S_ISDIR(info.st_mode) || S_ISLNK(info.st_mode)) {
        SetError(error, "TLS transaction path is not a real directory: " + path);
        return false;
    }
    *fd = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (*fd < 0) {
        SetError(error, ErrnoMessage("open directory " + path));
        return false;
    }
    return true;
}

inline bool FsyncDirectory(int fd, const std::string& name, std::string* error) {
    if (::fsync(fd) == 0) return true;
    SetError(error, ErrnoMessage("fsync directory " + name));
    return false;
}

inline bool EnsureRegularOrMissing(int dir_fd,
                                   const std::string& name,
                                   bool* exists,
                                   struct stat* info,
                                   std::string* error) {
    if (::fstatat(dir_fd, name.c_str(), info, AT_SYMLINK_NOFOLLOW) == 0) {
        *exists = true;
        if (!S_ISREG(info->st_mode) || S_ISLNK(info->st_mode)) {
            SetError(error, "TLS transaction refuses non-regular file: " + name);
            return false;
        }
        return true;
    }
    if (errno == ENOENT) {
        *exists = false;
        std::memset(info, 0, sizeof(*info));
        return true;
    }
    SetError(error, ErrnoMessage("stat " + name));
    return false;
}

inline bool CopyFileAt(int source_dir,
                       const std::string& source,
                       int destination_dir,
                       const std::string& destination,
                       mode_t mode,
                       std::string* error) {
    const int source_fd = ::openat(source_dir, source.c_str(), O_RDONLY | O_NOFOLLOW);
    if (source_fd < 0) {
        SetError(error, ErrnoMessage("open " + source));
        return false;
    }
    struct stat info;
    if (::fstat(source_fd, &info) != 0 || !S_ISREG(info.st_mode)) {
        if (errno == 0) errno = EINVAL;
        SetError(error, ErrnoMessage("validate " + source));
        ::close(source_fd);
        return false;
    }
    const int destination_fd = ::openat(destination_dir, destination.c_str(),
        O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, mode);
    if (destination_fd < 0) {
        SetError(error, ErrnoMessage("create " + destination));
        ::close(source_fd);
        return false;
    }
    std::string contents;
    bool ok = ReadAll(source_fd, &contents);
    if (!ok) SetError(error, ErrnoMessage("read " + source));
    if (ok && !WriteAll(destination_fd, contents)) {
        ok = false;
        SetError(error, ErrnoMessage("write " + destination));
    }
    if (ok && ::fchmod(destination_fd, mode) != 0) {
        ok = false;
        SetError(error, ErrnoMessage("chmod " + destination));
    }
    if (ok && ::fsync(destination_fd) != 0) {
        ok = false;
        SetError(error, ErrnoMessage("fsync " + destination));
    }
    if (::close(destination_fd) != 0 && ok) {
        ok = false;
        SetError(error, ErrnoMessage("close " + destination));
    }
    ::close(source_fd);
    if (!ok) ::unlinkat(destination_dir, destination.c_str(), 0);
    return ok;
}

inline bool WriteFileAt(int dir_fd,
                        const std::string& name,
                        const std::string& contents,
                        mode_t mode,
                        std::string* error) {
    const int fd = ::openat(dir_fd, name.c_str(),
        O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, mode);
    if (fd < 0) {
        SetError(error, ErrnoMessage("create " + name));
        return false;
    }
    bool ok = WriteAll(fd, contents);
    if (!ok) SetError(error, ErrnoMessage("write " + name));
    if (ok && ::fchmod(fd, mode) != 0) {
        ok = false;
        SetError(error, ErrnoMessage("chmod " + name));
    }
    if (ok && ::fsync(fd) != 0) {
        ok = false;
        SetError(error, ErrnoMessage("fsync " + name));
    }
    if (::close(fd) != 0 && ok) {
        ok = false;
        SetError(error, ErrnoMessage("close " + name));
    }
    if (!ok) ::unlinkat(dir_fd, name.c_str(), 0);
    return ok;
}

inline std::string HexEncode(const std::string& value) {
    static const char digits[] = "0123456789abcdef";
    std::string encoded;
    encoded.reserve(value.size() * 2);
    for (std::size_t index = 0; index < value.size(); ++index) {
        const unsigned char character = static_cast<unsigned char>(value[index]);
        encoded.push_back(digits[character >> 4]);
        encoded.push_back(digits[character & 15]);
    }
    return encoded;
}

inline int HexDigit(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

inline bool HexDecode(const std::string& encoded, std::string* value) {
    if ((encoded.size() % 2) != 0) return false;
    value->clear();
    for (std::size_t index = 0; index < encoded.size(); index += 2) {
        const int high = HexDigit(encoded[index]);
        const int low = HexDigit(encoded[index + 1]);
        if (high < 0 || low < 0) return false;
        value->push_back(static_cast<char>((high << 4) | low));
    }
    return true;
}

inline std::string JournalContents(const char* state,
                                   const std::vector<JournalEntry>& entries) {
    std::ostringstream stream;
    stream << "CZUR_TLS_TRANSACTION_V1\n" << state << "\n" << entries.size() << "\n";
    for (std::size_t index = 0; index < entries.size(); ++index) {
        stream << HexEncode(entries[index].name) << " "
               << (entries[index].existed ? 1 : 0) << " "
               << entries[index].mode << "\n";
    }
    return stream.str();
}

inline bool ParseJournal(const std::string& contents,
                         std::string* state,
                         std::vector<JournalEntry>* entries,
                         std::string* error) {
    std::istringstream stream(contents);
    std::string magic;
    std::size_t count = 0;
    if (!(stream >> magic >> *state >> count) ||
        magic != "CZUR_TLS_TRANSACTION_V1" ||
        (*state != "PENDING" && *state != "COMPLETE" && *state != "ROLLED_BACK") || count > 1024) {
        SetError(error, "Invalid TLS transaction journal header");
        return false;
    }
    entries->clear();
    for (std::size_t index = 0; index < count; ++index) {
        std::string encoded;
        int existed = 0;
        int mode = 0;
        JournalEntry entry;
        if (!(stream >> encoded >> existed >> mode) ||
            !HexDecode(encoded, &entry.name) || !IsDirectName(entry.name) ||
            (existed != 0 && existed != 1) || mode < 0 || mode > 0777) {
            SetError(error, "Invalid TLS transaction journal entry");
            return false;
        }
        entry.existed = existed != 0;
        entry.mode = mode;
        entries->push_back(entry);
    }
    std::string extra;
    if (stream >> extra) {
        SetError(error, "Unexpected TLS transaction journal data");
        return false;
    }
    return true;
}

inline bool ReadJournal(int transaction_fd,
                        std::string* state,
                        std::vector<JournalEntry>* entries,
                        std::string* error) {
    const int fd = ::openat(transaction_fd, "journal", O_RDONLY | O_NOFOLLOW);
    if (fd < 0) {
        SetError(error, ErrnoMessage("open TLS transaction journal"));
        return false;
    }
    struct stat info;
    std::string contents;
    bool ok = ::fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && ReadAll(fd, &contents);
    if (!ok) SetError(error, ErrnoMessage("read TLS transaction journal"));
    ::close(fd);
    return ok && ParseJournal(contents, state, entries, error);
}

inline bool WriteJournal(int transaction_fd,
                         const char* state,
                         const std::vector<JournalEntry>& entries,
                         std::string* error) {
    ::unlinkat(transaction_fd, "journal.next", 0);
    if (!WriteFileAt(transaction_fd, "journal.next", JournalContents(state, entries),
                     0600, error)) return false;
    if (::renameat(transaction_fd, "journal.next", transaction_fd, "journal") != 0) {
        SetError(error, ErrnoMessage("publish TLS transaction journal"));
        ::unlinkat(transaction_fd, "journal.next", 0);
        return false;
    }
    return FsyncDirectory(transaction_fd, ".tls-update", error);
}

inline bool ValidatePathBinding(const std::string& tls_dir,
                                const std::vector<std::string>& paths,
                                const std::vector<JournalEntry>& entries,
                                std::string* error) {
    std::vector<std::string> allowed;
    for (std::size_t index = 0; index < paths.size(); ++index) {
        std::string name;
        if (!NormalizeName(tls_dir, paths[index], &name)) {
            SetError(error, "TLS transaction allowed path is not a direct child");
            return false;
        }
        for (std::size_t prior = 0; prior < allowed.size(); ++prior) {
            if (allowed[prior] == name) {
                SetError(error, "Duplicate TLS transaction allowed path");
                return false;
            }
        }
        allowed.push_back(name);
    }
    std::size_t allowed_index = 0;
    for (std::size_t index = 0; index < entries.size(); ++index) {
        while (allowed_index < allowed.size() && allowed[allowed_index] != entries[index].name)
            ++allowed_index;
        if (allowed_index == allowed.size()) {
            SetError(error, "TLS transaction journal path binding mismatch");
            return false;
        }
        ++allowed_index;
    }
    return true;
}

inline bool RemoveKnownTransactionFiles(int transaction_fd,
                                        std::size_t count,
                                        std::string* error) {
    for (std::size_t index = 0; index < count; ++index) {
        std::ostringstream number;
        number << index;
        const std::string suffix = number.str();
        const char* prefixes[] = {"new.", "old.", "restore."};
        for (std::size_t prefix = 0; prefix < 3; ++prefix) {
            const std::string name = std::string(prefixes[prefix]) + suffix;
            struct stat info;
            if (::fstatat(transaction_fd, name.c_str(), &info, AT_SYMLINK_NOFOLLOW) == 0) {
                if (!S_ISREG(info.st_mode) || S_ISLNK(info.st_mode)) {
                    SetError(error, "TLS transaction cleanup refuses non-regular file: " + name);
                    return false;
                }
                if (::unlinkat(transaction_fd, name.c_str(), 0) != 0) {
                    SetError(error, ErrnoMessage("remove " + name));
                    return false;
                }
            } else if (errno != ENOENT) {
                SetError(error, ErrnoMessage("stat " + name));
                return false;
            }
        }
    }
    const char* journals[] = {"journal.next", "journal"};
    for (std::size_t index = 0; index < 2; ++index) {
        struct stat info;
        if (::fstatat(transaction_fd, journals[index], &info, AT_SYMLINK_NOFOLLOW) == 0) {
            if (!S_ISREG(info.st_mode) || S_ISLNK(info.st_mode)) {
                SetError(error, "TLS transaction cleanup refuses invalid journal");
                return false;
            }
            if (::unlinkat(transaction_fd, journals[index], 0) != 0) {
                SetError(error, ErrnoMessage("remove transaction journal"));
                return false;
            }
        } else if (errno != ENOENT) {
            SetError(error, ErrnoMessage("stat transaction journal"));
            return false;
        }
    }
    return FsyncDirectory(transaction_fd, ".tls-update", error);
}

inline bool RemoveTransactionDirectory(int tls_fd,
                                       int transaction_fd,
                                       std::size_t count,
                                       std::string* error) {
    if (!RemoveKnownTransactionFiles(transaction_fd, count, error)) return false;
    if (::unlinkat(tls_fd, ".tls-update", AT_REMOVEDIR) != 0) {
        SetError(error, ErrnoMessage("remove .tls-update"));
        return false;
    }
    return FsyncDirectory(tls_fd, "TLS directory", error);
}

inline bool InjectRecoveryFault(int point, std::string* error) {
#if defined(SDK_OPEN_TLS_TRANSACTION_TESTING)
    TestFaultState& state = RecoveryFaultState();
    if (state.point != point || state.action == kTlsTransactionTestNoFault) return false;
    if (state.action == kTlsTransactionTestExit) ::_exit(87);
    std::ostringstream message;
    message << "Injected TLS recovery failure at rollback point " << point;
    SetError(error, message.str());
    return true;
#else
    (void)point;
    (void)error;
    return false;
#endif
}

inline bool Rollback(int tls_fd,
                     int transaction_fd,
                     const std::vector<JournalEntry>& entries,
                     std::string* error) {
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const JournalEntry& entry = entries[index];
        bool destination_exists = false;
        struct stat destination_info;
        if (!EnsureRegularOrMissing(tls_fd, entry.name, &destination_exists,
                                    &destination_info, error)) return false;
        if (entry.existed) {
            std::ostringstream number;
            number << index;
            const std::string backup = "old." + number.str();
            const std::string restore = "restore." + number.str();
            struct stat backup_info;
            bool backup_exists = false;
            if (!EnsureRegularOrMissing(transaction_fd, backup, &backup_exists,
                                        &backup_info, error) || !backup_exists) {
                if (error && error->empty()) *error = "Missing TLS transaction backup: " + backup;
                return false;
            }
            ::unlinkat(transaction_fd, restore.c_str(), 0);
            if (!CopyFileAt(transaction_fd, backup, transaction_fd, restore,
                            static_cast<mode_t>(entry.mode), error)) return false;
            if (::renameat(transaction_fd, restore.c_str(), tls_fd,
                           entry.name.c_str()) != 0) {
                SetError(error, ErrnoMessage("restore " + entry.name));
                return false;
            }
        } else if (destination_exists && ::unlinkat(tls_fd, entry.name.c_str(), 0) != 0) {
            SetError(error, ErrnoMessage("remove newly created " + entry.name));
            return false;
        }
        if (!FsyncDirectory(tls_fd, "TLS directory", error)) return false;
        if (InjectRecoveryFault(static_cast<int>(index), error)) return false;
    }
    return true;
}

inline bool InjectFault(int point, std::string* error) {
#if defined(SDK_OPEN_TLS_TRANSACTION_TESTING)
    TestFaultState& state = FaultState();
    if (state.point != point || state.action == kTlsTransactionTestNoFault) return false;
    if (state.action == kTlsTransactionTestExit) ::_exit(86);
    std::ostringstream message;
    message << "Injected TLS transaction failure at commit point " << point;
    SetError(error, message.str());
    return true;
#else
    (void)point;
    (void)error;
    return false;
#endif
}

}  // namespace tls_transaction_detail

#if defined(SDK_OPEN_TLS_TRANSACTION_TESTING)
inline void SetTlsTransactionTestFault(int commit_point,
                                       TlsTransactionTestFaultAction action) {
    tls_transaction_detail::FaultState().point = commit_point;
    tls_transaction_detail::FaultState().action = action;
}

inline void ResetTlsTransactionTestFault() {
    tls_transaction_detail::FaultState().point = -1;
    tls_transaction_detail::FaultState().action = kTlsTransactionTestNoFault;
}

inline void SetTlsTransactionTestRecoveryFault(
    int rollback_point, TlsTransactionTestFaultAction action) {
    tls_transaction_detail::RecoveryFaultState().point = rollback_point;
    tls_transaction_detail::RecoveryFaultState().action = action;
}

inline void ResetTlsTransactionTestRecoveryFault() {
    tls_transaction_detail::RecoveryFaultState().point = -1;
    tls_transaction_detail::RecoveryFaultState().action = kTlsTransactionTestNoFault;
}
#endif

inline bool RecoverTlsTransaction(const std::string& tls_dir,
                                  const std::vector<std::string>& paths,
                                  std::string* error) {
    using namespace tls_transaction_detail;
    if (error) error->clear();
    int tls_fd = -1;
    if (!OpenDirectoryNoFollow(tls_dir, &tls_fd, error)) return false;
    struct stat transaction_info;
    if (::fstatat(tls_fd, ".tls-update", &transaction_info, AT_SYMLINK_NOFOLLOW) != 0) {
        if (errno == ENOENT) {
            ::close(tls_fd);
            return true;
        }
        SetError(error, ErrnoMessage("stat .tls-update"));
        ::close(tls_fd);
        return false;
    }
    if (!S_ISDIR(transaction_info.st_mode) || S_ISLNK(transaction_info.st_mode)) {
        SetError(error, "TLS transaction directory is not a real directory");
        ::close(tls_fd);
        return false;
    }
    const int transaction_fd =
        ::openat(tls_fd, ".tls-update", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (transaction_fd < 0) {
        SetError(error, ErrnoMessage("open .tls-update"));
        ::close(tls_fd);
        return false;
    }
    struct stat journal_info;
    if (::fstatat(transaction_fd, "journal", &journal_info, AT_SYMLINK_NOFOLLOW) != 0 &&
        errno == ENOENT) {
        const bool cleaned = RemoveTransactionDirectory(
            tls_fd, transaction_fd, paths.size(), error);
        ::close(transaction_fd);
        ::close(tls_fd);
        return cleaned;
    }
    std::string state;
    std::vector<JournalEntry> entries;
    bool ok = ReadJournal(transaction_fd, &state, &entries, error);
    if (ok) ok = ValidatePathBinding(tls_dir, paths, entries, error);
    if (ok && state == "PENDING") {
        ok = Rollback(tls_fd, transaction_fd, entries, error);
        if (ok) ok = WriteJournal(transaction_fd, "ROLLED_BACK", entries, error);
        if (ok && InjectRecoveryFault(static_cast<int>(entries.size()), error)) ok = false;
    }
    if (ok) ok = RemoveTransactionDirectory(tls_fd, transaction_fd, entries.size(), error);
    ::close(transaction_fd);
    ::close(tls_fd);
    return ok;
}

inline bool CommitTlsTransaction(const std::string& tls_dir,
                                 const std::vector<TlsTransactionFile>& files,
                                 std::string* error) {
    using namespace tls_transaction_detail;
    if (error) error->clear();
    if (files.empty()) {
        SetError(error, "TLS transaction has no files");
        return false;
    }
    std::vector<std::string> paths;
    std::vector<std::string> names;
    for (std::size_t index = 0; index < files.size(); ++index) {
        std::string name;
        if (!NormalizeName(tls_dir, files[index].path, &name)) {
            SetError(error, "TLS transaction path is not a direct child: " + files[index].path);
            return false;
        }
        const int mode = files[index].mode & 0777;
        if (mode != files[index].mode || (mode != 0600 && mode != 0644)) {
            SetError(error, "TLS transaction file mode must be 0600 or 0644: " + name);
            return false;
        }
        if (name.find("key") != std::string::npos && mode != 0600) {
            SetError(error, "TLS private key mode must be 0600: " + name);
            return false;
        }
        for (std::size_t prior = 0; prior < names.size(); ++prior) {
            if (names[prior] == name) {
                SetError(error, "Duplicate TLS transaction path: " + name);
                return false;
            }
        }
        paths.push_back(files[index].path);
        names.push_back(name);
    }
    if (!RecoverTlsTransaction(tls_dir, paths, error)) return false;

    int tls_fd = -1;
    if (!OpenDirectoryNoFollow(tls_dir, &tls_fd, error)) return false;
    if (::mkdirat(tls_fd, ".tls-update", 0700) != 0) {
        SetError(error, ErrnoMessage("create .tls-update"));
        ::close(tls_fd);
        return false;
    }
    if (!FsyncDirectory(tls_fd, "TLS directory", error)) {
        ::unlinkat(tls_fd, ".tls-update", AT_REMOVEDIR);
        ::close(tls_fd);
        return false;
    }
    const int transaction_fd =
        ::openat(tls_fd, ".tls-update", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (transaction_fd < 0) {
        SetError(error, ErrnoMessage("open .tls-update"));
        ::close(tls_fd);
        return false;
    }
    std::vector<JournalEntry> entries;
    bool ok = true;
    bool pending = false;
    for (std::size_t index = 0; ok && index < files.size(); ++index) {
        bool existed = false;
        struct stat info;
        ok = EnsureRegularOrMissing(tls_fd, names[index], &existed, &info, error);
        JournalEntry entry;
        entry.name = names[index];
        entry.existed = existed;
        entry.mode = existed ? static_cast<int>(info.st_mode & 0777) : files[index].mode;
        entries.push_back(entry);
        std::ostringstream number;
        number << index;
        if (ok && existed) {
            ok = CopyFileAt(tls_fd, names[index], transaction_fd,
                            "old." + number.str(), info.st_mode & 0777, error);
        }
        if (ok) ok = WriteFileAt(transaction_fd, "new." + number.str(),
                                 files[index].contents,
                                 static_cast<mode_t>(files[index].mode), error);
    }
    if (ok) ok = FsyncDirectory(transaction_fd, ".tls-update", error);
    if (ok) {
        ok = WriteJournal(transaction_fd, "PENDING", entries, error);
        pending = ok;
    }
    for (std::size_t index = 0; ok && index < files.size(); ++index) {
        bool exists = false;
        struct stat info;
        ok = EnsureRegularOrMissing(tls_fd, names[index], &exists, &info, error);
        if (!ok) break;
        std::ostringstream number;
        number << index;
        if (::renameat(transaction_fd, ("new." + number.str()).c_str(),
                       tls_fd, names[index].c_str()) != 0) {
            SetError(error, ErrnoMessage("commit " + names[index]));
            ok = false;
            break;
        }
        ok = FsyncDirectory(tls_fd, "TLS directory", error);
        if (ok && InjectFault(static_cast<int>(index), error)) ok = false;
    }
    if (!ok && pending) {
        const std::string commit_error = error ? *error : std::string();
        std::string rollback_error;
        if (!Rollback(tls_fd, transaction_fd, entries, &rollback_error)) {
            SetError(error, commit_error + "; rollback failed: " + rollback_error);
        } else {
            SetError(error, commit_error);
            std::string cleanup_error;
            if (!WriteJournal(transaction_fd, "ROLLED_BACK", entries, &cleanup_error)) {
                SetError(error, commit_error + "; rollback journal failed: " + cleanup_error);
            } else {
                RemoveTransactionDirectory(tls_fd, transaction_fd, entries.size(), &cleanup_error);
            }
        }
        ::close(transaction_fd);
        ::close(tls_fd);
        return false;
    }
    if (!ok) {
        std::string cleanup_error;
        RemoveTransactionDirectory(tls_fd, transaction_fd, entries.size(), &cleanup_error);
        ::close(transaction_fd);
        ::close(tls_fd);
        return false;
    }
    if (!WriteJournal(transaction_fd, "COMPLETE", entries, error)) {
        const std::string commit_error = error ? *error : std::string();
        std::string rollback_error;
        if (!Rollback(tls_fd, transaction_fd, entries, &rollback_error))
            SetError(error, commit_error + "; rollback failed: " + rollback_error);
        else {
            std::string journal_error;
            if (!WriteJournal(transaction_fd, "ROLLED_BACK", entries, &journal_error))
                SetError(error, commit_error + "; rollback journal failed: " + journal_error);
            else
                SetError(error, commit_error);
        }
        ::close(transaction_fd);
        ::close(tls_fd);
        return false;
    }
    if (InjectFault(static_cast<int>(files.size()), error)) {
        ::close(transaction_fd);
        ::close(tls_fd);
        return false;
    }
    ok = RemoveTransactionDirectory(tls_fd, transaction_fd, entries.size(), error);
    ::close(transaction_fd);
    ::close(tls_fd);
    return ok;
}

}  // namespace sdk
}  // namespace editor

#endif  // CZUR_SDK_OPEN_RUNTIME_SDK_TLS_FILE_TRANSACTION_H_
