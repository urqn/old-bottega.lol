# merge_offsets.ps1
# Regenerates the menu offset header (bottega.lol\src\sdk\offsets.h) from the fresh
# offsets file posted in the Discord after each Roblox client update.
#
# Usage (from the repo root):
#   powershell -ExecutionPolicy Bypass -File output\merge_offsets.ps1
#   powershell -ExecutionPolicy Bypass -File output\merge_offsets.ps1 -Dump <path-to-raw-offsets.h>
#
# The raw file only contains class-property offsets under `namespace offsets` (lowercase).
# This script:
#   1. Parses the raw offsets into (namespace, member) -> value.
#   2. Rewrites it into `namespace Offsets { ... }` (what the menu code uses).
#   3. Re-adds the ~15 non-dumped extras the code references. Wherever possible the
#      extras alias fields that DO exist in the dump (same value); the few that do
#      not (Misc::StringLength, Lighting::GlobalShadows, etc.) use the known stable
#      values that hold across builds.

param(
    [string]$Dump   = '',
    [string]$SdkOut = ''
)

$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
if (-not $SdkOut) { $SdkOut = Join-Path $RepoRoot 'bottega.lol\src\sdk\offsets.h' }

$foundDump = $null
$candidates = @($Dump,
    (Join-Path $PSScriptRoot 'dumper\offsets.h'),
    (Join-Path $RepoRoot 'offsets.h'),
    (Join-Path $RepoRoot 'output\offsets.h')) | Where-Object { $_ } | Select-Object -Unique
foreach ($c in $candidates) {
    if (Test-Path -LiteralPath $c) { $foundDump = $c; break }
}
if (-not $foundDump) {
    Write-Host "Raw offsets file not found. Put the offsets.h you downloaded from the" -ForegroundColor Red
    Write-Host "Discord (https://discord.gg/shvMwDHFF9) at:" -ForegroundColor Red
    Write-Host "   $($candidates[1])" -ForegroundColor Yellow
    Write-Host "then re-run this script, or point at it with -Dump <path>."
    exit 1
}
$Dump = $foundDump

function ConvertTo-Hex([int64]$v) {
    return '0x{0:X}' -f $v
}

function Parse-Value($raw) {
    if ($raw -match '^0x([0-9a-fA-F]+)$') {
        return ConvertTo-Hex ([Convert]::ToInt64($raw, 16))
    }
    if ($raw -match '^\d+$') {
        return ConvertTo-Hex ([Convert]::ToInt64($raw, 10))
    }
    throw "cannot parse value: $raw"
}

# Returns @{ 'Ns::Member' = '0x..' } and the roblox version string.
function Read-Dump($path) {
    $data = @{}
    $version = ''
    $ns = ''
    foreach ($line in [System.IO.File]::ReadAllLines($path)) {
        if ($line -match 'version-[0-9a-f]{14,}') {
            $version = $Matches[0]
        }
        if ($line -match '^\s*namespace\s+(\w+)\s*\{') {
            $ns = $Matches[1]
        }
        elseif ($line -match '^\s*inline\s+constexpr\s+uintptr_t\s+(\w+)\s*=\s*((?:0x[0-9a-fA-F]+)|\d+)\s*;') {
            $key = "$ns::$($Matches[1])"
            $data[$key] = Parse-Value $Matches[2]
        }
        elseif ($line -match '^\s*\}\s*') {
            $ns = ''
        }
    }
    return ,@($data, $version)
}

function Get-Val($data, $key, $fallback) {
    if ($data.ContainsKey($key)) { return $data[$key] }
    return $fallback
}

function Add-Extras($data) {
    # --- Misc (Roblox shared string header) -------------------------------
    # Layout used by sdk.h WriteString: +0x00 data/inline, +0x10 size, +0x18 cap.
    $data['Misc::StringLength'] = '0x10'
    $data['Misc::AnimationId']  = '0xC0'

    # --- Camera::Viewport is an alias of the dumped ViewportInt16 ---------
    $data['Camera::Viewport'] = Get-Val $data 'Camera::ViewportInt16' '0x28C'

    # --- Lighting::GlobalShadows: not dumped; layout matches to this day ---
    $data['Lighting::GlobalShadows'] = '0x144'

    # --- Player::ModelInstance == dumped Character -------------------------
    $data['Player::ModelInstance'] = Get-Val $data 'Player::Character' '0x298'

    # --- MeshContentProvider: aliases of the dumped mesh-cache classes ------
    $data['MeshContentProvider::AssetID']    = '0x10'
    $data['MeshContentProvider::Cache']      = Get-Val $data 'MeshContentProvider::LruHolder' '0xD8'
    $data['MeshContentProvider::LRUCache']   = Get-Val $data 'LruHolder::MemEnforcedLRUCache' '0x20'
    $data['MeshContentProvider::MeshData']   = Get-Val $data 'LruNode::CachedItem' '0x40'
    $data['MeshContentProvider::ToMeshData'] = Get-Val $data 'LruNode::CachedItem' '0x40'

    # --- MeshData: aliases of the dumped FileMeshData ----------------------
    $data['MeshData::VertexStart'] = Get-Val $data 'FileMeshData::Vertices'    '0x0'
    $data['MeshData::VertexEnd']   = Get-Val $data 'FileMeshData::VerticesEnd' '0x8'
    $data['MeshData::FaceStart']   = Get-Val $data 'FileMeshData::Faces'       '0x30'
    $data['MeshData::FaceEnd']     = Get-Val $data 'FileMeshData::FacesEnd'    '0x38'

    # --- RenderQueue: stable engine enumerations -----------------------------
    $rq = [ordered]@{
        Opaque = '0x0'; Terrain = '0x1'; Decals = '0x2'; OpaqueCasters = '0x3'
        OpaqueAdorns = '0x4'; OpaqueWithAlpha = '0x5'; Water = '0x6'
        GlassTint = '0x7'; Glass = '0x8'; Transparent = '0x9'
        TransparentCasters = '0xA'; OnTopWithDepth = '0xB'
        OnTopReadOnlyDepth = '0xC'; AlwaysOnTop = '0xD'
        AlwaysOnTopAdorns = '0xE'; Screen = '0xF'; ScreenOnTopOfBlur = '0x10'
    }
    foreach ($k in $rq.Keys) {
        $data["RenderQueue::$k"] = $rq[$k]
    }
    return $data
}

