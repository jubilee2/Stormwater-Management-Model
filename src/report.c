//-----------------------------------------------------------------------------
//   report.c
//
//   Project:  EPA SWMM5
//   Version:  5.1
//   Date:     03/21/2014  (Build 5.1.001)
//             04/14/14    (Build 5.1.004)
//             09/15/14    (Build 5.1.007)
//             04/02/15    (Build 5.1.008)
//             08/01/16    (Build 5.1.011)
//             03/14/17    (Build 5.1.012)
//   Author:   L. Rossman (EPA)
//
//   Report writing functions.
//
//   Build 5.1.004:
//   - Ignore RDII option reported.
//
//   Build 5.1.007:
//   - Total exfiltration loss reported.
//
//   Build 5.1.008:
//   - Number of threads option reported.
//   - LID drainage volume and outfall runon reported.
//   - "Internal Outflow" label changed to "Flooding Loss" in Flow Routing
//     Continuity table.
//   - Exfiltration loss added into Quality Routing Continuity table.
//
//   Build 5.1.011:
//   - Blank line added after writing project title.
//   - Text of error message saved to global variable ErrorMsg.
//   - Global variable Warnings incremented after warning message issued.
//
//   Build 5.1.012:
//   - System time step statistics adjusted for time in steady state.
//
//-----------------------------------------------------------------------------
#define _CRT_SECURE_NO_DEPRECATE

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "headers.h"

#define WRITE(x) (report_writeLine(sp,(x)))
#define LINE_10 "----------"
#define LINE_12 "------------"
#define LINE_51 \
"---------------------------------------------------"
#define LINE_64 \
"----------------------------------------------------------------"

//-----------------------------------------------------------------------------
//  Local functions
//-----------------------------------------------------------------------------
static void report_LoadingErrors(SWMM_Project *sp, int p1, int p2,
        TLoadingTotals* totals);
static void report_QualErrors(SWMM_Project *sp, int p1, int p2,
        TRoutingTotals* totals);
static void report_Subcatchments(SWMM_Project *sp);
static void report_SubcatchHeader(SWMM_Project *sp, char *id);
static void report_Nodes(SWMM_Project *sp);
static void report_NodeHeader(SWMM_Project *sp, char *id);
static void report_Links(SWMM_Project *sp);
static void report_LinkHeader(SWMM_Project *sp, char *id);


//=============================================================================

int report_readOptions(SWMM_Project *sp, char* tok[], int ntoks)
//
//  Input:   tok[] = array of string tokens
//           ntoks = number of tokens
//  Output:  returns an error code
//  Purpose: reads reporting options from a line of input
//
{
    char  k;
    int   j, m, t;
    if ( ntoks < 2 ) return error_setInpError(sp, ERR_ITEMS, "");
    k = (char)findmatch(tok[0], ReportWords);
    if ( k < 0 ) return error_setInpError(sp, ERR_KEYWORD, tok[0]);
    switch ( k )
    {
      case 0: // Input
        m = findmatch(tok[1], NoYesWords);
        if      ( m == YES ) sp->RptFlags.input = TRUE;
        else if ( m == NO )  sp->RptFlags.input = FALSE;
        else                 return error_setInpError(sp, ERR_KEYWORD, tok[1]);
        return 0;

      case 1: // Continuity
        m = findmatch(tok[1], NoYesWords);
        if      ( m == YES ) sp->RptFlags.continuity = TRUE;
        else if ( m == NO )  sp->RptFlags.continuity = FALSE;
        else                 return error_setInpError(sp, ERR_KEYWORD, tok[1]);
        return 0;

      case 2: // Flow Statistics
        m = findmatch(tok[1], NoYesWords);
        if      ( m == YES ) sp->RptFlags.flowStats = TRUE;
        else if ( m == NO )  sp->RptFlags.flowStats = FALSE;
        else                 return error_setInpError(sp, ERR_KEYWORD, tok[1]);
        return 0;

      case 3: // Controls
        m = findmatch(tok[1], NoYesWords);
        if      ( m == YES ) sp->RptFlags.controls = TRUE;
        else if ( m == NO )  sp->RptFlags.controls = FALSE;
        else                 return error_setInpError(sp, ERR_KEYWORD, tok[1]);
        return 0;

      case 4:  m = SUBCATCH;  break;  // Subcatchments
      case 5:  m = NODE;      break;  // Nodes
      case 6:  m = LINK;      break;  // Links

      case 7: // Node Statistics
        m = findmatch(tok[1], NoYesWords);
        if      ( m == YES ) sp->RptFlags.nodeStats = TRUE;
        else if ( m == NO )  sp->RptFlags.nodeStats = FALSE;
        else                 return error_setInpError(sp, ERR_KEYWORD, tok[1]);
        return 0;

      default: return error_setInpError(sp, ERR_KEYWORD, tok[1]);
    }
    k = (char)findmatch(tok[1], NoneAllWords);
    if ( k < 0 )
    {
        k = SOME;
        for (t = 1; t < ntoks; t++)
        {
            j = project_findObject(sp, m, tok[t]);
            if ( j < 0 ) return error_setInpError(sp, ERR_NAME, tok[t]);
            switch ( m )
            {
              case SUBCATCH:  sp->Subcatch[j].rptFlag = TRUE;  break;
              case NODE:      sp->Node[j].rptFlag = TRUE;  break;
              case LINK:      sp->Link[j].rptFlag = TRUE;  break;
            }
        }
    }
    switch ( m )
    {
      case SUBCATCH: sp->RptFlags.subcatchments = k;  break;
      case NODE:     sp->RptFlags.nodes = k;  break;
      case LINK:     sp->RptFlags.links = k;  break;
    }
    return 0;
}

//=============================================================================

void report_writeLine(SWMM_Project *sp, char *line)
//
//  Input:   line = line of text
//  Output:  none
//  Purpose: writes line of text to report file.
//
{
    if ( sp->Frpt.file ) fprintf(sp->Frpt.file, "\n  %s", line);
}

//=============================================================================

void report_writeSysTime(SWMM_Project *sp)
//
//  Input:   none
//  Output:  none
//  Purpose: writes starting/ending processing times to report file.
//
{
    char    theTime[9];
    double  elapsedTime;
    time_t  endTime;

    TReportShared *rprt = &sp->ReportShared;

    if ( sp->Frpt.file )
    {
        fprintf(sp->Frpt.file, FMT20, ctime(&rprt->SysTime));
        time(&endTime);
        fprintf(sp->Frpt.file, FMT20a, ctime(&endTime));
        elapsedTime = difftime(endTime, rprt->SysTime);
        fprintf(sp->Frpt.file, FMT21);
        if ( elapsedTime < 1.0 ) fprintf(sp->Frpt.file, "< 1 sec");
        else
        {
            elapsedTime /= SECperDAY;
            if (elapsedTime >= 1.0)
            {
                fprintf(sp->Frpt.file, "%d.", (int)floor(elapsedTime));
                elapsedTime -= floor(elapsedTime);
            }
            datetime_timeToStr(elapsedTime, theTime);
            fprintf(sp->Frpt.file, "%s", theTime);
        }
    }
}


//=============================================================================
//      SIMULATION OPTIONS REPORTING
//=============================================================================

