# Flashcard decks

Every `*.tsv` (or Anki "Notes in Plain Text" `*.txt`) file in `decks/` is
compiled into the firmware as one deck of the **Cards** app.

```text
#deck:Spanish Top 5000        optional display name (max 28 bytes)
# other lines starting with # are comments
front<TAB>back                one card per line, UTF-8
```

- Extra columns are ignored, simple HTML from Anki (`<br>`, `<b>`, `&nbsp;`) is stripped.
- Up to 16 000 cards per deck; fields longer than 240 bytes are shortened.
- The 5x7 font renders ASCII plus Latin-1 accents (á é í ó ú ñ ü ç à è ê ö ¿ ¡ ...).
  Other scripts (Cyrillic, CJK, ...) show as `?`.
- Decks are sorted by file name; that order fixes where each deck's progress is
  stored. Renaming a deck or changing its card count resets its progress.

After adding or editing a deck, regenerate the C++ data and rebuild:

```bash
python3 tools/generate_flashcards_decks.py
```

```bash
pio run -e sticky-release
```

## Included deck: `spanish_top_5000.tsv`

The 5000 most frequent Spanish lemmas (proper nouns and letters removed) with
short English glosses, built by `tools/build_spanish_frequency_deck.py`.
Nouns carry their article (`el`/`la`).

Sources and licenses (the deck is CC BY-SA 4.0 as a derivative work):

- Frequency ranking: [hermitdave/FrequencyWords](https://github.com/hermitdave/FrequencyWords) (CC BY-SA 3.0, OpenSubtitles 2018)
- Glosses: [English Wiktionary](https://en.wiktionary.org) (CC BY-SA)
- Both via [doozan/spanish_data](https://github.com/doozan/spanish_data) (`frequency.csv`, `es-en.data`)

Rebuild it with:

```bash
curl -LO https://raw.githubusercontent.com/doozan/spanish_data/master/frequency.csv
```

```bash
curl -LO https://raw.githubusercontent.com/doozan/spanish_data/master/es-en.data
```

```bash
python3 tools/build_spanish_frequency_deck.py frequency.csv es-en.data assets/flashcards/decks/spanish_top_5000.tsv --count 5000
```
