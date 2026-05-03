/**
 * video_rtsp.c - HTTP 视频流 + Web 仪表盘
 *
 * - K230 平台: 使用 lwIP socket
 * - PC 模拟:   使用 POSIX socket
 *
 * 端点:
 *   /           → Web 仪表盘主页
 *   /stream     → MJPEG 视频流 (multipart BMP)
 *   /snapshot   → 单帧 BMP 快照
 *   /api/status → 系统状态 JSON
 *   /api/events → 跌倒事件历史 JSON
 *   /api/reset  → 复位跌倒状态
 */
#include "video_rtsp.h"
#include "sys_monitor.h"
#include "fall_detect.h"
#include "ai_engine.h"
#include "person_log.h"
#include "pose_overlay.h"
#include "log.h"
#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>

#ifdef RT_USING_LWIP
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#endif

#ifdef RT_USING_MOCK
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#endif

#define RTSP_MAX_CLIENTS    32
#define MAX_EVENTS          20

typedef struct {
    int   fd;
    uint8_t active;
    uint64_t connected_tick;
} rtsp_client_t;

typedef struct {
    uint16_t port;
    uint8_t  running;
    int      server_fd;
    rt_thread_t thread;
    rtsp_client_t clients[RTSP_MAX_CLIENTS];
    uint32_t frame_count;

#ifdef RT_USING_MOCK
    uint8_t *latest_jpeg;
    int      latest_jpeg_size;
    pthread_mutex_t frame_lock;

    /* 跌倒事件历史 */
    fall_event_t events[MAX_EVENTS];
    int event_count;
    int event_write_idx;
    pthread_mutex_t event_lock;
#endif
} rtsp_ctx_t;

static rtsp_ctx_t g_rtsp;

/* ========== YUV420 → JPEG (通过 ffmpeg 管道) ========== */

#ifdef RT_USING_MOCK

#include <sys/wait.h>
#include <sys/select.h>

/**
 * YUV420 → JPEG: 用临时文件 + ffmpeg 编码
 */
static uint8_t *jpeg_encode_frame(const uint8_t *yuv, int w, int h, int *out_size)
{
    int y_size = w * h;
    int uv_size = (w / 2) * (h / 2);
    int frame_bytes = y_size + uv_size * 2;

    const char *yuv_path = "/tmp/_fdFrame.yuv";
    const char *jpg_path = "/tmp/_fdFrame.jpg";

    /* 写 YUV 到临时文件 */
    FILE *fp = fopen(yuv_path, "wb");
    if (!fp) return NULL;
    fwrite(yuv, 1, frame_bytes, fp);
    fclose(fp);

    /* ffmpeg 转码 (低质量高速编码) */
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
        "ffmpeg -loglevel error -y -f rawvideo -pix_fmt yuv420p "
        "-s %dx%d -i %s -q:v 5 -f image2 %s 2>/dev/null",
        w, h, yuv_path, jpg_path);
    int ret = system(cmd);

    remove(yuv_path);

    if (ret != 0) {
        LOG_W(LOG_TAG_VIDEO, "jpeg_encode_frame: ffmpeg failed (ret=%d)", ret);
        remove(jpg_path);
        return NULL;
    }

    /* 读取 JPEG */
    fp = fopen(jpg_path, "rb");
    if (!fp) return NULL;

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (fsize <= 0) {
        LOG_W(LOG_TAG_VIDEO, "jpeg_encode_frame: empty JPEG file");
        fclose(fp); remove(jpg_path); return NULL;
    }

    uint8_t *buf = (uint8_t *)malloc(fsize);
    if (!buf) { fclose(fp); remove(jpg_path); return NULL; }

    *out_size = (int)fsize;
    if (fread(buf, 1, fsize, fp) != (size_t)fsize) {
        free(buf); fclose(fp); remove(jpg_path); return NULL;
    }
    fclose(fp);
    remove(jpg_path);

    return buf;
}