void report_writeLogo(SWMM_Project *sp)
//
//  Input:   none
//  Output:  none
//  Purpose: writes report header lines to report file.
//
{
	char SEMVERSION[SEMVERSION_LEN];

	TReportShared *rprt = &sp->ReportShared;

	getSemVersion(SEMVERSION);

	sprintf(sp->Msg, \
		"\n  EPA STORM WATER MANAGEMENT MODEL - VERSION 5.1 (Build %s)", SEMVERSION);

    fprintf(sp->Frpt.file, sp->Msg);
    fprintf(sp->Frpt.file, FMT09);
    fprintf(sp->Frpt.file, FMT10);
    time(&rprt->SysTime);                    // Save starting wall clock time
}

//=============================================================================

void report_writeTitle(SWMM_Project *sp)
//
//  Input:   none
//  Output:  none
//  Purpose: writes project title to report file.
//
{
    int i;
    int lineCount = 0;                                                         //(5.1.011)
    if ( sp->ErrorCode ) return;
    for (i=0; i<MAXTITLE; i++) if ( strlen(sp->Title[i]) > 0 )
    {
        WRITE(sp->Title[i]);
        lineCount++;                                                           //(5.1.011)
    }
    if ( lineCount > 0 ) WRITE("");                                            //(5.1.011)
}

//=============================================================================

void report_writeOptions(SWMM_Project *sp)
//
//  Input:   none
//  Output:  none
//  Purpose: writes analysis options in use to report file.
//
{
    char str[80];

    WRITE("");
    WRITE("*********************************************************");
    WRITE("NOTE: The summary statistics displayed in this report are");
    WRITE("based on results found at every computational time step,  ");
    WRITE("not just on results from each reporting time step.");
    WRITE("*********************************************************");
    WRITE("");
    WRITE("****************");
    WRITE("Analysis Options");
    WRITE("****************");
    fprintf(sp->Frpt.file, "\n  Flow Units ............... %s",
        FlowUnitWords[sp->FlowUnits]);
    fprintf(sp->Frpt.file, "\n  Process Models:");
    fprintf(sp->Frpt.file, "\n    Rainfall/Runoff ........ ");
    if ( sp->IgnoreRainfall || sp->Nobjects[GAGE] == 0 )
        fprintf(sp->Frpt.file, "NO");
    else fprintf(sp->Frpt.file, "YES");

    fprintf(sp->Frpt.file, "\n    RDII ................... ");                     //(5.1.004)
    if ( sp->IgnoreRDII || sp->Nobjects[UNITHYD] == 0 )
        fprintf(sp->Frpt.file, "NO");
    else fprintf(sp->Frpt.file, "YES");

    fprintf(sp->Frpt.file, "\n    Snowmelt ............... ");
    if ( sp->IgnoreSnowmelt || sp->Nobjects[SNOWMELT] == 0 )
        fprintf(sp->Frpt.file, "NO");
    else fprintf(sp->Frpt.file, "YES");
    fprintf(sp->Frpt.file, "\n    Groundwater ............ ");
    if ( sp->IgnoreGwater || sp->Nobjects[AQUIFER] == 0 )
        fprintf(sp->Frpt.file, "NO");
    else fprintf(sp->Frpt.file, "YES");
    fprintf(sp->Frpt.file, "\n    Flow Routing ........... ");
    if ( sp->IgnoreRouting || sp->Nobjects[LINK] == 0 )
        fprintf(sp->Frpt.file, "NO");
    else
    {
        fprintf(sp->Frpt.file, "YES");
        fprintf(sp->Frpt.file, "\n    Ponding Allowed ........ ");
        if ( sp->AllowPonding ) fprintf(sp->Frpt.file, "YES");
        else                fprintf(sp->Frpt.file, "NO");
    }
    fprintf(sp->Frpt.file, "\n    Water Quality .......... ");
    if ( sp->IgnoreQuality || sp->Nobjects[POLLUT] == 0 )
        fprintf(sp->Frpt.file, "NO");
    else fprintf(sp->Frpt.file, "YES");

    if ( sp->Nobjects[SUBCATCH] > 0 )
    fprintf(sp->Frpt.file, "\n  Infiltration Method ...... %s",
        InfilModelWords[sp->InfilModel]);
    if ( sp->Nobjects[LINK] > 0 )
    fprintf(sp->Frpt.file, "\n  Flow Routing Method ...... %s",
        RouteModelWords[sp->RouteModel]);
    datetime_dateToStr(sp, sp->StartDate, str);
    fprintf(sp->Frpt.file, "\n  Starting Date ............ %s", str);
    datetime_timeToStr(sp->StartTime, str);
    fprintf(sp->Frpt.file, " %s", str);
    datetime_dateToStr(sp, sp->EndDate, str);
    fprintf(sp->Frpt.file, "\n  Ending Date .............. %s", str);
    datetime_timeToStr(sp->EndTime, str);
    fprintf(sp->Frpt.file, " %s", str);
    fprintf(sp->Frpt.file, "\n  Antecedent Dry Days ...... %.1f", sp->StartDryDays);
    datetime_timeToStr(datetime_encodeTime(0, 0, sp->ReportStep), str);
    fprintf(sp->Frpt.file, "\n  Report Time Step ......... %s", str);
    if ( sp->Nobjects[SUBCATCH] > 0 )
    {
        datetime_timeToStr(datetime_encodeTime(0, 0, sp->WetStep), str);
        fprintf(sp->Frpt.file, "\n  Wet Time Step ............ %s", str);
        datetime_timeToStr(datetime_encodeTime(0, 0, sp->DryStep), str);
        fprintf(sp->Frpt.file, "\n  Dry Time Step ............ %s", str);
    }
    if ( sp->Nobjects[LINK] > 0 )
    {
        fprintf(sp->Frpt.file, "\n  Routing Time Step ........ %.2f sec", sp->RouteStep);
		if ( sp->RouteModel == DW )
		{
		fprintf(sp->Frpt.file, "\n  Variable Time Step ....... ");
		if ( sp->CourantFactor > 0.0 ) fprintf(sp->Frpt.file, "YES");
		else                       fprintf(sp->Frpt.file, "NO");
		fprintf(sp->Frpt.file, "\n  Maximum Trials ........... %d", sp->MaxTrials);
        fprintf(sp->Frpt.file, "\n  Number of Threads ........ %d", sp->NumThreads);   //(5.1.008)
		fprintf(sp->Frpt.file, "\n  Head Tolerance ........... %.6f ",
            sp->HeadTol*UCF(sp, LENGTH));                                              //(5.1.008)
		if ( sp->UnitSystem == US ) fprintf(sp->Frpt.file, "ft");
		else                    fprintf(sp->Frpt.file, "m");
		}
    }
    WRITE("");
}


//=============================================================================
//      RAINFALL FILE REPORTING
//=============================================================================

void report_writeRainStats(SWMM_Project *sp, int i, TRainStats* r)
//
//  Input:   i = rain gage index
//           r = rain file summary statistics
//  Output:  none
//  Purpose: writes summary of rain data read from file to report file.
//
{
    char date1[] = "***********";
    char date2[] = "***********";

    if ( i < 0 )
    {
        WRITE("");
        WRITE("*********************");
        WRITE("Rainfall File Summary");
        WRITE("*********************");
        fprintf(sp->Frpt.file,
"\n  Station    First        Last         Recording   Periods    Periods    Periods");
        fprintf(sp->Frpt.file,
"\n  ID         Date         Date         Frequency  w/Precip    Missing    Malfunc.");
        fprintf(sp->Frpt.file,
"\n  -------------------------------------------------------------------------------\n");
    }
    else
    {
        if ( r->startDate != NO_DATE ) datetime_dateToStr(sp, r->startDate, date1);
        if ( r->endDate   != NO_DATE ) datetime_dateToStr(sp, r->endDate, date2);
        fprintf(sp->Frpt.file, "  %-10s %-11s  %-11s  %5d min    %6ld     %6ld     %6ld\n",
            sp->Gage[i].staID, date1, date2, sp->Gage[i].rainInterval/60,
            r->periodsRain, r->periodsMissing, r->periodsMalfunc);
    }
}


