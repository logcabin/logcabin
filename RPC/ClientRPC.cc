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

#include "RPC/ClientRPC.h"
#include "RPC/ClientSession.h"

namespace LogCabin {
namespace RPC {

ClientRPC::ClientRPC()
    : session()
    , responseToken(~0UL)
    , ready(false)
    , reply()
    , errorMessage()
{
}

ClientRPC::ClientRPC(ClientRPC&& other)
    : session(std::move(other.session))
    , responseToken(std::move(other.responseToken))
    , ready(std::move(other.ready))
    , reply(std::move(other.reply))
    , errorMessage(std::move(other.errorMessage))
{
}

ClientRPC::~ClientRPC()
{
    cancel();
}

ClientRPC&
ClientRPC::operator=(ClientRPC&& other)
{
    session = std::move(other.session);
    responseToken = std::move(other.responseToken);
    ready = std::move(other.ready);
    reply = std::move(other.reply);
    errorMessage = std::move(other.errorMessage);
    return *this;
}

void
ClientRPC::cancel()
{
    if (!ready)
        session->cancel(*this);
}

Buffer
ClientRPC::extractReply()
{
    waitForReply();
    if (!errorMessage.empty())
        throw Error(errorMessage);
    return std::move(reply);
}

std::string
ClientRPC::getErrorMessage() const
{
    update();
    return errorMessage;
}

bool
ClientRPC::isReady() const
{
    update();
    return ready;
}

Buffer*
ClientRPC::peekReply()
{
    update();
    if (ready && errorMessage.empty())
        return &reply;
    else
        return NULL;
}

const Buffer*
ClientRPC::peekReply() const
{
    update();
    if (ready && errorMessage.empty())
        return &reply;
    else
        return NULL;
}

void
ClientRPC::waitForReply() const
{
    if (!ready)
        session->wait(*const_cast<ClientRPC*>(this));
}

///// private methods /////

void
ClientRPC::update() const
{
    if (!ready)
        session->update(*const_cast<ClientRPC*>(this));
}

} // namespace LogCabin::RPC
} // namespace LogCabin
