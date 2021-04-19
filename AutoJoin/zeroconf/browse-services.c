/*
 *  This file is derived from avahi_client_browse.c, part of avahi.
 *
 *  avahi is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU Lesser General Public License as
 *  published by the Free Software Foundation; either version 2.1 of the
 *  License, or (at your option) any later version.
 *
 *  avahi is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 *  or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General
 *  Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with avahi; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 *  USA.
 */
/*
 * dnssd_hosts
 * Find all stations sending DNS-SD notifications by looking for
 * _workstation._tcp. For all stations where we have a MAC address,
 * output the MAC address and hostname.
 */
#include <assert.h>
#include <ctype.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/malloc.h>
#include <avahi-common/error.h>
char buf[AVAHI_ADDRESS_STR_MAX];
AvahiProtocol PROTOCOL_TYPE;
char host[80];

static AvahiSimplePoll *simple_poll = NULL;
static void replace_newlines(const char *src, char *dst, int dstlen)
{
  int i;
  for (i = 0; src[i] != '\0' && i < (dstlen - 1); i++)
  {
    dst[i] = (src[i] == '\n') ? '.' : src[i];
  }
  dst[i] = '\0';
}
static void service_resolver_callback(
    AvahiServiceResolver *r,
    AvahiIfIndex interface,
    AvahiProtocol protocol,
    AvahiResolverEvent event,
    const char *name,
    const char *type,
    const char *domain,
    const char *host_name,
    const AvahiAddress *a,
    uint16_t port,
    AvahiStringList *txt,
    AvahiLookupResultFlags flags,
    void *userdata)
{
  AvahiClient *c = (AvahiClient *)userdata;
  int err;
  char temphost[80];
  char tempbuf[AVAHI_ADDRESS_STR_MAX];
  char lowertemphost[80];

  assert(c);

  switch (event)
  {
  case AVAHI_RESOLVER_FAILURE:
    err = avahi_client_errno(c);
    fprintf(stderr, "AVAHI_RESOLVER_FAILURE %s\n", avahi_strerror(err));
    avahi_simple_poll_quit(simple_poll);
    return;
  case AVAHI_RESOLVER_FOUND:

    avahi_address_snprint(tempbuf, sizeof(tempbuf), a);
    replace_newlines(host_name, temphost, sizeof(temphost));
    strcpy(lowertemphost, name);
    for (int i = 0; i < strlen(lowertemphost); i++)
    {
      lowertemphost[i] = tolower(lowertemphost[i]);
    }
    char localnet[20] = "127.0.0.1";

    // cout << lowertemphost << "|" << host << "|" << name << endl;
    // cout << (strcmp(lowertemphost, host) == 0) << "|" << (protocol == PROTOCOL_TYPE) << "|" << strcmp(tempbuf, localnet) << endl;
    if ((strcmp(lowertemphost, host) == 0) && (protocol == PROTOCOL_TYPE) && strcmp(tempbuf, localnet))
    {

      strcpy(buf, tempbuf);
      // cout << tempbuf << "|" << buf << endl;

      // printf("%s|%s|%s\n", tempbuf, temphost, name);
      // cout << tempbuf << "|" << temphost << "|" << name << "|" << type << "|" << domain << "|" << interface << "|" << event << "|" << port << "|" << flags << "|" << txt << endl;
    }
    // cout << tempbuf << "|" << temphost << "|" << name << "|" << type << "|" << domain << "|" << interface << "|" << event << "|" << port << "|" << flags << "|" << txt << endl;

    break;
  }
}
static void service_browser_callback(
    AvahiServiceBrowser *b,
    AvahiIfIndex interface,
    AvahiProtocol protocol,
    AvahiBrowserEvent event,
    const char *name,
    const char *type,
    const char *domain,
    AvahiLookupResultFlags flags,
    void *userdata)
{
  AvahiClient *c = (AvahiClient *)userdata;
  int err;
  assert(c);
  switch (event)
  {
  case AVAHI_BROWSER_FAILURE:
    err = avahi_client_errno(c);
    fprintf(stderr, "AVAHI_BROWSER_FAILURE %s\n", avahi_strerror(err));
    avahi_simple_poll_quit(simple_poll);
    return;
  case AVAHI_BROWSER_NEW:
    if (avahi_service_resolver_new(
            c, interface, protocol, name, type, domain, AVAHI_PROTO_UNSPEC,
            0, service_resolver_callback, c) == NULL)
    {
      fprintf(stderr, "avahi_service_resolver_new failed.\n");
    }
    break;
  case AVAHI_BROWSER_REMOVE:
    break;
  case AVAHI_BROWSER_ALL_FOR_NOW:
    avahi_simple_poll_quit(simple_poll);
    break;
  case AVAHI_BROWSER_CACHE_EXHAUSTED:
    break;
  }
}
static void service_type_browser_callback(
    AvahiServiceTypeBrowser *b,
    AvahiIfIndex interface,
    AvahiProtocol protocol,
    AvahiBrowserEvent event,
    const char *type,
    const char *domain,
    AvahiLookupResultFlags flags,
    void *userdata)
{
  AvahiClient *c = (AvahiClient *)userdata;
  int err;
  assert(c);
  switch (event)
  {
  case AVAHI_BROWSER_FAILURE:
    err = avahi_client_errno(c);
    fprintf(stderr, "AVAHI_BROWSER_FAILURE %s\n", avahi_strerror(err));
    avahi_simple_poll_quit(simple_poll);
    return;
  case AVAHI_BROWSER_NEW:
    if (avahi_service_browser_new(
            c, interface, protocol, type, domain, 0,
            service_browser_callback, c) == NULL)
    {
      fprintf(stderr, "avahi_service_browser_new failed.\n");
    }
    break;
  case AVAHI_BROWSER_REMOVE:
    break;
  case AVAHI_BROWSER_ALL_FOR_NOW:
    avahi_simple_poll_quit(simple_poll);
    break;
  case AVAHI_BROWSER_CACHE_EXHAUSTED:
    break;
  }
}
static void client_callback(AvahiClient *c, AvahiClientState state,
                            void *userdata)
{
  switch (state)
  {
  case AVAHI_CLIENT_FAILURE:
    fprintf(stderr, "Client failure: %s\n",
            avahi_strerror(avahi_client_errno(c)));
    avahi_simple_poll_quit(simple_poll);
    break;
  case AVAHI_CLIENT_S_REGISTERING:
  case AVAHI_CLIENT_S_RUNNING:
  case AVAHI_CLIENT_S_COLLISION:
  case AVAHI_CLIENT_CONNECTING:
    break;
  }
}
int get_ifindex(const char *ifname)
{
  int fd;
  struct ifreq ifr;
  size_t nlen = strlen(ifname);
  if ((fd = socket(AF_PACKET, SOCK_DGRAM, 0)) < 0)
  {
    perror("socket");
    exit(1);
  }
  if (nlen >= sizeof(ifr.ifr_name))
  {
    fprintf(stderr, "interface name %s is too long\n", ifname);
    exit(1);
  }
  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, ifname, nlen);
  ifr.ifr_name[nlen] = '\0';
  if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0)
  {
    perror("SIOCGIFINDEX");
    exit(1);
  }
  close(fd);
  return ifr.ifr_ifindex;
} /* get_ifindex */
void usage(char *progname)
{
  fprintf(stderr, "usage: %s [-i ifname]\n", progname);
  fprintf(stderr, "\t-i ifname - interface to use (default: unspecified)\n");
  exit(1);
}

