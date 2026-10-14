#define _GNU_SOURCE
// Tiny HTTP/1.1 server on the lwIP raw API: serves the embedded config page and a small API.
// Keep-alive is supported so the page can poll /api/state quickly during calibration.

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include "pico/stdlib.h"
#include "lwip/tcp.h"

#include "http.h"
#include "settings.h"
#include "mapper.h"
#include "guncon3.h"
#include "diag.h"
#include "webpage.h"   // generated: webpage_gz[], webpage_gz_len

#define MAX_CONN   6
#define REQ_MAX    2048
#define RESP_MAX   4096

typedef struct {
  struct tcp_pcb *pcb;
  bool     used;
  int      req_len;
  char     req[REQ_MAX + 1];
  // pending response body
  const uint8_t *tx;
  uint32_t tx_left;
  bool     tx_copy;
  bool     close_after;
  uint8_t  idle;
  char     resp[RESP_MAX];
} conn_t;

static conn_t s_conn[MAX_CONN];
static int s_reboot_req = -1;

int http_take_reboot_request(void) { int r = s_reboot_req; s_reboot_req = -1; return r; }

//--------------------------------------------------------------------+
// Helpers
//--------------------------------------------------------------------+
static void conn_free(conn_t *c) {
  if (c->pcb) {
    tcp_arg(c->pcb, NULL);
    tcp_recv(c->pcb, NULL);
    tcp_sent(c->pcb, NULL);
    tcp_err(c->pcb, NULL);
    tcp_poll(c->pcb, NULL, 0);
    if (tcp_close(c->pcb) != ERR_OK) tcp_abort(c->pcb);
  }
  memset(c, 0, offsetof(conn_t, resp));
}

static void send_more(conn_t *c) {
  while (c->tx_left) {
    uint16_t room = tcp_sndbuf(c->pcb);
    if (!room) break;
    uint32_t n = c->tx_left < room ? c->tx_left : room;
    if (n > 1400) n = 1400;
    err_t e = tcp_write(c->pcb, c->tx, (uint16_t) n, c->tx_copy ? TCP_WRITE_FLAG_COPY : 0);
    if (e == ERR_MEM) break;
    if (e != ERR_OK) { conn_free(c); return; }
    c->tx += n; c->tx_left -= n;
  }
  tcp_output(c->pcb);
}

static void respond(conn_t *c, int code, const char *ctype, const void *body, uint32_t len, bool gz, bool copy) {
  char hdr[256];
  const char *status = code == 200 ? "OK" : code == 404 ? "Not Found" : code == 400 ? "Bad Request" : "Error";
  int h = snprintf(hdr, sizeof(hdr),
                   "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\n%s"
                   "Cache-Control: no-store\r\nConnection: %s\r\n\r\n",
                   code, status, ctype, (unsigned long) len, gz ? "Content-Encoding: gzip\r\n" : "",
                   c->close_after ? "close" : "keep-alive");
  tcp_write(c->pcb, hdr, (uint16_t) h, TCP_WRITE_FLAG_COPY | (len ? TCP_WRITE_FLAG_MORE : 0));
  c->tx = body; c->tx_left = len; c->tx_copy = copy;
  send_more(c);
}

static void respond_json(conn_t *c, int n) { respond(c, 200, "application/json", c->resp, (uint32_t) n, false, true); }

// Find `key` in an urlencoded body and copy its decoded value. Returns false if absent.
static bool form_get(const char *body, const char *key, char *out, size_t outlen) {
  size_t kl = strlen(key);
  const char *p = body;
  while (p && *p) {
    if (!strncmp(p, key, kl) && p[kl] == '=') {
      p += kl + 1;
      size_t o = 0;
      while (*p && *p != '&' && o + 1 < outlen) {
        if (*p == '+') { out[o++] = ' '; p++; }
        else if (*p == '%' && p[1] && p[2]) {
          char hx[3] = { p[1], p[2], 0 };
          out[o++] = (char) strtol(hx, NULL, 16);
          p += 3;
        } else out[o++] = *p++;
      }
      out[o] = 0;
      return true;
    }
    p = strchr(p, '&');
    if (p) p++;
  }
  return false;
}

