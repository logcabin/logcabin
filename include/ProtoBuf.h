/* Copyright (c) 2012 Stanford University
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

#include <google/protobuf/message.h>
#include <google/protobuf/text_format.h>
#include <stdexcept>
#include <string>

#include "Common.h"

/**
 * \file
 * Utilities for dealing with protocol buffers.
 */

#ifndef PROTOBUF_H
#define PROTOBUF_H

namespace google {
namespace protobuf {

/**
 * Equality for protocol buffers so that they can be used in EXPECT_EQ.
 * This is useful for testing.
 */
bool operator==(const Message& a, const Message& b);

/**
 * Inequality for protocol buffers so that they can be used in EXPECT_NE.
 * This is useful for testing.
 */
bool operator!=(const Message& a, const Message& b);

// Equality and inequality between protocol buffers and their text format
// representations. These are useful for testing.
bool operator==(const Message& a, const std::string& b);
bool operator==(const std::string& a, const Message& b);
bool operator!=(const Message& a, const std::string& b);
bool operator!=(const std::string& a, const Message& b);

} // namespace google::protobuf
} // namespace google

namespace DLog {
namespace ProtoBuf {

namespace Internal {

/// Helper for fromString template.
void fromString(const std::string& str, google::protobuf::Message& protoBuf);

} // namespace DLog::ProtoBuf::Internal

/**
 * Create a protocol buffer message form a text format.
 * This is useful for testing.
 * \tparam ProtoBuf
 *      A derived class of ProtoBuf::Message.
 * \param str
 *      The string representation of the protocol buffer message.
 * \return
 *      The parsed protocol buffer.
 * \throw std::runtime_error
 *      The given string does not define all the required fields for the
 *      protocol buffer.
 */
template<typename ProtoBuf>
ProtoBuf
fromString(const std::string& str)
{
    ProtoBuf protoBuf;
    Internal::fromString(str, protoBuf);
    return protoBuf;
}

/**
 * Dumps a protocol buffer message.
 * This is useful for debugging and for testing.
 *
 * \param protoBuf
 *      The protocol buffer message to dump out. It is safe to call this even
 *      if you haven't filled in all required fields, but the generated string
 *      will not be directly parse-able.
 * \param forCopyingIntoTest
 *      If set to true (default), this will return a string in a format most
 *      useful for writing unit tests. You can basically copy and paste this
 *      from your terminal into your test file without manual processing. If
 *      set to false, the output will be nicer to read but harder to copy into
 *      a test file.
 * \return
 *      Textual representation. This will be printable ASCII; binary will be
 *      escaped.
 */
std::string
dumpString(const google::protobuf::Message& protoBuf,
           bool forCopyingIntoTest = true);

} // namespace DLog::ProtoBuf
} // namespace DLog

#endif /* PROTOBUF_H */
