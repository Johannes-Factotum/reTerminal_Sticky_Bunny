# Habits assets

- `firmware/*.png`: 240 px 1-bit habit bunnies, composed from the desktop-pet
  line art by `tools/generate_habit_bunnies.py`, which also writes
  `src/ui/assets/habits_assets.{h,cpp}` (300 px page versions).
- `web/index.html`: the phone editor. `tools/generate_habits_web_page.py`
  embeds it (with the bunny art as data URIs) into
  `src/apps/habits/habits_web_page.cpp`.

To add a bunny: add a builder to `ART` in `tools/generate_habit_bunnies.py`,
a matching entry to `HabitIcon` in `src/apps/habits/habits_model.h`, its file
name to `ICONS` in `tools/generate_habits_web_page.py` and a label to
`ICON_NAMES` in `web/index.html`, then rerun both generators.
