/* hl_bridge.c - TCP->WebSocket bridge for Inimerse headless mode (multi-client)
   Usage: hl_bridge.exe <enginePort> <wsPort>
   - Connects to inimerse TCP (enginePort) as a client (auto-reconnect, non-blocking)
   - Serves WebSocket on wsPort for browsers (multi-client: engine frames are
     broadcast to every connected WS client; input from any client is forwarded
     to the engine)
   - HTTP endpoints: / (game page, g15), /forge, /vf, /wb, /workbench, /ip, /spawn, /diag
   WebSocket: server handshake + frame encode/decode (RFC6455, text frames only).
*/
#include <winsock2.h>
#include <winhttp.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "wb_embed.h"
#include "forge_embed.h"
#include "netplay_embed.h"
#include "desktop_embed.h"
#include "chat_embed.h"
#include "home_embed.h"

#define MAX_CLIENTS 16
#define CLIENT_BUF 1048576
#define WS_PAYLOAD_CAP 4096

/* kill_port: terminate any process LISTENING on the given port (reliable, API-based) */
static void kill_port(int port) {
    DWORD n = 0;
    if (GetExtendedTcpTable(NULL, &n, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) != ERROR_INSUFFICIENT_BUFFER) return;
    if (n == 0 || n > 512 * 1024) return;
    char *buf = (char*)malloc(n);
    if (!buf) return;
    if (GetExtendedTcpTable(buf, &n, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) == NO_ERROR) {
        MIB_TCPTABLE_OWNER_PID *tbl = (MIB_TCPTABLE_OWNER_PID*)buf;
        for (DWORD i = 0; i < tbl->dwNumEntries; i++) {
            MIB_TCPROW_OWNER_PID *r = &tbl->table[i];
            if (r->dwState == MIB_TCP_STATE_LISTEN && ntohs((u_short)r->dwLocalPort) == (unsigned)port) {
                HANDLE hp = OpenProcess(PROCESS_TERMINATE, FALSE, r->dwOwningPid);
                if (hp) { TerminateProcess(hp, 0); CloseHandle(hp); }
            }
        }
    }
    free(buf);
}

/* embedded web page (served at http://<ip>:<port>/) - g15 game page */
static const char *HTML_PAGE =
"<!DOCTYPE html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>Inimerse Web</title><style>body{margin:0;background:#111;color:#eee;font-family:sans-serif}"
"canvas{display:block;width:100vw;height:auto;max-width:900px;margin:0 auto;background:#000;touch-action:none;image-rendering:pixelated}</style></head><body>"
"<div id=s style='position:fixed;top:0;left:0;right:0;background:rgba(0,0,0,.85);color:#8f8;font:16px monospace;padding:8px;z-index:99;word-break:break-all'>connecting...</div>"
"<canvas id=c></canvas><script>if(!location.search){var _pv='g15-'+Date.now();location.replace(location.pathname+'?v='+_pv);}</script><script>"
"var cv=document.getElementById('c'),cx=cv.getContext('2d'),st=document.getElementById('s'),ws=null,port=location.port||'11461',PG='g15',lst=0;if(st)st.textContent='connecting '+location.hostname+':'+port+' ('+PG+')';"
"function col(v){return '#'+((v&0xFFFFFF)>>>0).toString(16).padStart(6,'0')}"
"var rc=0,lastErr='';function setS(t,c){st.textContent=t;st.style.color=c}"
"function conn(){try{setS('connecting '+location.hostname+':'+port+' (try '+(rc+1)+')','#ff8');ws=new WebSocket('ws://'+location.hostname+':'+port);"
"ws.onopen=function(){rc=0;setS('connected '+location.hostname+':'+port,'#8f8')};"
"ws.onmessage=function(e){var t=typeof e.data==='string'?e.data:new TextDecoder().decode(new Uint8Array(e.data));if(t.indexOf('{')===0)draw(t)};"
"ws.onclose=function(ev){rc++;lastErr='close code='+ev.code+' reason='+(ev.reason||'none');setS('WS CLOSED ['+rc+'] '+lastErr+' retry...','#f88');setTimeout(conn,1500)};"
"ws.onerror=function(ev){setS('WS ERROR '+location.hostname+':'+port+' bridge/engine unreachable','#f88');try{ws.close()}catch(x){}}}catch(x){}};"
"setTimeout(function(){if(ws&&ws.readyState!==1){setS('WS TIMEOUT '+location.hostname+':'+port+' no conn after 4s','#f88');try{ws.close()}catch(x){}}},4000);"
"conn();"
"function draw(str){var i=0;while(i<str.length){var n=str.indexOf('}{',i),e=n>=0?n+1:str.length;"
"try{var f=JSON.parse(str.slice(i,e));render(f)}catch(x){}i=e}}"
"function render(f){if(f.w&&f.h){cv.width=f.w;cv.height=f.h}"
"cx.fillStyle='#000';cx.fillRect(0,0,cv.width,cv.height);"
"(f.r||[]).forEach(function(sh){var c=col(sh.c);switch(sh.t){"
"case 0:cx.fillStyle=c;cx.fillRect(sh.x,sh.y,sh.w,sh.h);break;"
"case 1:cx.strokeStyle=c;cx.lineWidth=1;cx.strokeRect(sh.x,sh.y,sh.w,sh.h);break;"
"case 2:cx.strokeStyle=c;cx.beginPath();cx.moveTo(sh.x,sh.y);cx.lineTo(sh.x+sh.w,sh.y);cx.stroke();break;"
"case 3:cx.strokeStyle=c;cx.beginPath();cx.moveTo(sh.x,sh.y);cx.lineTo(sh.x,sh.y+sh.h);cx.stroke();break;"
"case 4:cx.fillStyle=c;cx.font='14px sans-serif';cx.textAlign=sh.a===1?'center':sh.a===2?'right':'left';"
"cx.fillText(sh.s||'',sh.a===1?sh.x+sh.w/2:sh.a===2?sh.x+sh.w:sh.x,sh.y+13);break;"
"default:cx.strokeStyle=c;cx.strokeRect(sh.x,sh.y,sh.w,sh.h)}});"
"(f.t||[]).forEach(function(t){cx.fillStyle='#fff';cx.font='14px sans-serif';cx.textAlign='left';cx.fillText(t.s,t.x,t.y+13)});"
"(f.c||[]).forEach(function(c){cx.strokeStyle='rgba(120,160,255,.7)';cx.strokeRect(c.x,c.y,c.w,c.h);"
"if(c.s){cx.fillStyle='#cde';cx.font='12px sans-serif';cx.fillText(c.s.slice(0,20),c.x+4,c.y+15)}})}"
"var KM={' ':'space',Enter:'enter',Tab:'tab',Escape:'esc',Backspace:'backspace',Delete:'delete',Shift:'shift',Control:'ctrl',Alt:'alt',"
"ArrowLeft:'left',ArrowRight:'right',ArrowUp:'up',ArrowDown:'down',F1:'f1',F2:'f2',F3:'f3',F4:'f4',F5:'f5',F6:'f6',F7:'f7',F8:'f8',F9:'f9',F10:'f10',F11:'f11',F12:'f12'};"
"function kn(e){if(KM[e.key])return KM[e.key];if(e.key&&e.key.length===1&&/[a-zA-Z]/.test(e.key))return e.key.toLowerCase();return null}"
"function snd(o){if(ws&&ws.readyState===1)ws.send(JSON.stringify(o))}"
"addEventListener('keydown',function(e){var k=kn(e);if(!k)return;if(k==='tab'||k==='left'||k==='right'||k==='up'||k==='down'||k==='space')e.preventDefault();snd({key:k,down:1})});"
"addEventListener('keyup',function(e){var k=kn(e);if(!k)return;snd({key:k,down:0})});"
"addEventListener('blur',function(){for(var k in KM)snd({key:KM[k],down:0})});"
"function mp(e){var r=cv.getBoundingClientRect(),x=(e.clientX-r.left)*cv.width/r.width,y=(e.clientY-r.top)*cv.height/r.height;"
"snd({mouse:{x:Math.round(x),y:Math.round(y),btn:1}})}"
"function mu(e){var r=cv.getBoundingClientRect(),x=(e.clientX-r.left)*cv.width/r.width,y=(e.clientY-r.top)*cv.height/r.height;"
"snd({mouse:{x:Math.round(x),y:Math.round(y),btn:0}})}"
"cv.addEventListener('mousedown',mp);cv.addEventListener('mouseup',mu);"
"cv.addEventListener('touchstart',function(e){e.preventDefault();var t=e.touches[0];mp(t)});"
"cv.addEventListener('touchend',function(e){e.preventDefault();mu(e.changedTouches[0])});"
"cv.addEventListener('touchmove',function(e){e.preventDefault();var t=e.touches[0],r=cv.getBoundingClientRect();"
"snd({mouse:{x:Math.round((t.clientX-r.left)*cv.width/r.width),y:Math.round((t.clientY-r.top)*cv.height/r.height),btn:1}})});"
"conn();</script></body></html>";

/* ---------------- multi-client state ---------------- */
typedef struct {
    SOCKET s;
    int ws;                /* 1 after websocket handshake */
    char buf[CLIENT_BUF];  /* recv buffer (HTTP req or WS bytes) */
    int blen;
    char ip[32];           /* client IP (for local-only endpoints) */
} Client;

static Client g_cli[MAX_CLIENTS];
static int g_ncli = 0;
static char g_http_ip[32] = ""; /* client IP of request being served */

static SOCKET g_engine = INVALID_SOCKET;   /* TCP to inimerse (non-blocking) */
static int g_engine_state = 0;             /* 0=connecting, 1=ready */
static DWORD g_engine_retry_at = 0;        /* tick when to retry connect */
static DWORD g_engine_connect_t0 = 0;      /* tick when last connect attempt started */

static int ws_send_text(SOCKET s, const char *data, int len) {
    unsigned char hdr[10]; int hn = 0;
    if (len < 126) { hdr[hn++] = 0x81; hdr[hn++] = (unsigned char)len; }
    else if (len < 65536) { hdr[hn++] = 0x81; hdr[hn++] = 126; hdr[hn++] = (unsigned char)(len >> 8); hdr[hn++] = (unsigned char)(len & 255); }
    else return -1;
    if (send(s, (const char*)hdr, hn, 0) == SOCKET_ERROR) return -1;
    return send(s, data, len, 0);
}

/* parse one WS text frame from client buffer; consume bytes on success.
   returns payload len (>0), 0 if need more data, -1 on protocol error */
static int ws_parse_buf(Client *c, char *out, int cap) {
    if (c->blen < 2) return 0;
    const unsigned char *b = (const unsigned char*)c->buf;
    int masked = (b[1] & 0x80) != 0;
    int len = b[1] & 0x7F;
    int off = 2;
    if (len == 126) { if (c->blen < 4) return 0; len = (b[2] << 8) | b[3]; off = 4; }
    else if (len == 127) return -1;
    int head = off + (masked ? 4 : 0);
    if (c->blen < head + len) return 0;
    if (len > cap) return -1;
    const char *pl = c->buf + head;
    if (masked) { for (int i = 0; i < len; i++) out[i] = pl[i] ^ b[off + i % 4]; }
    else memcpy(out, pl, len);
    out[len] = '\0';
    int consumed = head + len;
    memmove(c->buf, c->buf + consumed, c->blen - consumed);
    c->blen -= consumed;
    return len;
}

static void client_close(int i) {
    closesocket(g_cli[i].s);
    for (int j = i; j < g_ncli - 1; j++) g_cli[j] = g_cli[j + 1];
    g_ncli--;
}

