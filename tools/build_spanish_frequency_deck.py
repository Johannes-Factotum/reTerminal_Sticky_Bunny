#!/usr/bin/env python3
"""Build a Spanish->English frequency flashcard deck as TSV.

Inputs come from https://github.com/doozan/spanish_data :
  frequency.csv  (CC-BY-SA 3.0, hermitdave/FrequencyWords)
  es-en.data     (CC-BY-SA, en.wiktionary.org)

Usage:
  python3 tools/build_spanish_frequency_deck.py frequency.csv es-en.data \
      assets/flashcards/decks/spanish_top_5000.tsv --count 5000
"""

from __future__ import annotations

import argparse
import csv
import re
from pathlib import Path

POS_LABEL = {
    "n": "n.", "v": "v.", "adj": "adj.", "adv": "adv.", "pron": "pron.",
    "prep": "prep.", "conj": "conj.", "interj": "interj.", "num": "num.",
    "determiner": "det.", "art": "art.", "contraction": "contr.",
}
SKIP_POS = {"prop", "none", "letter", "prefix", "phrase", "particle", "pos"}
SKIP_GLOSS = re.compile(
    r"\b(form|plural|feminine|masculine|apocopic|alternative|spelling|"
    r"misspelling|abbreviation|superlative|diminutive|augmentative|"
    r"obsolete|archaic|initialism|clipping)\s+(form\s+)?of\b",
    re.IGNORECASE,
)
MAX_BACK = 70


def parse_dictionary(path: Path) -> dict[str, list[dict]]:
    entries: dict[str, list[dict]] = {}
    word = None
    current = None
    expect_word = False
    with path.open(encoding="utf-8") as handle:
        for raw in handle:
            line = raw.rstrip("\n")
            if line == "_____":
                expect_word = True
                continue
            if expect_word:
                word = line
                expect_word = False
                continue
            if line.startswith("pos: "):
                current = {"pos": line[5:].strip(), "g": "", "glosses": []}
                entries.setdefault(word, []).append(current)
            elif current is not None and line.startswith("  g: "):
                current["g"] = line[5:].strip()
            elif current is not None and line.startswith("  gloss: "):
                current["glosses"].append(line[9:].strip())
    return entries


def clean_gloss(text: str) -> str:
    text = re.sub(r"\{\{[^}]*\}\}", "", text)
    text = re.sub(r"^\([^)]*\)\s*", "", text)  # leading qualifier
    text = text.replace('"', "").strip(" ;,.")
    return re.sub(r"\s+", " ", text)


def english_for(entries: list[dict], pos: str) -> tuple[str, str]:
    glosses: list[str] = []
    gender = ""
    for entry in entries:
        if entry["pos"] != pos:
            continue
        if not gender and entry["g"]:
            gender = entry["g"]
        for gloss in entry["glosses"]:
            if SKIP_GLOSS.search(gloss):
                continue
            for part in clean_gloss(gloss).split(";"):
                part = part.strip(" ,.")
                if part and part.lower() not in (g.lower() for g in glosses):
                    glosses.append(part)
    back = ""
    for gloss in glosses:
        candidate = gloss if not back else f"{back}; {gloss}"
        if len(candidate) > MAX_BACK:
            if not back:
                back = gloss[:MAX_BACK - 3].rstrip() + "..."
            break
        back = candidate
        if len(back) > 40:
            break
    return back, gender


def front_for(word: str, pos: str, gender: str) -> str:
    if pos != "n":
        return word
    g = gender.split(",")[0].strip()
    if g.startswith("mf") or g == "mfbysense":
        return f"el/la {word}"
    if g.startswith("f"):
        return f"la {word}"
    if g.startswith("m"):
        return f"el {word}"
    return word


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("frequency")
    parser.add_argument("dictionary")
    parser.add_argument("output")
    parser.add_argument("--count", type=int, default=5000)
    args = parser.parse_args()

    dictionary = parse_dictionary(Path(args.dictionary))
    rows: list[tuple[str, str]] = []
    seen: set[tuple[str, str]] = set()
    with open(args.frequency, encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            if len(rows) >= args.count:
                break
            word, pos, flags = row["spanish"], row["pos"], row["flags"]
            if pos in SKIP_POS or flags in {"NOUSAGE", "DUPLICATE"}:
                continue
            if (word, pos) in seen or word not in dictionary:
                continue
            back, gender = english_for(dictionary[word], pos)
            if not back:
                continue
            seen.add((word, pos))
            label = POS_LABEL.get(pos, pos + ".")
            rows.append((front_for(word, pos, gender), f"({label}) {back}"))

    out = Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open("w", encoding="utf-8") as handle:
        handle.write("#deck:Spanish Top 5000\n")
        handle.write("#source:frequency hermitdave/FrequencyWords "
                     "(CC-BY-SA 3.0); glosses en.wiktionary.org (CC-BY-SA) "
                     "via github.com/doozan/spanish_data\n")
        for front, back in rows:
            handle.write(f"{front.replace(chr(9), ' ')}\t"
                         f"{back.replace(chr(9), ' ')}\n")
    print(f"wrote {len(rows)} cards to {out}")


if __name__ == "__main__":
    main()
