#!/bin/sh
# Real firmware HTTP/JSON code over lwIP on a TAP device (Linux, needs root + /dev/net/tun).
set -e
cd "$(dirname "$0")"
G=../..; L=${PICO_SDK_PATH:?}/lib/lwip/src; T=$G/lib/tinyusb/lib/networking
mkdir -p gen && python3 $G/tools/embed_web.py $G/web/index.html gen/webpage.h
cc -O1 -w -include stdint.h -include stddef.h -Istub -Igen -I$G/src -I$L/include -I$T -o /tmp/gc3_http_sim harness.c \
  $G/src/http.c $G/src/net.c $G/src/settings.c $T/dhserver.c $T/dnserver.c \
  $L/core/def.c $L/core/inet_chksum.c $L/core/init.c $L/core/ip.c $L/core/mem.c $L/core/memp.c $L/core/netif.c $L/core/pbuf.c \
  $L/core/raw.c $L/core/stats.c $L/core/sys.c $L/core/tcp.c $L/core/tcp_in.c $L/core/tcp_out.c $L/core/timeouts.c $L/core/udp.c \
  $L/core/ipv4/etharp.c $L/core/ipv4/icmp.c $L/core/ipv4/ip4.c $L/core/ipv4/ip4_addr.c $L/core/ipv4/ip4_frag.c $L/netif/ethernet.c
pkill -x gc3_http_sim 2>/dev/null || true
/tmp/gc3_http_sim & PID=$!
trap 'kill $PID 2>/dev/null' EXIT
sleep 1
python3 check.py
