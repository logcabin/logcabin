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

#include "DLogClient.h"
#include "Ref.h"
#include "ProtoBuf.h"
#include "../build/proto/dlog.pb.h"

#ifndef LIBDLOGCLIENT_CLIENTIMPL_H
#define LIBDLOGCLIENT_CLIENTIMPL_H

namespace DLog {
namespace Client {
namespace Internal {

/**
 * The implementation of the client library.
 * This is wrapped by the classes in DLogClient.
 */
class ClientImpl {
    ClientImpl();
  public:
    /// See Cluster::registerErrorCallback.
    void registerErrorCallback(std::unique_ptr<ErrorCallback> callback);
    /// See Cluster::openLog.
    Log openLog(const std::string& logName);
    /// See Cluster::deleteLog.
    void deleteLog(const std::string& logName);
    /// See Cluster::listLogs.
    std::vector<std::string> listLogs();
    /// See Log::append and Log::invalidate.
    EntryId append(uint64_t logId, const Entry& entry, EntryId previousId);
    /// See Log::read.
    std::vector<Entry> read(uint64_t logId, EntryId from);
    /// See Log::getLastId.
    EntryId getLastId(uint64_t logId);
  private:
    RefHelper<ClientImpl>::RefCount refCount;
    std::unique_ptr<ErrorCallback> errorCallback;
    friend class DLog::RefHelper<ClientImpl>;
    friend class DLog::MakeHelper;
};

/**
 * This is a placeholder for an actual RPC system.
 */
class PlaceholderRPC {
  public:
    typedef ProtoBuf::ClientRPC::OpCode OpCode;
    virtual ~PlaceholderRPC() {}
    /**
     * Send and receive an RPC to server.
     * This interface ignores any sort of network/host failures that might
     * occur.
     */
    virtual void leader(OpCode opCode,
                        const google::protobuf::Message& request,
                        google::protobuf::Message& response) = 0;
};
extern PlaceholderRPC* placeholderRPC;


} // namespace DLog::Client::Internal
} // namespace DLog::Client
} // namespace DLog

#endif /* LIBDLOGCLIENT_CLIENTIMPL_H */
