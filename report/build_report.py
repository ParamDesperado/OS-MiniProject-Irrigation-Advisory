#!/usr/bin/env python3
"""
Build the consolidated project report (HTML -> PDF).
All terminal outputs are inserted verbatim from report/evidence/outputs/run_logs,
which were captured on Ubuntu 24.04 (GitHub Codespaces) by scripts/run_all.sh.

usage: python report/build_report.py            (writes report/OS_UseCase_Report.html)
PDF:   msedge --headless --print-to-pdf=report/OS_UseCase_Report.pdf report/OS_UseCase_Report.html
"""
import base64
import html
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
LOGS = os.path.join(HERE, "evidence", "outputs", "run_logs")
FIGS = os.path.join(HERE, "figures")
REPO = "https://github.com/ParamDesperado/OS-MiniProject-Irrigation-Advisory"

TEAM = [("Param Sangani", "16010424108"), ("Ankit Vishwakarma", "16010424120"), ("Aarya Thota", "16010424115")]
BATCH = "B2"


def log(name):
    with open(os.path.join(LOGS, name), encoding="utf-8") as f:
        return f.read()


def section(text, start, end=None):
    """Lines from the first line containing `start` up to (not incl.) the next line containing `end`."""
    lines = text.splitlines()
    out, on = [], False
    for ln in lines:
        if not on and start in ln:
            on = True
        elif on and end and end in ln:
            break
        if on:
            out.append(ln)
    return "\n".join(out)


def term(text, caption):
    text = re.sub(r"\x1b\[[0-9;]*m", "", text).rstrip()
    return (f'<figure class="term"><div class="bar"><span></span><span></span><span></span>'
            f'<em>codespace@ubuntu-24.04: ~/OS-MiniProject-Irrigation-Advisory</em></div>'
            f'<pre>{html.escape(text)}</pre><figcaption>{caption}</figcaption></figure>')


def img(name, caption, width="100%"):
    with open(os.path.join(FIGS, name), "rb") as f:
        b64 = base64.b64encode(f.read()).decode()
    return (f'<figure><img src="data:image/png;base64,{b64}" style="width:{width}">'
            f'<figcaption>{caption}</figcaption></figure>')


def code(text):
    return f"<pre class='code'>{html.escape(text.strip())}</pre>"


SHOTS = os.path.join(HERE, "..", "docs", "screenshots")


def shot(name, caption):
    with open(os.path.join(SHOTS, name), "rb") as f:
        b64 = base64.b64encode(f.read()).decode()
    return (f'<figure class="shot"><img src="data:image/png;base64,{b64}">'
            f'<figcaption>{caption}</figcaption></figure>')


exp4 = log("exp4_pipeline.txt")
exp6m = log("exp6_measured.txt")
exp6b = log("exp6_batch.txt")
exp7s = log("exp7_safe.txt")
exp7u = log("exp7_unsafe.txt")
exp9 = log("exp9_banker.txt")
tests = log("tests.txt")
metrics = log("model_metrics.txt")
report_txt = log("advisory_report.txt")
ps_tree = log("ps_tree.txt")
stage_csv = log("stage_times.csv")
demand = log("pump_demand.csv")


# ---- key numbers, read from the logs so the text always matches the outputs ----
def table(text):
    rows = {}
    for ln in section(text, "Algorithm", "Lowest").splitlines()[1:]:
        f = ln.split()
        if len(f) == 6:
            rows[f[0]] = dict(wt=float(f[1]), tat=float(f[2]), rt=float(f[3]), sw=int(f[4]))
    return rows


def num(pattern, text, cast=float):
    return cast(re.search(pattern, text).group(1))


SM, SB = table(exp6m), table(exp6b)
red_sjf = 100 * (SM["FCFS"]["wt"] - SM["SJF"]["wt"]) / SM["FCFS"]["wt"]
red_srtf = 100 * (SM["FCFS"]["wt"] - SM["SRTF"]["wt"]) / SM["FCFS"]["wt"]
red_bsjf = 100 * (SB["FCFS"]["wt"] - SB["SJF"]["wt"]) / SB["FCFS"]["wt"]
P_WALL = num(r"total wall ([\d.]+) ms", exp4)
P_CPU = num(r"total child CPU ([\d.]+) ms", exp4)
BIG_BURST = max(float(l.split(",")[6]) for l in stage_csv.splitlines()[1:])
S_FULL = num(r"FULL events \(producer blocked\) : (\d+)", exp7s, int)
S_EMPTY = num(r"EMPTY events \(consumer blocked\): (\d+)", exp7s, int)
S_TPUT = num(r"throughput (\d+) readings/s", exp7s, int)
U_VALID = num(r"valid readings counted\s+\d+\s+(\d+)", exp7u, int)
U_LOST = num(r"(\d+) updates LOST", exp7u, int)
PPID = int(re.search(r"^\s*(\d+)\s+\d+\s+\S+\s+\./bin/pipeline", ps_tree, re.M).group(1))


