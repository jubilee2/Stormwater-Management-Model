//-----------------------------------------------------------------------------
//   hotstart.c
//
//   Project:  EPA SWMM5
//   Version:  5.1
//   Date:     03/20/14  (Build 5.1.001)
//             03/28/14  (Build 5.1.002)
//             04/23/14  (Build 5.1.005)
//             03/19/15  (Build 5.1.008)
//             08/01/16  (Build 5.1.011)
//   Author:   L. Rossman (EPA)
//
//   Hot Start file functions.

//   A SWMM hot start file contains the state of a SWMM project after
//   a simulation has been run, allowing it to be used to initialize
//   a subsequent simulation that picks up where the previous run ended.
//
//   An abridged version of the hot start file (version 2) is available 
//   that contains only variables that appear in the binary output file 
//   (groundwater upper moisture and water table elevation, node depth,
//   lateral inflow, and quality, and link flow, depth, setting and quality).
//
//   When reading a previously saved hot start file checks are made to
//   insure the the current SWMM project has the same number of major
//   components (subcatchments, land uses, nodes, links, and pollutants)
//   and unit system as does the hot start file. No test is made to
//   insure that these components are of the same sub-type and maintain
//   the same order as when the hot start file was created.
//
//   Build 5.1.008:
//   - Storage node hydraulic residence time (HRT) was added to the file.
//   - Link control settings are now applied when reading a hot start file.
//   - Runoff read from file assigned to newRunoff property instead of oldRunoff.
//   - Array indexing bug when reading snowpack state from file fixed.
//
//   Build 5.1.011:
//   - Link control setting bug when reading a hot start file fixed.    
//
//-----------------------------------------------------------------------------
#define _CRT_SECURE_NO_DEPRECATE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "headers.h"

//-----------------------------------------------------------------------------
//  External functions (declared in funcs.h)
//-----------------------------------------------------------------------------
// hotstart_open                          (called by swmm_start in swmm5.c)
// hotstart_close                         (called by swmm_end in swmm5.c)      //(5.1.005)

//-----------------------------------------------------------------------------
// Function declarations
//-----------------------------------------------------------------------------
static int  openHotstartFile1(SWMM_Project *sp);
static int  openHotstartFile2(SWMM_Project *sp);
static void readRunoff(SWMM_Project *sp);
static void saveRunoff(SWMM_Project *sp);
static void readRouting(SWMM_Project *sp);
static void saveRouting(SWMM_Project *sp);
static int  readFloat(SWMM_Project *sp, float *x, FILE* f);
static int  readDouble(SWMM_Project *sp, double* x, FILE* f);

//=============================================================================

int hotstart_open(SWMM_Project *sp)
{
    // --- open hot start files
    if ( !openHotstartFile1(sp) ) return FALSE;       //input hot start file
    if ( !openHotstartFile2(sp) ) return FALSE;       //output hot start file

    ////  Following lines removed. ////                                            //(5.1.005)
    //if ( Fhotstart1.file )
    //{
    //    readRunoff();
    //    readRouting();
    //    fclose(Fhotstart1.file);
    //}

    return TRUE;
}

//=============================================================================

void hotstart_close(SWMM_Project *sp)
{
    if ( sp->Fhotstart2.file )
    {
        saveRunoff(sp);
        saveRouting(sp);
        fclose(sp->Fhotstart2.file);
    }
}

//=============================================================================