static void encode_and_update(video_frame_t *frame)
{
    int jpeg_size = 0;
    uint8_t *jpeg = jpeg_encode_frame(frame->frame_buf, frame->width, frame->height, &jpeg_size);
    if (!jpeg) {
        LOG_W(LOG_TAG_VIDEO, "update_latest_frame: jpeg encode failed");
        return;
    }
    LOG_I(LOG_TAG_VIDEO, "update_latest_frame: JPEG %d bytes", jpeg_size);

    pthread_mutex_lock(&g_rtsp.frame_lock);
    if (g_rtsp.latest_jpeg) free(g_rtsp.latest_jpeg);
    g_rtsp.latest_jpeg = jpeg;
    g_rtsp.latest_jpeg_size = jpeg_size;
    pthread_mutex_unlock(&g_rtsp.frame_lock);
}

/* ========== JSON 辅助函数 ========== */

static int json_escape(char *dst, int dst_size, const char *src)
{
    int di = 0;
    for (int si = 0; src[si] && di < dst_size - 2; si++) {
        char c = src[si];
        if (c == '"' || c == '\\') {
            if (di >= dst_size - 3) break;
            dst[di++] = '\\';
            dst[di++] = c;
        } else if (c < 0x20) {
            if (di >= dst_size - 7) break;
            di += snprintf(dst + di, dst_size - di, "\\u%04x", (unsigned char)c);
        } else {
            dst[di++] = c;
        }
    }
    dst[di] = '\0';
    return di;
}

static void send_status_json(int client_fd)
{
    sys_status_t status;
    sys_monitor_get_status(&status);

    fall_state_t fall_state = fall_detect_get_state();
    uint32_t total_frames = 0, detected = 0, fp_count = 0;
    fall_detect_get_stats(&total_frames, &detected, &fp_count);
    int infer_ms = ai_engine_get_inference_time_ms();

    char json[512];
    int len = snprintf(json, sizeof(json),
        "{\"cpu\":%d,\"mem_free\":%lu,\"mem_total\":%lu,"
        "\"temp\":%d,\"uptime\":%llu,"
        "\"fall_state\":%d,\"total_frames\":%lu,"
        "\"detected\":%lu,\"false_pos\":%lu,"
        "\"infer_ms\":%d,\"clients\":%d}",
        status.cpu_usage,
        (unsigned long)status.mem_free,
        (unsigned long)status.mem_total,
        status.temperature,
        (unsigned long long)status.uptime_ms,
        (int)fall_state,
        (unsigned long)total_frames,
        (unsigned long)detected,
        (unsigned long)fp_count,
        infer_ms,
        video_rtsp_get_clients());

    char header[256];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n", len);

    send(client_fd, header, hlen, MSG_NOSIGNAL);
    send(client_fd, json, len, MSG_NOSIGNAL);
}

static void send_events_json(int client_fd)
{
    char json[4096];
    int pos = 0;

    pthread_mutex_lock(&g_rtsp.event_lock);

    pos += snprintf(json + pos, sizeof(json) - pos, "[");
    int count = g_rtsp.event_count;
    if (count > MAX_EVENTS) count = MAX_EVENTS;

    for (int i = 0; i < count && pos < (int)sizeof(json) - 200; i++) {
        int idx = (g_rtsp.event_write_idx - count + i + MAX_EVENTS) % MAX_EVENTS;
        fall_event_t *ev = &g_rtsp.events[idx];

        char escaped_id[32];
        json_escape(escaped_id, sizeof(escaped_id), ev->event_id);

        if (i > 0) pos += snprintf(json + pos, sizeof(json) - pos, ",");
        pos += snprintf(json + pos, sizeof(json) - pos,
            "{\"id\":\"%s\",\"ts\":%llu,\"conf\":%.2f,\"angle\":%.1f,\"state\":%d}",
            escaped_id,
            (unsigned long long)ev->timestamp,
            ev->confidence,
            ev->fall_angle,
            (int)ev->state);
    }
    pos += snprintf(json + pos, sizeof(json) - pos, "]");

    pthread_mutex_unlock(&g_rtsp.event_lock);

    char header[256];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n", pos);

    send(client_fd, header, hlen, MSG_NOSIGNAL);
    send(client_fd, json, pos, MSG_NOSIGNAL);
}