//=============================================================================
//      RDII REPORTING
//=============================================================================

void report_writeRdiiStats(SWMM_Project *sp, double rainVol, double rdiiVol)
//
//  Input:   rainVol = total rainfall volume over sewershed
//           rdiiVol = total RDII volume produced
//  Output:  none
//  Purpose: writes summary of RDII inflow to report file.
//
{
    double ratio;
    double ucf1, ucf2;

    ucf1 = UCF(sp, LENGTH) * UCF(sp, LANDAREA);
    if ( sp->UnitSystem == US) ucf2 = MGDperCFS / SECperDAY;
    else                   ucf2 = MLDperCFS / SECperDAY;

    WRITE("");
    fprintf(sp->Frpt.file,
    "\n  **********************           Volume        Volume");
    if ( sp->UnitSystem == US) fprintf(sp->Frpt.file,
    "\n  Rainfall Dependent I/I        acre-feet      10^6 gal");
    else fprintf(sp->Frpt.file,
    "\n  Rainfall Dependent I/I        hectare-m      10^6 ltr");
    fprintf(sp->Frpt.file,
    "\n  **********************        ---------     ---------");

    fprintf(sp->Frpt.file, "\n  Sewershed Rainfall ......%14.3f%14.3f",
            rainVol * ucf1, rainVol * ucf2);

    fprintf(sp->Frpt.file, "\n  RDII Produced ...........%14.3f%14.3f",
            rdiiVol * ucf1, rdiiVol * ucf2);

    if ( rainVol == 0.0 ) ratio = 0.0;
    else ratio = rdiiVol / rainVol;
    fprintf(sp->Frpt.file, "\n  RDII Ratio ..............%14.3f", ratio);
    WRITE("");
}


//=============================================================================
//      CONTROL ACTIONS REPORTING
//=============================================================================

void   report_writeControlActionsHeading(SWMM_Project *sp)
{
    WRITE("");
    WRITE("*********************");
    WRITE("Control Actions Taken");
    WRITE("*********************");
    fprintf(sp->Frpt.file, "\n");
}

//=============================================================================

void report_writeControlAction(SWMM_Project *sp, DateTime aDate, char* linkID,
        double value, char* ruleID)
//
//  Input:   aDate  = date/time of rule action
//           linkID = ID of link being controlled
//           value  = new status value of link
//           ruleID = ID of rule implementing the action
//  Output:  none
//  Purpose: reports action taken by a control rule.
//
{
    char     theDate[12];
    char     theTime[9];

    datetime_dateToStr(sp, aDate, theDate);
    datetime_timeToStr(aDate, theTime);

    fprintf(sp->Frpt.file,
            "  %11s: %8s Link %s setting changed to %6.2f by Control %s\n",
            theDate, theTime, linkID, value, ruleID);
}


//=============================================================================
//      CONTINUITY ERROR REPORTING
//=============================================================================

void report_writeRunoffError(SWMM_Project *sp, TRunoffTotals* totals,
        double totalArea)
//
//  Input:  totals = accumulated runoff totals
//          totalArea = total area of all subcatchments
//  Output:  none
//  Purpose: writes runoff continuity error to report file.
//
{

    if ( sp->Frunoff.mode == USE_FILE )
    {
        WRITE("");
        fprintf(sp->Frpt.file,
        "\n  **************************"
        "\n  Runoff Quantity Continuity"
        "\n  **************************"
        "\n  Runoff supplied by interface file %s", sp->Frunoff.name);
        WRITE("");
        return;
    }

    if ( totalArea == 0.0 ) return;
    WRITE("");

    fprintf(sp->Frpt.file,
    "\n  **************************        Volume         Depth");
    if ( sp->UnitSystem == US) fprintf(sp->Frpt.file,
    "\n  Runoff Quantity Continuity     acre-feet        inches");
    else fprintf(sp->Frpt.file,
    "\n  Runoff Quantity Continuity     hectare-m            mm");
    fprintf(sp->Frpt.file,
    "\n  **************************     ---------       -------");

    if ( totals->initStorage > 0.0 )
    {
        fprintf(sp->Frpt.file, "\n  Initial LID Storage ......%14.3f%14.3f",
            totals->initStorage * UCF(sp, LENGTH) * UCF(sp, LANDAREA),
            totals->initStorage / totalArea * UCF(sp, RAINDEPTH));
    }

    if ( sp->Nobjects[SNOWMELT] > 0 )
    {
        fprintf(sp->Frpt.file, "\n  Initial Snow Cover .......%14.3f%14.3f",
            totals->initSnowCover * UCF(sp, LENGTH) * UCF(sp, LANDAREA),
            totals->initSnowCover / totalArea * UCF(sp, RAINDEPTH));
    }

    fprintf(sp->Frpt.file, "\n  Total Precipitation ......%14.3f%14.3f",
            totals->rainfall * UCF(sp, LENGTH) * UCF(sp, LANDAREA),
            totals->rainfall / totalArea * UCF(sp, RAINDEPTH));

////  Following code segment added to release 5.1.008.  ////                   //(5.1.008)
    if ( totals->runon > 0.0 )
    {
        fprintf(sp->Frpt.file, "\n  Outfall Runon ............%14.3f%14.3f",
            totals->runon * UCF(sp, LENGTH) * UCF(sp, LANDAREA),
            totals->runon / totalArea * UCF(sp, RAINDEPTH));
    }
////

    fprintf(sp->Frpt.file, "\n  Evaporation Loss .........%14.3f%14.3f",
            totals->evap * UCF(sp, LENGTH) * UCF(sp, LANDAREA),
            totals->evap / totalArea * UCF(sp, RAINDEPTH));

    fprintf(sp->Frpt.file, "\n  Infiltration Loss ........%14.3f%14.3f",
            totals->infil * UCF(sp, LENGTH) * UCF(sp, LANDAREA),
            totals->infil / totalArea * UCF(sp, RAINDEPTH));

    fprintf(sp->Frpt.file, "\n  Surface Runoff ...........%14.3f%14.3f",
            totals->runoff * UCF(sp, LENGTH) * UCF(sp, LANDAREA),
            totals->runoff / totalArea * UCF(sp, RAINDEPTH));

////  Following code segment added to release 5.1.008.  ////                   //(5.1.008)
    if ( totals->drains > 0.0 )
    {
        fprintf(sp->Frpt.file, "\n  LID Drainage .............%14.3f%14.3f",
            totals->drains * UCF(sp, LENGTH) * UCF(sp, LANDAREA),
            totals->drains / totalArea * UCF(sp, RAINDEPTH));
    }

    if ( sp->Nobjects[SNOWMELT] > 0 )
    {
        fprintf(sp->Frpt.file, "\n  Snow Removed .............%14.3f%14.3f",
            totals->snowRemoved * UCF(sp, LENGTH) * UCF(sp, LANDAREA),
            totals->snowRemoved / totalArea * UCF(sp, RAINDEPTH));
        fprintf(sp->Frpt.file, "\n  Final Snow Cover .........%14.3f%14.3f",
            totals->finalSnowCover * UCF(sp, LENGTH) * UCF(sp, LANDAREA),
            totals->finalSnowCover / totalArea * UCF(sp, RAINDEPTH));
    }

    fprintf(sp->Frpt.file, "\n  Final Storage ............%14.3f%14.3f",           //(5.1.008)
            totals->finalStorage * UCF(sp, LENGTH) * UCF(sp, LANDAREA),
            totals->finalStorage / totalArea * UCF(sp, RAINDEPTH));

    fprintf(sp->Frpt.file, "\n  Continuity Error (%%) .....%14.3f",
            totals->pctError);
    WRITE("");
}

