// Renders the first page of a PDF to a PNG file using pdf.js + @napi-rs/canvas.
// Usage: node pdfjs_render.mjs <input.pdf> <output.png>

import { createCanvas } from "@napi-rs/canvas";
import { readFileSync, writeFileSync } from "fs";
import { getDocument } from "pdfjs-dist/legacy/build/pdf.mjs";

const [inputPath, outputPath] = process.argv.slice(2);
if (!inputPath || !outputPath) {
  console.error("Usage: node pdfjs_render.mjs <input.pdf> <output.png>");
  process.exit(1);
}

const data = new Uint8Array(readFileSync(inputPath));
const doc = await getDocument({ data, isEvalSupported: false }).promise;
const page = await doc.getPage(1);

// Render at 1x (72 DPI) to match the PDF's MediaBox dimensions.
const viewport = page.getViewport({ scale: 1.0 });
const canvas = createCanvas(viewport.width, viewport.height);
const ctx = canvas.getContext("2d");

await page.render({ canvasContext: ctx, viewport }).promise;

writeFileSync(outputPath, canvas.toBuffer("image/png"));
