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

#ifndef LOGCABIN_RPC_ADDRESS_H
#define LOGCABIN_RPC_ADDRESS_H

#include <sys/socket.h>
#include <string>

namespace LogCabin {
namespace RPC {

/**
 * An Address specifies a host and a port.
 * This class also handles DNS lookups for addressing hosts by name.
 */
class Address {
  public:
    /**
     * Constructor. This calls refresh() automatically.
     * \param str
     *      A string representation of the host and, optionally, a port number.
     *          - hostname:port
     *          - hostname
     *          - IPv4Address:port
     *          - IPv4Address
     *          - [IPv6Address]:port
     *          - [IPv6Address]
     * \param defaultPort
     *      The port number to use if none is specified in str.
     */
    Address(const std::string& str, uint16_t defaultPort);

    /// Copy constructor.
    Address(const Address& other);

    /// Assignment.
    Address& operator=(const Address& other);

    /**
     * Return true if the sockaddr returned by getSockAddr() is valid.
     * \return
     *      True if refresh() has ever succeeded for this host and port.
     *      False otherwise.
     */
    bool isValid() const;

    /**
     * Return a sockaddr that may be used to connect a socket to this Address.
     * \return
     *      The returned value will never be NULL and it is always safe to read
     *      the protocol field from it, even if getSockAddrLen() returns 0.
     */
    const sockaddr* getSockAddr() const;

    /**
     * Return the length in bytes of the sockaddr in getSockAddr().
     * This is the value you'll want to pass in to connect() or bind().
     */
    socklen_t getSockAddrLen() const;

    /**
     * Return a string describing the sockaddr within this Address.
     * This string will reflect the numeric address produced by the latest
     * successful call to refresh(), or "Unspecified".
     */
    std::string getResolvedString() const;

    /**
     * Return a string describing this Address.
     * This will contain both the user-provided string passed into the
     * constructor and the numeric address produced by the latest successful
     * call to refresh(). It's the best representation to use in error messages
     * for the user.
     */
    std::string toString() const;

    /**
     * Convert the host and port to a sockaddr.
     * If the host is a name instead of numeric, this will run a DNS query and
     * select a random result. If this query fails, any previous sockaddr will
     * be left intact.
     */
    void refresh();

  private:

    /**
     * The host name or numeric address as passed into the constructor.
     */
    std::string originalString;

    /**
     * The host name or numeric address as parsed from the string passed into
     * the constructor. This has brackets stripped out of IPv6 addresses and is
     * in the form needed by getaddrinfo().
     */
    std::string host;

    /**
     * An ASCII representation of the port number to use. It is stored in
     * string form because that's sometimes how it comes into the constructor
     * and always what refresh() needs to call getaddrinfo().
     */
    std::string port;

    /**
     * Storage for the sockaddr returned by getSockAddr.
     * This is always zeroed out from len to the end.
     */
    sockaddr_storage storage;

    /**
     * The length in bytes of storage that are in use.
     * The remaining bytes of storage are always zeroed out.
     */
    socklen_t len;
};

} // namespace LogCabin::RPC
} // namespace LogCabin

#endif /* LOGCABIN_RPC_ADDRESS_H */