//=============================================================================

void report_writeLoadingError(SWMM_Project *sp, TLoadingTotals* totals)
//
//  Input:   totals = accumulated pollutant loading totals
//           area = total area of all subcatchments
//  Output:  none
//  Purpose: writes runoff loading continuity error to report file.
//
{
    int p1, p2;
    p1 = 1;
    p2 = MIN(5, sp->Nobjects[POLLUT]);
    while ( p1 <= sp->Nobjects[POLLUT] )
    {
        report_LoadingErrors(sp, p1-1, p2-1, totals);
        p1 = p2 + 1;
        p2 = p1 + 4;
        p2 = MIN(p2, sp->Nobjects[POLLUT]);
    }
}

//=============================================================================

void report_LoadingErrors(SWMM_Project *sp, int p1, int p2, TLoadingTotals* totals)
//
//  Input:   p1 = index of first pollutant to report
//           p2 = index of last pollutant to report
//           totals = accumulated pollutant loading totals
//           area = total area of all subcatchments
//  Output:  none
//  Purpose: writes runoff loading continuity error to report file for
//           up to 5 pollutants at a time.
//
{
    int    i;
    int    p;
    double cf = 1.0;
    char   units[15];

    WRITE("");
    fprintf(sp->Frpt.file, "\n  **************************");
    for (p = p1; p <= p2; p++)
    {
        fprintf(sp->Frpt.file, "%14s", sp->Pollut[p].ID);
    }
    fprintf(sp->Frpt.file, "\n  Runoff Quality Continuity ");
    for (p = p1; p <= p2; p++)
    {
        i = sp->UnitSystem;
        if ( sp->Pollut[p].units == COUNT ) i = 2;
        strcpy(units, LoadUnitsWords[i]);
        fprintf(sp->Frpt.file, "%14s", units);
    }
    fprintf(sp->Frpt.file, "\n  **************************");
    for (p = p1; p <= p2; p++)
    {
        fprintf(sp->Frpt.file, "    ----------");
    }

    fprintf(sp->Frpt.file, "\n  Initial Buildup ..........");
    for (p = p1; p <= p2; p++)
    {
        fprintf(sp->Frpt.file, "%14.3f", totals[p].initLoad*cf);
    }
    fprintf(sp->Frpt.file, "\n  Surface Buildup ..........");
    for (p = p1; p <= p2; p++)
    {
        fprintf(sp->Frpt.file, "%14.3f", totals[p].buildup*cf);
    }
    fprintf(sp->Frpt.file, "\n  Wet Deposition ...........");
    for (p = p1; p <= p2; p++)
    {
        fprintf(sp->Frpt.file, "%14.3f", totals[p].deposition*cf);
    }
    fprintf(sp->Frpt.file, "\n  Sweeping Removal .........");
    for (p = p1; p <= p2; p++)
    {
        fprintf(sp->Frpt.file, "%14.3f", totals[p].sweeping*cf);
    }
    fprintf(sp->Frpt.file, "\n  Infiltration Loss ........");
    for (p = p1; p <= p2; p++)
    {
        fprintf(sp->Frpt.file, "%14.3f", totals[p].infil*cf);
    }
    fprintf(sp->Frpt.file, "\n  BMP Removal ..............");
    for (p = p1; p <= p2; p++)
    {
        fprintf(sp->Frpt.file, "%14.3f", totals[p].bmpRemoval*cf);
    }
    fprintf(sp->Frpt.file, "\n  Surface Runoff ...........");
    for (p = p1; p <= p2; p++)
    {
        fprintf(sp->Frpt.file, "%14.3f", totals[p].runoff*cf);
    }
    fprintf(sp->Frpt.file, "\n  Remaining Buildup ........");
    for (p = p1; p <= p2; p++)
    {
        fprintf(sp->Frpt.file, "%14.3f", totals[p].finalLoad*cf);
    }
    fprintf(sp->Frpt.file, "\n  Continuity Error (%%) .....");
    for (p = p1; p <= p2; p++)
    {
        fprintf(sp->Frpt.file, "%14.3f", totals[p].pctError);
    }
    WRITE("");
}

//=============================================================================

void report_writeGwaterError(SWMM_Project *sp, TGwaterTotals* totals,
        double gwArea)
//
//  Input:   totals = accumulated groundwater totals
//           gwArea = total area of all subcatchments with groundwater
//  Output:  none
//  Purpose: writes groundwater continuity error to report file.
//
{
    WRITE("");
    fprintf(sp->Frpt.file,
    "\n  **************************        Volume         Depth");
    if ( sp->UnitSystem == US) fprintf(sp->Frpt.file,
    "\n  Groundwater Continuity         acre-feet        inches");
    else fprintf(sp->Frpt.file,
    "\n  Groundwater Continuity         hectare-m            mm");
    fprintf(sp->Frpt.file,
    "\n  **************************     ---------       -------");
    fprintf(sp->Frpt.file, "\n  Initial Storage ..........%14.3f%14.3f",
            totals->initStorage * UCF(sp, LENGTH) * UCF(sp, LANDAREA),
            totals->initStorage / gwArea * UCF(sp, RAINDEPTH));

    fprintf(sp->Frpt.file, "\n  Infiltration .............%14.3f%14.3f",
            totals->infil * UCF(sp, LENGTH) * UCF(sp, LANDAREA),
            totals->infil / gwArea * UCF(sp, RAINDEPTH));

    fprintf(sp->Frpt.file, "\n  Upper Zone ET ............%14.3f%14.3f",
            totals->upperEvap * UCF(sp, LENGTH) * UCF(sp, LANDAREA),
            totals->upperEvap / gwArea * UCF(sp, RAINDEPTH));

    fprintf(sp->Frpt.file, "\n  Lower Zone ET ............%14.3f%14.3f",
            totals->lowerEvap * UCF(sp, LENGTH) * UCF(sp, LANDAREA),
            totals->lowerEvap / gwArea * UCF(sp, RAINDEPTH));

    fprintf(sp->Frpt.file, "\n  Deep Percolation .........%14.3f%14.3f",
            totals->lowerPerc * UCF(sp, LENGTH) * UCF(sp, LANDAREA),
            totals->lowerPerc / gwArea * UCF(sp, RAINDEPTH));

    fprintf(sp->Frpt.file, "\n  Groundwater Flow .........%14.3f%14.3f",
            totals->gwater * UCF(sp, LENGTH) * UCF(sp, LANDAREA),
            totals->gwater / gwArea * UCF(sp, RAINDEPTH));

    fprintf(sp->Frpt.file, "\n  Final Storage ............%14.3f%14.3f",
            totals->finalStorage * UCF(sp, LENGTH) * UCF(sp, LANDAREA),
            totals->finalStorage / gwArea * UCF(sp, RAINDEPTH));

    fprintf(sp->Frpt.file, "\n  Continuity Error (%%) .....%14.3f",
            totals->pctError);
    WRITE("");
}

