# ruff: noqa: E501
"""Local web server for the browser build (Python standard library only).

The build uses threads (SharedArrayBuffer), which browsers allow only on a
cross-origin-isolated page. This server serves the packaged folder the way
Cloudflare Pages will: it reads the folder's `_headers` file (same rules: `*`
splats, headers of all matching rules are added, `! Name` detaches an inherited
one). With no `_headers` it sends COOP same-origin and COEP require-corp
itself. It also supports HTTP Range (which SimpleHTTPRequestHandler does not)
and, with --gzip, gzip for text and wasm when the browser accepts it.

    python recomp/web/serve.py [DIR] [--port 8765] [--bind 127.0.0.1] [--gzip] [--cors]

DIR defaults to out/recomp/web/dist. --cors adds Access-Control-Allow-Origin: *
and Cross-Origin-Resource-Policy: cross-origin, so that a second copy of this
server (`serve.py out/recomp/web/demo --port 8766 --cors`) can play the part of
an R2 bucket or CDN holding the pack, to test `?demo=http://127.0.0.1:8766/`.
"""

from __future__ import annotations

import argparse
import functools
import gzip
import mimetypes
import os
import re
import sys
from http import HTTPStatus
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

mimetypes.add_type("application/wasm", ".wasm")
mimetypes.add_type("text/javascript", ".js")
mimetypes.add_type("application/json", ".json")

GZIP_TYPES = (
    "text/",
    "application/javascript",
    "application/json",
    "application/wasm",
    "image/svg+xml",
)
DEFAULT_HEADERS = {
    "Cross-Origin-Opener-Policy": "same-origin",
    "Cross-Origin-Embedder-Policy": "require-corp",
}


def parse_headers_file(text: str) -> list[tuple[re.Pattern[str], list[tuple[str, str | None]]]]:
    """Cloudflare `_headers`: a path line, then indented `Name: value` or `! Name` lines."""
    rules: list[tuple[re.Pattern[str], list[tuple[str, str | None]]]] = []
    cur: list[tuple[str, str | None]] | None = None
    for raw in text.splitlines():
        if not raw.strip() or raw.lstrip().startswith("#"):
            continue
        if not raw[0].isspace():
            pat = "".join(".*" if c == "*" else re.escape(c) for c in raw.strip())
            cur = []
            rules.append((re.compile(pat + r"\Z"), cur))
        elif cur is not None:
            line = raw.strip()
            if line.startswith("!"):
                cur.append((line[1:].strip().lower(), None))
            elif ":" in line:
                name, value = line.split(":", 1)
                cur.append((name.strip(), value.strip()))
    return rules


def headers_for(rules, path: str) -> list[tuple[str, str]]:
    out: list[tuple[str, str]] = []
    for pat, items in rules:
        if not pat.match(path):
            continue
        for name, value in items:
            if value is None:
                out = [(n, v) for n, v in out if n.lower() != name]
            else:
                out.append((name, value))
    # Same name from several rules is one comma-joined value, as on Cloudflare.
    merged: dict[str, tuple[str, list[str]]] = {}
    for n, v in out:
        merged.setdefault(n.lower(), (n, []))[1].append(v)
    return [(n, ", ".join(vs)) for n, vs in merged.values()]