ARCH_SVG = """
<svg viewBox="0 0 760 430" xmlns="http://www.w3.org/2000/svg" font-family="Segoe UI, Arial" font-size="11">
 <defs><marker id="a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto">
  <path d="M0,0 L10,5 L0,10 z" fill="#475569"/></marker></defs>
 <style>.b{fill:#fff;stroke:#334155;stroke-width:1.2;rx:6}.d{fill:#eff6ff;stroke:#2563eb}.p{fill:#f0fdf4;stroke:#16a34a}
  .o{fill:#fff7ed;stroke:#ea580c}.s{fill:#faf5ff;stroke:#7c3aed}.t{fill:#0f172a}.m{fill:#475569;font-size:10px}
  .l{stroke:#475569;stroke-width:1.2;fill:none;marker-end:url(#a)}.h{font-weight:600;font-size:12px}</style>
 <!-- data -->
 <rect class="b d" x="10" y="40" width="130" height="92"/>
 <text class="t h" x="20" y="58">data/raw (synthetic)</text>
 <text class="m" x="20" y="76">plots.csv (600)</text><text class="m" x="20" y="91">sensor_readings.csv</text>
 <text class="m" x="20" y="106">weather_forecast.csv</text><text class="m" x="20" y="121">training_history.csv</text>
 <rect class="b d" x="10" y="160" width="130" height="50"/>
 <text class="t h" x="20" y="180">train_model.py</text><text class="m" x="20" y="197">ridge + logistic → weights</text>
 <!-- pipeline parent -->
 <rect class="b p" x="170" y="10" width="420" height="300" style="fill:#f8fafc"/>
 <text class="t h" x="180" y="28">EXP 4 · bin/pipeline (parent) — fork → execv → wait4</text>
 <rect class="b p" x="185" y="45" width="120" height="40"/><text class="t" x="195" y="63">acquire sensors</text><text class="m" x="195" y="77">open/read/write</text>
 <rect class="b p" x="315" y="45" width="120" height="40"/><text class="t" x="325" y="63">acquire weather</text><text class="m" x="325" y="77">child process</text>
 <text class="m" x="450" y="68">Stage 1 (concurrent)</text>
 <rect class="b" x="185" y="100" width="290" height="26" style="fill:#f1f5f9"/><text class="m" x="195" y="117">data/processed/ sensor_features.csv, weather_features.csv</text>
 <g><rect class="b p" x="185" y="142" width="74" height="40"/><text class="t" x="191" y="160">predict</text><text class="m" x="191" y="174">Sameerwadi</text>
 <rect class="b p" x="265" y="142" width="74" height="40"/><text class="t" x="271" y="160">predict</text><text class="m" x="271" y="174">Mudhol</text>
 <rect class="b p" x="345" y="142" width="74" height="40"/><text class="t" x="351" y="160">predict</text><text class="m" x="351" y="174">Jamkhandi</text>
 <rect class="b p" x="425" y="142" width="74" height="40"/><text class="t" x="431" y="160">predict</text><text class="m" x="431" y="174">Rabkavi</text>
 <rect class="b p" x="505" y="142" width="74" height="40"/><text class="t" x="511" y="160">predict</text><text class="m" x="511" y="174">Terdal</text></g>
 <text class="m" x="235" y="198">Stage 2: one child per village, in parallel · ML + Monte-Carlo</text>
 <rect class="b p" x="185" y="212" width="160" height="40"/><text class="t" x="195" y="230">advise (Stage 3)</text><text class="m" x="195" y="244">EN + HI advisories</text>
 <rect class="b" x="360" y="212" width="220" height="40" style="fill:#f1f5f9"/><text class="m" x="368" y="229">logs/stage_times.csv  (pid, wall, CPU</text><text class="m" x="368" y="243">from rusage) · logs/pipeline.log</text>
 <rect class="b o" x="185" y="265" width="395" height="36"/><text class="t" x="195" y="282">outputs/ advisories.csv · advisory_report.txt · pump_demand.csv</text><text class="m" x="195" y="295">backups/ previous advisory copied read-only (0440)</text>
 <path class="l" d="M140,86 L183,65"/><path class="l" d="M140,185 L183,162"/>
 <path class="l" d="M245,85 L245,100"/><path class="l" d="M310,126 L310,142"/><path class="l" d="M222,182 L222,212"/>
 <!-- consumers -->
 <rect class="b s" x="610" y="110" width="140" height="80"/><text class="t h" x="620" y="128">EXP 6 · scheduler</text>
 <text class="m" x="620" y="145">FCFS · SJF · SRTF</text><text class="m" x="620" y="160">RR q=2,5,10</text><text class="m" x="620" y="175">Gantt, avg WT/TAT</text>
 <path class="l" d="M580,226 L596,226 L596,150 L608,150"/><text class="m" x="602" y="206">bursts</text>
 <rect class="b s" x="610" y="215" width="140" height="95"/><text class="t h" x="620" y="233">EXP 9 · banker</text>
 <text class="m" x="620" y="250">pumps · power slots</text><text class="m" x="620" y="265">valves · fert units</text><text class="m" x="620" y="280">safe / unsafe / wait</text><text class="m" x="620" y="295">max = pump_demand</text>
 <path class="l" d="M580,283 L608,283"/>
 <!-- exp 7 -->
 <rect class="b s" x="10" y="330" width="740" height="90" style="fill:#faf5ff"/>
 <text class="t h" x="20" y="348">EXP 7 · bin/sensor_sync — Producer–Consumer with POSIX semaphores + pthread mutex</text>
 <rect class="b" x="30" y="360" width="150" height="46"/><text class="t" x="40" y="378">5 producer threads</text><text class="m" x="40" y="393">village sensor gateways</text>
 <rect class="b" x="250" y="360" width="200" height="46"/><text class="t" x="260" y="378">bounded buffer (8 slots)</text><text class="m" x="260" y="393">sem empty / sem full / buf_mutex</text>
 <rect class="b" x="520" y="360" width="210" height="46"/><text class="t" x="530" y="378">3 consumer threads → plot table</text><text class="m" x="530" y="393">table_mutex (removed in --unsafe)</text>
 <path class="l" d="M180,383 L248,383"/><path class="l" d="M450,383 L518,383"/>
 <path class="l" d="M140,122 L155,122 L155,328"/><text class="m" x="60" y="322">sensor stream</text>
</svg>"""

CSS = """
@page { size: A4; margin: 16mm 15mm 16mm 15mm; }
:root { --ink:#0f172a; --muted:#475569; --line:#cbd5e1; --accent:#1d4ed8; --soft:#f1f5f9; }
* { box-sizing: border-box; }
body { font-family: "Segoe UI", Calibri, Arial, sans-serif; color: var(--ink); font-size: 10.5pt; line-height: 1.45; margin: 0; background:#fff; }
h1 { font-size: 20pt; margin: 0 0 4pt; color: var(--accent); }
h2 { font-size: 14.5pt; color: var(--accent); border-bottom: 2px solid var(--accent); padding-bottom: 3pt; margin: 18pt 0 8pt; break-after: avoid; }
h3 { font-size: 11.5pt; margin: 12pt 0 5pt; break-after: avoid; }
p, li { text-align: justify; }
table { border-collapse: collapse; width: 100%; margin: 6pt 0 10pt; font-size: 9.5pt; break-inside: avoid; }
th, td { border: 1px solid var(--line); padding: 4pt 6pt; text-align: left; vertical-align: top; }
th { background: var(--soft); }
.cover { height: 260mm; display: flex; flex-direction: column; justify-content: center; text-align: center; break-after: page; }
.cover .inst { font-size: 12pt; color: var(--muted); letter-spacing: .5px; }
.cover h1 { font-size: 24pt; margin: 18pt 0 6pt; }
.cover .sub { font-size: 13pt; color: var(--muted); margin-bottom: 22pt; }
.cover table { width: 75%; margin: 10pt auto; font-size: 11pt; }
.pill { display:inline-block; border:1px solid var(--accent); color:var(--accent); border-radius: 10pt; padding: 2pt 9pt; margin: 2pt; font-size: 10pt; }
.page { break-before: page; }
figure { margin: 8pt 0 12pt; break-inside: avoid; text-align: center; }
figure.shot img { width: 100%; border-radius: 6px; box-shadow: 0 1px 4px rgba(0,0,0,.25); }
figcaption { font-size: 9pt; color: var(--muted); margin-top: 3pt; text-align: center; }
.term { background: #0b1220; border-radius: 6px; overflow: hidden; text-align: left; }
.term .bar { background: #1e293b; padding: 4pt 8pt; display: flex; gap: 4pt; align-items: center; }
.term .bar span { width: 8px; height: 8px; border-radius: 50%; background: #ef4444; display:inline-block; }
.term .bar span:nth-child(2) { background: #f59e0b; } .term .bar span:nth-child(3) { background: #22c55e; }
.term .bar em { color: #94a3b8; font-size: 8pt; font-style: normal; margin-left: 6pt; }
.term pre { color: #e2e8f0; font-family: Consolas, "Cascadia Mono", monospace; font-size: 7.6pt; line-height: 1.3; margin: 0; padding: 7pt 9pt; white-space: pre-wrap; }
.term figcaption { background: #fff; padding: 4pt; }
pre.code { background: var(--soft); border: 1px solid var(--line); border-radius: 4px; font-family: Consolas, monospace; font-size: 8.4pt; padding: 6pt 8pt; white-space: pre-wrap; break-inside: avoid; }
.note { background: #fefce8; border-left: 4px solid #ca8a04; padding: 6pt 9pt; font-size: 9.5pt; margin: 8pt 0; }
.ok { color: #15803d; font-weight: 600; } .bad { color: #b91c1c; font-weight: 600; }
.qa b { display:block; margin-top: 6pt; }
.toc li { margin: 2pt 0; }
"""

