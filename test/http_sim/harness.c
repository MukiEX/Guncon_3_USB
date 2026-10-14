// Runs the firmware's real http.c + settings.c + net.c over lwIP on a Linux TAP interface.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include <arpa/inet.h>
#include "settings.h"
#include "mapper.h"
#include "guncon3.h"
#include "net.h"
uint8_t fake_flash[4096]; typedef struct { uint32_t scratch[8]; } wd_t; wd_t wdhw;
void flash_range_erase(uint32_t o, uint32_t n){ memset(fake_flash+o,0xFF,n); }
void flash_range_program(uint32_t o, const uint8_t *d, uint32_t n){ memcpy(fake_flash+o,d,n); }
int flash_safe_execute(void (*f)(void*), void *p, unsigned t){ (void)t; f(p); return 0; }
void diag_note_reboot_requested(void){}
int diag_json(char *b, size_t n){ return snprintf(b, n, "{\"boots\":1,\"lastReset\":\"power-on\",\"log\":\"a\\nb\"}"); }
static int tap, armed = 1;
bool tud_ready(void){return true;} void tud_task(void){}
bool tud_network_can_xmit(uint16_t s){(void)s;return true;}
uint16_t tud_network_xmit_cb(uint8_t*,void*,uint16_t); bool tud_network_recv_cb(const uint8_t*,uint16_t);
void tud_network_xmit(void*ref,uint16_t a){uint8_t b[2048];uint16_t n=tud_network_xmit_cb(b,ref,a);write(tap,b,n);}
void tud_network_recv_renew(void){armed=1;}
void guncon3_get(int s, gun_raw_t*o){memset(o,0,sizeof(*o));o->connected=s<2;o->path=s+2;o->x=100*s;}
int guncon3_usb_list(usb_dev_info_t*o,int m,uint32_t*e){(void)m;memset(o,0,sizeof(*o));o->addr=11;o->vid=0x05e3;o->pid=0x0610;o->is_hub=1;if(e)*e=3;return 1;}
void guncon3_request_reassign(void){}
static int aspect; static bool parked;
int mapper_aspect(void){return aspect;} void mapper_set_aspect(int a){aspect=a;}
void mapper_game_rect(int a,float r[4]){(void)a;r[0]=0;r[1]=0;r[2]=1;r[3]=1;}
static pad_out_t po[4]; const pad_out_t*mapper_pad(int i){return &po[i];}
bool mapper_aim_parked(void){return parked;} void mapper_set_aim_parked(bool p){parked=p;}
int main(void){
  tap=open("/dev/net/tun",O_RDWR); struct ifreq ifr={0}; ifr.ifr_flags=IFF_TAP|IFF_NO_PI; strcpy(ifr.ifr_name,"gc3tap");
  if(ioctl(tap,TUNSETIFF,&ifr)<0){perror("tun");return 1;}
  int s=socket(AF_INET,SOCK_DGRAM,0); struct sockaddr_in *a=(void*)&ifr.ifr_addr; a->sin_family=AF_INET; inet_pton(AF_INET,"192.168.5.2",&a->sin_addr);
  ioctl(s,SIOCSIFADDR,&ifr); inet_pton(AF_INET,"255.255.255.0",&a->sin_addr); ioctl(s,SIOCSIFNETMASK,&ifr);
  ioctl(s,SIOCGIFFLAGS,&ifr); ifr.ifr_flags|=IFF_UP|IFF_RUNNING; ioctl(s,SIOCSIFFLAGS,&ifr);
  settings_load(); net_init(); fcntl(tap,F_SETFL,O_NONBLOCK);
  uint8_t buf[2048];
  for(;;){ if(armed){ int n=read(tap,buf,sizeof buf); if(n>0){ armed=0; if(!tud_network_recv_cb(buf,n)) armed=1; } } net_task(); usleep(200);}  
}
