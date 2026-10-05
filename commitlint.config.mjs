/** @type {import('@commitlint/types').UserConfig} */
export default {
  extends: ["@commitlint/config-conventional"],
  rules: {
    "type-enum": [
      2,
      "always",
      [
        "build", // build system, CMake, dependencies
        "chore", // maintenance with no src or test change
        "ci", // GitHub Actions, COPR, OBS
        "docs", // README, CONTRIBUTING, code comments only
        "feat", // new user-facing capability
        "fix", // bug fix
        "perf", // performance with no behaviour change
        "refactor", // restructuring with no behaviour change
        "revert", // reverts a previous commit
        "style", // formatting only, no code change
        "test", // tests only
      ],
    ],
    "scope-enum": [
      2,
      "always",
      [
        "core", // libcalliecore: models, sync, storage
        "gui", // callie-gui C++ entry point
        "qml", // QML views and components
        "theme", // the design system
        "cli", // the callie command-line interface
        "google", // Google Calendar backend
        "caldav", // CalDAV backend
        "packaging", // rpm spec, debian/, repo metadata
        "data", // desktop entry, AppStream, icons
        "site", // callieapp.org under www/
        "ci",
        "deps",
      ],
    ],
    "scope-empty": [0], // cross-cutting commits may omit a scope
    "subject-case": [2, "always", "lower-case"],
    "subject-full-stop": [2, "never", "."],
    "header-max-length": [2, "always", 72],
    "body-max-line-length": [2, "always", 100],
  },
};