int openHotstartFile1(SWMM_Project *sp)
//
//  Input:   none
//  Output:  none
//  Purpose: opens a previously saved routing hotstart file.
//
{
    int   nSubcatch;
    int   nNodes;
    int   nLinks;
    int   nPollut;
    int   nLandUses;
    int   flowUnits;
    char  fStamp[]     = "SWMM5-HOTSTART";
    char  fileStamp[]  = "SWMM5-HOTSTART";
    char  fStampx[]    = "SWMM5-HOTSTARTx";
    char  fileStamp2[] = "SWMM5-HOTSTART2";
    char  fileStamp3[] = "SWMM5-HOTSTART3";
    char  fileStamp4[] = "SWMM5-HOTSTART4";                                    //(5.1.008)

    // --- try to open the file
    if ( sp->Fhotstart1.mode != USE_FILE ) return TRUE;
    if ( (sp->Fhotstart1.file = fopen(sp->Fhotstart1.name, "r+b")) == NULL)
    {
        report_writeErrorMsg(sp, ERR_HOTSTART_FILE_OPEN, sp->Fhotstart1.name);
        return FALSE;
    }

    // --- check that file contains proper header records
    fread(fStampx, sizeof(char), strlen(fileStamp2), sp->Fhotstart1.file);
    if      ( strcmp(fStampx, fileStamp4) == 0 ) sp->FileVersion = 4;              //(5.1.008)
    else if ( strcmp(fStampx, fileStamp3) == 0 ) sp->FileVersion = 3;
    else if ( strcmp(fStampx, fileStamp2) == 0 ) sp->FileVersion = 2;
    else
    {
        rewind(sp->Fhotstart1.file);
        fread(fStamp, sizeof(char), strlen(fileStamp), sp->Fhotstart1.file);
        if ( strcmp(fStamp, fileStamp) != 0 )
        {
            report_writeErrorMsg(sp, ERR_HOTSTART_FILE_FORMAT, "");
            return FALSE;
        }
        sp->FileVersion = 1;
    }

    nSubcatch = -1;
    nNodes = -1;
    nLinks = -1;
    nPollut = -1;
    nLandUses = -1;
    flowUnits = -1;
    if ( sp->FileVersion >= 2 )                                                    //(5.1.002)
    {    
        fread(&nSubcatch, sizeof(int), 1, sp->Fhotstart1.file);
    }
    else nSubcatch = sp->Nobjects[SUBCATCH];
    if ( sp->FileVersion >= 3 )                                                    //(5.1.008)
    {
        fread(&nLandUses, sizeof(int), 1, sp->Fhotstart1.file);
    }
    else nLandUses = sp->Nobjects[LANDUSE];
    fread(&nNodes, sizeof(int), 1, sp->Fhotstart1.file);
    fread(&nLinks, sizeof(int), 1, sp->Fhotstart1.file);
    fread(&nPollut, sizeof(int), 1, sp->Fhotstart1.file);
    fread(&flowUnits, sizeof(int), 1, sp->Fhotstart1.file);
    if ( nSubcatch != sp->Nobjects[SUBCATCH]
    ||   nLandUses != sp->Nobjects[LANDUSE]
    ||   nNodes    != sp->Nobjects[NODE]
    ||   nLinks    != sp->Nobjects[LINK]
    ||   nPollut   != sp->Nobjects[POLLUT]
    ||   flowUnits != sp->FlowUnits )
    {
         report_writeErrorMsg(sp, ERR_HOTSTART_FILE_FORMAT, "");
         return FALSE;
    }

    // --- read contents of the file and close it
    if ( sp->FileVersion >= 3 ) readRunoff(sp);                                      //(5.1.008)
    readRouting(sp);
    fclose(sp->Fhotstart1.file);
    if ( sp->ErrorCode ) return FALSE;
    else return TRUE;
}

//=============================================================================