team_rows = "".join(f"<tr><td>{n}</td><td>{r}</td><td>{BATCH}</td></tr>" for n, r in TEAM)

parts = []
P = parts.append

# ---------------------------------------------------------------- cover
P(f"""
<div class="cover">
 <div class="inst">K J SOMAIYA · OPERATING SYSTEMS · AI USE-CASE ASSIGNMENT</div>
 <h1>AI-Based Irrigation Advisory System for Sugarcane</h1>
 <div class="sub">Use case <b>KJS-AGR-01</b> (KIAAR · Godavari Biorefineries) — a Linux prototype integrating four Operating Systems experiments</div>
 <div><span class="pill">Exp 4 · System calls</span><span class="pill">Exp 6 · SJF &amp; RR</span>
      <span class="pill">Exp 7 · Producer–Consumer</span><span class="pill">Exp 9 · Banker's algorithm</span></div>
 <table><tr><th>Student name</th><th>Roll number</th><th>Batch</th></tr>{team_rows}</table>
 <p style="text-align:center">Source code: <a href="{REPO}">{REPO}</a></p>
 <p style="text-align:center;color:#475569">Platform: Ubuntu 24.04.5 LTS (GitHub Codespaces), Linux 6.8, gcc 13.3, Python 3</p>
</div>""")

# ---------------------------------------------------------------- contents + 1-3
P("""
<h2>Contents</h2>
<ol class="toc">
<li>Selected use case and experiments</li><li>Problem statement, objectives and prototype scope</li>
<li>Dataset and synthetic-data generation</li><li>Architecture, tools and AI component</li>
<li>Experiment 4 – File and process handling using system calls</li>
<li>Experiment 6 – Shortest Job First and Round Robin scheduling</li>
<li>Experiment 7 – Producer–Consumer using semaphores and mutex</li>
<li>Experiment 9 – Deadlock avoidance using the Banker's algorithm</li>
<li>Testing and performance comparison</li><li>Limitations, data privacy and improvements</li>
<li>Conclusion, references and AI-assistance acknowledgement</li>
</ol>

<h2>1. Selected Use Case and Experiments</h2>
<table>
<tr><th style="width:28%">Use case</th><td><b>KJS-AGR-01</b> — Irrigation Advisory System for Sugarcane Crop using AI and Sensor-based Technology.
Collaborators: K J Somaiya Institute of Applied Agricultural Research (KIAAR), Sameerwadi, and Godavari Biorefineries Ltd.</td></tr>
<tr><th>Experiments selected</th><td><b>4, 6, 7 and 9</b>. All four are from Experiments 4–10. The requirement is at least two.</td></tr>
<tr><th>Prototype scope (from assignment)</th><td>Process soil moisture, weather and plot records to generate plot-wise irrigation
recommendations. Sensor-reading, prediction and advisory tasks run as cooperating processes on Linux.</td></tr>
</table>
<table>
<tr><th>Exp</th><th>OS concept</th><th>Connection to the use case</th></tr>
<tr><td>4</td><td>fork, exec, wait, open/read/write/close</td><td>The application itself. A parent process forks and execs one child per workflow stage:
acquisition (2 children), AI prediction (1 child per village, in parallel) and advisory generation. All data files are read and written with raw system calls.</td></tr>
<tr><td>6</td><td>SJF and Round Robin</td><td>The 8 processes of the pipeline are scheduled using their <b>measured CPU bursts</b>, which the parent records with <code>wait4()</code>.
The experiment shows which policy suits a single-core field gateway.</td></tr>
<tr><td>7</td><td>Producer–Consumer with semaphores and mutex</td><td>Village sensor-gateway threads stream soil-moisture readings into a bounded buffer. Analytics threads
consume them and update the shared plot-water table. The race condition is shown and then prevented.</td></tr>
<tr><td>9</td><td>Banker's algorithm</td><td>Villages compete for shared pump sets, 3-phase power slots, canal valves and fertigation units. Each village's maximum claim
is the demand computed by the advisory stage.</td></tr>
</table>

<h2>2. Problem Statement, Objectives and Scope</h2>
<p><b>Problem.</b> Sugarcane is a water-intensive crop. In the KIAAR/GBL command area of North Karnataka, about 18,000–25,000 farmers decide when to irrigate
from experience rather than from soil, crop and weather data. The result is over-irrigation or delayed irrigation, which wastes water and electricity
and causes yield loss from moisture stress. Field staff also cannot monitor thousands of spread-out plots by hand.</p>
<p><b>Objectives of this prototype.</b></p>
<ol>
<li>Ingest soil-moisture sensor readings, a 3-day weather forecast and geo-fenced plot records, and validate them by rejecting missing, out-of-range and malformed data.</li>
<li>Predict, for every plot, the <b>irrigation depth</b> needed over the next 3 days and the <b>probability of water stress</b> using trained ML models, with uncertainty bounds.</li>
<li>Produce farmer-friendly, prioritised advisories in English and Hindi (irrigate today / tomorrow / within 3 days / skip because rain is expected), along with pump-hours.</li>
<li>Run the workflow as cooperating Linux processes and threads, and use OS techniques for scheduling, safe sharing of sensor data and deadlock-free allocation of irrigation resources.</li>
</ol>
<p><b>Scope.</b> The project is a software prototype on Linux with synthetic data. IoT hardware, satellite imagery, the mobile app and the LLM advisory layer of the
full use case are outside the scope of this assignment. They are discussed as future work.</p>

<h2>3. Dataset and Synthetic-Data Generation</h2>
<p>Real farmer data is not available to students and is personal data under the DPDP Act. All data is therefore <b>synthetic and clearly labelled</b>. It is generated by
<code>scripts/generate_data.py</code> using only the Python standard library with a fixed seed (42), so every run reproduces it exactly.</p>
<table>
<tr><th>File</th><th>Records</th><th>How it is generated</th></tr>
<tr><td>plots.csv</td><td>600 plots, 5 villages</td><td>Villages: Sameerwadi, Mudhol, Jamkhandi, Rabkavi and Terdal (unequal sizes 180/138/120/90/72). Each plot gets a random anonymised farmer token,
a soil type (black / loam / red, with field capacity and wilting point), area 0.4–3.5 ha, crop age 10–360 days, drip or furrow irrigation, and a pump rating of 3.7, 5.5 or 7.5 kW.</td></tr>
<tr><td>sensor_readings.csv</td><td>100,800</td><td>14 days of readings every 2 h per plot. Soil moisture follows a drying curve whose rate depends on the crop coefficient, plus irrigation refills and sensor noise.
<b>About 1.5 % of rows are deliberately faulty</b>: 524 missing, 495 out-of-range and 523 malformed.</td></tr>
<tr><td>weather_forecast.csv</td><td>15</td><td>A 3-day forecast per village with distinct profiles: Jamkhandi has rain expected, Rabkavi has heat (37 °C).</td></tr>
<tr><td>training_history.csv</td><td>5,000</td><td>Labelled samples. The labels come from a FAO-56 style soil water balance (depletion + crop ET − effective rain, with heat stress and a soil-specific refill target) plus Gaussian noise.
The stress label is drawn from a logistic function of relative available water.</td></tr>
</table>
""")

