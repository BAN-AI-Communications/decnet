@ECHO OFF
@REM
@REM vcmake.bat - Make library of functions for reading and writing VAX format
@REM              data for Windows Win32 using Microsoft Visual C (CL).
@REM
@REM Command Prompt command syntax: vcmake [ all | libvaxdata | clean ]
@REM
@REM
@REM Author:      Lawrence M. Baker
@REM              U.S. Geological Survey
@REM              345 Middlefield Road  MS977
@REM              Menlo Park, CA  94025
@REM              baker@usgs.gov
@REM
@REM Citation:    Baker, Lawrence M., 2005, libvaxdata: VAX Data Format Conver-
@REM                 sion Routines, US Geological Survey, Open-File Report no.
@REM                 2005-XXX, nn p.
@REM
@REM
@REM                                 Disclaimer
@REM
@REM Although  this program has been used by the USGS, no warranty, expressed or
@REM implied, is made by the USGS or the United  States  Government  as  to  the
@REM accuracy  and  functioning of the program and related program material, nor
@REM shall the fact  of  distribution  constitute  any  such  warranty,  and  no
@REM responsibility is assumed by the USGS in connection therewith.
@REM
@REM
@REM Modification History:
@REM
@REM  2-Sep-2005  L. M. Baker      Original version (from LibVFBB.bat).
@REM

@SET LIB_NAME=LibVAXData

@IF /I "%1" == "clean" GOTO :CLEAN

@REM /nologo (suppress display of sign-on banner) /ML (single threaded using
@REM MSVCRTD.LIB) /W3 (warning level) /GX (enable synchronous exception
@REM handling) /O2 (create fast code) /Za (disable extensions, i.e., ANSI C)
@SET CC=CL
@SET CFLAGS=/nologo /ML /W3 /GX /O2 /Za

@ECHO ON
@REM
@REM VAX Data Conversion Routines (C linkage)
@REM
%CC% /c %CFLAGS% ..\src\from_vax_i2.c
%CC% /c %CFLAGS% ..\src\from_vax_i4.c
%CC% /c %CFLAGS% ..\src\from_vax_r4.c
%CC% /c %CFLAGS% ..\src\from_vax_d8.c
%CC% /c %CFLAGS% ..\src\from_vax_g8.c
%CC% /c %CFLAGS% ..\src\from_vax_h16.c
%CC% /c %CFLAGS% ..\src\to_vax_i2.c
%CC% /c %CFLAGS% ..\src\to_vax_i4.c
%CC% /c %CFLAGS% ..\src\to_vax_r4.c
%CC% /c %CFLAGS% ..\src\to_vax_d8.c
%CC% /c %CFLAGS% ..\src\to_vax_g8.c
%CC% /c %CFLAGS% ..\src\to_vax_h16.c
%CC% /c %CFLAGS% ..\src\is_little_endian.c
@REM
@REM Create a static library
@REM
LIB /nologo /out:%LIB_NAME%.lib ^
    from_vax_i2.obj   from_vax_i4.obj   from_vax_r4.obj   from_vax_d8.obj   ^
    from_vax_g8.obj   from_vax_h16.obj  to_vax_i2.obj     to_vax_i4.obj     ^
    to_vax_r4.obj     to_vax_d8.obj     to_vax_g8.obj     to_vax_h16.obj    ^
    is_little_endian.obj
DEL from_vax_i2.obj   from_vax_i4.obj   from_vax_r4.obj   from_vax_d8.obj   ^
    from_vax_g8.obj   from_vax_h16.obj  to_vax_i2.obj     to_vax_i4.obj     ^
    to_vax_r4.obj     to_vax_d8.obj     to_vax_g8.obj     to_vax_h16.obj    ^
    is_little_endian.obj
@ECHO OFF

@SET CFLAGS=%CFLAGS% /DUPCASE /DFORTRAN_LINKAGE=__stdcall

