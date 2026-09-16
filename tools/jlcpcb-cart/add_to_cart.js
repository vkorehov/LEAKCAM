#!/usr/bin/env node
// Add every line of an LCSC order list to the JLCPCB parts cart through your own
// logged-in Chromium (puppeteer-core over the DevTools protocol).  JLCPCB has no
// public cart API; the calls below are the ones the parts-search page makes
// (found with dump_requests.js, 2026-09-15):
//   POST .../shoppingCart/smtGood/selectSmtComponentList/v2   keyword -> componentId
//   POST .../smtComponentShopCart/addSmtComponentGoods         {componentCode, lcscComponentId, presaleNumber}
// Both need the session's `secretkey` / `x-xsrf-token` headers, which the script
// copies from the first API request the page fires after a reload.
//
//   chromium --remote-debugging-port=9222 --user-data-dir=~/.config/jlcpcb-chromium   (log in)
//   node add_to_cart.js add ../../JLCPCB_parts_cart.csv [--dry-run]
//   node add_to_cart.js cart                                                           (list what the cart holds)
//   node add_to_cart.js lookup C15849                                                  (debug: raw search hit)
//
// CSV: header line, then "C-number,quantity" per row (JLCPCB_parts_cart.csv from find-cheapest-part).

const puppeteer = require('puppeteer-core');
const fs = require('fs');

const CDP_URL = process.env.CDP_URL || 'http://127.0.0.1:9222';
const CART_URL = 'https://jlcpcb.com/user-center/smtPrivateLibrary/partsCart/';
const API = 'https://jlcpcb.com/api/overseas-pcb-order/v1/';
const KEEP_HEADERS = ['secretkey', 'x-xsrf-token', 'accept', 'content-type'];

const sleep = ms => new Promise(r => setTimeout(r, ms));
const atLogin = u => { const x = new URL(u); return /passport/i.test(x.hostname) || /login|signin/i.test(x.pathname + x.hash); };   // host passport.jlcpcb.com or #/login; pathname alone missed the SSO page

async function session() {
  const browser = await puppeteer.connect({ browserURL: CDP_URL, defaultViewport: null });
  const pages = await browser.pages();
  const page = pages.find(p => p.url().includes('jlcpcb.com')) || await browser.newPage();
  await page.bringToFront();
  // capture the session headers from whatever API call the page makes while (re)loading
  const captured = new Promise(resolve => page.on('request', req => {
    const h = req.headers();
    if (req.url().startsWith(API) && h.secretkey) resolve(Object.fromEntries(KEEP_HEADERS.filter(k => h[k]).map(k => [k, h[k]])));
  }));
  await page.goto(CART_URL, { waitUntil: 'networkidle2', timeout: 60000 });
  if (atLogin(page.url())) { console.error('not logged in: log in to jlcpcb.com in that Chromium window, then rerun'); process.exit(2); }
  const headers = await Promise.race([captured, sleep(20000).then(() => null)]);
  if (!headers) { console.error('no API request with a secretkey header seen on the cart page'); process.exit(2); }
  headers['content-type'] = 'application/json';
  const call = (path, body) => page.evaluate(async (url, headers, body) => {   // body undefined -> GET
    const r = await fetch(url, body === undefined ? { headers, credentials: 'include' } : { method: 'POST', headers, body: JSON.stringify(body), credentials: 'include' });
    const text = await r.text(); try { return { status: r.status, json: JSON.parse(text) }; } catch { return { status: r.status, text }; }
  }, API + path, headers, body);
  return { browser, page, call };
}

async function cart(call) {   // what the parts cart holds now
  const r = await call('smtComponentShopCart/querySmtCartGoods?pageSize=100&currentPage=1&_t=' + Date.now());
  const d = r.json?.data; const list = d?.goodsResultVOList;
  if (!list) { console.log(JSON.stringify(r, null, 1).slice(0, 2000)); return; }
  for (const g of list) console.log(`${String(g.componentCode).padEnd(11)} x${String(g.presaleNumber).padEnd(5)} ${String(g.goodsMoney).padStart(7)} USD  stock=${String(g.stockCount).padEnd(8)} ${g.componentModelEn || ''}`);
  console.log(`${d.totalSize} lines, ${d.totalPrice} USD in the parts cart`);
}

