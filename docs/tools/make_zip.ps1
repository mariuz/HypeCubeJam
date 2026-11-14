# Stages a packaged build (RunUAT BuildCookRun ... -archivedirectory=Packaged\Temp) and zips it for itch.io:
#   Packaged\Temp\Windows  ->  Packaged\ZipStage\mariuz_HypeCubeJam  ->  Packaged\mariuz_HypeCubeJam.zip
# Leaves out debug symbols, UAT manifests and anything a test run wrote to Saved\; adds README.txt and NOTICES.txt from docs\.
param([string]$Project = "F:\work\HypeCubeJam", [string]$Name = "mariuz_HypeCubeJam")
$ErrorActionPreference = "Stop"
$src   = Join-Path $Project "Packaged\Temp\Windows"
$stage = Join-Path $Project "Packaged\ZipStage\$Name"
$zip   = Join-Path $Project "Packaged\$Name.zip"
$new   = "$zip.new"
if (-not (Test-Path (Join-Path $src "HypeCubeJam.exe"))) { throw "no packaged build in $src" }
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
robocopy $src $stage /E /NFL /NDL /NJH /NJS /NP /XF *.pdb Manifest_*.txt /XD (Join-Path $src "HypeCubeJam\Saved") | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed ($LASTEXITCODE)" }
# robocopy reports success as 1 ("files were copied"), 2 or 3; left alone, that number is what a caller of this script
# sees as its exit code, i.e. a failure although the zip is fine.
$global:LASTEXITCODE = 0
Copy-Item (Join-Path $Project "docs\README.txt") (Join-Path $stage "README.txt") -Force
Copy-Item (Join-Path $Project "docs\NOTICES.txt") (Join-Path $stage "NOTICES.txt") -Force
if (Test-Path $new) { Remove-Item $new -Force }
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
# Entry by entry with forward slashes: ZipFile.CreateFromDirectory of Windows PowerShell 5.1 writes backslashes,
# which tools on other systems do not take for folders.
$base = (Split-Path $stage -Parent).TrimEnd('\') + '\'
$archive = [System.IO.Compression.ZipFile]::Open($new, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($file in (Get-ChildItem $stage -Recurse -File)) {
        $entry = $file.FullName.Substring($base.Length).Replace('\', '/')
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $file.FullName, $entry, [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
} finally { $archive.Dispose() }
Move-Item $new $zip -Force
$files = (Get-ChildItem $stage -Recurse -File)
"{0}: {1:N1} MB zipped, {2:N1} MB unpacked, {3} files" -f $zip, ((Get-Item $zip).Length / 1MB), (($files | Measure-Object Length -Sum).Sum / 1MB), $files.Count
exit 0
