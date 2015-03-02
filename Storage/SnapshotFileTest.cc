/* Copyright (c) 2013 Stanford University
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

#include "Core/Debug.h"
#include "Core/StringUtil.h"
#include "Storage/FilesystemUtil.h"
#include "Storage/Layout.h"
#include "Storage/SnapshotFile.h"
#include "include/LogCabin/Debug.h"

namespace LogCabin {
namespace Storage {
namespace SnapshotFile {
namespace {

class StorageSnapshotFileTest : public ::testing::Test {
  public:
    StorageSnapshotFileTest()
        : layout()
    {
        layout.initTemporary();
        // the lock file pollutes the directory. we don't need it.
        Storage::FilesystemUtil::removeFile(layout.serverDir, "lock");
    }
    ~StorageSnapshotFileTest() {
    }
    Storage::Layout layout;
};


TEST_F(StorageSnapshotFileTest, basic)
{
    {
        Writer writer(layout);
        writer.getStream().WriteVarint32(482);
        writer.save();
    }
    {
        Reader reader(layout);
        uint32_t x = 0;
        reader.getStream().ReadVarint32(&x);
        EXPECT_EQ(482U, x);
    }
}

TEST_F(StorageSnapshotFileTest, reader_fileNotFound)
{
    EXPECT_THROW(Reader reader(layout), std::runtime_error);
}

TEST_F(StorageSnapshotFileTest, writer_orphan)
{
    // expect warning
    Core::Debug::setLogPolicy({
        {"Storage/SnapshotFile.cc", "ERROR"}
    });
    {
        Writer writer(layout);
        writer.getStream().WriteVarint32(482);
    }
    std::vector<std::string> files = FilesystemUtil::ls(layout.serverDir);
    EXPECT_EQ(1U, files.size()) <<
        Core::StringUtil::join(files, " ");
    std::string file = files.at(0);
    EXPECT_TRUE(Core::StringUtil::startsWith(file, "partial")) << file;
}

TEST_F(StorageSnapshotFileTest, writer_discard)
{
    {
        Writer writer(layout);
        writer.getStream().WriteVarint32(482);
        writer.discard();
    }
    EXPECT_EQ(0U, FilesystemUtil::ls(layout.serverDir).size());
}

TEST_F(StorageSnapshotFileTest, writer_flushToOS_continue)
{
    {
        Writer writer(layout);
        writer.getStream().WriteVarint32(482);
        writer.flushToOS();
        writer.getStream().WriteVarint32(998);
        writer.save();
    }
    {
        Reader reader(layout);
        uint32_t x = 0;
        reader.getStream().ReadVarint32(&x);
        EXPECT_EQ(482U, x);
        reader.getStream().ReadVarint32(&x);
        EXPECT_EQ(998U, x);
    }
}

TEST_F(StorageSnapshotFileTest, writer_flushToOS_forking)
{
    {
        Writer writer(layout);
        writer.getStream().WriteVarint32(482);
        writer.flushToOS();
        pid_t pid = fork();
        ASSERT_LE(0, pid);
        if (pid == 0) { // child
            writer.getStream().WriteVarint32(127);
            writer.flushToOS();
            _exit(0);
        } else { // parent
            int status = 0;
            pid = waitpid(pid, &status, 0);
            EXPECT_LE(0, pid);
            EXPECT_TRUE(WIFEXITED(status));
            EXPECT_EQ(0, WEXITSTATUS(status));
            writer.getStream().WriteVarint32(998);
            writer.save();
        }
    }
    {
        Reader reader(layout);
        uint32_t x = 0;
        reader.getStream().ReadVarint32(&x);
        EXPECT_EQ(482U, x);
        reader.getStream().ReadVarint32(&x);
        EXPECT_EQ(127U, x);
        reader.getStream().ReadVarint32(&x);
        EXPECT_EQ(998U, x);
    }
}

} // namespace LogCabin::Storage::SnapshotFile::<anonymous>
} // namespace LogCabin::Storage::SnapshotFile
} // namespace LogCabin::Storage
} // namespace LogCabin