# ---------------------------------------------------------------- 4 architecture
P(f"""
<h2 class="page">4. Architecture, Tools and AI Component</h2>
<figure>{ARCH_SVG}<figcaption>Figure 1 – Architecture: application data flow and the OS components used (processes, files, threads, shared buffer, synchronisation, resource allocation)</figcaption></figure>
<h3>Tools used</h3>
<table>
<tr><th>Item</th><th>Used</th></tr>
<tr><td>Linux distribution</td><td>Ubuntu 24.04.5 LTS, kernel 6.8.0 (GitHub Codespaces, 2 vCPU). The same code also runs on WSL, a VM or a Raspberry Pi.</td></tr>
<tr><td>Languages / compiler</td><td>C11 with gcc 13.3 (<code>-Wall -Wextra -O2</code>, zero warnings), POSIX threads and semaphores. Python 3 (standard library only) for data generation and training. Bash for scripts.</td></tr>
<tr><td>Libraries</td><td>glibc, libpthread, libm. matplotlib is used only to draw the report charts.</td></tr>
<tr><td>Build / run</td><td>Makefile (<code>make</code>, <code>make test</code>), <code>scripts/run_all.sh</code>, and GitHub Actions CI that builds and tests on Ubuntu after every push.</td></tr>
</table>
{shot("01_build.png", "Screenshot 1 – Linux environment and a clean build of all programs with gcc")}
<h3>AI / ML component</h3>
<p>Ten agronomic features are engineered from the raw data. They are the same in training (Python) and inference (C): soil-water depletion in the root zone, 3-day crop evapotranspiration
(Hargreaves ET<sub>0</sub> × FAO-56 sugarcane K<sub>c</sub> by growth stage), effective rain (amount × probability), heat index above 36 °C, total available water,
relative available water, K<sub>c</sub>, T<sub>max</sub>, forecast rain, and a black-soil interaction term.</p>
<ul>
<li><b>Irrigation-depth regressor</b>: ridge linear regression (closed-form normal equations).</li>
<li><b>Water-stress classifier</b>: logistic regression trained with IRLS (Newton's method).</li>
<li><b>Uncertainty</b>: for each plot, a 4,000-draw Monte-Carlo ensemble over rain occurrence and amount, temperature error and sensor noise gives a P10–P90 range for depth.
This ensemble is also what makes the per-village prediction processes CPU-heavy, which gives Experiment 6 realistic bursts.</li>
<li><b>Rule-based baseline</b> (clearly identified): a FAO-56 water balance, depth = depletion + 3-day ETc − forecast rain. It is used for comparison and as an automatic fallback if the model file is missing.</li>
</ul>
{shot("02_model_training.png", "Screenshot 2 – Model training and evaluation on the 20 % held-out test set (synthetic data)")}
{img("model_error.png", "Figure 2 – The ML model cuts depth-prediction error from 24.9 mm to 3.5 mm MAE (R² 0.986 vs 0.430)", "70%")}
<p><b>Interpretation.</b> The regressor clearly beats the rule baseline because it learns effects the fixed rule ignores: rain probability, the effective-rain fraction,
heat stress and the management-allowed depletion. The stress classifier is only marginally better than the simple threshold rule (F1 0.857 vs 0.855).
A threshold on relative available water is already a strong signal on this data, so we report the result honestly rather than over-claim it.</p>
""")

