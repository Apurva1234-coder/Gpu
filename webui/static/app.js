const state = { jobId: null, analysis: null, device: null, capabilities: null, benchmarkData: [], result: null };
const $ = (selector, root = document) => root.querySelector(selector);
const $$ = (selector, root = document) => [...root.querySelectorAll(selector)];
const nf = new Intl.NumberFormat("en-US", { maximumFractionDigits: 2 });

function escapeHtml(value = "") { return String(value).replace(/[&<>'"]/g, c => ({"&":"&amp;","<":"&lt;",">":"&gt;","'":"&#39;","\"":"&quot;"}[c])); }
function number(value, digits = 2) { return value === null || value === undefined || Number.isNaN(Number(value)) ? "—" : new Intl.NumberFormat("en-US", {maximumFractionDigits: digits}).format(Number(value)); }
function milliseconds(value) { return value === null || value === undefined ? "—" : `${number(value, 2)} ms`; }
function fileSize(bytes) { return bytes < 1024 * 1024 ? `${number(bytes / 1024, 1)} KB` : `${number(bytes / 1024 / 1024, 2)} MB`; }
function statusClass(status) { const text = (status || "").toUpperCase(); return text.includes("OPTIMAL") || text === "FEASIBLE" ? "pass" : text.includes("TIME") || text.includes("ITERATION") || text.includes("UNSUPPORTED") ? "warning" : "bad"; }
function toast(message) { const element = $("#toast"); element.textContent = message; element.classList.remove("hidden"); setTimeout(() => element.classList.add("hidden"), 4200); }
async function api(path, options = {}) { const response = await fetch(path, options); const data = await response.json().catch(() => ({})); if (!response.ok) throw new Error(data.detail || `Request failed (${response.status})`); return data; }

async function boot() {
  try {
    const [device, capabilities, benchmark] = await Promise.all([api("/api/device"), api("/api/capabilities"), api("/api/benchmarks")]);
    state.device = device; state.capabilities = capabilities; state.benchmarkData = benchmark.datasets || [];
    renderDevice(); renderHardware(); renderBenchmarks();
  } catch (error) { toast(`Service unavailable: ${error.message}`); $("#deviceStatus").textContent = "Adapter unavailable"; }
}

function renderDevice() {
  const device = state.device;
  if (!device) return;
  $("#deviceDot").classList.add("online");
  $("#deviceStatus").textContent = device.cuda_available ? "CUDA acceleration active" : "CPU fallback active";
  $("#heroBackend").textContent = device.cuda_available ? "CPU + CUDA" : "CPU";
  $("#heroDevice").textContent = device.cuda_available ? (device.gpu_name || `${device.gpu_count} CUDA device${device.gpu_count === 1 ? "" : "s"}`) : "CUDA unavailable · CPU fallback active";
  $("#engineState").textContent = "READY";
}

function renderHardware() {
  const device = state.device; if (!device) return;
  const cuda = device.cuda_available;
  $("#hardwareArea").innerHTML = `
    <article class="panel hardware-card"><p>CPU</p><strong>${escapeHtml(device.cpu)}</strong><small>${number(device.threads, 0)} logical threads detected</small></article>
    <article class="panel hardware-card"><p>CUDA ACCELERATION</p><strong class="${cuda ? "pass" : "warning"}">${cuda ? "AVAILABLE" : "UNAVAILABLE"}</strong><small>${cuda ? escapeHtml(device.gpu_name || `${device.gpu_count} CUDA device(s)`) : "CPU fallback active"}</small></article>
    <article class="panel hardware-card"><p>SOLVER BACKENDS</p><strong>CPU <span class="pass">AVAILABLE</span></strong><small>CUDA ${cuda ? "available for supported numerical operations" : "unavailable on this system"}</small></article>
    <article class="panel architecture-note">CUDA acceleration is used only for solver operations supported by the GPU backend. Presolve, simplex where applicable, branch-and-bound control, parsing, and verification remain CPU-managed unless the solver explicitly reports otherwise.</article>`;
}

function bindUpload() {
  const zone = $("#dropZone"), input = $("#fileInput");
  $("#browseButton").addEventListener("click", () => input.click());
  input.addEventListener("change", () => input.files[0] && analyzeFile(input.files[0]));
  ["dragenter", "dragover"].forEach(event => zone.addEventListener(event, e => { e.preventDefault(); zone.classList.add("dragging"); }));
  ["dragleave", "drop"].forEach(event => zone.addEventListener(event, e => { e.preventDefault(); zone.classList.remove("dragging"); }));
  zone.addEventListener("drop", e => e.dataTransfer.files[0] && analyzeFile(e.dataTransfer.files[0]));
  $$("[data-example]").forEach(button => button.addEventListener("click", () => loadExample(button.dataset.example)));
}

async function loadExample(name) {
  try { const response = await api(`/api/examples/${name}`, { method: "POST" }); applyAnalysis(response); $("#analysisArea").scrollIntoView({behavior:"smooth", block:"start"}); } catch (error) { toast(error.message); }
}

async function analyzeFile(file) {
  if (!/\.(mps|json|txt)$/i.test(file.name)) return toast("Use a supported MPS, JSON, or TXT model.");
  const form = new FormData(); form.append("file", file);
  $("#dropZone").classList.add("dragging");
  try { const response = await api("/api/analyze", { method: "POST", body: form }); applyAnalysis(response); } catch (error) { toast(error.message); } finally { $("#dropZone").classList.remove("dragging"); }
}

function applyAnalysis(response) {
  state.jobId = response.job_id;
  state.analysis = response.analysis;
  state.result = null;
  const area = $("#analysisArea");
  area.classList.remove("hidden");
  renderAnalysis();
  startAutomaticSolve();
}

function modelMethods(type) {
  if (type === "QP") return [{v:"qp", t:"Newton / Barrier"}];
  if (type === "MILP") return [{v:"milp",t:"Branch-and-Bound"},{v:"cutting-plane",t:"Cutting Planes"},{v:"feasibility-pump",t:"Feasibility Pump"},{v:"lp-relaxation",t:"LP Relaxation"}];
  return [{v:"auto",t:"Auto (Revised Simplex)"},{v:"revised-simplex",t:"Revised Simplex"},{v:"dual-simplex",t:"Dual Simplex"},{v:"ipm",t:"Mehrotra Interior Point"}];
}

function renderAnalysis() {
  const a = state.analysis; if (!a) return;
  const methods = modelMethods(a.problem_type).map(method => `<option value="${method.v}">${method.t}</option>`).join("");
  $("#analysisArea").innerHTML = `
    <div class="analysis-layout">
      <article class="panel analysis-card"><div class="analysis-title"><div><p class="eyebrow">MODEL ANALYSIS</p><h3>${escapeHtml(a.name)}</h3><p class="file-meta">${escapeHtml(a.file_name)} · ${a.format} · ${fileSize(a.file_size)}</p>${a.conversion ? `<p class="file-meta pass">${escapeHtml(a.conversion)}</p>` : ""}</div><span class="type-badge">${a.problem_type}</span></div>
        <div class="stat-grid">
          ${stat("VARIABLES", number(a.variables,0))}${stat("CONSTRAINTS",number(a.constraints,0))}${stat("NON-ZEROS",number(a.nonzeros,0))}${stat("SPARSITY",`${number(a.sparsity,2)}%`)}
          ${stat("CONTINUOUS",number(a.types.continuous,0))}${stat("DISCRETE",number(a.types.integer+a.types.binary,0))}${stat("BOUNDS",number(a.bounded_variables,0))}${stat("OBJECTIVE",escapeHtml(a.objective_sense.toUpperCase()))}
        </div>
      </article>
      <article class="panel analysis-card"><div class="analysis-title"><div><p class="eyebrow">AUTOMATIC OPTIMIZATION</p><h3 id="autoStatus">Preparing automatic pipeline</h3></div><span class="type-badge">AUTO</span></div>
        <div id="autoDecision" class="auto-decision"><div class="decision-row done"><span>MODEL DETECTED</span><b>${escapeHtml(a.problem_type)}</b></div><div class="decision-row done"><span>PROBLEM CLASSIFIED</span><b>${escapeHtml(a.classification_reason)}</b></div><div class="decision-row running"><span>PRESOLVE · SOLVER · COMPUTE</span><b>Automatic selection in progress</b></div></div>
        <details class="advanced expert-mode"><summary>ADVANCED / EXPERT MODE</summary><div class="config-list"><div class="config-item"><label>SOLVER METHOD</label><select id="methodSelect" class="select">${methods}</select></div><div class="config-item"><label>COMPUTE BACKEND</label><select id="backendSelect" class="select"><option value="auto">AUTO</option><option value="cpu">CPU</option><option value="cuda" ${state.device?.cuda_available ? "" : "disabled"}>CUDA${state.device?.cuda_available ? "" : " · unavailable"}</option></select></div><div class="toggle-row"><span>PRESOLVE</span><label class="switch"><input id="presolveToggle" type="checkbox" checked><i class="slider"></i></label></div><div class="config-item"><label>MAX ITERATIONS <span class="muted">(0 = unlimited)</span></label><input id="iterationInput" class="control" type="number" min="0" max="10000000" value="10000"></div><div class="config-item"><label>TIME LIMIT (SECONDS)</label><input id="timeInput" class="control" type="number" min="1" max="3600" value="60"></div><button id="expertRunButton" class="run-button">RUN WITH EXPERT SETTINGS <span>→</span></button></div></details>
      </article>
    </div>
    <article class="panel matrix-card"><div class="matrix-head"><h3>CONSTRAINT MATRIX SPARSITY</h3><span>${a.matrix.aggregated ? "Visualization aggregated for display" : "Exact display sampling"}</span></div><div class="matrix-viewport"><canvas id="matrixCanvas" aria-label="Constraint matrix sparsity plot"></canvas></div></article>
    <div id="resultArea"></div>`;
  $("#expertRunButton").addEventListener("click", runExpertSolve); renderMatrix();
}
function stat(label,value){return `<div class="stat"><p>${label}</p><strong>${value}</strong></div>`}

function renderMatrix() {
  const canvas = $("#matrixCanvas"), box = canvas.getBoundingClientRect(), a = state.analysis;
  canvas.width = Math.max(1, Math.floor(box.width * devicePixelRatio)); canvas.height = Math.max(1, Math.floor(box.height * devicePixelRatio));
  const ctx = canvas.getContext("2d"); ctx.scale(devicePixelRatio, devicePixelRatio); const width = box.width, height = box.height;
  ctx.fillStyle="#06111d"; ctx.fillRect(0,0,width,height); ctx.fillStyle="rgba(127,174,204,.08)"; for(let x=0;x<width;x+=30)ctx.fillRect(x,0,1,height); for(let y=0;y<height;y+=30)ctx.fillRect(0,y,width,1);
  ctx.fillStyle="#65d8bd"; const radius = a.nonzeros > 800 ? 1 : 1.45; a.matrix.points.forEach(([row,col]) => { const x=(col+.5)/Math.max(1,a.matrix.columns)*width; const y=(row+.5)/Math.max(1,a.matrix.rows)*height; ctx.fillRect(x-radius,y-radius,radius*2,radius*2); });
}

async function startAutomaticSolve() {
  renderRunning();
  try { const result = await api("/api/solve/auto", {method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify({job_id:state.jobId})}); state.result=result; renderAutomaticDecision(result); renderResult(result); }
  catch(error) { renderFailure(error.message); toast(error.message); const status=$("#autoStatus"); if(status) status.textContent="Automatic pipeline failed"; }
}

async function runExpertSolve() {
  const button = $("#expertRunButton"); button.disabled = true; button.textContent = "EXPERT SOLVER RUNNING…"; renderRunning();
  const request = {job_id:state.jobId, method:$("#methodSelect").value, backend:$("#backendSelect").value, presolve:$("#presolveToggle").checked, max_iterations:Number($("#iterationInput").value), time_limit_seconds:Number($("#timeInput").value)};
  try { const result = await api("/api/solve", {method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(request)}); state.result=result; renderResult(result); }
  catch(error) { renderFailure(error.message); toast(error.message); }
  finally { button.disabled=false; button.innerHTML="RUN WITH EXPERT SETTINGS <span>→</span>"; }
}

function renderRunning() {
  $("#resultArea").innerHTML = `<div class="result-area"><article class="panel pipeline">${["UPLOADED","PARSING","VALIDATING","CLASSIFYING","PRESOLVING","SELECTING","SOLVING","POSTSOLVING","VERIFYING","COMPLETE"].map((stage,index)=>`<span class="stage ${index===4?"running":index<4?"done":""}">${stage}</span>`).join("")}</article><article class="panel loading-panel">Automatic pipeline is running. The solver does not expose internal progress events, so this view remains indeterminate until it returns a real result.</article></div>`;
}

function renderAutomaticDecision(result) {
  const automation = result.automation; if (!automation || automation.mode !== "automatic") return;
  const status = $("#autoStatus"), host = $("#autoDecision"), selection = automation.selection || {};
  if (status) status.textContent = result.verification === "PASS" ? "Automatic pipeline completed" : "Automatic pipeline completed with a warning";
  if (!host) return;
  const pre = result.presolve || {}, attempts = automation.attempts || [];
  host.innerHTML = `<div class="decision-row done"><span>MODEL DETECTED</span><b>${escapeHtml(result.analysis.problem_type)}</b></div><div class="decision-row done"><span>PRESOLVE</span><b>${pre.before_variables ?? "—"} vars / ${pre.before_constraints ?? "—"} rows → ${pre.after_variables ?? "—"} vars / ${pre.after_constraints ?? "—"} rows</b></div><div class="decision-row done"><span>SOLVER SELECTED</span><b>${escapeHtml(automation.final_method || selection.method || "—")}</b></div><div class="decision-row done"><span>COMPUTE BACKEND</span><b>${escapeHtml(automation.final_backend || selection.backend || "—")}</b></div><div class="decision-row ${result.verification === "PASS" ? "done" : "warning"}"><span>ORIGINAL-MODEL VERIFICATION</span><b>${escapeHtml(result.verification)}</b></div><details class="selection-why"><summary>WHY WAS THIS METHOD SELECTED?</summary><p>${escapeHtml(selection.reason || "Expert settings were supplied.")}</p>${attempts.length > 1 ? `<p>Attempts: ${attempts.map(attempt => `${escapeHtml(attempt.method)} (${escapeHtml(attempt.status)})`).join(" → ")}</p>` : ""}</details>`;
}

function renderFailure(message) { $("#resultArea").innerHTML=`<div class="result-area"><article class="panel result-top"><div class="result-status"><span class="status-orb fail">!</span><div><h2>REQUEST FAILED</h2><p>${escapeHtml(message)}</p></div></div></article></div>`; }

function humanStatus(status) { return (status || "FAILED").replaceAll("_", " "); }
function statusExplanation(result) {
  const status=(result.status||"").toUpperCase(); if(status==="OPTIMAL") return "An optimal solution was found and independently verified against the original model.";
  if(status==="FEASIBLE") return "A feasible solution was returned. Review the reported bounds and verification details below.";
  if(status.includes("TIME")) return "The solver reached the configured time limit before proving optimality.";
  if(status.includes("ITERATION")) return `The solver completed ${number(result.metrics.iterations,0)} iterations without establishing an optimal solution.`;
  if(status.includes("UNSUPPORTED")) return "The solver rejected this model or method because the requested path is not implemented.";
  return result.metrics.message || "The solver did not return a verified optimal solution.";
}

function renderResult(result) {
  const good = ["OPTIMAL","FEASIBLE"].includes((result.status||"").toUpperCase()) && result.verification === "PASS";
  const warning = !good && /TIME|ITERATION|UNSUPPORTED/.test(result.status || "");
  const cls=good?"":warning?"warn":"fail", symbol=good?"✓":warning?"!":"×";
  const verificationFailed = ["OPTIMAL", "FEASIBLE"].includes((result.status || "").toUpperCase()) && result.verification !== "PASS";
  const headline = verificationFailed ? "VERIFICATION FAILED" : humanStatus(result.status);
  const headlineClass = verificationFailed ? "bad" : statusClass(result.status);
  const timings = result.timings, metrics = result.metrics, pre=result.presolve;
  const timingRows = [["Parsing",timings.parse_time_ms],["Presolve",timings.presolve_time_ms],["Solving",timings.solver_time_ms],["Postsolve",timings.postsolve_time_ms],["Verification",timings.verification_time_ms]].filter(row=>row[1]!==null && row[1]!==undefined);
  const largest=Math.max(...timingRows.map(row=>row[1]),1);
  const reduction = pre.before_constraints && pre.after_constraints !== null ? Math.max(0,(1-pre.after_constraints/pre.before_constraints)*100) : null;
  $("#resultArea").innerHTML = `<div class="result-area">
    <article class="panel pipeline">${["UPLOADED","PARSING","VALIDATING","CLASSIFYING","PRESOLVING","SELECTING","SOLVING","POSTSOLVING","VERIFYING","COMPLETE"].map(stage=>`<span class="stage done">${stage}</span>`).join("")}</article>
    <article class="panel result-top"><div class="result-status"><span class="status-orb ${cls}">${symbol}</span><div><h2 class="${headlineClass}">${escapeHtml(headline)}</h2><p>${escapeHtml(statusExplanation(result))}</p></div></div><div class="result-kpis"><div><label>OBJECTIVE</label><strong>${number(metrics.objective,6)}</strong></div><div><label>VERIFICATION</label><strong class="${result.verification === "PASS" ? "pass" : "warning"}">${escapeHtml(result.verification)}</strong></div><div><label>SOLVER TIME</label><strong>${milliseconds(timings.solver_time_ms)}</strong></div></div></article>
    <div class="result-grid"><article class="panel timing-card"><h3>PERFORMANCE BREAKDOWN</h3>${timingRows.map(([label,value])=>`<div class="timing-row"><span>${label}</span><div class="timing-bar"><i style="width:${Math.max(3,value/largest*100)}%"></i></div><b>${milliseconds(value)}</b></div>`).join("")}<p class="solver-highlight">SOLVER TIME is measured inside the solver process. Backend total (${milliseconds(timings.backend_total_time_ms)}) is shown separately and includes the adapter process boundary.</p></article>
    <article class="panel verify-card"><h3>INDEPENDENT SOLUTION VERIFICATION</h3><div class="verify-stack"><div class="verify-row"><span>Original model verification</span><b class="${result.verification === "PASS"?"pass":"warning"}">${escapeHtml(result.verification)}</b></div>${verificationRow("Maximum constraint violation",metrics.feasibility)}${verificationRow("Primal residual",metrics.primal_residual)}${verificationRow("Dual residual",metrics.dual_residual)}${verificationRow("Integrality",result.analysis.problem_type==="MILP" ? result.verification : null)}${verificationRow("Objective consistency",metrics.objective !== null ? result.verification : null)}</div></article></div>
    <article class="panel presolve-card"><h3>PRESOLVE TRANSFORMATION</h3><div class="presolve-flow"><div class="presolve-node"><p>BEFORE PRESOLVE</p><strong>${number(pre.before_variables,0)} vars · ${number(pre.before_constraints,0)} rows</strong></div><span class="presolve-arrow">→</span><div class="presolve-node"><p>AFTER PRESOLVE</p><strong>${number(pre.after_variables,0)} vars · ${number(pre.after_constraints,0)} rows</strong></div><span class="presolve-arrow">→</span><div class="presolve-node"><p>MEASURED REDUCTIONS</p><strong>${number(pre.reductions,0)}${reduction !== null ? ` · ${number(reduction,1)}% rows` : ""}</strong></div></div></article>
    ${milpPanel(metrics,result.analysis.problem_type)}
    <article class="panel table-card"><div class="table-toolbar explorer-heading"><div><p class="eyebrow">SOLUTION VARIABLES</p><h3>VARIABLE EXPLORER</h3><p id="variableStatus" class="file-meta"></p></div><div class="export-actions"><a class="export-button" href="/api/results/${encodeURIComponent(result.job_id)}/export?format=csv">EXPORT CSV</a><a class="export-button" href="/api/results/${encodeURIComponent(result.job_id)}/export?format=json">EXPORT JSON</a></div></div><div id="variableSummary" class="explorer-summary"></div><div id="keyVariables"></div><div class="explorer-controls"><div class="filter-chips"><button data-variable-filter="nonzero">NON-ZERO</button><button data-variable-filter="all">ALL</button><button data-variable-filter="integer">INTEGER</button><button data-variable-filter="binary">BINARY</button><button data-variable-filter="at-bound">AT BOUND</button><button data-variable-filter="fractional">FRACTIONAL</button></div><div class="explorer-selects"><select id="variableSort" class="select"><option value="absolute">Absolute value</option><option value="value">Value</option><option value="name">Variable name</option><option value="type">Type</option><option value="lower">Lower bound</option><option value="upper">Upper bound</option></select><select id="variableDirection" class="select"><option value="desc">Descending</option><option value="asc">Ascending</option></select><select id="variablePageSize" class="select"><option value="25">25 rows</option><option value="50">50 rows</option><option value="100">100 rows</option></select><input id="variableSearch" class="control search" placeholder="Search all variables" /></div></div><div class="benchmark-table-wrap"><table class="data-table"><thead><tr><th>VARIABLE</th><th>VALUE</th><th>TYPE</th><th>LOWER</th><th>UPPER</th><th>AT BOUND</th></tr></thead><tbody id="variableRows"></tbody></table></div><div id="variablePagination" class="variable-pagination"></div></article>
    <details class="panel log-card"><summary>TECHNICAL LOG</summary><pre>${escapeHtml(result.raw_log || "No solver log was returned.")}</pre></details>
  </div>`;
  initializeVariableExplorer(result);
}

function verificationRow(label,value){if(value===null||value===undefined)return "";const text=typeof value === "number" ? number(value,8) : value;return `<div class="verify-row"><span>${label}</span><b class="${String(value).toUpperCase()==="PASS"?"pass":""}">${escapeHtml(text)}</b></div>`}
function milpPanel(metrics,type){if(type!=="MILP") return "";return `<article class="panel presolve-card"><h3>MILP SEARCH STATE</h3><div class="stat-grid">${stat("NODES CREATED",number(metrics.nodes_created,0))}${stat("NODES PROCESSED",number(metrics.nodes_processed,0))}${stat("NODES PRUNED",number(metrics.nodes_pruned,0))}${stat("RELATIVE GAP",number(metrics.relative_gap,6))}</div></article>`}
const explorerState = { values: [], summary: {}, filter: "nonzero", sort: "absolute", direction: "desc", page: 1, pageSize: 25, query: "" };
function initializeVariableExplorer(result) {
  explorerState.values = result.variables || [];
  explorerState.summary = result.variable_summary || {};
  explorerState.filter = "nonzero"; explorerState.sort = "absolute"; explorerState.direction = "desc"; explorerState.page = 1; explorerState.pageSize = 25; explorerState.query = "";
  $$('[data-variable-filter]').forEach(button => button.addEventListener("click", () => { explorerState.filter = button.dataset.variableFilter; explorerState.page = 1; renderVariableExplorer(); }));
  $("#variableSearch").addEventListener("input", event => { explorerState.query = event.target.value.trim(); explorerState.page = 1; renderVariableExplorer(); });
  $("#variableSort").addEventListener("change", event => { explorerState.sort = event.target.value; explorerState.page = 1; renderVariableExplorer(); });
  $("#variableDirection").addEventListener("change", event => { explorerState.direction = event.target.value; explorerState.page = 1; renderVariableExplorer(); });
  $("#variablePageSize").addEventListener("change", event => { explorerState.pageSize = Number(event.target.value); explorerState.page = 1; renderVariableExplorer(); });
  renderVariableExplorer();
}
function isNumericValue(value) { return typeof value === "number" && Number.isFinite(value); }
function explorerRows() {
  const tolerance = Number(explorerState.summary.zero_tolerance || 1e-8);
  const query = explorerState.query.toLowerCase();
  let rows = explorerState.values.filter(row => query ? row.name.toLowerCase().includes(query) : explorerState.filter === "all" ? true : explorerState.filter === "nonzero" ? isNumericValue(row.value) && Math.abs(row.value) > tolerance : explorerState.filter === "integer" ? row.type === "integer" : explorerState.filter === "binary" ? row.type === "binary" : explorerState.filter === "at-bound" ? row.at_bound === true : row.fractional === true);
  const numeric = key => row => isNumericValue(row[key]) ? row[key] : null;
  const selectors = { value: numeric("value"), absolute: row => isNumericValue(row.value) ? Math.abs(row.value) : null, lower: numeric("lower"), upper: numeric("upper"), name: row => row.name, type: row => row.type || "" };
  const selector = selectors[explorerState.sort];
  rows = [...rows].sort((left, right) => { const a = selector(left), b = selector(right); let order; if (a === null) order = b === null ? 0 : 1; else if (b === null) order = -1; else order = typeof a === "string" ? a.localeCompare(b, undefined, {numeric:true}) : a - b; return explorerState.direction === "asc" ? order : -order; });
  return rows;
}
function explorerSummary(summary) { return [["TOTAL VARIABLES", summary.total], ["NON-ZERO", summary.nonzero], ["ZERO", summary.zero], ["INTEGER", summary.integer], ["BINARY", summary.binary]].map(([label, value]) => `<div class="explorer-stat"><p>${label}</p><strong>${number(value,0)}</strong></div>`).join(""); }
function renderVariableExplorer() {
  const summary = explorerState.summary, rows = explorerRows(), totalPages = Math.max(1, Math.ceil(rows.length / explorerState.pageSize));
  explorerState.page = Math.min(explorerState.page, totalPages);
  const slice = rows.slice((explorerState.page - 1) * explorerState.pageSize, explorerState.page * explorerState.pageSize);
  $("#variableSummary").innerHTML = explorerSummary(summary);
  $("#variableStatus").textContent = explorerState.query ? `Search spans all ${number(summary.total,0)} solution variables · ${number(rows.length,0)} match${rows.length === 1 ? "" : "es"}` : `Showing ${number(rows.length,0)} ${explorerState.filter === "nonzero" ? "non-zero" : explorerState.filter.replace("-", " ")} variables · tolerance ${summary.zero_tolerance ?? "—"}`;
  $$('[data-variable-filter]').forEach(button => button.classList.toggle("active", button.dataset.variableFilter === explorerState.filter));
  const key = [...explorerState.values].filter(row => isNumericValue(row.value) && Math.abs(row.value) > Number(summary.zero_tolerance || 1e-8)).sort((a,b) => Math.abs(b.value) - Math.abs(a.value)).slice(0, 8);
  $("#keyVariables").innerHTML = key.length ? `<div class="key-variables"><p>KEY SOLUTION VARIABLES</p>${key.map(row => `<span><b>${escapeHtml(row.name)}</b><em>${number(row.value,8)}</em></span>`).join("")}</div>` : "";
  const allZero = summary.available > 0 && summary.nonzero === 0;
  $("#variableRows").innerHTML = slice.length ? slice.map(row => `<tr><td>${escapeHtml(row.name)}</td><td>${number(row.value,8)}</td><td>${escapeHtml(row.type || "—")}</td><td>${number(row.lower,5)}</td><td>${number(row.upper,5)}</td><td>${row.at_bound === true ? "YES" : row.value === null ? "—" : "NO"}</td></tr>`).join("") : `<tr><td colspan="6">${allZero && explorerState.filter === "nonzero" ? "All solution variables are zero. <button class=\"inline-explorer-button\" data-view-all>VIEW ALL VARIABLES</button>" : "No variables match this filter."}</td></tr>`;
  $("#variablePagination").innerHTML = `<span>${number(rows.length,0)} result${rows.length === 1 ? "" : "s"} · Page ${explorerState.page} of ${totalPages}</span><div><button ${explorerState.page === 1 ? "disabled" : ""} data-page="previous">PREVIOUS</button><button ${explorerState.page === totalPages ? "disabled" : ""} data-page="next">NEXT</button></div>`;
  $$('[data-page]').forEach(button => button.addEventListener("click", () => { explorerState.page += button.dataset.page === "next" ? 1 : -1; renderVariableExplorer(); }));
  $$('[data-view-all]').forEach(button => button.addEventListener("click", () => { explorerState.filter = "all"; explorerState.page = 1; renderVariableExplorer(); }));
}

function renderBenchmarks() {
  const host=$("#benchmarkArea"), datasets=state.benchmarkData;if(!datasets.length){host.innerHTML="No local benchmark reports exist yet. Run the repository benchmark runner to populate this page with measured results.";return;}
  const choose=(name)=>{const active=datasets.find(data=>data.dataset===name)||datasets[0];const rows=active.instances||[];const verified=rows.filter(row=>["OPTIMAL","FEASIBLE"].includes(row.status)&&row.verification_pass===true).length;const failures=rows.filter(row=>!["OPTIMAL","FEASIBLE"].includes(row.status)).length;host.innerHTML=`<div class="benchmark-tabs">${datasets.map(data=>`<button class="${data===active?"active":""}" data-dataset="${escapeHtml(data.dataset)}">${escapeHtml(data.dataset.toUpperCase())}</button>`).join("")}</div><div class="benchmark-summary"><div class="bench-stat"><p>INSTANCES</p><strong>${rows.length}</strong></div><div class="bench-stat"><p>VERIFIED SUCCESS</p><strong class="pass">${verified}/${rows.length}</strong></div><div class="bench-stat"><p>NON-SUCCESS</p><strong class="${failures?"warning":"pass"}">${failures}</strong></div></div><div class="benchmark-table-wrap"><table class="data-table"><thead><tr><th>INSTANCE</th><th>STATUS</th><th>VERIFY</th><th>ITERATIONS / NODES</th><th>SOLVER TIME</th></tr></thead><tbody>${rows.map(row=>`<tr><td>${escapeHtml((row.instance||"—").split(/[\\/]/).pop())}</td><td class="${statusClass(row.status)}">${escapeHtml(row.status||"—")}</td><td class="${row.verification_pass?"pass":"warning"}">${row.verification_pass===true?"PASS":row.verification_pass===false?"FAIL":"N/A"}</td><td>${number(row.iterations ?? row.nodes_processed,0)}</td><td>${milliseconds(row.solve_time_ms)}</td></tr>`).join("")}</tbody></table></div>`;$$("[data-dataset]",host).forEach(button=>button.addEventListener("click",()=>choose(button.dataset.dataset)));};choose(datasets[0].dataset);
}

window.addEventListener("resize",()=>state.analysis&&renderMatrix());
bindUpload(); boot();
