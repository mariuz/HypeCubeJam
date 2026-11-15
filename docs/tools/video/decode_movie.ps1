# Decodes every frame of a movie to PNG thumbnails with the Windows (WPF / Media Foundation) decoder - no ffmpeg here.
# Used only as a time reference: the trailer's intro frames are the game's own, matched against these.
#   powershell -STA -File decode_movie.ps1 <movie> <out dir> <fps> <width> <height>
param([string]$Movie, [string]$OutDir, [double]$Fps = 24, [int]$W = 160, [int]$H = 90)
Add-Type -AssemblyName PresentationCore, WindowsBase
function Pump([int]$ms) {
    $frame = New-Object System.Windows.Threading.DispatcherFrame
    $timer = New-Object System.Windows.Threading.DispatcherTimer
    $timer.Interval = [TimeSpan]::FromMilliseconds($ms)
    $timer.add_Tick({ $frame.Continue = $false; $timer.Stop() }.GetNewClosure())
    $timer.Start()
    [System.Windows.Threading.Dispatcher]::PushFrame($frame)
}
$player = New-Object System.Windows.Media.MediaPlayer
$player.ScrubbingEnabled = $true
$player.Volume = 0
$player.Open([Uri]$Movie)
$player.Pause()
$tries = 0
while (-not $player.NaturalDuration.HasTimeSpan -and $tries -lt 100) { Pump 100; $tries++ }
if (-not $player.NaturalDuration.HasTimeSpan) { Write-Output "could not open"; exit 1 }
$dur = $player.NaturalDuration.TimeSpan.TotalSeconds
Write-Output ("opened: {0} x {1}, {2} s" -f $player.NaturalVideoWidth, $player.NaturalVideoHeight, $dur)
$n = [int][Math]::Floor($dur * $Fps)
for ($i = 0; $i -lt $n; $i++) {
    $player.Position = [TimeSpan]::FromSeconds(($i + 0.5) / $Fps)
    Pump 120
    $visual = New-Object System.Windows.Media.DrawingVisual
    $dc = $visual.RenderOpen()
    $dc.DrawVideo($player, (New-Object System.Windows.Rect 0, 0, $W, $H))
    $dc.Close()
    $bmp = New-Object System.Windows.Media.Imaging.RenderTargetBitmap $W, $H, 96, 96, ([System.Windows.Media.PixelFormats]::Pbgra32)
    $bmp.Render($visual)
    $enc = New-Object System.Windows.Media.Imaging.PngBitmapEncoder
    $enc.Frames.Add([System.Windows.Media.Imaging.BitmapFrame]::Create($bmp))
    $fs = [System.IO.File]::Create((Join-Path $OutDir ("ref_{0:D4}.png" -f $i)))
    $enc.Save($fs); $fs.Close()
}
$player.Close()
Write-Output "$n frames"
