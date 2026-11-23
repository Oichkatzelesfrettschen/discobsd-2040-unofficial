<#
.SYNOPSIS
discobsd-flash.ps1 is the former name of gizmobsd-flash.ps1. For one release
it prints one line on stderr and runs the new script with the same arguments.
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0, ValueFromRemainingArguments = $true)]
    [string[]]$Files,
    [switch]$Bootsel,
    [switch]$Eject
)
[Console]::Error.WriteLine('discobsd-flash.ps1 is renamed gizmobsd-flash.ps1; use the new name')
& (Join-Path $PSScriptRoot 'gizmobsd-flash.ps1') @PSBoundParameters
exit $LASTEXITCODE
