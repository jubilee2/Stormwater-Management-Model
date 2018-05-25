//-----------------------------------------------------------------------------
//   output.c
//
//   Project:  EPA SWMM5
//   Version:  5.1
//   Date:     03/20/14  (Build 5.1.001)
//             03/19/15  (Build 5.1.008)
//             08/05/15  (Build 5.1.010)
//   Author:   L. Rossman (EPA)
//
//   Binary output file access functions.
//
//   Build 5.1.008:
//   - Possible divide by zero for reported system wide variables avoided.
//   - Updating of maximum node depth at reporting times added.
//
//   Build 5.1.010:
//   - Potentional ET added to list of system-wide variables saved to file.
//
//-----------------------------------------------------------------------------
#define _CRT_SECURE_NO_DEPRECATE

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "headers.h"
#include "output.h"


enum InputDataType {INPUT_TYPE_CODE, INPUT_AREA, INPUT_INVERT, INPUT_MAX_DEPTH,
                    INPUT_OFFSET, INPUT_LENGTH};

//-----------------------------------------------------------------------------
//  Local functions
//-----------------------------------------------------------------------------
static void output_openOutFile(SWMM_Project *sp);
static void output_saveID(char* id, FILE* file);
static void output_saveSubcatchResults(SWMM_Project *sp, double reportTime, FILE* file);
static void output_saveNodeResults(SWMM_Project *sp, double reportTime, FILE* file);
static void output_saveLinkResults(SWMM_Project *sp, double reportTime, FILE* file);

//-----------------------------------------------------------------------------
//  External functions (declared in funcs.h)
//-----------------------------------------------------------------------------
//  output_open                   (called by swmm_start in swmm5.c)
//  output_end                    (called by swmm_end in swmm5.c)
//  output_close                  (called by swmm_close in swmm5.c)
//  output_saveResults            (called by swmm_step in swmm5.c)
//  output_checkFileSize          (called by swmm_report)
//  output_readDateTime           (called by routines in report.c)
//  output_readSubcatchResults    (called by report_Subcatchments)
//  output_readNodeResults        (called by report_Nodes)
//  output_readLinkResults        (called by report_Links)


//=============================================================================

