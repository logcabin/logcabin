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

#include "Core/Mutex.h"
#include "RPC/ClientSession.h"
#include "RPC/OpaqueClientRPC.h"

namespace LogCabin {
namespace RPC {

OpaqueClientRPC::OpaqueClientRPC()
    : mutex()
    , session()
    , responseToken(~0UL)
    , ready(false)
    , reply()
    , errorMessage()
{
}

OpaqueClientRPC::OpaqueClientRPC(OpaqueClientRPC&& other)
    : mutex()
    , session(std::move(other.session))
    , responseToken(std::move(other.responseToken))
    , ready(std::move(other.ready))
    , reply(std::move(other.reply))
    , errorMessage(std::move(other.errorMessage))
{
}

OpaqueClientRPC::~OpaqueClientRPC()
{
    cancel();
}

OpaqueClientRPC&
OpaqueClientRPC::operator=(OpaqueClientRPC&& other)
{
    std::unique_lock<std::mutex> mutexGuard(mutex);
    session = std::move(other.session);
    responseToken = std::move(other.responseToken);
    ready = std::move(other.ready);
    reply = std::move(other.reply);
    errorMessage = std::move(other.errorMessage);
    return *this;
}

void
OpaqueClientRPC::cancel()
{
    std::unique_lock<std::mutex> mutexGuard(mutex);
    if (ready)
        return;
    if (session)
        session->cancel(*this);
    ready = true;
    session.reset();
    reply.reset();
    errorMessage = "RPC canceled by user";
}

Buffer
OpaqueClientRPC::extractReply()
{
    waitForReply(); // called without the lock held to avoid deadlock with self
    std::unique_lock<std::mutex> mutexGuard(mutex);
    if (!errorMessage.empty())
        throw Error(errorMessage);
    return std::move(reply);
}

std::string
OpaqueClientRPC::getErrorMessage() const
{
    std::unique_lock<std::mutex> mutexGuard(mutex);
    const_cast<OpaqueClientRPC*>(this)->update();
    return errorMessage;
}

bool
OpaqueClientRPC::isReady()
{
    std::unique_lock<std::mutex> mutexGuard(mutex);
    update();
    return ready;
}

Buffer*
OpaqueClientRPC::peekReply()
{
    std::unique_lock<std::mutex> mutexGuard(mutex);
    update();
    if (ready && errorMessage.empty())
        return &reply;
    else
        return NULL;
}

void
OpaqueClientRPC::waitForReply()
{
    std::unique_lock<std::mutex> mutexGuard(mutex);
    if (ready)
        return;
    if (session) {
        {
            // release the mutex while calling wait()
            Core::MutexUnlock<std::mutex> unlockGuard(mutexGuard);
            session->wait(*this);
        }
        update();
    } else {
        ready = true;
        errorMessage = "This RPC was never associated with a ClientSession.";
    }
}

///// private methods /////

void
OpaqueClientRPC::update()
{
    if (!ready && session)
        session->update(*this);
}

} // namespace LogCabin::RPC
} // namespace LogCabin
