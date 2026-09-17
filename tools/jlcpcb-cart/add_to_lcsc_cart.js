#!/usr/bin/env node
// Add every line of an LCSC order list to the lcsc.com shopping cart through your own
// logged-in Chromium (puppeteer-core over the DevTools protocol).  Calls observed with
// dump_requests.js on 2026-09-15:
//   GET  https://wmsc.lcsc.com/wmsc/login/user/info                      null result = not logged in
//   POST https://wmsc.lcsc.com/ftps/wm/search/third  {"currentPage":1,"pageSize":10,"productCode":"C3010298","from":"detail"}
//        -> "Other suppliers" (marketplace) offers: productModel (offer title), productCodeManufacturer (MPN),
//           vendorCode, productSource ("waldom"...), supplyChannelType ("lc_order"), minBuyNumber, productPriceList
//   POST https://wmsc.lcsc.com/wmsc/cart/add/batch
//        LCSC own stock:   [{"quantity":50,"productMpn":"CL10A105KB8NNNC","customerTag":"","cartSource":"product_detail","price":"0.56",
//                            "isReel":false,"productSource":null,"productSourcePartId":null,"productCode":"C15849","searchZone":""}]
//        other supplier:   [{"quantity":1,"productMpn":"W25N02KVZEIR WINBOND 26+","cartSource":"product_list","price":"9.91",
//                            "productSource":"waldom","productSourcePartId":null,"productType":"lc_order","productModel":"W25N02KVZEIR",
//                            "vendorCode":"G10874","searchZone":""}]
//   GET  https://wmsc.lcsc.com/wmsc/cart/product/index?searchType=all&availabilitySortType=   cart contents
// LCSC's own product facts (MPN, MOQ, price ladder, reel flag, stock) come from the product
// page's embedded __NEXT_DATA__ JSON; lcsc.com has no open search API (payloads are encrypted).
//
// isReel is ALWAYS false: the product data's isReel means "tape & reel packaging", but in the cart it
// selects LCSC's continuous-reel service at reelPrice (3 USD) PER LINE (45 lines = 135 USD, 2026-09-15).
// Quantities are rounded UP to a multiple of the offer's minBuyNumber.  For every line the
// LCSC own line and each other-supplier offer are costed at their own MOQ-adjusted quantity;
// an offer wins when it is at least THIRD_WIN cheaper in total (or LCSC has no sellable
// line).  The dry run prints exactly what would be added and from whom.
//
//   ./chromium-debug.sh   (or any Chromium with --remote-debugging-port=9222, logged in to lcsc.com)
//   node add_to_lcsc_cart.js add ../../LCSC_parts_cart.csv [--dry-run]
//   node add_to_lcsc_cart.js cart
//   node add_to_lcsc_cart.js info C3010298          own line + other-supplier offers

const puppeteer = require('puppeteer-core');
const fs = require('fs');

const CDP_URL = process.env.CDP_URL || 'http://127.0.0.1:9222';
const WMSC = 'https://wmsc.lcsc.com/';
const THIRD_WIN = 0.8;   // other-supplier total must be <= 80 % of LCSC's own total to be chosen
const sleep = ms => new Promise(r => setTimeout(r, ms));

async function session() {
  const browser = await puppeteer.connect({ browserURL: CDP_URL, defaultViewport: null });
  const pages = await browser.pages();
  let page = pages.find(p => p.url().includes('lcsc.com'));
  if (!page) { page = await browser.newPage(); await page.goto('https://www.lcsc.com/', { waitUntil: 'networkidle2', timeout: 60000 }); }
  await page.bringToFront();
  // The lcsc.com home page navigates on its own (region redirect, banners), which kills the
  // execution context mid-fetch. Park the tab on a stable same-origin page and retry any
  // evaluate that dies with "Execution context was destroyed".
  const park = async () => { try { await page.goto('https://www.lcsc.com/products', { waitUntil: 'domcontentloaded', timeout: 60000 }); } catch {} };
  if (!/lcsc\.com\/(products|product-detail)/.test(page.url())) await park();
  const ev = async (fn, ...args) => {
    for (let i = 0; ; i++) {
      try { return await page.evaluate(fn, ...args); }
      catch (e) {
        if (i >= 3 || !/context was destroyed|Target closed|detached/i.test(e.message)) throw e;
        await sleep(1500); await park();
      }
    }
  };
  const fetchJson = (url, body) => ev(async (url, body) => {
    const r = await fetch(url, body === undefined ? { credentials: 'include' } : { method: 'POST', credentials: 'include', headers: { 'content-type': 'application/json' }, body: JSON.stringify(body) });
    const text = await r.text(); try { return { status: r.status, json: JSON.parse(text) }; } catch { return { status: r.status, text }; }
  }, url, body);
  const fetchText = url => ev(async url => (await fetch(url, { credentials: 'include' })).text(), url);
  const me = await fetchJson(WMSC + 'wmsc/login/user/info');
  if (!me.json?.result) { console.error('not logged in to lcsc.com in that Chromium window; log in and rerun'); process.exit(2); }
  return { browser, page, fetchJson, fetchText };
}

