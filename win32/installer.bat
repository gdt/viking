@echo OFF
:: License: CC0
::
:: TODO return an error code when not completed as expected
::
echo STARTING INSTALLER PROCESS...

:: For strip
set PATH=%PATH%;%SystemDrive%\MinGW\bin

echo Remove debugging symbols
pushd ..\src
strip.exe -g viking.exe
popd

set MYCOPY=copy /y
set DESTINATION=installer\bin
echo Copying locale files into layout required by NSIS
dir ..\po\*.gmo /B > gmolist.txt
:: Create directories like de\LC_MESSAGES
for /f %%i in (gmolist.txt) do mkdir %DESTINATION%\locale\%%~ni\LC_MESSAGES
for /f %%i in (gmolist.txt) do %MYCOPY% ..\po\%%i %DESTINATION%\locale\%%~ni\LC_MESSAGES\viking.mo
del gmolist.txt

echo Copying Viking
%MYCOPY% ..\src\viking.exe %DESTINATION%
%MYCOPY% installer\pixmaps\viking_icon.ico %DESTINATION%
%MYCOPY% ..\COPYING %DESTINATION%\COPYING_GPL.txt
%MYCOPY% ..\AUTHORS %DESTINATION%\AUTHORS.txt
%MYCOPY% ..\NEWS %DESTINATION%\NEWS.txt
%MYCOPY% ..\README %DESTINATION%\README.txt
:: ATM this relies on being generated by an external system
%MYCOPY% cache\ChangeLog.txt %DESTINATION%
:: ATM this relies on being generated by an external system
%MYCOPY% ..\help\C\viking.pdf %DESTINATION%
:: Python cache converter tool
%MYCOPY% ..\tools\viking-cache.py %DESTINATION%

echo Copying Extension Configuration Data
mkdir %DESTINATION%\data
%MYCOPY% ..\data\*.xml %DESTINATION%\data
%MYCOPY% ..\data\latlontz.txt %DESTINATION%\data

echo Copying Libraries
set MINGW=%SystemDrive%\MinGW
if not exist "%MINGW%" (
	echo Required %MINGW% does not exist
	goto Tidy
)

set MINGW_BIN=%MINGW%\Bin

REM Curl 7.17+ has quite a few dependencies for SSL support
set LIBCURL=%MINGW_BIN%\libcurl.dll
if exist "%LIBCURL%" (
	%MYCOPY% "%LIBCURL%" %DESTINATION%
	%MYCOPY% "%MINGW_BIN%\libeay32.dll" %DESTINATION%
	%MYCOPY% "%MINGW_BIN%\librtmp.dll" %DESTINATION%
	%MYCOPY% "%MINGW_BIN%\libssh2.dll" %DESTINATION%
	%MYCOPY% "%MINGW_BIN%\libidn-11.dll" %DESTINATION%
	%MYCOPY% "%MINGW_BIN%\ssleay32.dll" %DESTINATION%
::	%MYCOPY% "%MINGW_BIN%\zlib1.dll" %DESTINATION%
	%MYCOPY% "%MINGW%\COPYING_curl.txt" %DESTINATION%
) else (
	echo %LIBCURL% does not exist
	goto Tidy
)
set LIBEXIF=%MINGW_BIN%\libexif-12.dll
if exist "%LIBEXIF%" (
	%MYCOPY% "%LIBEXIF%" %DESTINATION%
) else (
	echo Required %LIBEXIF% does not exist
	goto Tidy
)
set LIBBZ2=%MINGW_BIN%\libbz2-2.dll
if exist "%LIBBZ2%" (
	%MYCOPY% "%LIBBZ2%" %DESTINATION%
	%MYCOPY% "%MINGW_BIN%\libgcc_s_dw2-1.dll" %DESTINATION%
) else (
	echo Required %LIBBZ2% does not exist
	goto Tidy
)
set LIBMAGIC=%MINGW_BIN%\magic1.dll
if exist "%LIBMAGIC%" (
	%MYCOPY% "%LIBMAGIC%" %DESTINATION%
	%MYCOPY% "%MINGW_BIN%\regex2.dll" %DESTINATION%
	%MYCOPY% "%MINGW%\share\misc\magic.mgc" %DESTINATION%
) else (
	echo Required %LIBMAGIC% does not exist
	goto Tidy
)
set LIBSQL3=%MINGW_BIN%\sqlite3.dll
if exist "%LIBSQL3%" (
	%MYCOPY% "%LIBSQL3%" %DESTINATION%
) else (
	echo Required %LIBSQL3% does not exist
	goto Tidy
)
set LIBZIP=%MINGW_BIN%\libzip.dll
if exist "%LIBZIP%" (
	%MYCOPY% "%LIBZIP%" %DESTINATION%
) else (
	echo Required %LIBZIP% does not exist
	goto Tidy
)

:: TODO Maybe embed http://gtk-win.sourceforge.net/home/index.php/Main/EmbeddingGTK directly in NSIS?
:: Best to use the same GTK version as we built against in prepare.bat!!
echo =+=+=
echo Checking gtk runtime
echo =+=+=
set GTK_RUNTIME=gtk2-runtime-2.24.10-2012-10-10-ash.exe
pushd cache
if not exist %GTK_RUNTIME% (
	set PATH=%PATH%;%ProgramFiles%\GnuWin32\bin
	wget http://downloads.sourceforge.net/gtk-win/%GTK_RUNTIME%
)
if not exist %GTK_RUNTIME% (
	echo Required GTK Runtime does not exist
	goto Tidy
)
:: Install GTK into temporary location so we can repackage it
:: Destination path appears to have to be an absolute kind
popd
cd > tmp.tmp
set /p PWD=<tmp.tmp
del tmp.tmp
cache\%GTK_RUNTIME% /sideeffects=no /setpath=no /dllpath=root /translations=no /compatdlls=yes /S /D=%PWD%\%DESTINATION%

echo Copying GPSBabel Installer
mkdir %DESTINATION%\Optional
%MYCOPY% cache\GPSBabel-1.5.4-Setup.exe %DESTINATION%\Optional
if ERRORLEVEL 1 goto Error

::
echo Copying Translations
%MYCOPY% installer\translations\*nsh %DESTINATION%
if ERRORLEVEL 1 goto Error

echo Running NSIS (command line version)
pushd installer
if exist "%ProgramFiles%\NSIS" (
	"%ProgramFiles%\NSIS\makensis.exe" /X"SetCompressor /SOLID lzma" viking-installer.nsi
) else (
	echo NSIS Not installed in known location
)
popd

goto Tidy

:Error
echo Exitting due to error: %ERRORLEVEL%

:Tidy
echo Tidy Up
rmdir /S /Q %DESTINATION%