# Namespaces of pure extras that get their own comment block.
$extraNamespaces = @('Misc', 'RenderQueue', 'MeshContentProvider', 'MeshData')
# Fields inside dumped namespaces that are extras (aliases).
$aliasFields = @{
    'Camera::Viewport'          = $true
    'Lighting::GlobalShadows'   = $true
    'Player::ModelInstance'     = $true
}

# Returns a list of (ns) ordered for emission: dumped namespaces first
# (alphabetical), then extras.
function Get-NamespaceOrder($data) {
    $nsSet = @{}
    foreach ($k in $data.Keys) {
        $ns = $k.Split('::')[0]
        $nsSet[$ns] = $true
    }
    $dumped = @($nsSet.Keys | Where-Object { $_ -notin $extraNamespaces } | Sort-Object)
    $extras = @($extraNamespaces)
    return @($dumped + $extras)
}

function Write-SdkHeader($data, $version, $outPath) {
    $sb = New-Object System.Text.StringBuilder
    [void]$sb.AppendLine('/*')
    [void]$sb.AppendLine(' * Auto-generated by output\merge_offsets.ps1 - do not edit by hand.')
    [void]$sb.AppendLine(" * Roblox Version: $version")
    [void]$sb.AppendLine(' * Source: raw offsets shared in the Discord (class-property offsets)')
    [void]$sb.AppendLine(' *         plus the non-dumped extras the menu code references')
    [void]$sb.AppendLine(' *         (Misc, RenderQueue, MeshContentProvider, MeshData and aliases).')
    [void]$sb.AppendLine(' */')
    [void]$sb.AppendLine('#pragma once')
    [void]$sb.AppendLine('#include <cstdint>')
    [void]$sb.AppendLine('')
    [void]$sb.AppendLine('namespace Offsets {')
    [void]$sb.AppendLine("    inline constexpr const char* roblox_version = `"$version`";")
    [void]$sb.AppendLine('')

    $nsOrder = Get-NamespaceOrder $data
    foreach ($ns in $nsOrder) {
        $isExtras = $ns -in $extraNamespaces
        [void]$sb.AppendLine("    namespace $ns {")
        if ($isExtras) {
            [void]$sb.AppendLine('        // Non-dumped extras consumed by the menu code.')
        }
        $members = @($data.Keys | Where-Object { $_.StartsWith($ns + '::') -and ($_ -split '::').Count -eq 2 } | Sort-Object)
        foreach ($key in $members) {
            $name = ($key -split '::')[1]
            $val  = $data[$key]
            if ($aliasFields.ContainsKey($key)) {
                [void]$sb.AppendLine("        inline constexpr uintptr_t $name = $val; // alias of a dumped field")
            }
            else {
                [void]$sb.AppendLine("        inline constexpr uintptr_t $name = $val;")
            }
        }
        [void]$sb.AppendLine('    }')
        [void]$sb.AppendLine('')
    }

    [void]$sb.AppendLine('} // namespace Offsets')
    [System.IO.File]::WriteAllText($outPath, $sb.ToString(), (New-Object System.Text.UTF8Encoding($false)))
}

$parsed  = Read-Dump $Dump
$data    = $parsed[0]
$version = if ($parsed[1]) { $parsed[1] } else { 'unknown' }

$oldVersion = $null
if (Test-Path -LiteralPath $SdkOut) {
    $m = Select-String -LiteralPath $SdkOut -Pattern 'Roblox Version:\s*(\S+)' | Select-Object -First 1
    if ($m) { $oldVersion = $m.Matches[0].Groups[1].Value }
}

$data = Add-Extras $data

Write-SdkHeader $data $version $SdkOut

$nsCount = @($data.Keys | ForEach-Object { $_.Split('::')[0] } | Sort-Object -Unique).Count
Write-Host "[merge_offsets] version: $version"
Write-Host "[merge_offsets] namespaces: $nsCount  offsets: $($data.Count)"
Write-Host "[merge_offsets] sdk -> $SdkOut"
if ($oldVersion -and $oldVersion -eq $version) {
    Write-Host '[merge_offsets] already up to date - header matches this version.'
}
else {
    Write-Host "[merge_offsets] $oldVersion -> $version - rebuild your build now."
}