'use strict';

// Live illustrated vehicle: uses existing DriveTrace telemetry, not fabricated sensor readings.
let carVisibleX = null;
let carSpeedSmoothed = 0;
let lastCarFrameMs = null;
let carAnimationPaused = false;
const carReducedMotion = window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches;

function updateCarScene(state) {
  const rawSpeed = Number(state.speed_kmh);
  const speed = Number.isFinite(rawSpeed) ? Math.max(0, rawSpeed) : 0;
  const active = speed > 0.5;
  const limit = Number(state.limit_kmh) || 50;
  const overspeed = speed > limit;
  $('roadLiveSpeed').textContent = speed.toFixed(1);
  $('roadSpeedLimit').textContent = String(Math.round(limit));
  $('roadMotionText').textContent = active ? (state.demo_running ? 'Auto driving' : 'Driving') : 'Parked';
  $('roadAlertText').textContent = overspeed ? '⚠ Over speed limit' : 'Within limit';
  $('roadAlertText').classList.toggle('road-alert-danger', overspeed);
  $('roadStatus').textContent = active ? '● MOVING' : '● STATIONARY';
  $('roadStatus').classList.toggle('is-active', active);
  $('roadViewport').classList.toggle('road-is-overspeed', overspeed);
  $('roadViewport').setAttribute('aria-label', 'Illustrated ' + (active ? 'moving' : 'parked') + ' car. Speed ' + speed.toFixed(1) + ' km/h, limit ' + limit + ' km/h.');
}

function animateRoadCar(now) {
  const road = $('roadViewport');
  if (!road) return;
  const last = lastCarFrameMs == null ? now : lastCarFrameMs;
  const dt = Math.max(0, Math.min(0.07, (now-last)/1000));
  lastCarFrameMs = now;
  const speed = lastState && Number.isFinite(Number(lastState.speed_kmh)) ? Math.max(0,Number(lastState.speed_kmh)) : 0;
  const animate = !carAnimationPaused && !carReducedMotion && speed > 0.5;
  const easing = Math.min(1, dt * 7);
  carSpeedSmoothed += ((animate ? speed : 0) - carSpeedSmoothed) * easing;
  const frameWidth = road.clientWidth;
  const carWidth = Math.min(224, frameWidth * .36);
  if (carVisibleX === null) carVisibleX = frameWidth * .08;
  if (animate && frameWidth > carWidth) {
    carVisibleX += dt * (45 + Math.min(160, speed) * 2.9);
    if (carVisibleX > frameWidth + 12) carVisibleX = -carWidth;
  }
  const car = $('roadCar');
  car.style.transform = 'translate3d(' + carVisibleX.toFixed(1) + 'px,0,0)';
  car.dataset.moving = animate ? 'true' : 'false';
  car.style.setProperty('--wheel-spin-duration', Math.max(.15, 1.8 - Math.min(160, carSpeedSmoothed) * .018).toFixed(2) + 's');
  const lane = $('roadLaneMarks');
  const offset = Number(lane.dataset.offset || 0);
  const nextOffset = animate ? (offset + dt * (30 + speed * 4.5)) % 134 : offset;
  lane.dataset.offset = String(nextOffset);
  lane.style.backgroundPositionX = (-nextOffset).toFixed(1) + 'px';
  requestAnimationFrame(animateRoadCar);
}

const $ = id => document.getElementById(id);
const svgNS = 'http://www.w3.org/2000/svg';
let lastState = null;
let limitFocused = false;
let toastTimer = null;
let selectedJourney = null;
let knownJourneyCount = -1;