int openHotstartFile2(SWMM_Project *sp)
//
//  Input:   none
//  Output:  none
//  Purpose: opens a new routing hotstart file to save results to.
//
{
    int   nSubcatch;
    int   nLandUses;
    int   nNodes;
    int   nLinks;
    int   nPollut;
    int   flowUnits;
    char  fileStamp[] = "SWMM5-HOTSTART4";                                     //(5.1.008)

    // --- try to open file
    if ( sp->Fhotstart2.mode != SAVE_FILE ) return TRUE;
    if ( (sp->Fhotstart2.file = fopen(sp->Fhotstart2.name, "w+b")) == NULL)
    {
        report_writeErrorMsg(sp, ERR_HOTSTART_FILE_OPEN, sp->Fhotstart2.name);
        return FALSE;
    }

    // --- write file stamp & number of objects to file
    nSubcatch = sp->Nobjects[SUBCATCH];
    nLandUses = sp->Nobjects[LANDUSE];
    nNodes = sp->Nobjects[NODE];
    nLinks = sp->Nobjects[LINK];
    nPollut = sp->Nobjects[POLLUT];
    flowUnits = sp->FlowUnits;
    fwrite(fileStamp, sizeof(char), strlen(fileStamp), sp->Fhotstart2.file);
    fwrite(&nSubcatch, sizeof(int), 1, sp->Fhotstart2.file);
    fwrite(&nLandUses, sizeof(int), 1, sp->Fhotstart2.file);
    fwrite(&nNodes, sizeof(int), 1, sp->Fhotstart2.file);
    fwrite(&nLinks, sizeof(int), 1, sp->Fhotstart2.file);
    fwrite(&nPollut, sizeof(int), 1, sp->Fhotstart2.file);
    fwrite(&flowUnits, sizeof(int), 1, sp->Fhotstart2.file);
    return TRUE;
}

//=============================================================================

void  saveRouting(SWMM_Project *sp)
//
//  Input:   none
//  Output:  none
//  Purpose: saves current state of all nodes and links to hotstart file.
//
{
    int   i, j;
    float x[3];

    for (i = 0; i < sp->Nobjects[NODE]; i++)
    {
        x[0] = (float)sp->Node[i].newDepth;
        x[1] = (float)sp->Node[i].newLatFlow;
        fwrite(x, sizeof(float), 2, sp->Fhotstart2.file);

////  New code added to release 5.1.008.  ////                                 //(5.1.008)
        if ( sp->Node[i].type == STORAGE )
        {
            j = sp->Node[i].subIndex;
            x[0] = (float)sp->Storage[j].hrt;
            fwrite(&x[0], sizeof(float), 1, sp->Fhotstart2.file);
        }
////

        for (j = 0; j < sp->Nobjects[POLLUT]; j++)
        {
            x[0] = (float)sp->Node[i].newQual[j];
            fwrite(&x[0], sizeof(float), 1, sp->Fhotstart2.file);
        }
    }
    for (i = 0; i < sp->Nobjects[LINK]; i++)
    {
        x[0] = (float)sp->Link[i].newFlow;
        x[1] = (float)sp->Link[i].newDepth;
        x[2] = (float)sp->Link[i].setting;
        fwrite(x, sizeof(float), 3, sp->Fhotstart2.file);
        for (j = 0; j < sp->Nobjects[POLLUT]; j++)
        {
            x[0] = (float)sp->Link[i].newQual[j];
            fwrite(&x[0], sizeof(float), 1, sp->Fhotstart2.file);
        }
    }
}

//=============================================================================