int output_open(SWMM_Project *sp)
//
//  Input:   none
//  Output:  returns an error code
//  Purpose: writes basic project data to binary output file.
//
{
    int   j;
    int   m;
    INT4  k;
    REAL4 x;
    REAL8 z;

    TOutputShared *otpt = &sp->OutputShared;
    TOutputExport *otptx = &sp->OutputExport;

    // --- open binary output file
    output_openOutFile(sp);
    if ( sp->ErrorCode ) return sp->ErrorCode;

    // --- ignore pollutants if no water quality analsis performed
    if ( sp->IgnoreQuality ) otpt->NumPolluts = 0;
    else otpt->NumPolluts = sp->Nobjects[POLLUT];

    // --- subcatchment results consist of Rainfall, Snowdepth, Evap, 
    //     Infil, Runoff, GW Flow, GW Elev, GW Sat, and Washoff
    otpt->NsubcatchResults = MAX_SUBCATCH_RESULTS - 1 + otpt->NumPolluts;

    // --- node results consist of Depth, Head, Volume, Lateral Inflow,
    //     Total Inflow, Overflow and Quality
    otpt->NnodeResults = MAX_NODE_RESULTS - 1 + otpt->NumPolluts;

    // --- link results consist of Depth, Flow, Velocity, Froude No.,
    //     Capacity and Quality
    otpt->NlinkResults = MAX_LINK_RESULTS - 1 + otpt->NumPolluts;

    // --- get number of objects reported on
    otpt->NumSubcatch = 0;
    otpt->NumNodes = 0;
    otpt->NumLinks = 0;

    for (j=0; j<sp->Nobjects[SUBCATCH]; j++)
        if (sp->Subcatch[j].rptFlag)
            otpt->NumSubcatch++;

    for (j=0; j<sp->Nobjects[NODE]; j++)
        if (sp->Node[j].rptFlag)
            otpt->NumNodes++;

    for (j=0; j<sp->Nobjects[LINK]; j++)
        if (sp->Link[j].rptFlag)
            otpt->NumLinks++;

    otpt->BytesPerPeriod = sizeof(REAL8)
        + otpt->NumSubcatch * otpt->NsubcatchResults * sizeof(REAL4)
        + otpt->NumNodes * otpt->NnodeResults * sizeof(REAL4)
        + otpt->NumLinks * otpt->NlinkResults * sizeof(REAL4)
        + MAX_SYS_RESULTS * sizeof(REAL4);
    sp->Nperiods = 0;

    otptx->SubcatchResults = NULL;
    otptx->NodeResults = NULL;
    otptx->LinkResults = NULL;
    otptx->SubcatchResults = (REAL4 *) calloc(otpt->NsubcatchResults, sizeof(REAL4));
    otptx->NodeResults = (REAL4 *) calloc(otpt->NnodeResults, sizeof(REAL4));
    otptx->LinkResults = (REAL4 *) calloc(otpt->NlinkResults, sizeof(REAL4));
    if ( !otptx->SubcatchResults || !otptx->NodeResults || !otptx->LinkResults )
    {
        report_writeErrorMsg(sp, ERR_MEMORY, "");
        return sp->ErrorCode;
    }

    fseek(sp->Fout.file, 0, SEEK_SET);
    k = MAGICNUMBER;
    fwrite(&k, sizeof(INT4), 1, sp->Fout.file);   // Magic number
    k = VERSION;
    fwrite(&k, sizeof(INT4), 1, sp->Fout.file);   // Version number
    k = sp->FlowUnits;
    fwrite(&k, sizeof(INT4), 1, sp->Fout.file);   // Flow units
    k = otpt->NumSubcatch;
    fwrite(&k, sizeof(INT4), 1, sp->Fout.file);   // # subcatchments
    k = otpt->NumNodes;
    fwrite(&k, sizeof(INT4), 1, sp->Fout.file);   // # nodes
    k = otpt->NumLinks;
    fwrite(&k, sizeof(INT4), 1, sp->Fout.file);   // # links
    k = otpt->NumPolluts;
    fwrite(&k, sizeof(INT4), 1, sp->Fout.file);   // # pollutants

    // --- save ID names of subcatchments, nodes, links, & pollutants 
    otpt->IDStartPos = ftell(sp->Fout.file);
    for (j=0; j<sp->Nobjects[SUBCATCH]; j++)
    {
        if ( sp->Subcatch[j].rptFlag )
            output_saveID(sp->Subcatch[j].ID, sp->Fout.file);
    }

    for (j=0; j<sp->Nobjects[NODE];     j++)
    {
        if ( sp->Node[j].rptFlag )
            output_saveID(sp->Node[j].ID, sp->Fout.file);
    }

    for (j=0; j<sp->Nobjects[LINK];     j++)
    {
        if ( sp->Link[j].rptFlag )
            output_saveID(sp->Link[j].ID, sp->Fout.file);
    }

    for (j=0; j < otpt->NumPolluts; j++)
        output_saveID(sp->Pollut[j].ID, sp->Fout.file);

    // --- save codes of pollutant concentration units
    for (j=0; j < otpt->NumPolluts; j++)
    {
        k = sp->Pollut[j].units;
        fwrite(&k, sizeof(INT4), 1, sp->Fout.file);
    }

    otpt->InputStartPos = ftell(sp->Fout.file);

    // --- save subcatchment area
    k = 1;
    fwrite(&k, sizeof(INT4), 1, sp->Fout.file);
    k = INPUT_AREA;
    fwrite(&k, sizeof(INT4), 1, sp->Fout.file);
    for (j=0; j<sp->Nobjects[SUBCATCH]; j++)
    {
         if ( !sp->Subcatch[j].rptFlag ) continue;
         otptx->SubcatchResults[0] = (REAL4)(sp->Subcatch[j].area * UCF(sp, LANDAREA));
         fwrite(&otptx->SubcatchResults[0], sizeof(REAL4), 1, sp->Fout.file);
    }

    // --- save node type, invert, & max. depth
    k = 3;
    fwrite(&k, sizeof(INT4), 1, sp->Fout.file);
    k = INPUT_TYPE_CODE;
    fwrite(&k, sizeof(INT4), 1, sp->Fout.file);
    k = INPUT_INVERT;
    fwrite(&k, sizeof(INT4), 1, sp->Fout.file);
    k = INPUT_MAX_DEPTH;
    fwrite(&k, sizeof(INT4), 1, sp->Fout.file);
    for (j=0; j<sp->Nobjects[NODE]; j++)
    {
        if ( !sp->Node[j].rptFlag ) continue;
        k = sp->Node[j].type;
        otptx->NodeResults[0] = (REAL4)(sp->Node[j].invertElev * UCF(sp, LENGTH));
        otptx->NodeResults[1] = (REAL4)(sp->Node[j].fullDepth * UCF(sp, LENGTH));
        fwrite(&k, sizeof(INT4), 1, sp->Fout.file);
        fwrite(otptx->NodeResults, sizeof(REAL4), 2, sp->Fout.file);
    }

    // --- save link type, offsets, max. depth, & length
    k = 5;
    fwrite(&k, sizeof(INT4), 1, sp->Fout.file);
    k = INPUT_TYPE_CODE;
    fwrite(&k, sizeof(INT4), 1, sp->Fout.file);
    k = INPUT_OFFSET;
    fwrite(&k, sizeof(INT4), 1, sp->Fout.file);
    k = INPUT_OFFSET;
    fwrite(&k, sizeof(INT4), 1, sp->Fout.file);
    k = INPUT_MAX_DEPTH;
    fwrite(&k, sizeof(INT4), 1, sp->Fout.file);
    k = INPUT_LENGTH;
    fwrite(&k, sizeof(INT4), 1, sp->Fout.file);

    for (j=0; j<sp->Nobjects[LINK]; j++)
    {
        if ( !sp->Link[j].rptFlag ) continue;
        k = sp->Link[j].type;
        if ( k == PUMP )
        {
            for (m=0; m<4; m++) otptx->LinkResults[m] = 0.0f;
        }
        else
        {
            otptx->LinkResults[0] = (REAL4)(sp->Link[j].offset1 * UCF(sp, LENGTH));
            otptx->LinkResults[1] = (REAL4)(sp->Link[j].offset2 * UCF(sp, LENGTH));
            if ( sp->Link[j].direction < 0 )
            {
                x = otptx->LinkResults[0];
                otptx->LinkResults[0] = otptx->LinkResults[1];
                otptx->LinkResults[1] = x;
            }
            if ( k == OUTLET ) otptx->LinkResults[2] = 0.0f;
            else otptx->LinkResults[2] = (REAL4)(sp->Link[j].xsect.yFull * UCF(sp, LENGTH));
            if ( k == CONDUIT )
            {
                m = sp->Link[j].subIndex;
                otptx->LinkResults[3] = (REAL4)(sp->Conduit[m].length * UCF(sp, LENGTH));
            }
            else otptx->LinkResults[3] = 0.0f;
        }
        fwrite(&k, sizeof(INT4), 1, sp->Fout.file);
        fwrite(otptx->LinkResults, sizeof(REAL4), 4, sp->Fout.file);
    }

    // --- save number & codes of subcatchment result variables
    k = otpt->NsubcatchResults;
    fwrite(&k, sizeof(INT4), 1, sp->Fout.file);
    k = SUBCATCH_RAINFALL;
    fwrite(&k, sizeof(INT4), 1, sp->Fout.file);
    k = SUBCATCH_SNOWDEPTH;
    fwrite(&k, sizeof(INT4), 1, sp->Fout.file);
    k = SUBCATCH_EVAP;
    fwrite(&k, sizeof(INT4), 1, sp->Fout.file);
    k = SUBCATCH_INFIL;
    fwrite(&k, sizeof(INT4), 1, sp->Fout.file);
    k = SUBCATCH_RUNOFF;
    fwrite(&k, sizeof(INT4), 1, sp->Fout.file);
    k = SUBCATCH_GW_FLOW;
    fwrite(&k, sizeof(INT4), 1, sp->Fout.file);
    k = SUBCATCH_GW_ELEV;
    fwrite(&k, sizeof(INT4), 1, sp->Fout.file);
    k = SUBCATCH_SOIL_MOIST;
    fwrite(&k, sizeof(INT4), 1, sp->Fout.file);

    for (j = 0; j < otpt->NumPolluts; j++)
    {
        k = SUBCATCH_WASHOFF + j;
        fwrite(&k, sizeof(INT4), 1, sp->Fout.file);
    }

    // --- save number & codes of node result variables
    k = otpt->NnodeResults;
    fwrite(&k, sizeof(INT4), 1, sp->Fout.file);
    k = NODE_DEPTH;
    fwrite(&k, sizeof(INT4), 1, sp->Fout.file);
    k = NODE_HEAD;
    fwrite(&k, sizeof(INT4), 1, sp->Fout.file);
    k = NODE_VOLUME;
    fwrite(&k, sizeof(INT4), 1, sp->Fout.file);
    k = NODE_LATFLOW;
    fwrite(&k, sizeof(INT4), 1, sp->Fout.file);
    k = NODE_INFLOW;
    fwrite(&k, sizeof(INT4), 1, sp->Fout.file);
    k = NODE_OVERFLOW;
    fwrite(&k, sizeof(INT4), 1, sp->Fout.file);
    for (j = 0; j < otpt->NumPolluts; j++)
    {
        k = NODE_QUAL + j;
        fwrite(&k, sizeof(INT4), 1, sp->Fout.file);
    }

    // --- save number & codes of link result variables
    k = otpt->NlinkResults;
    fwrite(&k, sizeof(INT4), 1, sp->Fout.file);
    k = LINK_FLOW;
    fwrite(&k, sizeof(INT4), 1, sp->Fout.file);
    k = LINK_DEPTH;
    fwrite(&k, sizeof(INT4), 1, sp->Fout.file);
    k = LINK_VELOCITY;
    fwrite(&k, sizeof(INT4), 1, sp->Fout.file);
    k = LINK_VOLUME;
    fwrite(&k, sizeof(INT4), 1, sp->Fout.file);
    k = LINK_CAPACITY;
    fwrite(&k, sizeof(INT4), 1, sp->Fout.file);
    for (j = 0; j < otpt->NumPolluts; j++)
    {
        k = LINK_QUAL + j;
        fwrite(&k, sizeof(INT4), 1, sp->Fout.file);
    }

    // --- save number & codes of system result variables
    k = MAX_SYS_RESULTS;
    fwrite(&k, sizeof(INT4), 1, sp->Fout.file);
    for (k=0; k<MAX_SYS_RESULTS; k++) fwrite(&k, sizeof(INT4), 1, sp->Fout.file);

    // --- save starting report date & report step
    //     (if reporting start date > simulation start date then
    //      make saved starting report date one reporting period
    //      prior to the date of the first reported result)
    z = (double)sp->ReportStep/86400.0;
    if ( sp->StartDateTime + z > sp->ReportStart ) z = sp->StartDateTime;
    else
    {
        z = floor((sp->ReportStart - sp->StartDateTime)/z) - 1.0;
        z = sp->StartDateTime + z*(double)sp->ReportStep/86400.0;
    }
    fwrite(&z, sizeof(REAL8), 1, sp->Fout.file);
    k = sp->ReportStep;
    if ( fwrite(&k, sizeof(INT4), 1, sp->Fout.file) < 1)
    {
        report_writeErrorMsg(sp, ERR_OUT_WRITE, "");
        return sp->ErrorCode;
    }
    otpt->OutputStartPos = ftell(sp->Fout.file);
    if ( sp->Fout.mode == SCRATCH_FILE ) output_checkFileSize(sp);
    return sp->ErrorCode;
}