# ---------------------------------------------------------------- Exp 4
P(f"""
<h2 class="page">5. Experiment 4 – File and Process Handling using System Calls</h2>
<p><b>Aim.</b> Write a program in which a parent process creates child processes with <code>fork()</code>, <code>exec()</code> and <code>wait()</code> for the stages of the workflow,
and reads and writes use-case data files with <code>open()</code>, <code>read()</code>, <code>write()</code> and <code>close()</code>.</p>
<p><b>Use-case connection.</b> This program <i>is</i> the irrigation-advisory application. <code>bin/pipeline</code> is the parent. Its children <code>exec</code> three separate programs:</p>
<table>
<tr><th>Stage</th><th>Child process(es)</th><th>Work done</th><th>System calls</th></tr>
<tr><td>1 Acquisition</td><td>acquire sensors, acquire weather (concurrent)</td><td>Validate 100,800 sensor rows (rejects missing, out-of-range and malformed rows) and aggregate features per plot. Aggregate the forecast per village.</td><td>open, read (8 KB buffered line reader), write, close</td></tr>
<tr><td>2 Prediction</td><td>predict &lt;village&gt; × 5 (concurrent)</td><td>ML inference + Monte-Carlo ensemble for that village's plots</td><td>open, read, write, close</td></tr>
<tr><td>3 Advisory</td><td>advise</td><td>Merge predictions and write the advisories, report and pump demand</td><td>open(O_CREAT|O_TRUNC, 0640), write, close</td></tr>
<tr><td>Parent</td><td>pipeline</td><td>Creates directories, backs up the previous advisory as read-only, forks and execs the stages, reaps children with <code>wait4()</code> to get exit status and CPU usage, and aborts if a stage fails</td><td>fork, execv, wait4, mkdir, open, read, write, close, getpid</td></tr>
</table>
<h3>Key program logic (src/pipeline.c)</h3>
{code('''pid_t pid = fork();
if (pid == 0) {                       /* child: become the stage program        */
    execv(argv[0], argv);             /* e.g. bin/predict Mudhol ...            */
    _exit(127);                       /* only reached if exec fails             */
}
/* parent: reap every child of the stage, collect exit code + CPU time       */
pid_t done = wait4(-1, &status, 0, &ru);
cpu_ms = ru.ru_utime + ru.ru_stime;   /* -> logs/stage_times.csv (used by Exp 6) */
if (WEXITSTATUS(status) != 0)  ->  abort workflow (no stage consumes bad data)''')}
<h3>Commands</h3>
{code('''make                      # builds bin/pipeline bin/acquire bin/predict bin/advise ...
./bin/pipeline            # run the application
ps -eo pid,ppid,ni,stat,comm | grep -E 'pipeline|predict'   # observe the process tree
cat logs/stage_times.csv  # per-process wall and CPU time''')}
{shot("03_exp4_pipeline.png", "Screenshot 3 – The parent forks 8 children in 3 stages. The 5 prediction processes run in parallel, and every child is reaped with its exit code and CPU time")}
{shot("04_exp4_process_tree.png", f"Screenshot 4 – ps during a run: the five predict processes all have PPID {PPID}, the pipeline parent")}
{term(stage_csv, "Output 1 – logs/stage_times.csv written by the parent with write(): measured burst times used in Experiment 6")}
{shot("05_advisory_report.png", "Screenshot 5 – outputs/advisory_report.txt generated by the advise child process, with farmer messages in English and Hindi")}
{img("advisory_mix.png", "Figure 3 – Advisory decisions per village. Jamkhandi has the most 'no irrigation' plots because rain is forecast there", "85%")}
<h3>Results</h3>
<ul>
<li>8 child processes ran successfully. Total wall time was {P_WALL:.0f} ms against {P_CPU:.0f} ms of total child CPU time, so the parallel village processes overlap on the 2-core machine.</li>
<li>1,542 faulty sensor rows were detected and rejected, and all 600 plots received an advisory: 244 today, 95 tomorrow, 230 within 3 days and 31 none.</li>
<li>The 3-day water requirement estimated by the AI advisory is <b>34.6 % lower</b> than the rule-based schedule (952,361 m³ vs 1,456,076 m³).</li>
<li>Failure handling: if the sensor file is missing, Stage 1 exits with code 2 and the parent stops the workflow. A corrupt file gives exit code 3. A missing model makes the system fall back to the rule baseline (see Section 9).</li>
</ul>
<div class="qa"><h3>Post-lab questions</h3>
<b>Q1. What is the difference between fork() and exec()?</b> fork() creates a new process that is a copy of the caller, with a new PID and the same code and data.
exec() replaces the code of the calling process with a new program and keeps the PID. The pipeline uses both: fork to create a child, then execv to turn the child into <code>acquire</code>, <code>predict</code> or <code>advise</code>.
<b>Q2. Why must the parent call wait()?</b> To collect the child's exit status, so the parent knows whether a stage succeeded, and to remove the child's process-table entry. Without it, finished children stay as zombies.
We use <code>wait4()</code> because it also returns the child's resource usage (CPU time).
<b>Q3. Why use open/read/write instead of fopen/fprintf?</b> They are the kernel system-call interface itself. They give direct control over file descriptors, permissions (0640 for outputs, 0440 for backups) and flags such as O_TRUNC.
The C library's stdio functions are buffered wrappers built on top of these calls.
<b>Q4. What is an orphan process?</b> A child whose parent exits first. It is adopted by init/systemd. The pipeline always waits for every child of a stage before it moves on, so no orphans or zombies are left.</div>
<p><b>Outcome / conclusion.</b> The advisory workflow runs as cooperating Linux processes created with fork/exec and synchronised by wait. Village-level parallelism and
stage-failure isolation come directly from the process model. All file I/O uses raw system calls.</p>
""")

