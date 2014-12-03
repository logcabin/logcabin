/* Copyright (c) 2012 Stanford University
 * Copyright (c) 2014 Diego Ongaro
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

#include <gtest/gtest.h>

#include "Core/Debug.h"
#include "Core/STLUtil.h"

namespace LogCabin {
namespace Core {
namespace Debug {

namespace Internal {
extern const char* logLevelToString[];
extern std::unordered_map<const char*, LogLevel> isLoggingCache;
LogLevel logLevelFromString(const std::string& level);
LogLevel getLogLevel(const char* fileName);
const char* relativeFileName(const char* fileName);
}

namespace {

class CoreDebugTest : public ::testing::Test {
  public:
    CoreDebugTest() {
        setLogPolicy({});
    }
};


TEST_F(CoreDebugTest, logLevelToString) {
    EXPECT_STREQ("SILENT",
                 Internal::logLevelToString[uint32_t(LogLevel::SILENT)]);
    EXPECT_STREQ("ERROR",
                 Internal::logLevelToString[uint32_t(LogLevel::ERROR)]);
    EXPECT_STREQ("WARNING",
                 Internal::logLevelToString[uint32_t(LogLevel::WARNING)]);
    EXPECT_STREQ("NOTICE",
                 Internal::logLevelToString[uint32_t(LogLevel::NOTICE)]);
    EXPECT_STREQ("VERBOSE",
                 Internal::logLevelToString[uint32_t(LogLevel::VERBOSE)]);
}

TEST_F(CoreDebugTest, logLevelFromString) {
    EXPECT_EQ(LogLevel::SILENT,
              Internal::logLevelFromString("SILeNT"));
    EXPECT_EQ(LogLevel::ERROR,
              Internal::logLevelFromString("ERrOR"));
    EXPECT_EQ(LogLevel::WARNING,
              Internal::logLevelFromString("WARNiNG"));
    EXPECT_EQ(LogLevel::NOTICE,
              Internal::logLevelFromString("NOTIcE"));
    EXPECT_EQ(LogLevel::VERBOSE,
              Internal::logLevelFromString("VERBOsE"));
    EXPECT_DEATH(Internal::logLevelFromString("asdlf"),
                 "'asdlf' is not a valid log level.");
}

TEST_F(CoreDebugTest, getLogLevel) {
    // verify default is NOTICE
    EXPECT_EQ(LogLevel::NOTICE, Internal::getLogLevel(__FILE__));

    setLogPolicy({{"prefix", "VERBOSE"},
                  {"suffix", "ERROR"},
                  {"", "WARNING"}});
    EXPECT_EQ(LogLevel::VERBOSE, Internal::getLogLevel("prefixabcsuffix"));
    EXPECT_EQ(LogLevel::ERROR, Internal::getLogLevel("abcsuffix"));
    EXPECT_EQ(LogLevel::WARNING, Internal::getLogLevel("asdf"));
}

TEST_F(CoreDebugTest, relativeFileName) {
    EXPECT_STREQ("Core/DebugTest.cc",
                 Internal::relativeFileName(__FILE__));
    EXPECT_STREQ("/a/b/c",
                 Internal::relativeFileName("/a/b/c"));
}

TEST_F(CoreDebugTest, isLogging) {
    EXPECT_TRUE(isLogging(LogLevel::ERROR, "abc"));
    EXPECT_TRUE(isLogging(LogLevel::ERROR, "abc"));
    EXPECT_FALSE(isLogging(LogLevel::VERBOSE, "abc"));
    EXPECT_EQ((std::vector<std::pair<const char*, LogLevel>> {
                    { "abc", LogLevel::NOTICE },
              }),
              STLUtil::getItems(Internal::isLoggingCache));
}

TEST_F(CoreDebugTest, setLogFile) {
    EXPECT_EQ(stderr, setLogFile(stdout));
    EXPECT_EQ(stdout, setLogFile(stderr));
}

// log: low cost-benefit in testing

} // namespace LogCabin::Core::Debug::<anonymous>
} // namespace LogCabin::Core::Debug
} // namespace LogCabin::Core
} // namespace LogCabin

