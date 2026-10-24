# Flathub status

No Flathub submission has been made. A GitHub release does not publish an application on Flathub.

Flathub's current [Generative AI policy](https://docs.flathub.org/docs/for-app-authors/requirements#generative-ai-policy) prohibits AI-generated or AI-assisted manifests and automated submission interactions. A human maintainer must author the manifest and submission material, disclose the generated material already present in the project, and decide whether to request review. The project also needs development and maintenance history under the [inclusion requirements](https://docs.flathub.org/docs/for-app-authors/requirements#insufficient-development-history).

For a future human-authored package, Qt's [WebEngine BaseApp](https://github.com/flathub/io.qt.qtwebengine.BaseApp) supplies WebEngine and Chromium sandbox support. Its documented Qt 6 branch is 6.11, paired with `org.kde.Platform` and `org.kde.Sdk` 6.11. Wallpaper imports should use the file chooser portal; the simulated disk can stay in the application's private XDG directory without home-directory access. The application needs graphics, audio and optional network access for its browser.

The upstream desktop file, icon and AppStream metadata are project assets, not a Flathub submission. Future packaging must be built and tested before following the official [submission process](https://docs.flathub.org/docs/for-app-authors/submission). Acceptance and publication remain Flathub's decision.