//=============================================================================

void report_writeFlowError(SWMM_Project *sp, TRoutingTotals *totals)
//
//  Input:  totals = accumulated flow routing totals
//  Output:  none
//  Purpose: writes flow routing continuity error to report file.
//
{
    double ucf1, ucf2;

    ucf1 = UCF(sp, LENGTH) * UCF(sp, LANDAREA);
    if ( sp->UnitSystem == US) ucf2 = MGDperCFS / SECperDAY;
    else                   ucf2 = MLDperCFS / SECperDAY;

    WRITE("");
    fprintf(sp->Frpt.file,
    "\n  **************************        Volume        Volume");
    if ( sp->UnitSystem == US) fprintf(sp->Frpt.file,
    "\n  Flow Routing Continuity        acre-feet      10^6 gal");
    else fprintf(sp->Frpt.file,
    "\n  Flow Routing Continuity        hectare-m      10^6 ltr");
    fprintf(sp->Frpt.file,
    "\n  **************************     ---------     ---------");

    fprintf(sp->Frpt.file, "\n  Dry Weather Inflow .......%14.3f%14.3f",
            totals->dwInflow * ucf1, totals->dwInflow * ucf2);

    fprintf(sp->Frpt.file, "\n  Wet Weather Inflow .......%14.3f%14.3f",
            totals->wwInflow * ucf1, totals->wwInflow * ucf2);

    fprintf(sp->Frpt.file, "\n  Groundwater Inflow .......%14.3f%14.3f",
            totals->gwInflow * ucf1, totals->gwInflow * ucf2);

    fprintf(sp->Frpt.file, "\n  RDII Inflow ..............%14.3f%14.3f",
            totals->iiInflow * ucf1, totals->iiInflow * ucf2);

    fprintf(sp->Frpt.file, "\n  External Inflow ..........%14.3f%14.3f",
            totals->exInflow * ucf1, totals->exInflow * ucf2);

    fprintf(sp->Frpt.file, "\n  External Outflow .........%14.3f%14.3f",
            totals->outflow * ucf1, totals->outflow * ucf2);

    fprintf(sp->Frpt.file, "\n  Flooding Loss ............%14.3f%14.3f",           //(5.1.008)
            totals->flooding * ucf1, totals->flooding * ucf2);

    fprintf(sp->Frpt.file, "\n  Evaporation Loss .........%14.3f%14.3f",
            totals->evapLoss * ucf1, totals->evapLoss * ucf2);

    fprintf(sp->Frpt.file, "\n  Exfiltration Loss ........%14.3f%14.3f",           //(5.1.007)
            totals->seepLoss * ucf1, totals->seepLoss * ucf2);

    fprintf(sp->Frpt.file, "\n  Initial Stored Volume ....%14.3f%14.3f",
            totals->initStorage * ucf1, totals->initStorage * ucf2);

    fprintf(sp->Frpt.file, "\n  Final Stored Volume ......%14.3f%14.3f",
            totals->finalStorage * ucf1, totals->finalStorage * ucf2);

    fprintf(sp->Frpt.file, "\n  Continuity Error (%%) .....%14.3f",
            totals->pctError);
    WRITE("");
}

//=============================================================================

void report_writeQualError(SWMM_Project *sp, TRoutingTotals QualTotals[])
//
//  Input:   totals = accumulated quality routing totals for each pollutant
//  Output:  none
//  Purpose: writes quality routing continuity error to report file.
//
{
    int p1, p2;
    p1 = 1;
    p2 = MIN(5, sp->Nobjects[POLLUT]);
    while ( p1 <= sp->Nobjects[POLLUT] )
    {
        report_QualErrors(sp, p1-1, p2-1, QualTotals);
        p1 = p2 + 1;
        p2 = p1 + 4;
        p2 = MIN(p2, sp->Nobjects[POLLUT]);
    }
}

//=============================================================================

void report_QualErrors(SWMM_Project *sp, int p1, int p2,
        TRoutingTotals QualTotals[])
{
    int   i;
    int   p;
    char  units[15];

    WRITE("");
    fprintf(sp->Frpt.file, "\n  **************************");
    for (p = p1; p <= p2; p++)
    {
        fprintf(sp->Frpt.file, "%14s", sp->Pollut[p].ID);
    }
    fprintf(sp->Frpt.file, "\n  Quality Routing Continuity");
    for (p = p1; p <= p2; p++)
    {
        i = sp->UnitSystem;
        if ( sp->Pollut[p].units == COUNT ) i = 2;
        strcpy(units, LoadUnitsWords[i]);
        fprintf(sp->Frpt.file, "%14s", units);
    }
    fprintf(sp->Frpt.file, "\n  **************************");
    for (p = p1; p <= p2; p++)
    {
        fprintf(sp->Frpt.file, "    ----------");
    }

    fprintf(sp->Frpt.file, "\n  Dry Weather Inflow .......");
    for (p = p1; p <= p2; p++)
    {
        fprintf(sp->Frpt.file, "%14.3f", QualTotals[p].dwInflow);
    }

    fprintf(sp->Frpt.file, "\n  Wet Weather Inflow .......");
    for (p = p1; p <= p2; p++)
    {
        fprintf(sp->Frpt.file, "%14.3f", QualTotals[p].wwInflow);
    }

    fprintf(sp->Frpt.file, "\n  Groundwater Inflow .......");
    for (p = p1; p <= p2; p++)
    {
        fprintf(sp->Frpt.file, "%14.3f", QualTotals[p].gwInflow);
    }

    fprintf(sp->Frpt.file, "\n  RDII Inflow ..............");
    for (p = p1; p <= p2; p++)
    {
        fprintf(sp->Frpt.file, "%14.3f", QualTotals[p].iiInflow);
    }

    fprintf(sp->Frpt.file, "\n  External Inflow ..........");
    for (p = p1; p <= p2; p++)
    {
        fprintf(sp->Frpt.file, "%14.3f", QualTotals[p].exInflow);
    }

    fprintf(sp->Frpt.file, "\n  External Outflow .........");
    for (p = p1; p <= p2; p++)
    {
        fprintf(sp->Frpt.file, "%14.3f", QualTotals[p].outflow);
    }

    fprintf(sp->Frpt.file, "\n  Flooding Loss ............");                      //(5.1.008)
    for (p = p1; p <= p2; p++)
    {
        fprintf(sp->Frpt.file, "%14.3f", QualTotals[p].flooding);
    }

////  Following code segment added to release 5.1.008.  ////                   //(5.1.008)
////
    fprintf(sp->Frpt.file, "\n  Exfiltration Loss ........");
    for (p = p1; p <= p2; p++)
    {
        fprintf(sp->Frpt.file, "%14.3f", QualTotals[p].seepLoss);
    }
////

    fprintf(sp->Frpt.file, "\n  Mass Reacted .............");
    for (p = p1; p <= p2; p++)
    {
        fprintf(sp->Frpt.file, "%14.3f", QualTotals[p].reacted);
    }

    fprintf(sp->Frpt.file, "\n  Initial Stored Mass ......");
    for (p = p1; p <= p2; p++)
    {
        fprintf(sp->Frpt.file, "%14.3f", QualTotals[p].initStorage);
    }

    fprintf(sp->Frpt.file, "\n  Final Stored Mass ........");
    for (p = p1; p <= p2; p++)
    {
        fprintf(sp->Frpt.file, "%14.3f", QualTotals[p].finalStorage);
    }

    fprintf(sp->Frpt.file, "\n  Continuity Error (%%) .....");
    for (p = p1; p <= p2; p++)
    {
        fprintf(sp->Frpt.file, "%14.3f", QualTotals[p].pctError);
    }
    WRITE("");
}

