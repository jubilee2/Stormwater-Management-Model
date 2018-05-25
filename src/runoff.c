//-----------------------------------------------------------------------------
//   runoff.c
//
//   Project:  EPA SWMM5
//   Version:  5.1
//   Date:     03/20/14   (Build 5.1.001)
//             09/15/14   (Build 5.1.007)
//             03/19/15   (Build 5.1.008)
//             08/01/16   (Build 5.1.011)
//             03/14/17   (Build 5.1.012)
//   Author:   L. Rossman
//             M. Tryby
//
//   Runoff analysis functions.
//
//   Build 5.1.007:
//   - Climate file now opened in climate.c module.
//
//   Build 5.1.008:
//   - Memory for runoff pollutant load now allocated and freed in this module.
//   - Runoff time step chosen so that simulation does not exceed total duration.
//   - State of LIDs considered when choosing wet or dry time step.
//   - More checks added to skip over subcatchments with zero area.
//   - Support added for sending outfall node discharge onto a subcatchment.
//
//   Build 5.1.011:
//   - Runoff wet time step kept aligned with reporting times.
//   - Prior runoff time step used to convert returned outfall volume to flow.
//
//   Build 5.1.012:
//   - Runoff wet time step no longer kept aligned with reporting times.
//-----------------------------------------------------------------------------
#define _CRT_SECURE_NO_DEPRECATE

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "headers.h"
#include "odesolve.h"

//-----------------------------------------------------------------------------
//  External functions (declared in funcs.h)
//-----------------------------------------------------------------------------
// runoff_open     (called from swmm_start in swmm5.c)
// runoff_execute  (called from swmm_step in swmm5.c)
// runoff_close    (called from swmm_end in swmm5.c)

//-----------------------------------------------------------------------------
// Local functions
//-----------------------------------------------------------------------------
static double runoff_getTimeStep(SWMM_Project *sp, DateTime currentDate);
static void   runoff_initFile(SWMM_Project *sp);
static void   runoff_readFromFile(SWMM_Project *sp);
static void   runoff_saveToFile(SWMM_Project *sp, float tStep);
static void   runoff_getOutfallRunon(SWMM_Project *sp, double tStep);                            //(5.1.008)

//=============================================================================

int runoff_open(SWMM_Project *sp)
//
//  Input:   none
//  Output:  returns the global error code
//  Purpose: opens the runoff analyzer.
//
{
    TRunoffShared *rnff = &sp->RunoffShared;

    rnff->IsRaining = FALSE;
    rnff->HasRunoff = FALSE;
    rnff->HasSnow = FALSE;
    rnff->Nsteps = 0;

    // --- open the Ordinary Differential Equation solver
    if ( !odesolve_open(sp, MAXODES) ) report_writeErrorMsg(sp, ERR_ODE_SOLVER, "");

    // --- allocate memory for pollutant runoff loads                          //(5.1.008)
    rnff->OutflowLoad = NULL;
    if ( sp->Nobjects[POLLUT] > 0 )
    {
        rnff->OutflowLoad = (double *) calloc(sp->Nobjects[POLLUT], sizeof(double));
        if ( !rnff->OutflowLoad ) report_writeErrorMsg(sp, ERR_MEMORY, "");
    }

    // --- see if a runoff interface file should be opened
    switch ( sp->Frunoff.mode )
    {
      case USE_FILE:
        if ( (sp->Frunoff.file = fopen(sp->Frunoff.name, "r+b")) == NULL)
            report_writeErrorMsg(sp, ERR_RUNOFF_FILE_OPEN, sp->Frunoff.name);
        else runoff_initFile(sp);
        break;
      case SAVE_FILE:
        if ( (sp->Frunoff.file = fopen(sp->Frunoff.name, "w+b")) == NULL)
            report_writeErrorMsg(sp, ERR_RUNOFF_FILE_OPEN, sp->Frunoff.name);
        else runoff_initFile(sp);
        break;
    }

////  Call to climate_openFile() moved to climate_validate().  ////            //(5.1.007)
    return sp->ErrorCode;
}

//=============================================================================