// LCSC's own line, from the product page's __NEXT_DATA__ (null when lcsc.com does not sell the part: 404 page)
async function own(fetchText, code) {
  const html = await fetchText(`https://www.lcsc.com/product-detail/${code}.html`);
  const m = html.match(/<script id="__NEXT_DATA__"[^>]*>([\s\S]*?)<\/script>/);
  if (!m) return null;
  let hit = null;
  const walk = v => {
    if (hit || !v || typeof v !== 'object') return;
    if (Array.isArray(v)) return v.forEach(walk);
    if (v.productCode === code && v.minBuyNumber !== undefined && v.productPriceList) { hit = v; return; }
    Object.values(v).forEach(walk);
  };
  walk(JSON.parse(m[1]));
  // isForeignDisplay=false: page renders, stock shows, but the cart answers "This product does not exist" (C3012377, C7507400)
  if (hit && hit.isForeignDisplay === false) { hit.notSoldAbroad = true; return null; }
  return hit;
}

async function others(fetchJson, code) {
  const r = await fetchJson(WMSC + 'ftps/wm/search/third', { currentPage: 1, pageSize: 10, productCode: code, from: 'detail' });
  return (r.json?.result?.productList || []).filter(o => o.isOnsale !== false && o.productPriceList?.length);
}

function ladderPrice(p, qty) {
  const tiers = (p.productPriceList || []).map(t => ({ ladder: +t.ladder, price: t.usdPrice ?? +t.productPrice ?? t.currencyPrice })).filter(t => t.price !== undefined && !isNaN(t.price)).sort((a, b) => a.ladder - b.ladder);
  let price = tiers[0]?.price; for (const t of tiers) if (qty >= t.ladder) price = t.price;
  return price;
}

// cost an offer (own line or other supplier) for a wanted quantity: MOQ, multiples, stock
function cost(p, wanted) {
  const moq = Math.max(1, +p.minBuyNumber || 1);
  const qty = Math.ceil(Math.max(wanted, moq) / moq) * moq;
  const price = ladderPrice(p, qty);
  if (price === undefined || (p.stockNumber !== undefined && p.stockNumber !== null && p.stockNumber < qty)) return null;
  return { qty, moq, price, total: price * qty };
}

// pick where to buy: LCSC own unless an other-supplier offer is clearly cheaper (or own is unavailable)
function choose(ownP, offers, wanted) {
  const o = ownP && cost(ownP, wanted);
  // an offer whose MOQ forces 10x the wanted quantity AND more than 5 USD is a reel, not a substitute (BLM15AG102SN1D: moq 10000 = 46 USD)
  const alts = offers.map(p => ({ p, c: cost(p, wanted) })).filter(a => a.c && !(a.c.qty > 10 * wanted && a.c.total > 5)).sort((a, b) => a.c.total - b.c.total);
  const best = alts[0];
  if (o && !(best && best.c.total <= THIRD_WIN * o.total)) return { kind: 'own', p: ownP, c: o, saving: best ? o.total - best.c.total : null };
  if (best) return { kind: 'other', p: best.p, c: best.c, saving: o ? o.total - best.c.total : null };
  return null;
}

function payload(pick, code) {
  const { p, c } = pick;
  if (pick.kind === 'own') return { quantity: c.qty, productMpn: p.productModel, customerTag: '', cartSource: 'product_detail', price: String(c.price), isReel: false, productSource: null, productSourcePartId: null, productCode: code, searchZone: '' };
  return { quantity: c.qty, productMpn: p.productModel, cartSource: 'product_list', price: String(c.price), productSource: p.productSource, productSourcePartId: null, productType: p.supplyChannelType || 'lc_order', productModel: p.productCodeManufacturer, vendorCode: p.vendorCode, searchZone: '' };
}

function describe(code, wanted, pick) {
  const { p, c } = pick;
  const who = pick.kind === 'own' ? 'LCSC' : `${p.productSource}/${p.vendorCode}`;
  const q = c.qty === wanted ? `x${c.qty}` : `x${c.qty} (bom ${wanted}, moq ${c.moq})`;
  return `${code.padEnd(11)} ${q.padEnd(26)} ${String(c.price).padStart(8)} USD = ${c.total.toFixed(2).padStart(6)}  ${who.padEnd(14)} stock=${String(p.stockNumber ?? '?').padEnd(8)} ${pick.kind === 'own' ? p.productModel : p.productModel}` + (pick.saving && pick.kind === 'other' ? `  (saves ${pick.saving.toFixed(2)} vs LCSC)` : '');
}