static void send_persons_json(int client_fd)
{
    const person_log_t *pl = person_log_get();
    char json[2048];
    int pos = 0;

    pos += snprintf(json + pos, sizeof(json) - pos,
        "{\"person_count\":%d,\"action\":%d,\"confidence\":%.2f,\"log\":[",
        pl->person_count, pl->action, pl->confidence);

    int count = pl->count;
    if (count > 10) count = 10;

    for (int i = 0; i < count && pos < (int)sizeof(json) - 200; i++) {
        int idx = (pl->write_idx - count + i + PERSON_LOG_SIZE) % PERSON_LOG_SIZE;
        const person_log_entry_t *e = &pl->entries[idx];
        if (i > 0) pos += snprintf(json + pos, sizeof(json) - pos, ",");
        pos += snprintf(json + pos, sizeof(json) - pos,
            "{\"ts\":%llu,\"cnt\":%d,\"act\":\"%s\",\"conf\":%.2f}",
            (unsigned long long)e->timestamp,
            e->person_count, e->action_name, e->confidence);
    }
    pos += snprintf(json + pos, sizeof(json) - pos, "]}");

    char header[256];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n", pos);

    send(client_fd, header, hlen, MSG_NOSIGNAL);
    send(client_fd, json, pos, MSG_NOSIGNAL);
}

/* ========== HTTP 请求处理 ========== */

