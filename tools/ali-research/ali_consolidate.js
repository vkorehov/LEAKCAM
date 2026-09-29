#!/usr/bin/env node
// Group a whole BOM by SELLER, because AliExpress shipping is charged PER SELLER.
// A 6-line BOM spread over 6 stores pays 6 shipping charges - often more than the goods.
//
//   node ali_consolidate.js bom.txt [--pages 2] [--cands 40] [--ship 1.70] [--json]
//
// bom.txt: one line per BOM item, `search query | match-regex` (regex optional but
// strongly recommended - see the --match note in SKILL.md).
//
// Output ranks stores by how many BOM lines each can cover, so you can collapse
// shipping charges. It does NOT prove a store has the right VARIANT in stock -
// always confirm the winners with ali_item.js before ordering.
const p = require("puppeteer-core");
const fs = require("fs");
const PORT = process.env.ALI_PORT || 9223;

const args = process.argv.slice(2);
const asJson = args.includes("--json");
const num = (f, d) => { const i = args.indexOf(f); return i < 0 ? d : parseFloat(args[i + 1]); };
const pages = num("--pages", 2), cands = num("--cands", 5), shipCost = num("--ship", 1.70);
// SortType matters a lot here. price_asc surfaces one-off cheap listings from many tiny
// sellers, which is exactly wrong for consolidation; default relevance surfaces the big
// broad-catalogue stores (TZT, diymore, Aideepen...) that can cover several BOM lines.
// Default to relevance; pass --price-sort to force price_asc.
const sortParam = args.includes("--price-sort") ? "SortType=price_asc&" : "";
const file = args.find(a => !a.startsWith("--") && !/^[\d.]+$/.test(a));
if (!file) { console.error('usage: ali_consolidate.js bom.txt [--pages N] [--cands N] [--ship 1.70]'); process.exit(2); }

const lines = fs.readFileSync(file, "utf8").split("\n").map(l => l.trim())
  .filter(l => l && !l.startsWith("#"))
  .map(l => { const [q, m] = l.split("|").map(x => (x || "").trim()); return { q, m: m || null }; });

