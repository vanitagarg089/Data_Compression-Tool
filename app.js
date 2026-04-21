/* =========================================================
   app.js  —  Huffman Compression Frontend Logic
   Talks to the C++ backend at http://localhost:8080
   ========================================================= */

const SERVER = "http://localhost:8080";

let currentTab   = "text";
let selectedFile = null;

/* ── Tab switching ── */
function switchTab(tab) {
  currentTab = tab;
  document.getElementById("textTab").style.display = tab === "text" ? "" : "none";
  document.getElementById("fileTab").style.display  = tab === "file" ? "" : "none";

  document.querySelectorAll(".tab").forEach((btn, i) => {
    btn.classList.toggle("active",
      (i === 0 && tab === "text") || (i === 1 && tab === "file")
    );
  });
}

/* ── File input handler ── */
function fileSelected(input) {
  if (!input.files.length) return;
  selectedFile = input.files[0];
  showFileName(selectedFile.name, selectedFile.size);
}

/* ── Drag & Drop ── */
function dropFile(e) {
  e.preventDefault();
  document.getElementById("dropZone").classList.remove("drag");
  if (!e.dataTransfer.files.length) return;
  selectedFile = e.dataTransfer.files[0];
  showFileName(selectedFile.name, selectedFile.size);
}

function showFileName(name, size) {
  const el = document.getElementById("fileLabel");
  el.style.display = "block";
  el.textContent   = `📄 ${name}  (${formatBytes(size)})`;
}

/* ── Main compress action ── */
async function compress() {
  const btn = document.getElementById("btn");
  btn.disabled    = true;
  btn.textContent = "⏳ Compressing…";

  try {
    let data;

    if (currentTab === "text") {
      /* ── Text mode ── */
      const text = document.getElementById("txt").value;
      if (!text.trim()) {
        showError("Please enter some text first.");
        return;
      }
      const resp = await fetch(`${SERVER}/compress/text`, {
        method: "POST",
        body:   text
      });
      if (!resp.ok) throw new Error(`Server ${resp.status}`);
      data = await resp.json();

    } else {
      /* ── File mode ── */
      if (!selectedFile) {
        showError("Please select or drop a file first.");
        return;
      }
      const fd = new FormData();
      fd.append("file", selectedFile);

      const resp = await fetch(`${SERVER}/compress/file`, {
        method: "POST",
        body:   fd
      });
      if (!resp.ok) throw new Error(`Server ${resp.status}`);
      data = await resp.json();
    }

    renderResult(data);

  } catch (err) {
    showError(
      `❌ Cannot reach C++ server at <strong>localhost:8080</strong>.<br>
       Make sure <code>./huffman_server</code> is running.`
    );
    console.error(err);
  } finally {
    btn.disabled    = false;
    btn.textContent = "⚡ Compress Now";
  }
}

/* ── Render result card ── */
function renderResult(d) {
  const compPct   = Math.round((d.compressed / d.original) * 100);
  const fileLabel = d.filename
    ? `<p style="font-size:13px;color:rgba(255,255,255,0.75);margin-bottom:10px">
         File: <strong>${escHtml(d.filename)}</strong>
       </p>`
    : "";

  document.getElementById("out").innerHTML = `
    <h2>✅ Compression Successful</h2>
    ${fileLabel}

    <div class="grid">
      <div class="badge">
        <div class="num">${fmt(d.original)}</div>
        <div class="lbl">Original bits</div>
      </div>
      <div class="badge">
        <div class="num">${fmt(d.compressed)}</div>
        <div class="lbl">Compressed bits</div>
      </div>
      <div class="badge saved">
        <div class="num">${d.savePct}%</div>
        <div class="lbl">Space saved</div>
      </div>
    </div>

    <div class="stat">
      <label>Original size — ${fmt(d.original)} bits</label>
      <div class="bar-bg">
        <div class="bar-fill bar-original" style="width:100%"></div>
      </div>
    </div>

    <div class="stat">
      <label>Compressed size — ${fmt(d.compressed)} bits (${compPct}% of original)</label>
      <div class="bar-bg">
        <div class="bar-fill bar-compressed" style="width:${compPct}%"></div>
      </div>
    </div>

    <div class="stat">
      <label>Unique characters in input: <strong>${d.uniqueChars}</strong></label>
    </div>

    <div class="codes">🔑 Top Huffman codes: ${escHtml(d.topCodes)}</div>
  `;
}

/* ── Error display ── */
function showError(msg) {
  document.getElementById("out").innerHTML = `
    <h2>⚠️ Error</h2>
    <p class="error-msg">${msg}</p>
  `;
}

/* ── Utilities ── */
function fmt(n)   { return Number(n).toLocaleString(); }

function formatBytes(bytes) {
  if (bytes < 1024)        return bytes + " B";
  if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + " KB";
  return (bytes / (1024 * 1024)).toFixed(2) + " MB";
}

function escHtml(str) {
  return String(str)
    .replace(/&/g,"&amp;")
    .replace(/</g,"&lt;")
    .replace(/>/g,"&gt;")
    .replace(/"/g,"&quot;");
}