async function lookup(call, code) {
  const r = await call('shoppingCart/smtGood/selectSmtComponentList/v2', {
    currentPage: 1, pageSize: 25, presaleType: 'stock', searchType: 2, keyword: code, componentLibraryType: null, stockFlag: null, stockSort: null,
    firstSortName: null, secondSortName: null, componentBrandList: [], searchSource: 'search', componentSpecificationList: [], componentAttributeList: [] });
  const list = r.json?.data?.componentPageInfo?.list || r.json?.data?.list || [];
  return { hit: list.find(c => c.componentCode === code), raw: r };
}

function readCsv(file) {
  return fs.readFileSync(file, 'utf8').split(/\r?\n/).slice(1).map(l => l.trim()).filter(Boolean).map(l => {
    const [code, qty] = l.split(',').map(s => s.trim().replace(/^"|"$/g, ''));
    if (!/^C\d+$/.test(code)) throw new Error('bad LCSC number in CSV: ' + l);
    return { code, qty: parseInt(qty || '1', 10) || 1 };
  });
}

async function add(csv, dryRun) {
  const rows = readCsv(csv);
  console.log(`${rows.length} parts from ${csv}${dryRun ? ' (dry run: lookups only)' : ''}`);
  const { browser, call } = await session();
  let ok = 0; const failed = [];
  for (const r of rows) {
    const { hit, raw } = await lookup(call, r.code);
    if (!hit) { console.log(`FAIL ${r.code.padEnd(11)} x${String(r.qty).padEnd(4)} not found by search (${raw.status} ${raw.json?.message || ''})`); failed.push(r.code); continue; }
    const id = hit.componentId ?? hit.lcscComponentId ?? hit.id;
    const stock = hit.stockCount ?? '?';
    if (dryRun) { console.log(`ok   ${r.code.padEnd(11)} x${String(r.qty).padEnd(4)} id=${id} stock=${stock} ${hit.componentModelEn || ''}`); ok++; await sleep(300); continue; }
    const res = await call('smtComponentShopCart/addSmtComponentGoods', {
      goodsType: 5, turnBuy: false, smtComponentAddCartGoodsVOS: [{ componentCode: r.code, lcscComponentId: id, presaleNumber: r.qty, presaleType: 'stock' }] });
    const good = res.status === 200 && (res.json?.code === 200 || res.json?.success === true);
    console.log(`${good ? 'ok  ' : 'FAIL'} ${r.code.padEnd(11)} x${String(r.qty).padEnd(4)} id=${id} stock=${stock} ${res.status} ${res.json?.message || res.text?.slice(0, 100) || ''}`);
    if (good) ok++; else failed.push(r.code);
    await sleep(600);
  }
  browser.disconnect();
  console.log(`\n${ok}/${rows.length} ${dryRun ? 'resolved' : 'added'}` + (failed.length ? `; failed: ${failed.join(' ')}` : ''));
  if (failed.length) process.exit(1);
}

(async () => {
  const [mode, arg, flag] = process.argv.slice(2);
  if (mode === 'add' && arg) await add(arg, flag === '--dry-run');
  else if (mode === 'cart') { const { browser, call } = await session(); await cart(call); browser.disconnect(); }
  else if (mode === 'lookup' && arg) { const { browser, call } = await session(); const { hit, raw } = await lookup(call, arg); console.log(JSON.stringify(hit || raw, null, 1).slice(0, 3000)); browser.disconnect(); }
  else { console.error('usage: add_to_cart.js add parts.csv [--dry-run] | cart | lookup C15849'); process.exit(2); }
})();
