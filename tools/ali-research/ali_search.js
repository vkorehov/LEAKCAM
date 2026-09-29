#!/usr/bin/env node
// Price-ascending AliExpress search through the logged-in Chromium.
//   node ali_search.js "fisheye csi camera module" [--pages 3] [--max 20] [--json]
// Prints the listing MINIMUM price (that is all a search page carries) plus itemId.
// Always follow up with ali_item.js on anything interesting: the minimum is often a
// sold-out or base variant and is NOT the price of the variant you actually want.
const p = require("puppeteer-core");
const PORT = process.env.ALI_PORT || 9223;

const args = process.argv.slice(2);
const asJson = args.includes("--json");
const num = (f, d) => { const i = args.indexOf(f); return i < 0 ? d : parseInt(args[i + 1], 10); };
const pages = num("--pages", 2), max = num("--max", 40);
const str = (f, d) => { const i = args.indexOf(f); return i < 0 ? d : args[i + 1]; };
// AliExpress matches keywords loosely, so SortType=price_asc floats cheap UNRELATED
// parts to the top (LEDs, sensors...). --match is effectively mandatory on any broad
// query: it is a case-insensitive regex applied to the listing title.
const match = str("--match", null);
// AliExpress ship-from filter. Goods already in EU free circulation are NOT an import:
// no EUR 3 per-tariff-line customs duty (in force since 2026-07-01), no courier handling fee.
// Verified: shipFromCountry=XX is a real server-side filter; an invalid code (ZZ) is
// IGNORED and returns the unfiltered set, so always sanity-check counts against no-filter.
// ES/PL/CZ/NL share one EU warehouse pool (near-identical results); DE is separate/thinner.
const shipFrom = str("--ship-from", null);
const matchRe = match ? new RegExp(match, "i") : null;
const query = args.filter((a, i) => !a.startsWith("--") && !(i > 0 && args[i - 1].startsWith("--"))).join(" ");
if (!query) { console.error('usage: ali_search.js "query" [--pages N] [--max N] [--json]'); process.exit(2); }

const slug = query.trim().replace(/\s+/g, "-").toLowerCase();

(async () => {
  const b = await p.connect({ browserURL: `http://127.0.0.1:${PORT}`, defaultViewport: null });
  const pgs = await b.pages();
  const pg = pgs.find(x => x.url().includes("aliexpress")) || pgs[0];
  const seen = new Map();

  for (let page = 1; page <= pages; page++) {
    const url = `https://www.aliexpress.com/w/wholesale-${slug}.html?SortType=price_asc&page=${page}`
      + (shipFrom ? `&shipFromCountry=${shipFrom}` : "");
    await pg.goto(url, { waitUntil: "domcontentloaded", timeout: 60000 });
    for (let i = 0; i < 25; i++) {
      const ok = await pg.evaluate(() => {
        try { return !!window._dida_config_._init_data_.data.data.root.fields.mods.itemList; } catch (e) { return false; }
      });
      if (ok) break;
      await new Promise(r => setTimeout(r, 800));
    }
    const items = await pg.evaluate(() => {
      const mods = window._dida_config_._init_data_.data.data.root.fields.mods;
      const list = mods?.itemList?.content || [];
      return list.map(it => ({
        itemId: it.productId || it.productIdStr,
        title: it.title?.displayTitle || it.title?.seoTitle || "",
        minPrice: it.prices?.salePrice?.formattedPrice || null,
        minPriceNum: it.prices?.salePrice?.minPrice ?? null,
        sold: it.trade?.realTradeCount || it.trade?.tradeDesc || null,
        rating: it.evaluation?.starRating ?? null,
        // NOTE: search results carry NO seller information (verified 2026-09-21:
        // 0/60 items had store.storeName). The seller is only on the product page
        // (SHOP_CARD_PC.storeName) - use ali_item.js / ali_consolidate.js for it.
        choice: !!it.productTags?.some?.(t => /choice/i.test(JSON.stringify(t)))
      }));
    }).catch(() => []);
    for (const it of items) if (it.itemId && !seen.has(it.itemId)) seen.set(it.itemId, it);
    if (!items.length) break;
  }
  await b.disconnect();

  const out0 = matchRe ? [...seen.values()].filter(r => matchRe.test(r.title)) : [...seen.values()];
  const out = out0.sort((a, c) => (a.minPriceNum ?? 1e9) - (c.minPriceNum ?? 1e9)).slice(0, max);
  if (asJson) { console.log(JSON.stringify(out, null, 1)); return; }
  console.log(`query="${query}"  ${out.length} listings (price asc; MIN-variant price only)\n`);
  for (const r of out) {
    console.log(`${String(r.minPrice).padEnd(10)} ${String(r.sold ?? "").padEnd(12)} ${r.itemId}`);
    console.log(`           ${r.title.slice(0, 110)}`);
  }
})().catch(e => { console.error("ERR", e.message); process.exit(1); });
