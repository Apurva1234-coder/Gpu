const state = { jobId: null, analysis: null, device: null, capabilities: null, benchmarkData: [], benchmarkAvailability: {}, benchmarkComparison: null, benchmarkSelectedInstance: null, result: null, runningTimer: null, runningStarted: null, runningConfiguration: null, demoDatasets: [], demoLoadedTypes: [], demoType: "LP" };
const $ = (selector, root = document) => root.querySelector(selector);
const $$ = (selector, root = document) => [...root.querySelectorAll(selector)];
const nf = new Intl.NumberFormat("en-US", { maximumFractionDigits: 2 });

function escapeHtml(value = "") { return String(value).replace(/[&<>'"]/g, c => ({"&":"&amp;","<":"&lt;",">":"&gt;","'":"&#39;","\"":"&quot;"}[c])); }
function number(value, digits = 2) { return value === null || value === undefined || Number.isNaN(Number(value)) ? "—" : new Intl.NumberFormat("en-US", {maximumFractionDigits: digits}).format(Number(value)); }
function duration(value) { if (value === null || value === undefined || !Number.isFinite(Number(value))) return "—"; const ms=Number(value); return ms >= 1000 ? `${number(ms/1000, 2)} s` : `${number(ms, ms < 1 ? 4 : 2)} ms`; }
function seconds(value) { return value === null || value === undefined ? "N/A" : `${number(value, 2)} s`; }
function timeLimitLabel(value) { const limit=Number(value); return Number.isFinite(limit) && limit > 0 ? `${number(limit,0)} seconds` : "No time limit"; }
function iterationLimitLabel(value) { return Number(value) === 0 ? "Unlimited" : number(value,0); }
function fileSize(bytes) { return bytes < 1024 * 1024 ? `${number(bytes / 1024, 1)} KB` : `${number(bytes / 1024 / 1024, 2)} MB`; }
function statusClass(status) { const text = (status || "").toUpperCase(); return text.includes("OPTIMAL") || text === "FEASIBLE" ? "pass" : text.includes("TIME") || text.includes("ITERATION") || text.includes("NODE_LIMIT") || text.includes("MEMORY_LIMIT") || text.includes("UNSUPPORTED") ? "warning" : "bad"; }
function toast(message) { const element = $("#toast"); element.textContent = message; element.classList.remove("hidden"); setTimeout(() => element.classList.add("hidden"), 4200); }
async function api(path, options = {}) { const response = await fetch(path, options); const data = await response.json().catch(() => ({})); if (!response.ok) throw new Error(data.detail || `Request failed (${response.status})`); return data; }
function demoDatasetError(dataset) {
  const message = String(dataset.error || "the model could not be parsed");
  if (/off-diagonal quadratic objective/i.test(message)) return "This QPLIB model has cross-variable terms in its quadratic objective. The current QP solver supports diagonal objectives only; solving it requires a full-Hessian QP method.";
  return `Statistics unavailable: ${message}`;
}

async function boot() {
  loadBuiltinDatasets();
  try {
    const [device, capabilities, benchmark] = await Promise.all([api("/api/device"), api("/api/capabilities"), api("/api/benchmarks")]);
    state.device = device; state.capabilities = capabilities; state.benchmarkData = benchmark.datasets || []; state.benchmarkAvailability = benchmark.comparators || {};
    renderDevice(); renderHardware(); renderBenchmarks();
  } catch (error) { toast(`Service unavailable: ${error.message}`); $("#deviceStatus").textContent = "Adapter unavailable"; }
}

async function loadBuiltinDatasets(problemType = state.demoType) {
  try {
    const kind = problemType.toUpperCase();
    const response = await api(`/api/datasets?problem_type=${encodeURIComponent(kind)}`);
    state.demoDatasets = [...state.demoDatasets.filter(dataset => dataset.problem_type !== kind), ...(response.datasets || [])];
    if (!state.demoLoadedTypes.includes(kind)) state.demoLoadedTypes.push(kind);
    if (state.demoType === kind) renderBuiltinDatasets();
  } catch (error) {
    const cards = $("#demoDatasetCards");
    if (cards) cards.innerHTML = `<div class="demo-empty">Could not load built-in datasets: ${escapeHtml(error.message)}</div>`;
  }
}

function renderBuiltinDatasets() {
  const tabs = $("#demoTypeTabs"), cards = $("#demoDatasetCards");
  if (!tabs || !cards) return;
  const types = ["LP", "MILP", "QP"];
  tabs.innerHTML = types.map(type => `<button type="button" role="tab" aria-selected="${state.demoType === type}" class="${state.demoType === type ? "active" : ""}" data-demo-type="${type}">${type}</button>`).join("");
  $$('[data-demo-type]', tabs).forEach(button => button.addEventListener("click", () => {
    state.demoType = button.dataset.demoType;
    if (state.demoLoadedTypes.includes(state.demoType)) renderBuiltinDatasets();
    else {
      $("#demoDatasetCards").innerHTML = `<div class="demo-empty">Loading ${escapeHtml(state.demoType)} datasets and calculating model statistics…</div>`;
      renderBuiltinDatasets();
      loadBuiltinDatasets(state.demoType);
    }
  }));
  if (!state.demoLoadedTypes.includes(state.demoType)) {
    cards.innerHTML = `<div class="demo-empty">Loading ${escapeHtml(state.demoType)} datasets and calculating model statistics…</div>`;
    return;
  }
  const entries = state.demoDatasets.filter(dataset => dataset.problem_type === state.demoType);
  if (!entries.length) {
    cards.innerHTML = `<div class="demo-empty">No ${state.demoType} datasets are installed.</div>`;
    return;
  }
  const sizeLabel = {small: "SMALL", medium: "MEDIUM", large: "LARGE"};
  const descriptionsByType = {
    LP: {small: "Quick LP demonstration", medium: "Medium-scale LP", large: "Large sparse LP benchmark"},
    MILP: {small: "Small-scale MIPLIB benchmark", medium: "Medium-scale MIPLIB benchmark", large: "Large-scale MIPLIB benchmark"},
    QP: {small: "Small-scale QPLIB benchmark", medium: "Medium-scale QPLIB benchmark", large: "Large-scale QPLIB benchmark"},
  };
  const descriptions = descriptionsByType[state.demoType] || {};
  cards.innerHTML = entries.map(dataset => {
    const a = dataset.analysis;
    const stats = a ? `<div class="demo-stats"><div><span>VARIABLES</span><b>${number(a.variables, 0)}</b></div><div><span>CONSTRAINTS</span><b>${number(a.constraints, 0)}</b></div><div><span>NONZEROS</span><b>${number(a.nonzeros, 0)}</b></div><div><span>SPARSITY</span><b>${number(a.sparsity, 2)}%</b></div></div>` : `<p class="demo-error">${escapeHtml(demoDatasetError(dataset))}</p>`;
    const size = sizeLabel[dataset.size] || dataset.size.toUpperCase();
    return `<article class="demo-dataset-card"><div class="demo-card-top"><span>${escapeHtml(size)}</span><span class="demo-type">${escapeHtml(a?.problem_type || dataset.problem_type)}</span></div><h4>${escapeHtml(dataset.name)}</h4><p class="demo-description">${escapeHtml(descriptions[dataset.size] || "Optimization benchmark")}</p><p class="demo-file">${escapeHtml(dataset.file_name)}</p>${stats}<button type="button" class="demo-run-button" data-demo-run="${escapeHtml(dataset.id)}" ${dataset.available ? "" : "disabled"}>${dataset.available ? "SOLVE DATASET  →" : "UNAVAILABLE"}</button></article>`;
  }).join("");
  $$('[data-demo-run]', cards).forEach(button => button.addEventListener("click", () => runBuiltinDataset(button.dataset.demoRun, button)));
}

async function runBuiltinDataset(datasetId, button) {
  button.disabled = true;
  button.textContent = "PREPARING MODEL…";
  try {
    const segments = datasetId.split("/").map(encodeURIComponent);
    const response = await api(`/api/datasets/${segments.join("/")}/analyze`, {method: "POST"});
    await applyAnalysis(response);
    $("#analysisArea").scrollIntoView({behavior: "smooth", block: "start"});
  } catch (error) {
    toast(error.message);
    button.disabled = false;
    button.textContent = "SOLVE DATASET  →";
  }
}

function renderDevice() {
  const device = state.device;
  if (!device) return;
  $("#deviceDot").classList.add("online");
  $("#deviceStatus").textContent = device.cuda_available ? "CUDA available" : "CPU fallback active";
  $("#heroBackend").textContent = device.cuda_available ? "CPU + CUDA kernels" : "CPU";
  $("#heroDevice").textContent = device.cuda_available ? (device.gpu_name || `${device.gpu_count} CUDA device${device.gpu_count === 1 ? "" : "s"}`) : device.cuda_reason;
  $("#engineState").textContent = "READY";
}

function renderHardware() {
  const device = state.device; if (!device) return;
  const cuda = device.cuda_available;
  $("#hardwareArea").innerHTML = `
    <article class="panel hardware-card"><p>CPU</p><strong>${escapeHtml(device.cpu)}</strong><small>${number(device.threads, 0)} logical threads detected</small></article>
    <article class="panel hardware-card"><p>CUDA ACCELERATION</p><strong class="${cuda ? "pass" : "warning"}">${cuda ? "AVAILABLE" : "CPU FALLBACK ACTIVE"}</strong><small>${escapeHtml(cuda ? (device.gpu_name || `${device.gpu_count} CUDA device(s)`) : device.cuda_reason)}</small></article>
    <article class="panel hardware-card"><p>SOLVER BACKENDS</p><strong>CPU <span class="pass">AVAILABLE</span></strong><small>${escapeHtml(device.cuda_reason)}</small></article>
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
  try { const response = await api(`/api/examples/${name}`, { method: "POST" }); await applyAnalysis(response); $("#analysisArea").scrollIntoView({behavior:"smooth", block:"start"}); } catch (error) { toast(error.message); }
}

async function analyzeFile(file) {
  if (!/\.(mps|json|txt|qplib)$/i.test(file.name)) return toast("Use a supported MPS, JSON, TXT, or QPLIB model.");
  const form = new FormData(); form.append("file", file);
  $("#dropZone").classList.add("dragging");
  try { const response = await api("/api/analyze", { method: "POST", body: form }); await applyAnalysis(response); } catch (error) { toast(error.message); } finally { $("#dropZone").classList.remove("dragging"); }
}

async function applyAnalysis(response) {
  state.jobId = response.job_id;
  state.analysis = response.analysis;
  state.result = null;
  state.policy = (await api(`/api/policy/${encodeURIComponent(state.jobId)}`)).selection;
  const area = $("#analysisArea");
  area.classList.remove("hidden");
  renderAnalysis();
  startAutomaticSolve();
}

function modelMethods(type) {
  if (type === "QP") return [{v:"qp", t:"Newton / Barrier"}];
  if (type === "MILP") return [{v:"milp",t:"Branch-and-Bound"},{v:"cutting-plane",t:"Cutting Planes"},{v:"feasibility-pump",t:"Feasibility Pump"},{v:"lp-relaxation",t:"LP Relaxation"}];
  return [{v:"auto",t:"Auto (policy selected)"},{v:"revised-simplex",t:"Revised Simplex"},{v:"dual-simplex",t:"Dual Simplex"},{v:"ipm",t:"Mehrotra Interior Point"},{v:"pdhg",t:"PDHG (large sparse LP)"}];
}

function renderAnalysis() {
  const a = state.analysis; if (!a) return;
  const policy = state.policy || {};
  const methods = modelMethods(a.problem_type).map(method => `<option value="${method.v}">${method.t}</option>`).join("");
  $("#analysisArea").innerHTML = `
    <div class="analysis-layout">
      <article class="panel analysis-card"><div class="analysis-title"><div><p class="eyebrow">MODEL ANALYSIS</p><h3>${escapeHtml(a.name)}</h3><p class="file-meta">${escapeHtml(a.file_name)} · ${a.format} · ${fileSize(a.file_size)}</p>${a.conversion ? `<p class="file-meta pass">${escapeHtml(a.conversion)}</p>` : ""}</div><span class="type-badge">${a.problem_type}</span></div>
        <div class="stat-grid">
          ${stat("VARIABLES", number(a.variables,0))}${stat("CONSTRAINTS",number(a.constraints,0))}${stat("NON-ZEROS",number(a.nonzeros,0))}${a.problem_type === "QP" ? stat("HESSIAN NON-ZEROS",number(a.quadratic_nonzeros,0)) : ""}${stat("SPARSITY",`${number(a.sparsity,2)}%`)}
          ${stat("CONTINUOUS",number(a.types.continuous,0))}${stat("DISCRETE",number(a.types.integer+a.types.binary,0))}${stat("BOUNDS",number(a.bounded_variables,0))}${stat("OBJECTIVE",escapeHtml(a.objective_sense.toUpperCase()))}
        </div>
      </article>
      <article class="panel analysis-card"><div class="analysis-title"><div><p class="eyebrow">AUTOMATIC EXECUTION POLICY</p><h3 id="autoStatus">Policy selected · optimization starting</h3></div><span class="type-badge">${escapeHtml(policy.model_size || "AUTO")}</span></div>
        <div id="autoDecision" class="auto-decision"><div class="decision-row done"><span>MODEL SIZE</span><b>${escapeHtml(policy.model_size || "—")} · score ${number(policy.complexity_score, 0)}</b></div><div class="decision-row done"><span>TIME LIMIT</span><b>${timeLimitLabel(policy.time_limit_seconds)}</b></div>${a.problem_type === "MILP" ? `<div class="decision-row done"><span>MAX B&B NODES</span><b>${number(policy.max_nodes, 0)}</b></div><div class="decision-row done"><span>MAX LP ITERATIONS</span><b>${iterationLimitLabel(policy.max_iterations)}</b></div>` : `<div class="decision-row done"><span>MAXIMUM ITERATIONS</span><b>${iterationLimitLabel(policy.max_iterations)}</b></div>`}<div class="decision-row done"><span>SOLVER · BACKEND</span><b>${escapeHtml(policy.method || "—")} · ${escapeHtml(policy.backend || "—")}</b></div><details class="selection-why"><summary>WHY?</summary><p>${escapeHtml(policy.reason || "Automatic policy is being prepared.")}</p><p>${escapeHtml(policy.backend_reason || "")}</p></details></div>
        <details class="advanced expert-mode"><summary>ADVANCED / EXPERT MODE</summary><div class="config-list"><div class="config-item"><label>SOLVER METHOD</label><select id="methodSelect" class="select">${methods}</select></div><div class="config-item"><label>COMPUTE BACKEND</label><select id="backendSelect" class="select"><option value="auto">AUTO</option><option value="cpu">CPU</option><option value="cuda" ${state.device?.cuda_available ? "" : "disabled"}>CUDA${state.device?.cuda_available ? "" : " · unavailable"}</option></select></div><div class="toggle-row"><span>PRESOLVE</span><label class="switch"><input id="presolveToggle" type="checkbox" checked><i class="slider"></i></label></div>${a.problem_type === "MILP" ? `<div class="config-item"><label>MAX B&B NODES <span class="muted">(0 = unlimited)</span></label><input id="nodeInput" class="control" type="number" min="0" max="10000000" value="${policy.max_nodes || 10000}"></div><div class="config-item"><label>MAX LP ITERATIONS <span class="muted">(0 = unlimited)</span></label><input id="iterationInput" class="control" type="number" min="0" max="10000000" value="${policy.max_iterations || 10000}"></div><p class="file-meta">The node limit controls search breadth. The LP iteration limit applies separately to each node relaxation.</p>` : `<div class="config-item"><label>MAX ITERATIONS <span class="muted">(0 = unlimited)</span></label><input id="iterationInput" class="control" type="number" min="0" max="10000000" value="${policy.max_iterations ?? 0}"></div><p class="file-meta">Automatic LP solving runs without an iteration or time cap.</p>`}<div class="config-item"><label>TIME LIMIT (SECONDS) <span class="muted">(0 = no limit)</span></label><input id="timeInput" class="control" type="number" min="0" max="3600" value="${policy.time_limit_seconds ?? 0}"></div><button id="expertRunButton" class="run-button">RUN WITH EXPERT SETTINGS <span>→</span></button></div></details>
      </article>
    </div>
    <article class="panel matrix-card"><div class="matrix-head"><div><h3>CONSTRAINT MATRIX OVERVIEW</h3><p>Shows where variables and constraints interact.</p></div><span>${a.matrix.aggregated ? "Aggregated view for display" : "Exact matrix view"}</span></div><div class="matrix-summary">${stat("VARIABLES",number(a.variables,0))}${stat("CONSTRAINTS",number(a.constraints,0))}${stat("NON-ZERO COEFFICIENTS",number(a.nonzeros,0))}${stat("SPARSITY",`${number(a.sparsity,2)}%`)}</div><div class="matrix-viewport"><canvas id="matrixCanvas" aria-label="Constraint matrix interaction heatmap"></canvas><div id="matrixUnavailable" class="matrix-unavailable hidden">Matrix visualization unavailable.</div><div id="matrixTooltip" class="matrix-tooltip hidden"></div></div><div class="matrix-legend"><span><i class="heat-key high"></i>More interactions</span><span><i class="heat-key low"></i>Fewer interactions</span><p>Fewer highlighted areas generally mean a sparser optimization problem.</p></div></article>
    <div id="resultArea"></div>`;
  $("#expertRunButton").addEventListener("click", runExpertSolve); renderMatrix();
}
function stat(label,value){return `<div class="stat"><p>${label}</p><strong>${value}</strong></div>`}

function renderMatrix() {
  const canvas = $("#matrixCanvas"), unavailable = $("#matrixUnavailable"), tooltip = $("#matrixTooltip"), a = state.analysis, matrix = a.matrix || {};
  const rows = Number(matrix.rows || 0), columns = Number(matrix.columns || 0), nonzeros = Number(a.linear_nonzeros ?? a.nonzeros ?? 0);
  const displayRows = Number(matrix.display_rows || 0), displayColumns = Number(matrix.display_columns || 0), buckets = Array.isArray(matrix.buckets) ? matrix.buckets : [];
  if (!rows || !columns || !nonzeros || !displayRows || !displayColumns || !buckets.length) { canvas.classList.add("hidden"); unavailable.classList.remove("hidden"); tooltip.classList.add("hidden"); return; }
  canvas.classList.remove("hidden"); unavailable.classList.add("hidden");
  const box = canvas.getBoundingClientRect(), ratio = window.devicePixelRatio || 1, width = box.width, height = box.height;
  canvas.width = Math.max(1, Math.floor(width * ratio)); canvas.height = Math.max(1, Math.floor(height * ratio));
  const ctx = canvas.getContext("2d"); ctx.setTransform(ratio, 0, 0, ratio, 0, 0); ctx.clearRect(0, 0, width, height);
  const gap = displayRows > 35 || displayColumns > 35 ? 1 : 2, cellWidth = width / displayColumns, cellHeight = height / displayRows, counts = new Map();
  buckets.forEach(([row, column, count]) => counts.set(`${row}:${column}`, Number(count)));
  const maxCount = Math.max(1, ...counts.values());
  for (let row = 0; row < displayRows; row += 1) for (let column = 0; column < displayColumns; column += 1) {
    const count = counts.get(`${row}:${column}`) || 0, intensity = count ? 0.2 + 0.8 * Math.log1p(count) / Math.log1p(maxCount) : 0;
    ctx.fillStyle = count ? `rgba(101,216,189,${intensity})` : "rgba(123,154,181,.10)";
    ctx.fillRect(column * cellWidth + gap / 2, row * cellHeight + gap / 2, Math.max(0, cellWidth - gap), Math.max(0, cellHeight - gap));
  }
  const hideTooltip = () => tooltip.classList.add("hidden");
  canvas.onmouseleave = hideTooltip;
  canvas.onmousemove = event => {
    const rect = canvas.getBoundingClientRect(), column = Math.min(displayColumns - 1, Math.max(0, Math.floor((event.clientX - rect.left) / cellWidth))), row = Math.min(displayRows - 1, Math.max(0, Math.floor((event.clientY - rect.top) / cellHeight)));
    const count = counts.get(`${row}:${column}`) || 0, rowStart = Math.floor(row * rows / displayRows) + 1, rowEnd = Math.floor((row + 1) * rows / displayRows), columnStart = Math.floor(column * columns / displayColumns) + 1, columnEnd = Math.floor((column + 1) * columns / displayColumns), area = Math.max(1, (rowEnd - rowStart + 1) * (columnEnd - columnStart + 1)), density = count / area * 100;
    tooltip.innerHTML = `Constraint range: ${rowStart}–${rowEnd}<br>Variable range: ${columnStart}–${columnEnd}<br>Non-zero coefficients: ${number(count,0)}<br>Density: ${number(density,2)}%`;
    tooltip.style.left = `${Math.min(width - 190, Math.max(8, event.clientX - rect.left + 12))}px`; tooltip.style.top = `${Math.min(height - 92, Math.max(8, event.clientY - rect.top + 12))}px`; tooltip.classList.remove("hidden");
  };
}

async function startAutomaticSolve() {
  renderRunning(state.policy || {});
  try { const result = await api("/api/solve/auto", {method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify({job_id:state.jobId})}); state.result=result; renderAutomaticDecision(result); renderResult(result); }
  catch(error) { renderFailure(error.message); toast(error.message); const status=$("#autoStatus"); if(status) status.textContent="Automatic pipeline failed"; }
}

async function runExpertSolve() {
  const button = $("#expertRunButton");
  const request = {job_id:state.jobId, method:$("#methodSelect").value, backend:$("#backendSelect").value, presolve:$("#presolveToggle").checked, max_iterations:Number($("#iterationInput")?.value ?? 10000), max_nodes:Number($("#nodeInput")?.value ?? 10000), time_limit_seconds:Number($("#timeInput").value)};
  button.disabled = true; button.textContent = "EXPERT SOLVER RUNNING…";
  renderRunning(request);
  try { const result = await api("/api/solve", {method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(request)}); state.result=result; renderResult(result); }
  catch(error) { renderFailure(error.message); toast(error.message); }
  finally { button.disabled=false; button.innerHTML="RUN WITH EXPERT SETTINGS <span>→</span>"; }
}

function clearRunningTimer() { if (state.runningTimer) clearInterval(state.runningTimer); state.runningTimer = null; }
function renderRunning(configuration = {}) {
  clearRunningTimer(); state.runningConfiguration = configuration; state.runningStarted = Date.now();
  const limit = Number(configuration.execution_time_limit_seconds ?? configuration.time_limit_seconds ?? 0);
  const hasTimeLimit = Number.isFinite(limit) && limit > 0;
  const limitText = hasTimeLimit ? ` / ${String(Math.floor(limit/60)).padStart(2,"0")}:${String(Math.floor(limit%60)).padStart(2,"0")}` : " · NO LIMIT";
  $("#resultArea").innerHTML = `<div class="result-area"><article class="panel pipeline">${["UPLOADED","PARSING","VALIDATING","CLASSIFYING","PRESOLVING","SELECTING","SOLVING","POSTSOLVING","VERIFYING","COMPLETE"].map((stage,index)=>`<span class="stage ${index===6?"running":index<6?"done":""}">${stage}</span>`).join("")}</article><article class="panel loading-panel solve-loading"><div class="solve-loading-head"><div><p class="eyebrow">ACTIVE EXECUTION</p><h3>OPTIMIZATION IN PROGRESS</h3></div><span class="solve-live-dot">LIVE</span></div><div class="solve-progress" role="progressbar" aria-label="Optimization is running"><i id="solveProgressFill"></i></div><div class="solve-progress-meta"><span>Preparation complete</span><b id="solveState">Solving on ${escapeHtml(configuration.backend || "cpu").toUpperCase()}</b></div><p>Elapsed: <b id="liveElapsed">00:00${limitText}</b></p><p>Method: <b>${escapeHtml(configuration.method || "automatic")}</b> · Backend: <b>${escapeHtml(configuration.backend || "cpu")}</b></p><p class="file-meta">The bar shows completed preparation and active solver work. Iteration events are not streamed by the C++ solver, so it remains animated until a verified result is returned.</p><button id="cancelSolveButton" class="export-button">CANCEL OPTIMIZATION</button></article></div>`;
  const updateElapsed = () => { const element=$("#liveElapsed"); if (!element) return; const elapsed=(Date.now()-state.runningStarted)/1000; const pad=v=>String(Math.floor(v)).padStart(2,"0"); element.textContent=`${pad(elapsed/60)}:${pad(elapsed%60)}${limitText}`; };
  updateElapsed(); state.runningTimer=setInterval(updateElapsed, 250);
  $("#cancelSolveButton").addEventListener("click", cancelOptimization);
}

async function cancelOptimization() {
  const button=$("#cancelSolveButton"); if (button) { button.disabled=true; button.textContent="CANCELLING…"; }
  try { await api(`/api/solve/${encodeURIComponent(state.jobId)}/cancel`, {method:"POST"}); toast("Cancellation requested."); }
  catch(error) { if (button) { button.disabled=false; button.textContent="CANCEL OPTIMIZATION"; } toast(error.message); }
}

function renderAutomaticDecision(result) {
  const automation = result.automation; if (!automation || automation.mode !== "automatic") return;
  const status = $("#autoStatus"), host = $("#autoDecision"), selection = automation.selection || {};
  if (status) status.textContent = result.verification === "PASS" ? "Automatic pipeline completed" : "Automatic pipeline completed with a warning";
  if (!host) return;
  const pre = result.presolve || {}, attempts = automation.attempts || [];
  host.innerHTML = `<div class="decision-row done"><span>MODEL DETECTED</span><b>${escapeHtml(result.analysis.problem_type)} · ${escapeHtml(selection.model_size || "—")}</b></div><div class="decision-row done"><span>PRESOLVE</span><b>${pre.before_variables ?? "—"} vars / ${pre.before_constraints ?? "—"} rows → ${pre.after_variables ?? "—"} vars / ${pre.after_constraints ?? "—"} rows</b></div><div class="decision-row done"><span>EXECUTION LIMITS</span><b>${timeLimitLabel(result.configuration.time_limit_seconds)} · ${result.analysis.problem_type === "MILP" ? `${number(result.configuration.max_nodes,0)} B&B nodes · ${iterationLimitLabel(result.configuration.max_iterations)} LP iterations/node` : `${iterationLimitLabel(result.configuration.max_iterations)} iterations`}</b></div><div class="decision-row done"><span>SOLVER SELECTED</span><b>${escapeHtml(automation.final_method || selection.method || "—")}</b></div><div class="decision-row done"><span>COMPUTE BACKEND</span><b>${escapeHtml(automation.final_backend || selection.backend || "—")}</b></div><div class="decision-row ${result.verification === "PASS" ? "done" : "warning"}"><span>ORIGINAL-MODEL VERIFICATION</span><b>${escapeHtml(result.verification)}</b></div><details class="selection-why"><summary>WHY WAS THIS METHOD SELECTED?</summary><p>${escapeHtml(selection.reason || "Expert settings were supplied.")}</p><p>${escapeHtml(selection.backend_reason || "")}</p>${attempts.length > 1 ? `<p>Attempts: ${attempts.map(attempt => `${escapeHtml(attempt.method)} (${escapeHtml(attempt.status)})`).join(" → ")}</p>` : ""}</details>`;
}

function renderFailure(message) {
  clearRunningTimer();
  const activeJob = String(message).match(/Optimization is already running for ([A-Za-z0-9_-]+)\./)?.[1];
  if (activeJob) {
    $("#resultArea").innerHTML = `<div class="result-area"><article class="panel result-top"><div class="result-status"><span class="status-orb warning">!</span><div><h2>SOLVER BUSY</h2><p>An earlier optimization is still using the solver. Cancel that run to free the solver, then upload or select your model and start again.</p><p class="file-meta">Active job: ${escapeHtml(activeJob)}</p><div class="export-actions"><a class="export-button" href="/api/solve/${encodeURIComponent(activeJob)}" target="_blank" rel="noopener">VIEW ACTIVE RUN</a><button id="cancelActiveSolveButton" class="export-button">CANCEL ACTIVE RUN</button></div><p id="activeSolveRecovery" class="file-meta" aria-live="polite"></p></div></div></article></div>`;
    $("#cancelActiveSolveButton").addEventListener("click", event => cancelActiveSolve(activeJob, event.currentTarget));
    return;
  }
  $("#resultArea").innerHTML=`<div class="result-area"><article class="panel result-top"><div class="result-status"><span class="status-orb fail">!</span><div><h2>REQUEST FAILED</h2><p>${escapeHtml(message)}</p></div></div></article></div>`;
}

async function cancelActiveSolve(jobId, button) {
  const recovery = $("#activeSolveRecovery");
  button.disabled = true; button.textContent = "CANCELLING…";
  try {
    await api(`/api/solve/${encodeURIComponent(jobId)}/cancel`, {method:"POST"});
    if (recovery) recovery.textContent = "Cancellation requested. Waiting for the solver to stop…";
    for (let attempt = 0; attempt < 20; attempt++) {
      await new Promise(resolve => setTimeout(resolve, 500));
      const status = await api(`/api/solve/${encodeURIComponent(jobId)}`);
      if (status.state !== "solving") {
        button.textContent = "ACTIVE RUN STOPPED";
        if (recovery) recovery.textContent = jobId === state.jobId
          ? "The active run stopped. Upload/select the model again, then start a fresh solve."
          : "The active run stopped. You can now start your solve again.";
        toast("The active solver run stopped.");
        return;
      }
    }
    button.disabled = false; button.textContent = "WAITING FOR SOLVER";
    if (recovery) recovery.textContent = "The solver has not confirmed cancellation yet. Check the active run status before starting another solve.";
  } catch (error) {
    button.disabled = false; button.textContent = "CANCEL ACTIVE RUN";
    if (recovery) recovery.textContent = error.message;
    toast(error.message);
  }
}

function humanStatus(status) { const value=(status || "FAILED").toUpperCase(); if(value==="TIME_LIMIT" || value==="TIME LIMIT") return "TIME LIMIT REACHED"; if(value==="ITERATION_LIMIT" || value==="ITERATION LIMIT") return "LP ITERATION LIMIT REACHED"; if(value==="NODE_LIMIT") return "NODE LIMIT REACHED"; if(value==="MEMORY_LIMIT") return "MEMORY LIMIT REACHED"; if(value==="SOLVER_ERROR") return "SOLVER ERROR"; if(value==="CANCELLED") return "CANCELLED BY USER"; return value.replaceAll("_", " "); }
function statusExplanation(result) {
  const status=(result.status||"").toUpperCase(); if(status==="OPTIMAL") return "An optimal solution was found and independently verified against the original model.";
  if(status==="FEASIBLE") return "A feasible solution was returned. Review the reported bounds and verification details below.";
  if(status.includes("TIME")) return "The solver reached the configured time limit before proving optimality.";
  if(status.includes("ITERATION")) return result.metrics.message || "The configured LP iteration limit was reached before optimality was established.";
  if(status.includes("NODE_LIMIT")) return "The configured branch-and-bound node limit was reached. Any returned incumbent and gap are reported below.";
  if(status==="CANCELLED") return "The active solver subprocess was terminated at the user's request.";
  if(status.includes("MEMORY_LIMIT") || status.includes("SOLVER_ERROR")) return result.metrics.message || "The solver process did not complete successfully.";
  if(status.includes("UNSUPPORTED")) return "The solver rejected this model or method because the requested path is not implemented.";
  return result.metrics.message || "The solver did not return a verified optimal solution.";
}

function renderResult(result) {
  clearRunningTimer();
  const good = ["OPTIMAL","FEASIBLE"].includes((result.status||"").toUpperCase()) && result.verification === "PASS";
  const warning = !good && /TIME|ITERATION|NODE_LIMIT|MEMORY_LIMIT|UNSUPPORTED/.test(result.status || "");
  const cls=good?"":warning?"warn":"fail", symbol=good?"✓":warning?"!":"×";
  const verificationFailed = ["OPTIMAL", "FEASIBLE"].includes((result.status || "").toUpperCase()) && result.verification !== "PASS";
  const headline = verificationFailed ? "VERIFICATION FAILED" : humanStatus(result.status);
  const headlineClass = verificationFailed ? "bad" : statusClass(result.status);
  const timings = result.timings, metrics = result.metrics, pre=result.presolve, configuration=result.configuration || {};
  const timingRows = [["Parsing",timings.parse_time_ms,""],["Model preparation",timings.model_preparation_time_ms,""],["Presolve",timings.presolve_time_ms,""],["Solver",timings.solver_time_ms,"solver-timing"],["Postsolve",timings.postsolve_time_ms,""],["Verification",timings.verification_time_ms,""]];
  const largest=Math.max(...timingRows.map(row=>Number(row[1])||0),1);
  const reduction = pre.before_constraints && pre.after_constraints !== null ? Math.max(0,(1-pre.after_constraints/pre.before_constraints)*100) : null;
  const limited = String(result.status).toUpperCase().includes("TIME") || String(result.status).toUpperCase().includes("ITERATION") || String(result.status).toUpperCase().includes("NODE_LIMIT") || String(result.status).toUpperCase().includes("MEMORY_LIMIT");
  const processedModel = limited ? `<article class="panel presolve-card"><p class="eyebrow">MODEL DATA COMPLETED BEFORE SOLVER STOPPED</p><h3>PARSED AND PRESOLVED MODEL</h3><p class="file-meta">No candidate solution was returned before the solver stopped. These are real model and presolve values recorded before solving.</p><div class="stat-grid">${stat("ORIGINAL VARIABLES",number(result.analysis.variables,0))}${stat("ORIGINAL CONSTRAINTS",number(result.analysis.constraints,0))}${stat("MATRIX NON-ZEROS",number(result.analysis.nonzeros,0))}${stat("MATRIX SPARSITY",`${number(result.analysis.sparsity,2)}%`)}${stat("POST-PRESOLVE VARIABLES",pre.after_variables === null || pre.after_variables === undefined ? "N/A" : number(pre.after_variables,0))}${stat("POST-PRESOLVE CONSTRAINTS",pre.after_constraints === null || pre.after_constraints === undefined ? "N/A" : number(pre.after_constraints,0))}${stat("MEASURED REDUCTIONS",pre.reductions === null || pre.reductions === undefined ? "N/A" : number(pre.reductions,0))}${stat("MODEL TYPE",escapeHtml(result.analysis.problem_type))}${stat("PARSING COMPLETED",duration(timings.parse_time_ms))}${stat("PRESOLVE COMPLETED",duration(timings.presolve_time_ms))}${stat("SELECTED METHOD",escapeHtml(configuration.method || metrics.method || "N/A"))}${stat("SELECTED BACKEND",escapeHtml(configuration.backend || metrics.backend || "N/A"))}</div></article>` : "";
  $("#resultArea").innerHTML = `<div class="result-area">
    <article class="panel pipeline">${["UPLOADED","PARSING","VALIDATING","CLASSIFYING","PRESOLVING","SELECTING","SOLVING","POSTSOLVING","VERIFYING","COMPLETE"].map(stage=>`<span class="stage done">${stage}</span>`).join("")}</article>
    <article class="panel result-top"><div class="result-status"><span class="status-orb ${cls}">${symbol}</span><div><h2 class="${headlineClass}">${escapeHtml(headline)}</h2><p>${escapeHtml(statusExplanation(result))}</p></div></div><div class="result-kpis"><div><label>OBJECTIVE</label><strong>${number(metrics.objective,6)}</strong></div><div><label>VERIFICATION</label><strong class="${result.verification === "PASS" ? "pass" : "warning"}">${escapeHtml(result.verification)}</strong></div><div class="solver-time-kpi"><label>SOLVER TIME</label><strong>${duration(timings.solver_time_ms)}</strong></div></div></article>
    ${processedModel}
    <div class="result-grid"><article class="panel timing-card"><h3>PERFORMANCE</h3>${timingRows.map(([label,value,rowClass])=>`<div class="timing-row ${rowClass}"><span>${label}</span><div class="timing-bar"><i style="width:${value===null||value===undefined?0:Math.max(3,value/largest*100)}%"></i></div><b>${duration(value)}</b></div>`).join("")}<div class="timing-total"><span>Overall / Backend</span><b>${duration(timings.backend_total_time_ms)}</b></div><p class="solver-highlight">Overall backend time includes measured model preparation and solver request work; it excludes browser rendering and network time.</p></article>
    <article class="panel verify-card"><h3>INDEPENDENT SOLUTION VERIFICATION</h3><div class="verify-stack"><div class="verify-row"><span>Original model verification</span><b class="${result.verification === "PASS"?"pass":"warning"}">${escapeHtml(result.verification)}</b></div>${verificationRow("Maximum constraint violation",metrics.feasibility)}${verificationRow("Primal residual",metrics.primal_residual)}${verificationRow("Dual residual",metrics.dual_residual)}${verificationRow("Integrality",result.analysis.problem_type==="MILP" ? result.verification : null)}${verificationRow("Objective consistency",metrics.objective !== null ? result.verification : null)}</div></article></div>
    ${limited ? `<article class="panel presolve-card"><h3>${escapeHtml(humanStatus(result.status))}</h3><div class="stat-grid">${stat("CONFIGURED TIME LIMIT",`${number(configuration.time_limit_seconds,0)} s`)}${stat("ELAPSED TIME",seconds(timings.backend_total_time_ms === null ? null : timings.backend_total_time_ms / 1000))}${result.analysis.problem_type === "MILP" ? stat("MAX B&B NODES",configuration.max_nodes === 0 ? "Unlimited" : number(configuration.max_nodes,0)) : stat("MAXIMUM ITERATIONS",iterationLimitLabel(configuration.max_iterations))}${result.analysis.problem_type === "MILP" ? stat("MAX LP ITERATIONS / NODE",iterationLimitLabel(configuration.max_iterations)) : ""}${result.analysis.problem_type === "MILP" ? stat("NODES PROCESSED",metrics.nodes_processed === null || metrics.nodes_processed === undefined ? "N/A" : number(metrics.nodes_processed,0)) : stat("ITERATIONS COMPLETED",metrics.iterations === null || metrics.iterations === undefined ? "N/A" : number(metrics.iterations,0))}${stat("LAST OBJECTIVE",metrics.objective === null || metrics.objective === undefined ? "N/A" : number(metrics.objective,6))}${stat("BACKEND",escapeHtml(configuration.backend || metrics.backend || "N/A"))}</div></article>` : ""}
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
  $("#variableStatus").textContent = summary.sparse ? `Showing ${number(rows.length,0)} non-zero variables; ${number(summary.zero,0)} zero values are omitted from the large-model response · tolerance ${summary.zero_tolerance}` : explorerState.query ? `Search spans all ${number(summary.total,0)} solution variables · ${number(rows.length,0)} match${rows.length === 1 ? "" : "es"}` : `Showing ${number(rows.length,0)} ${explorerState.filter === "nonzero" ? "non-zero" : explorerState.filter.replace("-", " ")} variables · tolerance ${summary.zero_tolerance ?? "—"}`;
  $$('[data-variable-filter]').forEach(button => button.classList.toggle("active", button.dataset.variableFilter === explorerState.filter));
  const key = [...explorerState.values].filter(row => isNumericValue(row.value) && Math.abs(row.value) > Number(summary.zero_tolerance || 1e-8)).sort((a,b) => Math.abs(b.value) - Math.abs(a.value)).slice(0, 8);
  $("#keyVariables").innerHTML = key.length ? `<div class="key-variables"><p>KEY SOLUTION VARIABLES</p>${key.map(row => `<span><b>${escapeHtml(row.name)}</b><em>${number(row.value,8)}</em></span>`).join("")}</div>` : "";
  const allZero = summary.available > 0 && summary.nonzero === 0;
  $("#variableRows").innerHTML = slice.length ? slice.map(row => `<tr><td>${escapeHtml(row.name)}</td><td>${number(row.value,8)}</td><td>${escapeHtml(row.type || "—")}</td><td>${number(row.lower,5)}</td><td>${number(row.upper,5)}</td><td>${row.at_bound === true ? "YES" : row.value === null ? "—" : "NO"}</td></tr>`).join("") : `<tr><td colspan="6">${allZero && explorerState.filter === "nonzero" ? "All solution variables are zero. <button class=\"inline-explorer-button\" data-view-all>VIEW ALL VARIABLES</button>" : "No variables match this filter."}</td></tr>`;
  $("#variablePagination").innerHTML = `<span>${number(rows.length,0)} result${rows.length === 1 ? "" : "s"} · Page ${explorerState.page} of ${totalPages}</span><div><button ${explorerState.page === 1 ? "disabled" : ""} data-page="previous">PREVIOUS</button><button ${explorerState.page === totalPages ? "disabled" : ""} data-page="next">NEXT</button></div>`;
  $$('[data-page]').forEach(button => button.addEventListener("click", () => { explorerState.page += button.dataset.page === "next" ? 1 : -1; renderVariableExplorer(); }));
  $$('[data-view-all]').forEach(button => button.addEventListener("click", () => { explorerState.filter = "all"; explorerState.page = 1; renderVariableExplorer(); }));
}

function benchStatus(row) { return String(row?.status || "—").toUpperCase().replaceAll("_", " "); }
function benchClass(status) { const value=String(status||"").toUpperCase(); return ["OPTIMAL","FEASIBLE"].includes(value)?"pass":value.includes("TIME")?"warning":value.includes("UNAVAILABLE")||value.includes("NOT_")||value.includes("LICENSE")||value==="—"?"bench-muted":"bad"; }
function benchComparatorName(name) { const info=state.benchmarkAvailability[name]||{}; if(info.available)return name==="highs"?"HiGHS":name==="gurobi"?"Gurobi":"CPLEX"; return name==="gurobi"?"Gurobi — Not Installed / License Unavailable":name==="cplex"?"CPLEX — Not Installed / License Unavailable":"HiGHS — Not Installed"; }
function benchMetric(value, suffix="") { return value===null||value===undefined||!Number.isFinite(Number(value))?"—":`${number(value,2)}${suffix}`; }
function renderLiveQpDetails(row) { const fields=[["Convexity",row.convexity],["Primal residual",row.primal_residual],["Dual residual",row.dual_residual],["KKT / complementarity residual",row.complementarity_residual]].filter(([,value])=>value!==null&&value!==undefined);return fields.length?`<dl>${fields.map(([label,value])=>`<dt>${label}</dt><dd>${typeof value==="number"?number(value,8):escapeHtml(value)}</dd>`).join("")}</dl>`:""; }
function renderHistoricalComparison(row) {
  if(!row)return "";const ref=row.highs,status=String(ref?.reference_solver_status||"").toUpperCase(),hasTime=["OPTIMAL","FEASIBLE"].includes(status)&&Number.isFinite(Number(ref?.reference_runtime_ms)),sovereignTime=["OPTIMAL","FEASIBLE"].includes(String(row.status||"").toUpperCase())&&Number.isFinite(Number(row.solve_time_ms)),times=[sovereignTime?Number(row.solve_time_ms):null,hasTime?Number(ref.reference_runtime_ms):null].filter(Number.isFinite),max=Math.max(1,...times);const agreement=ref?.objective_match===true?"✓ Objectives agree":ref?.objective_match===false?"⚠ Objective mismatch":ref?benchStatus(ref):"No comparator record";return `<div class="bench-history-compare"><div class="bench-quality ${ref?.objective_match===true?"pass":ref?.objective_match===false?"warning":"bench-muted"}"><b>SOLUTION QUALITY</b><strong>${escapeHtml(agreement)}</strong>${ref?.objective_difference!==undefined&&ref?.objective_difference!==null?`<span>Absolute objective difference: ${number(ref.objective_difference,8)}</span>`:""}</div><article class="bench-chart"><h4>SOLVE TIME <small>Recorded solver time</small></h4>${[{name:"Sovereign",time:sovereignTime?row.solve_time_ms:null,status:row.status},{name:"HiGHS",time:hasTime?ref.reference_runtime_ms:null,status:ref?.reference_solver_status}].map(item=>`<div class="bench-bar-row"><span>${item.name}</span>${Number.isFinite(Number(item.time))?`<div class="bench-bar-track"><i style="width:${Math.max(2,Number(item.time)/max*100)}%"></i></div><b>${duration(item.time)}</b>`:`<em class="${String(item.status||"").includes("TIME")?"warning":"bench-muted"}">${item.status?escapeHtml(benchStatus({status:item.status})):"—"}</em>`}</div>`).join("")}</article><div class="bench-solver-grid bench-history-cards"><article class="bench-solver-card"><h4>SOVEREIGN</h4><dl><dt>Status</dt><dd class="${benchClass(row.status)}">${escapeHtml(benchStatus(row))}</dd><dt>Objective</dt><dd>${benchMetric(row.objective)}</dd><dt>Iterations / nodes</dt><dd>${number(row.iterations??row.nodes_processed,0)}</dd><dt>Verification</dt><dd class="${row.verification_pass===true?"pass":row.verification_pass===false?"bad":"bench-muted"}">${row.verification_pass===true?"PASS":row.verification_pass===false?"FAIL":"N/A"}</dd><dt>Algorithm · backend</dt><dd>${escapeHtml(row.algorithm||"—")} · ${escapeHtml(row.backend||"—")}</dd></dl></article><article class="bench-solver-card"><h4>HiGHS</h4><dl><dt>Status</dt><dd class="${benchClass(ref?.reference_solver_status)}">${escapeHtml(ref?.reference_solver_status?benchStatus(ref):benchComparatorName("highs"))}</dd><dt>Objective</dt><dd>${benchMetric(ref?.reference_objective)}</dd><dt>Iterations / nodes</dt><dd>${number(ref?.reference_iterations??ref?.reference_nodes,0)}</dd><dt>Verification</dt><dd class="bench-muted">N/A</dd><dt>Backend</dt><dd>—</dd></dl></article></div></div>`;
}
function renderHistoricalSolverDetails(row) {
  if(row.problem_type==="QP")return `<div class="bench-timings"><h4>QP DIAGNOSTICS</h4>${[["Convexity",row.convexity], ["Hessian",row.hessian_type], ["Primal residual",row.primal_residual], ["Dual residual",row.dual_residual], ["KKT / complementarity residual",row.complementarity_residual]].map(([label,value])=>`<div><span>${label}</span><b>${value===null||value===undefined?"—":typeof value==="number"?number(value,8):escapeHtml(value)}</b></div>`).join("")}</div>`;
  if(row.problem_type==="MILP")return `<div class="bench-timings"><h4>MILP SEARCH TELEMETRY</h4>${[["Nodes created",row.nodes_created], ["Nodes processed",row.nodes_processed], ["Best bound",row.best_bound], ["Relative gap",row.relative_gap===null||row.relative_gap===undefined?null:`${number(row.relative_gap,6)}%`], ["Cuts generated",row.cuts_generated], ["Feasibility Pump",row.feasibility_pump_used]].map(([label,value])=>`<div><span>${label}</span><b>${value===null||value===undefined?"—":typeof value==="number"?number(value,6):escapeHtml(value)}</b></div>`).join("")}</div>`;
  return "";
}
function renderBenchmarkComparison() {
  const data=state.benchmarkComparison;if(!data)return "";
  const rows=data.solver_results||[], times=rows.filter(row=>["OPTIMAL","FEASIBLE"].includes(String(row.status).toUpperCase())&&row.verification==="PASS"&&Number.isFinite(Number(row.solve_time_ms))&&Number(row.solve_time_ms)>=0), maxTime=Math.max(1,...times.map(row=>Number(row.solve_time_ms)));
  const compareText=data.objective_agreement===true?"✓ AVAILABLE SOLVERS AGREE":data.objective_agreement===false?"⚠ OBJECTIVE MISMATCH":"Objective agreement unavailable — no verified cross-solver comparison.";
  const metricLabel=data.problem_type==="MILP"?"Incumbent objective":data.problem_type==="QP"?"Objective":"Objective";
  return `<section class="bench-live panel"><div class="bench-detail-heading"><div><p class="eyebrow">SAME-INSTANCE COMPARISON</p><h3>${escapeHtml(data.instance||"Uploaded model")}</h3><p class="file-meta">${escapeHtml(data.problem_type||"—")} · ${number(data.model?.variables,0)} variables · ${number(data.model?.constraints,0)} constraints · ${number(data.model?.nonzeros,0)} nonzeros · ${number(data.model?.sparsity,2)}% sparse</p></div><button id="clearBenchCompare" class="link-button">CLEAR COMPARISON</button></div><p class="bench-fairness">Same model <i>•</i> Same hardware <i>•</i> Same ${number(data.time_limit_seconds,0)} s wall-clock time limit</p><div class="bench-quality ${data.objective_agreement===false?"warning":data.objective_agreement===true?"pass":"bench-muted"}"><b>SOLUTION QUALITY</b><strong>${escapeHtml(compareText)}</strong>${(data.objective_comparisons||[]).map(item=>`<span>${escapeHtml(item.solver)}: absolute difference ${number(item.absolute_difference,8)} · relative ${number(item.relative_difference*100,6)}%</span>`).join("")}</div><div class="bench-solver-grid">${rows.map(row=>{const status=benchStatus(row);const sovereign=row.solver==="sovereign";const label=sovereign?"SOVEREIGN":benchComparatorName(row.solver);const ok=["OPTIMAL","FEASIBLE"].includes(String(row.status).toUpperCase());const tooltip=`Solver: ${label} · Status: ${status} · Solve time: ${row.solve_time_ms==null?"—":duration(row.solve_time_ms)} · Objective: ${row.objective??"—"} · Iterations/nodes: ${row.iterations??row.nodes??"—"} · Backend: ${row.backend??"—"}`;return `<article class="bench-solver-card" title="${escapeHtml(tooltip)}"><div class="bench-card-heading"><h4>${escapeHtml(label)}</h4><span class="${benchClass(row.status)}">${escapeHtml(status)}</span></div><dl><dt>${metricLabel}</dt><dd>${benchMetric(row.objective,"")}</dd>${data.problem_type==="MILP"?`<dt>Best bound · gap</dt><dd>${benchMetric(row.best_bound)} · ${benchMetric(row.relative_gap,"%")}</dd>`:""}<dt>Solve time</dt><dd>${row.status==="TIME_LIMIT"?"TIME LIMIT":ok?duration(row.solve_time_ms):benchMetric(row.solve_time_ms," ms")}</dd><dt>${data.problem_type==="MILP"?"Nodes":"Iterations"}</dt><dd>${number(data.problem_type==="MILP"?row.nodes:row.iterations,0)}</dd><dt>Verification</dt><dd class="${row.verification==="PASS"?"pass":row.verification==="FAIL"?"bad":"bench-muted"}">${escapeHtml(row.verification||"N/A")}</dd>${sovereign?`<dt>Algorithm · backend</dt><dd>${escapeHtml(row.algorithm||"—")} · ${escapeHtml(row.backend||"—")}</dd>`:""}</dl>${data.problem_type==="QP"&&sovereign?renderLiveQpDetails(row):""}${row.failure_reason?`<p class="bench-error">${escapeHtml(row.failure_reason)}</p>`:""}</article>`;}).join("")}</div><article class="bench-chart panel"><h4>SOLVE TIME <small>Measured solver time</small></h4>${rows.map(row=>{const actual=times.includes(row);const label=row.solver==="sovereign"?"Sovereign":benchComparatorName(row.solver);const tooltip=`Solver: ${label} · Status: ${benchStatus(row)} · Solve time: ${row.solve_time_ms==null?"—":duration(row.solve_time_ms)} · Objective: ${row.objective??"—"} · Iterations/nodes: ${row.iterations??row.nodes??"—"} · Backend: ${row.backend??"—"}`;return `<div class="bench-bar-row" title="${escapeHtml(tooltip)}"><span>${escapeHtml(label)}</span>${actual?`<div class="bench-bar-track"><i style="width:${Math.max(2,Number(row.solve_time_ms)/maxTime*100)}%"></i></div><b>${duration(row.solve_time_ms)}</b>`:`<em class="${row.status==="TIME_LIMIT"?"warning":"bench-muted"}">${row.status==="TIME_LIMIT"?"TIME LIMIT":"—"}</em>`}</div>`;}).join("")}</article><details class="bench-environment panel"><summary>BENCHMARK ENVIRONMENT</summary><div class="bench-environment-grid">${[["CPU",data.hardware?.cpu],["GPU",data.hardware?.gpu],["CUDA",data.hardware?.cuda_available===true?data.hardware?.cuda_version||"Available":data.hardware?.cuda_available===false?"Unavailable":null],["RAM",data.hardware?.ram_bytes?`${number(data.hardware.ram_bytes/1073741824,1)} GB`:null],["OS",data.hardware?.os],["Timestamp",data.timestamp_utc],["Timing definition","Solver-reported solve time; no browser/network rendering time"]].map(([label,value])=>`<div><span>${label}</span><b>${escapeHtml(value??"—")}</b></div>`).join("")}</div><p>${escapeHtml(data.fairness||"")}</p></details></section>`;
}
function renderBenchmarks(selectedName) {
  const host=$("#benchmarkArea"),datasets=state.benchmarkData||[];if(!host)return;
  const suiteOrder=["netlib","mittelmann","miplib","qplib"],labels={netlib:"NETLIB — LP",mittelmann:"MITTELMANN — LP",miplib:"MIPLIB — MILP",qplib:"QPLIB — QP"};
  const active=datasets.find(data=>data.dataset===selectedName)||datasets.find(data=>data.dataset===state.benchmarkSelectedInstance?.suite)||datasets.find(data=>data.instances?.length)||datasets[0]||{dataset:"netlib",metadata:{},instances:[]};state.benchmarkSelectedInstance={suite:active.dataset,instance:state.benchmarkSelectedInstance?.suite===active.dataset?state.benchmarkSelectedInstance.instance:null};
  const allRows=active.instances||[],query=(host.querySelector("#benchmarkSearch")?.value||"").toLowerCase(),filter=host.querySelector("#benchmarkFilter")?.value||"all";
  const verified=allRows.filter(row=>row.verification_pass===true&&["OPTIMAL","FEASIBLE"].includes(String(row.status).toUpperCase())).length;
  const timeouts=allRows.filter(row=>String(row.status).toUpperCase().includes("TIME_LIMIT")).length;
  const failures=allRows.filter(row=>row.verification_pass===false||["FAILED","NUMERICAL_FAILURE","UNSUPPORTED","ITERATION_LIMIT","NODE_LIMIT"].includes(String(row.status).toUpperCase())).length;
  const agreements=allRows.filter(row=>row.highs?.objective_match===true||row.highs?.objective_match===false),agreementCount=agreements.filter(row=>row.highs.objective_match===true).length;
  const selected=allRows.find(row=>row.instance===state.benchmarkSelectedInstance.instance)||allRows[0]||null;
  const filtered=allRows.filter(row=>{const status=String(row.status||"").toUpperCase(),agree=row.highs?.objective_match===true||row.highs?.objective_match===false;const matches=String(row.instance||"").toLowerCase().includes(query);return matches&&(filter==="all"||filter==="verified"&&(row.verification_pass===true)&&["OPTIMAL","FEASIBLE"].includes(status)||filter==="timeout"&&status.includes("TIME_LIMIT")||filter==="failed"&&["FAILED","NUMERICAL_FAILURE","UNSUPPORTED","ITERATION_LIMIT","NODE_LIMIT"].includes(status)||filter==="mismatch"&&agree&&row.highs.objective_match===false);});
  const availability=Object.entries(state.benchmarkAvailability).filter(([name])=>name!=="sovereign").map(([name,info])=>`<span class="${info.available?"pass":"bench-muted"}" title="${escapeHtml(info.reason||"")}">${escapeHtml(benchComparatorName(name))}</span>`).join("");
  const reportMeta=active.metadata||{};
  const rowStatus=(row,key)=>{if(key==="sovereign")return row.status||"—";if(key==="highs"&&row.highs)return row.highs.reference_solver_status||"—";return state.benchmarkAvailability[key]?.available?"—":state.benchmarkAvailability[key]?.status||"NOT AVAILABLE";};
  host.innerHTML=`<div class="bench-toolbar"><div><p class="eyebrow">SOLVER AVAILABILITY</p><div class="bench-availability"><span class="pass">Sovereign · Available</span>${availability}</div></div><div class="bench-compare-controls"><label class="bench-time-label">TIME LIMIT (SECONDS)<input id="benchmarkTimeLimit" class="control" type="number" min="1" max="3600" value="60"></label><input id="benchmarkCompareFile" type="file" accept=".mps,.json,.txt,.qplib" hidden><button id="compareBenchmarkButton" class="button primary">COMPARE SOLVERS <span>→</span></button></div></div><p class="bench-fairness">Same model <i>•</i> Same hardware <i>•</i> Same wall-clock time limit</p><div id="benchmarkCompareStatus" class="bench-compare-status" aria-live="polite"></div>${renderBenchmarkComparison()}<div class="benchmark-tabs">${suiteOrder.map(name=>`<button class="${name===active.dataset?"active":""}" data-dataset="${name}">${escapeHtml(labels[name])}</button>`).join("")}</div><span class="type-badge bench-type-badge">${escapeHtml(active.dataset==="miplib"?"MILP":active.dataset==="qplib"?"QP":"LP")}</span><div class="benchmark-summary"><div class="bench-stat"><p>TOTAL INSTANCES</p><strong>${allRows.length}</strong></div><div class="bench-stat"><p>VERIFIED SOLUTIONS</p><strong class="pass">${verified} / ${allRows.length}</strong></div><div class="bench-stat"><p>TIMEOUTS</p><strong class="${timeouts?"warning":"bench-muted"}">${timeouts}</strong></div><div class="bench-stat"><p>FAILED / UNSUPPORTED</p><strong class="${failures?"bad":"bench-muted"}">${failures}</strong></div><div class="bench-stat"><p>OBJECTIVE AGREEMENT</p><strong>${agreements.length?`${agreementCount} / ${agreements.length}`:"—"}</strong></div></div><div class="bench-record-layout"><div class="bench-record-list panel"><div class="bench-table-tools"><input id="benchmarkSearch" class="control" placeholder="Search benchmark instance…" value="${escapeHtml(query)}"><select id="benchmarkFilter" class="select"><option value="all" ${filter==="all"?"selected":""}>All</option><option value="verified" ${filter==="verified"?"selected":""}>Verified</option><option value="timeout" ${filter==="timeout"?"selected":""}>Timeout</option><option value="failed" ${filter==="failed"?"selected":""}>Failed</option><option value="mismatch" ${filter==="mismatch"?"selected":""}>Objective mismatch</option></select><button id="clearSuiteBenchmarks" class="bench-delete-button" title="Delete generated reports for ${escapeHtml(active.dataset)} only">DELETE ${escapeHtml(active.dataset.toUpperCase())} RESULTS</button></div><div class="benchmark-table-wrap"><table class="data-table bench-instance-table"><thead><tr><th>INSTANCE</th><th>TYPE</th><th>SOVEREIGN</th><th>HiGHS</th><th>GUROBI</th><th>CPLEX</th><th>AGREEMENT</th><th>VERIFY</th><th>DETAILS</th></tr></thead><tbody>${filtered.map(row=>`<tr data-bench-instance="${escapeHtml(row.instance||"")}" class="${row===selected?"selected":""}"><td>${escapeHtml((row.instance||"—").split(/[\\/]/).pop())}</td><td>${escapeHtml(row.problem_type||active.dataset.toUpperCase())}</td><td class="${benchClass(rowStatus(row,"sovereign"))}">${escapeHtml(rowStatus(row,"sovereign"))}${row.status==="OPTIMAL"&&row.solve_time_ms!==null?` · ${duration(row.solve_time_ms)}`:""}</td><td class="${benchClass(rowStatus(row,"highs"))}">${escapeHtml(rowStatus(row,"highs"))}</td><td class="bench-muted">${escapeHtml(rowStatus(row,"gurobi"))}</td><td class="bench-muted">${escapeHtml(rowStatus(row,"cplex"))}</td><td class="${row.highs?.objective_match===true?"pass":row.highs?.objective_match===false?"warning":"bench-muted"}">${row.highs?.objective_match===true?"Agree":row.highs?.objective_match===false?"Mismatch":"—"}</td><td class="${row.verification_pass===true?"pass":row.verification_pass===false?"bad":"bench-muted"}">${row.verification_pass===true?"PASS":row.verification_pass===false?"FAIL":"N/A"}</td><td><button class="bench-detail-button" data-bench-open="${escapeHtml(row.instance||"")}">DETAILS</button></td></tr>`).join("")||`<tr><td colspan="9">${allRows.length?"No instances match this search/filter.":"No recorded benchmark results for this suite."}</td></tr>`}</tbody></table></div></div><article class="bench-selected panel">${selected?`<p class="eyebrow">SELECTED INSTANCE</p><h3>${escapeHtml((selected.instance||"—").split(/[\\/]/).pop())}</h3><span class="type-badge">${escapeHtml(selected.problem_type||active.dataset.toUpperCase())} · ${escapeHtml(active.dataset.toUpperCase())}</span><div class="bench-size-grid">${[["VARIABLES",selected.variables],["CONSTRAINTS",selected.constraints],["NONZEROS",selected.nonzeros],["SPARSITY",selected.sparsity===null||selected.sparsity===undefined?null:`${number(selected.sparsity,2)}%`]].map(([label,value])=>`<div><span>${label}</span><b>${value===null||value===undefined?"—":typeof value==="number"?number(value,0):escapeHtml(value)}</b></div>`).join("")}</div>${renderHistoricalComparison(selected)}${renderHistoricalSolverDetails(selected)}<div class="bench-timings"><h4>RECORDED TIMINGS</h4>${[["Parse",selected.parse_time_ms],["Presolve",selected.presolve_time_ms],["Solver",selected.solve_time_ms],["Postsolve",selected.postsolve_time_ms],["Verification",selected.verification_time_ms],["Total backend",selected.total_time_ms]].map(([label,value])=>`<div><span>${label}</span><b>${duration(value)}</b></div>`).join("")}</div><p class="file-meta">Suite record timestamp: ${escapeHtml(reportMeta.timestamp_utc||"—")}</p>${selected.failure_reason?`<details class="bench-failure"><summary>FAILURE / LIMIT DETAIL</summary><pre>${escapeHtml(selected.failure_reason)}</pre></details>`:""}`:`<h3>No instance selected</h3>`}</article></div><p class="file-meta">Objective: the value optimized by the solver. Gap: distance from the MILP incumbent to its best bound. Nodes: branch-and-bound search nodes. Verification checks a solution against the original model.</p><details class="bench-environment panel"><summary>RECORDED BENCHMARK ENVIRONMENT</summary><div class="bench-environment-grid">${[["CPU",reportMeta.cpu_model],["GPU",reportMeta.gpu_info?.device],["CUDA",reportMeta.gpu_info?.runtime],["RAM",reportMeta.ram_bytes?`${number(reportMeta.ram_bytes/1073741824,1)} GB`:null],["OS",[reportMeta.system,reportMeta.release].filter(Boolean).join(" ")],["Timestamp",reportMeta.timestamp_utc]].map(([label,value])=>`<div><span>${label}</span><b>${escapeHtml(value??"—")}</b></div>`).join("")}</div></details>`;
  $$('[data-dataset]',host).forEach(button=>button.addEventListener("click",()=>{state.benchmarkSelectedInstance=null;renderBenchmarks(button.dataset.dataset);}));
  $$('[data-bench-open]',host).forEach(button=>button.addEventListener("click",()=>{state.benchmarkSelectedInstance={suite:active.dataset,instance:button.dataset.benchOpen};renderBenchmarks(active.dataset);}));
  const search=host.querySelector("#benchmarkSearch"),filterSelect=host.querySelector("#benchmarkFilter");if(search)search.addEventListener("input",()=>{const start=search.selectionStart;renderBenchmarks(active.dataset);const next=host.querySelector("#benchmarkSearch");next?.focus();next?.setSelectionRange(start,start);});if(filterSelect)filterSelect.addEventListener("change",()=>renderBenchmarks(active.dataset));
  host.querySelector("#compareBenchmarkButton")?.addEventListener("click",()=>host.querySelector("#benchmarkCompareFile")?.click());
  host.querySelector("#benchmarkCompareFile")?.addEventListener("change",event=>{const file=event.target.files?.[0];if(file)runBenchmarkComparison(file);});
  host.querySelector("#clearBenchCompare")?.addEventListener("click",()=>{state.benchmarkComparison=null;renderBenchmarks(active.dataset);});
  host.querySelector("#clearSuiteBenchmarks")?.addEventListener("click",async event=>{const button=event.currentTarget;if(!window.confirm(`Delete saved ${active.dataset.toUpperCase()} benchmark result reports? The original benchmark input files will not be deleted.`))return;button.disabled=true;try{const deleted=await api(`/api/benchmarks/${encodeURIComponent(active.dataset)}`,{method:"DELETE"});const refreshed=await api("/api/benchmarks");state.benchmarkData=refreshed.datasets||[];state.benchmarkAvailability=refreshed.comparators||state.benchmarkAvailability;state.benchmarkComparison=null;state.benchmarkSelectedInstance=null;renderBenchmarks(active.dataset);toast(`Deleted ${number(deleted.deleted_count,0)} saved report files.`);}catch(error){button.disabled=false;toast(`Could not delete saved reports: ${error.message}`);}});
}
async function runBenchmarkComparison(file) {
  const host=$("#benchmarkArea"),status=host.querySelector("#benchmarkCompareStatus"),button=host.querySelector("#compareBenchmarkButton"),limit=Number(host.querySelector("#benchmarkTimeLimit")?.value||60);
  if(!Number.isInteger(limit)||limit<1||limit>3600){toast("Choose a wall-clock limit from 1 to 3600 seconds.");return;}
  if(status)status.textContent=`Preparing ${file.name} and running available solvers…`;if(button){button.disabled=true;button.textContent="COMPARING SOLVERS…";}
  try{const form=new FormData();form.append("file",file);form.append("time_limit_seconds",String(limit));state.benchmarkComparison=await api("/api/benchmarks/compare",{method:"POST",body:form});renderBenchmarks();}
  catch(error){if(status)status.textContent=`Comparison failed: ${error.message}`;toast(error.message);}
}

window.addEventListener("resize",()=>state.analysis&&renderMatrix());
bindUpload(); boot();
