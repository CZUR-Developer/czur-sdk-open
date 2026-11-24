#define SDK_OPEN_TLS_TRANSACTION_TESTING
#include "runtime/sdk_tls_file_transaction.h"

#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

using editor::sdk::CommitTlsTransaction;
using editor::sdk::RecoverTlsTransaction;
using editor::sdk::ResetTlsTransactionTestFault;
using editor::sdk::ResetTlsTransactionTestRecoveryFault;
using editor::sdk::SetTlsTransactionTestFault;
using editor::sdk::SetTlsTransactionTestRecoveryFault;
using editor::sdk::TlsTransactionFile;
using editor::sdk::kTlsTransactionTestExit;
using editor::sdk::kTlsTransactionTestFail;

const char* const kNames[] = {
    "local-runtime-root-ca.crt",
    "local-runtime-root-ca.key",
    "sdk-runtime.localhost.fullchain.pem",
    "sdk-runtime.localhost.key",
    "tls-state.json"
};

std::string MakeTempDirectory() {
    char pattern[] = "/tmp/czur-tls-transaction.XXXXXX";
    char* path = ::mkdtemp(pattern);
    assert(path != NULL);
    return path;
}

void RemoveTempDirectory(const std::string& directory) {
    const std::string command = "/bin/rm -rf '" + directory + "'";
    assert(std::system(command.c_str()) == 0);
}

void WriteFile(const std::string& path, const std::string& contents, int mode) {
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, mode);
    assert(fd >= 0);
    std::size_t offset = 0;
    while (offset < contents.size()) {
        const ssize_t count = ::write(fd, contents.data() + offset,
                                      contents.size() - offset);
        assert(count > 0);
        offset += static_cast<std::size_t>(count);
    }
    assert(::fsync(fd) == 0);
    assert(::close(fd) == 0);
}

std::string ReadFile(const std::string& path) {
    std::ifstream stream(path.c_str(), std::ios::in | std::ios::binary);
    assert(stream.good());
    std::ostringstream contents;
    contents << stream.rdbuf();
    return contents.str();
}

bool Exists(const std::string& path) {
    struct stat info;
    return ::lstat(path.c_str(), &info) == 0;
}

std::vector<std::string> Paths(const std::string& directory) {
    std::vector<std::string> paths;
    for (std::size_t index = 0; index < 5; ++index)
        paths.push_back(directory + "/" + kNames[index]);
    return paths;
}

std::vector<TlsTransactionFile> Files(const std::string& directory,
                                      const std::string& prefix) {
    std::vector<TlsTransactionFile> files;
    for (std::size_t index = 0; index < 5; ++index) {
        TlsTransactionFile file;
        file.path = directory + "/" + kNames[index];
        file.contents = prefix + kNames[index];
        file.mode = (index == 1 || index == 3) ? 0600 : 0644;
        files.push_back(file);
    }
    return files;
}

void Seed(const std::string& directory, const std::string& prefix) {
    const std::vector<TlsTransactionFile> files = Files(directory, prefix);
    for (std::size_t index = 0; index < files.size(); ++index)
        WriteFile(files[index].path, files[index].contents, files[index].mode);
}

void AssertSuite(const std::string& directory, const std::string& prefix) {
    const std::vector<TlsTransactionFile> files = Files(directory, prefix);
    for (std::size_t index = 0; index < files.size(); ++index) {
        assert(ReadFile(files[index].path) == files[index].contents);
        struct stat info;
        assert(::lstat(files[index].path.c_str(), &info) == 0);
        assert(S_ISREG(info.st_mode));
        assert(static_cast<int>(info.st_mode & 0777) == files[index].mode);
    }
}

void TestBasicCommit() {
    const std::string directory = MakeTempDirectory();
    Seed(directory, "old:");
    std::string error;
    assert(CommitTlsTransaction(directory, Files(directory, "new:"), &error));
    assert(error.empty());
    AssertSuite(directory, "new:");
    assert(!Exists(directory + "/.tls-update"));
    RemoveTempDirectory(directory);
}

void TestReturnedFailureRollsBackEveryCommitPoint() {
    for (int point = 0; point < 5; ++point) {
        const std::string directory = MakeTempDirectory();
        Seed(directory, "old:");
        SetTlsTransactionTestFault(point, kTlsTransactionTestFail);
        std::string error;
        assert(!CommitTlsTransaction(directory, Files(directory, "new:"), &error));
        assert(error.find("Injected") != std::string::npos);
        ResetTlsTransactionTestFault();
        AssertSuite(directory, "old:");
        assert(!Exists(directory + "/.tls-update"));
        RemoveTempDirectory(directory);
    }
}