//=============================================================================

void  output_checkFileSize(SWMM_Project *sp)
//
//  Input:   none
//  Output:  none
//  Purpose: checks if the size of the binary output file will be too big
//           to access using an integer file pointer variable.
//
{
    TOutputShared *otpt = &sp->OutputShared;

    if ( sp->RptFlags.subcatchments != NONE ||
         sp->RptFlags.nodes != NONE ||
         sp->RptFlags.links != NONE )
    {
        if ( (double)otpt->OutputStartPos + (double)otpt->BytesPerPeriod *
                sp->TotalDuration/ 1000.0 / (double)sp->ReportStep
                >= (double)MAXFILESIZE )
        {
            report_writeErrorMsg(sp, ERR_FILE_SIZE, "");
        }
    }
}


//=============================================================================

void output_openOutFile(SWMM_Project *sp)
//
//  Input:   none
//  Output:  none
//  Purpose: opens a project's binary output file.
//
{

    // --- close output file if already opened
    if (sp->Fout.file != NULL) fclose(sp->Fout.file);

    // --- else if file name supplied then set file mode to SAVE
    else if (strlen(sp->Fout.name) != 0) sp->Fout.mode = SAVE_FILE;

    // --- otherwise set file mode to SCRATCH & generate a name
    else
    {
        sp->Fout.mode = SCRATCH_FILE;
        getTempFileName(sp, sp->Fout.name);
    }

    // --- try to open the file
    if ( (sp->Fout.file = fopen(sp->Fout.name, "w+b")) == NULL)
    {
        writecon(FMT14);
        sp->ErrorCode = ERR_OUT_FILE;
    }
}

