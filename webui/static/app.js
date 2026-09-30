const state = { jobId: null, analysis: null, device: null, capabilities: null, benchmarkData: [], result: null, runningTimer: null, runningStarted: null, runningConfiguration: null };
const $ = (selector, root = document) => root.querySelector(selector);
const $$ = (selector, root = document) => [...root.querySelectorAll(selector)];
const nf = new Intl.NumberFormat("en-US", { maximumFractionDigits: 2 });

function escapeHtml(value = "") { return String(value).replace(/[&<>'"]/g, c => ({"&":"&amp;","<":"&lt;",">":"&gt;","'":"&#39;","\"":"&quot;"}[c])); }
function number(value, digits = 2) { return value === null || value === undefined || Number.isNaN(Number(value)) ? "—" : new Intl.NumberFormat("en-US", {maximumFractionDigits: digits}).format(Number(value)); }
function milliseconds(value) { return value === null || value === undefined ? "—" : `${number(value, 2)} ms`; }
function seconds(value) { return value === null || value === undefined ? "N/A" : `${number(value, 2)} s`; }
function timeLimitLabel(value) { const limit=Number(value); return Number.isFinite(limit) && limit > 0 ? `${number(limit,0)} seconds` : "No time limit"; }
function fileSize(bytes) { return bytes < 1024 * 1024 ? `${number(bytes / 1024, 1)} KB` : `${number(bytes / 1024 / 1024, 2)} MB`; }
function statusClass(status) { const text = (status || "").toUpperCase(); return text.includes("OPTIMAL") || text === "FEASIBLE" ? "pass" : text.includes("TIME") || text.includes("ITERATION") || text.includes("NODE_LIMIT") || text.includes("MEMORY_LIMIT") || text.includes("UNSUPPORTED") ? "warning" : "bad"; }
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
  return [{v:"auto",t:"Auto (Revised Simplex)"},{v:"revised-simplex",t:"Revised Simplex"},{v:"dual-simplex",t:"Dual Simplex"},{v:"ipm",t:"Mehrotra Interior Point"}];
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
        <div id="autoDecision" class="auto-decision"><div class="decision-row done"><span>MODEL SIZE</span><b>${escapeHtml(policy.model_size || "—")} · score ${number(policy.complexity_score, 0)}</b></div><div class="decision-row done"><span>TIME LIMIT</span><b>${timeLimitLabel(policy.time_limit_seconds)}</b></div>${a.problem_type === "MILP" ? `<div class="decision-row done"><span>MAX B&B NODES</span><b>${number(policy.max_nodes, 0)}</b></div><div class="decision-row done"><span>MAX LP ITERATIONS</span><b>${number(policy.max_iterations, 0)}</b></div>` : `<div class="decision-row done"><span>MAXIMUM ITERATIONS</span><b>${number(policy.max_iterations, 0)}</b></div>`}<div class="decision-row done"><span>SOLVER · BACKEND</span><b>${escapeHtml(policy.method || "—")} · ${escapeHtml(policy.backend || "—")}</b></div><details class="selection-why"><summary>WHY?</summary><p>${escapeHtml(policy.reason || "Automatic policy is being prepared.")}</p><p>${escapeHtml(policy.backend_reason || "")}</p></details></div>
        <details class="advanced expert-mode"><summary>ADVANCED / EXPERT MODE</summary><div class="config-list"><div class="config-item"><label>SOLVER METHOD</label><select id="methodSelect" class="select">${methods}</select></div><div class="config-item"><label>COMPUTE BACKEND</label><select id="backendSelect" class="select"><option value="auto">AUTO</option><option value="cpu">CPU</option><option value="cuda" ${state.device?.cuda_available ? "" : "disabled"}>CUDA${state.device?.cuda_available ? "" : " · unavailable"}</option></select></div><div class="toggle-row"><span>PRESOLVE</span><label class="switch"><input id="presolveToggle" type="checkbox" checked><i class="slider"></i></label></div>${a.problem_type === "MILP" ? `<div class="config-item"><label>MAX B&B NODES <span class="muted">(0 = unlimited)</span></label><input id="nodeInput" class="control" type="number" min="0" max="10000000" value="${policy.max_nodes || 10000}"></div><div class="config-item"><label>MAX LP ITERATIONS <span class="muted">(0 = unlimited)</span></label><input id="iterationInput" class="control" type="number" min="0" max="10000000" value="${policy.max_iterations || 10000}"></div><p class="file-meta">The node limit controls search breadth. The LP iteration limit applies separately to each node relaxation.</p>` : `<div class="config-item"><label>MAX ITERATIONS <span class="muted">(0 = unlimited)</span></label><input id="iterationInput" class="control" type="number" min="0" max="10000000" value="${policy.max_iterations || 10000}"></div><p class="file-meta">Unlimited iterations may result in very long execution times.</p>`}<div class="config-item"><label>TIME LIMIT (SECONDS) <span class="muted">(0 = no limit)</span></label><input id="timeInput" class="control" type="number" min="0" max="3600" value="${policy.time_limit_seconds ?? 0}"></div><button id="expertRunButton" class="run-button">RUN WITH EXPERT SETTINGS <span>→</span></button></div></details>
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
  host.innerHTML = `<div class="decision-row done"><span>MODEL DETECTED</span><b>${escapeHtml(result.analysis.problem_type)} · ${escapeHtml(selection.model_size || "—")}</b></div><div class="decision-row done"><span>PRESOLVE</span><b>${pre.before_variables ?? "—"} vars / ${pre.before_constraints ?? "—"} rows → ${pre.after_variables ?? "—"} vars / ${pre.after_constraints ?? "—"} rows</b></div><div class="decision-row done"><span>EXECUTION LIMITS</span><b>${timeLimitLabel(result.configuration.time_limit_seconds)} · ${result.analysis.problem_type === "MILP" ? `${number(result.configuration.max_nodes,0)} B&B nodes · ${number(result.configuration.max_iterations,0)} LP iterations/node` : `${number(result.configuration.max_iterations,0)} iterations`}</b></div><div class="decision-row done"><span>SOLVER SELECTED</span><b>${escapeHtml(automation.final_method || selection.method || "—")}</b></div><div class="decision-row done"><span>COMPUTE BACKEND</span><b>${escapeHtml(automation.final_backend || selection.backend || "—")}</b></div><div class="decision-row ${result.verification === "PASS" ? "done" : "warning"}"><span>ORIGINAL-MODEL VERIFICATION</span><b>${escapeHtml(result.verification)}</b></div><details class="selection-why"><summary>WHY WAS THIS METHOD SELECTED?</summary><p>${escapeHtml(selection.reason || "Expert settings were supplied.")}</p><p>${escapeHtml(selection.backend_reason || "")}</p>${attempts.length > 1 ? `<p>Attempts: ${attempts.map(attempt => `${escapeHtml(attempt.method)} (${escapeHtml(attempt.status)})`).join(" → ")}</p>` : ""}</details>`;
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
  const timingRows = [["Parsing",timings.parse_time_ms],["Presolve",timings.presolve_time_ms],["Solving",timings.solver_time_ms],["Postsolve",timings.postsolve_time_ms],["Verification",timings.verification_time_ms]].filter(row=>row[1]!==null && row[1]!==undefined);
  const largest=Math.max(...timingRows.map(row=>row[1]),1);
  const reduction = pre.before_constraints && pre.after_constraints !== null ? Math.max(0,(1-pre.after_constraints/pre.before_constraints)*100) : null;
  const limited = String(result.status).toUpperCase().includes("TIME") || String(result.status).toUpperCase().includes("ITERATION") || String(result.status).toUpperCase().includes("NODE_LIMIT") || String(result.status).toUpperCase().includes("MEMORY_LIMIT");
  const processedModel = limited ? `<article class="panel presolve-card"><p class="eyebrow">MODEL DATA COMPLETED BEFORE SOLVER STOPPED</p><h3>PARSED AND PRESOLVED MODEL</h3><p class="file-meta">No candidate solution was returned before the solver stopped. These are real model and presolve values recorded before solving.</p><div class="stat-grid">${stat("ORIGINAL VARIABLES",number(result.analysis.variables,0))}${stat("ORIGINAL CONSTRAINTS",number(result.analysis.constraints,0))}${stat("MATRIX NON-ZEROS",number(result.analysis.nonzeros,0))}${stat("MATRIX SPARSITY",`${number(result.analysis.sparsity,2)}%`)}${stat("POST-PRESOLVE VARIABLES",pre.after_variables === null || pre.after_variables === undefined ? "N/A" : number(pre.after_variables,0))}${stat("POST-PRESOLVE CONSTRAINTS",pre.after_constraints === null || pre.after_constraints === undefined ? "N/A" : number(pre.after_constraints,0))}${stat("MEASURED REDUCTIONS",pre.reductions === null || pre.reductions === undefined ? "N/A" : number(pre.reductions,0))}${stat("MODEL TYPE",escapeHtml(result.analysis.problem_type))}${stat("PARSING COMPLETED",milliseconds(timings.parse_time_ms))}${stat("PRESOLVE COMPLETED",milliseconds(timings.presolve_time_ms))}${stat("SELECTED METHOD",escapeHtml(configuration.method || metrics.method || "N/A"))}${stat("SELECTED BACKEND",escapeHtml(configuration.backend || metrics.backend || "N/A"))}</div></article>` : "";
  $("#resultArea").innerHTML = `<div class="result-area">
    <article class="panel pipeline">${["UPLOADED","PARSING","VALIDATING","CLASSIFYING","PRESOLVING","SELECTING","SOLVING","POSTSOLVING","VERIFYING","COMPLETE"].map(stage=>`<span class="stage done">${stage}</span>`).join("")}</article>
    <article class="panel result-top"><div class="result-status"><span class="status-orb ${cls}">${symbol}</span><div><h2 class="${headlineClass}">${escapeHtml(headline)}</h2><p>${escapeHtml(statusExplanation(result))}</p></div></div><div class="result-kpis"><div><label>OBJECTIVE</label><strong>${number(metrics.objective,6)}</strong></div><div><label>VERIFICATION</label><strong class="${result.verification === "PASS" ? "pass" : "warning"}">${escapeHtml(result.verification)}</strong></div><div><label>SOLVER TIME</label><strong>${milliseconds(timings.solver_time_ms)}</strong></div></div></article>
    ${processedModel}
    <div class="result-grid"><article class="panel timing-card"><h3>PERFORMANCE</h3>${timingRows.map(([label,value])=>`<div class="timing-row"><span>${label}</span><div class="timing-bar"><i style="width:${Math.max(3,value/largest*100)}%"></i></div><b>${milliseconds(value)}</b></div>`).join("")}<div class="timing-row"><span>Backend Total</span><b>${milliseconds(timings.backend_total_time_ms)}</b></div><p class="solver-highlight">Backend time measures the C++ solver process and excludes browser rendering.</p></article>
    <article class="panel verify-card"><h3>INDEPENDENT SOLUTION VERIFICATION</h3><div class="verify-stack"><div class="verify-row"><span>Original model verification</span><b class="${result.verification === "PASS"?"pass":"warning"}">${escapeHtml(result.verification)}</b></div>${verificationRow("Maximum constraint violation",metrics.feasibility)}${verificationRow("Primal residual",metrics.primal_residual)}${verificationRow("Dual residual",metrics.dual_residual)}${verificationRow("Integrality",result.analysis.problem_type==="MILP" ? result.verification : null)}${verificationRow("Objective consistency",metrics.objective !== null ? result.verification : null)}</div></article></div>
    ${limited ? `<article class="panel presolve-card"><h3>${escapeHtml(humanStatus(result.status))}</h3><div class="stat-grid">${stat("CONFIGURED TIME LIMIT",`${number(configuration.time_limit_seconds,0)} s`)}${stat("ELAPSED TIME",seconds(timings.backend_total_time_ms === null ? null : timings.backend_total_time_ms / 1000))}${result.analysis.problem_type === "MILP" ? stat("MAX B&B NODES",configuration.max_nodes === 0 ? "Unlimited" : number(configuration.max_nodes,0)) : stat("MAXIMUM ITERATIONS",number(configuration.max_iterations,0))}${result.analysis.problem_type === "MILP" ? stat("MAX LP ITERATIONS / NODE",configuration.max_iterations === 0 ? "Unlimited" : number(configuration.max_iterations,0)) : ""}${result.analysis.problem_type === "MILP" ? stat("NODES PROCESSED",metrics.nodes_processed === null || metrics.nodes_processed === undefined ? "N/A" : number(metrics.nodes_processed,0)) : stat("ITERATIONS COMPLETED",metrics.iterations === null || metrics.iterations === undefined ? "N/A" : number(metrics.iterations,0))}${stat("LAST OBJECTIVE",metrics.objective === null || metrics.objective === undefined ? "N/A" : number(metrics.objective,6))}${stat("BACKEND",escapeHtml(configuration.backend || metrics.backend || "N/A"))}</div></article>` : ""}
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

function renderBenchmarks() {
  const host=$("#benchmarkArea"), datasets=state.benchmarkData;if(!datasets.length){host.innerHTML="No local benchmark reports exist yet. Run the repository benchmark runner to populate this page with measured results.";return;}
  const choose=(name)=>{const active=datasets.find(data=>data.dataset===name)||datasets[0];const rows=active.instances||[];const verified=rows.filter(row=>["OPTIMAL","FEASIBLE"].includes(row.status)&&row.verification_pass===true).length;const failures=rows.filter(row=>!["OPTIMAL","FEASIBLE"].includes(row.status)).length;host.innerHTML=`<div class="benchmark-tabs">${datasets.map(data=>`<button class="${data===active?"active":""}" data-dataset="${escapeHtml(data.dataset)}">${escapeHtml(data.dataset.toUpperCase())}</button>`).join("")}</div><div class="benchmark-summary"><div class="bench-stat"><p>INSTANCES</p><strong>${rows.length}</strong></div><div class="bench-stat"><p>VERIFIED SUCCESS</p><strong class="pass">${verified}/${rows.length}</strong></div><div class="bench-stat"><p>NON-SUCCESS</p><strong class="${failures?"warning":"pass"}">${failures}</strong></div></div><div class="benchmark-table-wrap"><table class="data-table"><thead><tr><th>INSTANCE</th><th>STATUS</th><th>VERIFY</th><th>ITERATIONS / NODES</th><th>SOLVER TIME</th></tr></thead><tbody>${rows.map(row=>`<tr><td>${escapeHtml((row.instance||"—").split(/[\\/]/).pop())}</td><td class="${statusClass(row.status)}">${escapeHtml(row.status||"—")}</td><td class="${row.verification_pass?"pass":"warning"}">${row.verification_pass===true?"PASS":row.verification_pass===false?"FAIL":"N/A"}</td><td>${number(row.iterations ?? row.nodes_processed,0)}</td><td>${milliseconds(row.solve_time_ms)}</td></tr>`).join("")}</tbody></table></div>`;$$("[data-dataset]",host).forEach(button=>button.addEventListener("click",()=>choose(button.dataset.dataset)));};choose(datasets[0].dataset);
}

window.addEventListener("resize",()=>state.analysis&&renderMatrix());
bindUpload(); boot();
