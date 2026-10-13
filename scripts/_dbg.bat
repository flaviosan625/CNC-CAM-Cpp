@echo off
echo dp0=[%~dp0]
echo parent=[%~dp0..]
for %%I in ("%~dp0..") do echo resolved=[%%~fI]