//=============================================================================

void output_saveResults(SWMM_Project *sp, double reportTime)
//
//  Input:   reportTime = elapsed simulation time (millisec)
//  Output:  none
//  Purpose: writes computed results for current report time to binary file.
//
{
    int i;
    REAL8 date;

    DateTime reportDate = getDateTime(sp, reportTime);

    TOutputShared *otpt = &sp->OutputShared;

    if ( reportDate < sp->ReportStart ) return;
    for (i=0; i<MAX_SYS_RESULTS; i++) otpt->SysResults[i] = 0.0f;
    date = reportDate;
    fwrite(&date, sizeof(REAL8), 1, sp->Fout.file);
    if (sp->Nobjects[SUBCATCH] > 0)
        output_saveSubcatchResults(sp, reportTime, sp->Fout.file);
    if (sp->Nobjects[NODE] > 0)
        output_saveNodeResults(sp, reportTime, sp->Fout.file);
    if (sp->Nobjects[LINK] > 0)
        output_saveLinkResults(sp, reportTime, sp->Fout.file);
    fwrite(otpt->SysResults, sizeof(REAL4), MAX_SYS_RESULTS, sp->Fout.file);
    if ( sp->Foutflows.mode == SAVE_FILE && !sp->IgnoreRouting )
        iface_saveOutletResults(sp, reportDate, sp->Foutflows.file);
    sp->Nperiods++;
}