void TestRollbackRestoresOriginallyMissingFilesAsMissing() {
    const std::string directory = MakeTempDirectory();
    Seed(directory, "old:");
    assert(::unlink((directory + "/tls-state.json").c_str()) == 0);
    SetTlsTransactionTestFault(4, kTlsTransactionTestFail);
    std::string error;
    assert(!CommitTlsTransaction(directory, Files(directory, "new:"), &error));
    ResetTlsTransactionTestFault();
    for (std::size_t index = 0; index < 4; ++index)
        assert(ReadFile(directory + "/" + kNames[index]) ==
               std::string("old:") + kNames[index]);
    assert(!Exists(directory + "/tls-state.json"));
    RemoveTempDirectory(directory);
}

void RunCrashCommit(const std::string& directory, int point) {
    const pid_t child = ::fork();
    assert(child >= 0);
    if (child == 0) {
        SetTlsTransactionTestFault(point, kTlsTransactionTestExit);
        std::string error;
        CommitTlsTransaction(directory, Files(directory, "new:"), &error);
        ::_exit(99);
    }
    int status = 0;
    assert(::waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 86);
}

void TestCrashRecoveryAtEveryCommitPoint() {
    for (int point = 0; point < 5; ++point) {
        const std::string directory = MakeTempDirectory();
        Seed(directory, "old:");
        RunCrashCommit(directory, point);
        std::string error;
        assert(RecoverTlsTransaction(directory, Paths(directory), &error));
        AssertSuite(directory, "old:");
        assert(RecoverTlsTransaction(directory, Paths(directory), &error));
        RemoveTempDirectory(directory);
    }
}

void TestRecoveryCanResumeAtEveryRollbackPoint() {
    for (int point = 0; point <= 5; ++point) {
        const std::string directory = MakeTempDirectory();
        Seed(directory, "old:");
        RunCrashCommit(directory, 4);
        const pid_t child = ::fork();
        assert(child >= 0);
        if (child == 0) {
            SetTlsTransactionTestRecoveryFault(point, kTlsTransactionTestExit);
            std::string error;
            RecoverTlsTransaction(directory, Paths(directory), &error);
            ::_exit(99);
        }
        int status = 0;
        assert(::waitpid(child, &status, 0) == child);
        assert(WIFEXITED(status));
        assert(WEXITSTATUS(status) == 87);
        ResetTlsTransactionTestRecoveryFault();
        std::string error;
        assert(RecoverTlsTransaction(directory, Paths(directory), &error));
        AssertSuite(directory, "old:");
        assert(!Exists(directory + "/.tls-update"));
        RemoveTempDirectory(directory);
    }
}

void TestCompleteJournalNeverRollsBack() {
    const std::string directory = MakeTempDirectory();
    Seed(directory, "old:");
    RunCrashCommit(directory, 5);
    AssertSuite(directory, "new:");
    std::string error;
    assert(RecoverTlsTransaction(directory, Paths(directory), &error));
    AssertSuite(directory, "new:");
    assert(!Exists(directory + "/.tls-update"));
    RemoveTempDirectory(directory);
}

void TestPathBindingPreventsWrongRecovery() {
    const std::string directory = MakeTempDirectory();
    Seed(directory, "old:");
    RunCrashCommit(directory, 1);
    std::vector<std::string> wrong = Paths(directory);
    wrong[0] = directory + "/wrong.crt";
    std::string error;
    assert(!RecoverTlsTransaction(directory, wrong, &error));
    assert(error.find("binding") != std::string::npos);
    assert(RecoverTlsTransaction(directory, Paths(directory), &error));
    AssertSuite(directory, "old:");
    RemoveTempDirectory(directory);
}

void TestRecoveryAllowsOrderedSubsetOfKnownPaths() {
    const std::string directory = MakeTempDirectory();
    Seed(directory, "old:");
    std::vector<TlsTransactionFile> custom_files = Files(directory, "new:");
    custom_files.erase(custom_files.begin(), custom_files.begin() + 2);
    const pid_t child = ::fork();
    assert(child >= 0);
    if (child == 0) {
        SetTlsTransactionTestFault(1, kTlsTransactionTestExit);
        std::string error;
        CommitTlsTransaction(directory, custom_files, &error);
        ::_exit(99);
    }
    int status = 0;
    assert(::waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 86);
    std::string error;
    assert(RecoverTlsTransaction(directory, Paths(directory), &error));
    AssertSuite(directory, "old:");
    RemoveTempDirectory(directory);
}

