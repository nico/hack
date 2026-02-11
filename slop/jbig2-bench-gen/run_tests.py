#!/usr/bin/env python3
"""JBIG2 decoder compatibility test suite.

Runs multiple JBIG2 decoders against test files and produces an HTML report.
"""

import concurrent.futures
import glob
import html
import json
import os
import signal
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path


def load_config(path="config.json"):
    with open(path) as f:
        return json.load(f)


def discover_tests(config):
    test_dir = config["test_dir"]
    exclude = set(config.get("exclude", []))
    files = sorted(glob.glob(os.path.join(test_dir, "*.jbig2")))
    return [f for f in files if os.path.basename(f) not in exclude]


def ensure_dirs(build_dir, decoder_names):
    os.makedirs(os.path.join(build_dir, "pdfs"), exist_ok=True)
    os.makedirs(os.path.join(build_dir, "jbig2"), exist_ok=True)
    for name in decoder_names:
        # Clean and recreate output/diff dirs so stale results don't linger.
        for subdir in ("output", "diffs"):
            d = os.path.join(build_dir, subdir, name)
            if os.path.exists(d):
                shutil.rmtree(d)
            os.makedirs(d)


def copy_jbig2_sources(test_files, build_dir):
    jbig2_dir = os.path.join(build_dir, "jbig2")
    for jbig2_path in test_files:
        dest = os.path.join(jbig2_dir, os.path.basename(jbig2_path))
        if (os.path.exists(dest)
                and os.path.getmtime(dest) >= os.path.getmtime(jbig2_path)):
            continue
        shutil.copy2(jbig2_path, dest)


def generate_pdfs(test_files, config, build_dir):
    script = config["jbig2_to_pdf_script"]
    pdf_dir = os.path.join(build_dir, "pdfs")
    for jbig2_path in test_files:
        name = Path(jbig2_path).stem
        pdf_path = os.path.join(pdf_dir, f"{name}.pdf")
        # Skip if PDF is newer than source
        if (os.path.exists(pdf_path)
                and os.path.getmtime(pdf_path) > os.path.getmtime(jbig2_path)):
            continue
        result = subprocess.run(
            [sys.executable, script, "-o", pdf_path, jbig2_path],
            capture_output=True, text=True, timeout=30,
        )
        if result.returncode != 0:
            print(f"  Warning: PDF generation failed for {name}: {result.stderr.strip()}")


def create_zip(src_dir, zip_path, extension):
    """Create a zip file from all files with the given extension in src_dir."""
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
        for f in sorted(glob.glob(os.path.join(src_dir, f"*{extension}"))):
            zf.write(f, os.path.basename(f))


def create_archives(build_dir):
    """Create zip archives of jbig2 and pdf test files."""
    create_zip(os.path.join(build_dir, "jbig2"),
               os.path.join(build_dir, "jbig2-tests.zip"), ".jbig2")
    create_zip(os.path.join(build_dir, "pdfs"),
               os.path.join(build_dir, "jbig2-tests-pdf.zip"), ".pdf")


def get_decoder_versions(decoders, config):
    """Detect version strings for each enabled decoder."""
    paths = config.get("paths", {})
    versions = {}
    for name, cfg in decoders.items():
        ver_cmd = cfg.get("version_command")
        if not ver_cmd:
            continue
        cmd = resolve_command(ver_cmd, paths)
        try:
            r = subprocess.run(cmd, capture_output=True, text=True, timeout=3,
                               start_new_session=True)
            out = (r.stdout.strip() or r.stderr.strip()).splitlines()
            if out:
                versions[name] = out[0]
        except (subprocess.TimeoutExpired, FileNotFoundError, OSError):
            pass
    return versions


def resolve_command(cmd_template, config_paths):
    """Replace {path_name} placeholders with values from config.paths."""
    resolved = []
    for part in cmd_template:
        for key, value in config_paths.items():
            part = part.replace(f"{{{key}}}", value)
        resolved.append(part)
    return resolved