//=============================================================================

void output_end(SWMM_Project *sp)
//
//  Input:   none
//  Output:  none
//  Purpose: writes closing records to binary file.
//
{
    INT4 k;

    TOutputShared *otpt = &sp->OutputShared;

    fwrite(&otpt->IDStartPos, sizeof(INT4), 1, sp->Fout.file);
    fwrite(&otpt->InputStartPos, sizeof(INT4), 1, sp->Fout.file);
    fwrite(&otpt->OutputStartPos, sizeof(INT4), 1, sp->Fout.file);
    k = sp->Nperiods;
    fwrite(&k, sizeof(INT4), 1, sp->Fout.file);
    k = (INT4)error_getCode(sp->ErrorCode);
    fwrite(&k, sizeof(INT4), 1, sp->Fout.file);
    k = MAGICNUMBER;
    if (fwrite(&k, sizeof(INT4), 1, sp->Fout.file) < 1)
    {
        report_writeErrorMsg(sp, ERR_OUT_WRITE, "");
    }
}

//=============================================================================

void output_close(SWMM_Project *sp)
//
//  Input:   none
//  Output:  none
//  Purpose: frees memory used for accessing the binary file.
//
{
    TOutputExport *otptx = &sp->OutputExport;

    FREE(otptx->SubcatchResults);
    FREE(otptx->NodeResults);
    FREE(otptx->LinkResults);
}