function readCsv(file) {
  return fs.readFileSync(file, 'utf8').split(/\r?\n/).slice(1).map(l => l.trim()).filter(Boolean).map(l => {
    const [code, qty] = l.split(',').map(s => s.trim().replace(/^"|"$/g, ''));
    if (!/^C\d+$/.test(code)) throw new Error('bad LCSC number in CSV: ' + l);
    return { code, qty: parseInt(qty || '1', 10) || 1 };
  });
}

async function cart(fetchJson) {
  const r = await fetchJson(WMSC + 'wmsc/cart/product/index?searchType=all&availabilitySortType=');
  const res = r.json?.result;
  if (!res) { console.log(JSON.stringify(r, null, 1).slice(0, 1500)); return; }
  let total = 0, n = 0;
  // other-supplier lines carry no productCode/productDetail here: cartType "lc_order", vendorCode, productModel (MPN), productMpn (offer title)
  const show = g => { const other = !g.productCode; const d = g.productDetail || {}; const price = other ? undefined : ladderPrice(d, g.productQuantity); if (price) total += price * g.productQuantity; n++;
    console.log(`${String(g.productCode || g.productModel || '-').padEnd(24)} x${String(g.productQuantity).padEnd(5)} ${String(price ?? '?').padStart(8)} USD  ${(other ? `other/${g.vendorCode}` : 'LCSC').padEnd(14)} moq=${String(d.minBuyNumber ?? '?').padEnd(4)} stock=${String(d.stockNumber ?? '?').padEnd(8)} ${g.productMpn || ''}`); };
  for (const g of res.cartProductList || []) show(g);
  for (const g of res.unavailableProductList || []) { n++; console.log(`${String(g.productCode || g.productModel || '-').padEnd(24)} x${String(g.productQuantity).padEnd(5)}        ? USD  UNAVAILABLE    ${g.productMpn || ''}`); }
  console.log(`${n} lines, ~${total.toFixed(2)} USD at ladder prices for the LCSC-own lines (other-supplier prices are not in this listing)`);
}

async function add(csv, dryRun) {
  const rows = readCsv(csv);
  console.log(`${rows.length} parts from ${csv}${dryRun ? ' (dry run: nothing added)' : ''}`);
  const { browser, fetchJson, fetchText } = await session();
  let ok = 0, total = 0, nOther = 0; const failed = [];
  for (const r of rows) {
    const [o, offers] = await Promise.all([own(fetchText, r.code), others(fetchJson, r.code)]);
    const pick = choose(o, offers, r.qty);
    if (!pick) { console.log(`FAIL ${r.code.padEnd(11)} x${String(r.qty).padEnd(5)} ${o ? 'no offer covers the quantity (stock)' : 'not sold on lcsc.com'}${offers.length ? ` (${offers.length} other-supplier offers, none usable)` : ''}`); failed.push(r.code); continue; }
    total += pick.c.total; if (pick.kind === 'other') nOther++;
    const line = describe(r.code, r.qty, pick);
    if (dryRun) { console.log('ok   ' + line); ok++; await sleep(200); continue; }
    const res = await fetchJson(WMSC + 'wmsc/cart/add/batch', [payload(pick, r.code)]);
    const good = res.status === 200 && (res.json?.code === 200 || res.json?.ok === true);
    console.log(`${good ? 'ok  ' : 'FAIL'} ${line}  -> ${res.status} ${res.json?.msg || res.json?.message || res.text?.slice(0, 100) || 'ok'}`);
    if (good) ok++; else failed.push(r.code);
    await sleep(500);
  }
  browser.disconnect();
  console.log(`\n${ok}/${rows.length} ${dryRun ? 'resolved' : 'added'}, ${nOther} from other suppliers, ~${total.toFixed(2)} USD at ladder prices` + (failed.length ? `; failed: ${failed.join(' ')}` : ''));
  if (failed.length) process.exit(1);
}

(async () => {
  const [mode, arg, flag] = process.argv.slice(2);
  if (mode === 'add' && arg) await add(arg, flag === '--dry-run');
  else if (mode === 'cart') { const { browser, fetchJson } = await session(); await cart(fetchJson); browser.disconnect(); }
  else if (mode === 'info' && arg) {
    const { browser, fetchJson, fetchText } = await session();
    const o = await own(fetchText, arg); const offers = await others(fetchJson, arg);
    console.log('LCSC own:', o ? `${o.productModel} moq=${o.minBuyNumber} stock=${o.stockNumber} ladder=${JSON.stringify((o.productPriceList || []).map(t => [t.ladder, t.usdPrice ?? t.productPrice]))}` : 'none');
    for (const p of offers) console.log(`other: ${p.productSource}/${p.vendorCode} "${p.productModel}" mpn=${p.productCodeManufacturer} moq=${p.minBuyNumber} stock=${p.stockNumber} ladder=${JSON.stringify(p.productPriceList.map(t => [t.ladder, t.usdPrice]))}`);
    browser.disconnect();
  }
  else { console.error('usage: add_to_lcsc_cart.js add parts.csv [--dry-run] | cart | info C15849'); process.exit(2); }
})();
