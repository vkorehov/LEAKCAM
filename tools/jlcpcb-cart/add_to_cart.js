#!/usr/bin/env node
// Add every line of an LCSC order list to the JLCPCB parts cart, through your own
// logged-in Chromium.  JLCPCB has no public API for the cart, so the script learns
// the request the page makes when you add ONE part by hand, then replays it for
// every CSV row from inside the page (same cookies, same headers, same session).
//
//   1. chromium --remote-debugging-port=9222          (log in to jlcpcb.com in it)
//   2. node add_to_cart.js record                     (add C15849 x7 by hand once)
//   3. node add_to_cart.js add ../../JLCPCB_parts_cart.csv [--dry-run]
//
// CSV: header line, then "C-number,quantity" per row (BOM_LCSC's JLCPCB_parts_cart.csv).
// Re-run `record` if `add` starts returning login/auth errors: the captured
// headers may carry a token that expired.

const puppeteer = require('puppeteer-core');
const fs = require('fs');
const path = require('path');

const CDP_URL = process.env.CDP_URL || 'http://127.0.0.1:9222';
const CART_URL = 'https://jlcpcb.com/user-center/smtPrivateLibrary/partsCart/';
const CFG = path.join(__dirname, 'cart_api.json');
const MARK_CODE = 'C15849';   // Samsung 1uF 0603, Basic part, always in stock
const MARK_QTY = 7;           // odd quantity so the qty field is unambiguous in the request
const DROP_HEADERS = new Set(['content-length', 'cookie', 'host', 'origin', 'referer', 'accept-encoding', 'connection']);

const sleep = ms => new Promise(r => setTimeout(r, ms));

async function cartPage() {
  const browser = await puppeteer.connect({ browserURL: CDP_URL, defaultViewport: null });
  const pages = await browser.pages();
  const page = pages.find(p => p.url().includes('jlcpcb.com')) || await browser.newPage();
  await page.bringToFront();
  if (!page.url().startsWith(CART_URL)) await page.goto(CART_URL, { waitUntil: 'networkidle2', timeout: 60000 });
  if (/login|passport|signin/i.test(page.url())) {
    console.error('not logged in: log in to jlcpcb.com in that Chromium window, then rerun');
    process.exit(2);
  }
  return { browser, page };
}

function readCsv(file) {
  return fs.readFileSync(file, 'utf8').split(/\r?\n/).slice(1).map(l => l.trim()).filter(Boolean).map(l => {
    const [code, qty] = l.split(',').map(s => s.trim().replace(/^"|"$/g, ''));
    if (!/^C\d+$/.test(code)) throw new Error('bad LCSC number in CSV: ' + l);
    return { code, qty: parseInt(qty || '1', 10) || 1 };
  });
}

// ---------- record: capture the add-to-cart request while the user clicks once
async function record() {
  const { browser, page } = await cartPage();
  console.log(`In the browser: search for ${MARK_CODE}, set quantity ${MARK_QTY}, add it to the parts cart.`);
  console.log('Waiting for that request (10 min)...');
  const found = await new Promise(resolve => {
    const t = setTimeout(() => resolve(null), 600000);
    page.on('request', req => {
      const url = req.url();
      if (!url.includes('jlcpcb.com/api/')) return;
      const body = req.postData() || '';
      if (!(url + body).includes(MARK_CODE)) return;
      if (/select|search|list|detail|query/i.test(url.split('/api/')[1] || '') && !body.includes(String(MARK_QTY))) return; // searches, not the add
      clearTimeout(t);
      const headers = {};
      for (const [k, v] of Object.entries(req.headers())) if (!k.startsWith(':') && !DROP_HEADERS.has(k.toLowerCase())) headers[k] = v;
      resolve({ url, method: req.method(), headers, body, code: MARK_CODE, qty: MARK_QTY, recorded: new Date().toISOString() });
    });
  });
  browser.disconnect();
  if (!found) { console.error('no add-to-cart request seen'); process.exit(1); }
  fs.writeFileSync(CFG, JSON.stringify(found, null, 2));
  console.log(`captured ${found.method} ${found.url}\nbody: ${found.body.slice(0, 300)}\nsaved ${CFG}`);
}

// ---------- add: substitute code + qty into the template and fire it from the page
function substitute(tpl, code, qty) {
  const url = tpl.url.split(tpl.code).join(code).replace(new RegExp(`([?&][^=&]*(qty|quantity|num|count)[^=&]*=)${tpl.qty}\\b`, 'i'), `$1${qty}`);
  let body = tpl.body;
  if (body) {
    try {
      const walk = v => {
        if (Array.isArray(v)) return v.map(walk);
        if (v && typeof v === 'object') return Object.fromEntries(Object.entries(v).map(([k, x]) => [k,
          x === tpl.code ? code : (/qty|quantity|num|count/i.test(k) && (x === tpl.qty || x === String(tpl.qty))) ? (typeof x === 'string' ? String(qty) : qty) : walk(x)]));
        return v;
      };
      body = JSON.stringify(walk(JSON.parse(body)));
    } catch {
      body = body.split(tpl.code).join(code).replace(new RegExp(`((qty|quantity|num|count)[^=&:]*[=:]"?)${tpl.qty}\\b`, 'i'), `$1${qty}`);
    }
  }
  return { url, body };
}

async function add(csv, dryRun) {
  if (!fs.existsSync(CFG)) { console.error(`no ${CFG}: run "record" first`); process.exit(2); }
  const tpl = JSON.parse(fs.readFileSync(CFG, 'utf8'));
  const rows = readCsv(csv);
  console.log(`${rows.length} parts from ${csv}; template ${tpl.method} ${tpl.url} (recorded ${tpl.recorded})`);
  if (dryRun) { for (const r of rows) { const s = substitute(tpl, r.code, r.qty); console.log(`${r.code} x${r.qty}\n  ${s.url}\n  ${s.body}`); } return; }
  const { browser, page } = await cartPage();
  let ok = 0; const failed = [];
  for (const r of rows) {
    const s = substitute(tpl, r.code, r.qty);
    const res = await page.evaluate(async (url, method, headers, body) => {
      const resp = await fetch(url, { method, headers, body: method === 'GET' ? undefined : body, credentials: 'include' });
      return { status: resp.status, text: await resp.text() };
    }, s.url, tpl.method, tpl.headers, s.body);
    let msg = res.text.slice(0, 120).replace(/\s+/g, ' ');
    let good = res.status === 200;
    try { const j = JSON.parse(res.text); good = good && (j.code === 200 || j.success === true || j.code === '200'); msg = j.message || j.msg || msg; } catch { }
    console.log(`${good ? 'ok  ' : 'FAIL'} ${r.code.padEnd(11)} x${String(r.qty).padEnd(4)} ${res.status} ${msg}`);
    if (good) ok++; else failed.push(r.code);
    await sleep(600);
  }
  await page.reload({ waitUntil: 'networkidle2' }).catch(() => { });
  browser.disconnect();
  console.log(`\n${ok}/${rows.length} added` + (failed.length ? `; failed: ${failed.join(' ')}` : ''));
  if (failed.length) process.exit(1);
}

const [mode, arg, flag] = process.argv.slice(2);
if (mode === 'record') record();
else if (mode === 'add' && arg) add(arg, flag === '--dry-run');
else { console.error('usage: add_to_cart.js record | add parts.csv [--dry-run]'); process.exit(2); }
