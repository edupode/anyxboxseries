# Contributing

Start with an updated main branch and create a branch for the change you are making. Pick a name that explains its purpose, such as docs/imports or compat/startup. If you are working from a fork, use that fork as origin and open the pull request against main in this repository.

Keep commits focused and give each one a message that describes the change. This example shows how to send a documentation update.

~~~powershell
git switch main
git pull --ff-only
git switch -c docs/imports
git add README.md
git commit -m "Explain the import report"
git push -u origin docs/imports
~~~

Send changes to main through a pull request. Preserve shared history and use a new commit to correct a change that is already published. Review git status before pushing. Keep game builds, generated binaries, credentials, and reports with local paths on your machine.

Explain what changed in the pull request and include the commands you ran. Run the native build and CTest for changes to graphics or startup. Run `python -m unittest discover -s tools` for changes to the import inspector. Use a test package you can share for viewer and import inspector checks.

The project is available under the [Common Public Attribution License 1.0](LICENSE). Keep the existing copyright and attribution notices when changing code. Send samples and SDK references as links to the original sources.