function svgElement(name, attrs = {}, content = null) {
  const el = document.createElementNS(svgNS, name);
  for (const [key, value] of Object.entries(attrs)) el.setAttribute(key, String(value));
  if (content !== null) el.textContent = content;
  return el;
}
function toast(message, error = false) {
  const bar = $('toast');
  bar.textContent = message;
  bar.classList.toggle('error', error);
  bar.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { bar.hidden = true; }, 3800);
}
function number(value, fallback = '—') {
  return Number.isFinite(Number(value)) && value !== null ? Number(value).toFixed(1) : fallback;
}
function dateLabel(iso) {
  if (!iso) return '—';
  const d = new Date(iso);
  return isNaN(d.getTime()) ? '—' : d.toLocaleString(undefined, {month:'short', day:'numeric', hour:'2-digit',minute:'2-digit',second:'2-digit'});
}
function drawGauge(speed) {
  const angle = Math.PI - Math.PI * Math.min(1, Math.max(0, speed / 160));
  const x = 170 + 135 * Math.cos(angle);
  const y = 166 - 135 * Math.sin(angle);
  const end = speed < 0.05 ? 'M 35 166' : `M 35 166 A 135 135 0 0 1 ${x.toFixed(2)} ${y.toFixed(2)}`;
  $('gaugeProgress').setAttribute('d', end);
  $('gaugeNumber').textContent = Math.round(speed);
  if ($('gaugeTicks').childNodes.length === 0) {
    for (let i = 0; i <= 8; i++) {
      const theta = Math.PI - i * Math.PI / 8;
      const x1 = 170 + 110 * Math.cos(theta), y1 = 166 - 110 * Math.sin(theta);
      const x2 = 170 + 119 * Math.cos(theta), y2 = 166 - 119 * Math.sin(theta);
      $('gaugeTicks').appendChild(svgElement('line', {x1:x1.toFixed(2), y1:y1.toFixed(2), x2:x2.toFixed(2), y2:y2.toFixed(2)}));
    }
  }
}
function drawChart(history, limit, paused = false) {
  const W = 740, H = 248, L = 40, R = 13, T = 15, B = 20;
  const bottom = H - B, right = W - R;
  const maxValue = Math.max(70, limit * 1.2, ...history.map(v => v.speed || 0));
  const yMax = Math.ceil(maxValue / 20) * 20;
  const y = v => bottom - v / yMax * (bottom - T);
  const newest = paused && history.length ? history[history.length - 1].t : Date.now() / 1000;
  const x = t => L + Math.min(1, Math.max(0, 1 - (newest - t) / 30)) * (right - L);
  const grid = $('chartGrid'), labels = $('chartLabels');
  grid.replaceChildren(); labels.replaceChildren();
  for (let i = 0; i <= 4; i++) {
    const value = (yMax / 4) * i, py = y(value);
    grid.appendChild(svgElement('line', {x1:L,y1:py,x2:right,y2:py,stroke:'#e9eef6','stroke-width':'1','stroke-dasharray':i === 0 ? '0' : '3 6'}));
    labels.appendChild(svgElement('text',{x:L-11,y:py+3,'text-anchor':'end',fill:'#a1b0c6','font-size':11,'font-weight':650},Math.round(value)));
  }
  for (let i = 0; i <= 4; i++) {
    let px = L + (right - L) * i / 4;
    grid.appendChild(svgElement('line',{x1:px,y1:T,x2:px,y2:bottom,stroke:'#f1f4f9','stroke-width':'1'}));
  }
  if (limit <= yMax) {
    const ly = y(limit);
    grid.appendChild(svgElement('line',{x1:L,y1:ly,x2:right,y2:ly,stroke:'#eaaa55','stroke-width':'1.5','stroke-dasharray':'7 6'}));
    labels.appendChild(svgElement('text',{x:right-3,y:ly-7,'text-anchor':'end',fill:'#d39b4e','font-size':10,'font-weight':800},`${Math.round(limit)} km/h limit`));
  }
  const points = history.filter(p => Number.isFinite(p.t) && Number.isFinite(p.speed) && p.t >= newest - 30.5).map(p => [x(p.t), y(p.speed)]);
  if (!points.length) {
    $('chartLine').setAttribute('d',''); $('chartArea').setAttribute('d',''); $('chartDot').style.display='none'; return;
  }
  const coords = points.map(p => `${p[0].toFixed(1)} ${p[1].toFixed(1)}`);
  const path = `M ${coords.join(' L ')}`;
  $('chartLine').setAttribute('d',path);
  $('chartArea').setAttribute('d', `${path} L ${points[points.length-1][0].toFixed(1)} ${bottom} L ${points[0][0].toFixed(1)} ${bottom} Z`);
  $('chartDot').style.display='block';
  $('chartDot').setAttribute('cx',points[points.length-1][0]);
  $('chartDot').setAttribute('cy',points[points.length-1][1]);
}
function render(state) {
  lastState = state;
  const hardware = state.mode === 'HARDWARE' && state.hardware_connected;
  const replay = state.mode === 'REPLAY' || state.mode === 'REPLAY_COMPLETE';
  const waiting = state.mode === 'WAITING_FOR_HARDWARE';
  const liveFeed = hardware || state.mode === 'REPLAY';
  const statusText = hardware ? 'HARDWARE MODE' : replay ? 'OFFLINE REPLAY' : waiting ? 'AWAITING ESP32' : 'SIMULATION MODE';
  $('topMode').textContent = '● ' + statusText;
  $('modeNoticeText').textContent = hardware
    ? 'Real USB packets are arriving. The device still requires sensor calibration and physical tests.'
    : replay ? 'REPLAY DATA ONLY: synthetic Hall / IMU / GPS packets are passing through the telemetry receiver. No physical ESP32 is attached.'
    : waiting ? 'Waiting for ESP32 USB serial data. No recent readings have been received.'
    : 'SIMULATION MODE: all readings are generated locally. No physical sensor is attached.';
  $('modeNoticeEnd').textContent = hardware ? 'ESP32 CONNECTED' : replay ? 'SYNTHETIC DATA' : waiting ? 'NOT CONNECTED' : 'NO LIVE HARDWARE';
  const diag = state.sensor_snapshot || {};
  $('feedStatusBadge').textContent = statusText;
  $('feedSource').textContent = state.feed_source === 'HARDWARE' ? 'ESP32 USB serial' : state.feed_source === 'REPLAY' ? 'Offline replay' : 'Simulation';
  $('hallPulses').textContent = diag.wheel_pulses == null ? 'Not available' : diag.wheel_pulses + ' pulses';
  $('imuStatus').textContent = diag.imu_valid ? ('ax ' + number(diag.imu_ax_g) + ' g (raw)') : 'Not available';
  $('gpsSpeed').textContent = diag.gps_valid ? (number(diag.gps_speed_kmh) + ' km/h') : 'No valid fix';
  $('gpsComparison').textContent = state.gps_consistency === 'GPS_AGREES' ? 'Within preliminary tolerance'
    : state.gps_consistency === 'GPS_DISAGREES' ? 'Difference detected — review'
    : 'Not available';
  $('packetAge').textContent = state.feed_age_seconds == null ? 'No packets' : number(state.feed_age_seconds) + ' s ago';
  $('hardwareMessage').textContent = waiting ? 'Check board COM port, USB cable, pyserial and firmware.'
    : state.mode === 'REPLAY_COMPLETE' ? 'The offline replay has finished. Stored events remain available.'
    : replay ? 'Offline test only. This does not measure a real vehicle.'
    : hardware ? (state.hardware_status || 'USB connected; calibrate before physical use.')
    : 'No physical sensors connected. Use --replay-hardware to test the USB-like data path.';
  $('speedSourceLabel').textContent = liveFeed ? (hardware ? 'ESP32 wheel speed' : 'Offline replay speed') : 'Simulated wheel speed';
  $('historyDescription').textContent = 'Last 30 seconds · ' + (hardware ? 'USB sensor data' : replay ? 'offline replay' : 'simulation');
  document.querySelectorAll('button[data-action]').forEach(button => {
    if (['start','accelerate','brake','stop','demo','reset'].includes(button.dataset.action))
      button.disabled = waiting || replay || hardware;
  });
  $('metricCurrent').textContent = number(state.speed_kmh);
  $('metricPeak').textContent = number(state.peak_recent_kmh);
  $('peakDelta').textContent = `${state.limit_kmh} km/h limit`;
  $('limitReference').textContent = `${state.limit_kmh} km/h`;
  $('metricCurrentStatus').textContent = state.speed_kmh > 0 ? '● In motion' : '● Stationary';
  $('metricCurrentStatus').className = 'metric-chip ' + (state.speed_kmh > 0 ? 'chip-positive' : 'chip-neutral');
  const overspeed = state.overspeed_now || state.overspeed_recent;
  $('metricLimitStatus').textContent = overspeed ? 'OVERSPEED' : 'NORMAL';
  $('metricLimitStatus').parentElement.classList.toggle('is-danger',overspeed);
  $('metricLimitBadge').textContent = overspeed ? '● Threshold exceeded' : '● Within threshold';
  $('metricLimitBadge').className = 'metric-chip ' + (overspeed ? 'chip-danger' : 'chip-positive');
  $('driveBadge').innerHTML = '<span class="small-led"></span>' + (state.demo_running ? 'AUTO-DEMO' : state.running ? 'DRIVING' : 'IDLE');
  $('driveBadge').classList.toggle('is-active',state.running);
  $('gaugeStatus').textContent = state.speed_kmh > 0 ? (state.overspeed_now ? 'SPEED LIMIT EXCEEDED' : 'VEHICLE MOVING') : 'VEHICLE STOPPED';
  if (!limitFocused) $('speedLimitInput').value = state.limit_kmh;
  drawGauge(state.speed_kmh);
  updateCarScene(state);
  const paused = !state.running && !state.feed_active;
  const preservedHistory = (state.history && state.history.length) ? state.history :
    (state.latest_event?.samples || []).map(p => ({t:p.t, speed:p.speed_kmh}));
  drawChart(preservedHistory, state.limit_kmh, paused);
  $('historyDescription').textContent = (paused ? 'Last captured 30 seconds' : 'Live 30-second window') +
      ' · ' + (hardware ? 'USB sensor data' : replay ? 'offline replay' : 'simulation');
  renderTft(state);
  const event = state.latest_event;
  if (event && event.event_type === 'STOP') {
    $('emptyEvidence').hidden = true; $('evidenceDetails').hidden = false;
    $('evidenceSpeed').textContent = number(event.speed_before_stop_kmh);
    $('metricBefore').textContent = number(event.speed_before_stop_kmh);
    $('evidenceMax').textContent = number(event.maximum_recent_speed_kmh) + ' km/h';
    $('evidenceOverspeed').textContent = event.overspeed_detected ? 'YES' : 'NO';
    $('evidenceOverspeed').className = event.overspeed_detected ? 'negative' : 'positive';
    $('evidenceTime').textContent = dateLabel(event.recorded_at_utc);
    $('evidenceIntegrity').textContent = state.latest_integrity ? '✓ Verified' : '✕ Failed';
    $('evidenceIntegrity').className = state.latest_integrity ? 'positive' : 'negative';
  } else if (!event) {
    $('emptyEvidence').hidden = false; $('evidenceDetails').hidden = true;
  }
}
async function loadState() {
  try {
    const response = await fetch('/api/state', {cache:'no-store'});
    if (!response.ok) throw new Error('Unable to read telemetry');
    render(await response.json());
  } catch (error) {
    if (!window._connectionError) toast('Dashboard cannot reach local simulator. Is the server running?', true);
    window._connectionError=true;
  }
}
async function action(name, value = undefined) {
  try {
    const response = await fetch('/api/action',{method:'POST',headers:{'Content-Type':'application/json','X-DriveTrace-Client':'dashboard'},body:JSON.stringify({action:name,value})});
    const data = await response.json();
    if (!response.ok || !data.ok) throw new Error(data.error || 'Action failed');
    toast(data.message || (data.verified ? 'Integrity verified.' : 'Action completed.'), name === 'verify' && !data.verified);
    await Promise.all([loadState(),loadEvents()]);
  } catch (error) {toast(error.message || 'Action could not complete',true);}
}
function cell(row,text) { const td=document.createElement('td');td.textContent=text;row.appendChild(td);return td; }
async function loadEvents() {
  try {
    const response = await fetch('/api/events',{cache:'no-store'});
    if (!response.ok) throw new Error('Could not load events');
    const events = await response.json();
    $('eventCount').textContent = events.length >= 24 ? '24+' : events.length;
    const body=$('eventRows');body.replaceChildren();
    if (!events.length) {
      const tr=document.createElement('tr');const td=cell(tr,'No recorded events. Run the automatic demonstration to create one.');td.colSpan=6;td.className='empty-row';body.appendChild(tr);return;
    }
    for (const event of events) {
      const tr=document.createElement('tr');
      const typeCell=cell(tr,''); const type=document.createElement('span');type.className='event-pill '+(event.type==='TAMPER'?'tamper':'stop');type.textContent=event.type || 'UNKNOWN';typeCell.appendChild(type);
      cell(tr,dateLabel(event.recorded_at_utc));
      cell(tr,event.speed_before_stop_kmh == null ? '—' : number(event.speed_before_stop_kmh)+' km/h');
      cell(tr,event.maximum_recent_speed_kmh == null ? '—' : number(event.maximum_recent_speed_kmh)+' km/h');
      const integCell=cell(tr,'');const badge=document.createElement('span');badge.className='integrity-pill '+(event.integrity==='PASS'?'pass':'fail');badge.textContent=event.integrity==='PASS'?'✓ Verified':'✕ Failed';integCell.appendChild(badge);
      const fileCell=cell(tr,'');const link=document.createElement('a');link.className='file-link';link.href='/api/event/'+encodeURIComponent(event.filename);link.textContent='↓ JSON';link.setAttribute('download',event.filename);fileCell.appendChild(link);body.appendChild(tr);
    }
  } catch(error) {toast('Unable to load event records',true);}
}

