/* Copyright (c) 2011-2012 Stanford University
 *
 * Copyright (c) 2011 Facebook
 *    startsWith() and endsWith() functions
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

#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstring>
#include <sstream>

#include "Core/StringUtil.h"

namespace LogCabin {
namespace Core {
namespace StringUtil {

namespace {

/**
 * Returns true for the ASCII characters that one would want to display in a
 * single line of text.
 */
bool
display(char c)
{
    return (32 <= c && c < 127);
}

} // anonymous namespace

// This comes from the RAMCloud project.
std::string
format(const char* format, ...)
{
    std::string s;
    va_list ap;
    va_start(ap, format);

    // We're not really sure how big of a buffer will be necessary.
    // Try 1K, if not the return value will tell us how much is necessary.
    int bufSize = 1024;
    while (true) {
        char buf[bufSize];
        // vsnprintf trashes the va_list, so copy it first
        va_list aq;
        __va_copy(aq, ap);
        int r = vsnprintf(buf, bufSize, format, aq);
        assert(r >= 0); // old glibc versions returned -1
        if (r < bufSize) {
            s = buf;
            break;
        }
        bufSize = r + 1;
    }

    va_end(ap);
    return s;
}

bool
isPrintable(const char* str)
{
    return isPrintable(str, strlen(str) + 1);
}

bool
isPrintable(const void* data, size_t length)
{
    const char* begin = static_cast<const char*>(data);
    const char* end = begin + length - 1;
    return (length >= 1 &&
            *end == '\0' &&
            std::all_of(begin, end, display));
}

void
replaceAll(std::string& haystack,
           const std::string& needle,
           const std::string& replacement)
{
    size_t startPos = 0;
    while (true) {
        size_t replacePos = haystack.find(needle, startPos);
        if (replacePos == haystack.npos)
            return;
        haystack.replace(replacePos, needle.length(), replacement);
        startPos = replacePos + replacement.length();
    }
}

std::vector<std::string>
split(const std::string& subject, char delimiter)
{
    std::vector<std::string> items;
    std::istringstream stream(subject);
    std::string item;
    while (std::getline(stream, item, delimiter))
        items.push_back(std::move(item));
    return items;
}

bool
startsWith(const std::string& haystack, const std::string& needle)
{
    return (haystack.compare(0, needle.length(), needle) == 0);
}

bool
endsWith(const std::string& haystack, const std::string& needle)
{
    if (haystack.length() < needle.length())
        return false;
    return (haystack.compare(haystack.length() - needle.length(),
                             needle.length(), needle) == 0);
}

} // namespace LogCabin::Core::StringUtil
} // namespace LogCabin::Core
} // namespace LogCabin
