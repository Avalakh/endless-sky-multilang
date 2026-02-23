#!/usr/bin/env python3
"""
translate_data.py

Translates Endless Sky .txt data files from data/ to Russian,
writing outputs to %APPDATA%/endless-sky/plugins/ru-data-translation/data/
while preserving file structure.

Translatable elements (per docs/Структура-и-реализация-переводов.md §9):
  - description / name fields inside outfit, ship, mission, start
  - description / spaceport fields inside planet (map planets.txt, map beyond patir.txt)
  - All backtick strings inside conversation and choice blocks
  - Strings inside word blocks (phrase, news, etc.)
  - Items inside trade commodity blocks
  - Items inside top-level category blocks
  - log and dialog field values

Everything else (identifiers, references, numeric attributes, control
keywords, placeholders) is kept as-is.

Uses LibreTranslate REST API at http://localhost:5000/translate.
Translations are cached in tools/translation_cache.json to avoid
re-translating identical strings on subsequent runs.

Usage:
    python translate_data.py                              # all files
    python translate_data.py --file human/outfits.txt    # one file
    python translate_data.py --dry-run                   # preview only
    python translate_data.py --no-cache                  # skip cache load
    python translate_data.py --skip-translated           # skip already translated strings
"""

import re
import sys
import json
import time
import os
import argparse
import requests
from pathlib import Path
from typing import Optional

# ── Paths ──────────────────────────────────────────────────────────────────────

SCRIPT_DIR = Path(__file__).resolve().parent
BASE_DIR = SCRIPT_DIR.parent
DATA_DIR = BASE_DIR / "data"
APPDATA_DIR = Path(os.environ.get("APPDATA", Path.home() / "AppData" / "Roaming"))
OUTPUT_DIR = APPDATA_DIR / "endless-sky" / "plugins" / "ru-data-translation" / "data"
CACHE_FILE = SCRIPT_DIR / "translation_cache.json"

API_URL = "http://localhost:5000/translate"
BATCH_SIZE = 32  # strings per API call; reduce if server times out

# ── ES grammar constants ────────────────────────────────────────────────────────

TOP_LEVEL_TYPES = frozenset({
    "outfit", "ship", "mission", "conversation", "phrase",
    "government", "planet", "system", "fleet", "event",
    "person", "start", "trade", "category", "gamerules",
    "interface", "news", "hazard", "minable", "formation",
})

# At depth >= 1, the value of these field names is translatable,
# keyed by the enclosing top-level object type.
TRANSLATABLE_FIELDS: dict[str, frozenset[str]] = {
    "outfit":  frozenset({"description"}),
    "ship":    frozenset({"description"}),
    "mission": frozenset({"name", "description"}),
    "start":   frozenset({"name", "description"}),
    "planet":  frozenset({"description", "spaceport"}),  # map planets.txt, map beyond patir.txt
}

# Sub-block keywords that introduce a "conversation" context
# (backtick strings inside are translatable). Only when used WITHOUT
# an identifier argument (e.g. bare `conversation`, not `conversation "id"`).
CONV_BLOCK_KEYWORDS = frozenset({"conversation", "choice"})

# Sub-block keyword that introduces a "word" context
WORD_BLOCK_KEYWORD = "word"

# Sub-block keyword that introduces a "commodity" context
COMMODITY_BLOCK_KEYWORD = "commodity"

# Field keywords whose value(s) are always translated
ALWAYS_TRANSLATE_VALUE = frozenset({"log", "dialog"})

# Field keywords whose value is an identifier/reference — never translated.
# This also covers sub-block starters that have an identifier argument.
REFERENCE_FIELDS = frozenset({
    "category", "series", "thumbnail", "sprite", "government", "names",
    "system", "planet", "fleet", "ship", "conversation", "phrase", "event",
    "source", "destination", "stopover", "clearance", "blocked", "formation",
    "account", "date", "credits", "score", "mortgage", "principal",
    "interest", "term", "plural", "swizzle", "color", "language", "type",
    "npc", "personality", "variant", "repeat", "index", "cost",
    "licenses", "logbook", "news", "portrait", "location", "attributes",
    "filters", "filter",
})

