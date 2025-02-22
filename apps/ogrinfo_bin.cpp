/******************************************************************************
 *
 * Project:  OpenGIS Simple Features Reference Implementation
 * Purpose:  Simple client for viewing OGR driver data.
 * Author:   Frank Warmerdam, warmerdam@pobox.com
 *
 ******************************************************************************
 * Copyright (c) 1999, Frank Warmerdam
 * Copyright (c) 2008-2013, Even Rouault <even dot rouault at spatialys.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 ****************************************************************************/

#include "cpl_port.h"

#include "gdal_version.h"
#include "gdal_priv.h"
#include "gdal_utils_priv.h"
#include "ogr_p.h"

#include "commonutils.h"

/************************************************************************/
/*                               Usage()                                */
/************************************************************************/

static void Usage(bool bIsError, const char *pszErrorMsg = nullptr)
{
    fprintf(bIsError ? stderr : stdout,
            "Usage: ogrinfo [--help] [--help-general]\n"
            "               [-json] [-ro] [-q] [-where "
            "<restricted_where>|@f<ilename>]\n"
            "               [-spat <xmin> <ymin> <xmax> <ymax>] [-geomfield "
            "<field>] "
            "[-fid <fid>]\n"
            "               [-sql <statement>|@<filename>] [-dialect "
            "<sql_dialect>] "
            "[-al] [-rl]\n"
            "               [-so|-features] [-fields={YES|NO}]]\n"
            "               [-geom={YES|NO|SUMMARY|WKT|ISO_WKT}] "
            "[-oo <NAME>=<VALUE>]...\n"
            "               [-nomd] [-listmdd] [-mdd {<domain>|all}]...\n"
            "               [-nocount] [-nogeomtype] "
            "[[-noextent] | [-extent3D]]\n"
            "               [-wkt_format WKT1|WKT2|<other_values>]\n"
            "               [-fielddomain <name>]\n"
            "               <datasource_name> [<layer> [<layer> ...]]\n");

    if (pszErrorMsg != nullptr)
        fprintf(stderr, "\nFAILURE: %s\n", pszErrorMsg);

    exit(bIsError ? 1 : 0);
}

/************************************************************************/
/*                                main()                                */
/************************************************************************/

MAIN_START(argc, argv)

{
    // Check strict compilation and runtime library version as we use C++ API.
    if (!GDAL_CHECK_VERSION(argv[0]))
        exit(1);

    EarlySetConfigOptions(argc, argv);

    OGRRegisterAll();

    argc = OGRGeneralCmdLineProcessor(argc, &argv, 0);
    if (argc < 1)
        exit(-argc);

    for (int i = 0; argv != nullptr && argv[i] != nullptr; i++)
    {
        if (EQUAL(argv[i], "--utility_version"))
        {
            printf("%s was compiled against GDAL %s and is running against "
                   "GDAL %s\n",
                   argv[0], GDAL_RELEASE_NAME, GDALVersionInfo("RELEASE_NAME"));
            CSLDestroy(argv);
            return 0;
        }
        else if (EQUAL(argv[i], "--help"))
        {
            Usage(false);
        }
    }
    argv = CSLAddString(argv, "-stdout");

    auto psOptionsForBinary =
        std::make_unique<GDALVectorInfoOptionsForBinary>();

    GDALVectorInfoOptions *psOptions =
        GDALVectorInfoOptionsNew(argv + 1, psOptionsForBinary.get());
    if (psOptions == nullptr)
        Usage(true);

    if (psOptionsForBinary->osFilename.empty())
        Usage(true, "No datasource specified.");

/* -------------------------------------------------------------------- */
/*      Open dataset.                                                   */
/* -------------------------------------------------------------------- */
#ifdef __AFL_HAVE_MANUAL_CONTROL
    int iIter = 0;
    while (__AFL_LOOP(1000))
    {
        iIter++;
#endif
        /* --------------------------------------------------------------------
         */
        /*      Open data source. */
        /* --------------------------------------------------------------------
         */
        int nFlags = GDAL_OF_VECTOR;
        bool bMayRetryUpdateMode = false;
        if (psOptionsForBinary->bUpdate)
            nFlags |= GDAL_OF_UPDATE | GDAL_OF_VERBOSE_ERROR;
        else if (psOptionsForBinary->bReadOnly)
            nFlags |= GDAL_OF_READONLY | GDAL_OF_VERBOSE_ERROR;
        else if (psOptionsForBinary->osSQLStatement.empty())
        {
            nFlags |= GDAL_OF_READONLY;
            if (GDALIdentifyDriverEx(psOptionsForBinary->osFilename.c_str(),
                                     GDAL_OF_VECTOR, nullptr, nullptr))
            {
                bMayRetryUpdateMode = true;
            }
            else
            {
                // And an error Will be emitted
                nFlags |= GDAL_OF_VERBOSE_ERROR;
            }
        }
        else
            nFlags |= GDAL_OF_UPDATE | GDAL_OF_VERBOSE_ERROR;
        GDALDataset *poDS = GDALDataset::Open(
            psOptionsForBinary->osFilename.c_str(), nFlags, nullptr,
            psOptionsForBinary->aosOpenOptions.List(), nullptr);

        if (poDS == nullptr && !psOptionsForBinary->bReadOnly &&
            !psOptionsForBinary->bUpdate)
        {
            if (psOptionsForBinary->osSQLStatement.empty() &&
                bMayRetryUpdateMode)
            {
                // In some cases (empty geopackage for example), opening in
                // read-only mode fails, so retry in update mode
                poDS = GDALDataset::Open(
                    psOptionsForBinary->osFilename.c_str(),
                    GDAL_OF_UPDATE | GDAL_OF_VECTOR, nullptr,
                    psOptionsForBinary->aosOpenOptions.List(), nullptr);
            }
            else if (!psOptionsForBinary->osSQLStatement.empty())
            {
                poDS = GDALDataset::Open(
                    psOptionsForBinary->osFilename.c_str(),
                    GDAL_OF_READONLY | GDAL_OF_VECTOR, nullptr,
                    psOptionsForBinary->aosOpenOptions.List(), nullptr);
                if (poDS != nullptr && psOptionsForBinary->bVerbose)
                {
                    printf("Had to open data source read-only.\n");
#ifdef __AFL_HAVE_MANUAL_CONTROL
                    psOptionsForBinary->bReadOnly = true;
#endif
                }
            }
        }

        int nRet = 0;
        if (poDS == nullptr)
        {
            nRet = 1;
            fprintf(stderr, "ogrinfo failed - unable to open '%s'.\n",
                    psOptionsForBinary->osFilename.c_str());
        }
        else
        {
            char *pszGDALVectorInfoOutput =
                GDALVectorInfo(GDALDataset::ToHandle(poDS), psOptions);

            if (pszGDALVectorInfoOutput)
                printf("%s", pszGDALVectorInfoOutput);
            else
                nRet = 1;

            CPLFree(pszGDALVectorInfoOutput);
        }

        delete poDS;
#ifdef __AFL_HAVE_MANUAL_CONTROL
    }
#endif

    GDALVectorInfoOptionsFree(psOptions);

    CSLDestroy(argv);

    GDALDumpOpenDatasets(stderr);

    GDALDestroyDriverManager();

    CPLDumpSharedList(nullptr);
    GDALDestroy();

    exit(nRet);
}
MAIN_END
