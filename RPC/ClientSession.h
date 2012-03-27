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

#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "Event/Timer.h"
#include "RPC/Address.h"
#include "RPC/Buffer.h"
#include "RPC/OpaqueClientRPC.h"
#include "RPC/MessageSocket.h"

#ifndef LOGCABIN_RPC_CLIENTSESSION_H
#define LOGCABIN_RPC_CLIENTSESSION_H

namespace LogCabin {

// forward declaration
namespace Event {
class Loop;
};

namespace RPC {

/**
 * A ClientSession is used to initiate OpaqueClientRPCs. It encapsulates a
 * connection to a server. Sessions can be relatively expensive to create, so
 * clients should keep them around.
 */
class ClientSession {
  private:
    /**
     * This constructor is private because the class must be allocated in a
     * particular way. See #makeSession().
     */
    ClientSession(Event::Loop& eventLoop,
                  const Address& address,
                  uint32_t maxMessageLength);
  public:
    /**
     * Return a new ClientSession object. This object is managed by a
     * std::shared_ptr to ensure that it remains alive while there are
     * outstanding RPCs.
     *
     * This should only be used from worker threads, as it invokes possibly
     * long-running syscalls.
     *
     * \param eventLoop
     *      Event::Loop that will be used to find out when the underlying
     *      socket may be read from or written to without blocking.
     * \param address
     *      The RPC server address on which to connect.
     * \param maxMessageLength
     *      The maximum number of bytes to allow per request/response. This
     *      exists to limit the amount of buffer space a single RPC can use.
     *      Attempting to send longer requests will PANIC; attempting to
     *      receive longer requests will disconnect the underlying socket.
     */
    static std::shared_ptr<ClientSession>
    makeSession(Event::Loop& eventLoop,
                const Address& address,
                uint32_t maxMessageLength);

    /**
     * Destructor.
     */
    ~ClientSession();

    /**
     * Initiate an RPC.
     * This method is safe to call from any thread.
     * \param request
     *      The contents of the RPC request.
     * \return
     *      This is be used to wait for and retrieve the reply to the RPC.
     */
    OpaqueClientRPC sendRequest(Buffer request);

    /**
     * If the socket has been disconnected, return a descriptive message.
     * The suggested way to detect errors is to wait until an RPC returns an
     * error. This method can be used to detect errors earlier.
     *
     * This method is safe to call from any thread.
     *
     * \return
     *      If an error has occurred, a message describing that error.
     *      Otherwise, an empty string.
     */
    std::string getErrorMessage() const;

  private:

    /**
     * This is a MessageSocket with callbacks set up for ClientSession.
     */
    class ClientMessageSocket : public MessageSocket {
      public:
        /**
         * Constructor.
         * \param clientSession
         *      ClientSession owning this socket.
         * \param fd
         *      A connected TCP socket.
         * \param maxMessageLength
         *      See MessageSocket's constructor.
         */
        ClientMessageSocket(ClientSession& clientSession,
                            int fd,
                            uint32_t maxMessageLength);
        void onReceiveMessage(MessageId messageId, Buffer message);
        void onDisconnect();
        ClientSession& session;
    };

    /**
     * This contains an expected response for a OpaqueClientRPC object.
     * This is created when the OpaqueClientRPC is created; it is deleted when
     * the OpaqueClientRPC object is either canceled or updated with a
     * response/error.
     */
    struct Response {
        /// Constructor.
        Response();
        /// True indicates #reply is valid.
        bool ready;
        /// The contents of the response. This is valid when #ready is set.
        Buffer reply;
    };

    /**
     * This is used to time out RPCs and sessions when the server is no longer
     * responding. After a timeout period, the client will send a ping to the
     * server. If no response is received within another timeout period, the
     * session is closed.
     */
    class Timer : public Event::Timer {
      public:
        explicit Timer(ClientSession& session);
        void handleTimerEvent();
        ClientSession& session;
    };

    // The cancel(), update(), and wait() methods are used by OpaqueClientRPC.
    friend class OpaqueClientRPC;

    /**
     * Called by the RPC when it is no longer interested in its response.
     *
     * TODO(ongaro): It'd be nice to cancel sending the request if it hasn't
     * already gone out, but I guess that's going to be a pretty rare case.
     */
    void cancel(OpaqueClientRPC& rpc);

    /**
     * Called by the RPC when it wants to be learn of its response
     * (non-blocking). This is the non-blocking version of wait().
     */
    void update(OpaqueClientRPC& rpc);

    /**
     * Called by the RPC when it wants to wait for its response (blocking).
     * This is the blocking version of update().
     */
    void wait(OpaqueClientRPC& rpc);

    /**
     * This is used to keep this object alive while there are outstanding RPCs.
     */
    std::weak_ptr<ClientSession> self;

    /**
     * The event loop that is used for non-blocking I/O.
     */
    Event::Loop& eventLoop;

    /**
     * The MessageSocket used to send RPC requests and receive RPC responses.
     * This may be NULL if the socket was never created. In this case,
     * #errorMessage will be set.
     */
    std::unique_ptr<ClientMessageSocket> messageSocket;

    /**
     * This is used to time out RPCs and sessions when the server is no longer
     * responding. See Timer.
     */
    Timer timer;

    /**
     * This mutex protects all of the members of this class defined below this
     * point.
     */
    mutable std::mutex mutex;

    /**
     * The message ID to assign to the next RPC. These start at 1 and
     * increment from there; the value 0 is reserved for ping messages to check
     * server liveness.
     */
    MessageSocket::MessageId nextMessageId;

    /**
     * OpaqueClientRPC objects wait on this condition variable inside of
     * wait(). It is notified when a new response arrives or the session is
     * disconnected.
     */
    std::condition_variable responseReceived;

    /**
     * A map from MessageId to Response objects that is used to store the
     * response to RPCs and look it up for OpaqueClientRPC objects. The
     * Response objects mapped to must be deleted manually when removed from
     * this map (gcc 4.4 doesn't support mapping to non-copyable objects).
     */
    std::unordered_map<MessageSocket::MessageId, Response*> responses;

    /**
     * If this session is disconnected then this holds the error message.
     * All new RPCs will be immediately 'ready' with this error message.
     * Otherwise, this is the empty string.
     */
    std::string errorMessage;

    /**
     * The number of outstanding RPC requests that have been sent but whose
     * responses have not yet been received. This does not include ping
     * requests sent by the #timer (which aren't real RPCs).
     */
    uint32_t numActiveRPCs;

    /**
     * When numActiveRPCs is > 0, this field indicates that we are waiting for
     * a ping response as evidence that the server is still alive.
     * When numActiveRPCs = 0, this field is undefined.
     */
    bool activePing;

    // ClientSession is non-copyable.
    ClientSession(const ClientSession&) = delete;
    ClientSession& operator=(const ClientSession&) = delete;
}; // class ClientSession

} // namespace LogCabin::RPC
} // namespace LogCabin

#endif /* LOGCABIN_RPC_CLIENTSESSION_H */