//=============================================================================

void output_saveID(char* id, FILE* file)
//
//  Input:   id = name of an object
//           file = ptr. to binary output file
//  Output:  none
//  Purpose: writes an object's name to the binary output file.
//
{
    INT4 n = strlen(id);
    fwrite(&n, sizeof(INT4), 1, file);
    fwrite(id, sizeof(char), n, file);
}

//=============================================================================

void output_saveSubcatchResults(SWMM_Project *sp, double reportTime, FILE* file)
//
//  Input:   reportTime = elapsed simulation time (millisec)
//           file = ptr. to binary output file
//  Output:  none
//  Purpose: writes computed subcatchment results to binary file.
//
{
    int      j;
    double   f;
    double   area;
    REAL4    totalArea = 0.0f; 
    DateTime reportDate = getDateTime(sp, reportTime);

    TOutputShared *otpt = &sp->OutputShared;
    TOutputExport *otptx = &sp->OutputExport;

    // --- update reported rainfall at each rain gage
    for ( j=0; j<sp->Nobjects[GAGE]; j++ )
    {
        gage_setReportRainfall(sp, j, reportDate);
    }

    // --- find where current reporting time lies between latest runoff times
    f = (reportTime - sp->OldRunoffTime) / (sp->NewRunoffTime - sp->OldRunoffTime);

    // --- write subcatchment results to file
    for ( j=0; j<sp->Nobjects[SUBCATCH]; j++)
    {
        // --- retrieve interpolated results for reporting time & write to file
        subcatch_getResults(sp, j, f, otptx->SubcatchResults);
        if ( sp->Subcatch[j].rptFlag )
            fwrite(otptx->SubcatchResults, sizeof(REAL4), otpt->NsubcatchResults, file);

        // --- update system-wide results
        area = sp->Subcatch[j].area * UCF(sp, LANDAREA);
        totalArea += (REAL4)area;
        otpt->SysResults[SYS_RAINFALL] +=
            (REAL4)(otptx->SubcatchResults[SUBCATCH_RAINFALL] * area);
        otpt->SysResults[SYS_SNOWDEPTH] +=
            (REAL4)(otptx->SubcatchResults[SUBCATCH_SNOWDEPTH] * area);
        otpt->SysResults[SYS_EVAP] +=
            (REAL4)(otptx->SubcatchResults[SUBCATCH_EVAP] * area);
        if ( sp->Subcatch[j].groundwater ) otpt->SysResults[SYS_EVAP] +=
            (REAL4)(sp->Subcatch[j].groundwater->evapLoss * UCF(sp, EVAPRATE) * area);
        otpt->SysResults[SYS_INFIL] +=
            (REAL4)(otptx->SubcatchResults[SUBCATCH_INFIL] * area);
        otpt->SysResults[SYS_RUNOFF] += (REAL4)otptx->SubcatchResults[SUBCATCH_RUNOFF];
    }

    // --- normalize system-wide results to catchment area
    if ( sp->UnitSystem == SI ) f = (5./9.) * (sp->Temp.ta - 32.0);
    else f = sp->Temp.ta;
    otpt->SysResults[SYS_TEMPERATURE] = (REAL4)f;
    
    f = sp->Evap.rate * UCF(sp, EVAPRATE);                                             //(5.1.010)
    otpt->SysResults[SYS_PET] = (REAL4)f;                                            //(5.1.010)

    if ( totalArea > 0.0 )                                                     //(5.1.008)
    {
        otpt->SysResults[SYS_EVAP]      /= totalArea;
        otpt->SysResults[SYS_RAINFALL]  /= totalArea;
        otpt->SysResults[SYS_SNOWDEPTH] /= totalArea;
        otpt->SysResults[SYS_INFIL]     /= totalArea;
    }
}