static const char DASHBOARD_HTML[] =
"<!DOCTYPE html><html><head><meta charset='utf-8'>"
"<title>Fall Detection Dashboard</title>"
"<style>"
"*{margin:0;padding:0;box-sizing:border-box}"
"body{background:#0f1117;color:#e0e0e0;font-family:'Segoe UI',system-ui,sans-serif;overflow-x:hidden}"
"header{background:linear-gradient(135deg,#1a1a2e 0%,#16213e 100%);padding:16px 24px;display:flex;align-items:center;justify-content:space-between;border-bottom:1px solid #2a2a4a}"
"header h1{font-size:20px;font-weight:600;background:linear-gradient(90deg,#e94560,#ff6b6b);-webkit-background-clip:text;-webkit-text-fill-color:transparent}"
"header .badge{padding:4px 12px;border-radius:12px;font-size:12px;font-weight:600}"
".badge-ok{background:#1b4332;color:#52b788}.badge-warn{background:#3d2600;color:#fca311}.badge-alert{background:#4a0000;color:#ff4444}"
".main{display:grid;grid-template-columns:1fr 360px;gap:16px;padding:16px;height:calc(100vh - 60px)}"
".video-section{display:flex;flex-direction:column;gap:16px}"
".video-container{position:relative;background:#000;border-radius:12px;overflow:hidden;border:1px solid #2a2a4a;flex:1;display:flex;align-items:center;justify-content:center}"
".video-container img{max-width:100%;max-height:100%;object-fit:contain}"
".video-overlay{position:absolute;top:12px;left:12px;display:flex;gap:8px}"
".video-overlay .tag{background:rgba(0,0,0,0.7);padding:4px 10px;border-radius:6px;font-size:11px;font-weight:600;backdrop-filter:blur(8px)}"
".tag-live{color:#ff4444}.tag-rec{color:#ff4444;animation:pulse 1.5s infinite}"
"@keyframes pulse{0%,100%{opacity:1}50%{opacity:0.4}}"
".controls{display:flex;gap:8px;padding:0 4px}"
".btn{padding:8px 16px;border:1px solid #3a3a5a;background:#1a1a2e;color:#e0e0e0;border-radius:8px;cursor:pointer;font-size:13px;transition:all 0.2s}"
".btn:hover{background:#2a2a4e;border-color:#e94560}"
".btn-primary{background:#e94560;border-color:#e94560;color:#fff}"
".btn-primary:hover{background:#c73650}"
".sidebar{display:flex;flex-direction:column;gap:16px;overflow-y:auto}"
".card{background:#1a1a2e;border:1px solid #2a2a4a;border-radius:12px;padding:16px}"
".card h3{font-size:13px;text-transform:uppercase;letter-spacing:1px;color:#888;margin-bottom:12px;display:flex;align-items:center;gap:8px}"
".card h3 .dot{width:8px;height:8px;border-radius:50%;display:inline-block}"
".dot-green{background:#52b788}.dot-yellow{background:#fca311}.dot-red{background:#ff4444;animation:pulse 1.5s infinite}"
".stats{display:grid;grid-template-columns:1fr 1fr;gap:10px}"
".stat{background:#0f1117;border-radius:8px;padding:10px;text-align:center}"
".stat .val{font-size:22px;font-weight:700;color:#fff;font-variant-numeric:tabular-nums}"
".stat .label{font-size:11px;color:#888;margin-top:4px}"
".state-display{text-align:center;padding:16px;border-radius:8px;font-size:18px;font-weight:700}"
".state-normal{background:#1b4332;color:#52b788}.state-falling{background:#3d2600;color:#fca311}"
".state-confirmed{background:#4a0000;color:#ff4444;animation:pulse 1s infinite}"
".state-cooldown{background:#1a1a3e;color:#8888cc}"
".events-list{max-height:240px;overflow-y:auto;display:flex;flex-direction:column;gap:6px}"
".events-list::-webkit-scrollbar{width:4px}.events-list::-webkit-scrollbar-thumb{background:#3a3a5a;border-radius:2px}"
".event-item{display:flex;justify-content:space-between;align-items:center;background:#0f1117;padding:8px 10px;border-radius:6px;font-size:12px;border-left:3px solid #e94560}"
".event-item .eid{color:#e94560;font-weight:600;font-family:monospace}"
".event-item .einfo{color:#888;text-align:right}"
".event-item .econf{color:#52b788;font-weight:600}"
".no-events{color:#555;text-align:center;padding:20px;font-size:13px}"
".progress-bar{height:4px;background:#2a2a4a;border-radius:2px;overflow:hidden;margin-top:8px}"
".progress-bar .fill{height:100%;border-radius:2px;transition:width 0.5s}"
".fill-green{background:#52b788}.fill-yellow{background:#fca311}.fill-red{background:#ff4444}"
"@media(max-width:900px){.main{grid-template-columns:1fr}.sidebar{order:-1}}"
"</style></head><body>"
"<header>"
"<h1>Fall Detection System</h1>"
"<div><span id='headerBadge' class='badge badge-ok'>NORMAL</span></div>"
"</header>"
"<div class='main'>"
"<div class='video-section'>"
"<div class='video-container'>"
"<img id='stream' src='/snapshot' alt='Live Video'>"
"<div class='video-overlay'>"
"<span class='tag tag-live'>LIVE</span>"
"<span id='fpsTag' class='tag'>-- fps</span>"
"<span id='resTag' class='tag'>720x480</span>"
"</div></div>"
"<div class='controls'>"
"<button class='btn' onclick=\"location.href='/snapshot'\">Snapshot</button>"
"<button class='btn' onclick='toggleStream()'>Pause/Resume</button>"
"<button class='btn btn-primary' onclick='resetFall()'>Reset Alert</button>"
"<button class='btn' onclick='location.reload()'>Reload</button>"
"</div></div>"
"<div class='sidebar'>"
"<div class='card'>"
"<h3><span class='dot dot-green' id='stateDot'></span> Fall Detection</h3>"
"<div id='stateDisplay' class='state-display state-normal'>NORMAL</div>"
"<div class='progress-bar'><div id='confBar' class='fill fill-green' style='width:0%'></div></div>"
"</div>"
"<div class='card'>"
"<h3>System Status</h3>"
"<div class='stats'>"
"<div class='stat'><div class='val' id='cpuVal'>--</div><div class='label'>CPU %</div></div>"
"<div class='stat'><div class='val' id='memVal'>--</div><div class='label'>Free MB</div></div>"
"<div class='stat'><div class='val' id='tempVal'>--</div><div class='label'>Temp C</div></div>"
"<div class='stat'><div class='val' id='inferVal'>--</div><div class='label'>AI ms</div></div>"
"<div class='stat'><div class='val' id='uptimeVal'>--</div><div class='label'>Uptime</div></div>"
"<div class='stat'><div class='val' id='clientsVal'>--</div><div class='label'>Clients</div></div>"
"</div></div>"
"<div class='card'>"
"<h3>Detection Stats</h3>"
"<div class='stats'>"
"<div class='stat'><div class='val' id='framesVal'>--</div><div class='label'>Frames</div></div>"
"<div class='stat'><div class='val' id='detVal'>--</div><div class='label'>Detected</div></div>"
"<div class='stat'><div class='val' id='fpVal'>--</div><div class='label'>False Pos</div></div>"
"<div class='stat'><div class='val' id='eventsVal'>--</div><div class='label'>Events</div></div>"
"</div></div>"
"<div class='card'>"
"<h3>Event Log</h3>"
"<div id='eventsList' class='events-list'><div class='no-events'>No events yet</div></div>"
"</div>"
"<div class='card'>"
"<h3><span class='dot dot-green' id='personDot'></span> Person Status</h3>"
"<div class='stats'>"
"<div class='stat'><div class='val' id='pCount'>0</div><div class='label'>People</div></div>"
"<div class='stat'><div class='val' id='pAction' style='font-size:14px'>--</div><div class='label'>Action</div></div>"
"<div class='stat'><div class='val' id='pConf'>--</div><div class='label'>Conf %</div></div>"
"</div></div>"
"<div class='card'>"
"<h3>Detection Log</h3>"
"<div id='personLog' class='events-list'><div class='no-events'>No data</div></div>"
"</div></div></div>"
"<script>"
"const S={NORMAL:0,FALLING:1,CONFIRMED:2,COOLDOWN:3};"
"const SN=['NORMAL','FALLING','CONFIRMED','COOLDOWN'];"
"const SC=['state-normal','state-falling','state-confirmed','state-cooldown'];"
"const DC=['dot-green','dot-yellow','dot-red','dot-green'];"
"const BC=['badge-ok','badge-warn','badge-alert','badge-ok'];"
"let paused=false,lastFrames=0,lastTime=Date.now(),prevEvents='';"
"function fmtUptime(ms){"
"let s=Math.floor(ms/1000),m=Math.floor(s/60),h=Math.floor(m/60);"
"s%=60;m%=60;"
"if(h>0)return h+'h'+m+'m';"
"if(m>0)return m+'m'+s+'s';"
"return s+'s';"
"}"
"function toggleStream(){"
"let img=document.getElementById('stream');"
"paused=!paused;"
"if(paused){img.alt='Paused';}"
"else{refreshFrame();}"
"}"
"function refreshFrame(){"
"if(!paused){let img=document.getElementById('stream');"
"img.src='/snapshot?t='+Date.now();}"
"}"
"setInterval(refreshFrame,500);"
"function resetFall(){"
"fetch('/api/reset').then(()=>pollStatus());"
"}"
"function pollStatus(){"
"fetch('/api/status').then(r=>r.json()).then(d=>{"
"let st=d.fall_state||0;"
"document.getElementById('stateDisplay').textContent=SN[st]||'UNKNOWN';"
"document.getElementById('stateDisplay').className='state-display '+SC[st];"
"document.getElementById('stateDot').className='dot '+DC[st];"
"document.getElementById('headerBadge').textContent=SN[st]||'NORMAL';"
"document.getElementById('headerBadge').className='badge '+BC[st];"
"document.getElementById('cpuVal').textContent=d.cpu;"
"document.getElementById('memVal').textContent=Math.round(d.mem_free/1024/1024);"
"document.getElementById('tempVal').textContent=d.temp;"
"document.getElementById('inferVal').textContent=d.infer_ms;"
"document.getElementById('uptimeVal').textContent=fmtUptime(d.uptime);"
"document.getElementById('clientsVal').textContent=d.clients;"
"document.getElementById('framesVal').textContent=d.total_frames;"
"document.getElementById('detVal').textContent=d.detected;"
"document.getElementById('fpVal').textContent=d.false_pos;"
"document.getElementById('eventsVal').textContent=d.events_total||0;"
"let now=Date.now();let dt=(now-lastTime)/1000;"
"if(dt>0){let fps=Math.round((d.total_frames-lastFrames)/dt);"
"document.getElementById('fpsTag').textContent=fps+' fps';"
"lastFrames=d.total_frames;lastTime=now;}"
"let cpuPct=d.cpu;"
"let bar=document.getElementById('confBar');"
"bar.style.width=cpuPct+'%';"
"bar.className='fill '+(cpuPct>80?'fill-red':cpuPct>50?'fill-yellow':'fill-green');"
"}).catch(()=>{});"
"}"
"function pollEvents(){"
"fetch('/api/events').then(r=>r.json()).then(evts=>{"
"let json=JSON.stringify(evts);"
"if(json===prevEvents)return;"
"prevEvents=json;"
"let el=document.getElementById('eventsList');"
"if(!evts||evts.length===0){el.innerHTML='<div class=\"no-events\">No events yet</div>';return;}"
"let html='';"
"evts.slice().reverse().forEach(e=>{"
"let t=new Date(e.ts*1000/100).toLocaleTimeString();"
"html+='<div class=\"event-item\">';"
"html+='<span class=\"eid\">'+e.id+'</span>';"
"html+='<span class=\"einfo\">'+t+' <span class=\"econf\">'+Math.round(e.conf*100)+'%</span></span>';"
"html+='</div>';"
"});"
"el.innerHTML=html;"
"}).catch(()=>{});"
"}"
"setInterval(pollStatus,1000);"
"setInterval(pollEvents,2000);"
"setInterval(pollPersons,1000);"
"pollStatus();pollEvents();pollPersons();"
"function pollPersons(){"
"fetch('/api/persons').then(r=>r.json()).then(d=>{"
"document.getElementById('pCount').textContent=d.person_count;"
"let act=d.action;"
"let actNames=['Stand','Walk','Sit','Crouch','FALL','Lying'];"
"let actStr=actNames[act]||'N/A';"
"let el=document.getElementById('pAction');"
"el.textContent=actStr;"
"el.style.color=act===4?'#ff4444':act===5?'#fca311':'#52b788';"
"document.getElementById('pConf').textContent=Math.round(d.confidence*100);"
"let dot=document.getElementById('personDot');"
"dot.className='dot '+(act>=4?'dot-red':act>=2?'dot-yellow':'dot-green');"
"let logEl=document.getElementById('personLog');"
"if(!d.log||d.log.length===0){logEl.innerHTML='<div class=\"no-events\">No data</div>';return;}"
"let html='';"
"d.log.slice().reverse().forEach(e=>{"
"let t=new Date(e.ts*1000/100).toLocaleTimeString();"
"let color=e.act==='FALL'?'#ff4444':e.act==='Lying'?'#fca311':'#52b788';"
"html+='<div class=\"event-item\">';"
"html+='<span class=\"eid\" style=\"color:'+color+'\">'+e.act+'</span>';"
"html+='<span class=\"einfo\">'+t+' <span class=\"econf\">'+e.cnt+'p '+Math.round(e.conf*100)+'%</span></span>';"
"html+='</div>';"
"});"
"logEl.innerHTML=html;"
"}).catch(()=>{});"
"}"
"</script></body></html>";

