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

#include "Core/Debug.h"
#include "Core/ProtoBuf.h"
#include "Core/STLUtil.h"
#include "Client/MockClientImpl.h"
#include "Tree/ProtoBuf.h"

namespace LogCabin {
namespace Client {

namespace PC = LogCabin::Protocol::Client;

namespace {

template<typename T>
struct RAIISwap {
    RAIISwap(T& a, T& b)
        : a(a)
        , b(b)
    {
        std::swap(a, b);
    }
    ~RAIISwap()
    {
        std::swap(a, b);
    }
    T& a;
    T& b;
};

typedef RAIISwap<std::shared_ptr<TestingCallbacks>> CallbackSwap;

/**
 * This class intercepts LeaderRPC calls from ClientImpl.
 * It's used to mock out the Tree RPCs by processing them directly.
 */
class TreeLeaderRPC : public LeaderRPCBase {
  public:
    explicit TreeLeaderRPC(MockClientImpl& mockClientImpl)
        : mockClientImpl(mockClientImpl)
        , mutex()
        , tree()
    {
    }
    Status call(OpCode opCode,
              const google::protobuf::Message& request,
              google::protobuf::Message& response,
              TimePoint timeout) {
        if (opCode == OpCode::OPEN_SESSION) {
            PC::OpenSession::Response& openSessionResponse =
                static_cast<PC::OpenSession::Response&>(response);
            openSessionResponse.set_client_id(1);
        } else if (opCode == OpCode::READ_ONLY_TREE) {
            std::unique_lock<std::recursive_mutex> lockGuard(mutex);
            PC::ReadOnlyTree::Request trequest;
            trequest.CopyFrom(request);
            auto& tresponse =
                static_cast<PC::ReadOnlyTree::Response&>(response);
            auto callbacks = std::make_shared<TestingCallbacks>();
            // set to noop for recursive calls, then restore
            CallbackSwap s(mockClientImpl.callbacks, callbacks);
            if (!callbacks->readOnlyTreeRPC(trequest, tresponse)) {
                tresponse.Clear();
                LogCabin::Tree::ProtoBuf::readOnlyTreeRPC(
                    tree, trequest, tresponse);
            }
        } else if (opCode == OpCode::READ_WRITE_TREE) {
            std::unique_lock<std::recursive_mutex> lockGuard(mutex);
            PC::ReadWriteTree::Request trequest;
            trequest.CopyFrom(request);
            auto& tresponse =
                static_cast<PC::ReadWriteTree::Response&>(response);
            auto callbacks = std::make_shared<TestingCallbacks>();
            // set to noop for recursive calls, then restore
            CallbackSwap s(mockClientImpl.callbacks, callbacks);
            if (!callbacks->readWriteTreeRPC(trequest, tresponse)) {
                tresponse.Clear();
                LogCabin::Tree::ProtoBuf::readWriteTreeRPC(
                    tree, trequest, tresponse);
            }
        } else {
            PANIC("Unexpected request: %d %s",
                  opCode,
                  Core::ProtoBuf::dumpString(request).c_str());
        }
        return Status::OK;
    }

    class Call : public LeaderRPCBase::Call {
      public:
        explicit Call(TreeLeaderRPC& leaderRPC)
            : leaderRPC(leaderRPC)
            , opCode()
            , request()
        {
        }
        void start(OpCode _opCode,
                   const google::protobuf::Message& _request,
                   TimePoint _timeout) {
            opCode = _opCode;
            request.reset(_request.New());
            request->CopyFrom(_request);
        }
        void cancel() {
            // no op
        }
        Status wait(google::protobuf::Message& response,
                    TimePoint timeout) {
            leaderRPC.call(opCode, *request, response, timeout);
            return Status::OK;
        }
        TreeLeaderRPC& leaderRPC;
        OpCode opCode;
        std::unique_ptr<google::protobuf::Message> request;

        // Call is not copyable.
        Call(const Call&) = delete;
        Call& operator=(const Call&) = delete;
    };

    std::unique_ptr<LeaderRPCBase::Call> makeCall() {
        return std::unique_ptr<LeaderRPCBase::Call>(new Call(*this));
    }

    MockClientImpl& mockClientImpl;
    /**
     * Prevents concurrent access to 'tree'. It's recursive so that you can
     * call the client library from within MockCallbacks, if you're that
     * insane.
     */
    std::recursive_mutex mutex;
    LogCabin::Tree::Tree tree;
};
} // anonymous namespace

MockClientImpl::MockClientImpl(std::shared_ptr<TestingCallbacks> callbacks)
    : callbacks(callbacks)
{
}

MockClientImpl::~MockClientImpl()
{
}

void
MockClientImpl::initDerived()
{
    leaderRPC.reset(new TreeLeaderRPC(*this));
}

std::pair<uint64_t, Configuration>
MockClientImpl::getConfiguration()
{
    return {0, {}};
}

ConfigurationResult
MockClientImpl::setConfiguration(uint64_t oldId,
                                 const Configuration& newConfiguration)
{
    ConfigurationResult result;
    result.status = ConfigurationResult::BAD;
    result.badServers = newConfiguration;
    return result;
}

} // namespace LogCabin::Client
} // namespace LogCabin
