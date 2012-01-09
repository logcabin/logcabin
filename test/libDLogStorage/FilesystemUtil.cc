/* Copyright (c) 2011 Stanford University
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR(S) DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/stat.h>
#include <sys/uio.h>

#include <queue>
#include <string>
#include <vector>

#include "Debug.h"
#include "libDLogStorage/FilesystemUtil.h"

namespace DLog {
namespace Storage {

using std::queue;
using std::string;
using std::vector;

namespace FilesystemUtil {
namespace System {
  extern ssize_t (*writev)(int fildes,
                           const struct iovec* iov,
                           int iovcnt);
} // namespace DLog::Storage::FilesystemUtil::System
} // namespace DLog::Storage::FilesystemUtil

namespace MockWritev {

struct State {
    State() : allowWrites(), written() {}
    queue<int> allowWrites; // negative values are taken as errno
    string written;
};
std::unique_ptr<State> state;

ssize_t
writev(int fildes, const struct iovec* iov, int iovcnt)
{
    if (state->allowWrites.empty()) {
        errno = EINVAL;
        return -1;
    }
    int allowWrite = state->allowWrites.front();
    state->allowWrites.pop();
    if (allowWrite < 0) {
        errno = -allowWrite;
        return -1;
    }

    std::string flattened;
    for (int i = 0; i < iovcnt; ++i) {
        flattened += string(static_cast<const char*>(iov[i].iov_base),
                                                     iov[i].iov_len);
    }
    state->written.append(flattened, 0, allowWrite);
    return allowWrite;
}

} // namespace Dlog::Storage::MockWritev

class FilesystemUtilTest : public ::testing::Test {
  public:
    FilesystemUtilTest()
        : tmpdir(FilesystemUtil::tmpnam())
    {
        if (mkdir(tmpdir.c_str(), 0755) != 0)
            PANIC("Couldn't create temporary directory for tests");
        MockWritev::state.reset(new MockWritev::State);
    }
    ~FilesystemUtilTest() {
        // It's a bit dubious to be using the functions we're testing to set up
        // the test fixture. Hopefully this won't trash your home directory.
        FilesystemUtil::remove(tmpdir);
        FilesystemUtil::System::writev = ::writev;
        MockWritev::state.reset();
    }
    std::string tmpdir;
};


TEST_F(FilesystemUtilTest, ls) {
    EXPECT_DEATH(FilesystemUtil::ls("/path/does/not/exist"),
                 "Could not list contents");
    // TODO(ongaro): Test readdir_r failure.

    EXPECT_EQ((vector<string> {}),
              sorted(FilesystemUtil::ls(tmpdir)));

    EXPECT_EQ(0, mkdir((tmpdir + "/a").c_str(), 0755));
    int fd = open((tmpdir + "/b").c_str(), O_WRONLY|O_CREAT, 0644);
    EXPECT_LE(0, fd);
    EXPECT_EQ(0, close(fd));
    EXPECT_EQ(0, mkdir((tmpdir + "/c").c_str(), 0755));
    EXPECT_EQ((vector<string> { "a", "b", "c" }),
              sorted(FilesystemUtil::ls(tmpdir)));
}

TEST_F(FilesystemUtilTest, remove) {
    // does not exist
    FilesystemUtil::remove(tmpdir + "/a");

    // dir exists with no children
    EXPECT_EQ(0, mkdir((tmpdir + "/b").c_str(), 0755));
    FilesystemUtil::remove(tmpdir + "/b");

    // file exists with no children
    int fd = open((tmpdir + "/c").c_str(), O_WRONLY|O_CREAT, 0644);
    EXPECT_LE(0, fd);
    EXPECT_EQ(0, close(fd));
    FilesystemUtil::remove(tmpdir + "/c");

    // dir exists with children
    EXPECT_EQ(0, mkdir((tmpdir + "/d").c_str(), 0755));
    EXPECT_EQ(0, mkdir((tmpdir + "/d/e").c_str(), 0755));
    EXPECT_EQ(0, mkdir((tmpdir + "/d/f").c_str(), 0755));
    FilesystemUtil::remove(tmpdir + "/d");

    EXPECT_EQ((vector<string> {}),
              sorted(FilesystemUtil::ls(tmpdir)));

    // error
    EXPECT_EQ(0, mkdir((tmpdir + "/g").c_str(), 0755));
    EXPECT_DEATH(FilesystemUtil::remove(tmpdir + "/g/."),
                 "Could not remove");
}

TEST_F(FilesystemUtilTest, tmpnam) {
    EXPECT_NE(FilesystemUtil::tmpnam(), FilesystemUtil::tmpnam());
}

TEST_F(FilesystemUtilTest, writeCommon) {
    int fd = open((tmpdir + "/a").c_str(), O_RDWR|O_CREAT, 0644);
    EXPECT_LE(0, fd);
    EXPECT_EQ(13, FilesystemUtil::write(fd, {
            {"hello ", 6},
            {"", 0},
            {"world!", 7},
        }));
    char buf[13];
    EXPECT_EQ(13, pread(fd, buf, sizeof(buf), 0));
    EXPECT_STREQ("hello world!", buf);
    EXPECT_EQ(0, close(fd));

}

TEST_F(FilesystemUtilTest, writeInterruption) {
    MockWritev::state->allowWrites.push(-EINTR);
    MockWritev::state->allowWrites.push(0);
    MockWritev::state->allowWrites.push(1);
    MockWritev::state->allowWrites.push(8);
    MockWritev::state->allowWrites.push(4);
    FilesystemUtil::System::writev = MockWritev::writev;
    EXPECT_EQ(13, FilesystemUtil::write(100, {
            {"hello ", 6},
            {"", 0},
            {"world!", 7},
        }));
    EXPECT_EQ(13U, MockWritev::state->written.size());
    EXPECT_STREQ("hello world!", MockWritev::state->written.c_str());
}

class FileContentsTest : public FilesystemUtilTest {
    FileContentsTest()
        : path(tmpdir + "/a") {
        int fd = open(path.c_str(), O_WRONLY|O_CREAT, 0644);
        if (FilesystemUtil::write(fd, "hello world!", 13) != 13)
            PANIC("write failed");
        close(fd);
    }
    std::string path;
};

TEST_F(FileContentsTest, constructor) {
    EXPECT_DEATH(FilesystemUtil::FileContents file(tmpdir + "/b"),
                 "Could not open");
}

TEST_F(FileContentsTest, getFileLength) {
    FilesystemUtil::FileContents file(path);
    EXPECT_EQ(13U, file.getFileLength());
}

TEST_F(FileContentsTest, copy) {
    FilesystemUtil::FileContents file(path);
    char buf[13];
    strcpy(buf, "cccccccccccc"); // NOLINT
    file.copy(0, buf, 13);
    EXPECT_STREQ("hello world!", buf);
    strcpy(buf, "cccccccccccc"); // NOLINT
    file.copy(13, buf, 0); // should be ok
    file.copy(15, buf, 0); // should be ok
    EXPECT_STREQ("cccccccccccc", buf);
    EXPECT_DEATH(file.copy(0, buf, 14),
                 "ERROR");
    EXPECT_DEATH(file.copy(1, buf, 13),
                 "ERROR");
}

TEST_F(FileContentsTest, copyPartial) {
    char buf[13];
    strcpy(buf, "cccccccccccc"); // NOLINT
    FilesystemUtil::FileContents file(path);
    EXPECT_EQ(13U, file.copyPartial(0, buf, 13));
    EXPECT_STREQ("hello world!", buf);
    strcpy(buf, "cccccccccccc"); // NOLINT
    EXPECT_EQ(0U, file.copyPartial(13, buf, 0));
    EXPECT_EQ(0U, file.copyPartial(15, buf, 0));
    EXPECT_STREQ("cccccccccccc", buf);
    EXPECT_EQ(13U, file.copyPartial(0, buf, 14));
    EXPECT_STREQ("hello world!", buf);
    strcpy(buf, "cccccccccccc"); // NOLINT
    EXPECT_EQ(12U, file.copyPartial(1, buf, 13));
    EXPECT_STREQ("ello world!", buf);
}

TEST_F(FileContentsTest, get) {
    FilesystemUtil::FileContents file(path);
    EXPECT_STREQ("hello world!",
                 file.get<char>(0, 13));
    file.get<char>(13, 0); // should be ok, result doesn't matter
    file.get<char>(15, 0); // should be ok, result doesn't matter
    EXPECT_DEATH(file.get(0, 14),
                 "ERROR");
    EXPECT_DEATH(file.get(1, 13),
                 "ERROR");
}

} // namespace DLog::Storage
} // namespace DLog
