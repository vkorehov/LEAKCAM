#!/usr/bin/env node
// Search EU distributors (no customs, no EUR 3 per-tariff-line duty) through the
// logged-in Chromium, so an AliExpress price can always be compared like-for-like.
//
//   node eu_search.js "NTC 10K 3435" [--vendor farnell|tme|all] [--max N] [--json]
//
// Since 2026-07-01 every non-EU consignment pays EUR 3 per customs tariff LINE, so a
// EUR 0.59 AliExpress pigtail really costs EUR 3.59 unless something else in the order
// shares its tariff heading. Below ~EUR 5 an EU distributor is usually cheaper outright.
const p = require("puppeteer-core");
const PORT = process.env.ALI_PORT || 9223;

const args = process.argv.slice(2);
const asJson = args.includes("--json");
const val = (f, d) => { const i = args.indexOf(f); return i < 0 ? d : args[i + 1]; };
const vendor = (val("--vendor", "all") || "all").toLowerCase();
const max = parseInt(val("--max", "8"), 10);
const query = args.filter((a, i) => !a.startsWith("--") && !(i > 0 && args[i - 1].startsWith("--"))).join(" ");
if (!query) { console.error('usage: eu_search.js "query" [--vendor farnell|tme|all] [--max N] [--json]'); process.exit(2); }

// lv.farnell.com renders EUR for a Latvian account. NOTE: since 2026-07-01 Farnell
// requires a valid EU VAT number on the account for some EU delivery options.
const VENDORS = {
  farnell: {
    url: q => `https://lv.farnell.com/search?st=${encodeURIComponent(q)}`,
    scrape: () => {
      const out = [];
      document.querySelectorAll('span[class*="TotalPriceValue"]').forEach(sp => {
        let row = sp.closest("tr") || sp.closest('div[class*="ProductListerTable"]');
        for (let i = 0; i < 6 && row && (row.innerText || "").length < 40; i++) row = row.parentElement;
        if (!row) return;
        const a = row.querySelector('a[href*="/dp/"]');
        const txt = (row.innerText || "").replace(/\s*\n\s*/g, " | ").trim();
        out.push({ price: sp.innerText.trim(), desc: txt.slice(0, 150),
                   url: a ? a.href : null });
      });
      return out;
    }
  },
  // TME: PARTIALLY WORKING (2026-09-21). The page renders and consent dismisses, but the
  // result rows lazy-load and the account defaults to "USD net" even on the /lv/ site.
  // To finish it: set currency to EUR in the TME UI once (the profile keeps it), raise the
  // post-consent wait, and re-probe selectors - they were not `tr[data-productid]`.
  tme: {
    url: q => `https://www.tme.eu/lv/en/katalog/?queryPhrase=${encodeURIComponent(q)}`,
    scrape: () => {
      const out = [];
      document.querySelectorAll('[data-product-symbol], tr[data-productid], .product-row').forEach(row => {
        const t = (row.innerText || "").replace(/\s*\n\s*/g, " | ").trim();
        if (!/\d+[.,]\d{2}/.test(t)) return;
        const a = row.querySelector("a[href]");
        out.push({ price: (t.match(/\d+[.,]\d{2}\s?(€|EUR)?/) || [""])[0], desc: t.slice(0, 150),
                   url: a ? a.href : null });
      });
      return out;
    }
  }
};

async function dismissConsent(pg) {
  // Click consent inside the page: elementHandle.click() HANGS if the button is
  // covered or never becomes visible, which is exactly what these overlays do.
  try {
    return await pg.evaluate(() => {
      const cands = [...document.querySelectorAll('button,a,[role="button"]')];
      const hit = cands.find(e => /accept|agree|allow all|piekr/i.test(e.innerText || e.id || ""));
      if (hit) { hit.click(); return true; }
      return false;
    });
  } catch (e) { return false; }
}

(async () => {
  const b = await p.connect({ browserURL: `http://127.0.0.1:${PORT}`, defaultViewport: null });
  const pg = await b.newPage();
  const names = vendor === "all" ? Object.keys(VENDORS) : [vendor];
  const results = {};
  for (const n of names) {
    const v = VENDORS[n];
    if (!v) { console.error(`unknown vendor ${n}`); continue; }
    try {
      await pg.goto(v.url(query), { waitUntil: "domcontentloaded", timeout: 45000 });
      await new Promise(r => setTimeout(r, 4000));
      await dismissConsent(pg);
      await new Promise(r => setTimeout(r, n === "tme" ? 9000 : 4000));
      results[n] = (await pg.evaluate(v.scrape)).slice(0, max);
    } catch (e) { results[n] = [{ error: e.message }]; }
  }
  await pg.close(); await b.disconnect();

  if (asJson) { console.log(JSON.stringify(results, null, 1)); return; }
  for (const [n, rows] of Object.entries(results)) {
    console.log(`\n##### ${n.toUpperCase()}  "${query}"  (${rows.length})`);
    if (!rows.length) console.log("   no results (or scrape selector stale)");
    for (const r of rows) {
      if (r.error) { console.log(`   ERROR ${r.error}`); continue; }
      console.log(`   ${String(r.price).padEnd(10)} ${r.desc}`);
      if (r.url) console.log(`              ${r.url}`);
    }
  }
})().catch(e => { console.error("ERR", e.message); process.exit(1); });