# Keywords that start control-flow lines — entire line is syntax, not content.
CONTROL_KEYWORDS = frozenset({
    "branch", "label", "goto", "decline", "accept", "fail",
    "job", "deadline", "invisible", "landing", "non-blocking",
    "random", "set", "apply", "mark", "unmark", "visit", "enter",
    "weight", "engine", "gun", "turret", "outfits",
    "on", "to", "has", "not", "and", "or", "action",
})

# "dialog phrase \"id\"" is a reference to a phrase, not display text — never translate.
def _is_dialog_phrase_reference(stripped: str) -> bool:
    if not stripped.startswith("dialog"):
        return False
    rest = stripped[len("dialog") :].strip()
    return rest.startswith("phrase ") and ("\"" in rest or "'" in rest)


# Placeholder pattern: <anything> — preserved verbatim inside translations.
PLACEHOLDER_RE = re.compile(r"<[^>]+>")
CYRILLIC_RE = re.compile(r"[А-Яа-яЁё]")
LATIN_RE = re.compile(r"[A-Za-z]")


# ── Translation cache ──────────────────────────────────────────────────────────

_cache: dict[str, str] = {}


def load_cache() -> None:
    global _cache
    if CACHE_FILE.exists():
        with open(CACHE_FILE, encoding="utf-8") as f:
            _cache = json.load(f)
        print(f"[cache] Loaded {len(_cache)} cached translations.", flush=True)


def save_cache() -> None:
    with open(CACHE_FILE, "w", encoding="utf-8") as f:
        json.dump(_cache, f, ensure_ascii=False, indent=2)
    print(f"[cache] Saved {len(_cache)} translations.", flush=True)


# ── Placeholder protection ─────────────────────────────────────────────────────

def protect_placeholders(text: str) -> tuple[str, list[str]]:
    """Replace <...> tokens with safe ASCII markers before translation."""
    tokens: list[str] = []

    def sub(m: re.Match) -> str:
        idx = len(tokens)
        tokens.append(m.group(0))
        return f"XPHX{idx}XPHX"

    return PLACEHOLDER_RE.sub(sub, text), tokens


def restore_placeholders(text: str, tokens: list[str]) -> str:
    for i, token in enumerate(tokens):
        text = text.replace(f"XPHX{i}XPHX", token)
    return text


def is_already_translated_text(text: str) -> bool:
    """
    Heuristic: treat a string as already translated when it contains Cyrillic
    and has no Latin letters. This avoids re-sending such strings to the API.
    """
    stripped = text.strip()
    if not stripped:
        return False
    return bool(CYRILLIC_RE.search(stripped)) and not bool(LATIN_RE.search(stripped))


# ── LibreTranslate API ─────────────────────────────────────────────────────────

def _api_translate_batch(texts: list[str]) -> list[str]:
    """
    Send a batch of texts to LibreTranslate. Tries array input first;
    falls back to sequential single calls if the server rejects arrays.
    Returns translated texts in the same order; returns originals on failure.
    """
    protected: list[str] = []
    token_lists: list[list[str]] = []
    for text in texts:
        p, t = protect_placeholders(text)
        protected.append(p)
        token_lists.append(t)

    # --- Try batch (array) request ---
    try:
        resp = requests.post(
            API_URL,
            json={"q": protected, "source": "en", "target": "ru", "format": "text"},
            timeout=120,
        )
        resp.raise_for_status()
        data = resp.json()
        translated = data.get("translatedText", [])
        if isinstance(translated, list) and len(translated) == len(texts):
            return [
                restore_placeholders(translated[i], token_lists[i])
                for i in range(len(texts))
            ]
        # Server returned a single string instead of a list — fall through to sequential
    except Exception:
        pass  # fall through to sequential

    # --- Sequential single-call fallback ---
    results: list[str] = []
    for i, text in enumerate(texts):
        for attempt in range(3):
            try:
                resp = requests.post(
                    API_URL,
                    json={"q": protected[i], "source": "en", "target": "ru", "format": "text"},
                    timeout=30,
                )
                resp.raise_for_status()
                result = resp.json().get("translatedText", text)
                results.append(restore_placeholders(result, token_lists[i]))
                break
            except Exception as exc:
                if attempt == 2:
                    print(
                        f"  [WARN] Translation failed for '{text[:50]}': {exc}",
                        file=sys.stderr,
                    )
                    results.append(text)
                else:
                    time.sleep(0.5 * (attempt + 1))

    return results


