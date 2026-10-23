# Dice un texto con una voz de Windows (incluidas las modernas, como "Microsoft Pablo")
# y lo guarda en un .wav.  Uso: voz_windows.ps1 -Voz "Microsoft Pablo" -Texto "..." -Salida x.wav
param([string]$Voz, [string]$Texto, [string]$Salida)

Add-Type -AssemblyName System.Runtime.WindowsRuntime
$null = [Windows.Media.SpeechSynthesis.SpeechSynthesizer, Windows.Media.SpeechSynthesis, ContentType = WindowsRuntime]
$null = [Windows.Storage.Streams.DataReader, Windows.Storage.Streams, ContentType = WindowsRuntime]

$asTask = [System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
    $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1'
} | Select-Object -First 1
function Esperar($operacion, [Type]$tipo) {
    $tarea = $asTask.MakeGenericMethod($tipo).Invoke($null, @($operacion))
    $tarea.Wait() | Out-Null
    $tarea.Result
}

$s = New-Object Windows.Media.SpeechSynthesis.SpeechSynthesizer
$s.Voice = [Windows.Media.SpeechSynthesis.SpeechSynthesizer]::AllVoices | Where-Object DisplayName -eq $Voz
$flujo = Esperar ($s.SynthesizeTextToStreamAsync($Texto)) ([Windows.Media.SpeechSynthesis.SpeechSynthesisStream])
$lector = New-Object Windows.Storage.Streams.DataReader($flujo.GetInputStreamAt(0))
$null = Esperar ($lector.LoadAsync([uint32]$flujo.Size)) ([uint32])
$bytes = New-Object byte[] $flujo.Size
$lector.ReadBytes($bytes)
[IO.File]::WriteAllBytes($Salida, $bytes)
