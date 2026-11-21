# Copyright (c) 2026 edupode
# SPDX-License-Identifier: CPAL-1.0
#
# The contents of this file are subject to the Common Public Attribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at https://opensource.org/license/CPAL-1.0. The License is based on the Mozilla Public License Version 1.1 but Sections 14 and 15 have been added to cover use of software over a computer network and provide for limited attribution for the Original Developer. In addition, Exhibit A has been modified to be consistent with Exhibit B.
# Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
# The Original Code is AnyXboxSeries.
# The Original Developer is the Initial Developer.
# The Initial Developer of the Original Code is edupode. All portions of the code written by edupode are Copyright (c) 2026 edupode. All Rights Reserved.
#

param(
    [string]$OutputPath = (Join-Path (Split-Path -Parent $PSScriptRoot) 'WSX-Resource-Launcher.exe')
)

$ErrorActionPreference = 'Stop'
$compiler = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if (-not (Test-Path -LiteralPath $compiler -PathType Leaf)) {
    throw 'The .NET Framework x64 compiler is required to build the viewer.'
}

$references = @('System.dll', 'System.Core.dll', 'System.Drawing.dll', 'System.Windows.Forms.dll', 'System.Xml.dll', 'System.Xml.Linq.dll')
$compilerDirectory = Split-Path -Parent $compiler
$compilerArguments = @('/nologo', '/noconfig', '/target:winexe', '/platform:x64', '/optimize+', '/codepage:65001', '/warnaserror+')
foreach ($reference in $references) {
    $compilerArguments += '/reference:' + (Join-Path $compilerDirectory $reference)
}
$compilerArguments += '/out:' + $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputPath)
$compilerArguments += (Join-Path $PSScriptRoot 'Launcher.cs')
$compilerArguments += (Join-Path $PSScriptRoot 'PackageReader.cs')

& $compiler @compilerArguments
if ($LASTEXITCODE -ne 0) {
    throw "Compilation failed with exit code $LASTEXITCODE."
}
