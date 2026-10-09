<#
check_deps.ps1 -- verify that every DLL under <Bin> (recursively) has all of its
non-system import dependencies available inside <Bin> itself.

Usage: powershell -File check_deps.ps1 -Bin <dist\bin> -Dumpbin <path\to\dumpbin.exe>
Exit code 0 = closure complete, 1 = missing dependencies (listed on stdout).

"System" DLLs are those that exist in %SystemRoot%\System32 on this machine,
api-ms-*/ext-ms-* API sets, and a short allow-list of optional GPU-vendor DLLs
(loaded only by hardware-specific GStreamer plugins that fail gracefully).
#>
param(
    [Parameter(Mandatory=$true)][string]$Bin,
    [Parameter(Mandatory=$true)][string]$Dumpbin
)

$ErrorActionPreference = 'Stop'
$sys32 = Join-Path $env:SystemRoot 'System32'
$optional = @('nvcuda.dll','nvencodeapi64.dll','nvcuvid.dll','d3d12core.dll','d3d12sdklayers.dll')

$present = @{}
Get-ChildItem -Path $Bin -Recurse -Filter *.dll | ForEach-Object { $present[$_.Name.ToLower()] = $true }

$missing = New-Object System.Collections.Generic.List[string]
Get-ChildItem -Path $Bin -Recurse -Filter *.dll | ForEach-Object {
    $f = $_
    $out = & $Dumpbin -dependents $f.FullName 2>$null
    foreach ($line in $out) {
        if ($line -match '^\s+(\S+\.dll)\s*$') {
            $dep = $Matches[1].ToLower()
            if ($present.ContainsKey($dep)) { continue }
            if ($dep -like 'api-ms-*' -or $dep -like 'ext-ms-*') { continue }
            if ($optional -contains $dep) { continue }
            if (Test-Path (Join-Path $sys32 $dep)) { continue }
            $missing.Add(("{0} -> {1}" -f $f.FullName.Substring($Bin.Length).TrimStart('\'), $dep))
        }
    }
}

if ($missing.Count -gt 0) {
    Write-Output "MISSING DEPENDENCIES:"
    $missing | Sort-Object -Unique | ForEach-Object { Write-Output ("  " + $_) }
    exit 1
}
Write-Output ("closure OK: {0} DLLs checked" -f $present.Count)
exit 0
