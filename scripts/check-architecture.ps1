$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

$checks = @(
    @{ Path = '.DESIGN/adr/ADR-0001-technology-baseline.md'; Name = 'ADR-0001' },
    @{ Path = '.DESIGN/decision-backlog.md'; Name = 'decision backlog' },
    @{ Path = 'README.md'; Name = 'README' },
    @{ Path = 'ROADMAP.md'; Name = 'ROADMAP' },
    @{ Path = '.DESIGN/Atlas_Master_Design_Specification_Revision_3_0.md'; Name = 'spec revision 3.0' }
)

foreach ($check in $checks) {
    if (-not (Test-Path $check.Path)) {
        throw "Missing $($check.Name): $($check.Path)"
    }
}

$requiredContent = @(
    'atlas-domain',
    'v0.1.0',
    'decision backlog'
)

$traceFiles = @(
    '.DESIGN/adr/ADR-0001-technology-baseline.md',
    '.DESIGN/decision-backlog.md',
    'README.md',
    'ROADMAP.md',
    '.DESIGN/Atlas_Master_Design_Specification_Revision_3_0.md'
)

foreach ($token in $requiredContent) {
    $found = $false
    foreach ($file in $traceFiles) {
        if (Test-Path $file) {
            $content = Get-Content -Path $file -Raw -ErrorAction SilentlyContinue
            if ($content -match [regex]::Escape($token)) {
                $found = $true
                break
            }
        }
    }

    if (-not $found) {
        throw "Missing required token '$token' in the traceable v0.1.0 documentation set."
    }
}

Write-Host 'Architecture governance checks passed.'