void runoff_close(SWMM_Project *sp)
//
//  Input:   none
//  Output:  none
//  Purpose: closes the runoff analyzer.
//
{
    TRunoffShared *rnff = &sp->RunoffShared;

    // --- close the ODE solver
    odesolve_close(sp);

    // --- free memory for pollutant runoff loads                              //(5.1.008)
    FREE(rnff->OutflowLoad);

    // --- close runoff interface file if in use
    if ( sp->Frunoff.file )
    {
        // --- write to file number of time steps simulated
        if ( sp->Frunoff.mode == SAVE_FILE )
        {
            fseek(sp->Frunoff.file, rnff->MaxStepsPos, SEEK_SET);
            fwrite(&rnff->Nsteps, sizeof(int), 1, sp->Frunoff.file);
        }
        fclose(sp->Frunoff.file);
    }

    // --- close climate file if in use
    if ( sp->Fclimate.file ) fclose(sp->Fclimate.file);
}

//=============================================================================

void runoff_execute(SWMM_Project *sp)
//
//  Input:   none
//  Output:  none
//  Purpose: computes runoff from each subcatchment at current runoff time.
//
{
    int      j;                        // object index
    int      day;                      // day of calendar year
    double   runoffStep;               // runoff time step (sec)
    double   oldRunoffStep;            // previous runoff time step (sec)      //(5.1.011)
    double   runoff;                   // subcatchment runoff (ft/sec)
    DateTime currentDate;              // current date/time 
    char     canSweep;                 // TRUE if street sweeping can occur

    TRunoffShared *rnff = &sp->RunoffShared;

    if ( sp->ErrorCode ) return;

    // --- find previous runoff time step in sec                               //(5.1.011)
    oldRunoffStep = (sp->NewRunoffTime - sp->OldRunoffTime) / 1000.0;                  //(5.1.011)

    // --- convert elapsed runoff time in milliseconds to a calendar date
    currentDate = getDateTime(sp, sp->NewRunoffTime);

    // --- update climatological conditions
    climate_setState(sp, currentDate);

    // --- if no subcatchments then simply update runoff elapsed time
    if ( sp->Nobjects[SUBCATCH] == 0 )
    {
        sp->OldRunoffTime = sp->NewRunoffTime;
        sp->NewRunoffTime += (double)(1000 * sp->DryStep);
        sp->NewRunoffTime = MIN(sp->NewRunoffTime, sp->TotalDuration);                     //(5.1.008)
        return;
    }

    // --- update current rainfall at each raingage
    //     NOTE: must examine gages in sequential order due to possible
    //     presence of co-gages (gages that share same rain time series).
    rnff->IsRaining = FALSE;
    for (j = 0; j < sp->Nobjects[GAGE]; j++)
    {
        gage_setState(sp, j, currentDate);
        if ( sp->Gage[j].rainfall > 0.0 ) rnff->IsRaining = TRUE;
    }

    // --- read runoff results from interface file if applicable
    if ( sp->Frunoff.mode == USE_FILE )
    {
        runoff_readFromFile(sp);
        return;
    }

    // --- see if street sweeping can occur on current date
    day = datetime_dayOfYear(currentDate);
    if ( day >= sp->ReportStep && day <= sp->SweepEnd ) canSweep = TRUE;
    else canSweep = FALSE;

    // --- get runoff time step (in seconds)
    runoffStep = runoff_getTimeStep(sp, currentDate);
    if ( runoffStep <= 0.0 )
    {
        sp->ErrorCode = ERR_TIMESTEP;
        return;
    }

    // --- update runoff time clock (in milliseconds)
    sp->OldRunoffTime = sp->NewRunoffTime;
    sp->NewRunoffTime += (double)(1000 * runoffStep);

////  Following code segment added to release 5.1.008.  ////                   //(5.1.008)
////
    // --- adjust runoff step so that total duration not exceeded
    if ( sp->NewRunoffTime > sp->TotalDuration )
    {
        runoffStep = (sp->TotalDuration - sp->OldRunoffTime) / 1000.0;
        sp->NewRunoffTime = sp->TotalDuration;
    }
////

    // --- update old state of each subcatchment, 
    for (j = 0; j < sp->Nobjects[SUBCATCH]; j++) subcatch_setOldState(sp, j);

    // --- determine any runon from drainage system outfall nodes              //(5.1.008)
    if ( oldRunoffStep > 0.0 ) runoff_getOutfallRunon(sp, oldRunoffStep);          //(5.1.011)

    // --- determine runon from upstream subcatchments, and implement snow removal
    for (j = 0; j < sp->Nobjects[SUBCATCH]; j++)
    {
        if ( sp->Subcatch[j].area == 0.0 ) continue;                               //(5.1.008)
        subcatch_getRunon(sp, j);
        if ( !sp->IgnoreSnowmelt ) snow_plowSnow(sp, j, runoffStep);
    }
    
    // --- determine runoff and pollutant buildup/washoff in each subcatchment
    rnff->HasSnow = FALSE;
    rnff->HasRunoff = FALSE;
    rnff->HasWetLids = FALSE;                                                        //(5.1.008)
    for (j = 0; j < sp->Nobjects[SUBCATCH]; j++)
    {
        // --- find total runoff rate (in ft/sec) over the subcatchment
        //     (the amount that actually leaves the subcatchment (in cfs)
        //     is also computed and is stored in sp->Subcatch[j].newRunoff)
        if ( sp->Subcatch[j].area == 0.0 ) continue;                               //(5.1.008)
        runoff = subcatch_getRunoff(sp, j, runoffStep);

        // --- update state of study area surfaces
        if ( runoff > 0.0 ) rnff->HasRunoff = TRUE;
        if ( sp->Subcatch[j].newSnowDepth > 0.0 ) rnff->HasSnow = TRUE;

        // --- skip pollutant buildup/washoff if quality ignored
        if ( sp->IgnoreQuality ) continue;

        // --- add to pollutant buildup if runoff is negligible
        if ( runoff < MIN_RUNOFF ) surfqual_getBuildup(sp, j, runoffStep);

        // --- reduce buildup by street sweeping
        if ( canSweep && sp->Subcatch[j].rainfall <= MIN_RUNOFF)
            surfqual_sweepBuildup(sp, j, currentDate);

        // --- compute pollutant washoff 
        surfqual_getWashoff(sp, j, runoff, runoffStep);
    }

    // --- update tracking of system-wide max. runoff rate
    stats_updateMaxRunoff(sp);

    // --- save runoff results to interface file if one is used
    rnff->Nsteps++;
    if ( sp->Frunoff.mode == SAVE_FILE )
    {
        runoff_saveToFile(sp, (float)runoffStep);
    }

    // --- reset subcatchment runon to 0
    for (j = 0; j < sp->Nobjects[SUBCATCH]; j++) sp->Subcatch[j].runon = 0.0;
}