// int browse(AvahiProtocol protocol, char *hostname)
// int browse(void)
// {
//   // int opt;
//   AvahiIfIndex ifindex = AVAHI_IF_UNSPEC;
//   AvahiClient *c = NULL;
//   AvahiServiceTypeBrowser *sb = NULL;
//   int error;

//   // PROTOCOL_TYPE = protocol;
//   // strcpy(host, hostname);

//   // for (int i = 0; i < strlen(hostname); i++)
//   // {
//   //   host[i] = tolower(hostname[i]);
//   // }

//   if ((c = avahi_client_new(avahi_simple_poll_get(simple_poll), 0,
//                             client_callback, NULL, &error)) == NULL)
//   {
//     fprintf(stderr, "avahi_client_new failed.\n");
//     return 1;
//   }
//   if ((sb = avahi_service_type_browser_new(
//            c, ifindex, AVAHI_PROTO_UNSPEC, NULL, 0,
//            service_type_browser_callback, c)) == NULL)
//   {
//     fprintf(stderr, "avahi_service_browser_new failed.\n");
//     return 1;
//   }
//   avahi_simple_poll_loop(simple_poll);
//   printf("%s|%s\n", buf, host);

//   return 0;
// }

int browse(AvahiProtocol protocol, char *hostname)
{
  int opt;
  AvahiIfIndex ifindex = AVAHI_IF_UNSPEC;
  AvahiClient *c = NULL;
  AvahiServiceTypeBrowser *sb = NULL;
  int error;

  PROTOCOL_TYPE = protocol;

  for (int i = 0; i < strlen(hostname); i++)
  {
    host[i] = tolower(hostname[i]);
  }

  if ((simple_poll = avahi_simple_poll_new()) == NULL)
  {
    fprintf(stderr, "avahi_simple_poll_new failed.\n");
    return 1;
  }
  if ((c = avahi_client_new(avahi_simple_poll_get(simple_poll), 0,
                            client_callback, NULL, &error)) == NULL)
  {
    fprintf(stderr, "avahi_client_new failed.\n");
    return 1;
  }
  if ((sb = avahi_service_type_browser_new(
           c, ifindex, AVAHI_PROTO_UNSPEC, NULL, 0,
           service_type_browser_callback, c)) == NULL)
  {
    fprintf(stderr, "avahi_service_browser_new failed.\n");
    return 1;
  }
  avahi_simple_poll_loop(simple_poll);
  // printf("%s|%s\n", buf, host);
  cout << "buf: " << buf << endl;
  return 0;
}

// int main(void)
// {
//   return browse();
// }
