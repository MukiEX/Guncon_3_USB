// lwIP over TinyUSB RNDIS/ECM (web config mode). Based on TinyUSB's net_lwip_webserver.
#include <string.h>
#include "pico/stdlib.h"
#include "tusb.h"
#include "dhserver.h"
#include "dnserver.h"
#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "lwip/sys.h"
#include "lwip/etharp.h"
#include "netif/ethernet.h"
#include "net.h"
#include "http.h"

#define INIT_IP4(a, b, c, d) { PP_HTONL(LWIP_MAKEU32(a, b, c, d)) }

uint8_t tud_network_mac_address[6] = {0x02, 0x47, 0x43, 0x33, 0x58, 0x00};

static struct netif s_netif;
static struct pbuf *s_rx;
static const ip4_addr_t s_ip = INIT_IP4(192, 168, 5, 1);
static const ip4_addr_t s_mask = INIT_IP4(255, 255, 255, 0);
static const ip4_addr_t s_gw = INIT_IP4(0, 0, 0, 0);

static dhcp_entry_t s_entries[] = {
  {{0}, INIT_IP4(192, 168, 5, 2), 24 * 60 * 60},
  {{0}, INIT_IP4(192, 168, 5, 3), 24 * 60 * 60},
  {{0}, INIT_IP4(192, 168, 5, 4), 24 * 60 * 60},
};
static const dhcp_config_t s_dhcp = {
  .router = INIT_IP4(0, 0, 0, 0),
  .port = 67,
  .dns = INIT_IP4(192, 168, 5, 1),
  "usb",
  TU_ARRAY_SIZE(s_entries),
  s_entries
};

static err_t linkoutput_fn(struct netif *netif, struct pbuf *p) {
  (void) netif;
  // Wait (servicing USB) for the previous frame to go out, but never forever:
  // a host that stops reading must not freeze the main loop.
  uint32_t start = sys_now();
  for (;;) {
    if (!tud_ready()) return ERR_USE;
    if (tud_network_can_xmit(p->tot_len)) {
      tud_network_xmit(p, 0);
      return ERR_OK;
    }
    if (sys_now() - start > 50) return ERR_IF;
    tud_task();
  }
}

static err_t ip4_output_fn(struct netif *netif, struct pbuf *p, const ip4_addr_t *addr) {
  return etharp_output(netif, p, addr);
}

static err_t netif_init_cb(struct netif *netif) {
  netif->mtu = CFG_TUD_NET_MTU;
  netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP | NETIF_FLAG_UP;
  netif->name[0] = 'E';
  netif->name[1] = 'X';
  netif->linkoutput = linkoutput_fn;
  netif->output = ip4_output_fn;
  return ERR_OK;
}

static bool dns_query_proc(const char *name, ip4_addr_t *addr) {
  (void) name;
  *addr = s_ip;   // answer everything with ourselves (e.g. http://gc3.usb)
  return true;
}

// Return value contract (TinyUSB): true = we kept the frame and will call tud_network_recv_renew()
// once it's processed; false = TinyUSB drops it and re-arms reception itself.
// Returning true for a frame we didn't keep stalls reception forever.
bool tud_network_recv_cb(const uint8_t *src, uint16_t size) {
  if (s_rx || !size) return false;
  struct pbuf *p = pbuf_alloc(PBUF_RAW, size, PBUF_POOL);
  if (!p) return false;
  pbuf_take(p, src, size);
  s_rx = p;
  return true;
}

uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg) {
  (void) arg;
  struct pbuf *p = (struct pbuf *) ref;
  return pbuf_copy_partial(p, dst, p->tot_len, 0);
}

void tud_network_init_cb(void) {
  if (s_rx) { pbuf_free(s_rx); s_rx = NULL; }
}

void net_init(void) {
  lwip_init();
  s_netif.hwaddr_len = sizeof(tud_network_mac_address);
  memcpy(s_netif.hwaddr, tud_network_mac_address, 6);
  s_netif.hwaddr[5] ^= 0x01;
  netif_add(&s_netif, &s_ip, &s_mask, &s_gw, NULL, netif_init_cb, ethernet_input);
  netif_set_default(&s_netif);
  while (dhserv_init(&s_dhcp) != ERR_OK) {}
  while (dnserv_init(IP_ADDR_ANY, 53, dns_query_proc) != ERR_OK) {}
  http_init();
}

void net_task(void) {
  if (s_rx) {
    if (ethernet_input(s_rx, &s_netif) != ERR_OK) pbuf_free(s_rx);
    s_rx = NULL;
    tud_network_recv_renew();
  }
  sys_check_timeouts();
}

// lwIP NO_SYS hooks
uint32_t sys_now(void) { return to_ms_since_boot(get_absolute_time()); }
sys_prot_t sys_arch_protect(void) { return 0; }
void sys_arch_unprotect(sys_prot_t pval) { (void) pval; }
