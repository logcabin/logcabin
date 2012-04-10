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
#include <memory>
#include <stdexcept>
#include <string>

#include "RPC/Buffer.h"

#ifndef LOGCABIN_RPC_OPAQUECLIENTRPC_H
#define LOGCABIN_RPC_OPAQUECLIENTRPC_H

namespace LogCabin {
namespace RPC {

class ClientSession; // forward declaration

/**
 * This class represents an asynchronous remote procedure call. A ClientSession
 * returns an instance when an RPC is initiated; this can be used to wait for
 * and retrieve the reply.
 */
class OpaqueClientRPC {
  public:
    /**
     * This may be thrown by #extractReply.
     */
    struct Error : public std::runtime_error {
        explicit Error(const std::string& message)
            : std::runtime_error(message) {}
    };

    /**
     * Default constructor. This doesn't create a valid RPC, but it is useful
     * as a placeholder.
     */
    OpaqueClientRPC();

    /**
     * Move constructor.
     */
    OpaqueClientRPC(OpaqueClientRPC&&);

    /**
     * Destructor.
     */
    ~OpaqueClientRPC();

    /**
     * Move assignment.
     */
    OpaqueClientRPC& operator=(OpaqueClientRPC&&);

    /**
     * Abort the RPC.
     * The caller is no longer interested in its reply.
     */
    void cancel();

    /**
     * Destructively return the RPC's response.
     *
     * This will wait for the RPC response to arrive (if it hasn't already) and
     * throw an exception if there were any problems. If the reply is received
     * successfully, this will return the reply, leaving the RPC with an empty
     * reply buffer.
     *
     * This may be used from worker threads only, because OpaqueClientRPC
     * objects rely on the event loop servicing their ClientSession in order to
     * make progress.
     *
     * \return
     *      The reply, leaving the RPC with an empty reply buffer.
     *
     * \throw Error
     *      There was an error executing the RPC. See #getErrorMessage().
     */
    Buffer extractReply();

    /**
     * If an error has occurred, return a message describing that error.
     *
     * All errors indicate that it is unknown whether or not the server
     * executed the RPC. Unless the RPC was canceled with #cancel(), the
     * ClientSession has been disconnected and is no longer useful for
     * initiating new RPCs.
     *
     * \return
     *      If an error has occurred, a message describing that error.
     *      Otherwise, an empty string.
     */
    std::string getErrorMessage() const;

    /**
     * Indicate whether a response or error has been received for
     * the RPC.
     * \return
     *      True means the reply is ready or an error has occurred.
     */
    bool isReady();

    /**
     * Look at the reply buffer.
     *
     * \return
     *      If the reply is already available and there were no errors, returns
     *      a pointer to the reply buffer inside this OpaqueClientRPC object.
     *      Otherwise, returns NULL.
     */
    Buffer* peekReply();

    /**
     * Block until the reply is ready or an error has occurred.
     *
     * This may be used from worker threads only, because OpaqueClientRPC
     * objects rely on the event loop servicing their ClientSession in order to
     * make progress.
     */
    void waitForReply();

  private:

    /**
     * Update the fields of this object if the RPC has not completed.
     */
    void update();

    /**
     * The session on which this RPC is executing.
     * The session itself will reset this field once the reply has been
     * received to eagerly drop its own reference count.
     */
    std::shared_ptr<ClientSession> session;

    /**
     * A token given to the session to look up new information about the
     * progress of this RPC's reply.
     */
    uint64_t responseToken;

    /**
     * True means that the RPC has completed (either with or without an
     * error), so the next call to waitForReply() should return immediately.
     */
    bool ready;

    /**
     * The payload of a successful reply, once available.
     * This becomes valid when #ready is set and #errorMessage is empty.
     * Then, extractReply() may later reset this buffer.
     */
    Buffer reply;

    /**
     * If an error occurred in the RPC then this holds the error message;
     * otherwise, this is the empty string.
     */
    std::string errorMessage;

    // The ClientSession class fills in the members of this object.
    friend class ClientSession;

    // OpaqueClientRPC is non-copyable.
    OpaqueClientRPC(const OpaqueClientRPC&) = delete;
    OpaqueClientRPC& operator=(const OpaqueClientRPC&) = delete;

}; // class OpaqueClientRPC

} // namespace LogCabin::RPC
} // namespace LogCabin

#endif /* LOGCABIN_RPC_OPAQUECLIENTRPC_H */
