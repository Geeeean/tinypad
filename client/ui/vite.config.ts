import path from "path";
import tailwindcss from "@tailwindcss/vite";
import react from "@vitejs/plugin-react";
import { defineConfig } from "vite";
import { viteSingleFile } from "vite-plugin-singlefile";

// https://vite.dev/config/
export default defineConfig({
  plugins: [react(), tailwindcss(), viteSingleFile()],
  resolve: {
    alias: {
      "@": path.resolve(__dirname, "./src"),
    },
  },
  build: {
    // The webview loads the build output via a file:// URL, not an http
    // server. Three file:// problems that solves for:
    //   - WebKit (confirmed via WKWebView: window opens, page stays blank,
    //     no error surfaced) refuses to execute <script type="module"> --
    //     even fully inlined with no imports -- when the document's origin
    //     is file://, because module script execution is origin-gated
    //     regardless of whether anything is actually fetched. Chrome is
    //     more lenient here, which is why this only showed up once tested
    //     in the real target (WKWebView), not a quick browser check.
    //   - Vite's default multi-chunk output references assets by URL, and
    //     those references would need to be relative to resolve at all
    //     under file://.
    // Forcing `format: 'iife'` makes Vite/Rollup emit the entry as a plain
    // classic <script> (no type="module"), sidestepping the WebKit
    // restriction entirely. vite-plugin-singlefile then inlines everything
    // -- JS, CSS, and referenced assets (fonts included, as base64 data
    // URIs) -- into that one script in a single index.html.
    cssCodeSplit: false,
    assetsInlineLimit: 100_000_000,
    rollupOptions: {
      output: {
        format: "iife",
      },
    },
  },
});
