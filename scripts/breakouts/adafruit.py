"""Adafruit catalogue access: the product list (public JSON API) and facts read from product pages.

Downloads are cached under ~/.cache/raft-breakouts/ (product list for a day, product pages for 30 days) and fetched
at most one per second, so re-running is cheap and polite.
"""
import html
import json
import re
import time
import urllib.request
from pathlib import Path

CACHE = Path.home() / ".cache" / "raft-breakouts" / "adafruit"
API_URL = "https://www.adafruit.com/api/products"
PRODUCT_URL = "https://www.adafruit.com/product/{pid}"
USER_AGENT = "RaftCore-breakouts/0.1 (catalogue script)"
_last_fetch = 0.0


def _fetch(url: str, path: Path, max_age_s: float) -> str:
    global _last_fetch
    if path.exists() and time.time() - path.stat().st_mtime < max_age_s:
        return path.read_text(encoding="utf-8", errors="replace")
    wait = 1.0 - (time.time() - _last_fetch)
    if wait > 0:
        time.sleep(wait)
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=60) as resp:
        text = resp.read().decode("utf-8", errors="replace")
    _last_fetch = time.time()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return text


def products() -> dict:
    """All products keyed by product id (str)."""
    text = _fetch(API_URL, CACHE / "products.json", 24 * 3600)
    return {p["product_id"]: p for p in json.loads(text)}


def _section(page: str, sec_id: str) -> str:
    m = re.search(rf'<section id="{sec_id}".*?</section>', page, re.S)
    if not m:
        return ""
    text = re.sub(r"<(script|style)\b.*?</\1>", " ", m.group(0), flags=re.S)
    text = re.sub(r"<[^>]+>", " ", text)
    return re.sub(r"\s+", " ", html.unescape(text)).strip()


# Phrases Adafruit uses for boards that work from 5V (regulator + level shifting) ...
FIVE_V_OK = re.compile(
    r"\b[23](?:\.\d)?\s*V?\s*(?:-|to|or|and|/)\s*5(?:\.5)?\s*V|"     # 3-5V, 3 to 5V, 2.7V to 5.5V, 3V and 5V
    r"3\.3\s*V[^.]{0,40}\bor\s+5\s*V|"                                 # 3.3V (Feather/Pi) or 5V (Arduino)
    r"level[- ]shift|"                                                 # regulator and level shifting
    r"compatib\w*\s+with\s+5\s*V", re.I)
# ... and for boards that don't.
THREE_V_ONLY = re.compile(
    r"not\s+5\s*V[- ]?(?:safe|tolerant|compatible|compliant)|3\.3\s*V\s+(?:power\s+and\s+logic\s+)?only|"
    r"(?:only|just)\s+(?:use|for|with)\s+3\.3\s*V|no\s+level[- ]shift", re.I)
ADDR = re.compile(r"\b0x[0-9A-Fa-f]{2}\b")


def _snippet(text: str, m) -> str:
    return text[max(0, m.start() - 60): m.end() + 60].strip()


def page_facts(pid: str) -> dict:
    """Connector, 5V tolerance and I2C addresses as stated on the product page.

    vcc: '3-5V' (5V tolerant), '3.3V' (not 5V tolerant) or '' (not stated); evidence holds the
    matching text so a person can check it.
    """
    page = _fetch(PRODUCT_URL.format(pid=pid), CACHE / "pages" / f"{pid}.html", 30 * 24 * 3600)
    text = _section(page, "description") + " " + _section(page, "technical-details")
    five = FIVE_V_OK.search(text)
    three = THREE_V_ONLY.search(text)
    if three and not five:
        vcc = "3.3V"
    elif five and not three:
        vcc = "3-5V"
    else:
        vcc = ""
    evidence = "; ".join(_snippet(text, m) for m in (five, three) if m)
    stemma_qt = bool(re.search(r"STEMMA\s*QT", text, re.I))
    stemma = bool(re.search(r"STEMMA(?!\s*QT)", text, re.I))
    qwiic = bool(re.search(r"Qwiic", text, re.I))
    connector = "stemma-qt" if stemma_qt else ("qwiic" if qwiic else ("stemma" if stemma else ""))
    addrs = sorted({a.lower() for a in ADDR.findall(text)})
    return {"connector": connector, "vcc": vcc, "vcc_evidence": evidence,
            "addresses": ",".join(addrs), "i2c": bool(re.search(r"\bI2C\b|\bI²C\b", text))}
