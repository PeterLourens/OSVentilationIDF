// send_json_ws.js
// Robust WebSocket client for OSVentilation without client-side rate limiting
// - Single requestPageData function
// - Only polls when tab is visible and this tab is the leader
// - Triggers on load, visibility change, popstate, and page_name mutations
// - Prefers slot_index for statemachine messages and falls back to state_index
// - No local debounce/rate-limiter (server-side rate limiting should remain)

(function () {
    'use strict';

    // Configuration
    const GATEWAY = (location.protocol === 'https:' ? 'wss://' : 'ws://') + window.location.host + '/ws';
    const RECONNECT_MS = 2000;
    const POLL_MS = 60000; // periodic poll interval when leader and visible

    // Page -> server command mapping
    const PAGE_TO_CMD = {
        'stats': 'stats',
        'status': 'stats',
        'index': 'index',
        'sensor_config': 'sensor_config',
        'config': 'config',
        'statemachine': 'statemachine',
        'valvecontrol': 'valvecontrol',
        'webserial': 'webserial'
    };

    // State
    let websocket = null;
    let pollIntervalId = null;
    let buttonsWired = false;

    // Leader election (single-tab polling) using BroadcastChannel when available
    const bc = ('BroadcastChannel' in window) ? new BroadcastChannel('osv_stats_channel') : null;
    let isLeader = true;

    // Small dedupe store for statemachine messages
    if (!window._sm_dedupe) window._sm_dedupe = {};

    // Safe console wrappers
    function logDebug(...args) { try { console.debug(...args); } catch (e) { } }
    function logInfo(...args) { try { console.info(...args); } catch (e) { } }
    function logWarn(...args) { try { console.warn(...args); } catch (e) { } }
    function logError(...args) { try { console.error(...args); } catch (e) { } }

    // BroadcastChannel leader protocol
    function announceLeader() {
        if (!bc) return;
        bc.postMessage({ type: 'hello', ts: Date.now() });
    }

    if (bc) {
        bc.onmessage = (ev) => {
            const msg = ev.data || {};
            if (msg.type === 'hello') {
                if (isLeader) bc.postMessage({ type: 'leader', ts: Date.now() });
            } else if (msg.type === 'leader') {
                isLeader = false;
            } else if (msg.type === 'i_am_closing') {
                isLeader = true;
            }
        };

        announceLeader();
        setTimeout(() => { if (isLeader) announceLeader(); }, 200);
        window.addEventListener('beforeunload', () => { if (bc) bc.postMessage({ type: 'i_am_closing' }); });
    }

    // WebSocket lifecycle
    function initWebSocket() {
        try {
            websocket = new WebSocket(GATEWAY);
        } catch (e) {
            logError('WS init failed', e);
            scheduleReconnect();
            return;
        }

        websocket.onopen = onOpen;
        websocket.onclose = onClose;
        websocket.onmessage = onMessage;
        websocket.onerror = onError;
    }

    function scheduleReconnect() {
        setTimeout(initWebSocket, RECONNECT_MS);
    }

    // Simple onClick wiring for Start/Stop/Save/Clear
    function wireSimpleButtons() {
        if (buttonsWired) return; // already wired

        const map = {
            start: 'Start',
            stop: 'Stop',
            save: 'Save',
            clear: 'Clear'
        };

        Object.keys(map).forEach((id) => {
            const btn = document.getElementById(id);
            if (!btn) return;

            // avoid double-attaching if this function is called again
            if (btn.dataset.wsWired === '1') return;

            btn.addEventListener('click', (ev) => {
                ev.preventDefault();
                const payload = map[id];

                // Local UI actions for Save / Clear
                if (id === 'clear') {
                    const logEl = document.getElementById('log');
                    if (logEl) while (logEl.firstChild) logEl.removeChild(logEl.firstChild);
                } else if (id === 'save') {
                    const logEl = document.getElementById('log');
                    if (logEl) {
                        const lines = Array.from(logEl.children).map(n => n.textContent || '');
                        const content = lines.join('\n');
                        const now = new Date();
                        const pad = n => String(n).padStart(2, '0');
                        const fname = `osvent_log_${now.getFullYear()}-${pad(now.getMonth() + 1)}-${pad(now.getDate())}_${pad(now.getHours())}${pad(now.getMinutes())}${pad(now.getSeconds())}.txt`;
                        const blob = new Blob([content], { type: 'text/plain;charset=utf-8' });
                        const url = URL.createObjectURL(blob);
                        const a = document.createElement('a');
                        a.href = url; a.download = fname; document.body.appendChild(a); a.click(); a.remove();
                        URL.revokeObjectURL(url);
                    }
                }

                // Send command over websocket (use the correct variable)
                if (websocket && websocket.readyState === WebSocket.OPEN) {
                    websocket.send(payload);
                    logDebug('WS sent:', payload);
                } else {
                    logWarn('WS not open; dropping command:', payload);
                }

                // brief visual feedback
                btn.setAttribute('data-ws-sent', '1');
                setTimeout(() => btn.removeAttribute('data-ws-sent'), 250);
            });

            btn.dataset.wsWired = '1';
        });

        buttonsWired = true;
    }

    function onOpen() {
        logInfo('WS open', GATEWAY);

        wireSimpleButtons(); // safe: will only attach once

        if (isLeader && !document.hidden) requestPageData();

        if (!pollIntervalId) {
            pollIntervalId = setInterval(() => {
                if (isLeader && !document.hidden) requestPageData();
            }, POLL_MS);
        }
    }

    function onClose() {
        logWarn('WS closed, reconnecting in', RECONNECT_MS, 'ms');
        if (pollIntervalId) {
            clearInterval(pollIntervalId);
            pollIntervalId = null;
        }
        scheduleReconnect();
    }

    function onError(e) {
        logError('WS error', e);
    }

    // Single request function (no client-side rate limiting)
    function requestPageData() {
        try {
            // Leader-only: prevents multiple tabs polling
            if (!isLeader) {
                logDebug('requestPageData: not leader, skipping');
                return;
            }

            // Visibility: don't poll when page is hidden
            if (document.hidden) {
                logDebug('requestPageData: page hidden, skipping');
                return;
            }

            if (!websocket || websocket.readyState !== WebSocket.OPEN) {
                logDebug('requestPageData: websocket not open, state=', websocket ? websocket.readyState : 'null');
                return;
            }

            const pageEl = document.getElementById('page_name');
            if (!pageEl) {
                logDebug('requestPageData: page_name element not found');
                return;
            }

            const raw = (pageEl.textContent || pageEl.innerText || pageEl.innerHTML || '').trim();
            if (!raw) {
                logDebug('requestPageData: page_name empty');
                return;
            }

            const key = raw.toLowerCase();
            const cmd = PAGE_TO_CMD[key] || key;

            // Debug line where cmd is defined
            logDebug('requestPageData: sending', cmd, 'isLeader=', isLeader, 'visible=', !document.hidden, 'wsState=', websocket && websocket.readyState);

            websocket.send(cmd);
            logInfo('WS sent command:', cmd, '(page:', raw, ')');
        } catch (err) {
            logError('requestPageData: unexpected error', err);
        }
    }

    // Message handling
    function onMessage(evt) {
        // Normalize incoming payload to a string
        let raw;
        try {
            if (evt && evt.data instanceof ArrayBuffer) {
                raw = new TextDecoder().decode(evt.data);
            } else if (evt && typeof evt.data === 'string') {
                raw = evt.data;
            } else if (evt && evt.data && typeof evt.data === 'object' && evt.data.toString) {
                // Some libs wrap data; fall back to toString
                raw = String(evt.data);
            } else {
                raw = '';
            }
        } catch (err) {
            logError('onMessage: failed to decode incoming data', err);
            return;
        }

        //logDebug('WS incoming raw:', raw);

        // Try to parse JSON; if parse succeeds, route to existing handlers
        let obj = null;
        try {
            obj = JSON.parse(raw);
        } catch (e) {
            // Not JSON — display raw log line(s) in the UI
            logDebug('WS: non-JSON message received:', raw);
            displayRawLog(raw);
            return;
        }

        // If parsed JSON is not an object, treat as raw text
        if (!obj || typeof obj !== 'object') {
            logDebug('WS parsed non-object JSON, treating as raw:', obj);
            displayRawLog(raw);
            return;
        }

        logDebug('WS parsed:', obj);

        // Route JSON messages using your existing handlers
        const type = obj.type || null;

        if (type && type.startsWith('statemachine')) {
            if (typeof handleStatemachineMessage === 'function') {
                handleStatemachineMessage(obj);
                return;
            }
        }

        if (type === 'summary') {
            if (typeof handleSummary === 'function') {
                handleSummary(obj);
                return;
            }
        }

        if (type === 'tasks_start' || type === 'tasks_page' || type === 'tasks_end') {
            if (typeof handleTasksMessages === 'function') {
                handleTasksMessages(obj);
                return;
            }
        }

        if (type === 'settings' || type === 'config' || type === 'configuration') {
            if (typeof handleSettings === 'function') {
                handleSettings(obj);
                return;
            }
        }

        // Fallback: apply payload to DOM if you have that function
        if (typeof applyPayloadToDOM === 'function') {
            applyPayloadToDOM(obj);
            return;
        }

        // If no handler matched, log the object to console and show a compact representation in the UI
        logDebug('WS: unhandled JSON message:', obj);
        displayRawLog(JSON.stringify(obj));
    }

    /* ---------- Helpers for rendering raw logs into #log ---------- */

    function escapeHtml(s) {
        if (s == null) return '';
        return String(s)
            .replace(/&/g, '&amp;')
            .replace(/</g, '&lt;')
            .replace(/>/g, '&gt;');
    }

    function displayRawLog(text) {
        const logEl = document.getElementById('log');
        if (!logEl) {
            // Fallback to console if #log not present
            console.debug('displayRawLog: #log element not found; raw:', text);
            return;
        }

        // Normalize and split into lines (some messages contain multiple lines)
        const lines = String(text).split(/\r?\n/).filter(l => l.trim().length > 0);

        for (const rawLine of lines) {
            const line = escapeHtml(rawLine.trim());

            // Detect severity prefix (common format in your logs: I/D/W/E)
            let cls = 'log-info';
            const m = line.match(/^\s*([IDEW])\b/);
            if (m) {
                const sev = m[1];
                if (sev === 'D') cls = 'log-debug';
                else if (sev === 'W') cls = 'log-warn';
                else if (sev === 'E') cls = 'log-error';
                else cls = 'log-info';
            }

            const div = document.createElement('div');
            div.className = cls;
            div.innerHTML = line;
            logEl.appendChild(div);
        }

        // Keep log size reasonable: remove oldest if > maxLines
        const maxLines = 500;
        while (logEl.children.length > maxLines) {
            logEl.removeChild(logEl.firstChild);
        }

        // Auto-scroll to bottom
        logEl.scrollTop = logEl.scrollHeight;
    }


    function applyRuntimeStats(payload) {
        const setText = (id, value) => {
            const el = document.getElementById(id);
            if (el) el.textContent = (value === null || typeof value === 'undefined') ? '' : String(value);
        };

        if (payload.date_time !== undefined) setText('stat_time', payload.date_time);
        else if (payload.time !== undefined) setText('stat_time', payload.time);

        if (payload.uptime !== undefined) setText('stat_uptime', payload.uptime);
        else if (payload.uptime_s !== undefined) setText('stat_uptime', formatUptime(payload.uptime_s));

        if (payload.cpu_temp_c !== undefined) {
            const value = (typeof payload.cpu_temp_c === 'number') ? payload.cpu_temp_c.toFixed(1) + ' °C' : payload.cpu_temp_c;
            setText('stat_cpu_temp', value);
        }
        if (payload.heap_free !== undefined) setText('stat_heap_free', payload.heap_free);
        if (payload.heap_min_ever !== undefined) setText('stat_heap_min', payload.heap_min_ever);
        if (payload.statemachine_state !== undefined) setText('stat_sm_state', payload.statemachine_state);
        if (payload.statemachine_fanspeed !== undefined) setText('stat_fanspeed', payload.statemachine_fanspeed);
        if (payload.co2_sensors_high !== undefined) setText('stat_co2_high', payload.co2_sensors_high);
        if (payload.rh_sensors_high !== undefined) setText('stat_rh_high', payload.rh_sensors_high);
        if (payload.wifi_ssid !== undefined) setText('stat_wifi_ssid', payload.wifi_ssid);
        if (payload.wifi_bssid !== undefined) setText('stat_wifi_bssid', payload.wifi_bssid);
        if (payload.wifi_rssi !== undefined) setText('stat_wifi_rssi', payload.wifi_rssi);
        if (payload.wifi_ip !== undefined) setText('stat_wifi_ip', payload.wifi_ip);
        if (payload.eth_ip !== undefined) setText('stat_eth_ip', payload.eth_ip);
        if (payload.eth_netmask !== undefined) setText('stat_eth_netmask', payload.eth_netmask);
    }

    // Summary handler
    function handleSummary(obj) {
        applyRuntimeStats(obj);
    }

    function formatUptime(s) {
        const sec = Number(s) || 0;
        const days = Math.floor(sec / 86400);
        const hours = Math.floor((sec % 86400) / 3600);
        const mins = Math.floor((sec % 3600) / 60);
        const secs = sec % 60;
        let human = '';
        if (days) human += days + 'd ';
        human += String(hours).padStart(2, '0') + ':' + String(mins).padStart(2, '0') + ':' + String(secs).padStart(2, '0');
        return human;
    }

    // Tasks messages handler (start/page/end)
    let tasksSeq = 0;
    function handleTasksMessages(obj) {
        const type = obj.type;
        if (type === 'tasks_start') {
            tasksSeq = obj.seq || 0;
            clearTasksTable();
            applyRuntimeStats(obj);
            const totalEl = document.getElementById('tasks_total');
            if (totalEl && typeof obj.total === 'number' && obj.total >= 0) totalEl.textContent = obj.total;
            return;
        }
        if (type === 'tasks_page') {
            if (obj.seq && tasksSeq && obj.seq !== tasksSeq) {
                logDebug('WS: tasks_page seq mismatch', obj.seq, tasksSeq);
                return;
            }
            if (Array.isArray(obj.tasks)) appendTasksRows(obj.tasks);
            return;
        }
        if (type === 'tasks_end') {
            logDebug('WS: tasks_end received seq', obj.seq);
            return;
        }
    }

    // Settings handler
    function handleSettings(obj) {
        if (obj.device_name !== undefined) {
            const el = document.getElementById('cfg_device_name');
            if (el) el.textContent = obj.device_name;
        }
        if (obj.wifi_ssid !== undefined) {
            const el = document.getElementById('cfg_wifi_ssid');
            if (el) el.textContent = obj.wifi_ssid;
        }
        if (obj.mqtt_enabled !== undefined) {
            const el = document.getElementById('cfg_mqtt_enabled');
            if (el) el.textContent = obj.mqtt_enabled ? 'on' : 'off';
        }
        applyPayloadToDOM(obj);
    }

    // Statemachine handling
    const statemachineCache = { totalStates: 0, received: {}, order: [] };

    function handleStatemachineMessage(obj) {
        const type = obj.type || '';

        if (type === 'statemachine_start') {
            statemachineCache.totalStates = Number(obj.total_states || obj.total || 0) || 0;
            statemachineCache.received = {};
            statemachineCache.order = [];
            const container = document.getElementById('statemachine_container');
            if (container) container.innerHTML = '';
            logInfo('Statemachine start, expecting', statemachineCache.totalStates, 'states');
            return;
        }

        if (type === 'statemachine_general') {
            applyPayloadToDOM(obj);
            logDebug('Statemachine general received', obj);
            return;
        }

        if (type === 'statemachine_state') {
            // prefer slot_index, fall back to state_index
            const idx = (typeof obj.slot_index !== 'undefined') ? Number(obj.slot_index) :
                (typeof obj.state_index !== 'undefined') ? Number(obj.state_index) : null;
            if (idx === null) {
                logWarn('statemachine_state without index', obj);
                return;
            }

            // dedupe identical messages arriving quickly
            const dedupeKey = 'sm_' + idx;
            const payloadStr = JSON.stringify(obj);
            const prev = window._sm_dedupe[dedupeKey];
            if (prev && prev.str === payloadStr && (Date.now() - prev.ts) < 1000) {
                logDebug('Duplicate statemachine_state ignored for index', idx);
                return;
            }
            window._sm_dedupe[dedupeKey] = { str: payloadStr, ts: Date.now() };

            statemachineCache.received[idx] = obj;
            statemachineCache.order.push(idx);

            renderStatemachineState(idx, obj);
            logDebug('Statemachine state received index', idx, obj);
            return;
        }

        if (type === 'statemachine_end') {
            logInfo('Statemachine end received. States received:', Object.keys(statemachineCache.received).length);
            finalizeStatemachineUI();
            return;
        }

        logDebug('Unhandled statemachine message type', type);
    }

    function finalizeStatemachineUI() {
        const total = statemachineCache.totalStates || 0;
        if (total > 0) {
            const missing = [];
            for (let i = 0; i < total; ++i) {
                if (!Object.prototype.hasOwnProperty.call(statemachineCache.received, i)) missing.push(i);
            }
            if (missing.length) logWarn('Statemachine missing states:', missing);
            else logInfo('All statemachine states received');
        }
    }

    // DOM helpers
    function clearTasksTable() {
        const tbody = document.getElementById('stat_tasks_full_body');
        if (tbody) tbody.innerHTML = '';
    }

    function appendTasksRows(tasks) {
        const tbody = document.getElementById('stat_tasks_full_body');
        if (!tbody) return;
        tasks.forEach(t => {
            const tr = document.createElement('tr');

            const tdName = document.createElement('td'); tdName.textContent = t.name || ''; tr.appendChild(tdName);
            const tdState = document.createElement('td'); tdState.textContent = t.state || ''; tr.appendChild(tdState);
            const tdPrio = document.createElement('td'); tdPrio.textContent = t.prio !== undefined ? t.prio : ''; tr.appendChild(tdPrio);
            const tdRun = document.createElement('td'); tdRun.textContent = t.run_ticks !== undefined ? t.run_ticks : ''; tr.appendChild(tdRun);
            const tdCpu = document.createElement('td'); tdCpu.textContent = t.cpu_pct !== undefined ? Number(t.cpu_pct).toFixed(2) : ''; tr.appendChild(tdCpu);
            const tdStack = document.createElement('td'); tdStack.textContent = t.stack_high_water !== undefined ? t.stack_high_water : ''; tr.appendChild(tdStack);

            tbody.appendChild(tr);
        });
    }

    // Generic payload -> DOM mapping
    function applyPayloadToDOM(payload) {
        if (!payload || typeof payload !== 'object') return;
        for (const key in payload) {
            if (!Object.prototype.hasOwnProperty.call(payload, key)) continue;
            const el = document.getElementById(key);
            if (!el) continue;
            const val = payload[key];
            if (el.tagName === 'INPUT' && el.type === 'checkbox') el.checked = Boolean(val);
            else if (el.tagName === 'SELECT') {
                const selectValue = (key === 'enable_dhcp' || key === 'eth_enable_dhcp')
                    ? (String(val).toLowerCase() === 'on' ? 'On' : 'Off')
                    : String(val);
                el.value = selectValue;
                if (!Array.from(el.options).some(o => o.value === el.value)) {
                    for (let o of el.options) if (o.text === selectValue) { o.selected = true; break; }
                }
            } else if (el.tagName === 'INPUT' || el.tagName === 'TEXTAREA') el.value = val;
            else el.textContent = val;
        }
    }

    // Render a single statemachine state into the UI (fills s{index}_* placeholders or creates dynamic block)
    function renderStatemachineState(index, stateObj) {
        try {
            logDebug('renderStatemachineState: filling state', index, stateObj);

            function setElementValue(id, value) {
                const el = document.getElementById(id);
                if (!el) return false;
                if (el.tagName === 'INPUT' || el.tagName === 'TEXTAREA' || el.tagName === 'SELECT') {
                    el.value = (value === null || typeof value === 'undefined') ? '' : String(value);
                } else {
                    el.textContent = (value === null || typeof value === 'undefined') ? '' : String(value);
                }
                //logDebug('renderStatemachineState: set', id, '=>', value);
                return true;
            }

            if (typeof stateObj.s_en !== 'undefined') {
                const enVal = stateObj.s_en ? 'On' : 'Off';
                setElementValue('s' + index + '_en', enVal);
            }

            if (typeof stateObj.s_fanspeed !== 'undefined') {
                setElementValue('s' + index + '_fanspeed', stateObj.s_fanspeed);
            }

            if (typeof stateObj.s_name !== 'undefined') {
                setElementValue('s' + index + '_name', stateObj.s_name);
            }

            const numericFields = {
                s_st_h: 's' + index + '_st_h',
                s_st_m: 's' + index + '_st_m',
                s_end_h: 's' + index + '_end_h',
                s_end_m: 's' + index + '_end_m',
                s_dur_m: 's' + index + '_dur_m',
                s_cycl_int_m: 's' + index + '_cycl_int_m'
            };
            for (const key in numericFields) {
                if (!Object.prototype.hasOwnProperty.call(numericFields, key)) continue;
                if (typeof stateObj[key] !== 'undefined') {
                    setElementValue(numericFields[key], stateObj[key]);
                }
            }

            if (Array.isArray(stateObj.s_valves)) {
                stateObj.s_valves.forEach((v, j) => {
                    const vid = 's' + index + '_v' + j + '_pos';
                    setElementValue(vid, v);
                });
            }

            const anyPlaceholder = !!document.getElementById('s' + index + '_name') ||
                !!document.getElementById('s' + index + '_fanspeed') ||
                !!document.getElementById('s' + index + '_v0_pos');

            if (!anyPlaceholder) {
                setTimeout(() => {
                    let container = document.getElementById('statemachine_container');
                    if (!container) {
                        container = document.createElement('div');
                        container.id = 'statemachine_container';
                        const main = document.getElementById('main') || document.body;
                        main.appendChild(container);
                        logDebug('renderStatemachineState: created statemachine_container');
                    }

                    const rowId = 'sm_state_' + index;
                    let row = document.getElementById(rowId);
                    if (!row) {
                        row = document.createElement('div');
                        row.id = rowId;
                        row.className = 'sm-state-row';
                        row.innerHTML = `<h4 class="sm-state-title">State ${index}</h4>
                             <div class="sm-state-body"></div>`;
                        container.appendChild(row);
                        logDebug('renderStatemachineState: created dynamic row', rowId);
                    }

                    const body = row.querySelector('.sm-state-body');
                    if (!body) return;

                    const name = stateObj.s_name || '';
                    const fanspeed = stateObj.s_fanspeed || '';
                    const startH = (typeof stateObj.s_st_h !== 'undefined') ? stateObj.s_st_h : '';
                    const startM = (typeof stateObj.s_st_m !== 'undefined') ? stateObj.s_st_m : '';
                    const endH = (typeof stateObj.s_end_h !== 'undefined') ? stateObj.s_end_h : '';
                    const endM = (typeof stateObj.s_end_m !== 'undefined') ? stateObj.s_end_m : '';
                    const dur = (typeof stateObj.s_dur_m !== 'undefined') ? stateObj.s_dur_m : '';
                    const cycl = (typeof stateObj.s_cycl_int_m !== 'undefined') ? stateObj.s_cycl_int_m : '';
                    const valves = Array.isArray(stateObj.s_valves) ? stateObj.s_valves.join(', ') : '';

                    body.innerHTML = '';
                    const fields = [
                        { label: 'Name', value: name },
                        { label: 'Fanspeed', value: fanspeed },
                        { label: 'Start', value: `${startH}:${String(startM).padStart(2, '0')}` },
                        { label: 'End', value: `${endH}:${String(endM).padStart(2, '0')}` },
                        { label: 'Duration (min)', value: dur },
                        { label: 'Cycle interval (min)', value: cycl },
                        { label: 'Valves', value: valves }
                    ];

                    fields.forEach(f => {
                        const p = document.createElement('div');
                        p.className = 'sm-field';
                        const lbl = document.createElement('strong');
                        lbl.textContent = f.label + ': ';
                        const val = document.createElement('span');
                        val.textContent = (f.value === null || typeof f.value === 'undefined') ? '' : String(f.value);
                        p.appendChild(lbl);
                        p.appendChild(val);
                        body.appendChild(p);
                    });

                    logDebug('renderStatemachineState: rendered dynamic block for', rowId);
                }, 0);
            }
        } catch (err) {
            logError('renderStatemachineState: unexpected error', err);
        }
    }

    // Expose small API for debugging
    window.OSV_WS = {
        requestPageData,
        isLeader: () => isLeader
    };

    // Ensure requestPageData is triggered on load, visibility change, popstate, and page_name changes
    window.addEventListener('load', () => {
        try { requestPageData(); } catch (e) { logDebug('requestPageData on load failed', e); }
    });

    document.addEventListener('visibilitychange', () => {
        if (!document.hidden && isLeader) {
            logDebug('visibilitychange: visible and leader, requesting page data');
            requestPageData();
        }
    });

    window.addEventListener('popstate', () => {
        logDebug('popstate: navigation detected, requesting page data');
        requestPageData();
    });

    (function observePageName() {
        const pageNameEl = document.getElementById('page_name');
        if (pageNameEl) {
            const mo = new MutationObserver((mutations) => {
                for (const m of mutations) {
                    if (m.type === 'characterData' || m.type === 'childList' || m.type === 'subtree') {
                        logDebug('page_name changed, requesting page data');
                        requestPageData();
                        break;
                    }
                }
            });
            mo.observe(pageNameEl, { childList: true, subtree: true, characterData: true });
        } else {
            // fallback: try again shortly (in case page_name is created later)
            setTimeout(() => {
                const el = document.getElementById('page_name');
                if (el) {
                    const mo2 = new MutationObserver(() => { requestPageData(); });
                    mo2.observe(el, { childList: true, subtree: true, characterData: true });
                }
            }, 500);
        }
    })();

    // Optional: attach nav click hooks to force request after navigation
    document.addEventListener('click', (ev) => {
        const target = ev.target;
        if (!target) return;
        // adjust selector to match your nav links if needed
        if (target.matches && target.matches('.nav-link, .nav-link *')) {
            setTimeout(() => {
                const name = document.getElementById('page_name')?.textContent?.trim().toLowerCase();
                if (name) requestPageData();
            }, 50);
        }
    });

    // Start WebSocket
    window.addEventListener('load', initWebSocket);

})();