//=============================================================================

void report_writeMaxStats(SWMM_Project *sp, TMaxStats maxMassBalErrs[],
        TMaxStats maxCourantCrit[], int nMaxStats)
//
//  Input:   maxMassBal[] = nodes with highest mass balance errors
//           maxCourantCrit[] = nodes most often Courant time step critical
//           maxLinkTimes[] = links most often Courant time step critical
//           nMaxStats = number of most critical nodes/links saved
//  Output:  none
//  Purpose: lists nodes & links with highest mass balance errors and
//           time Courant time step critical
//
{
    int i, j, k;

    if ( sp->RouteModel != DW || sp->Nobjects[LINK] == 0 ) return;
    if ( nMaxStats <= 0 ) return;
    if ( maxMassBalErrs[0].index >= 0 )
    {
        WRITE("");
        WRITE("*************************");
        WRITE("Highest Continuity Errors");
        WRITE("*************************");
        for (i=0; i<nMaxStats; i++)
        {
            j = maxMassBalErrs[i].index;
            if ( j < 0 ) continue;
            fprintf(sp->Frpt.file, "\n  Node %s (%.2f%%)",
                sp->Node[j].ID, maxMassBalErrs[i].value);
        }
        WRITE("");
    }

    if ( sp->CourantFactor == 0.0 ) return;
    WRITE("");
    WRITE("***************************");
    WRITE("Time-Step Critical Elements");
    WRITE("***************************");
    k = 0;
    for (i=0; i<nMaxStats; i++)
    {
        j = maxCourantCrit[i].index;
        if ( j < 0 ) continue;
        k++;
        if ( maxCourantCrit[i].objType == NODE )
             fprintf(sp->Frpt.file, "\n  Node %s", sp->Node[j].ID);
        else fprintf(sp->Frpt.file, "\n  Link %s", sp->Link[j].ID);
        fprintf(sp->Frpt.file, " (%.2f%%)", maxCourantCrit[i].value);
    }
    if ( k == 0 ) fprintf(sp->Frpt.file, "\n  None");
    WRITE("");
}

//=============================================================================

void report_writeMaxFlowTurns(SWMM_Project *sp, TMaxStats flowTurns[],
        int nMaxStats)
//
//  Input:   flowTurns[] = links with highest number of flow turns
//           nMaxStats = number of links in flowTurns[]
//  Output:  none
//  Purpose: lists links with highest number of flow turns (i.e., fraction
//           of time periods where the flow is higher (or lower) than the
//           flows in the previous and following periods).
//
{
    int i, j;

    if ( sp->Nobjects[LINK] == 0 ) return;
    WRITE("");
    WRITE("********************************");
    WRITE("Highest Flow Instability Indexes");
    WRITE("********************************");
    if ( nMaxStats <= 0 || flowTurns[0].index <= 0 )
        fprintf(sp->Frpt.file, "\n  All links are stable.");
    else
    {
        for (i=0; i<nMaxStats; i++)
        {
            j = flowTurns[i].index;
            if ( j < 0 ) continue;
            fprintf(sp->Frpt.file, "\n  Link %s (%.0f)",
                sp->Link[j].ID, flowTurns[i].value);
        }
    }
    WRITE("");
}

//=============================================================================

void report_writeSysStats(SWMM_Project *sp, TSysStats* sysStats)
//
//  Input:   sysStats = simulation statistics for overall system
//  Output:  none
//  Purpose: writes simulation statistics for overall system to report file.
//
{
    double x;
    double eventStepCount = (double)sp->StepCount - sysStats->steadyStateCount;    //(5.1.012)

    if ( sp->Nobjects[LINK] == 0 || sp->StepCount == 0
	                     || eventStepCount == 0.0 ) return;                //(5.1.012)   
    WRITE("");
    WRITE("*************************");
    WRITE("Routing Time Step Summary");
    WRITE("*************************");
    fprintf(sp->Frpt.file,
        "\n  Minimum Time Step           :  %7.2f sec",
        sysStats->minTimeStep);
    fprintf(sp->Frpt.file,
        "\n  Average Time Step           :  %7.2f sec",
        sysStats->avgTimeStep / eventStepCount);                               //(5.1.012)
    fprintf(sp->Frpt.file,
        "\n  Maximum Time Step           :  %7.2f sec",
        sysStats->maxTimeStep);
    x = (1.0 - sysStats->avgTimeStep * 1000.0 / sp->NewRoutingTime) * 100.0;       //(5.1.012)
    fprintf(sp->Frpt.file,
        "\n  Percent in Steady State     :  %7.2f", MIN(x, 100.0));
    fprintf(sp->Frpt.file,
        "\n  Average Iterations per Step :  %7.2f",
        sysStats->avgStepCount / eventStepCount);                              //(5.1.012)
    fprintf(sp->Frpt.file,
        "\n  Percent Not Converging      :  %7.2f",
        100.0 * (double)sp->NonConvergeCount / eventStepCount);                    //(5.1.012)
    WRITE("");
}


//=============================================================================
//      SIMULATION RESULTS REPORTING
//=============================================================================

void report_writeReport(SWMM_Project *sp)
//
//  Input:   none
//  Output:  none
//  Purpose: writes simulation results to report file.
//
{
    if ( sp->ErrorCode ) return;
    if ( sp->Nperiods == 0 ) return;
    if ( sp->RptFlags.subcatchments != NONE
         && ( sp->IgnoreRainfall == FALSE ||
              sp->IgnoreSnowmelt == FALSE ||
              sp->IgnoreGwater == FALSE)
       ) report_Subcatchments(sp);

    if ( sp->IgnoreRouting == TRUE && sp->IgnoreQuality == TRUE ) return;
    if ( sp->RptFlags.nodes != NONE ) report_Nodes(sp);
    if ( sp->RptFlags.links != NONE ) report_Links(sp);
}

//=============================================================================