void TestIncompleteStagingIsDiscarded() {
    const std::string directory = MakeTempDirectory();
    assert(::mkdir((directory + "/.tls-update").c_str(), 0700) == 0);
    WriteFile(directory + "/.tls-update/new.0", "partial", 0600);
    std::string error;
    assert(RecoverTlsTransaction(directory, Paths(directory), &error));
    assert(!Exists(directory + "/.tls-update"));
    RemoveTempDirectory(directory);
}

void TestSymlinksAndTraversalAreRejected() {
    const std::string directory = MakeTempDirectory();
    Seed(directory, "old:");
    assert(::unlink((directory + "/sdk-runtime.localhost.key").c_str()) == 0);
    assert(::symlink("local-runtime-root-ca.key",
                     (directory + "/sdk-runtime.localhost.key").c_str()) == 0);
    std::string error;
    assert(!CommitTlsTransaction(directory, Files(directory, "new:"), &error));
    assert(error.find("non-regular") != std::string::npos);

    std::vector<TlsTransactionFile> traversal = Files(directory, "new:");
    traversal[0].path = directory + "/../outside.crt";
    assert(!CommitTlsTransaction(directory, traversal, &error));
    assert(error.find("direct child") != std::string::npos);

    const std::string link = directory + ".link";
    assert(::symlink(directory.c_str(), link.c_str()) == 0);
    assert(!CommitTlsTransaction(link, Files(link, "new:"), &error));
    assert(error.find("real directory") != std::string::npos);
    assert(::unlink(link.c_str()) == 0);
    RemoveTempDirectory(directory);
}

void TestPermissionFailureLeavesOldSuite() {
    const std::string directory = MakeTempDirectory();
    Seed(directory, "old:");
    assert(::chmod(directory.c_str(), 0500) == 0);
    std::string error;
    assert(!CommitTlsTransaction(directory, Files(directory, "new:"), &error));
    assert(::chmod(directory.c_str(), 0700) == 0);
    AssertSuite(directory, "old:");
    RemoveTempDirectory(directory);
}

void TestCallerLockSerializesCommits() {
    const std::string directory = MakeTempDirectory();
    Seed(directory, "old:");
    const std::string lock_path = directory + "/tls-state.json.lock";
    WriteFile(lock_path, "", 0600);
    const int lock_fd = ::open(lock_path.c_str(), O_RDWR | O_NOFOLLOW);
    assert(lock_fd >= 0);
    assert(::flock(lock_fd, LOCK_EX) == 0);
    const pid_t child = ::fork();
    assert(child >= 0);
    if (child == 0) {
        const int child_lock = ::open(lock_path.c_str(), O_RDWR | O_NOFOLLOW);
        if (child_lock < 0 || ::flock(child_lock, LOCK_EX) != 0) ::_exit(2);
        std::string error;
        const bool ok = CommitTlsTransaction(directory, Files(directory, "child:"), &error);
        ::flock(child_lock, LOCK_UN);
        ::close(child_lock);
        ::_exit(ok ? 0 : 3);
    }
    ::usleep(50000);
    AssertSuite(directory, "old:");
    assert(::flock(lock_fd, LOCK_UN) == 0);
    ::close(lock_fd);
    int status = 0;
    assert(::waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    AssertSuite(directory, "child:");
    RemoveTempDirectory(directory);
}

}  // namespace

int main() {
    TestBasicCommit();
    TestReturnedFailureRollsBackEveryCommitPoint();
    TestRollbackRestoresOriginallyMissingFilesAsMissing();
    TestCrashRecoveryAtEveryCommitPoint();
    TestRecoveryCanResumeAtEveryRollbackPoint();
    TestCompleteJournalNeverRollsBack();
    TestPathBindingPreventsWrongRecovery();
    TestRecoveryAllowsOrderedSubsetOfKnownPaths();
    TestIncompleteStagingIsDiscarded();
    TestSymlinksAndTraversalAreRejected();
    TestPermissionFailureLeavesOldSuite();
    TestCallerLockSerializesCommits();
    std::cout << "tls_file_transaction_test: PASS\n";
    return 0;
}
