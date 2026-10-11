# Checks every PowerShell script of the project for mistakes PowerShell only reports when the line runs: a parse
# error, or a keyword standing alone as a command - an `else` / `elseif` / `catch` / `finally` cut off from its `if` or
# `try` parses as a call of a program named "else", and fails only on the PCs where that line is reached (1.4.0 and
# 1.4.1 shipped one: setup failed on every real install, while the tests ran setup in test mode and never got there).
# Runs in tools\package.ps1 before anything is zipped. Exit code = number of findings.
#   tools\scriptcheck.ps1 [files or folders...]
param([string[]]$Paths = @())
$root = Split-Path $PSScriptRoot
if (-not $Paths) { $Paths = @("$root\package", "$root\tools", "$root\launcher", "$root\build.ps1") }
$keywords = 'else', 'elseif', 'catch', 'finally', 'until', 'default'
$found = 0
foreach ($f in Get-ChildItem $Paths -Recurse -File -Include *.ps1 -ErrorAction SilentlyContinue) {
    $tokens = $null; $errors = $null
    $ast = [Management.Automation.Language.Parser]::ParseFile($f.FullName, [ref]$tokens, [ref]$errors)
    foreach ($e in $errors) { "$($f.FullName):$($e.Extent.StartLineNumber): parse error: $($e.Message)"; $found++ }
    $cmds = $ast.FindAll({ param($n) $n -is [Management.Automation.Language.CommandAst] }, $true)
    foreach ($c in $cmds) {
        $name = $c.GetCommandName()
        if ($name -and $keywords -contains $name.ToLowerInvariant()) {
            "$($f.FullName):$($c.Extent.StartLineNumber): '$name' runs as a command (cut off from its if/try?): $($c.Extent.Text.Split("`n")[0].Trim())"
            $found++
        }
    }
}
if ($found) { "$found problem(s)" } else { "scripts: clean" }
exit $found
