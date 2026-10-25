# P9 shader preparation release contract. Shared by the packer and audit; no
# game installation, Python, shader bytecode or driver cache is an input.
function Assert-PreparationKeys {
    param($Object, [string[]] $Allowed)
    foreach ($property in $Object.PSObject.Properties) {
        if ($Allowed -notcontains $property.Name) {
            throw "unexpected preparation field: $($property.Name)"
        }
    }
}

function Get-PreparationPayload {
    param([Parameter(Mandatory = $true)][string] $Directory)
    $path = Join-Path $Directory 'graphics-preparation.json'
    $text = [IO.File]::ReadAllText($path).Replace("`r`n", "`n")
    $catalogue = ConvertFrom-Json -InputObject $text
    Assert-PreparationKeys $catalogue @('schema', 'title_id', 'layouts', 'assets', 'sources', 'paths')
    if ($catalogue.schema -ne 1 -or $catalogue.title_id -ne '5841122D') {
        throw 'unsupported preparation catalogue'
    }
    Assert-PreparationKeys $catalogue.layouts @('schema', 'title_id', 'source_revision', 'linker', 'scope',
        'swizzle_masks_offset', 'format_sizes_offset', 'layouts', 'bootstrap_shaders')
    foreach ($layout in $catalogue.layouts.layouts) {
        Assert-PreparationKeys $layout @('name', 'declaration_offset', 'strides', 'fvf')
    }
    foreach ($source in $catalogue.layouts.bootstrap_shaders) {
        Assert-PreparationKeys $source @('name', 'offset', 'bytes', 'hash')
    }
    foreach ($asset in $catalogue.assets.PSObject.Properties) {
        Assert-PreparationKeys $asset.Value @('bytes')
    }
    foreach ($source in $catalogue.sources) {
        Assert-PreparationKeys $source @('asset', 'entry', 'permutation', 'offset', 'bytes',
            'vertex_layout', 'stage', 'hash', 'fetches')
        if ($source.hash -cnotmatch '^[0-9a-f]{16}$' -or
            @('pixel', 'vertex') -notcontains $source.stage -or
            $source.bytes -is [string] -or $source.bytes -le 0 -or $source.bytes -gt 65536) {
            throw 'invalid preparation source locator (instruction bytes must never ship)'
        }
    }
    $counts = [ordered]@{}
    $names = @('d3d12/rtv', 'd3d12/rov', 'vulkan/fbo', 'vulkan/fsi')
    Assert-PreparationKeys $catalogue.paths $names
    foreach ($name in $names) {
        $recipes = @($catalogue.paths.$name)
        if ($recipes.Count -eq 0) { throw "missing preparation path: $name" }
        $length = if ($name.StartsWith('d3d12/')) { 128 } else { 120 }
        foreach ($recipe in $recipes) {
            Assert-PreparationKeys $recipe @('hash', 'raw')
            if ($recipe.hash -cnotmatch '^[0-9a-f]{16}$' -or
                $recipe.raw -cnotmatch "^[0-9a-f]{$length}$") {
                throw "invalid portable pipeline description: $name"
            }
        }
        $counts[$name] = $recipes.Count
    }
    # C++ normalizes source-file line endings inside raw strings. Compare the
    # complete normalized UTF-8 catalogue, not a marker that a stale PE could keep.
    $latin1 = [Text.Encoding]::GetEncoding(28591)
    $bytes = [Text.Encoding]::UTF8.GetBytes($text)
    $executable = $latin1.GetString([IO.File]::ReadAllBytes((Join-Path $Directory 'rb_blitz.exe')))
    if (-not $executable.Contains($latin1.GetString($bytes))) {
        throw 'rb_blitz.exe does not embed graphics-preparation.json; rebuild before packaging'
    }
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $digest = ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
    return [pscustomobject]@{ Schema = 1; Sha256 = $digest; Sources = @($catalogue.sources).Count; Counts = $counts }
}
