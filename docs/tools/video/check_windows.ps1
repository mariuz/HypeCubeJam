# Asks Windows about an AVI without showing anything: the shell's property handler (length, frame size, frame rate,
# audio) and the WPF MediaPlayer (does a decoder open it, natural duration, video size; a frame is rendered to a PNG).
#   powershell -STA -File check_windows.ps1 <file.avi> [<frame.png> [<seconds>]]
param([string]$File, [string]$Png = '', [double]$At = 30)
$shell = New-Object -ComObject Shell.Application
$folder = $shell.Namespace((Split-Path $File))
$item = $folder.ParseName((Split-Path $File -Leaf))
foreach ($name in 'System.Media.Duration', 'System.Video.FrameWidth', 'System.Video.FrameHeight', 'System.Video.FrameRate',
                  'System.Video.TotalBitrate', 'System.Audio.SampleRate', 'System.Audio.ChannelCount', 'System.Audio.EncodingBitrate') {
    $v = $item.ExtendedProperty($name)
    if ($name -eq 'System.Media.Duration' -and $v) { $v = '{0:N2} s' -f ($v / 1e7) }
    Write-Output ("shell {0,-30} {1}" -f $name, $v)
}
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
$script:failed = $null
$player.add_MediaFailed({ param($s, $e) $script:failed = $e.ErrorException.Message })
$player.ScrubbingEnabled = $true
$player.Volume = 0
$player.Open([Uri]$File)
$player.Pause()
$tries = 0
while (-not $player.NaturalDuration.HasTimeSpan -and -not $script:failed -and $tries -lt 150) { Pump 100; $tries++ }
if ($script:failed -or -not $player.NaturalDuration.HasTimeSpan) { Write-Output "MediaPlayer: could not open ($script:failed)"; exit 1 }
Write-Output ("MediaPlayer: opened, {0} x {1}, {2:N2} s, has video {3}, has audio {4}" -f $player.NaturalVideoWidth, $player.NaturalVideoHeight,
    $player.NaturalDuration.TimeSpan.TotalSeconds, $player.HasVideo, $player.HasAudio)
if ($Png) {
    $player.Position = [TimeSpan]::FromSeconds($At)
    Pump 800
    $w = $player.NaturalVideoWidth; $h = $player.NaturalVideoHeight
    $visual = New-Object System.Windows.Media.DrawingVisual
    $dc = $visual.RenderOpen(); $dc.DrawVideo($player, (New-Object System.Windows.Rect 0, 0, $w, $h)); $dc.Close()
    $bmp = New-Object System.Windows.Media.Imaging.RenderTargetBitmap $w, $h, 96, 96, ([System.Windows.Media.PixelFormats]::Pbgra32)
    $bmp.Render($visual)
    $enc = New-Object System.Windows.Media.Imaging.PngBitmapEncoder
    $enc.Frames.Add([System.Windows.Media.Imaging.BitmapFrame]::Create($bmp))
    $fs = [System.IO.File]::Create($Png); $enc.Save($fs); $fs.Close()
    Write-Output "MediaPlayer: frame at $At s rendered to $Png"
}
$player.Close()