static bool form_int(const char *body, const char *key, int *v) {
  char b[16];
  if (!form_get(body, key, b, sizeof(b))) return false;
  *v = atoi(b);
  return true;
}

static bool form_float(const char *body, const char *key, float *v) {
  char b[24];
  if (!form_get(body, key, b, sizeof(b))) return false;
  *v = strtof(b, NULL);
  return true;
}

//--------------------------------------------------------------------+
// API
//--------------------------------------------------------------------+
#define APPEND(...) do { n += snprintf(c->resp + n, RESP_MAX - n, __VA_ARGS__); if (n >= RESP_MAX) n = RESP_MAX - 1; } while (0)

static void api_state(conn_t *c) {
  int n = 0;
  float r[4];
  mapper_game_rect(mapper_aspect(), r);
  APPEND("{\"aspect\":%d,\"rect\":[%.4f,%.4f,%.4f,%.4f],\"ms\":%lu,\"guns\":[",
         mapper_aspect(), r[0], r[1], r[2], r[3], (unsigned long) to_ms_since_boot(get_absolute_time()));
  for (int i = 0; i < MAX_GUNS; i++) {
    gun_raw_t g;
    guncon3_get(i, &g);
    const pad_out_t *o = mapper_pad(i);
    APPEND("%s{\"c\":%d,\"port\":%d,\"x\":%d,\"y\":%d,\"z\":%d,\"b\":%u,\"ax\":%u,\"ay\":%u,\"bx\":%u,\"by\":%u,"
           "\"ind\":%u,\"n\":%lu,\"bad\":%lu,\"shot\":%lu,\"sx\":%d,\"sy\":%d,\"cal\":%d,"
           "\"u\":%.4f,\"v\":%.4f,\"gx\":%.4f,\"gy\":%.4f,\"off\":%d,\"lx\":%d,\"ly\":%d,\"xb\":%u,\"age\":%ld}",
           i ? "," : "", g.connected, g.hub_port, g.x, g.y, g.z, g.buttons, g.ax, g.ay, g.bx, g.by,
           g.ind, (unsigned long) g.reports, (unsigned long) g.bad, (unsigned long) g.shot_seq, g.shot_x, g.shot_y,
           o->calibrated, o->u, o->v, o->gx, o->gy, o->offscreen, o->rep.lx, o->rep.ly, o->rep.buttons,
           g.connected ? (long)(to_ms_since_boot(get_absolute_time()) - g.last_ms) : -1L);
  }
  APPEND("],\"usb\":[");
  usb_dev_info_t ud[USB_DIAG_MAX + 4];
  uint32_t ev;
  int nd = guncon3_usb_list(ud, USB_DIAG_MAX + 4, &ev);
  for (int i = 0; i < nd; i++)
    APPEND("%s{\"a\":%u,\"vid\":%u,\"pid\":%u,\"hub\":%u,\"port\":%u,\"spd\":%u,\"isHub\":%u}", i ? "," : "",
           ud[i].addr, ud[i].vid, ud[i].pid, ud[i].hub_addr, ud[i].hub_port, ud[i].speed, ud[i].is_hub);
  APPEND("],\"usbEvents\":%lu}", (unsigned long) ev);
  respond_json(c, n);
}

