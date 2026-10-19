# SEQ landing page

The public site root introduces SEQ; the existing Studio remains at `ui/`.
The rhythm sketch is a standalone browser illustration, not the firmware engine.
Audio starts only on Play and stops when the page becomes hidden.

Build and preview from the repository root (choose a fresh output directory):

```sh
python3 tools/build_site.py --output /tmp/seq-site
python3 -m http.server 8790 --directory /tmp/seq-site
```

Open http://localhost:8790. The builder explicitly allows only index.html,
site.css, and site.mjs from this directory. No dependencies or remote fonts
are required. Publication uses the existing publish-studio GitHub Actions
workflow on main; macOS packaging remains in its existing workflow.

Validation: `python3 -m unittest discover -s tools -p 'test_*.py'`.
Check desktop and narrow screens, keyboard focus, step toggles, Play/Stop,
Reset, and Studio/manual links when changing the page.

## Web manual

`/manual/` reuses the keyboard markup from `apps/studio/index.html` at build
time, the keyboard controller, and the safe Markdown renderer from Studio.
Edit controls in Studio and prose in `docs/manual.md`; do not maintain separate
copies here. The web page stays in reference mode; live and draft-specific
help remains in Studio. Hash links preserve mode/key/OS/grid or section/search.