def batch_translate(
    unique_texts: list[str],
    dry_run: bool,
    skip_translated: bool = False,
) -> dict[str, str]:
    """
    Translate a deduplicated list of strings.
    Returns a mapping {original_stripped → translated}.
    Already-cached strings are not sent to the API.
    """
    results: dict[str, str] = {}
    to_translate: list[str] = []

    for text in unique_texts:
        if not text:
            results[text] = text
        elif skip_translated and is_already_translated_text(text):
            results[text] = text
        elif text in _cache:
            results[text] = _cache[text]
        elif dry_run:
            results[text] = f"[TR]{text}"
        else:
            to_translate.append(text)

    if not to_translate:
        return results

    total = len(to_translate)
    total_batches = (total + BATCH_SIZE - 1) // BATCH_SIZE
    print(f"  Translating {total} new strings in {total_batches} batch(es)...", flush=True)

    for batch_idx, i in enumerate(range(0, total, BATCH_SIZE), start=1):
        batch = to_translate[i : i + BATCH_SIZE]
        done = min(i + BATCH_SIZE, total)
        print(f"  [{batch_idx}/{total_batches}] strings {i + 1}-{done} of {total}", end="\r", flush=True)
        translated = _api_translate_batch(batch)
        for original, result in zip(batch, translated):
            results[original] = result
            # If API returned unchanged English text, don't poison cache:
            # let future runs retry instead of treating it as translated.
            if result == original and re.search(r"[A-Za-z]", original):
                continue
            _cache[original] = result

    print(f"  Done - {total} strings translated.          ", flush=True)
    return results


# ── ES file tokenisation helpers ───────────────────────────────────────────────

def count_tabs(line: str) -> int:
    return len(line) - len(line.lstrip("\t"))


def first_unquoted_token(s: str) -> str:
    """First whitespace-delimited token from a stripped string."""
    parts = s.split()
    return parts[0] if parts else ""


def has_trailing_content_after_first_quote(stripped: str) -> bool:
    """
    True when a line is `"quoted string" <something>` (there is content
    after the closing quote). Used to distinguish `"item"` (standalone
    commodity/category value) from `"key" value` (attribute pair).
    """
    q = stripped[0] if stripped else ""
    if q not in ('"', "`"):
        return False
    end = stripped.find(q, 1)
    if end == -1:
        return False
    return bool(stripped[end + 1 :].strip())


def extract_first_quoted(s: str) -> Optional[str]:
    """Return the content of the first quoted string (double or backtick)."""
    dq = s.find('"')
    bq = s.find("`")
    if dq == -1 and bq == -1:
        return None
    if dq == -1:
        pos, q = bq, "`"
    elif bq == -1:
        pos, q = dq, '"'
    else:
        pos, q = (dq, '"') if dq < bq else (bq, "`")
    end = s.find(q, pos + 1)
    return s[pos + 1 : end] if end != -1 else None


def extract_all_quoted(s: str) -> list[str]:
    """Return contents of all quoted strings in a line."""
    results: list[str] = []
    i = 0
    while i < len(s):
        if s[i] in ('"', "`"):
            q = s[i]
            end = s.find(q, i + 1)
            if end != -1:
                results.append(s[i + 1 : end])
                i = end + 1
                continue
        i += 1
    return results