# ---------------------------------------------------------------- Exp 6
P(f"""
<h2 class="page">6. Experiment 6 – Shortest Job First and Round Robin Scheduling</h2>
<p><b>Aim.</b> Model the tasks of the use case as processes with burst times taken from the prototype. Implement SJF and RR, draw Gantt charts, compute average waiting and turnaround time,
compare at least two time quanta, and justify the policy best suited to the use case.</p>
<p><b>Use-case connection and source of burst times.</b> The 8 processes are the real tasks of one advisory cycle. The burst times are <b>measured, not assumed</b>.
Each burst is the CPU time (user + system) of that child process, reported by <code>wait4()</code> rusage in Experiment 4 and stored in <code>logs/stage_times.csv</code>.
The time unit is 1 tick = 1 ms. Two arrival scenarios are analysed:</p>
<ul><li><b>Measured</b>: arrival = the fork offset measured by the pipeline (stage dependencies included).</li>
<li><b>Batch cycle</b>: all 8 tasks of a cycle are queued at t = 0 on a single-core field gateway (for example a Raspberry Pi).</li></ul>
<p>Algorithms implemented in <code>src/scheduler.c</code>: FCFS (reference), SJF (non-preemptive), SRTF (preemptive SJF) and Round Robin with quanta 2, 5 and 10 ms.
If no measurement log exists, documented estimates in <code>data/config/task_estimates.csv</code> are used and the program says so.</p>
{code('''./bin/scheduler -a measured -t 1 -q 2,5,10     # measured arrivals and bursts
./bin/scheduler -a zero     -t 1 -q 2,5,10     # batch cycle: all arrive at t = 0''')}
{shot("06_exp6_sjf.png", "Screenshot 6 – Process table built from the measured CPU bursts, and SJF for the batch cycle: the shortest tasks (weather, report, sensors) run first")}
{img("gantt_SJF.png", "Figure 4 – Gantt chart, SJF (batch cycle)", "92%")}
{term(section(exp6b, "==================== RR_q5", "==================== RR_q10"), "Output 2 – Round Robin q = 5 ms (batch cycle); the full chart is in outputs/scheduling/gantt_RR_q5.csv")}
{img("gantt_RR_q5.png", "Figure 5 – Gantt chart, Round Robin q = 5 ms (batch cycle)", "92%")}
<h3>Comparison</h3>
{shot("07_exp6_comparison.png", "Screenshot 7 – Comparison of all policies with measured arrivals")}
{term(section(exp6b, "Algorithm", "Lowest average waiting"), "Output 3 – Comparison for the batch cycle (all tasks queued at t = 0)")}
{img("sched_compare.png", "Figure 6 – Average waiting, turnaround and response time (measured scenario)", "92%")}
<h3>Analysis and justification</h3>
<ul>
<li><b>SJF/SRTF minimise average waiting time.</b> In the batch cycle, average WT drops from {SB["FCFS"]["wt"]:.1f} ms (FCFS) to {SB["SJF"]["wt"]:.1f} ms (SJF), a {red_bsjf:.0f} % reduction.
With measured arrivals it drops from {SM["FCFS"]["wt"]:.1f} ms to {SM["SJF"]["wt"]:.1f} ms (SJF, {red_sjf:.0f} %) and {SM["SRTF"]["wt"]:.1f} ms (SRTF, {red_srtf:.0f} %).
The short tasks (weather acquisition, report generation) no longer wait behind the {BIG_BURST:.0f} ms Sameerwadi prediction.</li>
<li><b>RR gives the best response time but the worst waiting time.</b> With q = 2 ms the average response time is only {SM["RR_q2"]["rt"]:.1f} ms, but there are {SM["RR_q2"]["sw"]} context switches and average WT rises to {SM["RR_q2"]["wt"]:.1f} ms.
Larger quanta (5 and 10 ms) cut the switches to {SM["RR_q5"]["sw"]} and {SM["RR_q10"]["sw"]} but increase response time. As q grows, RR behaves more like FCFS.</li>
<li><b>Best policy for this use case.</b> The advisory cycle is a <b>batch job with known, repeatable burst times</b>: each task's CPU time is proportional to its village's plot count and is measured every run.
This is the ideal case for <b>SJF</b>, because the main weakness of SJF (unknown burst lengths) does not apply here. SJF delivers the most advisories early, so
they are ready before the morning power window when farmers start their pumps. SRTF is marginally better but adds preemption overhead.
For interactive work on the same gateway, such as farmer queries or dashboard requests, RR with q ≈ 5–10 ms keeps response time under about 30 ms at a moderate switch cost.
A practical design is to run the batch tasks with SJF and the interactive tasks with RR.</li>
<li>Starvation risk: Sameerwadi, the largest village, always finishes last under SJF. With only 8 tasks per cycle this is bounded (it finishes in under 0.7 s). Ageing can be added if more villages are onboarded.</li>
</ul>
<div class="qa"><h3>Post-lab questions</h3>
<b>Q1. Why is SJF optimal for average waiting time, and why is it hard to use in practice?</b> Running the shortest job first minimises the sum of waiting times, which an exchange argument proves.
The difficulty is that burst lengths are usually unknown. In our use case they are measured each cycle, so SJF is practical here.
<b>Q2. How does the time quantum affect Round Robin?</b> A very small q gives good responsiveness but many context switches ({SM["RR_q2"]["sw"]} at q = 2). A very large q makes RR degrade into FCFS.
q should be somewhat larger than a typical short burst.
<b>Q3. What is the difference between SJF and SRTF?</b> SRTF is preemptive: a newly arrived job with a shorter remaining time preempts the running one. In the measured scenario this lowers average WT from {SM["SJF"]["wt"]:.1f} to {SM["SRTF"]["wt"]:.1f} ms.</div>
<p><b>Outcome / conclusion.</b> Scheduling the prototype's real, measured processes shows that SJF suits the periodic advisory batch and RR suits interactive requests. The choice is backed by the measured WT, TAT, response-time and context-switch figures.</p>
""")

# ---------------------------------------------------------------- Exp 7
P(f"""
<h2 class="page">7. Experiment 7 – Producer–Consumer using Semaphores and Mutex</h2>
<p><b>Aim.</b> Apply a classical synchronisation problem to a shared resource of the use case and show evidence that race conditions are prevented.</p>
<p><b>Use-case connection.</b> In the field, several village sensor gateways push soil-moisture readings to an edge server while analytics workers process them.
<code>bin/sensor_sync</code> models this exactly:</p>
<table>
<tr><th>Role</th><th>Implementation</th></tr>
<tr><td>Producers</td><td>5 threads, one per village gateway, streaming the 100,277 well-formed readings from sensor_readings.csv</td></tr>
<tr><td>Shared buffer</td><td>Bounded circular buffer of 8 readings</td></tr>
<tr><td>Consumers</td><td>3 analytics threads that validate each reading and update the <b>shared plot-water table</b> (per-plot count and moisture sum, plus global totals)</td></tr>
<tr><td>Synchronisation</td><td><code>sem_t empty</code> (free slots, initial value 8): the producer blocks when the buffer is FULL.
<code>sem_t full</code> (filled slots, initial value 0): the consumer blocks when the buffer is EMPTY.
<code>buf_mutex</code> protects the in/out indices. <code>table_mutex</code> protects the plot table. Poison-pill items shut the consumers down cleanly.</td></tr>
</table>
{code('''/* producer */                          /* consumer */
sem_wait(&empty);                       sem_wait(&full);
pthread_mutex_lock(&buf_mutex);         pthread_mutex_lock(&buf_mutex);
buf[in] = r; in = (in+1) % N;           r = buf[out]; out = (out+1) % N;
pthread_mutex_unlock(&buf_mutex);       pthread_mutex_unlock(&buf_mutex);
sem_post(&full);                        sem_post(&empty);
                                        pthread_mutex_lock(&table_mutex);   /* removed by --unsafe */
                                        plot_count[p]++; plot_sum[p] += sm; total_valid++;
                                        pthread_mutex_unlock(&table_mutex);''')}
{code('''./bin/sensor_sync            # safe: semaphores + mutex
./bin/sensor_sync --unsafe   # table_mutex removed -> race condition
./bin/sensor_sync -b 1 -p 8 -c 8   # stress test: buffer always full/empty''')}
{shot("08_exp7_safe.png", "Screenshot 8 – SAFE run: every check matches the sequential reference, so no updates are lost")}
{shot("09_exp7_unsafe.png", f"Screenshot 9 – UNSAFE run (no table mutex): {U_LOST:,} updates lost, which proves the race condition")}
{img("exp7_race.png", f"Figure 7 – Valid readings recorded in the shared table: the safe run matches exactly, the unsafe run loses {100 * (99258 - U_VALID) / 99258:.0f} %", "62%")}
<h3>Results</h3>
<ul>
<li><span class="ok">SAFE</span>: produced = consumed = 100,277, and the valid count, rejected count, per-plot entries and moisture checksum all equal the single-threaded reference.
Producers blocked on a FULL buffer {S_FULL:,} times and consumers blocked on an EMPTY buffer {S_EMPTY:,} times, which shows that the semaphores actually blocked threads. Throughput was about {S_TPUT // 1000} k readings/s.</li>
<li><span class="bad">UNSAFE</span>: with the mutex removed, two consumers read the same old counter value and both write back value + 1, so one increment is lost.
The table recorded only {U_VALID:,} of 99,258 valid readings. Note that produced = consumed still holds, because the buffer is still protected by its semaphores. The lost updates are confined to the unprotected table.</li>
<li>The buffer-size-1 stress test (8 producers, 8 consumers) is also consistent (Section 9).</li>
</ul>
<div class="qa"><h3>Post-lab questions</h3>
<b>Q1. Why are two semaphores and a mutex needed?</b> The counting semaphores track free and filled slots, so threads block instead of busy-waiting. The mutex gives mutual exclusion on the shared indices and table.
Semaphores alone do not stop two consumers from updating the table at the same time.
<b>Q2. What happens if the order of sem_wait(&amp;empty) and mutex_lock is swapped in the producer?</b> A producer could hold the mutex while it sleeps on a full buffer. The consumer then cannot acquire the mutex to free a slot, so the system deadlocks.
<b>Q3. What is a race condition?</b> The result depends on the timing of the interleaving of concurrent accesses to shared data. Here, an unprotected read-modify-write of a counter loses updates.</div>
<p><b>Outcome / conclusion.</b> The producer–consumer solution lets concurrent sensor gateways and analytics workers share the buffer and the plot table safely. The unsafe run gives direct, measured evidence of what goes wrong without synchronisation.</p>
""")