def run_decoder(decoder_name, decoder_cfg, test_file, config, build_dir):
    """Run a single decoder on a single test file. Returns a result dict."""
    name = Path(test_file).stem
    input_type = decoder_cfg["input_type"]
    project_dir = os.path.join(build_dir, "..")

    if input_type == "pdf":
        input_path = os.path.relpath(os.path.join(build_dir, "pdfs", f"{name}.pdf"), project_dir)
    else:
        input_path = os.path.relpath(os.path.join(build_dir, "jbig2", f"{name}.jbig2"), project_dir)

    output_dir = os.path.relpath(os.path.join(build_dir, "output", decoder_name), project_dir)
    suffix = decoder_cfg.get("output_suffix", ".png")
    output_path = os.path.join(output_dir, f"{name}{suffix}")
    output_stem = os.path.join(output_dir, name)

    # Build command from template
    cmd = resolve_command(decoder_cfg["command"], config.get("paths", {}))
    cmd = [
        part.replace("{input}", input_path)
            .replace("{output}", output_path)
            .replace("{output_stem}", output_stem)
        for part in cmd
    ]

    result = {
        "decoder": decoder_name,
        "test": name,
        "command": cmd,
        "exit_code": None,
        "stderr": "",
        "output_path": None,
        "error": None,
    }

    try:
        proc = subprocess.Popen(
            cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            start_new_session=True,
        )
        try:
            stdout, stderr = proc.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid, signal.SIGKILL)
            proc.wait()
            result["error"] = "timeout (5s)"
            return result
        result["exit_code"] = proc.returncode
        result["stderr"] = stderr.decode("utf-8", errors="replace")

        # Handle output_pattern (e.g. pdfium writes to {input}.0.png)
        if "output_pattern" in decoder_cfg:
            pattern_path = (decoder_cfg["output_pattern"]
                            .replace("{input}", input_path)
                            .replace("{output_stem}", output_stem))
            if os.path.exists(pattern_path) and pattern_path != output_path:
                final_path = os.path.join(output_dir, f"{name}.png")
                shutil.move(pattern_path, final_path)
                output_path = final_path

        if proc.returncode == 0 and os.path.exists(output_path):
            result["output_path"] = output_path
        elif proc.returncode != 0:
            result["error"] = f"exit code {proc.returncode}"
    except FileNotFoundError as e:
        result["error"] = f"command not found: {e}"

    return result


def compare_output(result, config, build_dir):
    """Compare decoder output against reference using imgcmp."""
    if result["output_path"] is None:
        result["match"] = None
        result["diff_path"] = None
        return result

    imgcmp = config["imgcmp"]
    reference = config["reference_bitmap"]
    project_dir = os.path.join(build_dir, "..")
    diff_dir = os.path.relpath(os.path.join(build_dir, "diffs", result["decoder"]), project_dir)
    diff_path = os.path.join(diff_dir, f"{result['test']}.png")

    try:
        proc = subprocess.run(
            [imgcmp, "--write-diff-image", diff_path, reference, result["output_path"]],
            capture_output=True, text=True, timeout=30,
        )
        result["match"] = proc.returncode == 0
        result["diff_path"] = diff_path if proc.returncode != 0 else None
        if proc.returncode != 0 and not result.get("stderr"):
            result["stderr"] = proc.stdout.strip()
    except (subprocess.TimeoutExpired, FileNotFoundError) as e:
        result["match"] = None
        result["diff_path"] = None
        result["error"] = f"imgcmp error: {e}"

    return result


def run_test_pair(args):
    """Worker function for parallel execution."""
    decoder_name, decoder_cfg, test_file, config, build_dir = args
    result = run_decoder(decoder_name, decoder_cfg, test_file, config, build_dir)
    result = compare_output(result, config, build_dir)
    return result


BROWSER_FORMATS = {".png", ".webp", ".jpg", ".jpeg", ".gif", ".svg"}


def ensure_browser_friendly(path, image_tool):
    """Convert image to webp if not in a browser-friendly format. Returns new path."""
    if not path or not os.path.exists(path):
        return path
    ext = Path(path).suffix.lower()
    if ext in BROWSER_FORMATS:
        return path
    webp_path = str(Path(path).with_suffix(".webp"))
    try:
        subprocess.run(
            [image_tool, "-o", webp_path, path],
            capture_output=True, timeout=10,
        )
        if os.path.exists(webp_path):
            return webp_path
    except (subprocess.TimeoutExpired, FileNotFoundError):
        pass
    return path


