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
<title>xiaozhi-DZ - Bộ Đổi Hình Nền & Video Nền</title>
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
.tag-vid{background:#0284c7}
.status-msg{padding:10px;border-radius:8px;text-align:center;font-size:13px;display:none;margin-top:10px}
.status-success{background:rgba(16,185,129,0.2);color:#34d399;border:1px solid #059669;display:block}
.status-error{background:rgba(239,68,68,0.2);color:#f87171;border:1px solid #dc2626;display:block}
video{width:100%;border-radius:10px;margin-bottom:8px;background:#000;max-height:180px}
.radio-group{display:flex;gap:8px;margin-top:4px}
.radio-btn{flex:1;padding:6px;border:1px solid var(--border);border-radius:8px;background:#151e2e;color:var(--text);font-size:12px;cursor:pointer;text-align:center}
.radio-btn.selected{background:var(--primary);border-color:var(--primary);font-weight:600}
</style>
</head>
<body>
<header>
  <div class="badge">xiaozhi-DZ Control</div>
  <h1>Quản Lý Hình Nền & Video Nền</h1>
</header>

<div class="card">
  <div class="card-title">📷 🎬 1. Tải Ảnh Hoặc Video Từ Điện Thoại</div>
  <div class="upload-box" onclick="document.getElementById('fileInput').click()">
    <div style="font-size:32px;margin-bottom:6px">🖼️ 🎬</div>
    <div style="font-weight:600;font-size:14px">Chạm vào đây để chọn Ảnh hoặc Video</div>
    <div style="font-size:12px;color:var(--text-muted);margin-top:4px">Hỗ trợ Ảnh tĩnh (JPG, PNG), Ảnh động (GIF) & Video clip (MP4, MOV)</div>
  </div>
  <input type="file" id="fileInput" accept="image/*,video/*">

  <!-- Video player preview if video selected -->
  <div id="videoBox" style="display:none">
    <video id="videoPlayer" playsinline muted controls></video>
    <div class="slider-group">
      <label><span>Thời điểm bắt đầu cắt clip</span><span id="vidStartVal">0.0s</span></label>
      <input type="range" id="vidStartRange" min="0" max="10" step="0.1" value="0">
    </div>
    <div class="slider-group">
      <label><span>Thời lượng Video nền (Loop)</span></label>
      <div class="radio-group">
        <div class="radio-btn selected" onclick="setDuration(2.5, this)">2.5 giây (Rất nhẹ)</div>
        <div class="radio-btn" onclick="setDuration(3.5, this)">3.5 giây (Chuẩn)</div>
        <div class="radio-btn" onclick="setDuration(5.0, this)">5.0 giây (Dài)</div>
      </div>
    </div>
  </div>

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
    <button class="btn" id="uploadBtn" onclick="uploadNewWallpaper()">🚀 Lưu Thành Nền Mới & Bật Ngay</button>
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
  <div class="card-title">🖼️ 🎬 3. Danh Sách Hình Nền & Video Đã Lưu</div>
  <div id="wallpaperList">Đang tải danh sách...</div>
</div>

<script>
var X={signature:"GIF",version:"89a",trailer:59,extensionIntroducer:33,applicationExtensionLabel:255,graphicControlExtensionLabel:249,imageSeparator:44,signatureSize:3,versionSize:3,globalColorTableFlagMask:128,colorResolutionMask:112,sortFlagMask:8,globalColorTableSizeMask:7,applicationIdentifierSize:8,applicationAuthCodeSize:3,disposalMethodMask:28,userInputFlagMask:2,transparentColorFlagMask:1,localColorTableFlagMask:128,interlaceFlagMask:64,idSortFlagMask:32,localColorTableSizeMask:7};function F(t=256){let e=0,s=new Uint8Array(t);return{get buffer(){return s.buffer},reset(){e=0},bytesView(){return s.subarray(0,e)},bytes(){return s.slice(0,e)},writeByte(r){n(e+1),s[e]=r,e++},writeBytes(r,o=0,i=r.length){n(e+i);for(let c=0;c<i;c++)s[e++]=r[c+o]},writeBytesView(r,o=0,i=r.byteLength){n(e+i),s.set(r.subarray(o,o+i),e),e+=i}};function n(r){var o=s.length;if(o>=r)return;var i=1024*1024;r=Math.max(r,o*(o<i?2:1.125)>>>0),o!=0&&(r=Math.max(r,256));let c=s;s=new Uint8Array(r),e>0&&s.set(c.subarray(0,e),0)}}var O=12,J=5003,lt=[0,1,3,7,15,31,63,127,255,511,1023,2047,4095,8191,16383,32767,65535];function at(t,e,s,n,r=F(512),o=new Uint8Array(256),i=new Int32Array(J),c=new Int32Array(J)){let x=i.length,a=Math.max(2,n);o.fill(0),c.fill(0),i.fill(-1);let l=0,f=0,g=a+1,h=g,b=!1,w=h,_=(1<<w)-1,u=1<<g-1,k=u+1,B=u+2,p=0,A=s[0],z=0;for(let y=x;y<65536;y*=2)++z;z=8-z,r.writeByte(a),I(u);let d=s.length;for(let y=1;y<d;y++){t:{let m=s[y],v=(m<<O)+A,M=m<<z^A;if(i[M]===v){A=c[M];break t}let V=M===0?1:x-M;for(;i[M]>=0;)if(M-=V,M<0&&(M+=x),i[M]===v){A=c[M];break t}I(A),A=m,B<1<<O?(c[M]=B++,i[M]=v):(i.fill(-1),B=u+2,b=!0,I(u))}}return I(A),I(k),r.writeByte(0),r.bytesView();function I(y){for(l&=lt[f],f>0?l|=y<<f:l=y,f+=w;f>=8;)o[p++]=l&255,p>=254&&(r.writeByte(p),r.writeBytesView(o,0,p),p=0),l>>=8,f-=8;if((B>_||b)&&(b?(w=h,_=(1<<w)-1,b=!1):(++w,_=w===O?1<<w:(1<<w)-1)),y==k){for(;f>0;)o[p++]=l&255,p>=254&&(r.writeByte(p),r.writeBytesView(o,0,p),p=0),l>>=8,f-=8;p>0&&(r.writeByte(p),r.writeBytesView(o,0,p),p=0)}}}var $=at;function D(t,e,s){return t<<8&63488|e<<2&992|s>>3}function G(t,e,s,n){return t>>4|e&240|(s&240)<<4|(n&240)<<8}function j(t,e,s){return t>>4<<8|e&240|s>>4}function R(t,e,s){return t<e?e:t>s?s:t}function T(t){return t*t}function tt(t,e,s){var n=0,r=1e100;let o=t[e],i=o.cnt,c=o.ac,x=o.rc,a=o.gc,l=o.bc;for(var f=o.fw;f!=0;f=t[f].fw){let h=t[f],b=h.cnt,w=i*b/(i+b);if(!(w>=r)){var g=0;s&&(g+=w*T(h.ac-c),g>=r)||(g+=w*T(h.rc-x),!(g>=r)&&(g+=w*T(h.gc-a),!(g>=r)&&(g+=w*T(h.bc-l),!(g>=r)&&(r=g,n=f))))}}o.err=r,o.nn=n}function Q(){return{ac:0,rc:0,gc:0,bc:0,cnt:0,nn:0,fw:0,bk:0,tm:0,mtm:0,err:0}}function ut(t,e){let s=e==="rgb444"?4096:65536,n=new Array(s),r=t.length;if(e==="rgba4444")for(let o=0;o<r;++o){let i=t[o],c=i>>24&255,x=i>>16&255,a=i>>8&255,l=i&255,f=G(l,a,x,c),g=f in n?n[f]:n[f]=Q();g.rc+=l,g.gc+=a,g.bc+=x,g.ac+=c,g.cnt++}else if(e==="rgb444")for(let o=0;o<r;++o){let i=t[o],c=i>>16&255,x=i>>8&255,a=i&255,l=j(a,x,c),f=l in n?n[l]:n[l]=Q();f.rc+=a,f.gc+=x,f.bc+=c,f.cnt++}else for(let o=0;o<r;++o){let i=t[o],c=i>>16&255,x=i>>8&255,a=i&255,l=D(a,x,c),f=l in n?n[l]:n[l]=Q();f.rc+=a,f.gc+=x,f.bc+=c,f.cnt++}return n}function H(t,e,s={}){let{format:n="rgb565",clearAlpha:r=!0,clearAlphaColor:o=0,clearAlphaThreshold:i=0,oneBitAlpha:c=!1}=s;if(!t||!t.buffer)throw new Error("quantize() expected RGBA Uint8Array data");if(!(t instanceof Uint8Array)&&!(t instanceof Uint8ClampedArray))throw new Error("quantize() expected RGBA Uint8Array data");let x=new Uint32Array(t.buffer),a=s.useSqrt!==!1,l=n==="rgba4444",f=ut(x,n),g=f.length,h=g-1,b=new Uint32Array(g+1);for(var w=0,u=0;u<g;++u){let C=f[u];if(C!=null){var _=1/C.cnt;l&&(C.ac*=_),C.rc*=_,C.gc*=_,C.bc*=_,f[w++]=C}}T(e)/w<.022&&(a=!1);for(var u=0;u<w-1;++u)f[u].fw=u+1,f[u+1].bk=u,a&&(f[u].cnt=Math.sqrt(f[u].cnt));a&&(f[u].cnt=Math.sqrt(f[u].cnt));var k,B,p;for(u=0;u<w;++u){tt(f,u,!1);var A=f[u].err;for(B=++b[0];B>1&&(p=B>>1,!(f[k=b[p]].err<=A));B=p)b[B]=k;b[B]=u}var z=w-e;for(u=0;u<z;){for(var d;;){var I=b[1];if(d=f[I],d.tm>=d.mtm&&f[d.nn].mtm<=d.tm)break;d.mtm==h?I=b[1]=b[b[0]--]:(tt(f,I,!1),d.tm=u);var A=f[I].err;for(B=1;(p=B+B)<=b[0]&&(p<b[0]&&f[b[p]].err>f[b[p+1]].err&&p++,!(A<=f[k=b[p]].err));B=p)b[B]=k;b[B]=I}var y=f[d.nn],m=d.cnt,v=y.cnt,_=1/(m+v);l&&(d.ac=_*(m*d.ac+v*y.ac)),d.rc=_*(m*d.rc+v*y.rc),d.gc=_*(m*d.gc+v*y.gc),d.bc=_*(m*d.bc+v*y.bc),d.cnt+=y.cnt,d.mtm=++u,f[y.bk].fw=y.fw,f[y.fw].bk=y.bk,y.mtm=h}let M=[];var V=0;for(u=0;;++V){let L=R(Math.round(f[u].rc),0,255),C=R(Math.round(f[u].gc),0,255),Y=R(Math.round(f[u].bc),0,255),E=255;if(l){if(E=R(Math.round(f[u].ac),0,255),c){let st=typeof c=="number"?c:127;E=E<=st?0:255}r&&E<=i&&(L=C=Y=o,E=0)}let K=l?[L,C,Y,E]:[L,C,Y];if(xt(M,K)||M.push(K),(u=f[u].fw)==0)break}return M}function xt(t,e){for(let s=0;s<t.length;s++){let n=t[s],r=n[0]===e[0]&&n[1]===e[1]&&n[2]===e[2],o=n.length>=4&&e.length>=4?n[3]===e[3]:!0;if(r&&o)return!0}return!1}function U(t,e){var s=0,n;for(n=0;n<t.length;n++){let r=t[n]-e[n];s+=r*r}return s}function P(t,e){return e>1?Math.round(t/e)*e:t}function et(t,{roundRGB:e=5,roundAlpha:s=10,oneBitAlpha:n=null}={}){let r=new Uint32Array(t.buffer);for(let o=0;o<r.length;o++){let i=r[o],c=i>>24&255,x=i>>16&255,a=i>>8&255,l=i&255;if(c=P(c,s),n){let f=typeof n=="number"?n:127;c=c<=f?0:255}l=P(l,e),a=P(a,e),x=P(x,e),r[o]=c<<24|x<<16|a<<8|l<<0}}function nt(t,e,s="rgb565"){if(!t||!t.buffer)throw new Error("quantize() expected RGBA Uint8Array data");if(!(t instanceof Uint8Array)&&!(t instanceof Uint8ClampedArray))throw new Error("quantize() expected RGBA Uint8Array data");if(e.length>256)throw new Error("applyPalette() only works with 256 colors or less");let n=new Uint32Array(t.buffer),r=n.length,o=s==="rgb444"?4096:65536,i=new Uint8Array(r),c=new Array(o),x=s==="rgba4444";if(s==="rgba4444")for(let a=0;a<r;a++){let l=n[a],f=l>>24&255,g=l>>16&255,h=l>>8&255,b=l&255,w=G(b,h,g,f),_=w in c?c[w]:c[w]=gt(b,h,g,f,e);i[a]=_}else{let a=s==="rgb444"?j:D;for(let l=0;l<r;l++){let f=n[l],g=f>>16&255,h=f>>8&255,b=f&255,w=a(b,h,g),_=w in c?c[w]:c[w]=bt(b,h,g,e);i[l]=_}}return i}function gt(t,e,s,n,r){let o=0,i=1e100;for(let c=0;c<r.length;c++){let x=r[c],a=x[3],l=q(a-n);if(l>i)continue;let f=x[0];if(l+=q(f-t),l>i)continue;let g=x[1];if(l+=q(g-e),l>i)continue;let h=x[2];l+=q(h-s),!(l>i)&&(i=l,o=c)}return o}function bt(t,e,s,n){let r=0,o=1e100;for(let i=0;i<n.length;i++){let c=n[i],x=c[0],a=q(x-t);if(a>o)continue;let l=c[1];if(a+=q(l-e),a>o)continue;let f=c[2];a+=q(f-s),!(a>o)&&(o=a,r=i)}return r}function rt(t,e,s=5){if(!t.length||!e.length)return;let n=t.map(i=>i.slice(0,3)),r=s*s,o=t[0].length;for(let i=0;i<e.length;i++){let c=e[i];c.length<o?c=[c[0],c[1],c[2],255]:c.length>o?c=c.slice(0,3):c=c.slice();let x=N(n,c.slice(0,3),U),a=x[0],l=x[1];l>0&&l<=r&&(t[a]=c)}}function q(t){return t*t}function W(t,e,s=U){let n=Infinity,r=-1;for(let o=0;o<t.length;o++){let i=t[o],c=s(e,i);c<n&&(n=c,r=o)}return r}function N(t,e,s=U){let n=Infinity,r=-1;for(let o=0;o<t.length;o++){let i=t[o],c=s(e,i);c<n&&(n=c,r=o)}return[r,n]}function ot(t,e,s=U){return t[W(t,e,s)]}function ct(t={}){let{initialCapacity:e=4096,auto:s=!0}=t,n=F(e),r=5003,o=new Uint8Array(256),i=new Int32Array(r),c=new Int32Array(r),x=!1;return{reset(){n.reset(),x=!1},finish(){n.writeByte(X.trailer)},bytes(){return n.bytes()},bytesView(){return n.bytesView()},get buffer(){return n.buffer},get stream(){return n},writeHeader:a,writeFrame(l,f,g,h={}){let{transparent:b=!1,transparentIndex:w=0,delay:_=0,palette:u=null,repeat:k=0,colorDepth:B=8,dispose:p=-1}=h,A=!1;if(s?x||(A=!0,a(),x=!0):A=Boolean(h.first),f=Math.max(0,Math.floor(f)),g=Math.max(0,Math.floor(g)),A){if(!u)throw new Error("First frame must include a { palette } option");pt(n,f,g,u,B),it(n,u),k>=0&&dt(n,k)}let z=Math.round(_/10);wt(n,p,z,b,w);let d=Boolean(u)&&!A;ht(n,f,g,d?u:null),d&&it(n,u),yt(n,l,f,g,B,o,i,c)}};function a(){ft(n,"GIF89a")}}function wt(t,e,s,n,r){t.writeByte(33),t.writeByte(249),t.writeByte(4),r<0&&(r=0,n=!1);var o,i;n?(o=1,i=2):(o=0,i=0),e>=0&&(i=e&7),i<<=2;let c=0;t.writeByte(0|i|c|o),S(t,s),t.writeByte(r||0),t.writeByte(0)}function pt(t,e,s,n,r=8){let o=1,i=0,c=Z(n.length)-1,x=o<<7|r-1<<4|i<<3|c,a=0,l=0;S(t,e),S(t,s),t.writeBytes([x,a,l])}function dt(t,e){t.writeByte(33),t.writeByte(255),t.writeByte(11),ft(t,"NETSCAPE2.0"),t.writeByte(3),t.writeByte(1),S(t,e),t.writeByte(0)}function it(t,e){let s=1<<Z(e.length);for(let n=0;n<s;n++){let r=[0,0,0];n<e.length&&(r=e[n]),t.writeByte(r[0]),t.writeByte(r[1]),t.writeByte(r[2])}}function ht(t,e,s,n){if(t.writeByte(44),S(t,0),S(t,0),S(t,e),S(t,s),n){let r=0,o=0,i=Z(n.length)-1;t.writeByte(128|r|o|0|i)}else t.writeByte(0)}function yt(t,e,s,n,r=8,o,i,c){$(s,n,e,r,t,o,i,c)}function S(t,e){t.writeByte(e&255),t.writeByte(e>>8&255)}function ft(t,e){for(var s=0;s<e.length;s++)t.writeByte(e.charCodeAt(s))}function Z(t){return Math.max(Math.ceil(Math.log2(t)),1)}var Bt=ct;window.GIFEncoder=ct;window.quantize=H;window.applyPalette=nt;
//# sourceMappingURL=gifenc.esm.js.map


let srcImg = null;
let srcVideo = null;
let isVideoMode = false;
let isGifDirect = false;
let rawGifBlob = null;
let curStatus = null;
let vidDuration = 2.5;

function setDuration(dur, el) {
  vidDuration = dur;
  document.querySelectorAll('.radio-btn').forEach(b => b.classList.remove('selected'));
  el.classList.add('selected');
}

const fileInput = document.getElementById('fileInput');
fileInput.onchange = function(e) {
  const file = e.target.files[0];
  if (!file) return;

  const isVid = file.type.startsWith('video/') || /\.(mp4|mov|webm|avi|mkv)$/i.test(file.name);
  const isGif = file.type === 'image/gif' || /\.gif$/i.test(file.name);

  if (isVid) {
    handleVideo(file);
  } else if (isGif) {
    handleGif(file);
  } else {
    handleImage(file);
  }
};

function handleVideo(file) {
  isVideoMode = true;
  isGifDirect = false;
  srcImg = null;
  const vid = document.getElementById('videoPlayer');
  vid.src = URL.createObjectURL(file);
  vid.onloadedmetadata = function() {
    document.getElementById('videoBox').style.display = 'block';
    document.getElementById('cropControls').style.display = 'block';
    const maxStart = Math.max(0, vid.duration - 1);
    const startRange = document.getElementById('vidStartRange');
    startRange.max = maxStart;
    startRange.value = 0;
    document.getElementById('vidStartVal').innerText = '0.0s';
    vid.currentTime = 0;
    srcVideo = vid;
    renderCanvases();
  };
  vid.ontimeupdate = function() {
    if (!vid.paused) renderCanvases();
  };
}

document.getElementById('vidStartRange').oninput = function() {
  document.getElementById('vidStartVal').innerText = parseFloat(this.value).toFixed(1) + 's';
  if (srcVideo) {
    srcVideo.currentTime = parseFloat(this.value);
    renderCanvases();
  }
};

function handleGif(file) {
  isVideoMode = false;
  isGifDirect = true;
  rawGifBlob = file;
  document.getElementById('videoBox').style.display = 'none';
  const reader = new FileReader();
  reader.onload = function(ev) {
    srcImg = new Image();
    srcImg.onload = function() {
      document.getElementById('cropControls').style.display = 'block';
      renderCanvases();
    };
    srcImg.src = ev.target.result;
  };
  reader.readAsDataURL(file);
}

function handleImage(file) {
  isVideoMode = false;
  isGifDirect = false;
  rawGifBlob = null;
  document.getElementById('videoBox').style.display = 'none';
  const reader = new FileReader();
  reader.onload = function(ev) {
    srcImg = new Image();
    srcImg.onload = function() {
      document.getElementById('cropControls').style.display = 'block';
      renderCanvases();
    };
    srcImg.src = ev.target.result;
  };
  reader.readAsDataURL(file);
}

['zoomRange','panXRange','panYRange'].forEach(id => {
  document.getElementById(id).oninput = function() {
    if (id === 'zoomRange') document.getElementById('zoomVal').innerText = this.value + 'x';
    renderCanvases();
  };
});

function getSourceElement() {
  return isVideoMode ? srcVideo : srcImg;
}

function renderCanvases() {
  const source = getSourceElement();
  if (!source) return;
  const zoom = parseFloat(document.getElementById('zoomRange').value);
  const panX = parseInt(document.getElementById('panXRange').value) / 100;
  const panY = parseInt(document.getElementById('panYRange').value) / 100;
  drawCover(document.getElementById('canvasP'), 240, 320, zoom, panX, panY, source);
  drawCover(document.getElementById('canvasL'), 320, 240, zoom, panX, panY, source);
}

function drawCover(canvas, targetW, targetH, zoom, panX, panY, source) {
  const ctx = canvas.getContext('2d');
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  const srcW = source.videoWidth || source.naturalWidth || source.width;
  const srcH = source.videoHeight || source.naturalHeight || source.height;
  if (!srcW || !srcH) return;

  const scale = Math.max(targetW / srcW, targetH / srcH) * zoom;
  const drawW = srcW * scale;
  const drawH = srcH * scale;
  const extraW = Math.max(0, drawW - targetW);
  const extraH = Math.max(0, drawH - targetH);
  const offX = -(extraW * panX);
  const offY = -(extraH * panY);

  const offCanvas = document.createElement('canvas');
  offCanvas.width = targetW;
  offCanvas.height = targetH;
  const offCtx = offCanvas.getContext('2d');
  offCtx.drawImage(source, offX, offY, drawW, drawH);
  ctx.drawImage(offCanvas, 0, 0, canvas.width, canvas.height);
}

function showMsg(text, isSuccess) {
  const el = document.getElementById('statusMsg');
  el.className = 'status-msg ' + (isSuccess ? 'status-success' : 'status-error');
  el.innerText = text;
  el.style.display = 'block';
  setTimeout(() => { el.style.display = 'none'; }, 5000);
}

async function loadStatus() {
  try {
    const res = await fetch('/api/status');
    curStatus = await res.json();
    let html = '';
    curStatus.wallpapers.forEach(wp => {
      const isActive = (wp.idx === curStatus.active_idx);
      const isVid = wp.is_video || false;
      const icon = isVid ? '🎬' : (wp.is_system ? '🔒' : '🖼️');
      const sub = isVid ? 'Video nền hoạt ảnh (Loop)' : (wp.is_system ? 'Ảnh gốc hệ thống' : 'Ảnh tĩnh đã lưu');
      html += `<div class="list-item ${isActive ? 'active' : ''}">
        <div class="list-info">
          <span style="font-size:22px">${icon}</span>
          <div>
            <div style="font-weight:600;font-size:14px">${wp.name} ${isActive ? '<span class="tag">Đang Bật</span>' : ''} ${isVid ? '<span class="tag tag-vid">Video Loop</span>' : ''}</div>
            <div style="font-size:11px;color:var(--text-muted)">${sub}</div>
          </div>
        </div>
        <div style="display:flex;gap:6px">
          ${!isActive ? `<button class="btn btn-sm" onclick="selectWp(${wp.idx})">Bật</button>` : `<button class="btn btn-sm" style="background:#059669;cursor:default;">✓ Đang Bật</button>`}
          ${!wp.is_system ? `<button class="btn btn-sm btn-danger" onclick="deleteWp(${wp.idx})">Xóa</button>` : ''}
        </div>
      </div>`;
    });
    document.getElementById('wallpaperList').innerHTML = html;
  } catch (e) {
    console.error(e);
  }
}

async function selectWp(idx) {
  try {
    await fetch(`/api/select?idx=${idx}`, { method: 'POST' });
    showMsg('Đã bật hình nền ' + idx, true);
    loadStatus();
  } catch (e) {
    showMsg('Lỗi khi bật ảnh/video', false);
  }
}

async function deleteWp(idx) {
  if (!confirm('Bạn có chắc muốn xóa Ảnh/Video ' + idx + '?')) return;
  try {
    await fetch(`/api/delete?idx=${idx}`, { method: 'POST' });
    showMsg('Đã xóa Ảnh/Video ' + idx, true);
    loadStatus();
  } catch (e) {
    showMsg('Lỗi khi xóa ảnh/video', false);
  }
}

// Convert video slice to GIF blob
async function videoToGifBlob(targetW, targetH, progressCallback) {
  const vid = srcVideo;
  const zoom = parseFloat(document.getElementById('zoomRange').value);
  const panX = parseInt(document.getElementById('panXRange').value) / 100;
  const panY = parseInt(document.getElementById('panYRange').value) / 100;
  const startTime = parseFloat(document.getElementById('vidStartRange').value);
  
  const fps = 10;
  const frameCount = Math.floor(vidDuration * fps);
  const frameInterval = 1 / fps;

  const offCanvas = document.createElement('canvas');
  offCanvas.width = targetW;
  offCanvas.height = targetH;
  const offCtx = offCanvas.getContext('2d', { willReadFrequently: true });

  const srcW = vid.videoWidth;
  const srcH = vid.videoHeight;
  const scale = Math.max(targetW / srcW, targetH / srcH) * zoom;
  const drawW = srcW * scale;
  const drawH = srcH * scale;
  const offX = -(Math.max(0, drawW - targetW) * panX);
  const offY = -(Math.max(0, drawH - targetH) * panY);

  const encoder = window.GIFEncoder();

  for (let i = 0; i < frameCount; i++) {
    const t = startTime + i * frameInterval;
    if (t > vid.duration) break;
    vid.currentTime = t;
    await new Promise(r => {
      const onSeek = () => { vid.removeEventListener('seeked', onSeek); r(); };
      vid.addEventListener('seeked', onSeek);
    });

    offCtx.clearRect(0, 0, targetW, targetH);
    offCtx.drawImage(vid, offX, offY, drawW, drawH);
    const imgData = offCtx.getImageData(0, 0, targetW, targetH);
    const palette = window.quantize(imgData.data, 128);
    const index = window.applyPalette(imgData.data, palette);
    encoder.writeFrame(index, targetW, targetH, { palette, delay: Math.round(1000 / fps) });

    if (progressCallback) progressCallback(i + 1, frameCount);
  }

  encoder.finish();
  return new Blob([encoder.bytes()], { type: 'image/gif' });
}

function imageCanvasToBlob(targetW, targetH) {
  return new Promise(resolve => {
    const zoom = parseFloat(document.getElementById('zoomRange').value);
    const panX = parseInt(document.getElementById('panXRange').value) / 100;
    const panY = parseInt(document.getElementById('panYRange').value) / 100;
    const fullCanvas = document.createElement('canvas');
    fullCanvas.width = targetW;
    fullCanvas.height = targetH;
    const ctx = fullCanvas.getContext('2d');
    const scale = Math.max(targetW / srcImg.width, targetH / srcImg.height) * zoom;
    const drawW = srcImg.width * scale;
    const drawH = srcImg.height * scale;
    const offX = -(Math.max(0, drawW - targetW) * panX);
    const offY = -(Math.max(0, drawH - targetH) * panY);
    ctx.drawImage(srcImg, offX, offY, drawW, drawH);
    fullCanvas.toBlob(blob => resolve(blob), 'image/jpeg', 0.88);
  });
}

async function uploadNewWallpaper() {
  const btn = document.getElementById('uploadBtn');
  btn.disabled = true;
  const nextIdx = curStatus ? curStatus.next_idx : 5;

  try {
    let blobP, blobL;
    if (isVideoMode && srcVideo) {
      btn.innerText = '⏳ Đang nén video Dọc (240x320)...';
      blobP = await videoToGifBlob(240, 320, (f, tot) => {
        btn.innerText = `⏳ Nén video Dọc: ${f}/${tot} khung hình...`;
      });
      btn.innerText = '⏳ Đang nén video Ngang (320x240)...';
      blobL = await videoToGifBlob(320, 240, (f, tot) => {
        btn.innerText = `⏳ Nén video Ngang: ${f}/${tot} khung hình...`;
      });
    } else if (isGifDirect && rawGifBlob) {
      btn.innerText = '⏳ Đang chuẩn bị file GIF...';
      blobP = rawGifBlob;
      blobL = rawGifBlob;
    } else if (srcImg) {
      btn.innerText = '⏳ Đang xử lý ảnh...';
      blobP = await imageCanvasToBlob(240, 320);
      blobL = await imageCanvasToBlob(320, 240);
    } else {
      return;
    }

    btn.innerText = '⏳ Đang gửi file lên thiết bị qua Wi-Fi...';
    const resP = await fetch(`/api/upload?type=portrait&idx=${nextIdx}`, { method: 'POST', body: blobP });
    if (!resP.ok) throw new Error('Failed to upload portrait');
    const resL = await fetch(`/api/upload?type=landscape&idx=${nextIdx}`, { method: 'POST', body: blobL });
    if (!resL.ok) throw new Error('Failed to upload landscape');

    await fetch(`/api/select?idx=${nextIdx}`, { method: 'POST' });
    showMsg(`🎉 Đã lưu thành công ${isVideoMode ? 'Video' : 'Ảnh'} ${nextIdx} và BẬT làm nền!`, true);
    document.getElementById('cropControls').style.display = 'none';
    document.getElementById('videoBox').style.display = 'none';
    srcImg = null;
    srcVideo = null;
    loadStatus();
  } catch (e) {
    console.error(e);
    showMsg('Lỗi khi tải lên thiết bị: ' + e.message, false);
  } finally {
    btn.disabled = false;
    btn.innerText = '🚀 Lưu Thành Nền Mới & Bật Ngay';
  }
}

window.onload = loadStatus;
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
                ",\"is_system\":" + (list[i].is_system ? "true" : "false") +
                ",\"is_video\":" + (list[i].is_video ? "true" : "false") + "}";
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
    if (total_len == 0 || total_len > 1024 * 1024 * 2) {
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