//=============================================================================

double runoff_getTimeStep(SWMM_Project *sp, DateTime currentDate)
//
//  Input:   currentDate = current simulation date/time
//  Output:  time step (sec)
//  Purpose: computes a time step to use for runoff calculations.
//
{
    int  j;
    long timeStep;
    long maxStep = sp->DryStep;

    TRunoffShared *rnff = &sp->RunoffShared;

    // --- find shortest time until next evaporation or rainfall value
    //     (this represents the maximum possible time step)
    timeStep = datetime_timeDiff(climate_getNextEvapDate(sp), currentDate);      //(5.1.008)
    if ( timeStep > 0.0 && timeStep < maxStep ) maxStep = timeStep;            //(5.1.008)
    for (j = 0; j < sp->Nobjects[GAGE]; j++)
    {
        timeStep = datetime_timeDiff(gage_getNextRainDate(sp, j, currentDate),
                   currentDate);
        if ( timeStep > 0 && timeStep < maxStep ) maxStep = timeStep;
    }

////  Following code segment modified for release 5.1.012.  ////               //(5.1.012)
    // --- determine whether wet or dry time step applies
    if ( rnff->IsRaining || rnff->HasSnow || rnff->HasRunoff || rnff->HasWetLids )
    {
        timeStep = sp->WetStep;
    }
////
    else timeStep = sp->DryStep;

    // --- limit time step if necessary
    if ( timeStep > maxStep ) timeStep = maxStep;
    return (double)timeStep;
}

//=============================================================================

