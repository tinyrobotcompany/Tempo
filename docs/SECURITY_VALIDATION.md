# Continuous security validation

The Security Audit required check includes C/C++, JavaScript, Actions and Python CodeQL analysis, secret scanning and current vulnerability advisories for the hash-locked certificate-service runtime/test dependencies. It also runs every Monday, even without code changes. Scheduled failures appear in GitHub Actions and must be investigated; this does not create Azure operational alerts.

C/C++ analysis uses CodeQL's supported no-build mode. It infers includes and macros and is an additional static check, not proof that every ESP-IDF configuration was analyzed. The actual ESP32-P4 build, real SDK identity/TLS tests and hardware acceptance remain necessary. See [GitHub's documented limitations](https://docs.github.com/en/code-security/reference/code-scanning/codeql/build-options-for-compiled-languages).

Dependabot checks backend pip and Docker dependencies monthly with one routine PR per ecosystem. Security alerts/updates remain separate. The existing GitHub Actions monthly grouping is unchanged. Keep production and test hash locks synchronized when accepting updates; image publication still requires review and release gates.

Public fork PRs do not spend the OpenAI credential automatically. Their required Codex check fails before the paid call; a maintainer must inspect the contribution and import it onto a repository branch, where normal CI and Codex review run. No fork code is executed with the review credential. This cost boundary does not make model review immune to malicious text and does not replace human review.

The full host suite now includes the mechanical CAD tests using the checked-in frozen uv lock. Install uv alongside the documented host tools. These tests validate the model, not a manufactured enclosure.
