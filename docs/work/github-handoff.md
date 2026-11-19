# GitHub publication handoff

The repository and workflows can be prepared without an authenticated GitHub API
session. SSH Git access was tested during the October preparation; current
transport availability does not verify hosted settings. The steps below are an owner runbook,
not independent verification that settings have been applied. Keep credentials outside Git and
chat. Use an already authenticated browser or run `gh auth login` on a trusted
machine; never paste a token into an issue, document, or command transcript.

## Owner-reported setup, recorded 28 October 2026

The owner has downloaded the documentation artifact from CI, merged the audit
and README pull requests, installed DCO for all organization repositories, and
added `DCO` to the required checks alongside `build-and-test` and `docs` in the
main-branch ruleset. At that time the repository was reported private and Pages deployment was
pending. These reports establish progress; this workspace has no authenticated
GitHub API session to verify the settings, run URLs or enforcement on the
publication revision. Use the steps below to verify the remaining launch items.

## Before changing visibility

1. Record institutional/contributor publication authorization. The
   [19 inventoried benchmark imports](../coverage/third-party-inputs.json) have
   verified SMT-LIB release provenance and CC BY 4.0 attribution; preserve their
   notice and recheck any additional imports. Complete review of remote branches, tags,
   attachments, release assets, and Actions logs. A local Git scan cannot inspect
   hosted artifacts. Do not delete history without an agreed remediation.
2. Review the closure of the [correctness findings](../coverage/page-review.md#correctness-follow-ups),
   then review the assembled manual and the candidate's qualification report. Record
   consolidated reader acceptance and the exact approved commit.
3. Push the verified preparation commits if they are not already on the remote.
   Merge the reviewed branch into `main`, preserving the documented source and
   historical commits. Keep visibility private during these preparatory actions.
4. Run {actionsworkflow}`CI <.github/workflows/ci.yml>` on the merged revision and inspect `build-and-test` and `docs`.
   Require both checks for future pull requests. Ensure the
   [DCO app](https://github.com/apps/dco) covers Primo and require its `DCO`
   check on new contributions. Preserve historical commits without adding
   retrospective sign-offs.
5. In **Settings → Environments**, configure `github-pages` to deploy only from
   `main`, with owner approval required. Enable owner self-review if the owner
   is the only maintainer. If protection is unavailable while private, keep
   deployment disabled and apply it immediately after the approved visibility
   change, before running publication.
6. In **Settings → Pages**, select **GitHub Actions** as the publishing source.
   Keep the project URL `https://primo-solver.github.io/primo/`; no custom domain
   or organization-homepage repository is needed.

## Publication and verification

After the owner approves publication, change repository visibility to public.
GitHub exposes Actions history/logs too; the audit above must already be complete.
Enable private vulnerability reporting under **Settings → Security** and verify
that **Security → Report a vulnerability** is available. Apply a main-branch rule
requiring CI/DCO checks, resolved discussions and review by a maintainer other
than the author where possible. For the single-maintainer starting point, keep
reviews for outside contributions and document owner-reviewed maintenance.
Prevent force pushes and deletion of `main`; use the protected deployment
environment for publication approval independently of the merge policy.

Run {actionsworkflow}`Publish documentation <.github/workflows/pages.yml>` manually on `main`, approve the environment job,
and verify its deployment result. A private-repository run intentionally skips
deployment. The {actionsworkflow}`External documentation links <.github/workflows/external-links.yml>` workflow is scheduled weekly
and can be run manually; inspect its reports rather than overriding failures.

From a fresh unauthenticated environment, verify:

- HTTPS clone and the documented build/test commands;
- the manual, assets/favicon, search, code/edit links, and historical fragments;
- private reporting and public issue/PR templates;
- required check names match actual CI and DCO runs;
- a harmless test PR without sign-off fails DCO, while a properly signed-off
  contribution passes (close the test without merging unrelated content).

Record the public revision, workflow run URLs, deployment URL, and verification
outcomes in the publication completion record. A numbered release is deferred.

Useful read-only checks on the authenticated machine:

```bash
gh repo view primo-solver/primo --json isPrivate,defaultBranchRef,homepageUrl
gh run list --repo primo-solver/primo --limit 10
gh api repos/primo-solver/primo/pages
gh api repos/primo-solver/primo/private-vulnerability-reporting
```

If configuration cannot be verified, leave that item pending. Do not infer
publication or successful CI from locally committed workflow files.
