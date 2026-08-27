#include "websocket.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void sha1(const unsigned char *in, size_t n, unsigned char out[20]) {
    uint32_t h[5]={0x67452301u,0xefcdab89u,0x98badcfeu,0x10325476u,0xc3d2e1f0u};
    size_t total=((n+9+63)/64)*64; unsigned char b[256];
    if (total > sizeof b) return;
    memset(b, 0, total); memcpy(b, in, n); b[n] = 0x80; uint64_t bits = (uint64_t)n * 8;
    for(int i=0;i<8;i++) b[total-1-i]=(unsigned char)(bits>>(i*8));
    for(size_t off=0;off<total;off+=64){ uint32_t w[80];
        for(int i=0;i<16;i++) w[i]=((uint32_t)b[off+i*4]<<24)|((uint32_t)b[off+i*4+1]<<16)|((uint32_t)b[off+i*4+2]<<8)|b[off+i*4+3];
        for(int i=16;i<80;i++){uint32_t x=w[i-3]^w[i-8]^w[i-14]^w[i-16];w[i]=(x<<1)|(x>>31);}
        uint32_t a=h[0],c=h[2],d=h[3],e=h[4],bb=h[1];
        for(int i=0;i<80;i++){uint32_t f,k;if(i<20){f=(bb&c)|((~bb)&d);k=0x5a827999u;}else if(i<40){f=bb^c^d;k=0x6ed9eba1u;}else if(i<60){f=(bb&c)|(bb&d)|(c&d);k=0x8f1bbcdcu;}else{f=bb^c^d;k=0xca62c1d6u;}uint32_t t=((a<<5)|(a>>27))+f+e+k+w[i];e=d;d=c;c=(bb<<30)|(bb>>2);bb=a;a=t;}
        h[0]+=a;h[1]+=bb;h[2]+=c;h[3]+=d;h[4]+=e;
    }
    for(int i=0;i<5;i++){out[i*4]=(unsigned char)(h[i]>>24);out[i*4+1]=(unsigned char)(h[i]>>16);out[i*4+2]=(unsigned char)(h[i]>>8);out[i*4+3]=(unsigned char)h[i];}
}
static void b64(const unsigned char *in, int n, char *out) { static const char t[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; int o=0; for(int i=0;i<n;i+=3){unsigned v=(unsigned)in[i]<<16;if(i+1<n)v|=(unsigned)in[i+1]<<8;if(i+2<n)v|=in[i+2];out[o++]=t[(v>>18)&63];out[o++]=t[(v>>12)&63];out[o++]=i+1<n?t[(v>>6)&63]:'=';out[o++]=i+2<n?t[v&63]:'=';}out[o]=0;}
static int send_all(ImSocket *s,const void *data,size_t n){const char *p=(const char*)data;while(n){int k=im_socket_send(s,p,n);if(k<=0)return -1;p+=k;n-=(size_t)k;}return 0;}
int im_ws_accept(ImSocket *s,const char *req,size_t n){if(!s||!req||n>65536)return -1;const char *k=strstr(req,"Sec-WebSocket-Key:");if(!k)return -1;k+=19;while(*k==' '||*k=='\t')k++;char key[128];size_t i=0;while(k[i]&&k[i]!='\r'&&k[i]!='\n'&&i<sizeof(key)-1)i++;memcpy(key,k,i);key[i]=0;char combo[256];snprintf(combo,sizeof combo,"%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11",key);unsigned char dig[20];sha1((unsigned char*)combo,strlen(combo),dig);char accept[32];b64(dig,20,accept);char out[256];int m=snprintf(out,sizeof out,"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n",accept);return send_all(s,out,(size_t)m)==0?0:-1;}
int im_ws_read_text(ImSocket *s,char *out,size_t cap){unsigned char h[2],mask[4];if(!s||!out||cap<1)return -1;int n=im_socket_recv(s,h,2);if(n!=2)return 0;unsigned long long len=h[1]&127;if((h[0]&15)==8)return 0;if(len==126){unsigned char x[2];if(im_socket_recv(s,x,2)!=2)return 0;len=((unsigned)x[0]<<8)|x[1];}else if(len==127)return -1;if(!(h[1]&128)||len>=cap)return -1;if(im_socket_recv(s,mask,4)!=4)return 0;size_t got=0;while(got<len){int k=im_socket_recv(s,out+got,(size_t)len-got);if(k<=0)return 0;got+=(size_t)k;}for(size_t i=0;i<got;i++)out[i]^=mask[i&3];out[got]=0;return (h[0]&15)==1?(int)got:-1;}
int im_ws_send_text(ImSocket *s,const char *data,size_t n){if(!s||!data||n>=65536)return -1;unsigned char h[4];size_t hn=2;if(n<126){h[1]=(unsigned char)n;}else{h[1]=126;h[2]=(unsigned char)(n>>8);h[3]=(unsigned char)n;hn=4;}h[0]=0x81;if(send_all(s,h,hn)||send_all(s,data,n))return -1;return 0;}
