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

#include "Debug.h"
#include "libDLogStorage/FilesystemUtil.h"

namespace DLog {
namespace Storage {

using std::string;
using std::vector;

namespace {
} // anonymous namespace

class FilesystemUtilTest : public ::testing::Test {
  public:
    FilesystemUtilTest()
        : tmpdir(FilesystemUtil::tmpnam())
    {
        if (mkdir(tmpdir.c_str(), 0755) != 0)
            PANIC("Couldn't create temporary directory for tests");
    }
    ~FilesystemUtilTest() {
        // It's a bit dubious to be using the functions we're testing to set up
        // the test fixture. Hopefully this won't trash your home directory.
        FilesystemUtil::remove(tmpdir);
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

} // namespace DLog::Storage
} // namespace DLog
