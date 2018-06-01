REM HUZZA FOR: https://dmerej.info/blog/post/cmake-visual-studio-and-the-command-line/
if not exist .vs md .vs
cd .vs
if exist CMakeFiles rd /s /q CMakeFiles
if exist CMakeCache.txt del CMakeCache.txt
call "C:\Program Files (x86)\Microsoft Visual Studio\2017\Enterprise\Common7\Tools\VsDevCmd.bat"
cmake -DWIN32=on -DCMAKE_BUILD_TYPE=Debug %1 -G"NMake Makefiles" ..
nmake