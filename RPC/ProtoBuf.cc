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

#include <string>

#include "include/Debug.h"
#include "Core/ProtoBuf.h"
#include "RPC/ProtoBuf.h"

namespace LogCabin {
namespace RPC {
/**
 * Utilities for dealing with protocol buffers in RPCs.
 */
namespace ProtoBuf {

namespace {

/// Remove the last character from the end of a string.
std::string
truncateEnd(std::string str)
{
    if (!str.empty())
        str.resize(str.length() - 1, 0);
    return str;
}

} // anonymous namespace

bool
parse(const RPC::Buffer& from,
      google::protobuf::Message& to,
      uint32_t skipBytes)
{
    if (!to.ParseFromArray(
                        static_cast<const char*>(from.getData()) + skipBytes,
                        from.getLength() - skipBytes)) {
        LOG(WARNING, "Missing fields in protocol buffer: %s",
            to.InitializationErrorString().c_str());
        return false;
    }
    LOG(DBG, "%s:\n%s",
        to.GetTypeName().c_str(),
        truncateEnd(Core::ProtoBuf::dumpString(to)).c_str());
    return true;
}

void
serialize(const google::protobuf::Message& from,
          RPC::Buffer& to,
          uint32_t skipBytes)
{
    // SerializeToArray seems to always return true, so we explicitly check
    // IsInitialized to make sure all required fields are set.
    if (!from.IsInitialized()) {
        PANIC("Missing fields in protocol buffer of type %s: %s",
              from.GetTypeName().c_str(),
              from.InitializationErrorString().c_str());
    }
    uint32_t length = from.ByteSize();
    char* data = new char[skipBytes + length];
    from.SerializeToArray(data + skipBytes, length);
    to.setData(data, skipBytes + length, RPC::Buffer::deleteArrayFn<char>);
}

} // namespace LogCabin::RPC::ProtoBuf
} // namespace LogCabin::RPC
} // namespace LogCabin