static void send_dashboard(int client_fd)
{
    char header[256];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n",
        (int)(sizeof(DASHBOARD_HTML) - 1));

    send(client_fd, header, hlen, MSG_NOSIGNAL);
    send(client_fd, DASHBOARD_HTML, sizeof(DASHBOARD_HTML) - 1, MSG_NOSIGNAL);
}

static void send_snapshot(int client_fd)
{
    pthread_mutex_lock(&g_rtsp.frame_lock);
    if (g_rtsp.latest_jpeg) {
        char header[256];
        int hlen = snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: image/jpeg\r\n"
            "Content-Length: %d\r\n"
            "Content-Disposition: inline; filename=snapshot.jpg\r\n"
            "Connection: close\r\n\r\n",
            g_rtsp.latest_jpeg_size);
        if (send(client_fd, header, hlen, MSG_NOSIGNAL) > 0) {
            send(client_fd, g_rtsp.latest_jpeg, g_rtsp.latest_jpeg_size, MSG_NOSIGNAL);
        }
    } else {
        const char *no_frame = "HTTP/1.1 503 Service Unavailable\r\n\r\nNo frame available";
        send(client_fd, no_frame, strlen(no_frame), MSG_NOSIGNAL);
    }
    pthread_mutex_unlock(&g_rtsp.frame_lock);
}

