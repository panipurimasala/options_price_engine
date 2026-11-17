#!/usr/bin/env python3
"""
Build a standalone copy of the front end in web/dist/ that runs anywhere:
a local server, GitHub Pages, or any static host.

web/index.html is written as a page fragment (the claude.ai artifact host adds
the doctype, <head> and charset when it publishes). This wraps it in a full
HTML document and copies the scripts next to it.

Usage:
    python web/build_static.py
    python -m http.server 8000 -d web/dist     # then open http://localhost:8000
"""
import shutil
from pathlib import Path

WEB = Path(__file__).resolve().parent
DIST = WEB / "dist"

HEAD = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<style>[hidden]{display:none!important} img{max-width:100%} body{margin:0}</style>
"""


def main():
    fragment = (WEB / "index.html").read_text(encoding="utf-8")
    # Everything up to the end of the <style> block belongs in <head>.
    split = fragment.index("</style>") + len("</style>")
    head, body = fragment[:split], fragment[split:]

    DIST.mkdir(exist_ok=True)
    (DIST / "index.html").write_text(
        HEAD + head + "\n</head>\n<body>\n" + body + "\n</body>\n</html>\n", encoding="utf-8")
    for name in ("engine.js", "data.js"):
        shutil.copy2(WEB / name, DIST / name)
    print(f"built {DIST}/ (index.html, engine.js, data.js)")
    print("run:  python -m http.server 8000 -d web/dist   then open http://localhost:8000")


if __name__ == "__main__":
    main()
