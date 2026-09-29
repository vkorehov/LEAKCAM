#!/usr/bin/env node
// Dump every purchasable SKU variant of an AliExpress listing, with its OWN price
// and stock, through the user's logged-in Chromium on the DevTools port.
//
//   node ali_item.js <url-or-itemId> [more...]        pretty table
//   node ali_item.js --json <url-or-itemId> [more...] machine readable
//
// Why this exists: AliExpress search pages and any static fetch only ever expose the
// listing's MINIMUM variant price ("from EUR 9.99"). That minimum is frequently a
// SOLD-OUT or narrow/base variant, so quoting it understates the real cost. The
// per-variant prices live only in page JS (window._d_c_), so the page must execute.
const p = require("puppeteer-core");

const PORT = process.env.ALI_PORT || 9223;

function itemUrl(a) {
  if (/^\d+$/.test(a)) return `https://www.aliexpress.com/item/${a}.html`;
  return a.split("?")[0];
}

async function scrape(pg, url) {
  await pg.goto(url, { waitUntil: "domcontentloaded", timeout: 60000 });
  // the SKU/PRICE blocks are populated by page JS after first paint
  for (let i = 0; i < 30; i++) {
    const ok = await pg.evaluate(() => {
      try { return !!window._d_c_.lifeCycleEventList[0].data.SKU; } catch (e) { return false; }
    });
    if (ok) break;
    await new Promise(r => setTimeout(r, 1000));
  }
  return pg.evaluate(() => {
    const d = window._d_c_.lifeCycleEventList[0].data;
    const priceMap = d.PRICE?.skuIdStrPriceInfoMap || {};
    const qtyMap = d.QUANTITY_PC?.allSkuQuantityView || {};

    // skuAttr looks like "14:29#200 degree MF;200007763:201336100" -> human label
    const label = a => (a || "").split(";").map(s => s.includes("#") ? s.split("#")[1] : null)
      .filter(Boolean).join(" / ");
    // fall back to resolving property ids when no #label is embedded
    const props = d.SKU?.skuProperties || [];
    const nameFor = attr => {
      const parts = (attr || "").split(";").map(s => s.split("#")[0]);
      const out = [];
      for (const part of parts) {
        const [pid, vid] = part.split(":");
        const prop = props.find(x => String(x.skuPropertyId) === pid);
        if (!prop) continue;
        const v = (prop.skuPropertyValues || []).find(x => String(x.propertyValueIdLong) === vid);
        if (v) out.push(`${prop.skuPropertyName}=${v.propertyValueDisplayName || v.propertyValueDefinitionName}`);
      }
      return out.join(" / ");
    };

    const variants = (d.SKU?.skuPaths || []).map(s => {
      // NOTE: skuId and skuIdStr disagree on some listings; the price and quantity
      // maps are keyed by skuIdStr, so never index them with skuId.
      const pr = priceMap[s.skuIdStr] || {};
      const q = qtyMap[s.skuIdStr] || {};
      return {
        sku: s.skuIdStr,
        name: nameFor(s.skuAttr) || label(s.skuAttr),
        price: pr.salePriceString || null,
        priceNum: pr.salePriceLocal ? parseFloat(String(pr.salePriceLocal).split("|")[0].replace(/[^\d.,]/g, "").replace(",", ".")) : null,
        stock: s.skuStock,
        salable: !!s.salable && q.maxBuyCount > 0
      };
    });

    const propsList = {};
    for (const g of (d.PRODUCT_PROP_PC?.props || [])) propsList[g.attrName] = g.attrValue;

    // Shipping: SHIPPING.deliveryLayoutInfo[] is the list of delivery options, each with
    // bizData.formattedAmount / displayAmount and shippingFee ("charge" | "free").
    // Options are NOT sorted, so scan for the minimum. Shipping is per SELLER, so ordering
    // the same BOM across 4 sellers pays 4 shipping charges - a major hidden cost.
    let shipping = null, shippingAll = [];
    try {
      const opts = (d.SHIPPING?.deliveryLayoutInfo || []).map(o => o?.bizData).filter(Boolean);
      shippingAll = opts.map(o => ({
        amount: typeof o.displayAmount === "number" ? o.displayAmount : parseFloat(o.displayAmount),
        text: o.formattedAmount || (o.shippingFee === "free" ? "FREE" : null),
        free: o.shippingFee === "free"
      })).filter(o => o.text);
      const free = shippingAll.find(o => o.free);
      const cheapest = shippingAll.filter(o => !isNaN(o.amount)).sort((a, b) => a.amount - b.amount)[0];
      shipping = free ? "FREE" : (cheapest ? cheapest.text : null);
    } catch (e) {}

    return {
      url: location.href,
      title: d.PRODUCT_TITLE?.subject || document.title,
      store: d.SHOP_CARD_PC?.storeName || null,
      storeRating: d.SHOP_CARD_PC?.sellerPositiveRate || null,
      rating: d.PC_RATING?.evarageStar || null,
      sold: d.PC_RATING?.tradeCount || d.DESC?.tradeCount || null,
      shipping,
      shippingOptions: shippingAll.slice(0, 4),
      specs: propsList,
      variants
    };
  });
}

(async () => {
  const args = process.argv.slice(2);
  const asJson = args.includes("--json");
  const targets = args.filter(a => a !== "--json").map(itemUrl);
  if (!targets.length) { console.error("usage: ali_item.js [--json] <url-or-itemId>..."); process.exit(2); }

  const b = await p.connect({ browserURL: `http://127.0.0.1:${PORT}`, defaultViewport: null });
  const pages = await b.pages();
  const pg = pages.find(x => x.url().includes("aliexpress")) || pages[0];

  const all = [];
  for (const t of targets) {
    try { all.push(await scrape(pg, t)); }
    catch (e) { all.push({ url: t, error: e.message }); }
  }
  await b.disconnect();

  if (asJson) { console.log(JSON.stringify(all, null, 1)); return; }

  for (const r of all) {
    if (r.error) { console.log(`\n!! ${r.url}\n   ERROR ${r.error}`); continue; }
    console.log(`\n${r.title}`);
    console.log(`   ${r.url}`);
    console.log(`   store=${r.store}  rating=${r.rating}  sold=${r.sold}  ship=${r.shipping || "n/a"}`);
    if (r.shippingOptions?.length > 1) console.log(`   shipping options: ${r.shippingOptions.map(o => o.text).join(" / ")}`);
    for (const k of ["Sensor", "Brand Name", "Model Number", "Origin"]) if (r.specs[k]) console.log(`   ${k}: ${r.specs[k]}`);
    for (const v of r.variants.sort((a, b2) => (a.priceNum ?? 1e9) - (b2.priceNum ?? 1e9))) {
      console.log(`   ${v.salable ? "  " : "XX"} ${String(v.price).padEnd(9)} ${String(v.stock).padStart(6)} in stock  ${v.name}${v.salable ? "" : "   <-- NOT BUYABLE"}`);
    }
  }
})().catch(e => { console.error("ERR", e.message); process.exit(1); });