/* handle buffered bytes of a WS client: extract frames, forward to engine */
static int client_pump_ws(int i) {
    Client *c = &g_cli[i];
    for (;;) {
        char pl[WS_PAYLOAD_CAP];
        int n = ws_parse_buf(c, pl, sizeof pl - 1);
        if (n < 0) return -1;
        if (n == 0) break;
        if (g_engine != INVALID_SOCKET && g_engine_state == 1) {
            if (n < (int)sizeof pl - 2) { pl[n] = '\n'; send(g_engine, pl, n + 1, 0); }
            else { send(g_engine, pl, n, 0); send(g_engine, "\n", 1, 0); }
        }
    }
    return 0;
}

/* broadcast one engine data chunk to every WS client */
static void engine_broadcast(const char *data, int len) {
    for (int i = 0; i < g_ncli; i++) {
        if (!g_cli[i].ws) continue;
        if (ws_send_text(g_cli[i].s, data, len) == SOCKET_ERROR) { client_close(i); i--; }
    }
}

static int port_listening(int port) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 0;
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) { WSACleanup(); return 0; }
    struct sockaddr_in sa; memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET; sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sa.sin_port = htons((unsigned short)port);
    int r = connect(s, (struct sockaddr*)&sa, sizeof sa);
    closesocket(s); WSACleanup();
    return r == 0;
}

static char *ci_strstr(const char *hay, const char *needle) {
    int hlen = (int)strlen(hay), nlen = (int)strlen(needle);
    if (nlen <= 0) return (char*)hay;
    for (int i = 0; i + nlen <= hlen; i++) {
        int j = 0;
        for (; j < nlen; j++) {
            char a = hay[i + j], b = needle[j];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) break;
        }
        if (j == nlen) return (char*)hay + i;
    }
    return NULL;
}


static void proj_walk(const char *dir, const char *prefix, char *out, int *op, int cap) {
  WIN32_FIND_DATAA fd;
  char pat[600];
  snprintf(pat, sizeof pat, "%s\\*", dir);
  HANDLE h = FindFirstFileA(pat, &fd);
  if (h == INVALID_HANDLE_VALUE) return;
  do {
    if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      char sub[600], subp[600];
      snprintf(sub, sizeof sub, "%s\\%s", dir, fd.cFileName);
      snprintf(subp, sizeof subp, "%s%s/", prefix, fd.cFileName);
      proj_walk(sub, subp, out, op, cap);
    } else {
      if (*op > 0 && *op < cap - 2) out[(*op)++] = ',';
      *op += snprintf(out + *op, cap - *op, "{\"p\":\"%s%s\",\"s\":%lu}", prefix, fd.cFileName, (unsigned long)fd.nFileSizeLow);
    }
  } while (FindNextFileA(h, &fd));
  FindClose(h);
}
static int proj_safe(const char *p) { return p[0] && !strstr(p, "..") && !strchr(p, ':') && !strchr(p, '\\'); }