@ECHO ON
@REM
@REM VAX Data Conversion Routines (Fortran linkage)
@REM
%CC% /c %CFLAGS% /FoFROM_VAX_I2@n.obj      ..\src\from_vax_i2.c
%CC% /c %CFLAGS% /FoFROM_VAX_I4@n.obj      ..\src\from_vax_i4.c
%CC% /c %CFLAGS% /FoFROM_VAX_R4@n.obj      ..\src\from_vax_r4.c
%CC% /c %CFLAGS% /FoFROM_VAX_D8@n.obj      ..\src\from_vax_d8.c
%CC% /c %CFLAGS% /FoFROM_VAX_G8@n.obj      ..\src\from_vax_g8.c
%CC% /c %CFLAGS% /FoFROM_VAX_H16@n.obj     ..\src\from_vax_h16.c
%CC% /c %CFLAGS% /FoTO_VAX_I2@n.obj        ..\src\to_vax_i2.c
%CC% /c %CFLAGS% /FoTO_VAX_I4@n.obj        ..\src\to_vax_i4.c
%CC% /c %CFLAGS% /FoTO_VAX_R4@n.obj        ..\src\to_vax_r4.c
%CC% /c %CFLAGS% /FoTO_VAX_D8@n.obj        ..\src\to_vax_d8.c
%CC% /c %CFLAGS% /FoTO_VAX_G8@n.obj        ..\src\to_vax_g8.c
%CC% /c %CFLAGS% /FoTO_VAX_H16@n.obj       ..\src\to_vax_h16.c
%CC% /c %CFLAGS% /FoIS_LITTLE_ENDIAN@n.obj ..\src\is_little_endian.c
LIB /nologo %LIB_NAME%.lib ^
    FROM_VAX_I2@n.obj   FROM_VAX_I4@n.obj   FROM_VAX_R4@n.obj   ^
    FROM_VAX_D8@n.obj   FROM_VAX_G8@n.obj   FROM_VAX_H16@n.obj  ^
    TO_VAX_I2@n.obj     TO_VAX_I4@n.obj     TO_VAX_R4@n.obj     ^
    TO_VAX_D8@n.obj     TO_VAX_G8@n.obj     TO_VAX_H16@n.obj    ^
    IS_LITTLE_ENDIAN@n.obj
DEL FROM_VAX_I2@n.obj   FROM_VAX_I4@n.obj   FROM_VAX_R4@n.obj   ^
    FROM_VAX_D8@n.obj   FROM_VAX_G8@n.obj   FROM_VAX_H16@n.obj  ^
    TO_VAX_I2@n.obj     TO_VAX_I4@n.obj     TO_VAX_R4@n.obj     ^
    TO_VAX_D8@n.obj     TO_VAX_G8@n.obj     TO_VAX_H16@n.obj    ^
    IS_LITTLE_ENDIAN@n.obj
@GOTO :EOF

:CLEAN
@ECHO ON
DEL %LIB_NAME%.lib
DEL from_vax_i2.obj   from_vax_i4.obj   from_vax_r4.obj   from_vax_d8.obj   ^
    from_vax_g8.obj   from_vax_h16.obj  to_vax_i2.obj     to_vax_i4.obj     ^
    to_vax_r4.obj     to_vax_d8.obj     to_vax_g8.obj     to_vax_h16.obj    ^
    is_little_endian.obj
DEL FROM_VAX_I2@n.obj   FROM_VAX_I4@n.obj   FROM_VAX_R4@n.obj   ^
    FROM_VAX_D8@n.obj   FROM_VAX_G8@n.obj   FROM_VAX_H16@n.obj  ^
    TO_VAX_I2@n.obj     TO_VAX_I4@n.obj     TO_VAX_R4@n.obj     ^
    TO_VAX_D8@n.obj     TO_VAX_G8@n.obj     TO_VAX_H16@n.obj    ^
    IS_LITTLE_ENDIAN@n.obj
