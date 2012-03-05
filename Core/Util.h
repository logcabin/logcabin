/* Copyright (c) 2011-2012 Stanford University
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

/**
 * \file
 * Common utilities and definitions.
 * See also Core::STLUtil and Core::StringUtil.
 */

#include <cassert>
#include <cinttypes>
#include <string>
#include <vector>

#ifndef LOGCABIN_CORE_UTIL_H
#define LOGCABIN_CORE_UTIL_H

namespace LogCabin {
namespace Core {
namespace Util {

/**
 * Cast a bigger int down to a smaller one.
 * Asserts that no precision is lost at runtime.
 */
// This was taken from the RAMCloud project.
template<typename Small, typename Large>
Small
downCast(const Large& large)
{
    Small small = static_cast<Small>(large);
    // The following comparison (rather than "large==small") allows
    // this method to convert between signed and unsigned values.
    assert(large - small == 0);
    return small;
}

/// Like sizeof but returns a uint32_t.
#define sizeof32(x) LogCabin::Core::Util::downCast<uint32_t>(sizeof(x))

} // namespace LogCabin::Core::Util
} // namespace LogCabin::Core
} // namespace LogCabin

#endif /* LOGCABIN_CORE_UTIL_H */