void readRouting(SWMM_Project *sp)
//
//  Input:   none 
//  Output:  none
//  Purpose: reads initial state of all nodes, links and groundwater objects
//           from hotstart file.
//
{
    int   i, j;
    float x;
    double xgw[4];
    FILE* f = sp->Fhotstart1.file;

    // --- for file format 2, assign GW moisture content and lower depth
    if ( sp->FileVersion == 2 )
    {
        // --- flow and available upper zone volume not used
        xgw[2] = 0.0;
        xgw[3] = MISSING;
        for (i = 0; i < sp->Nobjects[SUBCATCH]; i++)
        {
            // --- read moisture content and water table elevation as floats
            if ( !readFloat(sp, &x, f) ) return;
            xgw[0] = x;
            if ( !readFloat(sp, &x, f) ) return;
            xgw[1] = x;

            // --- set GW state
            if ( sp->Subcatch[i].groundwater != NULL ) gwater_setState(sp, i, xgw);
        }
    }

    // --- read node states
    for (i = 0; i < sp->Nobjects[NODE]; i++)
    {
        if ( !readFloat(sp, &x, f) ) return;
        sp->Node[i].newDepth = x;
        if ( !readFloat(sp, &x, f) ) return;
        sp->Node[i].newLatFlow = x;

////  New code added to release 5.1.008.  ////                                 //(5.1.008)
        if ( sp->FileVersion >= 4 &&  sp->Node[i].type == STORAGE )
        {
            if ( !readFloat(sp, &x, f) ) return;
            j = sp->Node[i].subIndex;
            sp->Storage[j].hrt = x;
        }
////

        for (j = 0; j < sp->Nobjects[POLLUT]; j++)
        {
            if ( !readFloat(sp, &x, f) ) return;
            sp->Node[i].newQual[j] = x;
        }

        // --- read in zeros here for backwards compatibility
        if ( sp->FileVersion <= 2 )
        {
            for (j = 0; j < sp->Nobjects[POLLUT]; j++)
            {
                if ( !readFloat(sp, &x, f) ) return;
            }
        }
    }

    // --- read link states
    for (i = 0; i < sp->Nobjects[LINK]; i++)
    {
        if ( !readFloat(sp, &x, f) ) return;
        sp->Link[i].newFlow = x;
        if ( !readFloat(sp, &x, f) ) return;
        sp->Link[i].newDepth = x;
        if ( !readFloat(sp, &x, f) ) return;
        sp->Link[i].setting = x;

////  Following code section moved to here.  ////                              //(5.1.011)
        // --- set link's target setting to saved setting 
        sp->Link[i].targetSetting = x;
        link_setTargetSetting(sp, i);
        link_setSetting(sp, i, 0.0);
////
        for (j = 0; j < sp->Nobjects[POLLUT]; j++)
        {
            if ( !readFloat(sp, &x, f) ) return;
            sp->Link[i].newQual[j] = x;
        }

    }
}

//=============================================================================

void  saveRunoff(SWMM_Project *sp)
//
//  Input:   none
//  Output:  none
//  Purpose: saves current state of all subcatchments to hotstart file.
//
{
    int   i, j, k, sizeX;
    double* x;
    FILE*  f = sp->Fhotstart2.file;

    sizeX = MAX(6, sp->Nobjects[POLLUT]+1);
    x = (double *) calloc(sizeX, sizeof(double));

    for (i = 0; i < sp->Nobjects[SUBCATCH]; i++)
    {
        // Ponded depths for each sub-area & total runoff (4 elements)
        for (j = 0; j < 3; j++) x[j] = sp->Subcatch[i].subArea[j].depth;
        x[3] = sp->Subcatch[i].newRunoff;
        fwrite(x, sizeof(double), 4, f);

        // Infiltration state (max. of 6 elements)
        for (j=0; j<sizeX; j++) x[j] = 0.0;
        infil_getState(sp, i, sp->InfilModel, x);
        fwrite(x, sizeof(double), 6, f);

        // Groundwater state (4 elements)
        if ( sp->Subcatch[i].groundwater != NULL )
        {
            gwater_getState(sp, i, x);
            fwrite(x, sizeof(double), 4, f);
        }

        // Snowpack state (5 elements for each of 3 snow surfaces)
        if ( sp->Subcatch[i].snowpack != NULL )
        {
            for (j=0; j<3; j++)
            {
                snow_getState(sp, i, j, x);
                fwrite(x, sizeof(double), 5, f);
            }
        }

        // Water quality
        if ( sp->Nobjects[POLLUT] > 0 )                                            //(5.1.008)
        {
            // Runoff quality
            for (j=0; j<sp->Nobjects[POLLUT]; j++) x[j] = sp->Subcatch[i].newQual[j];
            fwrite(x, sizeof(double), sp->Nobjects[POLLUT], f);

            // Ponded quality
            for (j=0; j<sp->Nobjects[POLLUT]; j++) x[j] = sp->Subcatch[i].pondedQual[j];
            fwrite(x, sizeof(double), sp->Nobjects[POLLUT], f);
            
            // Buildup and when streets were last swept
            for (k=0; k<sp->Nobjects[LANDUSE]; k++)
            {
                for (j=0; j<sp->Nobjects[POLLUT]; j++)
                    x[j] = sp->Subcatch[i].landFactor[k].buildup[j];
                fwrite(x, sizeof(double), sp->Nobjects[POLLUT], f);
                x[0] = sp->Subcatch[i].landFactor[k].lastSwept;
                fwrite(x, sizeof(double), 1, f);
            }
        }
    }
    free(x);
}