def extract_field_value(stripped: str, field_name: str) -> Optional[str]:
    """
    For a line like `description "text"` or `name `text``,
    return the string content after the field name.
    """
    after = stripped[len(field_name) :].lstrip()
    if not after:
        return None
    q = after[0]
    if q not in ('"', "`"):
        return None
    end = after.find(q, 1)
    return after[1:end] if end != -1 else None


# ── First pass: collect translatable strings ───────────────────────────────────

class Collector:
    """
    Walks file lines and collects all strings that need translation.
    Produces a flat list (with duplicates); callers deduplicate it.
    """

    def __init__(self) -> None:
        self._ctx: dict[int, str] = {}
        self._obj_type: Optional[str] = None
        self.strings: list[str] = []

    def collect(self, content: str) -> list[str]:
        self._ctx = {}
        self._obj_type = None
        self.strings = []
        for line in content.splitlines(keepends=True):
            self._visit(line)
        return self.strings

    # ── line visitor ──────────────────────────────────────────────────────────

    def _visit(self, line: str) -> None:
        depth = count_tabs(line)
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            return
        self._clear(depth)

        # ── depth 0: top-level object definitions ────────────────────────────
        if depth == 0:
            tok = first_unquoted_token(stripped)
            if tok in TOP_LEVEL_TYPES:
                self._obj_type = tok
                self._ctx[0] = tok
            else:
                self._obj_type = None
            return  # identifiers are never translated

        # ── backtick narrative / choice option ───────────────────────────────
        if stripped.startswith("`"):
            if self._backtick_translatable():
                val = extract_first_quoted(stripped)  # also handles backtick
                # extract_first_quoted finds " first; find ` manually
                bq_val = self._extract_backtick_content(stripped)
                if bq_val is not None:
                    self.strings.append(bq_val.strip())
            return

        # ── standalone quoted narrative inside conversation/choice ───────────
        # e.g. `"Narrative text."` should be translated, while `"key" value`
        # remains a technical attribute/reference and must be skipped.
        if stripped.startswith('"') and self._in_ctx("conversation"):
            if not has_trailing_content_after_first_quote(stripped):
                val = extract_first_quoted(stripped)
                if val is not None:
                    self.strings.append(val.strip())
            return

        tok = first_unquoted_token(stripped)

        # ── control flow ──────────────────────────────────────────────────────
        if tok in CONTROL_KEYWORDS:
            return

        # ── word block intro ─────────────────────────────────────────────────
        if tok == WORD_BLOCK_KEYWORD:
            self._ctx[depth] = "word"
            return

        # ── conversation / choice block intro (only when bare, no identifier) ─
        if tok in CONV_BLOCK_KEYWORDS:
            after = stripped[len(tok) :].strip()
            if not (after.startswith('"') or after.startswith("`")):
                self._ctx[depth] = tok
            return

        # ── commodity block intro ─────────────────────────────────────────────
        if tok == COMMODITY_BLOCK_KEYWORD:
            self._ctx[depth] = "commodity"
            return

        # ── items inside word block ───────────────────────────────────────────
        if self._in_ctx("word"):
            if stripped.startswith('"') or stripped.startswith("`"):
                val = extract_first_quoted(stripped)
                if val is not None:
                    self.strings.append(val.strip())
            return

        # ── items inside commodity block ──────────────────────────────────────
        if self._in_ctx("commodity"):
            if stripped.startswith('"') and not has_trailing_content_after_first_quote(stripped):
                val = extract_first_quoted(stripped)
                if val is not None:
                    self.strings.append(val)
            return

        # ── items inside top-level category block ─────────────────────────────
        if self._obj_type == "category" and depth == 1 and stripped.startswith('"'):
            if not has_trailing_content_after_first_quote(stripped):
                val = extract_first_quoted(stripped)
                if val is not None:
                    self.strings.append(val)
            return

        # ── log / dialog: translate all quoted arguments ──────────────────────
        # "dialog phrase \"id\"" is a reference, not display text — skip.
        if tok in ALWAYS_TRANSLATE_VALUE:
            if _is_dialog_phrase_reference(stripped):
                return
            for val in extract_all_quoted(stripped):
                if val.strip():
                    self.strings.append(val.strip())
            return

        # ── quoted-key lines like `"mass" 5` → never translate ───────────────
        if stripped.startswith('"'):
            return

        # ── reference fields → skip ──────────────────────────────────────────
        if tok in REFERENCE_FIELDS:
            return

        # ── named translatable fields (description, name) ────────────────────
        if tok in TRANSLATABLE_FIELDS.get(self._obj_type or "", frozenset()):
            val = extract_field_value(stripped, tok)
            if val is not None:
                self.strings.append(val.strip())

    # ── helpers ───────────────────────────────────────────────────────────────

    def _clear(self, depth: int) -> None:
        for d in list(self._ctx.keys()):
            if d >= depth:
                del self._ctx[d]

    def _in_ctx(self, block_type: str) -> bool:
        return block_type in self._ctx.values()

    def _backtick_translatable(self) -> bool:
        ctx_vals = set(self._ctx.values())
        return self._obj_type == "conversation" or bool(
            ctx_vals & (CONV_BLOCK_KEYWORDS | {"word"})
        )

    @staticmethod
    def _extract_backtick_content(stripped: str) -> Optional[str]:
        pos = stripped.find("`")
        if pos == -1:
            return None
        end = stripped.find("`", pos + 1)
        return stripped[pos + 1 : end] if end != -1 else None


