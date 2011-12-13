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

/**
 * \file
 * Common utilities and definitions.
 */

#include <cassert>
#include <cinttypes>
#include <memory>

#ifndef COMMON_H
#define COMMON_H

namespace DLog {

/**
 * Construct a new object and wrap it in a unique_ptr.
 *
 * For example:
 *      unique<int>(4)
 * is the same as:
 *      std::unique_ptr<int>(new int(4))
 *
 * \tparam T
 *      The type to construct.
 * \param args
 *      Arguments to T's constructor.
 * \return
 *      A new T(args) that is wrapped in a unique_ptr<T>.
 */
template<typename T, typename... Args>
std::unique_ptr<T>
unique(Args&&... args) {
    return std::unique_ptr<T>(new T(static_cast<Args&&>(args)...));
}

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

} // namespace DLog

#endif /* COMMON_H */