void runoff_initFile(SWMM_Project *sp)
//
//  Input:   none
//  Output:  none
//  Purpose: initializes a Runoff Interface file for saving results.
//
{
    int   nSubcatch;
    int   nPollut;
    int   flowUnits;
    char  fileStamp[] = "SWMM5-RUNOFF";
    char  fStamp[] = "SWMM5-RUNOFF";

    TRunoffShared *rnff = &sp->RunoffShared;

    rnff->MaxSteps = 0;
    if ( sp->Frunoff.mode == SAVE_FILE )
    {
        // --- write file stamp, # subcatchments & # pollutants to file
        nSubcatch = sp->Nobjects[SUBCATCH];
        nPollut = sp->Nobjects[POLLUT];
        flowUnits = sp->FlowUnits;
        fwrite(fileStamp, sizeof(char), strlen(fileStamp), sp->Frunoff.file);
        fwrite(&nSubcatch, sizeof(int), 1, sp->Frunoff.file);
        fwrite(&nPollut, sizeof(int), 1, sp->Frunoff.file);
        fwrite(&flowUnits, sizeof(int), 1, sp->Frunoff.file);
        rnff->MaxStepsPos = ftell(sp->Frunoff.file);
        fwrite(&rnff->MaxSteps, sizeof(int), 1, sp->Frunoff.file);
    }

    if ( sp->Frunoff.mode == USE_FILE )
    {
        // --- check that interface file contains proper header records
        fread(fStamp, sizeof(char), strlen(fileStamp), sp->Frunoff.file);
        if ( strcmp(fStamp, fileStamp) != 0 )
        {
            report_writeErrorMsg(sp, ERR_RUNOFF_FILE_FORMAT, "");
            return;
        }
        nSubcatch = -1;
        nPollut = -1;
        flowUnits = -1;
        fread(&nSubcatch, sizeof(int), 1, sp->Frunoff.file);
        fread(&nPollut, sizeof(int), 1, sp->Frunoff.file);
        fread(&flowUnits, sizeof(int), 1, sp->Frunoff.file);
        fread(&rnff->MaxSteps, sizeof(int), 1, sp->Frunoff.file);
        if ( nSubcatch != sp->Nobjects[SUBCATCH]
        ||   nPollut   != sp->Nobjects[POLLUT]
        ||   flowUnits != sp->FlowUnits
        ||   rnff->MaxSteps  <= 0 )
        {
             report_writeErrorMsg(sp, ERR_RUNOFF_FILE_FORMAT, "");
        }
    }
}

//=============================================================================

void  runoff_saveToFile(SWMM_Project *sp, float tStep)
//
//  Input:   tStep = runoff time step (sec)
//  Output:  none
//  Purpose: saves current runoff results to Runoff Interface file.
//
{
    int j;

    TOutputExport *otptx = &sp->OutputExport;

    int n = MAX_SUBCATCH_RESULTS + sp->Nobjects[POLLUT] - 1;
    

    fwrite(&tStep, sizeof(float), 1, sp->Frunoff.file);
    for (j=0; j<sp->Nobjects[SUBCATCH]; j++)
    {
        subcatch_getResults(sp, j, 1.0, otptx->SubcatchResults);
        fwrite(otptx->SubcatchResults, sizeof(float), n, sp->Frunoff.file);
    }
}

//=============================================================================