class Handler(SimpleHTTPRequestHandler):
    rules: list = []
    use_gzip = False
    cors = False
    plain = False
    _range_left: int | None = None

    def _url_path(self) -> str:
        from urllib.parse import unquote

        return unquote(self.path.split("?", 1)[0].split("#", 1)[0])

    def _rule_headers(self) -> list[tuple[str, str]]:
        if self.plain:
            return []  # no isolation headers: for testing the page's message
        hdrs = (
            headers_for(self.rules, self._url_path())
            if self.rules
            else list(DEFAULT_HEADERS.items())
        )
        if self.cors:
            hdrs = [h for h in hdrs if h[0].lower() not in ("cross-origin-resource-policy",)]
            hdrs += [
                ("Access-Control-Allow-Origin", "*"),
                ("Access-Control-Expose-Headers", "Content-Length, Content-Range, Accept-Ranges"),
                ("Cross-Origin-Resource-Policy", "cross-origin"),
            ]
        return hdrs

    def guess_type(self, path):  # noqa: ANN001, ANN201 - stdlib signature
        for n, v in self._rule_headers():
            if n.lower() == "content-type":
                return v
        return super().guess_type(path)

    def end_headers(self) -> None:
        hdrs = self._rule_headers()
        for n, v in hdrs:
            if n.lower() != "content-type":  # already used for the response by guess_type
                self.send_header(n, v)
        if not any(n.lower() == "cache-control" for n, _ in hdrs):
            self.send_header("Cache-Control", "no-cache")  # a dev server never hides a rebuild
        self.send_header("Accept-Ranges", "bytes")
        super().end_headers()

    def do_OPTIONS(self) -> None:
        self.send_response(HTTPStatus.NO_CONTENT)
        self.send_header("Access-Control-Allow-Methods", "GET, HEAD, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Range")
        self.end_headers()

    def send_head(self):  # noqa: ANN201 - stdlib signature
        self._range_left = None
        path = self.translate_path(self.path)
        if os.path.isdir(path):
            return super().send_head()
        rng = self.headers.get("Range")
        ctype = self.guess_type(path)
        try:
            f = open(path, "rb")  # noqa: SIM115 - returned to the caller, which closes it
        except OSError:
            self.send_error(HTTPStatus.NOT_FOUND, "File not found")
            return None
        size = os.fstat(f.fileno()).st_size
        if rng:
            m = re.fullmatch(r"bytes=(\d*)-(\d*)", rng.strip())
            if m and (m.group(1) or m.group(2)):
                if not m.group(1):
                    start, end = max(0, size - int(m.group(2))), size - 1
                else:
                    start = int(m.group(1))
                    end = min(int(m.group(2)), size - 1) if m.group(2) else size - 1
                if start >= size or start > end:
                    f.close()
                    self.send_response(HTTPStatus.REQUESTED_RANGE_NOT_SATISFIABLE)
                    self.send_header("Content-Range", f"bytes */{size}")
                    self.end_headers()
                    return None
                self.send_response(HTTPStatus.PARTIAL_CONTENT)
                self.send_header("Content-Type", ctype)
                self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
                self.send_header("Content-Length", str(end - start + 1))
                self.end_headers()
                f.seek(start)
                self._range_left = end - start + 1
                return f
        if (
            self.use_gzip
            and "gzip" in self.headers.get("Accept-Encoding", "")
            and ctype.startswith(GZIP_TYPES)
        ):
            data = gzip.compress(f.read(), compresslevel=6)
            f.close()
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Encoding", "gzip")
            self.send_header("Vary", "Accept-Encoding")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            import io

            return io.BytesIO(data)
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(size))
        self.end_headers()
        return f

    def copyfile(self, source, outputfile) -> None:  # noqa: ANN001
        left = self._range_left
        try:
            if left is None:
                super().copyfile(source, outputfile)
                return
            self._range_left = None
            while left > 0:
                chunk = source.read(min(left, 1 << 20))
                if not chunk:
                    break
                outputfile.write(chunk)
                left -= len(chunk)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass  # the browser cancelled the request

    def log_message(self, fmt: str, *args) -> None:  # noqa: ANN002
        if os.environ.get("WD_WEB_QUIET"):
            return
        super().log_message(fmt, *args)


def make_server(
    root: Path,
    port: int,
    bind: str = "127.0.0.1",
    gzip_on: bool = False,
    cors: bool = False,
    plain: bool = False,
) -> ThreadingHTTPServer:
    hf = root / "_headers"
    handler = type(
        "DreamsHandler",
        (Handler,),
        {
            "rules": parse_headers_file(hf.read_text(encoding="utf-8")) if hf.is_file() else [],
            "use_gzip": gzip_on,
            "cors": cors,
            "plain": plain,
        },
    )
    return ThreadingHTTPServer((bind, port), functools.partial(handler, directory=str(root)))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("dir", nargs="?", default=str(REPO / "out" / "recomp" / "web" / "dist"))
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--bind", default="127.0.0.1")
    ap.add_argument("--gzip", action="store_true", help="gzip text and wasm responses")
    ap.add_argument(
        "--cors",
        action="store_true",
        help="CORS and CORP cross-origin (to act as a separate asset host)",
    )
    args = ap.parse_args()
    root = Path(args.dir).resolve()
    if not root.is_dir():
        sys.exit(f"{root} does not exist; run recomp/web/package.py first")
    with make_server(root, args.port, args.bind, args.gzip, args.cors) as httpd:
        mode = "_headers" if (root / "_headers").is_file() else "default COOP/COEP"
        print(
            f"serving {root} at http://{args.bind}:{args.port}/ ({mode}, Range{', gzip' if args.gzip else ''}{', CORS' if args.cors else ''})"
        )
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
