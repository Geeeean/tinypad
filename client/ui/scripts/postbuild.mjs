// Vite's HTML plugin hardcodes type="module" crossorigin on the entry
// script tag regardless of build.rollupOptions.output.format -- iife output
// alone doesn't remove it, and vite-plugin-singlefile's inlining happens at
// a build stage after transformIndexHtml hooks run, so a Vite plugin can't
// cleanly post-process it either. Simplest fix: edit the built file
// directly, after Vite is done.
//
// This matters because the webview loads the app via a file:// URL, and
// WebKit refuses to execute <script type="module"> there (confirmed via
// WKWebView: window opens, page silently stays blank, no console error) --
// module script execution is origin-gated, even fully inlined with no
// imports. Chrome is more lenient, so a quick browser check alone won't
// catch this.
//
// Stripping type="module" alone isn't enough, though: Vite places the entry
// script in <head>, ahead of <div id="root"> in <body>. Module scripts
// defer themselves until the document is parsed; classic scripts don't, so
// without that the script runs immediately and React's createRoot() throws
// (minified error #299, "target container is not a DOM element") because
// #root doesn't exist yet. The obvious fix -- add a `defer` attribute --
// doesn't work either: per the HTML spec, `defer` is a no-op on scripts
// with no `src`, and this one is fully inlined. So instead the whole script
// body is wrapped in a DOMContentLoaded listener, which delays execution
// regardless of the tag's position in the document.

import { readFileSync, writeFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import path from "node:path";

const distIndex = path.resolve(
  path.dirname(fileURLToPath(import.meta.url)),
  "../dist/index.html",
);

let html = readFileSync(distIndex, "utf8");

const openTag = '<script type="module" crossorigin>';
const openIndex = html.indexOf(openTag);
if (openIndex === -1) {
  throw new Error(
    'postbuild: expected to find \'<script type="module" crossorigin>\' in the built ' +
      "index.html, but found no match -- Vite's output shape may have changed.",
  );
}

const closeTag = "</script>";
const closeIndex = html.lastIndexOf(closeTag);
if (closeIndex === -1 || closeIndex < openIndex) {
  throw new Error("postbuild: found the entry <script> open tag but no matching close tag.");
}

const before = html.slice(0, openIndex);
const body = html.slice(openIndex + openTag.length, closeIndex);
const after = html.slice(closeIndex + closeTag.length);

html =
  before +
  '<script>document.addEventListener("DOMContentLoaded",function(){' +
  body +
  "});</script>" +
  after;

writeFileSync(distIndex, html);
