"use strict";
// Frontend for the ESP32-S3 thermometer. Polls the JSON API (core/json_api
// contract), draws the history with uPlot (gaps for missing/invalid samples),
// and posts config/actions. The device has no RTC, so wall-clock time for the
// chart is computed here from uptime_s + stride_s.

const POLL_MS = 5000;
const $ = (id) => document.getElementById(id);

// centi-°C integer (or null) -> "23.45" / "–"
function c100(v) {
  return v === null || v === undefined ? "–" : (v / 100).toFixed(2);
}

function fmtUptime(sec) {
  const d = Math.floor(sec / 86400);
  const h = Math.floor((sec % 86400) / 3600);
  const m = Math.floor((sec % 3600) / 60);
  const s = sec % 60;
  return (d ? d + "d " : "") + String(h).padStart(2, "0") + ":" +
         String(m).padStart(2, "0") + ":" + String(s).padStart(2, "0");
}

const ADVICE = {
  open: ["OTEVŘÍT OKNO", "open"],
  close: ["ZAVŘÍT OKNO", "close"],
  nochange: ["BEZE ZMĚNY", "nochange"],
};

function renderCurrent(s) {
  $("inner").textContent = c100(s.inner_c100);
  $("outer").textContent = c100(s.outer_c100);
  $("diff").textContent = c100(s.diff_c100);

  const [label, cls] = ADVICE[s.window] || ADVICE.nochange;
  const adv = $("advice");
  adv.textContent = label;
  adv.className = "advice " + cls;

  const alarms = [];
  if (s.fire) alarms.push("🔥 POŽÁR");
  if (s.sensor_fault) alarms.push("⚠ PORUCHA ČIDLA");
  if (s.diff_alarm) alarms.push("Δ rozdíl překročen");
  const a = $("alarms");
  a.textContent = alarms.join("  •  ");
  a.className = alarms.length ? "alarm" : "";

  $("uptime").textContent = fmtUptime(s.uptime_s);
  $("rssi").textContent = s.rssi === 127 ? "—" : s.rssi + " dBm";
  $("heap").textContent = (s.free_heap / 1024).toFixed(1) + " kB";
  $("minheap").textContent = (s.min_free_heap / 1024).toFixed(1) + " kB";
  $("romIn").textContent = s.inner_rom;
  $("romOut").textContent = s.outer_rom;

  // Populate the config form once (don't clobber the user mid-edit).
  const f = $("cfgForm");
  if (!f.dataset.loaded) {
    f.beeper.checked = s.beeper;
    f.email.checked = s.email;
    f.window_goal.value = String(s.window_goal);
    f.diff_thr.value = (s.diff_thr_c100 / 100).toFixed(1);
    f.diff_hyst.value = (s.diff_hyst_c100 / 100).toFixed(1);
    f.fire_thr.value = (s.fire_thr_c100 / 100).toFixed(1);
    f.fire_hyst.value = (s.fire_hyst_c100 / 100).toFixed(1);
    f.contrast.value = s.contrast;
    f.dataset.loaded = "1";
  }
}

let chart = null;
function makeChart() {
  const opts = {
    width: $("chart").clientWidth || 800,
    height: 320,
    series: [
      {},
      { label: "Vnitřní", stroke: "#d32f2f", spanGaps: false, value: (u, v) => v == null ? "–" : v.toFixed(2) + " °C" },
      { label: "Venkovní", stroke: "#1976d2", spanGaps: false, value: (u, v) => v == null ? "–" : v.toFixed(2) + " °C" },
    ],
    scales: { x: { time: true } },
    axes: [
      {
        values: (u, vals, space, incr) => vals.map(v => {
          if (v == null) return "";
          const d = new Date(v * 1000);
          if (incr >= 86400) {
            return String(d.getDate()).padStart(2, "0") + "." + String(d.getMonth() + 1).padStart(2, "0") + ".";
          }
          return String(d.getHours()).padStart(2, "0") + ":" + String(d.getMinutes()).padStart(2, "0");
        }),
      },
    ],
  };
  chart = new uPlot(opts, [[], [], []], $("chart"));
  window.addEventListener("resize", () => chart.setSize({ width: $("chart").clientWidth, height: 320 }));
}

function renderHistory(h) {
  // Oldest-first. wall-clock(i) = now - (count-1-i) * stride_s. Newest == now.
  const nowS = Math.floor(Date.now() / 1000);
  const xs = [], inner = [], outer = [];
  for (let i = 0; i < h.count; i++) {
    const r = h.samples[i];
    xs.push(nowS - (h.count - 1 - i) * h.stride_s);
    inner.push(r.i === null ? null : r.i / 100); // null -> gap (FR-18)
    outer.push(r.o === null ? null : r.o / 100);
  }
  if (!chart) makeChart();
  chart.setData([xs, inner, outer]);
}

async function poll() {
  try {
    const [cur, hist] = await Promise.all([
      fetch("/api/current").then((r) => r.json()),
      fetch("/api/history").then((r) => r.json()),
    ]);
    renderCurrent(cur);
    renderHistory(hist);
    const now = new Date();
    const t = String(now.getHours()).padStart(2, "0") + ":" +
              String(now.getMinutes()).padStart(2, "0") + ":" +
              String(now.getSeconds()).padStart(2, "0");
    $("conn").textContent = "Aktualizováno " + t;
  } catch (e) {
    $("conn").textContent = "Zařízení nedostupné (" + e + ")";
  }
}

async function postForm(url, params) {
  const res = await fetch(url, {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: new URLSearchParams(params).toString(),
  });
  if (!res.ok) throw new Error("HTTP " + res.status);
  return res;
}

function wireForms() {
  $("cfgForm").addEventListener("submit", async (ev) => {
    ev.preventDefault();
    const f = ev.target;
    const params = {
      beeper: f.beeper.checked ? 1 : 0,
      email: f.email.checked ? 1 : 0,
      window_goal: f.window_goal.value,
      diff_thr_c100: Math.round(parseFloat(f.diff_thr.value) * 100),
      diff_hyst_c100: Math.round(parseFloat(f.diff_hyst.value) * 100),
      fire_thr_c100: Math.round(parseFloat(f.fire_thr.value) * 100),
      fire_hyst_c100: Math.round(parseFloat(f.fire_hyst.value) * 100),
      contrast: f.contrast.value,
    };
    try {
      await postForm("/api/config", params);
      $("actionMsg").textContent = "Konfigurace uložena.";
    } catch (e) {
      $("actionMsg").textContent = "Uložení selhalo: " + e;
    }
  });

  document.querySelectorAll("button[data-action]").forEach((btn) => {
    btn.addEventListener("click", async () => {
      const action = btn.dataset.action;
      if (action === "restart" && !confirm("Opravdu restartovat zařízení?")) return;
      try {
        const params = action === "set-contrast" ? { contrast: $("cfgForm").contrast.value } : {};
        await postForm("/api/action/" + action, params);
        $("actionMsg").textContent = "Akce '" + action + "' odeslána.";
      } catch (e) {
        $("actionMsg").textContent = "Akce selhala: " + e;
      }
    });
  });
}

wireForms();
poll();
setInterval(poll, POLL_MS);
