# merge_offsets.ps1
# Regenerates the two offset headers consumed by the bottega.lol projects from a
# fresh phantomX-Roblox-Dumper output file.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File output\merge_offsets.ps1
#   powershell -ExecutionPolicy Bypass -File output\merge_offsets.ps1 -Dump <path-to-dumped-offsets.h>
#
# The dump only contains class-property offsets under `namespace offsets` (lowercase).
# This script:
#   1. Parses the dump into (namespace, member) -> value.
#   2. Rewrites it into `namespace Offsets { ... }` (what the menu code uses).
#   3. Re-adds the ~15 non-dumped extras the code references. Wherever possible the
#      extras alias fields that DO exist in the dump (same value); the few that do
#      not (Misc::StringLength, Lighting::GlobalShadows, etc.) use the known stable
#      values that hold across builds.
#   4. Rewrites the in-process mirror `offsets_native.h` with the exact fields the
#      native chams renderer needs.

param(
    [string]$Dump    = "C:\Users\sirrw\Desktop\phantomX\bottega.lol\output\dumper\offsets.h",
    [string]$SdkOut  = "C:\Users\sirrw\Desktop\phantomX\bottega.lol\bottega.lol\src\sdk\offsets.h",
    [string]$NativeOut = "C:\Users\sirrw\Desktop\phantomX\bottega.lol\bottega.lol.internal\src\offsets_native.h"
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Dump)) {
    Write-Error "dump file not found: $Dump"
}

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
    [void]$sb.AppendLine(' * Source: fresh phantomX-Roblox-Dumper output (class-property offsets)')
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
            [void]$sb.AppendLine('        // Non-dumped extras consumed by menu/native code.')
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

function Write-NativeHeader($data, $version, $outPath) {
    $sb = New-Object System.Text.StringBuilder
    [void]$sb.AppendLine('// Auto-generated by output\merge_offsets.ps1 - do not edit by hand.')
    [void]$sb.AppendLine("// Roblox Version: $version")
    [void]$sb.AppendLine('// In-process offsets mirroring src/sdk/offsets.h (subset the native chams renderer needs).')
    [void]$sb.AppendLine('#pragma once')
    [void]$sb.AppendLine('#include <cstdint>')
    [void]$sb.AppendLine('')
    [void]$sb.AppendLine('namespace nofs {')

    [void]$sb.AppendLine('namespace VisualEngine {')
    [void]$sb.AppendLine("    inline constexpr uintptr_t Pointer    = $(Get-Val $data 'VisualEngine::Pointer' '0x846F768');")
    [void]$sb.AppendLine("    inline constexpr uintptr_t ViewMatrix = $(Get-Val $data 'VisualEngine::ViewMatrix' '0x1B0');")
    [void]$sb.AppendLine('}')
    [void]$sb.AppendLine('namespace FakeDataModel {')
    [void]$sb.AppendLine("    inline constexpr uintptr_t Pointer       = $(Get-Val $data 'FakeDataModel::Pointer' '0x8E42C98');")
    [void]$sb.AppendLine("    inline constexpr uintptr_t RealDataModel = $(Get-Val $data 'FakeDataModel::RealDataModel' '0x1F8');")
    [void]$sb.AppendLine('}')
    [void]$sb.AppendLine('namespace Players {')
    [void]$sb.AppendLine("    inline constexpr uintptr_t LocalPlayer = $(Get-Val $data 'Players::LocalPlayer' '0x130');")
    [void]$sb.AppendLine('}')
    [void]$sb.AppendLine('namespace BasePart {')
    [void]$sb.AppendLine("    inline constexpr uintptr_t Primitive = $(Get-Val $data 'BasePart::Primitive' '0x188');")
    [void]$sb.AppendLine("    inline constexpr uintptr_t Massless  = $(Get-Val $data 'BasePart::Massless' '0x137');")
    [void]$sb.AppendLine('}')
    [void]$sb.AppendLine('namespace Primitive {')
    [void]$sb.AppendLine("    inline constexpr uintptr_t Frame = $(Get-Val $data 'Primitive::CFrame' '0xB0'); // 48-byte CFrame (rot r[9] + pos)")
    [void]$sb.AppendLine("    inline constexpr uintptr_t Size  = $(Get-Val $data 'Primitive::Size' '0x1C4');")
    [void]$sb.AppendLine('}')

    [void]$sb.AppendLine('} // namespace nofs')
    [System.IO.File]::WriteAllText($outPath, $sb.ToString(), (New-Object System.Text.UTF8Encoding($false)))
}

$parsed  = Read-Dump $Dump
$data    = $parsed[0]
$version = if ($parsed[1]) { $parsed[1] } else { 'unknown' }

$data = Add-Extras $data

Write-SdkHeader   $data $version $SdkOut
Write-NativeHeader $data $version $NativeOut

$nsCount = @($data.Keys | ForEach-Object { $_.Split('::')[0] } | Sort-Object -Unique).Count
Write-Host "[merge_offsets] version: $version"
Write-Host "[merge_offsets] namespaces: $nsCount  offsets: $($data.Count)"
Write-Host "[merge_offsets] sdk   -> $SdkOut"
Write-Host "[merge_offsets] native-> $NativeOut"