void report_Subcatchments(SWMM_Project *sp)
//
//  Input:   none
//  Output:  none
//  Purpose: writes results for selected subcatchments to report file.
//
{
    int      j, p, k;
    int      period;
    DateTime days;
    char     theDate[12];
    char     theTime[9];
    int      hasSnowmelt = (sp->Nobjects[SNOWMELT] > 0 && !sp->IgnoreSnowmelt);
    int      hasGwater   = (sp->Nobjects[AQUIFER] > 0  && !sp->IgnoreGwater);
    int      hasQuality  = (sp->Nobjects[POLLUT] > 0 && !sp->IgnoreQuality);

    TOutputExport *otptx = &sp->OutputExport;

    if ( sp->Nobjects[SUBCATCH] == 0 ) return;
    WRITE("");
    WRITE("********************");
    WRITE("Subcatchment Results");
    WRITE("********************");
    k = 0;
    for (j = 0; j < sp->Nobjects[SUBCATCH]; j++)
    {
        if ( sp->Subcatch[j].rptFlag == TRUE )
        {
            report_SubcatchHeader(sp, sp->Subcatch[j].ID);
            for ( period = 1; period <= sp->Nperiods; period++ )
            {
                output_readDateTime(sp, period, &days);
                datetime_dateToStr(sp, days, theDate);
                datetime_timeToStr(days, theTime);
                output_readSubcatchResults(sp, period, k);
                fprintf(sp->Frpt.file, "\n  %11s %8s %10.3f%10.3f%10.4f",
                    theDate, theTime, otptx->SubcatchResults[SUBCATCH_RAINFALL],
                    otptx->SubcatchResults[SUBCATCH_EVAP]/24.0 +
                    otptx->SubcatchResults[SUBCATCH_INFIL],
                    otptx->SubcatchResults[SUBCATCH_RUNOFF]);
                if ( hasSnowmelt )
                    fprintf(sp->Frpt.file, "  %10.3f",
                            otptx->SubcatchResults[SUBCATCH_SNOWDEPTH]);
                if ( hasGwater )
                    fprintf(sp->Frpt.file, "%10.3f%10.4f",
                            otptx->SubcatchResults[SUBCATCH_GW_ELEV],
                            otptx->SubcatchResults[SUBCATCH_GW_FLOW]);
                if ( hasQuality )
                    for (p = 0; p < sp->Nobjects[POLLUT]; p++)
                        fprintf(sp->Frpt.file, "%10.3f",
                                otptx->SubcatchResults[SUBCATCH_WASHOFF+p]);
            }
            WRITE("");
            k++;
        }
    }
}

//=============================================================================

void  report_SubcatchHeader(SWMM_Project *sp, char *id)
//
//  Input:   id = subcatchment ID name
//  Output:  none
//  Purpose: writes table headings for subcatchment results to report file.
//
{
    int i;
    int hasSnowmelt = (sp->Nobjects[SNOWMELT] > 0 && !sp->IgnoreSnowmelt);
    int hasGwater   = (sp->Nobjects[AQUIFER] > 0  && !sp->IgnoreGwater);
    int hasQuality  = (sp->Nobjects[POLLUT] > 0 && !sp->IgnoreQuality);

    // --- print top border of header
    WRITE("");
    fprintf(sp->Frpt.file,"\n  <<< Subcatchment %s >>>", id);
    WRITE(LINE_51);
    if ( hasSnowmelt  > 0 ) fprintf(sp->Frpt.file, LINE_12);
    if ( hasGwater )
    {
        fprintf(sp->Frpt.file, LINE_10);
        fprintf(sp->Frpt.file, LINE_10);
    }
    if ( hasQuality )
    {
        for (i = 0; i < sp->Nobjects[POLLUT]; i++) fprintf(sp->Frpt.file, LINE_10);
    }

    // --- print first line of column headings
    fprintf(sp->Frpt.file,
    "\n  Date        Time        Precip.    Losses    Runoff");
    if ( hasSnowmelt ) fprintf(sp->Frpt.file, "  Snow Depth");
    if ( hasGwater   ) fprintf(sp->Frpt.file, "  GW Elev.   GW Flow");
    if ( hasQuality ) for (i = 0; i < sp->Nobjects[POLLUT]; i++)
        fprintf(sp->Frpt.file, "%10s", sp->Pollut[i].ID);

    // --- print second line of column headings
    if ( sp->UnitSystem == US ) fprintf(sp->Frpt.file,
    "\n                            in/hr     in/hr %9s", FlowUnitWords[sp->FlowUnits]);
    else fprintf(sp->Frpt.file,
    "\n                            mm/hr     mm/hr %9s", FlowUnitWords[sp->FlowUnits]);
    if ( hasSnowmelt )
    {
        if ( sp->UnitSystem == US ) fprintf(sp->Frpt.file, "      inches");
        else                    fprintf(sp->Frpt.file, "     mmeters");
    }
    if ( hasGwater )
    {
        if ( sp->UnitSystem == US )
            fprintf(sp->Frpt.file, "      feet %9s", FlowUnitWords[sp->FlowUnits]);
        else
            fprintf(sp->Frpt.file, "    meters %9s", FlowUnitWords[sp->FlowUnits]);
    }
    if ( hasQuality ) for (i = 0; i < sp->Nobjects[POLLUT]; i++)
        fprintf(sp->Frpt.file, "%10s", QualUnitsWords[sp->Pollut[i].units]);

    // --- print lower border of header
    WRITE(LINE_51);
    if ( hasSnowmelt ) fprintf(sp->Frpt.file, LINE_12);
    if ( hasGwater )
    {
        fprintf(sp->Frpt.file, LINE_10);
        fprintf(sp->Frpt.file, LINE_10);
    }
    if ( hasQuality ) for (i = 0; i < sp->Nobjects[POLLUT]; i++)
        fprintf(sp->Frpt.file, LINE_10);
}

//=============================================================================

void report_Nodes(SWMM_Project *sp)
//
//  Input:   none
//  Output:  none
//  Purpose: writes results for selected nodes to report file.
//
{
    int      j, p, k;
    int      period;
    DateTime days;
    char     theDate[20];
    char     theTime[20];

    TOutputExport *otptx = &sp->OutputExport;

    if ( sp->Nobjects[NODE] == 0 ) return;
    WRITE("");
    WRITE("************");
    WRITE("Node Results");
    WRITE("************");
    k = 0;
    for (j = 0; j < sp->Nobjects[NODE]; j++)
    {
        if ( sp->Node[j].rptFlag == TRUE )
        {
            report_NodeHeader(sp, sp->Node[j].ID);
            for ( period = 1; period <= sp->Nperiods; period++ )
            {
                output_readDateTime(sp, period, &days);
                datetime_dateToStr(sp, days, theDate);
                datetime_timeToStr(days, theTime);
                output_readNodeResults(sp, period, k);
                fprintf(sp->Frpt.file, "\n  %11s %8s  %9.3f %9.3f %9.3f %9.3f",
                    theDate, theTime, otptx->NodeResults[NODE_INFLOW],
                    otptx->NodeResults[NODE_OVERFLOW], otptx->NodeResults[NODE_DEPTH],
                    otptx->NodeResults[NODE_HEAD]);
                if ( !sp->IgnoreQuality ) for (p = 0; p < sp->Nobjects[POLLUT]; p++)
                    fprintf(sp->Frpt.file, " %9.3f", otptx->NodeResults[NODE_QUAL + p]);
            }
            WRITE("");
            k++;
        }
    }
}

//=============================================================================

void  report_NodeHeader(SWMM_Project *sp, char *id)
//
//  Input:   id = node ID name
//  Output:  none
//  Purpose: writes table headings for node results to report file.
//
{
    int i;
    char lengthUnits[9];

    WRITE("");
    fprintf(sp->Frpt.file,"\n  <<< Node %s >>>", id);
    WRITE(LINE_64);
    for (i = 0; i < sp->Nobjects[POLLUT]; i++) fprintf(sp->Frpt.file, LINE_10);

    fprintf(sp->Frpt.file,
    "\n                           Inflow  Flooding     Depth      Head");
    if ( !sp->IgnoreQuality ) for (i = 0; i < sp->Nobjects[POLLUT]; i++)
        fprintf(sp->Frpt.file, "%10s", sp->Pollut[i].ID);
    if ( sp->UnitSystem == US) strcpy(lengthUnits, "feet");
    else strcpy(lengthUnits, "meters");
    fprintf(sp->Frpt.file,
    "\n  Date        Time      %9s %9s %9s %9s",
        FlowUnitWords[sp->FlowUnits], FlowUnitWords[sp->FlowUnits],
        lengthUnits, lengthUnits);
    if ( !sp->IgnoreQuality ) for (i = 0; i < sp->Nobjects[POLLUT]; i++)
        fprintf(sp->Frpt.file, "%10s", QualUnitsWords[sp->Pollut[i].units]);

    WRITE(LINE_64);
    if ( !sp->IgnoreQuality )
        for (i = 0; i < sp->Nobjects[POLLUT]; i++) fprintf(sp->Frpt.file, LINE_10);
}