void  runoff_readFromFile(SWMM_Project *sp)
//
//  Input:   none
//  Output:  none
//  Purpose: reads runoff results from Runoff Interface file for current time.
//
{
    int    i, j;
    int    nResults;                   // number of results per subcatch.
    int    kount;                      // count of items read from file
    float  tStep;                      // runoff time step (sec)
    TGroundwater* gw;                  // ptr. to Groundwater object

    TRunoffShared *rnff = &sp->RunoffShared;
    TOutputExport *otptx = &sp->OutputExport;

    // --- make sure not past end of file
    if ( rnff->Nsteps > rnff->MaxSteps )
    {
         report_writeErrorMsg(sp, ERR_RUNOFF_FILE_END, "");
         return;
    }

    // --- replace old state with current one for all subcatchments
    for (j = 0; j < sp->Nobjects[SUBCATCH]; j++) subcatch_setOldState(sp, j);

    // --- read runoff time step
    kount = 0;
    kount += fread(&tStep, sizeof(float), 1, sp->Frunoff.file);

    // --- compute number of results saved for each subcatchment
    nResults = MAX_SUBCATCH_RESULTS + sp->Nobjects[POLLUT] - 1;

    // --- for each subcatchment
    for (j = 0; j < sp->Nobjects[SUBCATCH]; j++)
    {
        // --- read vector of saved results
        kount += fread(otptx->SubcatchResults, sizeof(float), nResults, sp->Frunoff.file);

        // --- extract hydrologic results, converting units where necessary
        //     (results were saved to file in user's units)
        sp->Subcatch[j].newSnowDepth = otptx->SubcatchResults[SUBCATCH_SNOWDEPTH] /
                                   UCF(sp, RAINDEPTH);
        sp->Subcatch[j].evapLoss     = otptx->SubcatchResults[SUBCATCH_EVAP] /
                                   UCF(sp, RAINFALL);
        sp->Subcatch[j].infilLoss    = otptx->SubcatchResults[SUBCATCH_INFIL] /
                                   UCF(sp, RAINFALL);
        sp->Subcatch[j].newRunoff    = otptx->SubcatchResults[SUBCATCH_RUNOFF] /
                                   UCF(sp, FLOW);
        gw = sp->Subcatch[j].groundwater;
        if ( gw )
        {
            gw->newFlow    = otptx->SubcatchResults[SUBCATCH_GW_FLOW] / UCF(sp, FLOW);
            gw->lowerDepth = sp->Aquifer[gw->aquifer].bottomElev -
                             (otptx->SubcatchResults[SUBCATCH_GW_ELEV] / UCF(sp, LENGTH));
            gw->theta      = otptx->SubcatchResults[SUBCATCH_SOIL_MOIST];
        }

        // --- extract water quality results
        for (i = 0; i < sp->Nobjects[POLLUT]; i++)
        {
            sp->Subcatch[j].newQual[i] = otptx->SubcatchResults[SUBCATCH_WASHOFF + i];
        }
    }

    // --- report error if not enough values were read
    if ( kount < 1 + sp->Nobjects[SUBCATCH] * nResults )
    {
         report_writeErrorMsg(sp, ERR_RUNOFF_FILE_READ, "");
         return;
    }

    // --- update runoff time clock
    sp->OldRunoffTime = sp->NewRunoffTime;
    sp->NewRunoffTime = sp->OldRunoffTime + (double)(tStep)*1000.0;
    sp->NewRunoffTime = MIN(sp->NewRunoffTime, sp->TotalDuration);                         //(5.1.008)
    rnff->Nsteps++;
}

//=============================================================================

////  New function added for release 5.1.008.  ////                            //(5.1.008)

void runoff_getOutfallRunon(SWMM_Project *sp, double tStep)
//
//  Input:   tStep = previous runoff time step (sec)                           //(5.1.011)
//  Output:  none
//  Purpose: adds flow and pollutant loads leaving drainage system outfalls    //(5.1.011)
//           during the previous runoff time step to designated subcatchments. //(5.1.011)
//
{
    int i, k, p;
    double w;

    // --- examine each outfall node
    for (i = 0; i < sp->Nnodes[OUTFALL]; i++)
    {
        // --- ignore node if outflow not re-routed onto a subcatchment
        k = sp->Outfall[i].routeTo;
        if ( k < 0 ) continue;
        if ( sp->Subcatch[k].area == 0.0 ) continue;

        // --- add outfall's flow to subcatchment as runon and re-set routed
        //     flow volume to 0
        subcatch_addRunonFlow(sp, k, sp->Outfall[i].vRouted/tStep);
        massbal_updateRunoffTotals(sp, RUNOFF_RUNON, sp->Outfall[i].vRouted);
        sp->Outfall[i].vRouted = 0.0;

        // --- add outfall's pollutant load on to subcatchment's wet
        //     deposition load and re-set routed load to 0
        //     (Subcatch.newQual is being used as a temporary load accumulator)
        for (p = 0; p < sp->Nobjects[POLLUT]; p++)
        {
            w = sp->Outfall[i].wRouted[p] * LperFT3;
            massbal_updateLoadingTotals(sp, DEPOSITION_LOAD, p, w * sp->Pollut[p].mcf);
            sp->Subcatch[k].newQual[p] += w / tStep;
            sp->Outfall[i].wRouted[p] = 0.0;
        }
    }
}