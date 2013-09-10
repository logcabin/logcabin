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
#include "Server/SnapshotFile.h"

namespace LogCabin {
namespace Server {
namespace SnapshotFile {
namespace {

namespace FilesystemUtil = Storage::FilesystemUtil;

class ServerSnapshotFileTest : public ::testing::Test {
  public:
    ServerSnapshotFileTest()
        : tmpdir()
    {
        std::string path = FilesystemUtil::mkdtemp();
        tmpdir = FilesystemUtil::File(open(path.c_str(), O_RDONLY|O_DIRECTORY),
                                      path);
    }
    ~ServerSnapshotFileTest() {
        tmpdir.close();
        FilesystemUtil::remove(tmpdir.path);
    }
    FilesystemUtil::File tmpdir;
};


TEST_F(ServerSnapshotFileTest, basic)
{
    {
        Writer writer(tmpdir);
        writer.getStream().WriteVarint32(482);
        writer.save();
    }
    {
        Reader reader(tmpdir);
        uint32_t x = 0;
        reader.getStream().ReadVarint32(&x);
        EXPECT_EQ(482U, x);
    }
}

TEST_F(ServerSnapshotFileTest, reader_fileNotFound)
{
    EXPECT_THROW(Reader reader(tmpdir), std::runtime_error);
}

TEST_F(ServerSnapshotFileTest, writer_orphan)
{
    // expect warning
    Core::Debug::setLogPolicy({
        {"Server/SnapshotFile.cc", "ERROR"}
    });
    {
        Writer writer(tmpdir);
        writer.getStream().WriteVarint32(482);
    }
    std::vector<std::string> files = FilesystemUtil::ls(tmpdir);
    EXPECT_EQ(1U, files.size());
    std::string file = files.at(0);
    EXPECT_TRUE(Core::StringUtil::startsWith(file, "partial")) << file;
}

TEST_F(ServerSnapshotFileTest, writer_discard)
{
    {
        Writer writer(tmpdir);
        writer.getStream().WriteVarint32(482);
        writer.discard();
    }
    EXPECT_EQ(0U, FilesystemUtil::ls(tmpdir).size());
}

TEST_F(ServerSnapshotFileTest, writer_flushToOS_continue)
{
    {
        Writer writer(tmpdir);
        writer.getStream().WriteVarint32(482);
        writer.flushToOS();
        writer.getStream().WriteVarint32(998);
        writer.save();
    }
    {
        Reader reader(tmpdir);
        uint32_t x = 0;
        reader.getStream().ReadVarint32(&x);
        EXPECT_EQ(482U, x);
        reader.getStream().ReadVarint32(&x);
        EXPECT_EQ(998U, x);
    }
}

TEST_F(ServerSnapshotFileTest, writer_flushToOS_forking)
{
    {
        Writer writer(tmpdir);
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
        Reader reader(tmpdir);
        uint32_t x = 0;
        reader.getStream().ReadVarint32(&x);
        EXPECT_EQ(482U, x);
        reader.getStream().ReadVarint32(&x);
        EXPECT_EQ(127U, x);
        reader.getStream().ReadVarint32(&x);
        EXPECT_EQ(998U, x);
    }
}

} // namespace LogCabin::Server::SnapshotFile::<anonymous>
} // namespace LogCabin::Server::SnapshotFile
} // namespace LogCabin::Server
} // namespace LogCabin