# ── Second pass: reconstruct file with translations ───────────────────────────

class Translator:
    """
    Replaces translatable strings in a file using a pre-built mapping.
    Mirrors Collector logic exactly; must stay in sync with it.
    """

    def __init__(self, translations: dict[str, str]) -> None:
        self._tr = translations
        self._ctx: dict[int, str] = {}
        self._obj_type: Optional[str] = None

    def translate(self, content: str) -> str:
        self._ctx = {}
        self._obj_type = None
        lines = content.splitlines(keepends=True)
        return "".join(self._process(line) for line in lines)

    # ── line processor ────────────────────────────────────────────────────────

    def _process(self, line: str) -> str:
        depth = count_tabs(line)
        raw = line.rstrip("\n").rstrip("\r")
        newline = line[len(raw) :]
        stripped = raw.lstrip("\t")
        tabs = raw[:depth]

        if not stripped or stripped.startswith("#"):
            return line
        self._clear(depth)

        # ── depth 0 ──────────────────────────────────────────────────────────
        if depth == 0:
            tok = first_unquoted_token(stripped)
            if tok in TOP_LEVEL_TYPES:
                self._obj_type = tok
                self._ctx[0] = tok
            else:
                self._obj_type = None
            return line

        # ── backtick line ─────────────────────────────────────────────────────
        if stripped.startswith("`"):
            if self._backtick_translatable():
                return tabs + self._sub_backtick(stripped) + newline
            return line

        # ── standalone quoted narrative inside conversation/choice ───────────
        if stripped.startswith('"') and self._in_ctx("conversation"):
            if not has_trailing_content_after_first_quote(stripped):
                return tabs + self._sub_first_quoted(stripped) + newline
            return line

        tok = first_unquoted_token(stripped)

        # ── control flow ──────────────────────────────────────────────────────
        if tok in CONTROL_KEYWORDS:
            return line

        # ── word block ────────────────────────────────────────────────────────
        if tok == WORD_BLOCK_KEYWORD:
            self._ctx[depth] = "word"
            return line

        # ── conversation / choice block ───────────────────────────────────────
        if tok in CONV_BLOCK_KEYWORDS:
            after = stripped[len(tok) :].strip()
            if not (after.startswith('"') or after.startswith("`")):
                self._ctx[depth] = tok
            return line

        # ── commodity block ───────────────────────────────────────────────────
        if tok == COMMODITY_BLOCK_KEYWORD:
            self._ctx[depth] = "commodity"
            return line

        # ── items inside word block ───────────────────────────────────────────
        if self._in_ctx("word"):
            if stripped.startswith('"') or stripped.startswith("`"):
                return tabs + self._sub_first_quoted(stripped) + newline
            return line

        # ── items inside commodity block ──────────────────────────────────────
        if self._in_ctx("commodity"):
            if stripped.startswith('"') and not has_trailing_content_after_first_quote(stripped):
                return tabs + self._sub_first_quoted(stripped) + newline
            return line

        # ── items inside category block ───────────────────────────────────────
        if self._obj_type == "category" and depth == 1 and stripped.startswith('"'):
            if not has_trailing_content_after_first_quote(stripped):
                return tabs + self._sub_first_quoted(stripped) + newline
            return line

        # ── log / dialog ──────────────────────────────────────────────────────
        # "dialog phrase \"id\"" is a reference, not display text — leave unchanged.
        if tok in ALWAYS_TRANSLATE_VALUE:
            if _is_dialog_phrase_reference(stripped):
                return line
            return tabs + self._sub_all_quoted(stripped) + newline

        # ── quoted-key attribute lines ────────────────────────────────────────
        if stripped.startswith('"'):
            return line

        # ── reference fields ──────────────────────────────────────────────────
        if tok in REFERENCE_FIELDS:
            return line

        # ── named translatable fields ─────────────────────────────────────────
        if tok in TRANSLATABLE_FIELDS.get(self._obj_type or "", frozenset()):
            return tabs + self._sub_field_value(stripped, tok) + newline

        return line

    # ── string substitution helpers ───────────────────────────────────────────

    def _lookup(self, text: str, quote: Optional[str] = None) -> str:
        """
        Look up a translation for `text`. Preserves leading/trailing spaces
        so dialog indentation (e.g. `   "Quoted speech"`) survives.
        """
        key = text.strip()
        translated = self._tr.get(key, key)
        if quote in ('"', "`"):
            translated = self._sanitize_for_quote(translated, quote)
        leading = len(text) - len(text.lstrip(" "))
        trailing = len(text) - len(text.rstrip(" "))
        return " " * leading + translated + " " * trailing

    @staticmethod
    def _sanitize_for_quote(text: str, quote: str) -> str:
        """
        Endless Sky strings are delimiter-based; unescaped quote delimiters
        inside translated text can break parsing. To keep generated data safe
        in all contexts, normalize both ES quote styles.
        """
        return text.replace('"', "'").replace("`", "'")

    def _sub_backtick(self, stripped: str) -> str:
        """Replace the content of a backtick string."""
        pos = stripped.find("`")
        if pos == -1:
            return stripped
        end = stripped.find("`", pos + 1)
        if end == -1:
            return stripped
        original = stripped[pos + 1 : end]
        return stripped[: pos + 1] + self._lookup(original, "`") + stripped[end:]

    def _sub_first_quoted(self, stripped: str) -> str:
        """Replace the first quoted string (double or backtick)."""
        dq = stripped.find('"')
        bq = stripped.find("`")
        if dq == -1 and bq == -1:
            return stripped
        if dq == -1:
            pos, q = bq, "`"
        elif bq == -1:
            pos, q = dq, '"'
        else:
            pos, q = (dq, '"') if dq < bq else (bq, "`")
        end = stripped.find(q, pos + 1)
        if end == -1:
            return stripped
        original = stripped[pos + 1 : end]
        return stripped[: pos + 1] + self._lookup(original, q) + stripped[end:]

    def _sub_all_quoted(self, stripped: str) -> str:
        """Replace every quoted string in a line (for log/dialog)."""
        result: list[str] = []
        i = 0
        while i < len(stripped):
            if stripped[i] in ('"', "`"):
                q = stripped[i]
                end = stripped.find(q, i + 1)
                if end != -1:
                    original = stripped[i + 1 : end]
                    result.append(q + self._lookup(original, q) + q)
                    i = end + 1
                    continue
            result.append(stripped[i])
            i += 1
        return "".join(result)

    def _sub_field_value(self, stripped: str, field_name: str) -> str:
        """
        Replace the value in `field_name "value"` or `field_name `value``.
        Preserves the whitespace between the keyword and the quote.
        """
        after = stripped[len(field_name) :]
        after_s = after.lstrip()
        ws = after[: len(after) - len(after_s)]
        if not after_s:
            return stripped
        q = after_s[0]
        if q not in ('"', "`"):
            return stripped
        end = after_s.find(q, 1)
        if end == -1:
            return stripped
        original = after_s[1:end]
        translated = self._lookup(original, q)
        return field_name + ws + q + translated + after_s[end:]

    # ── context helpers ───────────────────────────────────────────────────────

    def _clear(self, depth: int) -> None:
        for d in list(self._ctx.keys()):
            if d >= depth:
                del self._ctx[d]

    def _in_ctx(self, block_type: str) -> bool:
        return block_type in self._ctx.values()

    def _backtick_translatable(self) -> bool:
        ctx_vals = set(self._ctx.values())
        return self._obj_type == "conversation" or bool(
            ctx_vals & (CONV_BLOCK_KEYWORDS | {"word"})
        )