# ---------------------------------------------------------------- Exp 9
P(f"""
<h2 class="page">8. Experiment 9 – Deadlock Avoidance using the Banker's Algorithm</h2>
<p><b>Aim.</b> Identify the processes and shared resources of the use case and apply the Banker's safety and resource-request algorithms to safe and unsafe scenarios.</p>
<p><b>Use-case connection.</b> Within the next 48-hour irrigation window, the five villages (processes) compete for four types of shared resource managed by the cane department:</p>
<table><tr><th>Resource</th><th>Meaning</th><th>Total instances</th></tr>
<tr><td>Pumps</td><td>shared mobile pump sets</td><td>14</td></tr><tr><td>Power</td><td>3-phase feeder slots of 15 kW during the power window</td><td>6</td></tr>
<tr><td>Valves</td><td>canal outlet gates</td><td>10</td></tr><tr><td>FertUnits</td><td>drip fertigation units</td><td>6</td></tr></table>
<p>Each village's <b>maximum claim</b> is the demand computed by the advisory stage of Experiment 4 (<code>outputs/pump_demand.csv</code>).
The values are fixed in <code>data/config/banker_scenario.txt</code> so that the demonstration is reproducible. A deadlock here would mean, for example, that Sameerwadi holds pumps and waits for power slots
while Mudhol holds power slots and waits for pumps, so neither can irrigate.</p>
{term(demand, "Output 4 – outputs/pump_demand.csv from the advisory stage: the source of the maximum claims")}
{code('''./bin/banker                                   # runs data/config/banker_scenario.txt''')}
{shot("10_exp9_banker.png", "Screenshot 10 – Initial state (Allocation, Max, Need, Available), the safety algorithm, and the first request being granted")}
{shot("11_exp9_decisions.png", "Screenshot 11 – Decision for every request: GRANTED, DENIED (unsafe), GRANTED, ERROR, WAIT, RELEASE, GRANTED")}
{term(section(exp9, "REQUEST Sameerwadi", "REQUEST Mudhol"), "Output 5 – Detail: Sameerwadi's request leaves no job able to finish, so it is denied. Terdal's full-claim request is granted")}
<h3>Results</h3>
<table><tr><th>#</th><th>Event</th><th>Outcome</th><th>Reason</th></tr>
<tr><td>0</td><td>Initial state</td><td class="ok">SAFE</td><td>Safe sequence: Jamkhandi → Rabkavi → Terdal → Sameerwadi → Mudhol</td></tr>
<tr><td>1</td><td>Terdal requests [1 1 0 0]</td><td class="ok">GRANTED</td><td>The state remains safe</td></tr>
<tr><td>2</td><td>Sameerwadi requests [2 1 0 0]</td><td class="bad">DENIED</td><td>It would take the last pumps and power, leaving no job able to finish (unsafe → possible deadlock), so the request is rolled back</td></tr>
<tr><td>3</td><td>Terdal requests its full remaining need [2 0 2 1]</td><td class="ok">GRANTED</td><td>Terdal can complete and release its resources</td></tr>
<tr><td>4</td><td>Mudhol requests 6 pumps</td><td class="bad">ERROR</td><td>This exceeds Mudhol's declared need of 5</td></tr>
<tr><td>5</td><td>Jamkhandi requests 1 pump</td><td>WAIT</td><td>0 pumps available</td></tr>
<tr><td>6</td><td>Terdal finishes irrigating</td><td>RELEASE</td><td>[4 2 3 1] is returned</td></tr>
<tr><td>7</td><td>Jamkhandi retries</td><td class="ok">GRANTED</td><td>Safe after the release</td></tr></table>
<div class="qa"><h3>Post-lab questions</h3>
<b>Q1. What is a safe state? Is an unsafe state always a deadlock?</b> A state is safe if there is some order in which every process can obtain its remaining need and finish. An unsafe state is not necessarily a deadlock,
but the system can no longer guarantee to avoid one. The Banker's algorithm therefore never enters an unsafe state (request 2).
<b>Q2. Which of the four Coffman conditions hold in this use case?</b> All four can hold. A pump serves one plot at a time (mutual exclusion). A village holds pumps while waiting for power (hold and wait).
Pumps cannot be taken away mid-irrigation (no preemption). Circular wait can form between villages. Banker's avoids deadlock by refusing requests that could lead to circular wait.
<b>Q3. What are the limitations of the Banker's algorithm here?</b> Maximum claims must be declared in advance. Our advisory stage computes them, which is realistic. The number of processes is assumed fixed, and the safety check costs O(m·n²) per request,
which is trivial for 5 villages and 4 resource types.</div>
<p><b>Outcome / conclusion.</b> Treating irrigation resources as OS resources lets the Banker's algorithm produce a deadlock-free pump and power allocation plan. The connection to the application is direct, because the maximum claims come from the AI advisory's demand.</p>
""")

