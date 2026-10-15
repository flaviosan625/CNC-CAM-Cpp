param(
    [Parameter(Mandatory = $true)][string]$Before,
    [Parameter(Mandatory = $true)][string]$After,
    [string]$Output = ''
)

$ErrorActionPreference = 'Stop'
$taskCulture = [System.Globalization.CultureInfo]::InvariantCulture

function Read-Measurements([string]$Path) {
    $taskRecords = @{}
    foreach ($taskLine in [System.IO.File]::ReadAllLines((Resolve-Path -LiteralPath $Path).Path)) {
        if (!$taskLine.StartsWith('CNC_PERF ')) { continue }
        $taskRecord = $taskLine.Substring(9) | ConvertFrom-Json
        $taskRecords[[int]$taskRecord.copies] = $taskRecord
    }
    if ($taskRecords.Count -eq 0) { throw "Nenhuma medicao CNC_PERF em: $Path" }
    return $taskRecords
}

$taskBefore = Read-Measurements $Before
$taskAfter = Read-Measurements $After
$taskStages = [ordered]@{
    'load' = 'Abrir arquivo'
    'duplicate' = 'Duplicar desenhos'
    'first_refresh' = 'Primeiro desenho'
    'unchanged_refresh' = 'Atualizar sem edicao'
    'selection_refresh' = 'Selecionar'
    'single_piece_edit' = 'Editar uma peca'
    'cursor_snap' = 'Snap do cursor'
    'moving_snap' = 'Snap ao mover'
    'pick' = 'Localizar vetor'
    'render_whole_sheet' = 'Pintar chapa inteira'
    'zoom_repaint' = 'Alterar zoom e pintar'
    'interactive_drag_8_frames' = 'Arrastar no viewport'
}
$taskReport = [System.Collections.Generic.List[string]]::new()
$taskReport.Add('# Comparacao de desempenho')
$taskReport.Add('')
$taskReport.Add('Tempos por operacao em milissegundos. A aceleracao divide o tempo anterior pelo posterior.')
$taskReport.Add('')
$taskReport.Add('| Copias | Entidades | Etapa | Antes (ms) | Depois (ms) | Aceleracao |')
$taskReport.Add('| ---: | ---: | --- | ---: | ---: | ---: |')
foreach ($taskCopies in ($taskBefore.Keys | Sort-Object)) {
    if (!$taskAfter.ContainsKey($taskCopies)) { throw "Falta carga de $taskCopies copias no arquivo posterior" }
    $taskOld = $taskBefore[$taskCopies]
    $taskNew = $taskAfter[$taskCopies]
    foreach ($taskField in @('entities', 'polyline_points', 'texts', 'iterations')) {
        if ($taskOld.$taskField -ne $taskNew.$taskField) {
            throw "Carga diferente para $taskCopies copias: $taskField"
        }
    }
    if (($taskOld.viewport -join ',') -ne ($taskNew.viewport -join ',')) { throw 'Viewports diferentes' }
    if ($taskOld.snap_hits -ne $taskNew.snap_hits -or $taskOld.moving_snap_hits -ne $taskNew.moving_snap_hits) {
        throw "Resultados de snap diferentes para $taskCopies copias"
    }
    foreach ($taskStage in $taskStages.Keys) {
        $taskOldStage = $taskOld.stages.$taskStage
        $taskNewStage = $taskNew.stages.$taskStage
        if (!$taskOldStage -or !$taskNewStage) { throw "Etapa ausente: $taskStage" }
        if ($taskOldStage.operations -ne $taskNewStage.operations) { throw "Contagem diferente: $taskStage" }
        $taskOldMs = [double]$taskOldStage.ms_per_operation
        $taskNewMs = [double]$taskNewStage.ms_per_operation
        $taskAcceleration = if ($taskNewMs -gt 0) { ($taskOldMs / $taskNewMs).ToString('F2', $taskCulture) + 'x' } else { '-' }
        $taskReport.Add('| ' + $taskCopies + ' | ' + $taskOld.entities + ' | ' + $taskStages[$taskStage] + ' | ' +
            $taskOldMs.ToString('F3', $taskCulture) + ' | ' + $taskNewMs.ToString('F3', $taskCulture) + ' | ' +
            $taskAcceleration + ' |')
    }
}
$taskText = $taskReport -join [Environment]::NewLine
if ($Output) { [System.IO.File]::WriteAllText([System.IO.Path]::GetFullPath($Output), $taskText + [Environment]::NewLine) }
$taskText