# ── File-level processing ──────────────────────────────────────────────────────

def process_file(
    src: Path,
    dst: Path,
    dry_run: bool,
    skip_translated: bool = False,
) -> int:
    """
    Translate one file and write the result.
    Returns the count of unique strings that required translation.
    """
    content = src.read_text(encoding="utf-8")

    # Phase 1 — collect
    collector = Collector()
    raw_strings = collector.collect(content)

    # Deduplicate while preserving first-occurrence order
    seen: set[str] = set()
    unique: list[str] = []
    for s in raw_strings:
        if s and s not in seen:
            seen.add(s)
            unique.append(s)

    if not unique:
        # Nothing to translate; write a verbatim copy
        if not dry_run:
            dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_text(content, encoding="utf-8")
        return 0

    rel = src.relative_to(DATA_DIR)
    print(f"  {rel}: {len(unique)} unique strings", flush=True)

    # Phase 2 — translate
    translations = batch_translate(unique, dry_run, skip_translated=skip_translated)

    # Phase 3 — reconstruct
    translator = Translator(translations)
    result = translator.translate(content)

    if not dry_run:
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_text(result, encoding="utf-8")

    return len(unique)


# ── Entry point ────────────────────────────────────────────────────────────────

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Translate Endless Sky data files to Russian."
    )
    parser.add_argument(
        "--file",
        metavar="PATH",
        help="Single file path relative to data/ (e.g. human/outfits.txt).",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Collect and show counts without calling the API or writing files.",
    )
    parser.add_argument(
        "--no-cache",
        action="store_true",
        help="Do not load the existing translation cache.",
    )
    parser.add_argument(
        "--skip-translated",
        action="store_true",
        help="Skip strings that already look translated (Cyrillic, no Latin).",
    )
    args = parser.parse_args()

    if not args.no_cache:
        load_cache()

    if args.file:
        files = [DATA_DIR / args.file]
    else:
        files = sorted(DATA_DIR.rglob("*.txt"))

    total_strings = 0
    total_files = 0
    errors = 0

    for src in files:
        if not src.exists():
            print(f"[ERROR] Not found: {src}", file=sys.stderr)
            errors += 1
            continue

        rel = src.relative_to(DATA_DIR)
        dst = OUTPUT_DIR / rel
        print(f"Processing: {rel}", flush=True)

        try:
            count = process_file(
                src,
                dst,
                args.dry_run,
                skip_translated=args.skip_translated,
            )
            total_strings += count
            total_files += 1
        except Exception as exc:
            print(f"  [ERROR] {rel}: {exc}", file=sys.stderr)
            errors += 1

    print(
        f"\nDone. {total_files} file(s) processed, "
        f"{total_strings} unique string(s) translated, "
        f"{errors} error(s)."
    )

    if not args.no_cache and not args.dry_run:
        save_cache()


if __name__ == "__main__":
    main()