static void *http_client_thread(void *arg)
{
    int client_fd = (int)(intptr_t)arg;
    char req[2048];

    int n = recv(client_fd, req, sizeof(req) - 1, 0);
    if (n <= 0) goto done;
    req[n] = '\0';

    if (strstr(req, "GET /snapshot") || strstr(req, "GET /stream")) {
        LOG_I(LOG_TAG_VIDEO, "Client requesting /snapshot");
        send_snapshot(client_fd);

    } else if (strstr(req, "GET /api/status ") || strstr(req, "GET /api/status\r")) {
        send_status_json(client_fd);

    } else if (strstr(req, "GET /api/events ") || strstr(req, "GET /api/events\r")) {
        send_events_json(client_fd);

    } else if (strstr(req, "GET /api/persons")) {
        send_persons_json(client_fd);

    } else if (strstr(req, "GET /api/reset ") || strstr(req, "GET /api/reset\r")) {
        fall_detect_reset();
        const char *ok = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n"
                         "Access-Control-Allow-Origin: *\r\n"
                         "Connection: close\r\n\r\nOK";
        send(client_fd, ok, strlen(ok), MSG_NOSIGNAL);

    } else {
        /* 默认: 仪表盘主页 */
        send_dashboard(client_fd);
    }

done:
    close(client_fd);
    for (int i = 0; i < RTSP_MAX_CLIENTS; i++) {
        if (g_rtsp.clients[i].active && g_rtsp.clients[i].fd == client_fd) {
            g_rtsp.clients[i].active = 0;
            break;
        }
    }
    return NULL;
}