//=============================================================================

void output_saveNodeResults(SWMM_Project *sp, double reportTime, FILE* file)
//
//  Input:   reportTime = elapsed simulation time (millisec)
//           file = ptr. to binary output file
//  Output:  none
//  Purpose: writes computed node results to binary file.
//
{
    int j;

    TMassbalShared *mssbl = &sp->MassbalShared;
    TOutputShared *otpt = &sp->OutputShared;
    TOutputExport *otptx = &sp->OutputExport;

    // --- find where current reporting time lies between latest routing times
    double f = (reportTime - sp->OldRoutingTime) /
               (sp->NewRoutingTime - sp->OldRoutingTime);

    // --- write node results to file
    for (j=0; j<sp->Nobjects[NODE]; j++)
    {
        // --- retrieve interpolated results for reporting time & write to file
        node_getResults(sp, j, f, otptx->NodeResults);
        if ( sp->Node[j].rptFlag )
            fwrite(otptx->NodeResults, sizeof(REAL4), otpt->NnodeResults, file);
        stats_updateMaxNodeDepth(sp, j, otptx->NodeResults[NODE_DEPTH]);                 //(5.1.008)

        // --- update system-wide storage volume 
        otpt->SysResults[SYS_STORAGE] += otptx->NodeResults[NODE_VOLUME];
    }

    // --- update system-wide flows 
    otpt->SysResults[SYS_FLOODING] = (REAL4) (mssbl->StepFlowTotals.flooding * UCF(sp, FLOW));
    otpt->SysResults[SYS_OUTFLOW]  = (REAL4) (mssbl->StepFlowTotals.outflow * UCF(sp, FLOW));
    otpt->SysResults[SYS_DWFLOW] = (REAL4)(mssbl->StepFlowTotals.dwInflow * UCF(sp, FLOW));
    otpt->SysResults[SYS_GWFLOW] = (REAL4)(mssbl->StepFlowTotals.gwInflow * UCF(sp, FLOW));
    otpt->SysResults[SYS_IIFLOW] = (REAL4)(mssbl->StepFlowTotals.iiInflow * UCF(sp, FLOW));
    otpt->SysResults[SYS_EXFLOW] = (REAL4)(mssbl->StepFlowTotals.exInflow * UCF(sp, FLOW));
    otpt->SysResults[SYS_INFLOW] = otpt->SysResults[SYS_RUNOFF] +
                             otpt->SysResults[SYS_DWFLOW] +
                             otpt->SysResults[SYS_GWFLOW] +
                             otpt->SysResults[SYS_IIFLOW] +
                             otpt->SysResults[SYS_EXFLOW];
}

//=============================================================================

void output_saveLinkResults(SWMM_Project *sp, double reportTime, FILE* file)
//
//  Input:   reportTime = elapsed simulation time (millisec)
//           file = ptr. to binary output file
//  Output:  none
//  Purpose: writes computed link results to binary file.
//
{
    int j;
    double f;
    double z;

    TOutputShared *otpt = &sp->OutputShared;
    TOutputExport *otptx = &sp->OutputExport;

    // --- find where current reporting time lies between latest routing times
    f = (reportTime - sp->OldRoutingTime) / (sp->NewRoutingTime - sp->OldRoutingTime);

    // --- write link results to file
    for (j=0; j<sp->Nobjects[LINK]; j++)
    {
        // --- retrieve interpolated results for reporting time & write to file
        link_getResults(sp, j, f, otptx->LinkResults);
        if ( sp->Link[j].rptFlag ) 
            fwrite(otptx->LinkResults, sizeof(REAL4), otpt->NlinkResults, file);

        // --- update system-wide results
        z = ((1.0-f)*sp->Link[j].oldVolume + f*sp->Link[j].newVolume) * UCF(sp, VOLUME);
        otpt->SysResults[SYS_STORAGE] += (REAL4)z;
    }
}

