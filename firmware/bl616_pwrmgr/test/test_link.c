/* Host test: BL616 k230_link.c parser fed with frames built exactly like leakcam_agent.c link_send(). */
#include <stdio.h>
#include <string.h>
#include "k230_link.h"
#include "bflb_uart.h"
static struct bflb_device_s dev; static const char *rx; static size_t rxi; static char tx[256]; static size_t txi;
struct bflb_device_s *bflb_device_get_by_name(const char *n){(void)n;return &dev;}
int bflb_uart_getchar(struct bflb_device_s *d){(void)d; return rx[rxi] ? (unsigned char)rx[rxi++] : -1;}
int bflb_uart_putchar(struct bflb_device_s *d,int c){(void)d; tx[txi++]=(char)c; tx[txi]=0; return 0;}
/* K230 agent's framing, copied verbatim from leakcam_agent.c link_send() */
static void agent_frame(char *out, const char *cmd, const char *arg){ char body[64]; snprintf(body,sizeof body,arg?"%s,%s":"%s",cmd,arg); unsigned char s=0; for(const char*p=body;*p;p++)s^=(unsigned char)*p; sprintf(out,"$%s*%02X\n",body,s);}
int main(void){
  char a[80],b[80],c[80],stream[512]; int fails=0;
  agent_frame(a,"READY",NULL); agent_frame(b,"SLEEP","600"); agent_frame(c,"HALTED",NULL);
  snprintf(stream,sizeof stream,"[    0.000] Linux version 5.10.4 $junk*ZZ\n%s partial$SLEEP,5*00\n%s\r\n$THIS_IS_A_VERY_LONG_GARBAGE_FRAME_THAT_SHOULD_OVERFLOW_THE_BUFFER_XXXXXXXXXXXXXXXXXX*00\n%s",a,b,c);
  rx=stream; rxi=0; struct link_msg m; const char *want[]={"READY","SLEEP","HALTED"}; int k=0;
  while(link_poll(&m)){ printf("got %-7s arg=%u has_arg=%d\n",m.cmd,m.arg,m.has_arg); if(k>=3||strcmp(m.cmd,want[k]))fails++; if(k==1&&m.arg!=600)fails++; k++; }
  if(k!=3) fails++;
  link_send("WAKE","leak,1790000000"); char expect[80]; agent_frame(expect,"WAKE","leak,1790000000");
  printf("BL616 sends %s", tx); if(strcmp(tx,expect)) fails++;
  printf(fails? "FAIL (%d)\n":"link protocol: all frames round-trip, noise and bad checksums dropped\n",fails);
  return fails!=0;
}
