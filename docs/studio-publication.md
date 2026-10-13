# Hosted Studio publication

Studio is hosted at https://oreoluwa-xyz.github.io/seq-studio/ui/.

Edit the UI in `apps/studio`, the manual in `docs/manual.md`, and module authoring documentation/examples in `modules`. Push changes to `main`. The **Publish Studio** workflow tests and builds the static site, replaces its generated files in `oreoluwa-xyz/seq-studio`, and pushes a commit. GitHub Pages then deploys that public repository's `main` branch. Firmware and desktop releases remain on the separate version-tag workflow.

The workflow runs for changes to those inputs, the license, or publication scripts. It can also be run manually from Actions. A retry always checks out current `main`; unchanged output does not create a commit. Runs are serialized. The public repository's README and other non-generated files are preserved; obsolete files inside the generated UI and modules directories are removed.

`tools/build_site.py` includes only browser runtime HTML/CSS/modules, manual, module documentation/schema/examples, license and entry point. Fixtures, tests, firmware, local server code and device data are excluded. Hosted firmware management directs users to the desktop app/local service.

## Access and maintenance

`STUDIO_DEPLOY_KEY` is an Actions secret in the private platform repository. It is an SSH deploy key with write access only to the public `seq-studio` repository. Its public key is listed under that repository's Deploy keys as **seq-platform Studio publisher**. No personal access token is stored in CI. The private platform's workflow token has read-only contents permission.

To rotate access, create a new dedicated SSH key, replace the public repository's write deploy key and the platform secret, and run **Publish Studio** manually. Never commit a private key. Removing the key or secret disables automatic publication.

Check **Publish Studio** in the private repository for build/push failures, and **pages build and deployment** in the public repository for hosting failures. Successful publication is followed by a separate Pages deployment; a successful sync alone does not prove the site is live.

Local verification:

```sh
python3 -m unittest discover -s tools -p 'test_site.py'
python3 tools/build_site.py --output /tmp/seq-site-new
```

The output directory must not already exist. Do not edit generated files in `seq-studio`; subsequent syncs replace them.