static void *http_server_thread(void *arg)
{
    (void)arg;

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        LOG_E(LOG_TAG_VIDEO, "HTTP server socket create failed");
        return NULL;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(g_rtsp.port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_E(LOG_TAG_VIDEO, "HTTP server bind failed (port %d): %s",
              g_rtsp.port, strerror(errno));
        close(server_fd);
        return NULL;
    }

    if (listen(server_fd, RTSP_MAX_CLIENTS) < 0) {
        LOG_E(LOG_TAG_VIDEO, "HTTP server listen failed");
        close(server_fd);
        return NULL;
    }

    g_rtsp.server_fd = server_fd;

    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(server_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    LOG_I(LOG_TAG_VIDEO, "=========================================");
    LOG_I(LOG_TAG_VIDEO, "  Fall Detection Dashboard started");
    LOG_I(LOG_TAG_VIDEO, "  http://localhost:%d/", g_rtsp.port);
    LOG_I(LOG_TAG_VIDEO, "=========================================");

    while (g_rtsp.running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) continue;

        int slot = -1;
        for (int i = 0; i < RTSP_MAX_CLIENTS; i++) {
            if (!g_rtsp.clients[i].active) {
                slot = i;
                break;
            }
        }

        if (slot >= 0) {
            struct timeval client_tv = { .tv_sec = 5, .tv_usec = 0 };
            setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &client_tv, sizeof(client_tv));

            /* 强制关闭时立即释放, 避免 CLOSE_WAIT 堆积 */
            struct linger lg = { .l_onoff = 1, .l_linger = 0 };
            setsockopt(client_fd, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));

            g_rtsp.clients[slot].fd = client_fd;
            g_rtsp.clients[slot].active = 1;
            g_rtsp.clients[slot].connected_tick = rt_tick_get();

            pthread_t tid;
            pthread_create(&tid, NULL, http_client_thread, (void *)(intptr_t)client_fd);
            pthread_detach(tid);
        } else {
            const char *busy = "HTTP/1.1 503 Service Unavailable\r\n\r\nServer busy";
            send(client_fd, busy, strlen(busy), MSG_NOSIGNAL);
            close(client_fd);
        }
    }

    close(server_fd);
    return NULL;
}

#endif /* RT_USING_MOCK */

/* ========== 公共 API ========== */

