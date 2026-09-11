@echo off
echo Copying assets to build directories...

echo Copying Models...
xcopy /E /I /Y "Models" "x64\Debug\Models"
xcopy /E /I /Y "Models" "x64\Release\Models"

echo Copying Shaders...
xcopy /E /I /Y "Shaders" "x64\Debug\Shaders"
xcopy /E /I /Y "Shaders" "x64\Release\Shaders"
copy /Y "Shaders\sponza.hlsl" "x64\Debug\sponza.hlsl"
copy /Y "Shaders\sponza.hlsl" "x64\Release\sponza.hlsl"
copy /Y "sponza.hlsl" "x64\Debug\sponza.hlsl" 2>nul
copy /Y "sponza.hlsl" "x64\Release\sponza.hlsl" 2>nul

echo Done! All assets copied to x64\Debug and x64\Release
pause
