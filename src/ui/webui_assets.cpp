#include "ui/webui.h"

namespace mxr
{

const char *kWebUiHtml = R"HTML(<!doctype html>
<html lang="zh">
<head>
<meta charset="utf-8">
<title>MiniXray 监控</title>
<style>
  body { font: 14px/1.6 system-ui, sans-serif; margin: 0; color: #222; }
  header { padding: 12px 20px; background: #f5f5f5; border-bottom: 1px solid #ddd; }
  header h1 { font-size: 16px; margin: 0; }
  #layout { display: flex; height: calc(100vh - 50px); }
  #sidebar { width: 260px; border-right: 1px solid #ddd; overflow: auto; padding: 12px; }
  #main { flex: 1; overflow: auto; padding: 16px; }
  .run-item { padding: 6px 8px; cursor: pointer; border-radius: 4px; }
  .run-item:hover { background: #eef; }
  .run-item.active { background: #dde; font-weight: 600; }
  .step-item { padding: 4px 16px; color: #555; }
  table { border-collapse: collapse; margin: 8px 0; }
  th, td { border: 1px solid #ccc; padding: 4px 10px; text-align: left; }
  th { background: #fafafa; }
  .warn { color: #b8860b; } .fail { color: #c00; }
  #status { color: #888; font-size: 12px; }
</style>
<script src="https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.min.js"></script>
<script>mermaid.initialize({startOnLoad:false, theme:'default'});</script>
</head>
<body>
<header><h1>MiniXray 监控</h1><span id="status">加载中…</span></header>
<div id="layout">
  <div id="sidebar"></div>
  <div id="main"></div>
</div>
<script>
let currentDir = null, lastMermaid = null, renderSeq = 0;

function esc(s) {
  return String(s == null ? '—' : s).replace(/[&<>"]/g, c =>
    ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
}

function loadList() {
  fetch('/__xray/api/runs').then(r => r.json()).then(data => {
    const box = document.getElementById('sidebar');
    const runs = data.runs || [];
    box.innerHTML = '<strong>会话 (' + runs.length + ')</strong>' +
      runs.map(d => '<div class="run-item' + (d === currentDir ? ' active' : '') +
        '" data-dir="' + esc(d) + '">' + esc(d) + '</div>').join('');
    box.querySelectorAll('.run-item').forEach(el => {
      el.onclick = () => { currentDir = el.dataset.dir; loadRun(true); loadList(); };
    });
    if (!currentDir && runs.length) { currentDir = runs[0]; loadRun(true); loadList(); }
    document.getElementById('status').textContent = '共 ' + runs.length + ' 个会话';
  }).catch(e => { document.getElementById('status').textContent = '列表加载失败: ' + e; });
}

function loadRun(force) {
  if (!currentDir) return;
  fetch('/__xray/api/run?dir=' + encodeURIComponent(currentDir))
    .then(r => r.json()).then(data => {
      if (!data.ok) { document.getElementById('status').textContent = data.error; return; }
      render(data);
      if (force || data.mermaid !== lastMermaid) {
        lastMermaid = data.mermaid;
        const seq = ++renderSeq;
        mermaid.render('graph-' + seq, data.mermaid || 'flowchart TD').then(({svg}) => {
          const host = document.getElementById('graph');
          if (host) host.innerHTML = svg;
        }).catch(err => {
          const host = document.getElementById('graph');
          if (host) host.textContent = '图渲染失败: ' + err;
        });
      }
      document.getElementById('status').textContent = '更新于 ' + new Date().toLocaleTimeString();
    }).catch(e => { document.getElementById('status').textContent = '加载失败: ' + e; });
}

function render(data) {
  const m = data.meta || {};
  const steps = data.steps || [];
  let html = '<h2>' + esc(data.dir) + '</h2>';
  html += '<table><tr><th>步数</th><th>工具</th><th>token</th><th>耗时</th></tr><tr>' +
    '<td>' + esc(m.step_count) + '</td><td>' + esc(m.tool_count) + '</td>' +
    '<td>' + esc(m.total_tokens) + '</td><td>' + esc(m.end_ms - m.start_ms) + 'ms</td>' +
    '</tr></table>';
  html += '<h3>执行图</h3><div id="graph">渲染中…</div>';
  html += '<h3>步骤</h3><table><tr><th>#</th><th>模型</th><th>耗时</th><th>token</th><th>输出</th></tr>';
  steps.forEach(s => {
    html += '<tr><td>' + esc(s.index) + '</td><td>' + esc(s.model) + '</td><td>' +
      esc(s.duration_ms) + 'ms</td><td>' + esc(s.tokens) + '</td><td>' + esc(s.output) + '</td></tr>';
  });
  html += '</table>';
  document.getElementById('main').innerHTML = html;
}

loadList();
setInterval(() => { loadList(); loadRun(false); }, 3000);
</script>
</body>
</html>
)HTML";

} // namespace mxr
