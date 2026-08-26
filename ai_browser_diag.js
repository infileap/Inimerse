// AI Browser Diag: capture browser window -> llava analysis -> print diagnosis
// Usage: node ai_browser_diag.js [extra_prompt]
const { execSync } = require('child_process');
const fs = require('fs');
const http = require('http');

const SHOT = 'D:/inimerse_stable/_shot.png';
const extra = process.argv.slice(2).join(' ') || '';
let diagInfo = '';
try { diagInfo = fs.readFileSync('D:/inimerse_stable/_diag_info.txt', 'utf8'); } catch(e) {}
const infoPart = diagInfo ? '\n\nub�ʭ�o(e�ubJS6�):\n' + diagInfo.slice(0, 1500) : '';

// 1. capture
console.log('>> capturing browser window...');
let cap;
try {
  cap = execSync('powershell -NoProfile -ExecutionPolicy Bypass -File D:/inimerse_stable/diag_browser.ps1', { encoding: 'utf8', timeout: 20000 });
} catch (e) { cap = e.stdout || ''; }
console.log('capture:', cap.trim());
if (cap.trim().startsWith('NO_WINDOW')) {
  console.log('RESULT: 没有找到浏览器窗口。请先打开 Forge/工作台页面再运行。');
  process.exit(1);
}

// 2. base64 image
const b64 = fs.readFileSync(SHOT).toString('base64');

// 3. ask llava
const prompt = '你是界面诊断助手。请仔细观察这张截图(它是Inimerse网页界面的截图),用中文回答:\n' +
  '1. 页面整体是什么界面(标题、布局)?\n' +
  '2. 项目列表/下拉框区域:里面有几个项目?项目名称是什么?(仔细看下拉框或列表)\n' +
  '3. 页面上有没有乱码(像锟斤拷/å¥½/æ¼¢这种乱码字符)?有的话在哪里?\n' +
  '4. 按钮区域:有哪些按钮?是否正常显示?\n' +
  '5. 有没有明显的界面错误(空白区、加载失败提示、报错)?\n' +
  '请具体描述,不要泛泛而谈。' + (extra ? '\n额外问题: ' + extra : '');

const body = JSON.stringify({ model: 'llava:7b', prompt, images: [b64], stream: false });

const req = http.request({ host: '127.0.0.1', port: 11434, path: '/api/generate', method: 'POST',
  headers: { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(body) } },
  (res) => {
    let d = '';
    res.on('data', c => d += c);
    res.on('end', () => {
      try {
        const j = JSON.parse(d);
        console.log('\n========== AI 诊断结果 ==========');
        console.log(j.response || '(empty)');
        console.log('==================================');
      } catch (e) { console.log('AI response parse error:', d.slice(0, 300)); }
    });
  });
req.on('error', e => {
  console.log('AI请求失败(确认 ollama 在运行: ollama serve)');
  console.log(e.message);
});
req.write(body);
req.end();
