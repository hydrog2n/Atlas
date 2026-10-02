# Phase 4 --- Release Integration

## Agent Operating Rules

This file is the execution contract for this phase of an Atlas release.

-   The target release is the version specified by the user's
    invocation. Treat it as `[VERSION]`.
-   Consult the canonical Atlas Master Design Specification,
    `ROADMAP.md`, applicable ADRs, repository documentation, actual
    repository state, and Phase 3 validation evidence.
-   The Master Design Specification is the architectural authority.
-   Perform **only this phase**.
-   Stop and report rather than guess when a mandatory stop condition or
    unresolved architectural decision is encountered.

## Objective

Prepare the validated implementation as the Atlas **\[VERSION\]**
release.

## Versioning

Propagate the **application version `[VERSION]`** consistently through
applicable build metadata, source constants, README/status information,
CI/release configuration, and package/release metadata.

Do **not** automatically change project-schema, geometry-engine,
exporter, plugin-API, or specification versions. These are independent
compatibility dimensions and change only when their corresponding
contract changed. Search the repository for stale version references
rather than relying on a predetermined file list.

## Documentation and Comments

Update applicable README/current status, roadmap status, build/developer
documentation, relevant design/interface documentation, examples,
developer instructions, and comments/API documentation made stale by
implementation.

Documentation must accurately distinguish shipped, current, and planned
capabilities. Do not modify canonical architecture merely to legitimize
an implementation shortcut.

## Changelog / Release Notes

Create/update `[VERSION]` release notes with applicable sections:

-   Added
-   Changed
-   Fixed
-   Testing / Validation
-   Compatibility / Migration
-   Known Limitations

## Final Verification

1.  Search for stale versions and incorrect current-release claims.
2.  Verify documentation/comments against actual behavior.
3.  Rerun the appropriate build and test/conformance workflow.
4.  Verify compatibility fixtures and release gates remain valid.
5.  Confirm build/test instructions remain reproducible.
6.  Confirm required release evidence is available.
7.  Sign the final Windows desktop executable with the configured
    self-signed development code-signing certificate and verify the
    Authenticode signature.
8.  Confirm no Atlas stop-ship condition remains.

## Windows Executable Signing

After the final Windows Qt build and `windeployqt` deployment have completed,
sign `build/windows-qt-sdk/atlas.exe`. Signing MUST be the last operation that
modifies the executable; if it is rebuilt or modified afterward, sign and
verify it again.

Use the existing certificate in `Cert:\CurrentUser\My` whose parsed simple
name is `Hydrogen Studios, LLC`. Windows may quote the comma in the displayed
distinguished name (for example, `CN="Hydrogen Studios, LLC"`), so do not
compare the raw `Subject` string. Do not create/import a certificate
automatically, request a password, export a PFX, or access/export the private
key. Require exactly one matching, unexpired code-signing certificate with
`HasPrivateKey = True`; report its thumbprint and expiration in the Phase 4
evidence without exposing key material.

From the repository root, the signing and verification commands are:

```powershell
$certificates = @(Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert |
    Where-Object {
        $_.GetNameInfo([System.Security.Cryptography.X509Certificates.X509NameType]::SimpleName, $false) -eq 'Hydrogen Studios, LLC' -and
        $_.HasPrivateKey -and $_.NotAfter -gt (Get-Date)
    })
if ($certificates.Count -ne 1) {
    throw "Expected exactly one usable Atlas code-signing certificate; found $($certificates.Count)."
}
$cert = $certificates[0]
$exe = Join-Path $PWD 'build/windows-qt-sdk/atlas.exe'
$signtool = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.28000.0\x64\signtool.exe'
if (-not (Test-Path $signtool)) {
    throw "SignTool not found: $signtool"
}
& $signtool sign /sha1 $cert.Thumbprint /fd SHA256 $exe
if ($LASTEXITCODE -ne 0) { throw 'SignTool failed to sign atlas.exe.' }
& $signtool verify /pa /v $exe
if ($LASTEXITCODE -ne 0) { throw 'SignTool Authenticode verification failed.' }
$signature = Get-AuthenticodeSignature $exe
$signature | Format-List Status, StatusMessage, SignerCertificate
if ($signature.Status -ne 'Valid') {
    throw "PowerShell signature verification failed: $($signature.StatusMessage)"
}
```

The certificate is self-signed for development. A valid local signature does
not imply that other computers trust Atlas; do not describe it as a trusted
public publisher signature. Never commit or distribute a private-key PFX file.
If the certificate, private key, SignTool, or successful verification is
unavailable, Phase 4 MUST report `Release complete: no` and identify the
signing blocker; it MUST NOT silently skip signing.

## Final Scope Lock

Phase 4 is an integration and evidence-reconciliation phase, not a place to
reinterpret the roadmap.

- Read the original Phase 1 scope ledger, the final Phase 2 reconciliation,
    and the final Phase 3 result together.
- Do not declare a release complete when any in-scope packet, ship item, CORE
    requirement, acceptance criterion, release gate, compatibility fixture, or
    earlier-release regression is partial, unvalidated, or merely simulated
    without the evidence required by the roadmap.
- A green build and passing tests are necessary but not sufficient. The
    release status must match the roadmap's version-completion statement.
- If the ledger and implementation disagree, report `Release complete: no`
    and stop. Do not rewrite the roadmap, specification, or ledger to make the
    implementation appear complete.
- Preserve independent schema, geometry, exporter, and plugin versions; only
    propagate the application version during release integration.

Do not alter tests, architecture, fixtures, or canonical documentation
merely to make the release appear complete.

## Phase Gate

Declare `[VERSION]` complete only when implementation, tests, version
metadata, documentation, compatibility information, release evidence,
and applicable release gates agree. Missing required evidence is a
failed release gate, not deferred cleanup.

## Required Completion Report

End this phase with:

``` text
Result: completed | partial | blocked
Phase: 4 — Release Integration
Version: [VERSION]

Release Integration:
- Scope shipped:
- Final scope-ledger reconciliation:
- Application-version locations updated:
- Independent compatibility versions changed:
- Windows executable signing: certificate subject/thumbprint/expiry, signing result, SignTool verification, PowerShell status:
- Changelog/release notes updated:
- Documentation/status updated:
- Compatibility/migration impact:
- Known limitations:

Final Verification:
- Final commands/checks run:
- Result:
- Signed executable path and signature verification result:
- Release gates confirmed:
- Release evidence status:
- Stop-ship defects:
- Unvalidated or partial roadmap items:

Release Status:
- Release complete: yes | no
- ADR or design decision needed:
- Reason:
```

Declare the release complete only when the applicable release gates and
required evidence are demonstrated.

## Release Development Log

After producing the Required Completion Report, append that report to the existing release log:

`.DESIGN/agent-workflow/logs/[VERSION].md`

Do not replace or modify earlier phase records.

Append:

---

## Phase 4 — Release Integration

<insert the complete Phase 4 Required Completion Report here>

RULES:

The report written to the log must match the report returned to the user. Preserve all earlier phase records exactly. The completed log serves as the development and release-process record for Atlas [VERSION].
If the expected release log does not exist, stop and report the missing development history rather than silently creating an incomplete log.
