# Bug reporting implementation

This guide describes the development implementation. It does not establish
availability in New Horizons 0.87.1 or the currently validated playable build.
Packaging, rendered acceptance and platform verification are separate gates.

## Manual and retained-session reports

The Adventure Map and Settings callbacks open the same SDL `BugReportWindow`.
The manual form runs in the existing client and does not submit gameplay commands.
The additional Adventure Map row is size-gated; Settings provides access at the
minimum supported viewport. Both the form and diagnostic editor are 620×556.

The launcher can start a separate client process with
`--nointro --report-bug SESSION_DIRECTORY` after an eligible unexpected exit.
`--report-bug pending` selects the newest eligible retained session for a fallback
review. The report-only entry path runs before ordinary game initialization:
`BugReportBootstrap::runReporter` initializes filesystem/settings and native SDL
interface resources, without constructing `GameInstance`, starting simulation,
or initializing gameplay networking. It opens the same form as manual reporting.
It still needs a working renderer and the matching installed interface resources.

Linux launcher supervision recognizes a bounded set of signal-derived statuses.
Forwarded cancellation and debugger launches do not trigger this classification.
Windows `Test-ReportableGameExit` recognizes selected exception statuses and CRT
abort/terminate code 3. These are reasons to offer a report about an unclean exit;
they do not prove that a crash occurred or identify its cause. In particular,
code 3 alone is insufficient evidence of a crash. Other unexpected endings can
remain available through retained-session fallback on a later launcher start.
The reporter is not recursively supervised as another crashing game session.

## Capture, identity and retention

`CrashReportSession` creates a private `crash-reports/run-TOKEN` directory under
the configured client log root. The launcher assigns an exact session path to
its child through `NH_CRASH_SESSION_DIR`; immediate reporting uses that same path
rather than guessing which run ended. Direct client launches can allocate their
own session. Capture failure leaves ordinary logging and client startup available.

`CrashReportLogTarget` retains `client.log` and `client.previous.log`, each capped
at 1 MiB, independently of the ordinary client log. It flushes retained records
and rotates the two segments. Individual oversized records are truncated.
Retention keeps the newest five sessions, counting the current capture; starting
a new capture prunes older inactive sessions to make room. Creation timestamps
and a deterministic path tie-break select the newest eligible report.

Store, active-session and review locks protect retention and review. A second
active capture or a report currently under review prevents conflicting capture
cleanup. Active or already reviewed-in-another-process sessions are not eligible
for fallback selection. This is bounded diagnostic retention, not save recovery
or a minidump facility; it installs no signal handler to recover gameplay state.

`report.json` captures engine version, source identity, OS and product version.
The product version is recorded when its runtime configuration becomes readable.
An older retained session keeps its captured product/build identity even when a
newer reporter opens it. Missing previous-version metadata is labeled unknown;
the reporter's current product version is shown separately and must not silently
replace the version of the affected session.

## Review and publication

Diagnostic inclusion starts unchecked. Checking **Include a sanitized log**
immediately opens the editable review. The user inspects or edits the excerpt,
chooses **Use log**, then **Add log to issue description**. Reviewing alone does
not add it. **Review log** reopens the editor; accepting an edited excerpt requires
adding it again. Unchecking inclusion removes the reviewed log and any remembered
clipboard body. With inclusion checked, **Continue** requires a nonempty added
log. The editor can be canceled independently.

The initial excerpt uses a bounded tail of the captured segments or the ordinary
manual-report log. Automatic sanitization removes records matching sensitive
data, paths and network identifiers; it is deliberately conservative and can
also discard useful diagnostics. It cannot guarantee that arbitrary log text is
safe to publish. The editable review is therefore part of the contract, not a
substitute for review by the person sharing the report.

The added log appears in a collapsible **Reviewed diagnostic log** section of the
issue body. Its code-block delimiter is longer than every backtick sequence in
the log. **Continue** opens a draft for `thegandalf196/new-horizons`; the game does
not submit an issue or upload an attachment. The user reviews and submits on
GitHub. There is no local-save step in this form.

When the encoded URL is at most 7,000 bytes, the complete body, including any
added log, is prefilled through the browser URL. Longer reports require **Copy
full report**, a successful clipboard write, then **Continue**. This opens a
title-only draft where the user pastes the complete copied body. The handoff
requires the remembered copied body to equal the current body; editing the report
after copying requires another copy. A failed copy or stale body cannot enable
the title-only handoff. The browser handoff does not silently truncate content.

Title and description are required. Limits are 120 bytes for the title, 1,200
bytes per description field, 64 KiB for the reviewed log, and 64 KiB for the
complete wrapped issue body. The complete-body bound applies to both handoff
routes. User-edited log text still requires the user's privacy review.

Canceling the report closes the form. For a retained report, dismissal marks the
session dismissed so fallback does not repeatedly offer it; it does not claim
that an issue was submitted. Clean game teardown removes its capture directory
after closing log targets. An unclean exit leaves evidence pending. If the
reporter cannot initialize, retained evidence remains available. Log-read,
clipboard-copy and browser-open failures are reported in the form without
discarding the report text. The launcher preserves the game exit status through
the reporter's exit.

## Contributor checks

Use the platform's documented CMake/Conan setup first. In an existing configured
development build, explicitly enable the client, unit tests and bug-report
regressions; preserve its other configuration and toolchain options:

```sh
cmake -S . -B build -DENABLE_CLIENT=ON -DENABLE_TEST=ON -DENABLE_NH_BUG_REPORT_REGRESSION=ON
cmake --build build --target vcmiclient nhCrashReportCaptureTest nhBugReportDraftTest nhBugReportDataTest --parallel 10
ctest --test-dir build --output-on-failure -R '^(nhCrashReportCaptureTest|nhBugReportDraftTest|nhBugReportDataTest)$'
```

CTest registration lives in `test/CMakeLists.txt`; the capture executable is
declared in `clientapp/CMakeLists.txt`. Do not rely on the test-directory option's
default to enable an earlier clientapp target; pass the option explicitly.
Run only one compilation invocation at a time across a shared workspace.

The CPU capture regression starts owned synthetic children that exit uncleanly,
then checks active/review locking, segment rotation, bounded retention, newest
selection and clean-session removal. Draft/data regressions check issue encoding,
validation, version distinction, inline log fencing, URL/complete-body bounds,
exact copied-body freshness, sanitization and the retained local-export helper.
The local-export helper is not a form control. These tests do not launch the
actual graphical client into a crash, establish
correct SDL transitions, or prove Windows process/renderer behavior.

Before playable delivery, verify manual entry, editor input, opt-in reset,
normal/pressed/disabled/highlighted controls, minimum-resolution fit, short-body
browser handoff, complete clipboard readback and title-only handoff, stale-copy
refusal, browser failure, cancellation, retained-session selection and reporter
cleanup in the actual renderer. Use the isolated graphical workflow and audio requirements in
[AGENTS.md](../../AGENTS.md). Native Windows launcher classification and a matching
Windows package require their own verification; Linux CPU tests are insufficient.
See [Linux playable snapshots](../../tools/ci/LINUX_PLAYABLE_SNAPSHOT.md) for the
validation and promotion boundary. Build success alone does not promote a build.