fall_err_t video_rtsp_start(uint16_t port)
{
    if (g_rtsp.running) return FALL_OK;

    rt_memset(&g_rtsp, 0, sizeof(g_rtsp));
    g_rtsp.port = port;
    g_rtsp.running = 1;

#ifdef RT_USING_MOCK
    signal(SIGPIPE, SIG_IGN);
    pthread_mutex_init(&g_rtsp.frame_lock, NULL);
    pthread_mutex_init(&g_rtsp.event_lock, NULL);

    pthread_t tid;
    if (pthread_create(&tid, NULL, http_server_thread, NULL) != 0) {
        LOG_E(LOG_TAG_VIDEO, "Failed to create HTTP server thread");
        g_rtsp.running = 0;
        return FALL_ERR_NOMEM;
    }
    pthread_detach(tid);
#elif defined(RT_USING_LWIP)
    g_rtsp.thread = rt_thread_create("rtsp_srv", rtsp_server_thread,
                                      RT_NULL, 8192,
                                      RT_THREAD_PRIORITY_MAX - 3, 10);
    if (g_rtsp.thread == RT_NULL) {
        LOG_E(LOG_TAG_VIDEO, "Failed to create RTSP server thread");
        g_rtsp.running = 0;
        return FALL_ERR_NOMEM;
    }
    rt_thread_startup(g_rtsp.thread);
#endif

    return FALL_OK;
}

void video_rtsp_stop(void)
{
    g_rtsp.running = 0;

#ifdef RT_USING_MOCK
    if (g_rtsp.server_fd >= 0) {
        close(g_rtsp.server_fd);
        g_rtsp.server_fd = -1;
    }

    for (int i = 0; i < RTSP_MAX_CLIENTS; i++) {
        if (g_rtsp.clients[i].active) {
            close(g_rtsp.clients[i].fd);
            g_rtsp.clients[i].active = 0;
        }
    }

    pthread_mutex_lock(&g_rtsp.frame_lock);
    if (g_rtsp.latest_jpeg) {
        free(g_rtsp.latest_jpeg);
        g_rtsp.latest_jpeg = NULL;
    }
    pthread_mutex_unlock(&g_rtsp.frame_lock);

    pthread_mutex_destroy(&g_rtsp.frame_lock);
    pthread_mutex_destroy(&g_rtsp.event_lock);
#elif defined(RT_USING_LWIP)
    for (int i = 0; i < RTSP_MAX_CLIENTS; i++) {
        if (g_rtsp.clients[i].active) {
            lwip_close(g_rtsp.clients[i].fd);
            g_rtsp.clients[i].active = 0;
        }
    }
#endif

    LOG_I(LOG_TAG_VIDEO, "Video server stopped");
}

void video_rtsp_push_frame(video_frame_t *frame)
{
    if (frame == RT_NULL) return;
    g_rtsp.frame_count++;
    /* PC 模式: 由 video_rtsp_push_frame_with_overlay 统一处理编码 */
}

void video_rtsp_push_frame_with_overlay(video_frame_t *frame)
{
    if (frame == RT_NULL) return;
    g_rtsp.frame_count++;

#ifdef RT_USING_MOCK
    if (g_rtsp.running) {
        pose_overlay_draw(frame->frame_buf, frame->width, frame->height);
        encode_and_update(frame);
    }
#endif
}

void video_rtsp_push_event(const fall_event_t *event)
{
    if (event == RT_NULL) return;

#ifdef RT_USING_MOCK
    pthread_mutex_lock(&g_rtsp.event_lock);
    int idx = g_rtsp.event_write_idx % MAX_EVENTS;
    rt_memcpy(&g_rtsp.events[idx], event, sizeof(fall_event_t));
    g_rtsp.event_write_idx = (g_rtsp.event_write_idx + 1) % MAX_EVENTS;
    if (g_rtsp.event_count < MAX_EVENTS) g_rtsp.event_count++;
    pthread_mutex_unlock(&g_rtsp.event_lock);
#else
    (void)event;
#endif
}

int video_rtsp_get_clients(void)
{
    int count = 0;
    for (int i = 0; i < RTSP_MAX_CLIENTS; i++) {
        if (g_rtsp.clients[i].active) count++;
    }
    return count;
}
