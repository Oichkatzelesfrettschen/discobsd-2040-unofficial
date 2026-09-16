<#
.SYNOPSIS
discobsd-flash -- reflash the DiscoBSD RP2040 board with picotool on Windows.

.DESCRIPTION
The running kernel answers picotool's reset request over its Reset
interface (WinUSB) and reboots into BOOTSEL, where the boot ROM offers a
removable drive named RPI-RP2 and the PICOBOOT interface picotool loads
through. Windows keeps that drive open until the board leaves; this
script ejects it through the Shell before every reboot out of BOOTSEL,
so no "device was not ejected" complaint follows, and it waits for the
boot ROM itself, because picotool's own wait after a forced reboot can
be shorter than the enumeration.

    discobsd-flash FILE.uf2 ...    load each image in order, then reboot
    discobsd-flash -Bootsel        into BOOTSEL and stop, drive mounted,
                                   for a copy by hand; -Eject afterwards
    discobsd-flash -Eject          eject the drive, reboot into the kernel

A hung kernel does not answer the reset request: hold BOOTSEL through a
replug, then run this the same way. Run from PowerShell 5.1 or 7; if
scripts are blocked: powershell -ExecutionPolicy Bypass -File discobsd-flash.ps1
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0, ValueFromRemainingArguments = $true)]
    [string[]]$Files,
    [switch]$Bootsel,
    [switch]$Eject
)
$ErrorActionPreference = 'Stop'
$Volume = 'RPI-RP2'

function Fail([string]$Message) {
    [Console]::Error.WriteLine("discobsd-flash: $Message")
    exit 1
}

function Note([string]$Message) {
    [Console]::Error.WriteLine("discobsd-flash: $Message")
}

function In-Bootsel {
    & picotool info *> $null
    return ($LASTEXITCODE -eq 0)
}

function Wait-Bootsel {
    for ($i = 0; $i -lt 40; $i++) {
        if (In-Bootsel) { return }
        Start-Sleep -Milliseconds 500
    }
    Fail 'the board did not reach BOOTSEL; hold BOOTSEL through a replug'
}

function Enter-Bootsel {
    if (In-Bootsel) { return }
    Note 'rebooting the kernel into BOOTSEL'
    & picotool reboot -u -f *> $null
    Wait-Bootsel
}

# The drive letter Windows gave the boot ROM's volume, or nothing.
function Get-MountedAt {
    $d = Get-CimInstance Win32_LogicalDisk -Filter "VolumeName='$Volume'" -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($d) { return $d.DeviceID }
    return $null
}

# The mount follows enumeration by a moment; wait for it so an eject is
# not overtaken by a mount that arrives a second later.
function Wait-Mount {
    for ($i = 0; $i -lt 20; $i++) {
        if (Get-MountedAt) { return }
        Start-Sleep -Milliseconds 500
    }
}

function Eject-Volume {
    Wait-Mount
    $m = Get-MountedAt
    if (-not $m) { return }
    Note "ejecting $m"
    # The Shell's Eject verb is what the notification-area icon runs.
    $shell = New-Object -ComObject Shell.Application
    $item = $shell.NameSpace(17).ParseName("$m\")
    if ($item) { $item.InvokeVerb('Eject') }
    for ($i = 0; $i -lt 20; $i++) {
        if (-not (Get-MountedAt)) { return }
        Start-Sleep -Milliseconds 250
    }
}

function Leave-Bootsel {
    Eject-Volume
    Note 'rebooting into the kernel'
    & picotool reboot *> $null
}

if (-not (Get-Command picotool -ErrorAction SilentlyContinue)) {
    Fail 'picotool is not on PATH (release zip from github.com/raspberrypi/pico-sdk-tools)'
}

if ($Bootsel) {
    Enter-Bootsel
    Wait-Mount
    Note "in BOOTSEL; $Volume is drive $(Get-MountedAt); copy a UF2 there, then discobsd-flash -Eject"
    exit 0
}
if ($Eject) {
    if (-not (In-Bootsel)) { Fail 'the board is not in BOOTSEL' }
    Leave-Bootsel
    exit 0
}
if (-not $Files) {
    Fail 'usage: discobsd-flash FILE.uf2 ... | -Bootsel | -Eject'
}
foreach ($f in $Files) {
    if (-not (Test-Path -LiteralPath $f -PathType Leaf)) { Fail "cannot read $f" }
    if ([IO.Path]::GetExtension($f) -ne '.uf2') { Fail "$f is not a .uf2 file" }
}
Enter-Bootsel
# The drive goes first: picotool loads through PICOBOOT, and a drive still
# mounted during the write is a stale view of the flash anyway.
Eject-Volume
foreach ($f in $Files) {
    Note "loading $f"
    & picotool load $f
    if ($LASTEXITCODE -ne 0) { Fail "picotool load $f failed" }
}
Note 'rebooting into the kernel'
& picotool reboot *> $null
exit 0