function renderTft(state) {
  $('tftSpeed').textContent = number(state.speed_kmh);
  $('tftPeak').textContent = number(Math.max(state.peak_recent_kmh || 0, state.latest_event?.maximum_recent_speed_kmh || 0)) + ' km/h';
  $('tftLimit').textContent = number(state.limit_kmh) + ' km/h';
  $('tftStop').textContent = state.latest_event ? number(state.latest_event.speed_before_stop_kmh) + ' km/h' : '—';
  $('tftMotion').textContent = state.speed_kmh > .5 ? 'MOVING' : 'STATIONARY';
  $('tftRecording').textContent = state.journey_recording ? '● ACTIVE' : 'STANDBY';
  $('rideRecordingBadge').textContent = state.journey_recording ? '● LOGGING RIDE' : '● STANDBY';
  const red = state.overspeed_now || state.overspeed_recent;
  $('tftWarning').textContent = red ? '⚠ OVERSPEED EVENT · DEMO INPUT' : 'READY · DEMO / SENSOR INPUT';
  $('tftWarning').classList.toggle('danger',red);
}
function friendlyDuration(seconds) {
  const s = Math.max(0,Math.round(seconds || 0));
  return Math.floor(s/3600) ? `${Math.floor(s/3600)}h ${Math.floor(s%3600/60)}m` : `${Math.floor(s/60)}m ${s%60}s`;
}
function drawJourneyGraph(points, limit=50, duration=0) {
  const line=$('journeyLine'), fill=$('journeyFill');
  if (!points?.length) {line.setAttribute('d','');fill.setAttribute('d','');return;}
  const W=546,left=42,top=13,base=159, maxSpeed=Math.max(60,limit*1.2,...points.map(v=>v.speed));
  const maxY=Math.ceil(maxSpeed/20)*20, maxT=Math.max(1,duration);
  const x = v=> left + W*Math.max(0,Math.min(1,v.t/maxT));
  const y = v=> base - (base-top)*Math.max(0,Math.min(1,v/maxY));
  const path=points.map((v,i)=>`${i?'L':'M'} ${x(v).toFixed(1)} ${y(v.speed).toFixed(1)}`).join(' ');
  line.setAttribute('d',path);
  fill.setAttribute('d',path+` L ${x(points[points.length-1])} ${base} L ${x(points[0])} ${base} Z`);
  const limitY=y(limit); $('journeyLimit').setAttribute('y1',limitY);$('journeyLimit').setAttribute('y2',limitY);
  $('journeyYTop').textContent=Math.round(maxY)+' km/h';
  $('journeyXEnd').textContent='Ride duration '+friendlyDuration(duration);
}
async function showJourney(id) {
  selectedJourney=id || null;
  if (!id) {drawJourneyGraph([]);return;}
  try {
    const response=await fetch('/api/journey/'+encodeURIComponent(id),{cache:'no-store'});
    if (!response.ok) throw new Error('Journey detail unavailable');
    const data=await response.json(), s=data.summary;
    $('ridePeak').textContent=number(s.peak_kmh)+' km/h';
    $('rideDuration').textContent=friendlyDuration(s.duration_seconds);
    $('rideDistance').textContent=number(s.distance_km_estimate)+' km';
    $('rideIntegrity').textContent=s.verification==='VERIFIED_LOCAL'?'✓ Local HMAC':s.verification==='UNVERIFIED_IMPORT'?'Unverified import':s.verification==='RECORDING'?'Recording':'Check failed';
    $('downloadJourney').hidden=false;
    $('downloadJourney').href='/api/journey/'+encodeURIComponent(id)+'.csv';
    drawJourneyGraph(data.samples,s.limit_kmh,s.duration_seconds);
  } catch(err) {toast(err.message,true);}
}
async function loadJourneys(force=false) {
  try {
    const response=await fetch('/api/journeys',{cache:'no-store'});
    if(!response.ok)throw new Error('Unable to read journeys');
    const items=await response.json();
    const select=$('journeySelect');
    const ids=items.map(v=>v.id).join(',');
    if (force || select.dataset.ids!==ids) {
      const retain=selectedJourney;
      select.replaceChildren();
      if (!items.length) {
        const option=document.createElement('option');option.value='';option.textContent='No journeys yet';select.appendChild(option);
        $('downloadJourney').hidden=true;
        $('ridePeak').textContent=$('rideDuration').textContent=$('rideDistance').textContent=$('rideIntegrity').textContent='—';
        drawJourneyGraph([]);
        selectedJourney=null;
      } else {
        for(const item of items) {
          const option=document.createElement('option');option.value=item.id;
          option.textContent=dateLabel(item.start_utc)+' · '+number(item.peak_kmh)+' km/h · '+(item.origin==='MICROSD_IMPORT'?'microSD':item.status==='RECORDING'?'Recording':'PC');
          select.appendChild(option);
        }
        const picked=items.some(x=>x.id===retain)?retain:items[0].id;
        select.value=picked;
        await showJourney(picked);
      }
      select.dataset.ids=ids;
    } else if (selectedJourney) {
      await showJourney(selectedJourney);
    }
  } catch(err) {toast('Journey records unavailable',true);}
}
async function sendCsv(url, file=null) {
  try {
    if (file && file.size>8*1024*1024)throw new Error('CSV must be smaller than 8 MB');
    const headers={'X-DriveTrace-Client':'dashboard'};
    if(file)headers['Content-Type']='text/csv';
    const result=await fetch(url,{method:'POST',headers,body:file||null});
    const body=await result.json();
    if(!result.ok || !body.ok)throw new Error(body.error||'Import failed');
    toast('Journey imported. External SD card files remain unverified.');
    selectedJourney=body.journey.id;
    await loadJourneys(true);
  } catch(err) {toast(err.message,true);}
}

