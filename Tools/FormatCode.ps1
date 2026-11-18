<#
.SYNOPSIS
    Formats C/C++ sources via Tools/format_code.py.
#>

$ErrorActionPreference = "Stop"
$Py = Join-Path $PSScriptRoot "format_code.py"
& python $Py @args
exit $LASTEXITCODE