def convert_outputs_for_browser(results, image_tool):
    """Convert any non-browser-friendly output/diff images to webp."""
    if not image_tool:
        return
    for r in results:
        for key in ("output_path", "diff_path"):
            path = r.get(key)
            if path:
                r[key] = ensure_browser_friendly(path, image_tool)


def generate_report(results, test_names, decoder_names, build_dir, versions=None):
    """Generate an HTML report with images referenced by relative path."""
    versions = versions or {}
    # Organize results: results_map[test][decoder] = result
    results_map = {}
    for r in results:
        results_map.setdefault(r["test"], {})[r["decoder"]] = r

    # Compute stats per decoder
    stats = {}
    for dec in decoder_names:
        passed = sum(1 for t in test_names
                     if results_map.get(t, {}).get(dec, {}).get("match") is True)
        failed = sum(1 for t in test_names
                     if results_map.get(t, {}).get(dec, {}).get("match") is False)
        errors = sum(1 for t in test_names
                     if results_map.get(t, {}).get(dec, {}).get("match") is None)
        stats[dec] = {"passed": passed, "failed": failed, "errors": errors}

    total_tests = len(test_names)

    # Build HTML
    lines = []
    lines.append("""<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>JBIG2 Decoder Compatibility Report</title>
<style>
* { box-sizing: border-box; margin: 0; padding: 0; }
body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, monospace;
       font-size: 14px; background: #f5f5f5; padding: 20px; }
h1 { margin-bottom: 10px; }
p { margin: 10px 0; color: #555; max-width: 800px; line-height: 1.5; }
.summary { display: flex; gap: 20px; margin-bottom: 20px; flex-wrap: wrap; }
.summary-card { background: white; border-radius: 8px; padding: 12px 18px;
                box-shadow: 0 1px 3px rgba(0,0,0,0.1); }
.summary-card h3 { font-size: 13px; color: #666; margin-bottom: 4px; }
.summary-card .num { font-size: 22px; font-weight: bold; }
.pass { color: #22863a; } .fail { color: #cb2431; } .error { color: #999; }
table { border-collapse: collapse; background: white; box-shadow: 0 1px 3px rgba(0,0,0,0.1); }
th, td { border: 1px solid #ddd; padding: 4px 8px; text-align: center; font-size: 13px; }
thead th { background: #f0f0f0; position: sticky; top: 0; z-index: 2; }
th.test-name, td.test-name { text-align: left; position: sticky; left: 0;
  background: white; z-index: 1; white-space: nowrap; }
thead th.test-name { z-index: 3; background: #f0f0f0; }
.cell { position: relative; }
.cell:hover { background: #fff3cd; }
.cell .tooltip { display: none; position: absolute; left: 100%; top: 50%;
  transform: translateY(-50%); background: white; border: 1px solid #ccc;
  border-radius: 6px; padding: 10px; z-index: 100; box-shadow: 0 4px 12px rgba(0,0,0,0.15);
  min-width: 320px; text-align: left; white-space: normal; }
.cell:hover .tooltip { display: block; }
.tooltip img { max-width: 280px; max-height: 200px; display: block; margin: 6px 0;
               image-rendering: pixelated; border: 1px solid #eee; }
.tooltip code { display: block; font-size: 11px; background: #f5f5f5; padding: 6px;
                border-radius: 4px; word-break: break-all; margin-top: 6px; white-space: pre-wrap; }
.tooltip h4 { font-size: 12px; color: #666; margin-top: 8px; }
.pass-mark { font-size: 18px; color: #22863a; }
.fail-mark { font-size: 18px; color: #cb2431; }
.error-mark { font-size: 18px; color: #999; }
.pct { font-size: 11px; color: #666; display: block; }
.file-links { font-size: 11px; margin-left: 6px; }
.file-links a { color: #0366d6; text-decoration: none; margin-right: 4px; }
.file-links a:hover { text-decoration: underline; }
</style>
</head>
<body>
<h1>JBIG2 Decoder Compatibility Report</h1>
<p>
<a href="https://en.wikipedia.org/wiki/JBIG2">JBIG2</a> is a compression standard
for bi-level (black &amp; white) images.
Every PDF viewer implements a JBIG2 decoder.
This page shows results for various JBIG2 decoders against
<a href="https://github.com/SerenityOS/serenity/tree/master/Tests/LibGfx/test-inputs/jbig2">107 test files</a>
from the SerenityOS repository. Each test file encodes the same 399&times;400 1-bit bitmap.
Decoders that take a PDF as input use a thin PDF wrapper around the raw JBIG2 data.
</p>
<p>
Download all test files: <a href="build/jbig2-tests.zip">jbig2</a>,
<a href="build/jbig2-tests-pdf.zip">pdf</a>.
</p>
""")

    # Summary
    lines.append('<div class="summary">')
    lines.append(f'<div class="summary-card"><h3>Tests</h3><div class="num">{total_tests}</div></div>')
    for dec in decoder_names:
        s = stats[dec]
        ver = versions.get(dec)
        ver_html = f'<div style="font-size:11px;color:#999;margin-top:2px">{html.escape(ver)}</div>' if ver else ''
        lines.append(f'<div class="summary-card"><h3>{html.escape(dec)}</h3>')
        lines.append(f'<div class="num"><span class="pass">{s["passed"]}</span> / ')
        lines.append(f'<span class="fail">{s["failed"]}</span> / ')
        lines.append(f'<span class="error">{s["errors"]}</span></div>{ver_html}</div>')
    lines.append('</div>')

    decoder_urls = {
        "jbig2dec": "https://jbig2dec.com/",
        "pdfium": "https://pdfium.googlesource.com/pdfium/",
        "mupdf": "https://mupdf.com/",
        "preview": "https://support.apple.com/guide/preview/welcome/mac",
        "ghostscript": "https://www.ghostscript.com/",
        "poppler": "https://poppler.freedesktop.org/",
        "serenity-image": "https://github.com/SerenityOS/serenity",
        "pdfjs": "https://mozilla.github.io/pdf.js/",
        "itu-ref": "https://www.itu.int/rec/T-REC-T.88/en",
        "hayro-jbig2": "https://crates.io/crates/hayro-jbig2",
    }

    # Legend
    lines.append("""<p>
Green (<span class="pass-mark">&#x2713;</span>) = pixel-perfect match against the reference,
red (<span class="fail-mark">&#x2717;</span>) = mismatch (hover for diff),
gray (<span class="error-mark">—</span>) = error or timeout.
</p>
""")

    # Table
    lines.append('<table><thead><tr><th class="test-name">Test</th>')
    for dec in decoder_names:
        s = stats[dec]
        pct = f"{s['passed']*100//total_tests}%" if total_tests else "—"
        url = decoder_urls.get(dec)
        if url:
            name_html = f'<a href="{html.escape(url)}" style="color:inherit;text-decoration:none;border-bottom:1px dashed #999">{html.escape(dec)}</a>'
        else:
            name_html = html.escape(dec)
        lines.append(f'<th>{name_html}<span class="pct">{pct}</span></th>')
    lines.append('</tr></thead><tbody>')

    json_base = "https://github.com/SerenityOS/serenity/blob/master/Tests/LibGfx/test-inputs/jbig2/json"

    for test in test_names:
        jbig2_rel = f"build/jbig2/{test}.jbig2"
        pdf_rel = f"build/pdfs/{test}.pdf"
        json_url = f"{json_base}/{test}.json"
        links = (f'<span class="file-links">'
                 f'<a href="{html.escape(jbig2_rel)}">jbig2</a>'
                 f'<a href="{html.escape(pdf_rel)}">pdf</a>'
                 f'<a href="{html.escape(json_url)}">json</a>'
                 f'</span>')
        lines.append(f'<tr><td class="test-name">{html.escape(test)}{links}</td>')
        for dec in decoder_names:
            r = results_map.get(test, {}).get(dec)
            if r is None:
                lines.append('<td class="cell"><span class="error-mark">—</span></td>')
                continue

            if r["match"] is True:
                lines.append('<td class="cell"><span class="pass-mark">&#x2713;</span></td>')
            elif r["match"] is False:
                # Fail with tooltip
                lines.append('<td class="cell"><span class="fail-mark">&#x2717;</span>')
                lines.append('<div class="tooltip">')
                lines.append(f'<h4>Decoder output:</h4>')
                out_path = r.get("output_path")
                if out_path and os.path.exists(out_path):
                    lines.append(f'<img loading="lazy" src="{html.escape(out_path)}" alt="output">')
                lines.append(f'<h4>Diff:</h4>')
                diff_path = r.get("diff_path")
                if diff_path and os.path.exists(diff_path):
                    lines.append(f'<img loading="lazy" src="{html.escape(diff_path)}" alt="diff">')
                cmd_str = " ".join(r.get("command", []))
                lines.append(f'<h4>Reproduce:</h4><code>{html.escape(cmd_str)}</code>')
                if r.get("stderr"):
                    lines.append(f'<h4>stderr:</h4><code>{html.escape(r["stderr"][:500])}</code>')
                lines.append('</div></td>')
            else:
                # Error
                lines.append('<td class="cell"><span class="error-mark">—</span>')
                err = r.get("error", "unknown error")
                cmd_str = " ".join(r.get("command", []))
                lines.append(f'<div class="tooltip"><h4>Error:</h4><code>{html.escape(err)}</code>')
                lines.append(f'<h4>Reproduce:</h4><code>{html.escape(cmd_str)}</code>')
                if r.get("stderr"):
                    lines.append(f'<h4>stderr:</h4><code>{html.escape(r["stderr"][:500])}</code>')
                lines.append('</div></td>')

        lines.append('</tr>')

    lines.append('</tbody></table></body></html>')

    report_path = os.path.join(build_dir, "..", "report.html")
    with open(report_path, "w") as f:
        f.write("\n".join(lines))
    return report_path