function init() {
  $('roadPauseButton').addEventListener('click',()=>{carAnimationPaused=!carAnimationPaused;$('roadPauseButton').textContent=carAnimationPaused?'Resume animation':'Pause animation';$('roadPauseButton').setAttribute('aria-pressed',String(carAnimationPaused));});
  requestAnimationFrame(animateRoadCar);
  document.querySelectorAll('button[data-action]').forEach(button => button.addEventListener('click',() => action(button.dataset.action)));
  $('speedLimitInput').addEventListener('focus',() => {limitFocused=true;});
  $('speedLimitInput').addEventListener('blur',() => {limitFocused=false;});
  $('speedLimitInput').addEventListener('keydown', event => { if(event.key==='Enter') $('applyLimit').click(); });
  $('applyLimit').addEventListener('click',()=> {
    const value=Number($('speedLimitInput').value);
    if (!$('speedLimitInput').value.trim() || !Number.isFinite(value) || value < 1 || value > 200) {toast('Enter a speed limit between 1 and 200 km/h',true);return;}
    action('set_limit',value);
  });
  $('verifyLatest').addEventListener('click',()=>action('verify_latest'));
  $('refreshEvents').addEventListener('click',()=>loadEvents());
  $('teacherDemo').addEventListener('click',()=>sendCsv('/api/journeys/demo'));
  $('importMicroSD').addEventListener('click',()=>{
    const file=$('sdCsvFile').files?.[0];
    if(!file){toast('Choose a microSD CSV file first',true);return;}
    sendCsv('/api/journeys/import',file);
  });
  $('journeySelect').addEventListener('change',e=>showJourney(e.target.value));
  document.querySelectorAll('.nav-link').forEach(a => a.addEventListener('click',()=>{
    document.querySelectorAll('.nav-link').forEach(link=>link.classList.toggle('active',link===a));
  }));
  loadState();loadEvents();loadJourneys();
  setInterval(loadState,500);
  setInterval(loadEvents,3500);
  setInterval(loadJourneys,3000);
}
document.addEventListener('DOMContentLoaded',init);