static void api_settings_get(conn_t *c) {
  settings_t *s = &g_settings;
  int n = 0;
  APPEND("{\"fw\":\"%s\",\"maxGuns\":%d,\"pads\":%d,\"aspectDefault\":%d,\"aspectCycle\":%d,\"offscreen\":%d,"
         "\"margin\":%d,\"smoothing\":%d,\"stickA\":%d,\"stickB\":%d,\"deadzone\":%d,\"hotkeys\":%d,"
         "\"monitorAspect\":%.5f,\"custom\":[%.4f,%.4f,%.4f,%.4f],\"map\":[",
         FW_VERSION, MAX_GUNS, s->pad_count, s->aspect_default, s->aspect_cycle_mask, s->offscreen_mode,
         s->offscreen_margin, s->smoothing, s->stick_a_dest, s->stick_b_dest, s->dpad_deadzone, s->hotkeys,
         s->monitor_aspect, s->custom_rect[0], s->custom_rect[1], s->custom_rect[2], s->custom_rect[3]);
  for (int i = 0; i < GB_COUNT; i++) APPEND("%s%d", i ? "," : "", s->button_map[i]);
  APPEND("],\"calib\":[");
  for (int g = 0; g < MAX_GUNS; g++) {
    APPEND("%s{\"valid\":%d,\"points\":%d,\"h\":[", g ? "," : "", s->calib[g].valid, s->calib[g].points);
    for (int k = 0; k < 8; k++) APPEND("%s%.9g", k ? "," : "", (double) s->calib[g].h[k]);
    APPEND("]}");
  }
  APPEND("]}");
  respond_json(c, n);
}

static void api_ok(conn_t *c, bool ok) {
  int n = snprintf(c->resp, RESP_MAX, "{\"ok\":%s}", ok ? "true" : "false");
  respond_json(c, n);
}

static void api_settings_post(conn_t *c, const char *body) {
  settings_t *s = &g_settings;
  int v;
  float f;
  char key[16];
  if (form_int(body, "pads", &v)) s->pad_count = (uint8_t) v;
  if (form_int(body, "aspectDefault", &v)) s->aspect_default = (uint8_t) v;
  if (form_int(body, "aspectCycle", &v)) s->aspect_cycle_mask = (uint8_t) v;
  if (form_int(body, "offscreen", &v)) s->offscreen_mode = (uint8_t) v;
  if (form_int(body, "margin", &v)) s->offscreen_margin = (uint8_t) v;
  if (form_int(body, "smoothing", &v)) s->smoothing = (uint8_t) v;
  if (form_int(body, "stickA", &v)) s->stick_a_dest = (uint8_t) v;
  if (form_int(body, "stickB", &v)) s->stick_b_dest = (uint8_t) v;
  if (form_int(body, "deadzone", &v)) s->dpad_deadzone = (uint8_t) v;
  if (form_int(body, "hotkeys", &v)) s->hotkeys = (uint8_t) !!v;
  if (form_float(body, "monitorAspect", &f)) s->monitor_aspect = f;
  for (int i = 0; i < 4; i++) {
    snprintf(key, sizeof(key), "custom%d", i);
    if (form_float(body, key, &f)) s->custom_rect[i] = f;
  }
  for (int i = 0; i < GB_COUNT; i++) {
    snprintf(key, sizeof(key), "map%d", i);
    if (form_int(body, key, &v)) s->button_map[i] = (uint8_t) v;
  }
  settings_sanitize(s);
  mapper_set_aspect(s->aspect_default);
  api_ok(c, settings_save());
}

static void api_calib_post(conn_t *c, const char *body) {
  int gun, pts = 0;
  char key[8];
  if (!form_int(body, "gun", &gun) || gun < 0 || gun >= MAX_GUNS) { api_ok(c, false); return; }
  gun_calib_t cal = {0};
  for (int k = 0; k < 8; k++) {
    snprintf(key, sizeof(key), "h%d", k);
    if (!form_float(body, key, &cal.h[k])) { api_ok(c, false); return; }
  }
  form_int(body, "points", &pts);
  cal.points = (uint8_t) pts;
  cal.valid = 1;
  float ma;
  if (form_float(body, "monitorAspect", &ma)) g_settings.monitor_aspect = ma;
  g_settings.calib[gun] = cal;
  settings_sanitize(&g_settings);
  api_ok(c, settings_save());
}

static void api_calib_clear(conn_t *c, const char *body) {
  int gun;
  if (!form_int(body, "gun", &gun) || gun < 0 || gun >= MAX_GUNS) { api_ok(c, false); return; }
  memset(&g_settings.calib[gun], 0, sizeof(gun_calib_t));
  api_ok(c, settings_save());
}