//=============================================================================

void report_Links(SWMM_Project *sp)
//
//  Input:   none
//  Output:  none
//  Purpose: writes results for selected links to report file.
//
{
    int      j, p, k;
    int      period;
    DateTime days;
    char     theDate[12];
    char     theTime[9];

    TOutputExport *otptx = &sp->OutputExport;

    if ( sp->Nobjects[LINK] == 0 ) return;
    WRITE("");
    WRITE("************");
    WRITE("Link Results");
    WRITE("************");
    k = 0;
    for (j = 0; j < sp->Nobjects[LINK]; j++)
    {
        if ( sp->Link[j].rptFlag == TRUE )
        {
            report_LinkHeader(sp, sp->Link[j].ID);
            for ( period = 1; period <= sp->Nperiods; period++ )
            {
                output_readDateTime(sp, period, &days);
                datetime_dateToStr(sp, days, theDate);
                datetime_timeToStr(days, theTime);
                output_readLinkResults(sp, period, k);
                fprintf(sp->Frpt.file, "\n  %11s %8s  %9.3f %9.3f %9.3f %9.3f",
                    theDate, theTime, otptx->LinkResults[LINK_FLOW],
                    otptx->LinkResults[LINK_VELOCITY], otptx->LinkResults[LINK_DEPTH],
                    otptx->LinkResults[LINK_CAPACITY]);
                if ( !sp->IgnoreQuality ) for (p = 0; p < sp->Nobjects[POLLUT]; p++)
                    fprintf(sp->Frpt.file, " %9.3f", otptx->LinkResults[LINK_QUAL + p]);
            }
            WRITE("");
            k++;
        }
    }
}

//=============================================================================

void  report_LinkHeader(SWMM_Project *sp, char *id)
//
//  Input:   id = link ID name
//  Output:  none
//  Purpose: writes table headings for link results to report file.
//
{
    int i;

    WRITE("");
    fprintf(sp->Frpt.file,"\n  <<< Link %s >>>", id);
    WRITE(LINE_64);
    for (i = 0; i < sp->Nobjects[POLLUT]; i++) fprintf(sp->Frpt.file, LINE_10);

    fprintf(sp->Frpt.file,
    "\n                             Flow  Velocity     Depth  Capacity/");
    if ( !sp->IgnoreQuality ) for (i = 0; i < sp->Nobjects[POLLUT]; i++)
        fprintf(sp->Frpt.file, "%10s", sp->Pollut[i].ID);

    if ( sp->UnitSystem == US )
        fprintf(sp->Frpt.file,
        "\n  Date        Time     %10s    ft/sec      feet   Setting ",
        FlowUnitWords[sp->FlowUnits]);
    else
        fprintf(sp->Frpt.file,
        "\n  Date        Time     %10s     m/sec    meters   Setting ",
        FlowUnitWords[sp->FlowUnits]);
    if ( !sp->IgnoreQuality ) for (i = 0; i < sp->Nobjects[POLLUT]; i++)
        fprintf(sp->Frpt.file, " %9s", QualUnitsWords[sp->Pollut[i].units]);

    WRITE(LINE_64);
    if ( !sp->IgnoreQuality )
        for (i = 0; i < sp->Nobjects[POLLUT]; i++) fprintf(sp->Frpt.file, LINE_10);
}


//=============================================================================
//      ERROR REPORTING
//=============================================================================

void report_writeErrorMsg(SWMM_Project *sp, int code, char* s)
//
//  Input:   code = error code
//           s = error message text
//  Output:  none
//  Purpose: writes error message to report file.
//
{
    if ( sp->Frpt.file )
    {
        WRITE("");
        fprintf(sp->Frpt.file, error_getMsg(code), s);
    }
    sp->ErrorCode = code;

////  Following code segment added to release 5.1.011.  ////                   //(5.1.011)
    // --- save message to ErrorMsg if it's not for a line of input data
    if ( sp->ErrorCode <= ERR_INPUT || sp->ErrorCode >= ERR_FILE_NAME )
    {                                                
        sprintf(sp->ErrorMsg, error_getMsg(sp->ErrorCode), s);
    }
////
}

//=============================================================================

void report_writeErrorCode(SWMM_Project *sp)
//
//  Input:   none
//  Output:  none
//  Purpose: writes error message to report file.
//
{
    if ( sp->Frpt.file )
    {
        if ( (sp->ErrorCode >= ERR_MEMORY && sp->ErrorCode <= ERR_TIMESTEP)
        ||   (sp->ErrorCode >= ERR_FILE_NAME && sp->ErrorCode <= ERR_OUT_FILE)
        ||   (sp->ErrorCode == ERR_SYSTEM) )
            fprintf(sp->Frpt.file, error_getMsg(sp->ErrorCode));
    }
}

//=============================================================================

void report_writeInputErrorMsg(SWMM_Project *sp, int k, int sect, char* line,
        long lineCount)
//
//  Input:   k = error code
//           sect = number of input data section where error occurred
//           line = line of data containing the error
//           lineCount = line number of data file containing the error
//  Output:  none
//  Purpose: writes input error message to report file.
//
{
    if ( sp->Frpt.file )
    {
        report_writeErrorMsg(sp, k, sp->ErrString);
        if ( sect < 0 ) fprintf(sp->Frpt.file, FMT17, lineCount);
        else            fprintf(sp->Frpt.file, FMT18, lineCount, SectWords[sect]);
        fprintf(sp->Frpt.file, "\n  %s", line);
    }
}

//=============================================================================

void report_writeWarningMsg(SWMM_Project *sp, char* msg, char* id)
//
//  Input:   msg = text of warning message
//           id = ID name of object that message refers to
//  Output:  none
//  Purpose: writes a warning message to the report file.
//
{
    fprintf(sp->Frpt.file, "\n  %s %s", msg, id);
    sp->Warnings++;                                                                //(5.1.011)
}

//=============================================================================

void report_writeTseriesErrorMsg(SWMM_Project *sp, int code, TTable *tseries)
//
//  Input:   tseries = pointer to a time series
//  Output:  none
//  Purpose: writes the date where a time series' data is out of order.
//
{
    char     theDate[20];
    char     theTime[20];
    DateTime x;

    if (code == ERR_CURVE_SEQUENCE)
    {
        x = tseries->x2;
        datetime_dateToStr(sp, x, theDate);
        datetime_timeToStr(x, theTime);
        report_writeErrorMsg(sp, ERR_TIMESERIES_SEQUENCE, tseries->ID);
        fprintf(sp->Frpt.file, " at %s %s.", theDate, theTime);
    }
    else report_writeErrorMsg(sp, code, tseries->ID);
}