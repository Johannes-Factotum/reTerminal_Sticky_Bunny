# Device test checklist

Run `python3 tools/device_smoke_test.py --port <PORT> --erase --seconds 300`
on the computer the Sticky is plugged into, then go through the list while it
listens. The script prints `[seen]` for each logged step and a PASS/FAIL
summary at the end.

## Launcher

- [ ] Tap the AI key: six cards (Pet, Pomodoro, Cards, To-Do, Habits, Draftling).
- [ ] Rotate to landscape: 3 x 2 layout; rotating from portrait opens Status Board.
- [ ] Shake with the launcher open: Book of Answers.

## Pomodoro

- [ ] Start a short custom timer (e.g. 10 s): two-note cue at start.
- [ ] At zero the melody repeats until `END`.

## Cards

- [ ] `STUDY NOW` shows a Spanish word; accents display (e.g. *mañana*).
- [ ] `SHOW ANSWER`, then `GOOD`: next card appears.
- [ ] Answer 10 cards: log shows `flashcards=storage save ... ESP_OK`.
- [ ] Power-cycle: the home page keeps the learned count.

## To-Do

- [ ] `+ ADD`, type a task with the `123` page accents, `SAVE`.
- [ ] Tick it: chime. `CLEAR DONE` removes it. Survives a restart.

## Habits

- [ ] Start page lists the four example habits.
- [ ] `START ROUTINE`: page 1/4 with the sun-salutation bunny.
- [ ] `DONE` chimes and goes to 2/4; `SKIP` moves on silently.
- [ ] After 4/4: "ROUTINE DONE!", jingle, streak 1.
- [ ] `EDIT ON PHONE`: two QR codes. Scan the first to join the Wi-Fi,
      the second opens the editor. Status changes to "PHONE CONNECTED".
- [ ] Rename a habit, change its bunny, `Save to Sticky`: the Sticky chimes and
      the start page shows the change. Wi-Fi turns off after `DONE`.

## Draftling

- [ ] Without Draftling flashed: card shows "not installed" and `BACK` works.
- [ ] After `tools/build_draftling.sh <PORT>`: `OPEN DRAFTLING` restarts into it.
- [ ] In Draftling, hold Up + Down for 2 s: returns to Bunny.
- [ ] F1 menu shows "Back to Bunny (Up+Down 2 s)" and it works.
