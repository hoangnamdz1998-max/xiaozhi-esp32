#include "wallpaper_server.h"
#include "wallpaper_manager.h"
#include "board.h"
#include "display.h"
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <string>
#include <vector>

#define TAG "WallpaperSrv"

static const char HTML_PAGE[] = R"rawliteral(<!DOCTYPE html>
<html lang="vi">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
<title>xiaozhi-DZ - Bộ Đổi Hình Nền</title>
<style>
:root{--primary:#0d9488;--primary-dark:#0f766e;--bg:#0f172a;--card:#1e293b;--text:#f8fafc;--text-muted:#94a3b8;--border:#334155;--danger:#ef4444}
*{box-sizing:border-box;margin:0;padding:0;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif}
body{background:var(--bg);color:var(--text);padding:14px;max-width:480px;margin:0 auto}
header{text-align:center;padding:12px 0 16px;border-bottom:1px solid var(--border);margin-bottom:16px}
.badge{display:inline-block;background:linear-gradient(135deg,#0d9488,#3b82f6);color:#fff;font-weight:700;font-size:12px;padding:4px 10px;border-radius:20px;text-transform:uppercase;letter-spacing:1px;margin-bottom:6px}
h1{font-size:20px;color:#fff}
.card{background:var(--card);border:1px solid var(--border);border-radius:14px;padding:14px;margin-bottom:16px;box-shadow:0 4px 6px -1px rgba(0,0,0,0.2)}
.card-title{font-size:15px;font-weight:600;margin-bottom:12px;display:flex;align-items:center;gap:8px;color:#38bdf8}
.btn{display:inline-flex;align-items:center;justify-content:center;gap:6px;background:var(--primary);color:#fff;border:none;border-radius:10px;padding:10px 16px;font-size:14px;font-weight:600;cursor:pointer;width:100%;transition:0.2s}
.btn:active{transform:scale(0.98);background:var(--primary-dark)}
.btn-secondary{background:#334155}
.btn-danger{background:var(--danger)}
.btn-sm{padding:6px 14px;font-size:13px;width:auto}
input[type=file]{display:none}
.upload-box{border:2px dashed var(--border);border-radius:12px;padding:20px;text-align:center;cursor:pointer;background:#151e2e;margin-bottom:12px}
.preview-container{display:flex;gap:12px;justify-content:center;margin:12px 0;align-items:flex-end}
.preview-col{text-align:center}
.preview-col span{font-size:12px;color:var(--text-muted);display:block;margin-bottom:4px}
canvas{border-radius:8px;border:2px solid var(--primary);box-shadow:0 4px 10px rgba(0,0,0,0.4);background:#000}
.slider-group{margin:10px 0}
.slider-group label{font-size:12px;color:var(--text-muted);display:flex;justify-content:space-between}
input[type=range]{width:100%;margin-top:4px;accent-color:var(--primary)}
.list-item{display:flex;align-items:center;justify-content:space-between;padding:10px;background:#151e2e;border:1px solid var(--border);border-radius:10px;margin-bottom:8px}
.list-item.active{border-color:var(--primary);background:rgba(13,148,136,0.18)}
.list-info{display:flex;align-items:center;gap:10px}
.tag{font-size:11px;padding:2px 8px;border-radius:6px;background:#059669;color:#fff;font-weight:600}
.status-msg{padding:10px;border-radius:8px;text-align:center;font-size:13px;display:none;margin-top:10px}
.status-success{background:rgba(16,185,129,0.2);color:#34d399;border:1px solid #059669;display:block}
.status-error{background:rgba(239,68,68,0.2);color:#f87171;border:1px solid #dc2626;display:block}
</style>
</head>
<body>
<header>
  <div class="badge">xiaozhi-DZ Control</div>
  <h1>Quản Lý Hình Nền Màn Hình Chờ</h1>
</header>

<div class="card">
  <div class="card-title">📷 1. Tải Ảnh Mới Từ Điện Thoại</div>
  <div class="upload-box" onclick="document.getElementById('fileInput').click()">
    <div style="font-size:32px;margin-bottom:6px">🖼️</div>
    <div style="font-weight:600;font-size:14px">Chạm vào đây để chọn ảnh từ điện thoại</div>
    <div style="font-size:12px;color:var(--text-muted);margin-top:4px">Hệ thống sẽ tự căn chỉnh vừa khít cả màn Dọc & Ngang</div>
  </div>
  <input type="file" id="fileInput" accept="image/*">

  <div id="cropControls" style="display:none">
    <div class="preview-container">
      <div class="preview-col">
        <span>Dọc (240x320)</span>
        <canvas id="canvasP" width="120" height="160"></canvas>
      </div>
      <div class="preview-col">
        <span>Ngang (320x240)</span>
        <canvas id="canvasL" width="160" height="120"></canvas>
      </div>
    </div>
    <div class="slider-group">
      <label><span>Thu phóng (Zoom)</span><span id="zoomVal">1.0x</span></label>
      <input type="range" id="zoomRange" min="1" max="2.5" step="0.05" value="1">
    </div>
    <div class="slider-group">
      <label><span>Dịch chuyển Dọc</span><span id="panYVal">Giữa</span></label>
      <input type="range" id="panYRange" min="0" max="100" value="50">
    </div>
    <div class="slider-group">
      <label><span>Dịch chuyển Ngang</span><span id="panXVal">Giữa</span></label>
      <input type="range" id="panXRange" min="0" max="100" value="50">
    </div>
    <button class="btn" id="uploadBtn" onclick="uploadNewWallpaper()">🚀 Lưu Thành Ảnh Mới & Bật Ngay</button>
  </div>
  <div id="statusMsg" class="status-msg"></div>
</div>

<div class="card">
  <div class="card-title">🕒 2. Bố Cục Ngày & Giờ (Màn 2)</div>
  <div style="background:#151e2e;padding:12px;border-radius:10px;border:1px solid var(--border)">
    <div style="font-weight:600;color:#38bdf8">Bố cục 1: Đồng hồ lật ở sát đáy + Thời tiết ở đỉnh (Cố định)</div>
    <div style="font-size:12px;color:var(--text-muted);margin-top:4px">Đồng hồ kiểu Flip Clock chân thực nằm dưới chân màn hình, thời tiết và nhiệt độ ở góc trên.</div>
  </div>
</div>

<div class="card">
  <div class="card-title">🖼️ 3. Danh Sách Hình Nền Đã Lưu</div>
  <div id="wallpaperList">Đang tải danh sách...</div>
</div>

<script>
let srcImg=null,curStatus=null;
document.getElementById('fileInput').onchange=function(e){
  const file=e.target.files[0];
  if(!file)return;
  const reader=new FileReader();
  reader.onload=function(ev){
    srcImg=new Image();
    srcImg.onload=function(){
      document.getElementById('cropControls').style.display='block';
      renderCanvases();
    };
    srcImg.src=ev.target.result;
  };
  reader.readAsDataURL(file);
};
['zoomRange','panXRange','panYRange'].forEach(id=>{
  document.getElementById(id).oninput=function(){
    if(id==='zoomRange')document.getElementById('zoomVal').innerText=this.value+'x';
    renderCanvases();
  };
});
function renderCanvases(){
  if(!srcImg)return;
  const zoom=parseFloat(document.getElementById('zoomRange').value);
  const panX=parseInt(document.getElementById('panXRange').value)/100;
  const panY=parseInt(document.getElementById('panYRange').value)/100;
  drawCover(document.getElementById('canvasP'),240,320,zoom,panX,panY);
  drawCover(document.getElementById('canvasL'),320,240,zoom,panX,panY);
}
function drawCover(canvas,targetW,targetH,zoom,panX,panY){
  const ctx=canvas.getContext('2d');
  ctx.clearRect(0,0,canvas.width,canvas.height);
  const scale=Math.max(targetW/srcImg.width,targetH/srcImg.height)*zoom;
  const drawW=srcImg.width*scale;
  const drawH=srcImg.height*scale;
  const extraW=Math.max(0,drawW-targetW);
  const extraH=Math.max(0,drawH-targetH);
  const offX=-(extraW*panX);
  const offY=-(extraH*panY);
  const offCanvas=document.createElement('canvas');
  offCanvas.width=targetW;
  offCanvas.height=targetH;
  const offCtx=offCanvas.getContext('2d');
  offCtx.drawImage(srcImg,offX,offY,drawW,drawH);
  ctx.drawImage(offCanvas,0,0,canvas.width,canvas.height);
}
function showMsg(text,isSuccess){
  const el=document.getElementById('statusMsg');
  el.className='status-msg '+(isSuccess?'status-success':'status-error');
  el.innerText=text;
  el.style.display='block';
  setTimeout(()=>{el.style.display='none'},4000);
}
async function loadStatus(){
  try{
    const res=await fetch('/api/status');
    curStatus=await res.json();
    let html='';
    curStatus.wallpapers.forEach(wp=>{
      const isActive=(wp.idx===curStatus.active_idx);
      html+=`<div class="list-item ${isActive?'active':''}">
        <div class="list-info">
          <span style="font-size:20px">${wp.is_system?'🔒':'🖼️'}</span>
          <div>
            <div style="font-weight:600;font-size:14px">${wp.name} ${isActive?'<span class="tag">Đang Bật</span>':''}</div>
            <div style="font-size:11px;color:var(--text-muted)">${wp.is_system?'Ảnh gốc hệ thống (Không thể xóa)':'Ảnh tùy chỉnh đã lưu'}</div>
          </div>
        </div>
        <div style="display:flex;gap:6px">
          ${!isActive?`<button class="btn btn-sm" onclick="selectWp(${wp.idx})">Bật</button>`:`<button class="btn btn-sm" style="background:#059669;cursor:default;">✓ Đang Bật</button>`}
          ${!wp.is_system?`<button class="btn btn-sm btn-danger" onclick="deleteWp(${wp.idx})">Xóa</button>`:''}
        </div>
      </div>`;
    });
    document.getElementById('wallpaperList').innerHTML=html;
  }catch(e){
    console.error(e);
  }
}
async function selectWp(idx){
  try{
    await fetch(`/api/select?idx=${idx}`,{method:'POST'});
    showMsg('Đã bật hình nền '+idx,true);
    loadStatus();
  }catch(e){
    showMsg('Lỗi khi bật ảnh',false);
  }
}
async function deleteWp(idx){
  if(!confirm('Bạn có chắc muốn xóa Ảnh '+idx+'?'))return;
  try{
    await fetch(`/api/delete?idx=${idx}`,{method:'POST'});
    showMsg('Đã xóa Ảnh '+idx,true);
    loadStatus();
  }catch(e){
    showMsg('Lỗi khi xóa ảnh',false);
  }
}
function canvasToBlob(canvas,targetW,targetH){
  return new Promise(resolve=>{
    const zoom=parseFloat(document.getElementById('zoomRange').value);
    const panX=parseInt(document.getElementById('panXRange').value)/100;
    const panY=parseInt(document.getElementById('panYRange').value)/100;
    const fullCanvas=document.createElement('canvas');
    fullCanvas.width=targetW;
    fullCanvas.height=targetH;
    const ctx=fullCanvas.getContext('2d');
    const scale=Math.max(targetW/srcImg.width,targetH/srcImg.height)*zoom;
    const drawW=srcImg.width*scale;
    const drawH=srcImg.height*scale;
    const offX=-(Math.max(0,drawW-targetW)*panX);
    const offY=-(Math.max(0,drawH-targetH)*panY);
    ctx.drawImage(srcImg,offX,offY,drawW,drawH);
    fullCanvas.toBlob(blob=>resolve(blob),'image/jpeg',0.88);
  });
}
async function uploadNewWallpaper(){
  if(!srcImg)return;
  const btn=document.getElementById('uploadBtn');
  btn.disabled=true;
  btn.innerText='⏳ Đang xử lý & gửi ảnh lên thiết bị...';
  try{
    const nextIdx=curStatus?curStatus.next_idx:1;
    const blobP=await canvasToBlob(null,240,320);
    const blobL=await canvasToBlob(null,320,240);
    const resP=await fetch(`/api/upload?type=portrait&idx=${nextIdx}`,{method:'POST',body:blobP});
    if(!resP.ok) throw new Error('Failed to upload portrait');
    const resL=await fetch(`/api/upload?type=landscape&idx=${nextIdx}`,{method:'POST',body:blobL});
    if(!resL.ok) throw new Error('Failed to upload landscape');
    await fetch(`/api/select?idx=${nextIdx}`,{method:'POST'});
    showMsg(`🎉 Đã lưu thành công Ảnh ${nextIdx} và BẬT làm hình nền!`,true);
    document.getElementById('cropControls').style.display='none';
    srcImg=null;
    loadStatus();
  }catch(e){
    console.error(e);
    showMsg('Lỗi khi tải ảnh lên thiết bị',false);
  }finally{
    btn.disabled=false;
    btn.innerText='🚀 Lưu Thành Ảnh Mới & Bật Ngay';
  }
}
window.onload=loadStatus;
</script>
</body>
</html>
)rawliteral";

WallpaperServer& WallpaperServer::GetInstance() {
    static WallpaperServer instance;
    return instance;
}

WallpaperServer::WallpaperServer() {
}

WallpaperServer::~WallpaperServer() {
    Stop();
}

static esp_err_t root_get_handler(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, HTML_PAGE, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t status_get_handler(httpd_req_t* req) {
    httpd_resp_set_type(req, "application/json");

    auto& mgr = WallpaperManager::GetInstance();
    int active_idx = mgr.GetActiveWallpaperIndex();
    int active_layout = mgr.GetActiveLayoutIndex();
    int next_idx = mgr.GetNextAvailableIndex();
    auto list = mgr.GetWallpaperList();

    std::string json = "{\"device\":\"xiaozhi-DZ\",\"active_idx\":" + std::to_string(active_idx) +
                       ",\"active_layout\":" + std::to_string(active_layout) +
                       ",\"next_idx\":" + std::to_string(next_idx) +
                       ",\"wallpapers\":[";

    for (size_t i = 0; i < list.size(); ++i) {
        if (i > 0) json += ",";
        json += "{\"idx\":" + std::to_string(list[i].index) +
                ",\"name\":\"" + list[i].name + "\"" +
                ",\"is_system\":" + (list[i].is_system ? "true" : "false") + "}";
    }
    json += "]}";

    return httpd_resp_send(req, json.c_str(), json.length());
}

static esp_err_t upload_post_handler(httpd_req_t* req) {
    char query[128] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing query string");
        return ESP_FAIL;
    }

    char type_str[32] = {0};
    char idx_str[16] = {0};
    httpd_query_key_value(query, "type", type_str, sizeof(type_str));
    httpd_query_key_value(query, "idx", idx_str, sizeof(idx_str));

    bool is_portrait = (strcmp(type_str, "landscape") != 0);
    int idx = atoi(idx_str);
    if (idx <= 0) idx = 1;

    size_t total_len = req->content_len;
    if (total_len == 0 || total_len > 1024 * 512) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid file size");
        return ESP_FAIL;
    }

    uint8_t* buf = (uint8_t*)heap_caps_malloc(total_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = (uint8_t*)malloc(total_len);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    size_t received = 0;
    while (received < total_len) {
        int ret = httpd_req_recv(req, (char*)buf + received, total_len - received);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
            free(buf);
            return ESP_FAIL;
        }
        received += ret;
    }

    bool ok = WallpaperManager::GetInstance().SaveWallpaperData(idx, is_portrait, buf, received);
    free(buf);

    if (!ok) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save file");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    const char* resp = "{\"status\":\"ok\"}";
    return httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t select_post_handler(httpd_req_t* req) {
    char query[128] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char idx_str[16] = {0};
        if (httpd_query_key_value(query, "idx", idx_str, sizeof(idx_str)) == ESP_OK) {
            int idx = atoi(idx_str);
            WallpaperManager::GetInstance().SetActiveWallpaperIndex(idx);
        }
    }

    auto display = Board::GetInstance().GetDisplay();
    if (display) {
        display->ReloadWallpaper();
    }

    httpd_resp_set_type(req, "application/json");
    const char* resp = "{\"status\":\"ok\"}";
    return httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t delete_post_handler(httpd_req_t* req) {
    char query[128] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char idx_str[16] = {0};
        if (httpd_query_key_value(query, "idx", idx_str, sizeof(idx_str)) == ESP_OK) {
            int idx = atoi(idx_str);
            WallpaperManager::GetInstance().DeleteWallpaper(idx);
        }
    }

    auto display = Board::GetInstance().GetDisplay();
    if (display) {
        display->ReloadWallpaper();
    }

    httpd_resp_set_type(req, "application/json");
    const char* resp = "{\"status\":\"ok\"}";
    return httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
}

bool WallpaperServer::Start() {
    if (server_ != nullptr) return true;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 8;
    config.stack_size = 8192;

    esp_err_t ret = httpd_start(&server_, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        return false;
    }

    httpd_uri_t uri_root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(server_, &uri_root);

    httpd_uri_t uri_status = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = status_get_handler,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(server_, &uri_status);

    httpd_uri_t uri_upload = {
        .uri = "/api/upload",
        .method = HTTP_POST,
        .handler = upload_post_handler,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(server_, &uri_upload);

    httpd_uri_t uri_select = {
        .uri = "/api/select",
        .method = HTTP_POST,
        .handler = select_post_handler,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(server_, &uri_select);

    httpd_uri_t uri_delete = {
        .uri = "/api/delete",
        .method = HTTP_POST,
        .handler = delete_post_handler,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(server_, &uri_delete);

    ESP_LOGI(TAG, "Wallpaper Web Server started on port 80");
    return true;
}

void WallpaperServer::Stop() {
    if (server_ != nullptr) {
        httpd_stop(server_);
        server_ = nullptr;
    }
}
