#!/usr/bin/env node
// Debug helper: log every jlcpcb.com request from EVERY tab (method, url, body) so the
// add-to-cart call can be identified.  node dump_requests.js [seconds]
const puppeteer = require('puppeteer-core');
(async () => {
  const browser = await puppeteer.connect({ browserURL: process.env.CDP_URL || 'http://127.0.0.1:9222', defaultViewport: null });
  const seen = new Set();
  const attach = async page => {
    if (!page || seen.has(page)) return; seen.add(page);
    page.on('request', req => {
      const u = req.url(); if (!["xhr","fetch"].includes(req.resourceType()) || !u.includes("jlcpcb.com") || /\.(js|css|png|jpg|svg|woff2?|gif|ico)(\?|$)/.test(u)) return;
      console.log(new Date().toISOString().slice(11, 19), req.method(), u.slice(0, 160), (req.postData() || '').slice(0, 300));
    });
  };
  for (const p of await browser.pages()) await attach(p);
  browser.on('targetcreated', async t => attach(await t.page().catch(() => null)));
  console.log('logging all jlcpcb.com requests from all tabs...');
  setTimeout(() => { browser.disconnect(); process.exit(0); }, 1000 * (parseInt(process.argv[2] || '180', 10)));
})();