static void api_aspect(conn_t *c, const char *body) {
  int a;
  if (form_int(body, "a", &a)) mapper_set_aspect(a);
  api_ok(c, true);
}

static void api_reboot(conn_t *c, const char *body) {
  char m[16] = "xinput";
  form_get(body, "mode", m, sizeof(m));
  s_reboot_req = !strcmp(m, "web") ? 1 : !strcmp(m, "bootsel") ? 2 : 0;
  c->close_after = true;
  api_ok(c, true);
}

static void api_defaults(conn_t *c) {
  settings_t keep = g_settings;
  settings_defaults(&g_settings);
  memcpy(g_settings.calib, keep.calib, sizeof(keep.calib));     // keep calibrations
  g_settings.monitor_aspect = keep.monitor_aspect;
  mapper_set_aspect(g_settings.aspect_default);
  api_ok(c, settings_save());
}

//--------------------------------------------------------------------+
// Request handling
//--------------------------------------------------------------------+
static void handle_request(conn_t *c, char *method, char *path, char *body) {
  char *q = strchr(path, '?');
  if (q) *q = 0;
  bool get = !strcmp(method, "GET"), post = !strcmp(method, "POST");

  if (get && (!strcmp(path, "/") || !strcmp(path, "/index.html")))
    respond(c, 200, "text/html; charset=utf-8", webpage_gz, webpage_gz_len, true, false);
  else if (get && !strcmp(path, "/api/state")) api_state(c);
  else if (get && !strcmp(path, "/api/settings")) api_settings_get(c);
  else if (post && !strcmp(path, "/api/settings")) api_settings_post(c, body);
  else if (post && !strcmp(path, "/api/calib")) api_calib_post(c, body);
  else if (post && !strcmp(path, "/api/calib/clear")) api_calib_clear(c, body);
  else if (post && !strcmp(path, "/api/aspect")) api_aspect(c, body);
  else if (post && !strcmp(path, "/api/reboot")) api_reboot(c, body);
  else if (post && !strcmp(path, "/api/defaults")) api_defaults(c);
  else if (get && !strcmp(path, "/api/diag")) {
    static char dbuf[16 * 1024];
    int n = diag_json(dbuf, sizeof(dbuf));
    respond(c, 200, "application/json", dbuf, (uint32_t) n, false, true);
  }
  else if (get) {
    // Unknown page (captive-portal probes etc.): send them to the config page
    static const char redir[] = "HTTP/1.1 302 Found\r\nLocation: http://192.168.5.1/\r\nContent-Length: 0\r\n\r\n";
    tcp_write(c->pcb, redir, sizeof(redir) - 1, 0);
    tcp_output(c->pcb);
  } else respond(c, 404, "text/plain", "not found", 9, false, false);
}

// Try to parse one complete request from the buffer; returns bytes consumed (0 = need more)
static int try_parse(conn_t *c) {
  c->req[c->req_len] = 0;
  char *hend = strstr(c->req, "\r\n\r\n");
  if (!hend) return c->req_len >= REQ_MAX ? -1 : 0;
  int hlen = (int)(hend - c->req) + 4;

  int clen = 0;
  for (char *l = c->req; l && l < hend; l = strstr(l, "\r\n"), l = l ? l + 2 : NULL) {
    if (!strncasecmp(l, "Content-Length:", 15)) clen = atoi(l + 15);
    if (!strncasecmp(l, "Connection:", 11)) {
      char *eol = strstr(l, "\r\n");
      char *cl = strcasestr(l, "close");
      if (cl && eol && cl < eol) c->close_after = true;
    }
  }
  if (clen < 0 || hlen + clen > REQ_MAX) return -1;
  if (c->req_len < hlen + clen) return 0;

  // Request line
  char *sp1 = strchr(c->req, ' ');
  if (!sp1) return -1;
  *sp1 = 0;
  char *path = sp1 + 1;
  char *sp2 = strchr(path, ' ');
  if (!sp2) return -1;
  *sp2 = 0;

  char saved = c->req[hlen + clen];
  c->req[hlen + clen] = 0;
  handle_request(c, c->req, path, c->req + hlen);
  c->req[hlen + clen] = saved;
  return hlen + clen;
}