/* ---------- userdata API (M1 identity/social) ---------- */
static const char *ud_read(const char *path, char *buf, int cap) {
  buf[0] = 0;
  FILE *f = fopen(path, "rb");
  if (!f) return buf;
  size_t n = fread(buf, 1, cap - 1, f);
  fclose(f);
  buf[n] = 0;
  return buf;
}
static void ud_write(const char *path, const char *content) {
  FILE *f = fopen(path, "wb");
  if (!f) return;
  fwrite(content, 1, strlen(content), f);
  fclose(f);
}
static void ud_ensure_profile(void) {
  CreateDirectoryA("D:\\inimerse_stable\\userdata", NULL);
  CreateDirectoryA("D:\\inimerse_stable\\userdata\\messages", NULL);
  char path[512]; snprintf(path, sizeof path, "D:\\inimerse_stable\\userdata\\profile.json");
  FILE *f = fopen(path, "rb");
  if (f) { fclose(f); return; }
  srand((unsigned)(time(NULL) ^ GetTickCount()));
  char buf[512];
  snprintf(buf, sizeof buf,
    "{\"id\":\"u_%08x%08x\",\"name\":\"player%d\",\"avatar\":\"\",\"bio\":\"\",\"created\":%ld}",
    (unsigned)rand(), (unsigned)rand(), (rand() % 9000) + 1000, (long)time(NULL));
  ud_write(path, buf);
}
static void http_json(SOCKET s, const char *body) {
  int bl = (int)strlen(body);
  char *res = (char*)malloc(bl + 256);
  if (!res) return;
  snprintf(res, bl + 256, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nContent-Length: %d\r\n\r\n%s", bl, body);
  send(s, res, (int)strlen(res), 0);
  free(res);
}static void qparam(const char *req, const char *key, char *out, int cap) {
  out[0] = 0;
  char pat[128]; snprintf(pat, sizeof pat, "%s=", key);
  const char *p = strstr(req, pat);
  if (!p) return;
  p += strlen(pat);
  const char *e = p;
  while (*e && *e != '&' && *e != ' ' && *e != '\r' && *e != '\n') e++;
  int n = (int)(e - p); if (n > cap - 1) n = cap - 1;
  memcpy(out, p, n); out[n] = 0;
  int w = 0;
  for (int r = 0; out[r]; r++) {
    if (out[r] == '+') { out[w++] = ' '; }
    else if (out[r] == '%' && out[r+1] && out[r+2]) {
      int hi = -1, lo = -1;
      char c1 = out[r+1]; if (c1 >= '0' && c1 <= '9') hi = c1 - '0'; else if (c1 >= 'a' && c1 <= 'f') hi = c1 - 'a' + 10; else if (c1 >= 'A' && c1 <= 'F') hi = c1 - 'A' + 10;
      char c2 = out[r+2]; if (c2 >= '0' && c2 <= '9') lo = c2 - '0'; else if (c2 >= 'a' && c2 <= 'f') lo = c2 - 'a' + 10; else if (c2 >= 'A' && c2 <= 'F') lo = c2 - 'A' + 10;
      if (hi >= 0 && lo >= 0) { out[w++] = (char)((hi << 4) | lo); r += 2; }
      else out[w++] = out[r];
    } else out[w++] = out[r];
  }
  out[w] = 0;
}
static void json_esc(const char *in, char *out, int cap) {
  int oi = 0;
  for (const char *p = in; *p && oi < cap - 2; p++) {
    if (*p == '"' || *p == '\\') { if (oi < cap - 2) out[oi++] = '\\'; }
    out[oi++] = *p;
  }
  out[oi] = 0;
}
/* ---------- AI helpers (M2 bridge side) ---------- */
static void ai_load_cfg(char *ep, int ecap, char *key, int kcap, char *model, int mcap) {
  ep[0] = 0; key[0] = 0; snprintf(model, mcap, "Qwen2.5-7B-Instruct:latest");
  char buf[2048], path[512];
  snprintf(path, sizeof path, "D:\\inimerse_stable\\userdata\\ai_config.json");
  ud_read(path, buf, sizeof buf);
  char *p = strstr(buf, "\"endpoint\":\"");
  if (p) { p += 12; char *e = strchr(p, '"'); if (e) { int n = (int)(e - p); if (n > ecap - 1) n = ecap - 1; memcpy(ep, p, n); ep[n] = 0; } }
  p = strstr(buf, "\"api_key\":\"");
  if (p) { p += 11; char *e = strchr(p, '"'); if (e) { int n = (int)(e - p); if (n > kcap - 1) n = kcap - 1; memcpy(key, p, n); key[n] = 0; } }
  p = strstr(buf, "\"model\":\"");
  if (p) { p += 9; char *e = strchr(p, '"'); if (e) { int n = (int)(e - p); if (n > mcap - 1) n = mcap - 1; memcpy(model, p, n); model[n] = 0; } }
}
static int ai_http_post(const char *host, int port, const char *path, int https, const char *body, const char *headers, char *out, int cap) {
  wchar_t whost[256], wpath[512];
  MultiByteToWideChar(CP_UTF8, 0, host, -1, whost, 256);
  MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 512);
  HINTERNET hI = WinHttpOpen(L"Inimerse-AI", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
  int rc = -1;
  if (hI) {
    HINTERNET hC = WinHttpConnect(hI, whost, (INTERNET_PORT)port, 0);
    if (hC) {
      DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
      HINTERNET hR = WinHttpOpenRequest(hC, L"POST", wpath, NULL, NULL, NULL, flags);
      if (hR) {
        char hdrs[1024];
        snprintf(hdrs, sizeof hdrs, "Content-Type: application/json\r\n%s", headers ? headers : "");
        wchar_t whdrs[1024]; LPCWSTR wh = NULL;
        if (hdrs[0]) { MultiByteToWideChar(CP_UTF8, 0, hdrs, -1, whdrs, 1024); wh = whdrs; }
        if (WinHttpSendRequest(hR, wh, wh ? (DWORD)-1L : 0, (LPVOID)body, (DWORD)strlen(body), (DWORD)strlen(body), 0)) {
          if (WinHttpReceiveResponse(hR, NULL)) {
            DWORD total = 0; char tmp[8192]; DWORD avail = 0;
            while (WinHttpQueryDataAvailable(hR, &avail) && avail > 0) {
              DWORD rd = 0;
              if (!WinHttpReadData(hR, tmp, avail < 8191 ? avail : 8191, &rd)) break;
              if (total + rd < (DWORD)cap - 1) { memcpy(out + total, tmp, rd); total += rd; }
              else break;
            }
            out[total] = '\0'; rc = 0;
          }
        }
        WinHttpCloseHandle(hR);
      }
      WinHttpCloseHandle(hC);
    }
    WinHttpCloseHandle(hI);
  }
  return rc;
}
static void ai_parse_ep(const char *ep, char *host, int hcap, int *port, char *path, int pcap, int *https) {
  *https = 0; *port = 80;
  if (!ep[0] || strcmp(ep, "ollama") == 0) {
    snprintf(host, hcap, "127.0.0.1"); *port = 11434; snprintf(path, pcap, "/api/chat"); return;
  }
  const char *p = ep;
  if (strncmp(p, "https://", 8) == 0) { *https = 1; *port = 443; p += 8; }
  else if (strncmp(p, "http://", 7) == 0) { p += 7; }
  snprintf(path, pcap, "/chat/completions");
  const char *slash = strchr(p, '/');
  char hp[256];
  if (slash) { int n = (int)(slash - p); if (n > 255) n = 255; memcpy(hp, p, n); hp[n] = 0; }
  else snprintf(hp, sizeof hp, "%s", p);
  const char *colon = strchr(hp, ':');
  if (colon) {
    snprintf(host, hcap, "%.*s", (int)(colon - hp), hp);
    *port = atoi(colon + 1);
    if (slash && slash[0]) { char rest[256]; snprintf(rest, sizeof rest, "%s", slash); snprintf(path, pcap, "%s/chat/completions", rest); }
  } else snprintf(host, hcap, "%s", hp);
}
static void ai_extract(const char *json, char *out, int cap) {
  out[0] = 0;
  const char *p = strstr(json, "\"content\":\"");
  if (!p) { snprintf(out, cap, "%s", json); return; }
  p += 11;
  const char *e = strchr(p, '"');
  if (!e) { snprintf(out, cap, "%s", json); return; }
  int n = (int)(e - p);
  if (n > cap - 1) n = cap - 1;
  memcpy(out, p, n); out[n] = 0;
}
static int ai_find_b(const char *id, char *name, int ncap, char *persona, int pcap,
                     char *model, int mcap, double *temperature, int *max_tokens, double *reply_freq) {
  char path[512], buf[8192];
  snprintf(path, sizeof path, "D:\\inimerse_stable\\userdata\\ai.json");
  ud_read(path, buf, sizeof buf);
  char pat[128]; snprintf(pat, sizeof pat, "\"id\":\"%s\"", id);
  char *p = strstr(buf, pat);
  if (!p) return 0;
  char *obj = p;
  while (obj > buf && *obj != '{') obj--;
  char *e = strchr(p, '}');
  if (!e) return 0;
  char tmp[1024];
  int n = (int)(e - obj + 1);
  if (n > 1023) n = 1023;
  memcpy(tmp, obj, n); tmp[n] = 0;
  name[0] = persona[0] = model[0] = 0;
  *temperature = 0.7; *max_tokens = 256; *reply_freq = 1.0;
  char *f = strstr(tmp, "\"name\":\"");
  if (f) { f += 8; char *q = strchr(f, '"'); if (q) { int l = (int)(q - f); if (l > ncap - 1) l = ncap - 1; memcpy(name, f, l); name[l] = 0; } }
  f = strstr(tmp, "\"persona\":\"");
  if (f) { f += 11; char *q = strchr(f, '"'); if (q) { int l = (int)(q - f); if (l > pcap - 1) l = pcap - 1; memcpy(persona, f, l); persona[l] = 0; } }
  f = strstr(tmp, "\"model\":\"");
  if (f) { f += 9; char *q = strchr(f, '"'); if (q) { int l = (int)(q - f); if (l > mcap - 1) l = mcap - 1; memcpy(model, f, l); model[l] = 0; } }
  f = strstr(tmp, "\"temperature\":");
  if (f) { f += 14; *temperature = atof(f); }
  f = strstr(tmp, "\"max_tokens\":");
  if (f) { f += 13; *max_tokens = atoi(f); }
  f = strstr(tmp, "\"reply_freq\":");
  if (f) { f += 13; *reply_freq = atof(f); }
  return 1;
}
static int ai_call_b(const char *model, const char *persona, const char *prompt,
                     double temperature, int max_tokens, char *out, int cap) {
  char ep[256], key[256], m2[128];
  ai_load_cfg(ep, sizeof ep, key, sizeof key, m2, sizeof m2);
  if (!model || !model[0]) model = m2;
  char sys[4000], usr[4000], body[9000];
  json_esc(persona ? persona : "", sys, sizeof sys);
  json_esc(prompt ? prompt : "", usr, sizeof usr);
  char host[256], path[512];
  int port, https;
  int isOllama = (!ep[0] || strcmp(ep, "ollama") == 0);
  if (isOllama) {
    snprintf(body, sizeof body, "{\"model\":\"%s\",\"messages\":[{\"role\":\"system\",\"content\":\"%s\"},{\"role\":\"user\",\"content\":\"%s\"}],\"stream\":false,\"options\":{\"temperature\":%.2f}}", model, sys, usr, temperature);
    ai_parse_ep("ollama", host, sizeof host, &port, path, sizeof path, &https);
  } else {
    snprintf(body, sizeof body, "{\"model\":\"%s\",\"messages\":[{\"role\":\"system\",\"content\":\"%s\"},{\"role\":\"user\",\"content\":\"%s\"}],\"temperature\":%.2f,\"max_tokens\":%d}", model, sys, usr, temperature, max_tokens > 0 ? max_tokens : 256);
    ai_parse_ep(ep, host, sizeof host, &port, path, sizeof path, &https);
  }
  char headers[1024] = "";
  if (!isOllama && key[0]) snprintf(headers, sizeof headers, "Authorization: Bearer %s\r\n", key);
  char raw[32768];
  raw[0] = 0;
  int rc = ai_http_post(host, port, path, https, body, headers, raw, sizeof raw);
  if (rc != 0) { snprintf(out, cap, "AI:ERR http"); return -1; }
  ai_extract(raw, out, cap);
  if (!out[0]) { snprintf(out, cap, "AI:EMPTY"); return -1; }
  return 0;
}

typedef struct { char peer[256]; char text[2048]; } AiTaskArg;
static DWORD WINAPI ai_reply_worker(LPVOID arg) {
  AiTaskArg *a = (AiTaskArg*)arg;
  char aname[128], apersona[4096], amodel[128];
  double atemp, arf; int amt;
  if (ai_find_b(a->peer, aname, sizeof aname, apersona, sizeof apersona, amodel, sizeof amodel, &atemp, &amt, &arf)) {
    char ares[32768];
    if (ai_call_b(amodel, apersona, a->text, atemp, amt, ares, sizeof ares) == 0 && ares[0] && strncmp(ares, "AI:", 3) != 0) {
      char path[512]; snprintf(path, sizeof path, "D:\\inimerse_stable\\userdata\\messages\\%s.json", a->peer);
      char esc2[4096]; json_esc(ares, esc2, sizeof esc2);
      char aentry[4096]; snprintf(aentry, sizeof aentry, "{\"from\":\"%s\",\"text\":\"%s\",\"ts\":%ld}", a->peer, esc2, (long)time(NULL));
      char aold[65536]; ud_read(path, aold, sizeof aold);
      char abuf[70000];
      char *acl = strrchr(aold, ']');
      if (acl) { size_t ah = (size_t)(acl - aold); snprintf(abuf, sizeof abuf, "%.*s,%s%s", (int)ah, aold, aentry, acl); }
      else snprintf(abuf, sizeof abuf, "{\"peer\":\"%s\",\"msgs\":[%s]}", a->peer, aentry);
      ud_write(path, abuf);
    }
  }
  free(a);
  return 0;
}

/* ---------- P2P node discovery & messaging (M3) ---------- */
#define UDP_PORT 11555
#define P2P_PORT 11556
static SOCKET g_udp = INVALID_SOCKET;
static SOCKET g_p2p = INVALID_SOCKET;
static char g_node_id[64] = "";
static char g_node_name[64] = "";
static DWORD g_announce_t = 0;
static DWORD g_node_clean_t = 0;

static void json_field(const char *json, const char *key, char *out, int cap) {
  out[0] = 0;
  char pat[128];
  snprintf(pat, sizeof pat, "\"%s\":\"", key);
  const char *p = strstr(json, pat);
  if (!p) return;
  p += strlen(pat);
  const char *e = strchr(p, '"');
  if (!e) return;
  int n = (int)(e - p);
  if (n > cap - 1) n = cap - 1;
  memcpy(out, p, n);
  out[n] = 0;
}
static long json_long(const char *json, const char *key) {
  char pat[128];
  snprintf(pat, sizeof pat, "\"%s\":", key);
  const char *p = strstr(json, pat);
  if (!p) return 0;
  p += strlen(pat);
  while (*p == ' ') p++;
  return atol(p);
}
static void p2p_node_self(void) {
  char path[512], buf[4096];
  snprintf(path, sizeof path, "D:\\inimerse_stable\\userdata\\profile.json");
  ud_read(path, buf, sizeof buf);
  char *p = strstr(buf, "\"id\":\"");
  if (p) { p += 6; char *e = strchr(p, '"'); if (e) { int n = (int)(e - p); if (n > 63) n = 63; memcpy(g_node_id, p, n); g_node_id[n] = 0; } }
  p = strstr(buf, "\"name\":\"");
  if (p) { p += 8; char *e = strchr(p, '"'); if (e) { int n = (int)(e - p); if (n > 63) n = 63; memcpy(g_node_name, p, n); g_node_name[n] = 0; } }
}
static void p2p_get_lan_ip(char *out, int cap) {
  char hostn[256] = "";
  gethostname(hostn, sizeof hostn);
  struct addrinfo hints, *res = NULL;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  snprintf(out, cap, "127.0.0.1");
  if (getaddrinfo(hostn, NULL, &hints, &res) == 0) {
    for (struct addrinfo *p0 = res; p0; p0 = p0->ai_next) {
      struct sockaddr_in *sa = (struct sockaddr_in*)p0->ai_addr;
      unsigned long a = ntohl(sa->sin_addr.s_addr);
      if ((a >> 24) == 127 || (a >> 24) == 169) continue;
      snprintf(out, cap, "%lu.%lu.%lu.%lu", (a >> 24) & 255, (a >> 16) & 255, (a >> 8) & 255, a & 255);
      break;
    }
    freeaddrinfo(res);
  }
}
static void p2p_init(void) {
  p2p_node_self();
  g_udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (g_udp != INVALID_SOCKET) {
    int bcast = 1;
    setsockopt(g_udp, SOL_SOCKET, SO_BROADCAST, (const char*)&bcast, sizeof bcast);
    struct sockaddr_in ba;
    memset(&ba, 0, sizeof ba);
    ba.sin_family = AF_INET;
    ba.sin_addr.s_addr = htonl(INADDR_ANY);
    ba.sin_port = htons(UDP_PORT);
    bind(g_udp, (struct sockaddr*)&ba, sizeof ba);
    u_long nb = 1;
    ioctlsocket(g_udp, FIONBIO, &nb);
  }
  g_p2p = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (g_p2p != INVALID_SOCKET) {
    int reuse = 1;
    setsockopt(g_p2p, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof reuse);
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port = htons(P2P_PORT);
    if (bind(g_p2p, (struct sockaddr*)&sa, sizeof sa) == 0) {
      listen(g_p2p, 8);
      u_long nb = 1;
      ioctlsocket(g_p2p, FIONBIO, &nb);
      fprintf(stderr, "[p2p] node %s (%s) listening :%d\n", g_node_id, g_node_name, P2P_PORT);
    } else {
      closesocket(g_p2p);
      g_p2p = INVALID_SOCKET;
    }
  }
}
static void p2p_announce(void) {
  if (g_udp == INVALID_SOCKET) return;
  char ip[64];
  p2p_get_lan_ip(ip, sizeof ip);
  char buf[512];
  snprintf(buf, sizeof buf, "{\"op\":\"hello\",\"id\":\"%s\",\"name\":\"%s\",\"ip\":\"%s\",\"p2p\":%d}",
           g_node_id, g_node_name, ip, P2P_PORT);
  struct sockaddr_in ba;
  memset(&ba, 0, sizeof ba);
  ba.sin_family = AF_INET;
  ba.sin_addr.s_addr = htonl(INADDR_BROADCAST);
  ba.sin_port = htons(UDP_PORT);
  sendto(g_udp, buf, (int)strlen(buf), 0, (struct sockaddr*)&ba, sizeof ba);
}
static void p2p_node_upsert(const char *id, const char *name, const char *ip, int p2p) {
  if (!id[0] || strcmp(id, g_node_id) == 0) return;
  char path[512], buf[16384];
  snprintf(path, sizeof path, "D:\\inimerse_stable\\userdata\\nodes.json");
  ud_read(path, buf, sizeof buf);
  long now = (long)time(NULL);
  char en[512];
  snprintf(en, sizeof en, "{\"id\":\"%s\",\"name\":\"%s\",\"ip\":\"%s\",\"p2p\":%d,\"last_seen\":%ld}",
           id, name, ip, p2p, now);
  char pat[128];
  snprintf(pat, sizeof pat, "\"id\":\"%s\"", id);
  char out[17000];
  if (strstr(buf, pat)) {
    /* replace existing entry */
    char *p = strstr(buf, pat);
    char *obj = p;
    while (obj > buf && *obj != '{') obj--;
    char *e = strchr(p, '}');
    if (e) {
      size_t head = (size_t)(obj - buf);
      snprintf(out, sizeof out, "%.*s%s%s", (int)head, buf, en, e + 1);
      ud_write(path, out);
    }
  } else {
    char nb[17000];
    if (strstr(buf, "\"id\":\"") == NULL) snprintf(nb, sizeof nb, "{\"nodes\":[%s]}", en);
    else {
      char *cl = strrchr(buf, ']');
      if (cl) { size_t h = (size_t)(cl - buf); snprintf(nb, sizeof nb, "%.*s,%s%s", (int)h, buf, en, cl); }
      else snprintf(nb, sizeof nb, "{\"nodes\":[%s]}", en);
    }
    ud_write(path, nb);
  }
}
static void p2p_clean_nodes(void) {
  char path[512], buf[16384];
  snprintf(path, sizeof path, "D:\\inimerse_stable\\userdata\\nodes.json");
  ud_read(path, buf, sizeof buf);
  if (!buf[0]) return;
  long now = (long)time(NULL);
  char out[16384];
  int oi = 0;
  snprintf(out + oi, sizeof out - oi, "{\"nodes\":[");
  oi += 10;
  const char *p = buf;
  int first = 1;
  while (p && *p) {
    const char *objStart = strchr(p, '{');
    if (!objStart) break;
    const char *objEnd = strchr(objStart, '}');
    if (!objEnd) break;
    char obj[600];
    int n = (int)(objEnd - objStart + 1);
    if (n > 599) n = 599;
    memcpy(obj, objStart, n);
    obj[n] = 0;
    p = objEnd + 1;
    long ls = 0;
    char *f = strstr(obj, "\"last_seen\":");
    if (f) ls = atol(f + 12);
    if (now - ls < 35) {
      if (!first) { out[oi++] = ','; out[oi] = 0; }
      first = 0;
      snprintf(out + oi, sizeof out - oi, "%s", obj);
      oi += (int)strlen(obj);
    }
  }
  if (oi > 11) oi--; /* remove trailing comma if any */
  out[oi] = 0;
  int ol = (int)strlen(out);
  if (ol >= 12 && out[ol - 1] == ',') out[--ol] = 0;
  snprintf(out + strlen(out), sizeof out - strlen(out), "]}");
  ud_write(path, out);
}
static void p2p_send_ip(const char *ip, int port, const char *json) {
  if (!ip[0]) return;
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return;
  struct sockaddr_in sa;
  memset(&sa, 0, sizeof sa);
  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = inet_addr(ip);
  sa.sin_port = htons((unsigned short)port);
  if (connect(s, (struct sockaddr*)&sa, sizeof sa) == 0) {
    char msg[7000];
    snprintf(msg, sizeof msg, "%s\n", json);
    send(s, msg, (int)strlen(msg), 0);
  }
  closesocket(s);
}
static void p2p_send_to(const char *peer_id, const char *json) {
  char path[512], buf[16384];
  snprintf(path, sizeof path, "D:\\inimerse_stable\\userdata\\nodes.json");
  ud_read(path, buf, sizeof buf);
  char pat[128];
  snprintf(pat, sizeof pat, "\"id\":\"%s\"", peer_id);
  char *p = strstr(buf, pat);
  if (!p) return;
  char ip[64] = "";
  int port = P2P_PORT;
  char *f = strstr(p, "\"ip\":\"");
  if (f) { f += 6; char *e = strchr(f, '"'); if (e) { int n = (int)(e - f); if (n > 63) n = 63; memcpy(ip, f, n); ip[n] = 0; } }
  f = strstr(p, "\"p2p\":");
  if (f) { f += 6; port = atoi(f); }
  p2p_send_ip(ip, port, json);
}
static void p2p_dispatch(const char *json) {
  char op[64] = "", from[128] = "", to[128] = "", text[2048] = "";
  long ts = 0;
  json_field(json, "op", op, sizeof op);
  if (strcmp(op, "chat") == 0) {
    json_field(json, "from", from, sizeof from);
    json_field(json, "to", to, sizeof to);
    json_field(json, "text", text, sizeof text);
    ts = json_long(json, "ts");
    if (!from[0]) return;
    char path[512];
    snprintf(path, sizeof path, "D:\\inimerse_stable\\userdata\\messages\\%s.json", from);
    char esc[4096];
    json_esc(text, esc, sizeof esc);
    char entry[4096];
    snprintf(entry, sizeof entry, "{\"from\":\"%s\",\"text\":\"%s\",\"ts\":%ld}", from, esc, ts);
    char aold[65536];
    ud_read(path, aold, sizeof aold);
    char abuf[70000];
    char *acl = strrchr(aold, ']');
    if (acl) { size_t ah = (size_t)(acl - aold); snprintf(abuf, sizeof abuf, "%.*s,%s%s", (int)ah, aold, entry, acl); }
    else snprintf(abuf, sizeof abuf, "{\"peer\":\"%s\",\"msgs\":[%s]}", from, entry);
    ud_write(path, abuf);
    fprintf(stderr, "[p2p] chat from %s\n", from);
  } else if (strcmp(op, "room_query") == 0) {
    int room = (int)json_long(json, "room");
    json_field(json, "from", from, sizeof from);
    if (room > 0 && from[0] && port_listening(room)) {
      char ip2[64];
      p2p_get_lan_ip(ip2, sizeof ip2);
      char resp[512];
      snprintf(resp, sizeof resp, "{\"op\":\"room_resp\",\"room\":%d,\"ip\":\"%s\"}", room, ip2);
      p2p_send_to(from, resp);
    }
  } else if (strcmp(op, "profile") == 0) {
    json_field(json, "from", from, sizeof from);
    char path[512], pbuf[4096];
    snprintf(path, sizeof path, "D:\\inimerse_stable\\userdata\\profile.json");
    ud_read(path, pbuf, sizeof pbuf);
    char resp[4600];
    snprintf(resp, sizeof resp, "{\"op\":\"profile_resp\",\"to\":\"%s\",\"profile\":%s}", from, pbuf);
    p2p_send_to(from, resp);
  }
}
static void p2p_poll_udp(void) {
  if (g_udp == INVALID_SOCKET) return;
  char buf[1024];
  struct sockaddr_in fr;
  int fl = sizeof fr;
  int n = recvfrom(g_udp, buf, sizeof buf - 1, 0, (struct sockaddr*)&fr, &fl);
  if (n > 0) {
    buf[n] = 0;
    char op[64] = "", id[128] = "", name[128] = "", ip[64] = "";
    int p2p = P2P_PORT;
    json_field(buf, "op", op, sizeof op);
    if (strcmp(op, "hello") == 0) {
      json_field(buf, "id", id, sizeof id);
      json_field(buf, "name", name, sizeof name);
      json_field(buf, "ip", ip, sizeof ip);
      p2p = (int)json_long(buf, "p2p");
      if (!ip[0]) inet_ntop(AF_INET, &fr.sin_addr, ip, sizeof ip);
      if (id[0]) p2p_node_upsert(id, name, ip, p2p);
    }
  }
}
static void p2p_poll_accept(void) {
  if (g_p2p == INVALID_SOCKET) return;
  for (;;) {
    SOCKET cs = accept(g_p2p, NULL, NULL);
    if (cs == INVALID_SOCKET) {
      if (WSAGetLastError() == WSAEWOULDBLOCK) break;
      break;
    }
    /* wait briefly for data (short-lived message connections) */
    fd_set rd;
    FD_ZERO(&rd);
    FD_SET(cs, &rd);
    struct timeval tv = { 0, 200000 };
    char buf[8192];
    int n = -1;
    if (select(0, &rd, NULL, NULL, &tv) > 0) {
      n = recv(cs, buf, sizeof buf - 1, 0);
    }
    if (n > 0) {
      buf[n] = 0;
      char *nl = strchr(buf, '\n');
      if (nl) *nl = 0;
      p2p_dispatch(buf);
    }
    closesocket(cs);
  }
}

/* lookup friend's node ip in friends.json */
static void friend_node(const char *id, char *out, int cap) {
  out[0] = 0;
  char path[512], buf[8192];
  snprintf(path, sizeof path, "D:\\inimerse_stable\\userdata\\friends.json");
  ud_read(path, buf, sizeof buf);
  char pat[128];
  snprintf(pat, sizeof pat, "\"id\":\"%s\"", id);
  char *p = strstr(buf, pat);
  if (!p) return;
  char *f = strstr(p, "\"node\":\"");
  if (f) { f += 8; char *e = strchr(f, '"'); if (e) { int n = (int)(e - f); if (n > cap - 1) n = cap - 1; memcpy(out, f, n); out[n] = 0; } }
}
static void me_id(char *out, int cap) {
  out[0] = 0;
  char path[512], buf[4096];
  snprintf(path, sizeof path, "D:\\inimerse_stable\\userdata\\profile.json");
  ud_read(path, buf, sizeof buf);
  char *p = strstr(buf, "\"id\":\"");
  if (p) { p += 6; char *e = strchr(p, '"'); if (e) { int n = (int)(e - p); if (n > cap - 1) n = cap - 1; memcpy(out, p, n); out[n] = 0; } }
}
static int http_serve_page(SOCKET s, char *reqbuf, int got) {
    if (ci_strstr(reqbuf, "upgrade: websocket")) return 0; /* WS: caller handles (case-insensitive) */
    if (strstr(reqbuf, "GET /forge") || strstr(reqbuf, "GET /vf")) {
        const char *pg = FORGE_HTML;
        int plen2 = (int)strlen(pg);
        char resp2[512];
        snprintf(resp2, sizeof resp2,
            "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
            "Access-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", plen2);
        if (send(s, resp2, (int)strlen(resp2), 0) == SOCKET_ERROR) return -1;
        if (send(s, pg, plen2, 0) == SOCKET_ERROR) return -1;
        return 1;
    }

    /* /netplay: multiplayer lobby page (g16) */
    if (strstr(reqbuf, "GET /netplay") || strstr(reqbuf, "GET /np")) {
        const char *pg = NETPLAY_HTML;
        int plen2 = (int)strlen(pg);
        char resp2[512];
        snprintf(resp2, sizeof resp2,
            "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
            "Access-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", plen2);
        if (send(s, resp2, (int)strlen(resp2), 0) == SOCKET_ERROR) return -1;
        if (send(s, pg, plen2, 0) == SOCKET_ERROR) return -1;
        return 1;
    }
    /* /projects: list available projects (dirs under projects/ with main.im) */
    if (strstr(reqbuf, "GET /desktop")) {
  const char *pg = DESKTOP_HTML;
  char res[16384];
  snprintf(res, sizeof res, "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nContent-Length: %d\r\n\r\n%s", (int)strlen(pg), pg);
  send(s, res, (int)strlen(res), 0);
  return 1;
}
if (strstr(reqbuf, "GET /chat")) {
  const char *pg = CHAT_HTML;
  char res[16384];
  snprintf(res, sizeof res, "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nContent-Length: %d\r\n\r\n%s", (int)strlen(pg), pg);
  send(s, res, (int)strlen(res), 0);
  return 1;
}
if (strstr(reqbuf, "GET /home") || strstr(reqbuf, "GET /me") || strstr(reqbuf, "GET /profile")) {
  const char *pg = HOME_HTML;
  char res[16384];
  snprintf(res, sizeof res, "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nContent-Length: %d\r\n\r\n%s", (int)strlen(pg), pg);
  send(s, res, (int)strlen(res), 0);
  return 1;
}
if (strstr(reqbuf, "GET /projects")) {
        char jbuf[2048]; int jp = 0;
        jbuf[jp++] = '[';
        WIN32_FIND_DATAA fd;
        HANDLE hf = FindFirstFileA("D:\\inimerse_stable\\projects\\*", &fd);
        if (hf != INVALID_HANDLE_VALUE) {
            int first = 1;
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                if (fd.cFileName[0] == '.') continue;
                char mp[512];
                snprintf(mp, sizeof mp, "D:\\inimerse_stable\\projects\\%s\\main.im", fd.cFileName);
                FILE *chk = fopen(mp, "rb");
                if (!chk) continue;
                fclose(chk);
                if (!first) jbuf[jp++] = ',';
                jbuf[jp++] = '"';
                for (char *cp = fd.cFileName; *cp && jp < (int)sizeof jbuf - 8; cp++) {
                    if (*cp == '"') jbuf[jp++] = '\\';
                    jbuf[jp++] = *cp;
                }
                jbuf[jp++] = '"';
                first = 0;
            } while (jp < (int)sizeof jbuf - 64 && FindNextFileA(hf, &fd));
            FindClose(hf);
        }
        jbuf[jp++] = ']';
        char resp[2048];
        snprintf(resp, sizeof resp, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", jp);
        send(s, resp, (int)strlen(resp), 0);
        send(s, jbuf, jp, 0);
        return 1;
    }
    /* /server/create?project=X&pass=Y : start a room server, return {room,url} */
    if (strstr(reqbuf, "/server/create")) {
        char proj[128] = "demo", pass[128] = "";
        const char *pp = strstr(reqbuf, "project=");
        if (pp) { const char *e = strchr(pp, '&'); if (!e) e = strchr(pp, ' '); int l = e ? (int)(e - pp - 8) : 100; if (l > 120) l = 120; if (l > 0) { memcpy(proj, pp + 8, l); proj[l] = 0; } }
        const char *pa = strstr(reqbuf, "pass=");
        if (pa) { const char *e = strchr(pa, '&'); if (!e) e = strchr(pa, ' '); int l = e ? (int)(e - pa - 5) : 100; if (l > 120) l = 120; if (l > 0) { memcpy(pass, pa + 5, l); pass[l] = 0; } }
        /* url-decode pass (only %XX, keep simple) */
        char dp[128]; int dw = 0;
        for (char *cp = pass; *cp && dw < 120; cp++) {
            if (*cp == '%' && cp[1] && cp[2]) {
                int h1 = (cp[1] >= '0' && cp[1] <= '9') ? cp[1]-'0' : ((cp[1]|32)-'a'+10);
                int h2 = (cp[2] >= '0' && cp[2] <= '9') ? cp[2]-'0' : ((cp[2]|32)-'a'+10);
                dp[dw++] = (char)((h1<<4)|h2); cp += 2;
            } else dp[dw++] = *cp;
        }
        dp[dw] = 0;
        /* find free room port */
        int room = 0;
        for (int p = 11510; p < 11700; p += 10) {
            if (!port_listening(p) && !port_listening(p+1)) { room = p; break; }
        }
        char out[400];
        if (!room) {
            snprintf(out, sizeof out, "{\"error\":\"no free port\"}");
        } else {
            char cmd2[900];
            snprintf(cmd2, sizeof cmd2, "\"D:\\inimerse_stable\\inimerse.exe\" --headless --port %d --http-port %d --time-limit 0 \"D:\\inimerse_stable\\projects\\%s\\main.im\"", room, room+10, proj);
            STARTUPINFOA si2; PROCESS_INFORMATION pi2;
            memset(&si2, 0, sizeof si2); si2.cb = sizeof si2;
            CreateProcessA(NULL, cmd2, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, "D:\\inimerse_stable", &si2, &pi2);
            if (pi2.hProcess) CloseHandle(pi2.hProcess);
            char cmd3[500];
            snprintf(cmd3, sizeof cmd3, "\"D:\\inimerse_stable\\hl_bridge.exe\" %d %d", room, room+1);
            memset(&si2, 0, sizeof si2); si2.cb = sizeof si2;
            CreateProcessA(NULL, cmd3, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, "D:\\inimerse_stable", &si2, &pi2);
            if (pi2.hProcess) CloseHandle(pi2.hProcess);
            /* room file (shared with server mod) */
            CreateDirectoryA("D:\\inimerse_stable\\rooms", NULL);
            char rf[512];
            snprintf(rf, sizeof rf, "D:\\inimerse_stable\\rooms\\%d.txt", room);
            FILE *rfh = fopen(rf, "wb");
            if (rfh) { fprintf(rfh, "project=%s\npass=%s\n", proj, dp); fclose(rfh); }
            char ip[64] = "127.0.0.1";
            { char hn[256] = ""; gethostname(hn, sizeof hn);
              struct addrinfo hi, *hr = NULL;
              memset(&hi, 0, sizeof hi); hi.ai_family = AF_INET; hi.ai_socktype = SOCK_STREAM;
              if (getaddrinfo(hn, NULL, &hi, &hr) == 0) {
                  for (struct addrinfo *ai = hr; ai; ai = ai->ai_next) {
                      struct sockaddr_in *sa = (struct sockaddr_in*)ai->ai_addr;
                      unsigned long a = ntohl(sa->sin_addr.s_addr);
                      if ((a>>24)==127 || (a>>24)==169) continue;
                      sprintf(ip, "%lu.%lu.%lu.%lu", (a>>24)&255, (a>>16)&255, (a>>8)&255, a&255);
                      break;
                  }
                  freeaddrinfo(hr);
              }
            }
            snprintf(out, sizeof out, "{\"room\":%d,\"url\":\"http://%s:%d/\"}", room, ip, room+1);
        }
        int ol = (int)strlen(out);
        char resp[400];
        snprintf(resp, sizeof resp, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", ol);
        send(s, resp, (int)strlen(resp), 0);
        send(s, out, ol, 0);
        return 1;
    }
    /* /server/join?room=R&pass=Y : verify password, return {url} or {error} */
    if (strstr(reqbuf, "/server/join")) {
        int room = 0; char pass[128] = "";
        const char *rq = strstr(reqbuf, "room=");
        if (rq) room = atoi(rq + 5);
        const char *pa = strstr(reqbuf, "pass=");
        if (pa) { const char *e = strchr(pa, '&'); if (!e) e = strchr(pa, ' '); int l = e ? (int)(e - pa - 5) : 100; if (l > 120) l = 120; if (l > 0) { memcpy(pass, pa + 5, l); pass[l] = 0; } }
        char dp[128]; int dw = 0;
        for (char *cp = pass; *cp && dw < 120; cp++) {
            if (*cp == '%' && cp[1] && cp[2]) {
                int h1 = (cp[1] >= '0' && cp[1] <= '9') ? cp[1]-'0' : ((cp[1]|32)-'a'+10);
                int h2 = (cp[2] >= '0' && cp[2] <= '9') ? cp[2]-'0' : ((cp[2]|32)-'a'+10);
                dp[dw++] = (char)((h1<<4)|h2); cp += 2;
            } else dp[dw++] = *cp;
        }
        dp[dw] = 0;
        char out[300];
        char rf[512];
        snprintf(rf, sizeof rf, "D:\\inimerse_stable\\rooms\\%d.txt", room);
        FILE *rfh = fopen(rf, "rb");
        if (!rfh || !port_listening(room)) {
            if (rfh) fclose(rfh);
            snprintf(out, sizeof out, "{\"error\":\"ROOM_NOT_FOUND\"}");
        } else {
            char line[256]; int okp = 0;
            while (fgets(line, sizeof line, rfh)) {
                if (strncmp(line, "pass=", 5) == 0) {
                    line[strcspn(line, "\r\n")] = 0;
                    if (strcmp(line + 5, dp) == 0) okp = 1;
                }
            }
            fclose(rfh);
            if (!okp) {
                snprintf(out, sizeof out, "{\"error\":\"WRONG_PASSWORD\"}");
            } else {
                char ip[64] = "127.0.0.1";
                char hn[256] = ""; gethostname(hn, sizeof hn);
                struct addrinfo hi, *hr = NULL;
                memset(&hi, 0, sizeof hi); hi.ai_family = AF_INET; hi.ai_socktype = SOCK_STREAM;
                if (getaddrinfo(hn, NULL, &hi, &hr) == 0) {
                    for (struct addrinfo *ai = hr; ai; ai = ai->ai_next) {
                        struct sockaddr_in *sa = (struct sockaddr_in*)ai->ai_addr;
                        unsigned long a = ntohl(sa->sin_addr.s_addr);
                        if ((a>>24)==127 || (a>>24)==169) continue;
                        sprintf(ip, "%lu.%lu.%lu.%lu", (a>>24)&255, (a>>16)&255, (a>>8)&255, a&255);
                        break;
                    }
                    freeaddrinfo(hr);
                }
                snprintf(out, sizeof out, "{\"url\":\"http://%s:%d/\"}", ip, room+1);
            }
        }
        int ol = (int)strlen(out);
        char resp[300];
        snprintf(resp, sizeof resp, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", ol);
        send(s, resp, (int)strlen(resp), 0);
        send(s, out, ol, 0);
        return 1;
    }
    /* /ip: return this machine's LAN IP */
    

/* ---- userdata API (M1) ---- */

/* ---- AI friends API (M2) ---- */
if (strstr(reqbuf, "GET /api/ai ") || strstr(reqbuf, "GET /api/ai?")) {
  char buf[8192]; char path[512];
  snprintf(path, sizeof path, "D:\\inimerse_stable\\userdata\\ai.json");
  ud_read(path, buf, sizeof buf);
  if (!buf[0]) http_json(s, "{\"ais\":[]}");
  else http_json(s, buf);
  return 1;
}
if (strstr(reqbuf, "/api/ai/register")) {
  char name[256], persona[4096];
  qparam(reqbuf, "name", name, sizeof name);
  qparam(reqbuf, "persona", persona, sizeof persona);
  CreateDirectoryA("D:\\inimerse_stable\\userdata", NULL);
  srand((unsigned)(time(NULL) ^ GetTickCount()));
  char id[64]; snprintf(id, sizeof id, "ai_%08x", (unsigned)rand());
  char path[512], old[8192], en[2048], buf[9216];
  snprintf(path, sizeof path, "D:\\inimerse_stable\\userdata\\ai.json");
  ud_read(path, old, sizeof old);
  char pe[4096]; json_esc(persona, pe, sizeof pe);
  snprintf(en, sizeof en, "{\"id\":\"%s\",\"name\":\"%s\",\"persona\":\"%s\",\"model\":\"qwen2.5:7b\",\"temperature\":0.7,\"max_tokens\":256,\"reply_freq\":1.0}", id, name, pe);
  if (strstr(old, "\"id\":\"") == NULL) snprintf(buf, sizeof buf, "{\"ais\":[%s]}", en);
  else { char *cl = strrchr(old, ']'); if (cl) { size_t h = (size_t)(cl - old); snprintf(buf, sizeof buf, "%.*s,%s%s", (int)h, old, en, cl); } else snprintf(buf, sizeof buf, "{\"ais\":[%s]}", en); }
  ud_write(path, buf);
  snprintf(path, sizeof path, "D:\\inimerse_stable\\userdata\\friends.json");
  ud_read(path, old, sizeof old);
  snprintf(en, sizeof en, "{\"id\":\"%s\",\"name\":\"%s\",\"added\":%ld,\"is_ai\":1}", id, name, (long)time(NULL));
  if (strstr(old, "\"id\":\"") == NULL) snprintf(buf, sizeof buf, "{\"friends\":[%s]}", en);
  else { char *cl = strrchr(old, ']'); if (cl) { size_t h = (size_t)(cl - old); snprintf(buf, sizeof buf, "%.*s,%s%s", (int)h, old, en, cl); } else snprintf(buf, sizeof buf, "{\"friends\":[%s]}", en); }
  ud_write(path, buf);
  char rj[512]; snprintf(rj, sizeof rj, "{\"ok\":1,\"id\":\"%s\"}", id);
  http_json(s, rj);
  return 1;
}
if (strstr(reqbuf, "/api/ai/params")) {
  char id[256], t[64], m[64], r[64];
  qparam(reqbuf, "id", id, sizeof id);
  qparam(reqbuf, "temperature", t, sizeof t);
  qparam(reqbuf, "max_tokens", m, sizeof m);
  qparam(reqbuf, "reply_freq", r, sizeof r);
  char path[512], buf[8192];
  snprintf(path, sizeof path, "D:\\inimerse_stable\\userdata\\ai.json");
  ud_read(path, buf, sizeof buf);
  char pat[128]; snprintf(pat, sizeof pat, "\"id\":\"%s\"", id);
  char *p = strstr(buf, pat);
  if (!p) { http_json(s, "{\"ok\":0}"); return 1; }
  char *obj = p; while (obj > buf && *obj != '{') obj--;
  char *e = strchr(p, '}');
  if (!e) { http_json(s, "{\"ok\":0}"); return 1; }
  char an[128], ap[4096], am[128]; double at, ar; int amt;
  ai_find_b(id, an, sizeof an, ap, sizeof ap, am, sizeof am, &at, &amt, &ar);
  double ntemp = t[0] ? atof(t) : at;
  int ntok = m[0] ? atoi(m) : amt;
  double nrf = r[0] ? atof(r) : ar;
  char en[1536];
  snprintf(en, sizeof en, "{\"id\":\"%s\",\"name\":\"%s\",\"persona\":\"%s\",\"model\":\"%s\",\"temperature\":%.2f,\"max_tokens\":%d,\"reply_freq\":%.2f}", id, an, ap, am, ntemp, ntok, nrf);
  char nb[9216];
  size_t head = (size_t)(obj - buf);
  snprintf(nb, sizeof nb, "%.*s%s%s", (int)head, buf, en, e + 1);
  ud_write(path, nb);
  http_json(s, "{\"ok\":1}");
  return 1;
}

/* ---- P2P API (M3) ---- */
if (strstr(reqbuf, "GET /api/nodes")) {
  char buf[16384]; char path[512];
  snprintf(path, sizeof path, "D:\\inimerse_stable\\userdata\\nodes.json");
  ud_read(path, buf, sizeof buf);
  if (!buf[0]) http_json(s, "{\"nodes\":[]}");
  else http_json(s, buf);
  return 1;
}
if (strstr(reqbuf, "GET /api/projtree")) {
  char proj[128];
  qparam(reqbuf, "proj", proj, sizeof proj);
  char dir[600];
  snprintf(dir, sizeof dir, "D:\\inimerse_stable\\projects\\%s", proj);
  char *out2 = (char*)malloc(32768);
  if (out2) {
    int op = 0;
    op += snprintf(out2 + op, 32767 - op, "{\"proj\":\"%s\",\"files\":[", proj);
    proj_walk(dir, "", out2, &op, 32767);
    op += snprintf(out2 + op, 32767 - op, "]}");
    http_json(s, out2);
    free(out2);
  }
  return 1;
}
if (strstr(reqbuf, "GET /api/projfile")) {
  char proj[128], path[512];
  qparam(reqbuf, "proj", proj, sizeof proj);
  qparam(reqbuf, "path", path, sizeof path);
  if (!proj_safe(proj) || !proj_safe(path)) { http_json(s, "{\"err\":\"bad path\"}"); return 1; }
  char full[600];
  snprintf(full, sizeof full, "D:\\inimerse_stable\\projects\\%s\\%s", proj, path);
  size_t n = 0;
  char *buf = (char*)malloc(262144);
  FILE *f = fopen(full, "rb");
  if (f && buf) { n = fread(buf, 1, 262143, f); fclose(f); }
  if (buf) {
    buf[n] = 0;
    char *esc = (char*)malloc(524288);
    char *res = (char*)malloc(524416);
    if (esc && res) {
      json_esc(buf, esc, 524288);
      snprintf(res, 524416, "{\"path\":\"%s\",\"size\":%zu,\"content\":\"%s\"}", path, n, esc);
      http_json(s, res);
    }
    free(esc); free(res); free(buf);
  }
  return 1;
}
if (strstr(reqbuf, "POST /api/projfile")) {
  char proj[128], path[512];
  char *content = (char*)malloc(262144);
  qparam(reqbuf, "proj", proj, sizeof proj);
  qparam(reqbuf, "path", path, sizeof path);
  qparam(reqbuf, "content", content, 262144);
  if (!proj_safe(proj) || !proj_safe(path)) { free(content); http_json(s, "{\"err\":\"bad path\"}"); return 1; }
  char full[600];
  snprintf(full, sizeof full, "D:\\inimerse_stable\\projects\\%s\\%s", proj, path);
  FILE *f = fopen(full, "wb");
  if (!f) { free(content); http_json(s, "{\"err\":\"write fail\"}"); return 1; }
  fwrite(content, 1, strlen(content), f);
  fclose(f);
  free(content);
  http_json(s, "{\"ok\":1}");
  return 1;
}
if (strstr(reqbuf, "GET /api/me")) {
  ud_ensure_profile();
  char buf[4096]; char path[512];
  snprintf(path, sizeof path, "D:\\inimerse_stable\\userdata\\profile.json");
  http_json(s, ud_read(path, buf, sizeof buf));
  return 1;
}
if (strstr(reqbuf, "/api/profile")) {
  char name[256], bio[1024], avatar[256];
  qparam(reqbuf, "name", name, sizeof name);
  qparam(reqbuf, "bio", bio, sizeof bio);
  qparam(reqbuf, "avatar", avatar, sizeof avatar);
  ud_ensure_profile();
  char path[512]; snprintf(path, sizeof path, "D:\\inimerse_stable\\userdata\\profile.json");
  char old[4096]; ud_read(path, old, sizeof old);
  char id[64] = "";
  char *ps = strstr(old, "\"id\":\"");
  if (ps) { ps += 6; char *pe = strchr(ps, '"'); if (pe) { int nn = (int)(pe - ps); if (nn > 63) nn = 63; memcpy(id, ps, nn); id[nn] = 0; } }
  char nb[256]; qparam(reqbuf, "name", nb, sizeof nb); if (!nb[0]) snprintf(nb, sizeof nb, "%s", id);
  char buf[4096];
  snprintf(buf, sizeof buf, "{\"id\":\"%s\",\"name\":\"%s\",\"avatar\":\"%s\",\"bio\":\"%s\",\"created\":%ld}",
           id, nb, avatar, bio, (long)time(NULL));
  ud_write(path, buf);
  http_json(s, "{\"ok\":1}");
  return 1;
}
if (strstr(reqbuf, "/api/friend/del")) {
  char id[256]; qparam(reqbuf, "id", id, sizeof id);
  char path[512]; snprintf(path, sizeof path, "D:\\inimerse_stable\\userdata\\friends.json");
  char old[8192]; ud_read(path, old, sizeof old);
  char pat[128]; snprintf(pat, sizeof pat, "{\"id\":\"%s\"", id);
  char out[8192]; int oi = 0;
  const char *p = old;
  while (p && *p) {
    const char *m = strstr(p, pat);
    if (!m) { while (*p) out[oi++] = *p++; break; }
    int head = (int)(m - p);
    memcpy(out + oi, p, head); oi += head;
    const char *e = strchr(m, '}');
    if (!e) { while (*p) out[oi++] = *p++; break; }
    p = e + 1;
    if (*p == ',') p++;
  }
  out[oi] = 0;
  char tmp[8192]; snprintf(tmp, sizeof tmp, "%s", out);
  memset(out, 0, sizeof out);
  int ti = 0;
  for (int i = 0; tmp[i]; i++) {
    if (tmp[i] == ',' && (tmp[i + 1] == ',' || tmp[i + 1] == ']')) continue;
    out[ti++] = tmp[i];
  }
  out[ti] = 0;
  ud_write(path, out);
  http_json(s, "{\"ok\":1}");
  return 1;
}
if (strstr(reqbuf, "/api/friend?")) {
  char name[256], id[256], node[256];
  qparam(reqbuf, "name", name, sizeof name);
  qparam(reqbuf, "id", id, sizeof id);
  qparam(reqbuf, "node", node, sizeof node);
  ud_ensure_profile();
  char path[512]; snprintf(path, sizeof path, "D:\\inimerse_stable\\userdata\\friends.json");
  char old[8192]; ud_read(path, old, sizeof old);
  char en[600];
  if (node[0]) snprintf(en, sizeof en, "{\"id\":\"%s\",\"name\":\"%s\",\"added\":%ld,\"node\":\"%s\"}", id, name, (long)time(NULL), node);
  else snprintf(en, sizeof en, "{\"id\":\"%s\",\"name\":\"%s\",\"added\":%ld}", id, name, (long)time(NULL));
  char buf[9216];
  if (strstr(old, "\"id\":\"") == NULL) snprintf(buf, sizeof buf, "{\"friends\":[%s]}", en);
  else {
    char *cl = strrchr(old, ']');
    if (cl) { size_t h = (size_t)(cl - old); snprintf(buf, sizeof buf, "%.*s,%s%s", (int)h, old, en, cl); }
    else snprintf(buf, sizeof buf, "{\"friends\":[%s]}", en);
  }
  ud_write(path, buf);
  http_json(s, "{\"ok\":1}");
  return 1;
}
if (strstr(reqbuf, "GET /api/friends")) {
  char buf[16384]; char path[512];
  snprintf(path, sizeof path, "D:\\inimerse_stable\\userdata\\friends.json");
  ud_read(path, buf, sizeof buf);
  if (!buf[0]) http_json(s, "{\"friends\":[]}");
  else http_json(s, buf);
  return 1;
}
if (strstr(reqbuf, "GET /api/chat")) {
  char peer[256]; qparam(reqbuf, "peer", peer, sizeof peer);
  char path[512]; snprintf(path, sizeof path, "D:\\inimerse_stable\\userdata\\messages\\%s.json", peer);
  char buf[65536]; ud_read(path, buf, sizeof buf);
  if (!buf[0]) { char b2[512]; snprintf(b2, sizeof b2, "{\"peer\":\"%s\",\"msgs\":[]}", peer); http_json(s, b2); }
  else http_json(s, buf);
  return 1;
}
if (strstr(reqbuf, "POST /api/chat")) {
  char peer[256], text[2048];
  qparam(reqbuf, "peer", peer, sizeof peer);
  qparam(reqbuf, "text", text, sizeof text);
  ud_ensure_profile();
  char path[512]; snprintf(path, sizeof path, "D:\\inimerse_stable\\userdata\\messages\\%s.json", peer);
  char old[65536]; ud_read(path, old, sizeof old);
  char esc[4096]; json_esc(text, esc, sizeof esc);
  char entry[4096]; snprintf(entry, sizeof entry, "{\"from\":\"me\",\"text\":\"%s\",\"ts\":%ld}", esc, (long)time(NULL));
  char buf[70000];
  if (strstr(old, "\"msgs\"") == NULL) snprintf(buf, sizeof buf, "{\"peer\":\"%s\",\"msgs\":[%s]}", peer, entry);
  else {
    char *cl = strrchr(old, ']');
    if (cl) { size_t h = (size_t)(cl - old); snprintf(buf, sizeof buf, "%.*s,%s%s", (int)h, old, entry, cl); }
    else snprintf(buf, sizeof buf, "{\"peer\":\"%s\",\"msgs\":[%s]}", peer, entry);
  }
  ud_write(path, buf);

  /* forward to remote peer via P2P */
  {
    char fnode[256] = "";
    friend_node(peer, fnode, sizeof fnode);
    if (fnode[0]) {
      char mid[128] = "";
      me_id(mid, sizeof mid);
      char fwd[6000];
      snprintf(fwd, sizeof fwd, "{\"op\":\"chat\",\"from\":\"%s\",\"to\":\"%s\",\"text\":\"%s\",\"ts\":%ld}",
               mid, peer, esc, (long)time(NULL));
      p2p_send_ip(fnode, P2P_PORT, fwd);
    }
  }
  http_json(s, "{\"ok\":1}");
  /* AI auto-reply (async, non-blocking) */
  {
    char aname[128], apersona[4096], amodel[128];
    double atemp, arf; int amt;
    if (ai_find_b(peer, aname, sizeof aname, apersona, sizeof apersona, amodel, sizeof amodel, &atemp, &amt, &arf)) {
      AiTaskArg *ta = (AiTaskArg*)malloc(sizeof(AiTaskArg));
      if (ta) {
        snprintf(ta->peer, sizeof ta->peer, "%s", peer);
        snprintf(ta->text, sizeof ta->text, "%s", text);
        CreateThread(NULL, 0, ai_reply_worker, ta, 0, NULL);
      }
    }
  }
  http_json(s, "{\"ok\":1}");
  return 1;
}
if (strstr(reqbuf, "GET /ip") || strstr(reqbuf, "GET /ip?")) {
        char hostn[256] = ""; gethostname(hostn, sizeof hostn);
        struct addrinfo hints, *res = NULL, *p0;
        memset(&hints, 0, sizeof hints); hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
        char out[256] = "127.0.0.1";
        if (getaddrinfo(hostn, NULL, &hints, &res) == 0) {
            for (p0 = res; p0; p0 = p0->ai_next) {
                struct sockaddr_in *sa = (struct sockaddr_in*)p0->ai_addr;
                unsigned long a = ntohl(sa->sin_addr.s_addr);
                if ((a >> 24) == 127 || (a >> 24) == 169) continue;
                sprintf(out, "%lu.%lu.%lu.%lu", (a >> 24) & 255, (a >> 16) & 255, (a >> 8) & 255, a & 255);
                break;
            }
            freeaddrinfo(res);
        }
        int ol = (int)strlen(out);
        char resp[256];
        snprintf(resp, sizeof resp, "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nAccess-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", ol);
        send(s, resp, (int)strlen(resp), 0);
        send(s, out, ol, 0);
        return 1;
    }
    /* /spawn: start a 2nd engine+bridge instance on next ports (p= project, port= engine port) */
    if (strstr(reqbuf, "/spawn")) {
        /* parse p= and port= from query */
        const char *pp = strstr(reqbuf, "p=");
        const char *pz = strstr(reqbuf, "port=");
        int hport = pz ? atoi(pz + 5) : 11462;
        int aport = hport + 10;
        char proj[300] = "main.im";
        if (pp) {
            const char *e = strchr(pp, '&'); if (!e) e = strchr(pp, ' ');
            int l = e ? (int)(e - pp - 2) : 200;
            if (l > 280) l = 280;
            if (l > 0) { memcpy(proj, pp + 2, l); proj[l] = 0; }
        }
        /* use D: drive exes (C: drive copies occasionally hit Access denied) */
        char cmd2[900];
        snprintf(cmd2, sizeof cmd2, "\"D:\\inimerse_stable\\inimerse.exe\" --headless --port %d --http-port %d \"D:\\inimerse_stable\\%s\"", hport, aport, proj[0] ? proj : "main.im");
        STARTUPINFOA si2; PROCESS_INFORMATION pi2;
        memset(&si2, 0, sizeof si2); si2.cb = sizeof si2;
        CreateProcessA(NULL, cmd2, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, "D:\\inimerse_stable", &si2, &pi2);
        if (pi2.hProcess) CloseHandle(pi2.hProcess);
        /* kill any stale bridge/engine on target ports first */
        kill_port(hport);
        kill_port(hport + 1);
        char cmd3[500];
        snprintf(cmd3, sizeof cmd3, "\"D:\\inimerse_stable\\hl_bridge.exe\" %d %d", hport, hport + 1);
        CreateProcessA(NULL, cmd3, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, "D:\\inimerse_stable", &si2, &pi2);
        if (pi2.hProcess) CloseHandle(pi2.hProcess);
        /* preview mode = quick test, do NOT record a persistent room */
        int preview = (strstr(reqbuf, "preview=1") != NULL);
        if (!preview) {
        /* record forge room so "My Rooms" can show it */
        CreateDirectoryA("D:\\inimerse_stable\\rooms", NULL);
        char frf[512];
        snprintf(frf, sizeof frf, "D:\\inimerse_stable\\rooms\\%d.txt", hport);
        FILE *frfh = fopen(frf, "wb");
        if (frfh) { fprintf(frfh, "project=%s\npass=\n", proj); fclose(frfh); }
        }
        char url[600];
        snprintf(url, sizeof url, "http://127.0.0.1:%d/", hport + 1);
        char body[600]; int bl2 = snprintf(body, sizeof body, "{\"url\":\"%s\",\"api\":%d}", url, aport);
        char resp[1024];
        snprintf(resp, sizeof resp, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", bl2);
        send(s, resp, (int)strlen(resp), 0);
        send(s, body, bl2, 0);
        return 1;
    }

    /* GET /server/rooms - list rooms; full info only for localhost */
    if (strstr(reqbuf, "/server/rooms")) {
        char rbuf[8192]; int rp = 0;
        int isLocal = (strcmp(g_http_ip, "127.0.0.1") == 0 || strcmp(g_http_ip, "::1") == 0 || g_http_ip[0] == 0);
        rp += snprintf(rbuf + rp, sizeof rbuf - rp, "{\"local_only\":%d,\"rooms\":[", isLocal ? 0 : 1);
        WIN32_FIND_DATAA fd;
        HANDLE hf = FindFirstFileA("D:\\inimerse_stable\\rooms\\*.txt", &fd);
        int first = 1;
        if (hf != INVALID_HANDLE_VALUE) {
            do {
                int port = atoi(fd.cFileName);
                if (port > 0) {
                    char rf[512]; snprintf(rf, sizeof rf, "D:\\inimerse_stable\\rooms\\%d.txt", port);
                    char project[256] = "", pass[256] = "";
                    FILE *f = fopen(rf, "rb");
                    if (f) {
                        char line[512];
                        while (fgets(line, sizeof line, f)) {
                            if (strncmp(line, "project=", 8) == 0) { line[strcspn(line, "\r\n")] = 0; snprintf(project, sizeof project, "%s", line + 8); }
                            else if (strncmp(line, "pass=", 5) == 0) { line[strcspn(line, "\r\n")] = 0; snprintf(pass, sizeof pass, "%s", line + 5); }
                        }
                        fclose(f);
                    }
                    int online = port_listening(port) && port_listening(port + 1);
                    if (!first) rp += snprintf(rbuf + rp, sizeof rbuf - rp, ",");
                    first = 0;
                    if (isLocal)
                        rp += snprintf(rbuf + rp, sizeof rbuf - rp, "{\"room\":%d,\"port\":%d,\"pass\":\"%s\",\"project\":\"%s\",\"online\":%d}", port, port, pass, project, online);
                    else
                        rp += snprintf(rbuf + rp, sizeof rbuf - rp, "{\"room\":%d,\"port\":%d,\"project\":\"%s\",\"online\":%d}", port, port, project, online);
                }
            } while (FindNextFileA(hf, &fd));
            FindClose(hf);
        }
        rp += snprintf(rbuf + rp, sizeof rbuf - rp, "]}");
        char resp[8200];
        snprintf(resp, sizeof resp, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s", rp, rbuf);
        send(s, resp, (int)strlen(resp), 0);
        return 1;
    }
    /* /diag: capture screen + run local AI analysis, return report */
    if (strstr(reqbuf, "/diag")) {
        char *dq = strstr(reqbuf, "info=");
        if (dq) {
            dq += 5;
            char *dsp = strchr(dq, ' ');
            if (dsp) *dsp = 0;
            FILE *df = fopen("D:\\inimerse_stable\\_diag_info.txt", "w");
            if (df) { fprintf(df, "%s", dq); fclose(df); }
            if (dsp) *dsp = ' ';
        }
        char cmd[1024];
        snprintf(cmd, sizeof cmd, "node D:\\inimerse_stable\\ai_browser_diag.js 2>&1");
        FILE *pp = _popen(cmd, "r");
        char out[8192]; int oi = 0;
        if (pp) {
            char line[1024];
            while (fgets(line, sizeof line, pp) && oi < 7800) {
                int ln = (int)strlen(line);
                memcpy(out + oi, line, (size_t)ln); oi += ln;
            }
            _pclose(pp);
        }
        out[oi] = 0;
        char resp[8400];
        int rl = snprintf(resp, sizeof resp,
            "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\n"
            "Access-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",
            oi, out);
        send(s, resp, rl, 0);
        return 1;
    }
    if (strstr(reqbuf, "GET /wb") || strstr(reqbuf, "GET /workbench")) {
        const char *pg = WORKBENCH_HTML;
        int plen2 = (int)strlen(pg);
        char resp2[512];
        snprintf(resp2, sizeof resp2,
            "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
            "Access-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", plen2);
        if (send(s, resp2, (int)strlen(resp2), 0) == SOCKET_ERROR) return -1;
        if (send(s, pg, plen2, 0) == SOCKET_ERROR) return -1;
        return 1;
    }
    char resp[4096];
    int plen = (int)strlen(HTML_PAGE);
    snprintf(resp, sizeof resp,
        "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
        "Access-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", plen);
    if (send(s, resp, (int)strlen(resp), 0) == SOCKET_ERROR) return -1;
    if (send(s, HTML_PAGE, plen, 0) == SOCKET_ERROR) return -1;
    return 1;
}

/* websocket handshake from an already-buffered request header (prebuf, NUL-terminated) */
static int ws_handshake(SOCKET s, const char *prebuf) {
    char buf[4096]; int got = 0;
    if (prebuf && prebuf[0]) {
        strncpy(buf, prebuf, sizeof buf - 1); buf[sizeof buf - 1] = '\0';
        got = (int)strlen(buf);
    } else {
        while (got < (int)sizeof buf - 1) {
            int n = recv(s, buf + got, 1, 0);
            if (n <= 0) return -1;
            got += n;
            if (got >= 4 && buf[got-4] == '\r' && buf[got-3] == '\n' && buf[got-2] == '\r' && buf[got-1] == '\n') break;
        }
        buf[got] = '\0';
    }
    char *k = ci_strstr(buf, "sec-websocket-key:");
    if (!k) return -1;
    k += 18; while (*k == ' ') k++; /* Sec-WebSocket-Key: is 18 chars */
    char *ke = strchr(k, '\r'); if (!ke) return -1;
    char key[128]; int kl = (int)(ke - k); if (kl > 120) kl = 120;
    memcpy(key, k, kl); key[kl] = '\0';
    HCRYPTPROV hp; HCRYPTHASH hh;
    char concat[256]; snprintf(concat, sizeof concat, "%s%s", key, "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    DWORD clen = (DWORD)strlen(concat);
    BYTE digest[20];
    if (CryptAcquireContext(&hp, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        if (CryptCreateHash(hp, CALG_SHA1, 0, 0, &hh)) {
            CryptHashData(hh, (const BYTE*)concat, clen, 0);
            DWORD dl = 20; CryptGetHashParam(hh, HP_HASHVAL, digest, &dl, 0);
            CryptDestroyHash(hh);
        }
        CryptReleaseContext(hp, 0);
    } else return -1;
    static const char *b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char accept[64]; int ai = 0;
    for (int i = 0; i < 20; i += 3) {
        int v = (digest[i] << 16) | ((i+1<20?digest[i+1]:0) << 8) | (i+2<20?digest[i+2]:0);
        accept[ai++] = b64[(v >> 18) & 63];
        accept[ai++] = b64[(v >> 12) & 63];
        accept[ai++] = i+1 < 20 ? b64[(v >> 6) & 63] : '=';
        accept[ai++] = i+2 < 20 ? b64[v & 63] : '=';
    }
    accept[ai] = '\0';
    char resp[512];
    snprintf(resp, sizeof resp,
        "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n\r\n", accept);
    return send(s, resp, (int)strlen(resp), 0) == SOCKET_ERROR ? -1 : 0;
}

/* handle one recv'd chunk for a client still reading HTTP: returns 0 keep, -1 close */
static int client_recv_http(int i) {
    Client *c = &g_cli[i];
    int hdr_end = -1;
    for (int k = 3; k < c->blen; k++) {
        if (c->buf[k-3] == '\r' && c->buf[k-2] == '\n' && c->buf[k-1] == '\r' && c->buf[k] == '\n') { hdr_end = k + 1; break; }
    }
    if (hdr_end < 0) {
        if (c->blen >= (int)sizeof(c->buf) - 1) return -1; /* overflow */
        return 0; /* need more */
    }
    c->buf[hdr_end] = '\0';
    snprintf(g_http_ip, sizeof g_http_ip, "%s", c->ip[0] ? c->ip : "127.0.0.1");
    int hret = http_serve_page(c->s, c->buf, hdr_end);
    if (hret == 1) return -1;   /* page served, close */
    if (hret < 0) return -1;
    /* websocket upgrade: handshake, keep socket */
    if (ws_handshake(c->s, c->buf) != 0) return -1;
    int left = c->blen - hdr_end;
    if (left > 0) memmove(c->buf, c->buf + hdr_end, left);
    c->blen = left;
    c->ws = 1;
    fprintf(stderr, "ws client connected (%d active)\n", g_ncli);
    return client_pump_ws(i); /* leftover bytes may already contain frames */
}

/* begin (re)connecting to the engine socket; non-blocking, never blocks */
static void engine_start_connect(int ep) {
    if (g_engine != INVALID_SOCKET) return;
    g_engine = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_engine == INVALID_SOCKET) { g_engine_retry_at = GetTickCount() + 1000; return; }
    { int one = 1; setsockopt(g_engine, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one); }
    struct sockaddr_in ea; memset(&ea, 0, sizeof ea);
    ea.sin_family = AF_INET; ea.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ea.sin_port = htons((unsigned short)ep);
    u_long nb = 1;
    ioctlsocket(g_engine, FIONBIO, &nb);
    int cr = connect(g_engine, (struct sockaddr*)&ea, sizeof ea);
    if (cr == 0) { g_engine_state = 1; g_engine_connect_t0 = GetTickCount(); fprintf(stderr, "engine connected\n"); return; }
    int werr = WSAGetLastError();
    if (werr == WSAEWOULDBLOCK) { g_engine_state = 0; g_engine_connect_t0 = GetTickCount(); return; } /* in progress */
    closesocket(g_engine); g_engine = INVALID_SOCKET;
    g_engine_retry_at = GetTickCount() + 1000;
}


/* restore_rooms: respawn engine+bridge for every room file whose ports are down */
static void restore_rooms(void) {
    WIN32_FIND_DATAA fd;
    HANDLE hf = FindFirstFileA("D:\\inimerse_stable\\rooms\\*.txt", &fd);
    if (hf == INVALID_HANDLE_VALUE) return;
    STARTUPINFOA si2; PROCESS_INFORMATION pi2;
    do {
        int port = atoi(fd.cFileName);
        if (port >= 11490 && port < 11700) {
            int engUp = port_listening(port);
            int brUp = port_listening(port + 1);
            if (engUp && brUp) continue;
            char rf[512]; snprintf(rf, sizeof rf, "D:\\inimerse_stable\\rooms\\%d.txt", port);
            char project[256] = "demo", pass[256] = "";
            FILE *f = fopen(rf, "rb");
            if (f) {
                char line[512];
                while (fgets(line, sizeof line, f)) {
                    if (strncmp(line, "project=", 8) == 0) { line[strcspn(line, "\r\n")] = 0; snprintf(project, sizeof project, "%s", line + 8); }
                    else if (strncmp(line, "pass=", 5) == 0) { line[strcspn(line, "\r\n")] = 0; snprintf(pass, sizeof pass, "%s", line + 5); }
                }
                fclose(f);
            }
            char cmd[1024];
            if (!engUp) {
            snprintf(cmd, sizeof cmd, "\"D:\\inimerse_stable\\inimerse.exe\" --headless --port %d --http-port %d --time-limit 0 \"D:\\inimerse_stable\\projects\\%s\\main.im\"", port, port + 10, project);
            memset(&si2, 0, sizeof si2); si2.cb = sizeof si2;
            CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, "D:\\inimerse_stable", &si2, &pi2);
            if (pi2.hProcess) CloseHandle(pi2.hProcess);
            }
            if (!brUp) {
            char cmd2[500];
            snprintf(cmd2, sizeof cmd2, "\"D:\\inimerse_stable\\hl_bridge.exe\" %d %d", port, port + 1);
            memset(&si2, 0, sizeof si2); si2.cb = sizeof si2;
            CreateProcessA(NULL, cmd2, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, "D:\\inimerse_stable", &si2, &pi2);
            if (pi2.hProcess) CloseHandle(pi2.hProcess);
            }
            fprintf(stderr, "[restore] room %d (%s) eng=%d br=%d\n", port, project, engUp, brUp);
        }
    } while (FindNextFileA(hf, &fd));
    FindClose(hf);
}

int main(int argc, char **argv) {
    int ep = argc > 1 ? atoi(argv[1]) : 11460;
    int wp = argc > 2 ? atoi(argv[2]) : 11461;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { fprintf(stderr, "wsa fail\n"); return 1; }
    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in sa; memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET; sa.sin_addr.s_addr = htonl(INADDR_ANY); /* all interfaces (LAN/mobile) */
    sa.sin_port = htons((unsigned short)wp);
    if (bind(ls, (struct sockaddr*)&sa, sizeof sa) != 0) { fprintf(stderr, "bind %d fail\n", wp); return 1; }
    listen(ls, 16);
    { u_long nb = 1; ioctlsocket(ls, FIONBIO, &nb); } /* non-blocking listen: accept loop must not block */
    fprintf(stderr, "bridge ws://0.0.0.0:%d <-> engine %d (multi-client)\n", wp, ep);
    g_engine_retry_at = 0;
    if (ep == 11460) p2p_init(); /* main bridge acts as P2P node */
    for (;;) {
        static DWORD r_t0 = 0; static int r_first = 1;
        DWORD r_now = GetTickCount();
        if (ep == 11460) { /* only main bridge (11461) restores rooms */
            if (r_first) { r_t0 = r_now; r_first = 0; restore_rooms(); } /* once at startup */
            if (r_now - r_t0 >= 60000) { r_t0 = r_now; restore_rooms(); } /* then every 60s */
        }

        fd_set rf, wf; FD_ZERO(&rf); FD_ZERO(&wf);
        int maxf = (int)ls;
        FD_SET(ls, &rf);
        DWORD now = GetTickCount();
        if (g_engine == INVALID_SOCKET && (int)(now - g_engine_retry_at) >= 0) engine_start_connect(ep);
        if (g_engine != INVALID_SOCKET) {
            if (g_engine_state == 0) { FD_SET(g_engine, &wf); if ((int)g_engine > maxf) maxf = (int)g_engine; }
            else { FD_SET(g_engine, &rf); if ((int)g_engine > maxf) maxf = (int)g_engine; }
        }
        for (int i = 0; i < g_ncli; i++) {
            FD_SET(g_cli[i].s, &rf);
            if ((int)g_cli[i].s > maxf) maxf = (int)g_cli[i].s;
        }
        if (g_udp != INVALID_SOCKET) { FD_SET(g_udp, &rf); if ((int)g_udp > maxf) maxf = (int)g_udp; }
        if (g_p2p != INVALID_SOCKET) { FD_SET(g_p2p, &rf); if ((int)g_p2p > maxf) maxf = (int)g_p2p; }
        struct timeval tv = { 0, 100000 }; /* 100ms */
        int sel = select(maxf + 1, &rf, &wf, NULL, &tv);
        if (sel == SOCKET_ERROR) { Sleep(10); continue; }
        /* p2p maintenance */
        if (ep == 11460) {
            DWORD pnow = GetTickCount();
            if (pnow - g_announce_t >= 10000) { g_announce_t = pnow; p2p_announce(); }
            if (pnow - g_node_clean_t >= 30000) { g_node_clean_t = pnow; p2p_clean_nodes(); }
            p2p_poll_udp();
            p2p_poll_accept();
        }
        /* engine connect finished? */
        if (g_engine != INVALID_SOCKET && g_engine_state == 0) {
            if ((int)(now - g_engine_connect_t0) > 3000) {
                /* connect stuck (select never reported writable): force rebuild */
                closesocket(g_engine); g_engine = INVALID_SOCKET; g_engine_state = 0;
                g_engine_retry_at = now + 100;
                fprintf(stderr, "engine connect timeout, rebuild\n");
            } else if (FD_ISSET(g_engine, &wf)) {
                int soerr = 0, sl = sizeof soerr;
                getsockopt(g_engine, SOL_SOCKET, SO_ERROR, (char*)&soerr, &sl);
                if (soerr == 0) { g_engine_state = 1; fprintf(stderr, "engine connected\n"); }
                else { closesocket(g_engine); g_engine = INVALID_SOCKET; g_engine_state = 0; g_engine_retry_at = GetTickCount() + 1000; fprintf(stderr, "engine connect fail, retry\n"); }
            }
        }
        /* engine frames -> broadcast */
        if (g_engine != INVALID_SOCKET && g_engine_state == 1 && FD_ISSET(g_engine, &rf)) {
            char buf[65536];
            int n = recv(g_engine, buf, sizeof buf, 0);
            if (n > 0) engine_broadcast(buf, n);
            else if (n == 0) {
                closesocket(g_engine); g_engine = INVALID_SOCKET; g_engine_state = 0;
                g_engine_retry_at = GetTickCount() + 500;
                fprintf(stderr, "engine disconnected, reconnect...\n");
            } else {
                int werr = WSAGetLastError();
                if (werr == WSAEWOULDBLOCK) { /* transient, ignore */ }
                else {
                    closesocket(g_engine); g_engine = INVALID_SOCKET; g_engine_state = 0;
                    g_engine_retry_at = GetTickCount() + 500;
                    fprintf(stderr, "engine recv err %d, reconnect...\n", werr);
                }
            }
        }
        /* new connections */
        if (FD_ISSET(ls, &rf)) {
            for (;;) {
                struct sockaddr_in cliAddr;
                int cliLen = sizeof(cliAddr);
                SOCKET cs = accept(ls, (struct sockaddr*)&cliAddr, &cliLen);
                if (cs == INVALID_SOCKET) {
                    if (WSAGetLastError() == WSAEWOULDBLOCK) break; /* queue drained */
                    break;
                }
                if (g_ncli >= MAX_CLIENTS) { closesocket(cs); break; }
                u_long nb = 1; ioctlsocket(cs, FIONBIO, &nb);
                inet_ntop(AF_INET, &cliAddr.sin_addr, g_cli[g_ncli].ip, sizeof g_cli[g_ncli].ip);
                if (!g_cli[g_ncli].ip[0]) snprintf(g_cli[g_ncli].ip, sizeof g_cli[g_ncli].ip, "0.0.0.0");
                g_cli[g_ncli].s = cs; g_cli[g_ncli].ws = 0; g_cli[g_ncli].blen = 0;
                g_ncli++;
            }
        }
        /* client sockets */
        for (int i = 0; i < g_ncli; ) {
            if (!FD_ISSET(g_cli[i].s, &rf)) { i++; continue; }
            Client *c = &g_cli[i];
            int n = recv(c->s, c->buf + c->blen, (int)sizeof(c->buf) - 1 - c->blen, 0);
            if (n > 0) c->blen += n;
            else if (n == 0) { client_close(i); continue; }
            else {
                int werr = WSAGetLastError();
                if (werr == WSAEWOULDBLOCK) { i++; continue; }
                client_close(i); continue;
            }
            int r = c->ws ? client_pump_ws(i) : client_recv_http(i);
            if (r < 0) { client_close(i); continue; }
            i++;
        }
    }
    closesocket(ls);
    return 0;
}
