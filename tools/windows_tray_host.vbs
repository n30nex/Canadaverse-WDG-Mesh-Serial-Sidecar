Option Explicit

If WScript.Arguments.Count <> 1 Then WScript.Quit 2

Dim shell, powerShellPath, scriptPath, command, exitCode
Set shell = CreateObject("WScript.Shell")
powerShellPath = shell.ExpandEnvironmentStrings("%SystemRoot%") & _
    "\System32\WindowsPowerShell\v1.0\powershell.exe"
scriptPath = WScript.Arguments(0)
command = Quote(powerShellPath) & _
    " -NoProfile -STA -ExecutionPolicy Bypass -WindowStyle Hidden -File " & _
    Quote(scriptPath)
exitCode = shell.Run(command, 0, True)
WScript.Quit exitCode

Function Quote(value)
    Quote = Chr(34) & Replace(value, Chr(34), Chr(34) & Chr(34)) & Chr(34)
End Function