//=============================================================================

void output_readDateTime(SWMM_Project *sp, int period, DateTime* days)
//
//  Input:   period = index of reporting time period
//  Output:  days = date/time value
//  Purpose: retrieves the date/time for a specific reporting period
//           from the binary output file.
//
{
    INT4 bytePos;

    TOutputShared *otpt = &sp->OutputShared;

    bytePos = otpt->OutputStartPos + (period-1)*otpt->BytesPerPeriod;
    fseek(sp->Fout.file, bytePos, SEEK_SET);
    *days = NO_DATE;
    fread(days, sizeof(REAL8), 1, sp->Fout.file);
}

//=============================================================================

void output_readSubcatchResults(SWMM_Project *sp, int period, int index)
//
//  Input:   period = index of reporting time period
//           index = subcatchment index
//  Output:  none
//  Purpose: reads computed results for a subcatchment at a specific time
//           period.
//
{
    INT4 bytePos;

    TOutputShared *otpt = &sp->OutputShared;
    TOutputExport *otptx = &sp->OutputExport;

    bytePos = otpt->OutputStartPos + (period-1)*otpt->BytesPerPeriod;
    bytePos += sizeof(REAL8) + index*otpt->NsubcatchResults*sizeof(REAL4);
    fseek(sp->Fout.file, bytePos, SEEK_SET);
    fread(otptx->SubcatchResults, sizeof(REAL4), otpt->NsubcatchResults, sp->Fout.file);
}

//=============================================================================

void output_readNodeResults(SWMM_Project *sp, int period, int index)
//
//  Input:   period = index of reporting time period
//           index = node index
//  Output:  none
//  Purpose: reads computed results for a node at a specific time period.
//
{
    INT4 bytePos;

    TOutputShared *otpt = &sp->OutputShared;
    TOutputExport *otptx = &sp->OutputExport;

    bytePos = otpt->OutputStartPos + (period-1)*otpt->BytesPerPeriod;
    bytePos += sizeof(REAL8) + otpt->NumSubcatch*otpt->NsubcatchResults*sizeof(REAL4);
    bytePos += index*otpt->NnodeResults*sizeof(REAL4);
    fseek(sp->Fout.file, bytePos, SEEK_SET);
    fread(otptx->NodeResults, sizeof(REAL4), otpt->NnodeResults, sp->Fout.file);
}

//=============================================================================

void output_readLinkResults(SWMM_Project *sp, int period, int index)
//
//  Input:   period = index of reporting time period
//           index = link index
//  Output:  none
//  Purpose: reads computed results for a link at a specific time period.
//
{
    INT4 bytePos;

    TOutputShared *otpt = &sp->OutputShared;
    TOutputExport *otptx = &sp->OutputExport;

    bytePos = otpt->OutputStartPos + (period-1)*otpt->BytesPerPeriod;
    bytePos += sizeof(REAL8) + otpt->NumSubcatch*otpt->NsubcatchResults*sizeof(REAL4);
    bytePos += otpt->NumNodes*otpt->NnodeResults*sizeof(REAL4);
    bytePos += index*otpt->NlinkResults*sizeof(REAL4);
    fseek(sp->Fout.file, bytePos, SEEK_SET);
    fread(otptx->LinkResults, sizeof(REAL4), otpt->NlinkResults, sp->Fout.file);
    fread(otpt->SysResults, sizeof(REAL4), MAX_SYS_RESULTS, sp->Fout.file);
}

//=============================================================================