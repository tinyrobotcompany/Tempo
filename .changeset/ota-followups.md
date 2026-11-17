---
"tempo": patch
---

OTA and credential follow-ups from the #18/#19 reviews:

- Update downloads may redirect only from this repository's GitHub release URL to GitHub's asset CDN. Redirects back to github.com, to other hosts, or containing spaces, control characters or backslashes are rejected. Release metadata never redirects.
- The download step now releases its HTTP client, hash context and OTA handle in a single cleanup path. Messages and activation order are unchanged.
- After a reboot, the display and phone page report a saved transport key straight away, without waiting for the first transport fetch.
- CI: the standalone Security Audit is no longer cancelled by the copy that runs inside Publish OTA Release, and the changeset self-test no longer creates red error annotations on passing jobs.
- README: the build instructions use key provisioning instead of `.env`.

Validation: host tests cover the redirect rules, cleanup accounting on hash, begin and write failures, and the first-query credential probe. No hardware qualification is claimed.
