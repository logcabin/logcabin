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

#include <gtest/gtest.h>

#include "Core/Debug.h"
#include "RPC/Address.h"

namespace LogCabin {
namespace RPC {
namespace {

TEST(RPCAddressTest, constructor) {
    EXPECT_EQ(" (resolved to Unspecified)",
              Address("", 90).toString());

    // hostname
    Address name("example.com", 80);
    EXPECT_EQ("example.com", name.hosts.at(0).first);
    EXPECT_EQ("80", name.hosts.at(0).second);
    EXPECT_EQ("example.com", name.originalString);
    Address namePort("example.com:80", 90);
    EXPECT_EQ("example.com", namePort.hosts.at(0).first);
    EXPECT_EQ("80", namePort.hosts.at(0).second);
    EXPECT_EQ("example.com:80", namePort.originalString);

    // IPv4
    Address ipv4("1.2.3.4", 80);
    EXPECT_EQ("1.2.3.4", ipv4.hosts.at(0).first);
    EXPECT_EQ("80", ipv4.hosts.at(0).second);
    EXPECT_EQ("1.2.3.4", ipv4.originalString);
    Address ipv4Port("1.2.3.4:80", 90);
    EXPECT_EQ("1.2.3.4", ipv4Port.hosts.at(0).first);
    EXPECT_EQ("80", ipv4Port.hosts.at(0).second);
    EXPECT_EQ("1.2.3.4:80", ipv4Port.originalString);

    // IPv6
    Address ipv6("[1:2:3:4:5:6:7:8]", 80);
    EXPECT_EQ("1:2:3:4:5:6:7:8", ipv6.hosts.at(0).first);
    EXPECT_EQ("80", ipv6.hosts.at(0).second);
    EXPECT_EQ("[1:2:3:4:5:6:7:8]", ipv6.originalString);
    Address ipv6Port("[1:2:3:4:5:6:7:8]:80", 90);
    EXPECT_EQ("1:2:3:4:5:6:7:8", ipv6Port.hosts.at(0).first);
    EXPECT_EQ("80", ipv6Port.hosts.at(0).second);
    EXPECT_EQ("[1:2:3:4:5:6:7:8]:80", ipv6Port.originalString);
    Address ipv6Short("[::1]", 80);
    EXPECT_EQ("::1", ipv6Short.hosts.at(0).first);
    EXPECT_EQ("80", ipv6Short.hosts.at(0).second);
    EXPECT_EQ("[::1]", ipv6Short.originalString);

    // multiple hosts
    Address all("example.com;"
                "example.com:80;"
                "1.2.3.4;"
                "1.2.3.4:80;"
                "[1:2:3:4:5:6:7:8];"
                "[1:2:3:4:5:6:7:8]:80;"
                "[::1]", 80);
    EXPECT_EQ((std::vector<std::pair<std::string, std::string>> {
                {"example.com", "80"},
                {"example.com", "80"},
                {"1.2.3.4", "80"},
                {"1.2.3.4", "80"},
                {"1:2:3:4:5:6:7:8", "80"},
                {"1:2:3:4:5:6:7:8", "80"},
                {"::1", "80"},
               }),
              all.hosts);

    Address semicolons(";;;example.com;;;;", 80);
    EXPECT_EQ((std::vector<std::pair<std::string, std::string>> {
                {"example.com", "80"},
               }),
              semicolons.hosts);
}

TEST(RPCAddressTest, constructor_copy) {
    Address a("127.0.0.1", 80);
    Address b(a);
    EXPECT_EQ(a.hosts, b.hosts);
    EXPECT_EQ(a.len, b.len);
    EXPECT_EQ(a.toString(), b.toString());
    EXPECT_EQ(a.getResolvedString(), b.getResolvedString());
}

TEST(RPCAddressTest, assignment) {
    Address a("127.0.0.1", 80);
    Address b("127.0.0.2", 81);
    b = a;
    EXPECT_EQ(a.hosts, b.hosts);
    EXPECT_EQ(a.len, b.len);
    EXPECT_EQ(a.toString(), b.toString());
    EXPECT_EQ(a.getResolvedString(), b.getResolvedString());
}

TEST(RPCAddressTest, isValid) {
    Address a("127.0.0.1", 80);
    Address b("qqq", 81);
    EXPECT_TRUE(a.isValid());
    EXPECT_FALSE(b.isValid());
}

TEST(RPCAddressTest, getResolvedString) {
    // getResolvedString is tested adequately in the refresh test.
}

TEST(RPCAddressTest, toString) {
    Address a("127.0.0.1:80", 90);
    a.originalString = "example.org:80";
    EXPECT_EQ("example.org:80 (resolved to 127.0.0.1:80)",
              a.toString());
}

TEST(RPCAddressTest, refresh) {
    Address empty("", 80);
    empty.refresh();
    EXPECT_FALSE(empty.isValid());

    // should be random, but should eventually refresh to all addresses
    Address multi("1.2.3.4;5.6.7.8", 80);
    std::set<std::string> resolved;
    for (uint64_t i = 0; i < 20; ++i) {
        multi.refresh();
        resolved.insert(multi.getResolvedString());
    }
    EXPECT_EQ(2U, resolved.size());

    // This should be a pretty stable IP address, since it is supposed to be
    // easy to be memorize (at least for IPv4).
    std::string googleDNS =
        Address("google-public-dns-a.google.com", 80).getResolvedString();
    if (googleDNS != "[2001:4860:4860::8888]:80") {
        EXPECT_EQ("8.8.8.8:80", googleDNS)
            << "This test requires connectivity to the Internet for a DNS "
            << "lookup. Alternatively, you can point "
            << "google-public-dns-a.google.com to 8.8.8.8 "
            << "in your /etc/hosts file.";
    }

    // IPv4
    EXPECT_EQ("1.2.3.4:80", Address("1.2.3.4", 80).getResolvedString());
    EXPECT_EQ("0.0.0.0:80", Address("0", 80).getResolvedString())
        << "any address";

    // IPv6
    const char* disclaimer = "Failure of this test is normal if no external "
                             "network interface has an IPv6 address set.";
    EXPECT_EQ("[1:2:3:4:5:6:7:8]:80",
              Address("[1:2:3:4:5:6:7:8]", 80).getResolvedString())
              << "random IPv6 address. " << disclaimer;
    EXPECT_EQ("[::1]:80",
              Address("[::1]", 80).getResolvedString())
              << "localhost. " << disclaimer;
    EXPECT_EQ("[::]:80",
              Address("[::]", 80).getResolvedString())
              << "any address. " << disclaimer;
}

} // namespace LogCabin::RPC::<anonymous>
} // namespace LogCabin::RPC
} // namespace LogCabin