//=============================================================================

void  readRunoff(SWMM_Project *sp)
//
//  Input:   none
//  Output:  none
//  Purpose: reads saved state of all subcatchments from a hot start file.
//
{
    int    i, j, k;
    double x[6];
    FILE*  f = sp->Fhotstart1.file;

    for (i = 0; i < sp->Nobjects[SUBCATCH]; i++)
    {
        // Ponded depths & runoff (4 elements)
        for (j = 0; j < 3; j++)
        {
            if ( !readDouble(sp, &sp->Subcatch[i].subArea[j].depth, f) ) return;
        }
        if ( !readDouble(sp, &sp->Subcatch[i].newRunoff, f) ) return;                  //(5.1.008)

        // Infiltration state (max. of 6 elements)
        for (j=0; j<6; j++) if ( !readDouble(sp, &x[j], f) ) return;
        infil_setState(sp, i, sp->InfilModel, x);

        // Groundwater state (4 elements)
        if ( sp->Subcatch[i].groundwater != NULL )
        {
            for (j=0; j<4; j++) if ( !readDouble(sp, &x[j], f) ) return;
            gwater_setState(sp, i, x);
        }

        // Snowpack state (5 elements for each of 3 snow surfaces)
        if ( sp->Subcatch[i].snowpack != NULL )
        {
            for (j=0; j<3; j++) 
            {
                for (k=0; k<5; k++) if ( !readDouble(sp, &x[k], f) ) return;       //(5.1.008)
                snow_setState(sp, i, j, x);
            }
        }

        // Water quality
        if ( sp->Nobjects[POLLUT] > 0 )                                            //(5.1.008)
        {
            // Runoff quality
            for (j=0; j<sp->Nobjects[POLLUT]; j++)
                if ( ! readDouble(sp, &sp->Subcatch[i].newQual[j], f) ) return;        //(5.1.008)

            // Ponded quality
            for (j=0; j<sp->Nobjects[POLLUT]; j++)
                if ( !readDouble(sp, &sp->Subcatch[i].pondedQual[j], f) ) return;
            
            // Buildup and when streets were last swept
            for (k=0; k<sp->Nobjects[LANDUSE]; k++)
            {
                for (j=0; j<sp->Nobjects[POLLUT]; j++)
                {
                    if ( !readDouble(sp,
                        &sp->Subcatch[i].landFactor[k].buildup[j], f) ) return;
                }
                if ( !readDouble(sp, &sp->Subcatch[i].landFactor[k].lastSwept, f) )
                    return;
            }
        }
    }
}

//=============================================================================

int  readFloat(SWMM_Project *sp, float *x, FILE* f)
//
//  Input:   none
//  Output:  x  = pointer to a float variable
//  Purpose: reads a floating point value from a hot start file
//
{
    // --- read a value from the file
    fread(x, sizeof(float), 1, f);

    // --- test if the value is NaN (not a number)
    if ( *(x) != *(x) )
    {
        report_writeErrorMsg(sp, ERR_HOTSTART_FILE_READ, "");
        *(x) = 0.0;
        return FALSE;
    }
    return TRUE;
}

//=============================================================================

int  readDouble(SWMM_Project *sp, double* x, FILE* f)
//
//  Input:   none
//  Output:  x  = pointer to a double variable
//  Purpose: reads a floating point value from a hot start file
//
{
    // --- read a value from the file
    if ( feof(f) )
    {    
        *(x) = 0.0;
        report_writeErrorMsg(sp, ERR_HOTSTART_FILE_READ, "");
        return FALSE;
    }
    fread(x, sizeof(double), 1, f);

    // --- test if the value is NaN (not a number)
    if ( *(x) != *(x) )
    {
        *(x) = 0.0;
        return FALSE;
    }
    return TRUE;
}