// Handle as many buffered requests as possible (one at a time; waits while a body is still going out)
static void process(conn_t *c) {
  while (c->used && !c->tx_left && c->req_len && !c->close_after) {
    int used = try_parse(c);
    if (used < 0) { conn_free(c); return; }
    if (used == 0) return;
    if (!c->used) return;                 // freed while handling
    memmove(c->req, c->req + used, c->req_len - used);
    c->req_len -= used;
  }
}

//--------------------------------------------------------------------+
// lwIP callbacks
//--------------------------------------------------------------------+
static err_t on_sent(void *arg, struct tcp_pcb *pcb, u16_t len) {
  (void) pcb; (void) len;
  conn_t *c = arg;
  if (!c) return ERR_OK;
  c->idle = 0;
  if (c->tx_left) send_more(c);
  if (!c->used) return ERR_OK;
  if (!c->tx_left) {
    if (c->close_after) conn_free(c);
    else process(c);
  }
  return ERR_OK;
}

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
  conn_t *c = arg;
  if (!p || err != ERR_OK) {
    if (p) pbuf_free(p);
    if (c) conn_free(c);
    else tcp_close(pcb);
    return ERR_OK;
  }
  c->idle = 0;
  uint16_t tot = p->tot_len;
  if (c->req_len + tot > REQ_MAX) {
    pbuf_free(p);
    tcp_recved(pcb, tot);
    conn_free(c);
    return ERR_OK;
  }
  pbuf_copy_partial(p, c->req + c->req_len, tot, 0);
  c->req_len += tot;
  pbuf_free(p);
  tcp_recved(pcb, tot);

  process(c);
  return ERR_OK;
}

static void on_err(void *arg, err_t err) {
  (void) err;
  conn_t *c = arg;
  if (c) { c->pcb = NULL; memset(c, 0, offsetof(conn_t, resp)); }
}

static err_t on_poll(void *arg, struct tcp_pcb *pcb) {
  conn_t *c = arg;
  if (!c) { tcp_abort(pcb); return ERR_ABRT; }
  if (c->tx_left) send_more(c);
  if (++c->idle > 20) { conn_free(c); }        // ~10 s idle
  return ERR_OK;
}

static err_t on_accept(void *arg, struct tcp_pcb *pcb, err_t err) {
  (void) arg;
  if (err != ERR_OK || !pcb) return ERR_VAL;
  conn_t *c = NULL;
  for (int i = 0; i < MAX_CONN; i++) if (!s_conn[i].used) { c = &s_conn[i]; break; }
  if (!c) {
    // Out of slots: drop the oldest idle connection
    int best = -1;
    for (int i = 0; i < MAX_CONN; i++) if (!s_conn[i].tx_left && (best < 0 || s_conn[i].idle > s_conn[best].idle)) best = i;
    if (best < 0) { tcp_abort(pcb); return ERR_ABRT; }
    conn_free(&s_conn[best]);
    c = &s_conn[best];
  }
  memset(c, 0, offsetof(conn_t, resp));
  c->used = true;
  c->pcb = pcb;
  tcp_setprio(pcb, TCP_PRIO_MIN);
  tcp_nagle_disable(pcb);
  tcp_arg(pcb, c);
  tcp_recv(pcb, on_recv);
  tcp_sent(pcb, on_sent);
  tcp_err(pcb, on_err);
  tcp_poll(pcb, on_poll, 1);
  return ERR_OK;
}

void http_init(void) {
  memset(s_conn, 0, sizeof(s_conn));
  struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
  tcp_bind(pcb, IP_ANY_TYPE, 80);
  pcb = tcp_listen_with_backlog(pcb, 4);
  tcp_accept(pcb, on_accept);
}
