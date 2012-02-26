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

#include <cinttypes>
#include <google/protobuf/message.h>

#include "RPC/Buffer.h"

#ifndef LOGCABIN_RPC_PROTOBUF_H
#define LOGCABIN_RPC_PROTOBUF_H

namespace LogCabin {
namespace RPC {
namespace ProtoBuf {

/**
 * Parse a protocol buffer message out of an RPC::Buffer.
 * \param from
 *      The RPC::Buffer from which to extract a protocol buffer.
 * \param[out] to
 *      The empty protocol buffer to fill in with the contents of the
 *      RPC::Buffer.
 * \param skipBytes
 *      The number of bytes to skip at the beginning of 'from' (defaults to 0).
 * \return
 *      True if the protocol buffer was parsed successfully; false otherwise
 *      (for example, if a required field is missing).
 */
bool
parse(const RPC::Buffer& from,
      google::protobuf::Message& to,
      uint32_t skipBytes = 0);

/**
 * Serialize a protocol buffer message into an RPC::Buffer.
 * \param from
 *      The protocol buffer containing the contents to serialize into the
 *      RPC::Buffer. All required fields must be set or this will PANIC.
 * \param[out] to
 *      The RPC::Buffer to fill in with the contents of the protocol buffer.
 * \param skipBytes
 *      The number of bytes to allocate at the beginning of 'to' but leave
 *      uninitialized for someone else to fill in (defaults to 0).
 */
void
serialize(const google::protobuf::Message& from,
          RPC::Buffer& to,
          uint32_t skipBytes = 0);

} // namespace LogCabin::RPC::ProtoBuf
} // namespace LogCabin::RPC
} // namespace LogCabin

#endif // LOGCABIN_RPC_PROTOBUF_H