(async () => {
  const b = await p.connect({ browserURL: `http://127.0.0.1:${PORT}`, defaultViewport: null });
  const pgs = await b.pages();
  const pg = pgs.find(x => x.url().includes("aliexpress")) || pgs[0];

  const perLine = [];
  for (const { q, m } of lines) {
    const slug = q.replace(/\s+/g, "-").toLowerCase();
    const re = m ? new RegExp(m, "i") : null;
    const seen = new Map();
    for (let page = 1; page <= pages; page++) {
      await pg.goto(`https://www.aliexpress.com/w/wholesale-${slug}.html?${sortParam}page=${page}`,
        { waitUntil: "domcontentloaded", timeout: 60000 });
      for (let i = 0; i < 25; i++) {
        const ok = await pg.evaluate(() => { try { return !!window._dida_config_._init_data_.data.data.root.fields.mods.itemList; } catch (e) { return false; } });
        if (ok) break;
        await new Promise(r => setTimeout(r, 800));
      }
      const items = await pg.evaluate(() => {
        const l = window._dida_config_._init_data_.data.data.root.fields.mods?.itemList?.content || [];
        return l.map(it => ({ id: it.productId || it.productIdStr, title: it.title?.displayTitle || "",
          price: it.prices?.salePrice?.formattedPrice, num: it.prices?.salePrice?.minPrice ?? null }));
      }).catch(() => []);
      for (const it of items) if (it.id && (!re || re.test(it.title)) && !seen.has(it.id)) seen.set(it.id, it);
    }
    perLine.push({ q, items: [...seen.values()].sort((a, c) => (a.num ?? 1e9) - (c.num ?? 1e9)).slice(0, cands) });
  }

  // Seller is NOT in the search payload (verified: 0/60 items carry store.storeName).
  // It only exists on the product page, so every candidate must be opened. That is the
  // expensive part - keep --cands small (default 5) or this takes many minutes.
  for (const L of perLine) {
    for (const it of L.items) {
      try {
        await pg.goto(`https://www.aliexpress.com/item/${it.id}.html`, { waitUntil: "domcontentloaded", timeout: 60000 });
        for (let i = 0; i < 20; i++) {
          const ok = await pg.evaluate(() => { try { return !!window._d_c_.lifeCycleEventList[0].data.SHOP_CARD_PC; } catch (e) { return false; } });
          if (ok) break;
          await new Promise(r => setTimeout(r, 700));
        }
        const info = await pg.evaluate(() => {
          const d = window._d_c_.lifeCycleEventList[0].data;
          let ship = null;
          try {
            const o = (d.SHIPPING?.deliveryLayoutInfo || []).map(x => x?.bizData).filter(Boolean)
              .map(x => ({ a: parseFloat(x.displayAmount), t: x.formattedAmount, f: x.shippingFee === "free" }))
              .filter(x => x.t);
            ship = o.find(x => x.f) ? "FREE" : (o.filter(x => !isNaN(x.a)).sort((p, q2) => p.a - q2.a)[0]?.t || null);
          } catch (e) {}
          return { store: d.SHOP_CARD_PC?.storeName || null, ship };
        });
        it.store = info.store; it.ship = info.ship;
      } catch (e) { it.store = null; }
    }
  }
  await b.disconnect();

  // store -> which BOM lines it can cover (cheapest offer per line)
  const stores = new Map();
  perLine.forEach((L, li) => {
    const best = new Map();
    for (const it of L.items) if (!best.has(it.store)) best.set(it.store, it);
    for (const [store, it] of best) {
      if (!stores.has(store)) stores.set(store, new Map());
      stores.get(store).set(li, it);
    }
  });
  const ranked = [...stores.entries()].map(([s, m]) => ({ store: s, n: m.size,
    goods: [...m.values()].reduce((a, i) => a + (i.num || 0), 0), lines: m })).sort((a, c) => c.n - a.n || a.goods - c.goods);

  if (asJson) { console.log(JSON.stringify(ranked.slice(0, 15).map(r => ({ store: r.store, covers: r.n,
    items: [...r.lines.entries()].map(([li, i]) => ({ line: lines[li].q, id: i.id, price: i.price })) })), null, 1)); return; }

  const tot = perLine.reduce((a, L) => a + L.items.length, 0);
  const res = perLine.reduce((a, L) => a + L.items.filter(i => i.store).length, 0);
  console.log(`seller resolution: ${res}/${tot} candidates resolved` + (res < tot * 0.6 ? "  <-- LOW, product pages may not have loaded" : ""));
  perLine.forEach(L => console.log(`   "${L.q}": ${L.items.map(i => i.store || "?").join(", ")}`));
  console.log(`BOM lines: ${lines.length}   naive worst case: ${lines.length} sellers x EUR ${shipCost.toFixed(2)} = EUR ${(lines.length * shipCost).toFixed(2)} shipping\n`);
  console.log("=== stores ranked by BOM coverage ===");
  for (const r of ranked.slice(0, 12)) {
    if (r.n < 2) continue;
    console.log(`  ${String(r.n)}/${lines.length}  ${r.store}`);
    for (const [li, it] of r.lines) console.log(`         ${String(it.price).padEnd(10)} ship=${String(it.ship||"?").padEnd(7)} ${lines[li].q}  (${it.id})`);
  }
  if (!ranked.some(r => r.n >= 2)) console.log("  no store covers more than one line - consolidation not possible on these queries");
  const bestN = ranked.length ? ranked[0].n : 1;
  console.log(`\nbest single store covers ${bestN}/${lines.length} lines -> saves up to EUR ${((bestN - 1) * shipCost).toFixed(2)} of shipping`);
  console.log("NOTE: coverage is by listing title only. Confirm the VARIANT and stock with ali_item.js before ordering.");
})().catch(e => { console.error("ERR", e.message); process.exit(1); });