# ---------------------------------------------------------------- testing
P(f"""
<h2 class="page">9. Testing and Performance Comparison</h2>
<p><code>tests/run_tests.sh</code> (also run in GitHub Actions CI on every push) runs normal and failure scenarios for all four experiments and checks exit codes and output text.</p>
{shot("12_tests.png", "Screenshot 12 – 16 / 16 normal and failure-scenario tests pass on Ubuntu 24.04")}
<table><tr><th>Failure scenario</th><th>Expected behaviour</th><th>Observed</th></tr>
<tr><td>Sensor data file missing</td><td>Stage 1 child exits 2, parent aborts and later stages never run</td><td class="ok">Pass (exit 1)</td></tr>
<tr><td>Corrupt sensor file (no valid row)</td><td>acquire exits 3</td><td class="ok">Pass</td></tr>
<tr><td>Model file missing</td><td>Warning and automatic rule-based fallback</td><td class="ok">Pass</td></tr>
<tr><td>Unknown village / no forecast</td><td>predict exits 3</td><td class="ok">Pass</td></tr>
<tr><td>Stage-time log missing</td><td>Scheduler uses documented estimates and says so</td><td class="ok">Pass</td></tr>
<tr><td>Invalid time quantum (0)</td><td>Rejected with an error</td><td class="ok">Pass</td></tr>
<tr><td>Buffer of size 1 (always full/empty)</td><td>Still consistent</td><td class="ok">Pass</td></tr>
<tr><td>Missing mutex (--unsafe)</td><td>Race detected (exit 4)</td><td class="ok">Pass</td></tr>
<tr><td>Unsafe / over-claim / unavailable resource request</td><td>DENIED / ERROR / WAIT</td><td class="ok">Pass</td></tr></table>
<h3>Measurements summary</h3>
<table><tr><th>Metric</th><th>Value</th></tr>
<tr><td>Depth prediction error (ML vs rule)</td><td>MAE 3.51 mm vs 24.93 mm · R² 0.986 vs 0.430</td></tr>
<tr><td>Stress classifier (ML vs rule)</td><td>Accuracy 0.861 vs 0.858 · F1 0.857 vs 0.855</td></tr>
<tr><td>Pipeline execution time (600 plots, 8 processes)</td><td>{P_WALL:.0f} ms wall, {P_CPU:.0f} ms total child CPU (2 vCPU)</td></tr>
<tr><td>Sensor data quality</td><td>99,258 valid, 1,542 rejected (1.53 %)</td></tr>
<tr><td>Estimated water saving vs rule schedule</td><td>34.6 % (3-day requirement)</td></tr>
<tr><td>Scheduling, average WT (batch cycle)</td><td>FCFS {SB["FCFS"]["wt"]:.1f} · SJF {SB["SJF"]["wt"]:.1f} · SRTF {SB["SRTF"]["wt"]:.1f} · RR(q=2/5/10) {SB["RR_q2"]["wt"]:.1f} / {SB["RR_q5"]["wt"]:.1f} / {SB["RR_q10"]["wt"]:.1f} ms</td></tr>
<tr><td>Scheduling, average WT (measured arrivals)</td><td>FCFS {SM["FCFS"]["wt"]:.1f} · SJF {SM["SJF"]["wt"]:.1f} · SRTF {SM["SRTF"]["wt"]:.1f} · RR(q=2/5/10) {SM["RR_q2"]["wt"]:.1f} / {SM["RR_q5"]["wt"]:.1f} / {SM["RR_q10"]["wt"]:.1f} ms</td></tr>
<tr><td>Producer–consumer throughput</td><td>≈ {S_TPUT:,} readings/s (safe) · {U_LOST:,} updates lost when unsafe</td></tr>
<tr><td>Banker's decisions</td><td>3 granted, 1 denied (unsafe), 1 error, 1 wait</td></tr></table>
<p class="note">Timings vary slightly between runs because they are measured on a shared cloud VM. The values in this report are from the run whose full logs are in <code>report/evidence/</code>.</p>

<h2>10. Limitations, Data Privacy and Improvements</h2>
<p><b>Limitations.</b></p>
<ul>
<li>The data is synthetic and the labels come from a physically based simulation, so the model accuracy shows the method works but not its field accuracy. The model must be retrained and validated on KIAAR data before any real use.</li>
<li>The models are linear or logistic and use an assumed pump flow (10 m³/h per kW) and irrigation efficiencies (drip 0.9, furrow 0.6).</li>
<li>The scheduler is a simulation using measured bursts. The real Linux CFS scheduler ran the actual processes. The Banker's maximum claims are fixed per window.</li>
</ul>
<p><b>Data privacy.</b> Farmer identities are anonymised tokens. No names, phone numbers or exact boundaries are stored. Output files are created with mode 0640 and backups are read-only (0440).
A deployment would need encryption, role-based access and DPDP-Act-compliant consent, as listed in the use-case governance table. Advisories are decision support: the farmer or field officer keeps the final decision (human in the loop).</p>
<p><b>Possible improvements.</b> Train gradient-boosted or LSTM models on real sensor history. Add an LLM layer for Kannada and Marathi advisories and a chatbot. Use shared memory and message queues between stages.
Add an MQTT-based IoT ingestion service. Use ageing in SJF. Recompute the Banker's claims dynamically every advisory cycle. Add a web dashboard with a map of the plots.</p>

<h2>11. Conclusion</h2>
<p>We built a working Linux prototype of the KJS-AGR-01 irrigation advisory system with a clear input → processing → output flow. It turns raw sensor, weather and plot data into plot-wise,
prioritised irrigation advisories using a trained ML model. Four OS experiments are built into the application rather than shown as separate demos.
Processes and system calls run the workflow (Exp 4). Its measured bursts drive the scheduling study (Exp 6). Semaphores and a mutex protect the shared sensor buffer and plot table (Exp 7).
The Banker's algorithm allocates the pumps and power that the advisory asks for without deadlock (Exp 9).</p>

<h2>References</h2>
<ol>
<li>KJS-AGR-01 AI Use Case Profile – Irrigation Advisory System for Sugarcane, K J Somaiya (Dr. Shyamal Virnodkar et al.).</li>
<li>Operating Systems Experiments through Somaiya Vidyavihar AI Use Cases – assignment document.</li>
<li>Allen R. G., Pereira L. S., Raes D., Smith M., <i>FAO Irrigation and Drainage Paper 56: Crop Evapotranspiration</i>, FAO, 1998.</li>
<li>Hargreaves G. H., Samani Z. A., "Reference crop evapotranspiration from temperature", <i>Applied Engineering in Agriculture</i>, 1985.</li>
<li>Silberschatz A., Galvin P. B., Gagne G., <i>Operating System Concepts</i>, 10th ed., Wiley.</li>
<li>Linux man-pages: fork(2), execve(2), wait4(2), open(2), read(2), write(2), sem_init(3), pthread_mutex_lock(3).</li>
</ol>

<h2>Acknowledgement of AI-Assisted Work</h2>
<p>As permitted by the assignment, AI tools were used for assistance with coding and documentation. All code was compiled, executed and verified by the team on Ubuntu Linux.
Every output, timing and screenshot in this report comes from real runs (full logs in <code>report/evidence/</code>).</p>
""")

doc = f"""<!doctype html><html lang="en"><head><meta charset="utf-8">
<title>OS Use Case Report – KJS-AGR-01</title><style>{CSS}</style></head><body>{''.join(parts)}</body></html>"""

out = os.path.join(HERE, "OS_UseCase_Report.html")
with open(out, "w", encoding="utf-8") as f:
    f.write(doc)
print("written", out)