def main():
    config = load_config()
    build_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "build")

    # Discover enabled decoders
    decoders = {name: cfg for name, cfg in config["decoders"].items() if cfg.get("enabled")}
    decoder_names = list(decoders.keys())

    # Discover test files
    test_files = discover_tests(config)
    test_names = [Path(f).stem for f in test_files]
    print(f"Found {len(test_files)} test files, {len(decoders)} enabled decoders")

    # Create directories
    ensure_dirs(build_dir, decoder_names)

    # Copy jbig2 source files
    copy_jbig2_sources(test_files, build_dir)

    # Generate PDFs
    print("Generating PDFs...")
    generate_pdfs(test_files, config, build_dir)
    print(f"  PDFs ready in build/pdfs/")

    # Create zip archives
    print("Creating archives...")
    create_archives(build_dir)

    # Build work items
    work = []
    for test_file in test_files:
        for dec_name, dec_cfg in decoders.items():
            work.append((dec_name, dec_cfg, test_file, config, build_dir))

    # Run decoders in parallel
    print(f"Running {len(work)} decoder invocations...")
    results = []
    with concurrent.futures.ThreadPoolExecutor() as executor:
        futures = {executor.submit(run_test_pair, w): w for w in work}
        done = 0
        for future in concurrent.futures.as_completed(futures):
            done += 1
            if done % 50 == 0 or done == len(work):
                print(f"  {done}/{len(work)}")
            try:
                results.append(future.result())
            except Exception as e:
                w = futures[future]
                print(f"  Error: {w[0]}/{Path(w[2]).stem}: {e}")

    # Convert non-browser-friendly images (e.g. .pbm, .bmp) to webp
    image_tool = config.get("image")
    if image_tool:
        print("Converting images for browser...")
        convert_outputs_for_browser(results, image_tool)

    # Detect decoder versions
    versions = get_decoder_versions(decoders, config)

    # Generate report
    print("Generating report...")
    report_path = generate_report(results, test_names, decoder_names, build_dir, versions)
    print(f"Report written to {report_path}")

    # Print summary
    for dec in decoder_names:
        passed = sum(1 for r in results if r["decoder"] == dec and r["match"] is True)
        failed = sum(1 for r in results if r["decoder"] == dec and r["match"] is False)
        errors = sum(1 for r in results if r["decoder"] == dec and r["match"] is None)
        print(f"  {dec}: {passed} pass, {failed} fail, {errors} error")


if __name__ == "__main__":
    main()
