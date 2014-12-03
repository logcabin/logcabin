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

#include <mutex>
#include <map>
#include <unordered_map>
#include <vector>
#include "include/LogCabin/Client.h"
#include "Client/ClientImpl.h"

#ifndef LOGCABIN_CLIENT_MOCKCLIENTIMPL_H
#define LOGCABIN_CLIENT_MOCKCLIENTIMPL_H

namespace LogCabin {
namespace Client {

/**
 * A mock implementation of the client library that operates against a
 * temporary, local, in-memory implementation.
 *
 * TODO(ongaro): This used to derive from ClientImplBase, but we want to make
 * use of the ClientImpl methods for the tree calls. This needs some more
 * thought.
 */
class MockClientImpl : public ClientImpl {
  public:
    /// Constructor.
    MockClientImpl();
    /// Destructor.
    ~MockClientImpl();

    // Implementations of ClientImplBase methods
    void initDerived();
    std::pair<uint64_t, Configuration> getConfiguration();
    ConfigurationResult setConfiguration(
                uint64_t oldId,
                const Configuration& newConfiguration);

    using ClientImpl::read;

  private:

    // MockClientImpl is not copyable
    MockClientImpl(const MockClientImpl&) = delete;
    MockClientImpl& operator=(const MockClientImpl&) = delete;
};

} // namespace LogCabin::Client
} // namespace LogCabin

#endif /* LOGCABIN_CLIENT_MOCKCLIENTIMPL_H */
