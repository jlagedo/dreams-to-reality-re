# Shared local path lookup for PowerShell tools. Process environment wins.
$script:DreamsLocalEnvFile = Join-Path $PSScriptRoot '..\.dreams.local.env'
$script:DreamsLocalSettings = $null

function Get-DreamsSetting {
    param([Parameter(Mandatory)][string]$Name)

    $value = [Environment]::GetEnvironmentVariable($Name, 'Process')
    if (-not [string]::IsNullOrWhiteSpace($value)) { return $value.Trim() }

    if ($null -eq $script:DreamsLocalSettings) {
        $script:DreamsLocalSettings = @{}
        if (Test-Path -LiteralPath $script:DreamsLocalEnvFile) {
            $lineNumber = 0
            foreach ($raw in Get-Content -LiteralPath $script:DreamsLocalEnvFile -Encoding utf8) {
                $lineNumber++
                $line = $raw.Trim()
                if (-not $line -or $line.StartsWith('#')) { continue }
                $equals = $line.IndexOf('=')
                if ($equals -lt 1) {
                    throw ('{0}:{1}: expected NAME=VALUE' -f $script:DreamsLocalEnvFile, $lineNumber)
                }
                $key = $line.Substring(0, $equals).Trim()
                $entry = $line.Substring($equals + 1).Trim()
                if (-not $key) {
                    throw ('{0}:{1}: environment name is empty' -f $script:DreamsLocalEnvFile, $lineNumber)
                }
                if ($entry.Length -ge 2 -and $entry[0] -eq $entry[$entry.Length - 1] -and
                    ($entry[0] -eq "'" -or $entry[0] -eq '"')) {
                    $entry = $entry.Substring(1, $entry.Length - 2)
                }
                $script:DreamsLocalSettings[$key] = $entry
            }
        }
    }
    $value = $script:DreamsLocalSettings[$Name]
    if (-not [string]::IsNullOrWhiteSpace($value)) { return $value.Trim() }
    return $null
}
